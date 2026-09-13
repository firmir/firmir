#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <stdexcept>
#include <optional>
#include <unordered_map>

namespace dbus {

// ============================================================
// 1. Типы и исключения
// ============================================================

enum class MessageType : uint8_t {
    MethodCall   = 1,
    MethodReturn = 2,
    Error        = 3,
    Signal       = 4,
};

enum class Endianness : uint8_t {
    Little = 'l',
    Big    = 'B',
};

// Коды заголовочных полей
enum class HeaderField : uint8_t {
    Path         = 1,
    Interface    = 2,
    Member       = 3,
    ErrorName    = 4,
    ReplySerial  = 5,
    Destination  = 6,
    Sender       = 7,
    Signature    = 8,
    UnixFDs      = 9,
};

// Теги типов D-Bus
namespace type {
    constexpr char Byte      = 'y';
    constexpr char Boolean   = 'b';
    constexpr char Int16     = 'n';
    constexpr char UInt16    = 'q';
    constexpr char Int32     = 'i';
    constexpr char UInt32    = 'u';
    constexpr char Int64     = 'x';
    constexpr char UInt64    = 't';
    constexpr char Double    = 'd';
    constexpr char String    = 's';
    constexpr char ObjectPath= 'o';
    constexpr char Signature = 'g';
    constexpr char Array     = 'a';
    constexpr char Variant   = 'v';
    constexpr char Struct    = 'r';
    constexpr char DictEntry = 'e';
}

class DbusError : public std::runtime_error {
public:
    explicit DbusError(const std::string& msg)
        : std::runtime_error(msg) {}
};

// ============================================================
// 2. Reader — чтение из буфера с выравниванием
// ============================================================

class Reader {
public:
    Reader(const uint8_t* data, size_t size, Endianness endian)
        : data_(data), size_(size), pos_(0), endian_(endian) {}

    size_t position() const { return pos_; }
    bool   eof()      const { return pos_ >= size_; }

    // Выравнивание: пропускаем байты до границы align
    void align(size_t align) {
        size_t rem = pos_ % align;
        if (rem != 0) pos_ += (align - rem);
        if (pos_ > size_)
            throw DbusError("Reader: align past end of buffer");
    }

    uint8_t  read_u8()  { check(1); return data_[pos_++]; }
    uint16_t read_u16() { align(2); check(2); uint16_t v = get_u16(); pos_ += 2; return v; }
    uint32_t read_u32() { align(4); check(4); uint32_t v = get_u32(); pos_ += 4; return v; }
    uint64_t read_u64() { align(8); check(8); uint64_t v = get_u64(); pos_ += 8; return v; }

    int16_t  read_i16() { return static_cast<int16_t>(read_u16()); }
    int32_t  read_i32() { return static_cast<int32_t>(read_u32()); }
    int64_t  read_i64() { return static_cast<int64_t>(read_u64()); }
    double   read_double() {
        align(8);
        check(8);
        uint64_t bits = get_u64();
        pos_ += 8;
        double d;
        std::memcpy(&d, &bits, sizeof(d));
        return d;
    }

    bool read_bool() {
        uint32_t v = read_u32();
        return v != 0;
    }

    // Строка: uint32 length, затем length байт + NUL (NUL не входит в length)
    std::string read_string() {
        uint32_t len = read_u32();
        check(len + 1);
        std::string s(reinterpret_cast<const char*>(data_ + pos_), len);
        pos_ += len;
        if (data_[pos_] != 0)
            throw DbusError("Reader: string not NUL-terminated");
        pos_ += 1;
        return s;
    }

    // Сигнатура: uint8 length, затем length байт + NUL
    std::string read_signature() {
        uint8_t len = read_u8();
        check(len + 1);
        std::string s(reinterpret_cast<const char*>(data_ + pos_), len);
        pos_ += len;
        if (data_[pos_] != 0)
            throw DbusError("Reader: signature not NUL-terminated");
        pos_ += 1;
        return s;
    }

    // Массив: uint32 length в байтах, затем элементы.
    // Возвращает границы данных массива (pos начала и конца).
    std::pair<size_t, size_t> read_array_bounds(size_t element_align) {
        uint32_t len = read_u32();
        align(element_align); // выравнивание под первый элемент
        size_t start = pos_;
        size_t end   = pos_ + len;
        if (end > size_)
            throw DbusError("Reader: array past end of buffer");
        return {start, end};
    }

    // Прочитать сырые байты (для тела)
    std::vector<uint8_t> read_bytes(size_t n) {
        check(n);
        std::vector<uint8_t> v(data_ + pos_, data_ + pos_ + n);
        pos_ += n;
        return v;
    }

    // Переместить позицию вручную
    void seek(size_t p) {
        if (p > size_) throw DbusError("Reader: seek past end");
        pos_ = p;
    }

    void check(size_t n) const {
        if (pos_ + n > size_)
            throw DbusError("Reader: read past end of buffer");
    }

