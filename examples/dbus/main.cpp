#include "dbus-message.h"

#include <iostream>
#include <string>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

// ============================================================
// Логирование
// ============================================================

static void log_line(const std::string& s) {
    std::cout << s << "\n";
    std::cout.flush();
}

static const char* msg_type_name(dbus::MessageType t) {
    switch (t) {
        case dbus::MessageType::MethodCall:   return "METHOD_CALL";
        case dbus::MessageType::MethodReturn: return "METHOD_RETURN";
        case dbus::MessageType::Error:        return "ERROR";
        case dbus::MessageType::Signal:       return "SIGNAL";
    }
    return "UNKNOWN";
}

static void hex_dump(const std::string& label,
                     const uint8_t* data, size_t size) {
    std::printf("%s (%zu bytes):\n", label.c_str(), size);
    for (size_t i = 0; i < size; i++) {
        std::printf("%02x ", data[i]);
        if ((i + 1) % 16 == 0) std::printf("\n");
    }
    if (size % 16 != 0) std::printf("\n");
    std::printf("\n");
    std::fflush(stdout);
}

// ============================================================
// Утилиты
// ============================================================

static std::string uid_to_hex(uid_t uid) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%u", uid);
    std::string s = buf;
    std::string hex;
    for (char c : s) {
        char h[3];
        std::snprintf(h, sizeof(h), "%02x", static_cast<unsigned char>(c));
        hex += h;
    }
    return hex;
}

static std::string read_line(int fd) {
    std::string line;
    char c;
    while (true) {
        ssize_t n = read(fd, &c, 1);
        if (n <= 0) {
            throw dbus::DbusError("read_line: connection closed");
        }
        if (c == '\n') break;
        if (c != '\r') line += c;
    }
    return line;
}

static void read_exact(int fd, uint8_t* buf, size_t n, const char* stage) {
    size_t got = 0;
    while (got < n) {
        ssize_t r = read(fd, buf + got, n - got);
        if (r == 0) {
            std::printf("[read_exact:%s] EOF after %zu/%zu bytes\n",
                        stage, got, n);
            std::fflush(stdout);
            throw dbus::DbusError("read_exact: connection closed");
        }
        if (r < 0) {
            std::printf("[read_exact:%s] errno=%d (%s)\n",
                        stage, errno, std::strerror(errno));
            std::fflush(stdout);
            throw dbus::DbusError("read_exact: read error");
        }
        got += static_cast<size_t>(r);
    }
}

// ============================================================
// Чтение одного D-Bus сообщения целиком
// ============================================================

static dbus::Message read_message(int fd) {
    log_line("[read_message] reading 16-byte fixed header...");

    uint8_t header[16];
    read_exact(fd, header, 16, "fixed-header");

    std::printf("[read_message] header: ");
    for (int i = 0; i < 16; i++) std::printf("%02x ", header[i]);
    std::printf("\n");
    std::fflush(stdout);

    dbus::Endianness endian = static_cast<dbus::Endianness>(header[0]);
    if (endian != dbus::Endianness::Little && endian != dbus::Endianness::Big) {
        throw dbus::DbusError("invalid endianness in header");
    }

    auto bswap32 = [](uint32_t v) {
        return ((v & 0xFF000000u) >> 24) | ((v & 0x00FF0000u) >> 8)
             | ((v & 0x0000FF00u) << 8)  | ((v & 0x000000FFu) << 24);
    };

    uint32_t body_len;
    std::memcpy(&body_len, header + 4, 4);
    if (endian == dbus::Endianness::Big) body_len = bswap32(body_len);

    uint32_t serial;
    std::memcpy(&serial, header + 8, 4);
    if (endian == dbus::Endianness::Big) serial = bswap32(serial);

    // ВОТ ЗДЕСЬ: длина массива полей уже в заголовке, байты 12-15
    uint32_t fields_len;
    std::memcpy(&fields_len, header + 12, 4);
    if (endian == dbus::Endianness::Big) fields_len = bswap32(fields_len);

    std::printf("[read_message] fields_len=%u\n", fields_len);

    // Сразу читаем содержимое массива полей — оно начинается с байта 16.
    std::vector<uint8_t> fields(fields_len);
    if (fields_len > 0) {
        read_exact(fd, fields.data(), fields_len, "fields-body");
    }

    // Далее — padding до 8 байт перед телом.
    // Общая длина "заголовочной части" = 16 (fixed) + fields_len.
    size_t header_total = 16 + fields_len;
    size_t padding = (8 - (header_total % 8)) % 8;

    std::printf("[read_message] header_total=%zu padding=%zu\n",
                header_total, padding);
    std::fflush(stdout);

    std::vector<uint8_t> pad(padding);
    if (padding > 0) {
        read_exact(fd, pad.data(), padding, "padding");
    }

    // Тело
    std::vector<uint8_t> body(body_len);
    if (body_len > 0) {
        read_exact(fd, body.data(), body_len, "body");
    }

    // Собираем всё в один буфер и парсим
    std::vector<uint8_t> full;
    full.reserve(16 + fields_len + padding + body_len);
    full.insert(full.end(), header, header + 16);
    full.insert(full.end(), fields.begin(), fields.end());
    full.insert(full.end(), pad.begin(), pad.end());
    full.insert(full.end(), body.begin(), body.end());

    dbus::Message m = dbus::Message::parse(full.data(), full.size());

    std::printf("[read_message] parsed: type=%s",
                msg_type_name(m.type));
    if (m.reply_serial) std::printf(" reply_serial=%u", *m.reply_serial);
    if (m.serial)       std::printf(" serial=%u", m.serial);
    if (m.member)       std::printf(" member=%s", m.member->c_str());
    if (m.interface)    std::printf(" interface=%s", m.interface->c_str());
    if (m.path)         std::printf(" path=%s", m.path->c_str());
    if (m.signature)    std::printf(" signature=%s", m.signature->c_str());
    std::printf("\n");
    std::fflush(stdout);

    return m;
}

