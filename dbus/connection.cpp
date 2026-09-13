/**
 * SPDX-FileCopyrightText: 2026 Mikhail Firsov <firsov62121@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "firmir/dbus/connection_p.h"
#include "firmir/dbus/pending-reply_p.h"
#include "firmir/dbus/message_p.h"
#include "firmir/logging.h"
#include "firmir/event-loop_p.h"

#include <cstring>

#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

namespace Firmir {
namespace DBus {

namespace {

std::string uidToHex(uid_t uid) {
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

} // anonymous namespace

ConnectionPrivate::ConnectionPrivate(Connection *parent, Type type)
    : parent(parent)
    , type(type)
{
    startConnect();
}

ConnectionPrivate::~ConnectionPrivate()
{
    if (sock >= 0) {
        EventLoopPrivate::get(EventLoop::instance()).removeEvent(sock);
        close(sock);
        sock = -1;
    }
}

void ConnectionPrivate::startConnect()
{
    auto path = getPath();
    Debug() << __func__ << path;

    auto fail = [this, &path] (const std::string &err) {
        Error() << "startConnect to path" << path << "error:" << err;
        updateState(State::Error);
    };

    sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock == -1) {
        fail("create socket");
        return;
    }

    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);

    sockaddr_un addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        fail("connect to socket");
        return;
    }

    EventLoopPrivate::get(EventLoop::instance()).addEvent(EventLoopPrivate::EventInfo {
        .fd = sock,
        .events = EPOLLIN,
        .onRead = [this] () {
            readData();
        },
        .onWrite = nullptr,
    });

    Debug() << __func__ << "[auth] sending NUL byte";
    char nul = '\0';
    if (write(sock, &nul, 1) != 1) {
        fail("write nul");
        return;
    }

    updateState(State::Auth);
    std::string authCmd = "AUTH EXTERNAL " + uidToHex(getuid()) + "\r\n";
    Debug() << __func__ << "[auth] >>>" << authCmd.substr(0, authCmd.size() - 2);
    if (write(sock, authCmd.data(), authCmd.size()) == -1) {
        fail("write auth");
        return;
    }
}

std::string ConnectionPrivate::getPath()
{
    auto getDefaultPath = [this] () -> std::string {
        return type == Type::Session
               ? std::string("/run/user/") + std::to_string(getuid()) + "/bus"
               : "/run/dbus/system_bus_socket";
    };

    const char *addrEnv = std::getenv(type == Type::Session ? "DBUS_SESSION_BUS_ADDRESS" : "DBUS_SYSTEM_BUS_ADDRESS");
    if (!addrEnv)
        return getDefaultPath();

    std::string addr = addrEnv;
    auto pos = addr.find("path=");
    if (pos == std::string::npos)
        return getDefaultPath();

    auto path = addr.substr(pos + 5);
    auto end = path.find(',');
    if (end != std::string::npos)
        path = path.substr(0, end);
    return path;
}

void ConnectionPrivate::updateState(State value)
{
    if (state == value)
        return;

    state = value;
    parent->emitEvent(Connection::Event::StateChanged);
}

void ConnectionPrivate::readData()
{
    if (state == State::Auth) {
        doAuth();
        return;
    }
    
}

void ConnectionPrivate::doAuth()
{
    std::string line;
    char c;
    ssize_t n = read(sock, &c, 1);
    if (n <= 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return;

        Error() << __func__ << "Failed to read data";
        updateState(State::Error);
        return;
    }

    if (c != '\n') {
        if (c != '\r')
            authData += c;
        doAuth();
        return;
    }

    if (authData.rfind("OK", 0) != 0) {
        Error() << "Authentication failed: " << authData;
        updateState(State::Error);
        return;
    }

    Debug() << __func__ << "[auth] >>> BEGIN";
    const char *beginCmd = "BEGIN\r\n";
    if (write(sock, beginCmd, std::strlen(beginCmd)) == -1){
        Error() << "Failed to send begin command";
        updateState(State::Error);
        return;
    }

    doGetName();
}

void ConnectionPrivate::doGetName()
{
    updateState(State::GettingName);
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
}

std::shared_ptr<PendingReply> ConnectionPrivate::send(const Message &msg)
{
    if (state != State::GettingName && state != State::Connected) {
        Error() << "Trying to send msg while connection not ready:" << static_cast<int>(state);
        return {};
    }

    auto &pMsg = MessagePrivate::get(msg);
    while (replies.contains(nextSerial))
        ++nextSerial;

    pMsg.serial = nextSerial++;
    auto bytes = pMsg.serialize();
    if (bytes.empty()) {
        Error() << "Failed to serialize message";
        return {};
    }

    if (write(sock, bytes.data(), bytes.size()) == -1) {
        Error() << "Failed to write data to socket";
        return {};
    }

    auto reply = std::make_shared<PendingReply>();
    replies[pMsg.serial] = reply;
    return reply;
}

ConnectionPrivate &ConnectionPrivate::get(const Connection &connection)
{
    return *(connection.pImpl);
}

Connection::Connection(Type type)
    : pImpl(new ConnectionPrivate(this, type))
{
}

Connection::~Connection()
{
    delete pImpl;
}

Connection::State Connection::getState()
{
    return pImpl->state;
}

std::shared_ptr<PendingReply> Connection::send(const Message &msg)
{
    return pImpl->send(msg);
}

} // namespace DBus
} // namespace Firmir