    uint16_t get_u16() const {
        uint16_t v;
        std::memcpy(&v, data_ + pos_, 2);
        if (endian_ == Endianness::Big) v = bswap16(v);
        return v;
    }
    uint32_t get_u32() const {
        uint32_t v;
        std::memcpy(&v, data_ + pos_, 4);
        if (endian_ == Endianness::Big) v = bswap32(v);
        return v;
    }
    uint64_t get_u64() const {
        uint64_t v;
        std::memcpy(&v, data_ + pos_, 8);
        if (endian_ == Endianness::Big) v = bswap64(v);
        return v;
    }

    static uint16_t bswap16(uint16_t v) { return (v >> 8) | (v << 8); }
    static uint32_t bswap32(uint32_t v) {
        return ((v & 0xFF000000u) >> 24) | ((v & 0x00FF0000u) >> 8)
             | ((v & 0x0000FF00u) << 8)  | ((v & 0x000000FFu) << 24);
    }
    static uint64_t bswap64(uint64_t v) {
        return ((uint64_t)bswap32((uint32_t)v) << 32) | bswap32((uint32_t)(v >> 32));
    }

private:
    const uint8_t* data_;
    size_t size_;
    size_t pos_;
    Endianness endian_;
};

// ============================================================
// 3. Writer — запись в буфер с выравниванием
// ============================================================

class Writer {
public:
    explicit Writer(Endianness endian = Endianness::Little)
        : endian_(endian) {}

    size_t size() const { return buf_.size(); }

    void align(size_t a) {
        while (buf_.size() % a != 0) buf_.push_back(0);
    }

    void write_u8(uint8_t v) { buf_.push_back(v); }
    void write_u16(uint16_t v) {
        align(2);
        if (endian_ == Endianness::Big) v = Reader::bswap16(v);
        uint8_t b[2]; std::memcpy(b, &v, 2);
        buf_.insert(buf_.end(), b, b + 2);
    }
    void write_u32(uint32_t v) {
        align(4);
        if (endian_ == Endianness::Big) v = Reader::bswap32(v);
        uint8_t b[4]; std::memcpy(b, &v, 4);
        buf_.insert(buf_.end(), b, b + 4);
    }
    void write_u64(uint64_t v) {
        align(8);
        if (endian_ == Endianness::Big) v = Reader::bswap64(v);
        uint8_t b[8]; std::memcpy(b, &v, 8);
        buf_.insert(buf_.end(), b, b + 8);
    }
    void write_i16(int16_t v)  { write_u16(static_cast<uint16_t>(v)); }
    void write_i32(int32_t v)  { write_u32(static_cast<uint32_t>(v)); }
    void write_i64(int64_t v)  { write_u64(static_cast<uint64_t>(v)); }
    void write_bool(bool v)    { write_u32(v ? 1u : 0u); }
    void write_double(double v) {
        uint64_t bits;
        std::memcpy(&bits, &v, sizeof(bits));
        write_u64(bits);
    }

    void write_string(const std::string& s) {
        write_u32(static_cast<uint32_t>(s.size()));
        buf_.insert(buf_.end(), s.begin(), s.end());
        buf_.push_back(0);
    }

    void write_signature(const std::string& s) {
        if (s.size() > 255) throw DbusError("signature too long");
        write_u8(static_cast<uint8_t>(s.size()));
        buf_.insert(buf_.end(), s.begin(), s.end());
        buf_.push_back(0);
    }

    // Записать массив: сначала плейсхолдер длины, потом callback заполняет данные,
    // потом мы возвращаемся и подставляем реальную длину.
    // element_align — выравнивание первого элемента.
    template <typename F>
    void write_array(size_t element_align, F&& write_elements) {
        size_t len_pos = buf_.size();
        write_u32(0);              // плейсхолдер
        align(element_align);      // выравнивание под первый элемент
        size_t data_start = buf_.size();
        write_elements();
        size_t data_end = buf_.size();
        uint32_t len = static_cast<uint32_t>(data_end - data_start);
        // Патчим длину
        if (endian_ == Endianness::Big) len = Reader::bswap32(len);
        std::memcpy(buf_.data() + len_pos, &len, 4);
    }

    // Добавить сырые байты (для тела)
    void append(const std::vector<uint8_t>& v) {
        buf_.insert(buf_.end(), v.begin(), v.end());
    }

    const std::vector<uint8_t>& buffer() const { return buf_; }
    std::vector<uint8_t>&       buffer()       { return buf_; }

private:
    std::vector<uint8_t> buf_;
    Endianness endian_;
};

// ============================================================
// 4. Message — заголовок + тело
// ============================================================

struct Message {
    Endianness  endian      = Endianness::Little;
    MessageType type        = MessageType::MethodCall;
    uint8_t     flags       = 0;
    uint32_t    serial      = 0;

    // Заголовочные поля
    std::optional<std::string> path;
    std::optional<std::string> interface;
    std::optional<std::string> member;
    std::optional<std::string> error_name;
    std::optional<uint32_t>    reply_serial;
    std::optional<std::string> destination;
    std::optional<std::string> sender;
    std::optional<std::string> signature;
    std::optional<uint32_t>    unix_fds;