// ============================================================
// main
// ============================================================

int main() {
    // 1. Путь к сокету сессионной шины
    const char* addr_env = std::getenv("DBUS_SESSION_BUS_ADDRESS");
    std::string socket_path;

    if (addr_env) {
        std::string addr = addr_env;
        auto pos = addr.find("path=");
        if (pos != std::string::npos) {
            socket_path = addr.substr(pos + 5);
            auto end = socket_path.find(',');
            if (end != std::string::npos) socket_path = socket_path.substr(0, end);
        }
    }

    if (socket_path.empty()) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "/run/user/%u/bus", getuid());
        socket_path = buf;
    }

    log_line("Connecting to: " + socket_path);

    // 2. Подключение
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock == -1) {
        std::perror("socket");
        return 1;
    }

    struct sockaddr_un addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);

    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        std::perror("connect");
        close(sock);
        return 1;
    }

    try {
        // 3. Обязательный нулевой байт
        log_line("[auth] sending NUL byte");
        char nul = '\0';
        if (write(sock, &nul, 1) != 1) {
            std::perror("write nul");
            close(sock);
            return 1;
        }

        // 4. AUTH EXTERNAL
        std::string auth_cmd = "AUTH EXTERNAL " + uid_to_hex(getuid()) + "\r\n";
        log_line("[auth] >>> " + auth_cmd.substr(0, auth_cmd.size() - 2));
        if (write(sock, auth_cmd.data(), auth_cmd.size()) == -1) {
            std::perror("write auth");
            close(sock);
            return 1;
        }

        // 5. Ответ
        std::string resp = read_line(sock);
        log_line("[auth] <<< " + resp);

        if (resp.rfind("OK", 0) != 0) {
            std::cerr << "Authentication failed: " << resp << "\n";
            close(sock);
            return 1;
        }

        // 6. BEGIN
        log_line("[auth] >>> BEGIN");
        const char* begin_cmd = "BEGIN\r\n";
        if (write(sock, begin_cmd, std::strlen(begin_cmd)) == -1) {
            std::perror("write begin");
            close(sock);
            return 1;
        }

        log_line("--- Authenticated ---");

        // 7. Формируем Hello
        dbus::Message hello;
        hello.endian      = dbus::Endianness::Little;
        hello.type        = dbus::MessageType::MethodCall;
        hello.serial      = 1;
        hello.path        = "/org/freedesktop/DBus";
        hello.interface   = "org.freedesktop.DBus";
        hello.member      = "Hello";
        hello.destination = "org.freedesktop.DBus";

        auto bytes = hello.serialize();
        hex_dump("[send] Hello", bytes.data(), bytes.size());

        if (write(sock, bytes.data(), bytes.size()) == -1) {
            std::perror("write hello");
            close(sock);
            return 1;
        }
        log_line("[send] Hello sent, waiting for reply...");

        // 8. Читаем сообщения в цикле, пока не получим ответ именно
        //    на наш Hello (reply_serial == hello.serial).
        dbus::Message reply;
        bool got_reply = false;
        int iter = 0;

        while (!got_reply) {
            ++iter;
            std::printf("[loop] iteration %d\n", iter);
            std::fflush(stdout);

            reply = read_message(sock);

            if (reply.type == dbus::MessageType::MethodReturn
                && reply.reply_serial
                && *reply.reply_serial == hello.serial)
            {
                log_line("[loop] got METHOD_RETURN for our Hello");
                got_reply = true;
            }
            else if (reply.type == dbus::MessageType::Signal) {
                log_line("[loop] signal received, ignoring and reading next");
            }
            else if (reply.type == dbus::MessageType::Error) {
                log_line("[loop] ERROR reply received");
                got_reply = true;
            }
            else {
                log_line("[loop] unrelated message, ignoring");
            }
        }

        // 9. Разбор ответа
        log_line("--- Reply parsed ---");
        std::printf("type        : %s\n", msg_type_name(reply.type));
        if (reply.reply_serial) std::printf("reply_serial: %u\n", *reply.reply_serial);
        if (reply.sender)       std::printf("sender      : %s\n", reply.sender->c_str());
        if (reply.signature)    std::printf("signature   : %s\n", reply.signature->c_str());
        std::fflush(stdout);

        if (reply.type == dbus::MessageType::MethodReturn) {
            dbus::Reader r(reply.body.data(), reply.body.size(), reply.endian);
            std::string unique_name = r.read_string();
            std::printf(">>> My unique name: %s\n", unique_name.c_str());
        }
        else if (reply.type == dbus::MessageType::Error) {
            if (reply.error_name)
                std::cerr << "  Error name: " << *reply.error_name << "\n";
            if (!reply.body.empty()) {
                try {
                    dbus::Reader r(reply.body.data(), reply.body.size(), reply.endian);
                    std::string err_msg = r.read_string();
                    std::cerr << "  Message: " << err_msg << "\n";
                } catch (...) {}
            }
        }

    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << "\n";
        close(sock);
        return 1;
    }

    close(sock);
    return 0;
}