    // Тело — сырые байты, готовые к отправке.
    // Для Hello оно пустое; для более сложных методов сюда кладётся
    // результат Writer::buffer().
    std::vector<uint8_t> body;

    // ---------- Парсинг ----------

    static Message parse(const uint8_t* data, size_t size) {
        if (size < 16) throw DbusError("message too short");

        Message m;
        m.endian = static_cast<Endianness>(data[0]);
        if (m.endian != Endianness::Little && m.endian != Endianness::Big)
            throw DbusError("invalid endianness byte");
        m.type   = static_cast<MessageType>(data[1]);
        m.flags  = data[2];
        uint8_t vers = data[3];
        if (vers != 1) throw DbusError("unsupported protocol version");

        Reader r(data, size, m.endian);
        r.seek(4);
        uint32_t body_len = r.read_u32();
        m.serial = r.read_u32();

        // Поля заголовка
        auto [start, end] = r.read_array_bounds(8);
        r.seek(start);
        while (r.position() < end) {
            r.align(8);
            if (r.position() >= end) break;
            uint8_t code = r.read_u8();
            // Значение — вариант: сигнатура + значение
            std::string sig = r.read_signature();
            parse_header_field(r, sig, code, m);
        }

        // Тело
        r.align(8);
        size_t body_start = r.position();
        if (body_start + body_len > size)
            throw DbusError("body length exceeds buffer");
        m.body.assign(data + body_start, data + body_start + body_len);
        return m;
    }

    // ---------- Сериализация ----------

    std::vector<uint8_t> serialize() const {
        Writer w(endian);

        // Временный заголовок: 16 байт (order, type, flags, vers, body_len, serial)
        w.write_u8(static_cast<uint8_t>(endian));
        w.write_u8(static_cast<uint8_t>(type));
        w.write_u8(flags);
        w.write_u8(1);
        w.write_u32(static_cast<uint32_t>(body.size()));
        w.write_u32(serial);

        // Массив заголовочных полей
        w.write_array(8, [&]() {
            if (path)        write_object_path_field(w, HeaderField::Path,        *path);
            if (interface)   write_string_field(w, HeaderField::Interface,   *interface);
            if (member)      write_string_field(w, HeaderField::Member,      *member);
            if (error_name)  write_string_field(w, HeaderField::ErrorName,   *error_name);
            if (reply_serial)write_uint32_field(w, HeaderField::ReplySerial, *reply_serial);
            if (destination) write_string_field(w, HeaderField::Destination, *destination);
            if (sender)      write_string_field(w, HeaderField::Sender,      *sender);
            if (signature)   write_signature_field(w, HeaderField::Signature,*signature);
            if (unix_fds)    write_uint32_field(w, HeaderField::UnixFDs,    *unix_fds);
        });

        // Тело: выравниваем до 8 и добавляем
        w.align(8);
        w.append(body);

        return w.buffer();
    }

private:
    static void parse_header_field(Reader& r, const std::string& sig,
                                   uint8_t code, Message& m) {
        HeaderField f = static_cast<HeaderField>(code);
        if (sig == "s" || sig == "o") {
            std::string v = r.read_string();
            switch (f) {
                case HeaderField::Path:        m.path        = v; break;
                case HeaderField::Interface:   m.interface   = v; break;
                case HeaderField::Member:      m.member      = v; break;
                case HeaderField::ErrorName:   m.error_name  = v; break;
                case HeaderField::Destination: m.destination = v; break;
                case HeaderField::Sender:      m.sender      = v; break;
                default: break; // неизвестное поле — игнорируем
            }
        } else if (sig == "g") {
            std::string v = r.read_signature();
            if (f == HeaderField::Signature) m.signature = v;
        } else if (sig == "u") {
            uint32_t v = r.read_u32();
            switch (f) {
                case HeaderField::ReplySerial: m.reply_serial = v; break;
                case HeaderField::UnixFDs:     m.unix_fds     = v; break;
                default: break;
            }
        } else {
            // Неизвестный тип — пропускаем (по спецификации нужно игнорировать
            // неизвестные поля, но для простоты здесь просто пропускаем).
        }
    }

    static void write_string_field(Writer& w, HeaderField f, const std::string& v) {
        w.align(8);
        w.write_u8(static_cast<uint8_t>(f));
        w.write_signature("s");
        w.write_string(v);
    }
    static void write_object_path_field(Writer& w, HeaderField f, const std::string& v) {
        w.align(8);
        w.write_u8(static_cast<uint8_t>(f));
        w.write_signature("o");   // <-- ключевое отличие
        w.write_string(v);        // формат строки и object_path идентичен
    }
    static void write_signature_field(Writer& w, HeaderField f, const std::string& v) {
        w.align(8);
        w.write_u8(static_cast<uint8_t>(f));
        w.write_signature("g");
        w.write_signature(v);
    }
    static void write_uint32_field(Writer& w, HeaderField f, uint32_t v) {
        w.align(8);
        w.write_u8(static_cast<uint8_t>(f));
        w.write_signature("u");
        w.write_u32(v);
    }
};

} // namespace dbus
