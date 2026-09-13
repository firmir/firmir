/**
 * SPDX-FileCopyrightText: 2026 Mikhail Firsov <firsov62121@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "firmir/dbus/message_p.h"
#include "firmir/logging.h"

#include <cstring>
#include <sstream>

#include <unistd.h>

namespace Firmir {
namespace DBus {

namespace {

uint16_t bswap16(uint16_t v)
{
    return (v >> 8) | (v << 8);
}

uint32_t bswap32(uint32_t v)
{
    return ((v & 0xFF000000u) >> 24) | ((v & 0x00FF0000u) >> 8)
           | ((v & 0x0000FF00u) << 8)  | ((v & 0x000000FFu) << 24);
}

uint64_t bswap64(uint64_t v)
{
    return ((uint64_t)bswap32((uint32_t)v) << 32) | bswap32((uint32_t)(v >> 32));
}

std::optional<std::vector<std::byte>> readExact(int fd, size_t n)
{
    size_t got = 0;
    std::vector<std::byte> data(n);
    while (got < n) {
        ssize_t r = read(fd, data.data() + got, n - got);
        if (r <= 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                data.resize(got);
                return data;
            }

            Error() << __func__ << "Failed to read data";
            return {};
        }

        got += static_cast<size_t>(r);
    }

    return data;
}

} // anonymous namespace

MessagePrivate::FixedHeader::FixedHeader(Reader &reader)
{
    auto fail = [] (const std::string &err) {
        Error() << __func__ << "Failed to create FixedHeader:" << err;
    };

    if (reader.data.size() < 16) {
        fail("Not enough data");
        return;
    }

    endian = static_cast<Endianness>(reader.readUInt8().value());
    type = static_cast<Type>(reader.readUInt8().value());
    flags = reader.readUInt8().value();
    version = reader.readUInt8().value();
    if (version != 1) {
        fail("unsupported protocol version");
        return;
    }

    bodyLen = reader.readUInt32().value();
    serial = reader.readUInt32().value();
    fieldsLen = reader.readUInt32().value();
    valid = true;
}

void MessagePrivate::FixedHeader::serialize(Writer &writer) const
{
    writer.endian = endian;
    writer.writeUInt8(static_cast<uint8_t>(endian));
    writer.writeUInt8(static_cast<uint8_t>(type));
    writer.writeUInt8(static_cast<uint8_t>(flags));
    writer.writeUInt8(static_cast<uint8_t>(version));
    writer.writeUInt32(bodyLen);
    writer.writeUInt32(serial);
    writer.writeUInt32(fieldsLen);
}

MessagePrivate::DynamicHeader::DynamicHeader(Reader &reader, size_t end)
{
    auto fail = [] (const std::string &err) {
        Error() << __func__ << "Failed to create DynamicHeader:" << err;
    };

    while (reader.pos < end) {
        reader.align(8);
        if (reader.pos >= end)
            break;

        auto code = reader.readUInt8();
        if (!code) {
            fail("read code");
            return;
        }

        auto sigOpt = reader.readSignature();
        if (!sigOpt) {
            fail("read signature");
            return;
        }

        auto &sig = *sigOpt;
        auto f = static_cast<Field>(*code);
        if (sig == "s" || sig == "o") {
            auto valueOpt = reader.readString();
            if (!valueOpt) {
                fail("read string header field");
                return;
            }

            auto &v = *valueOpt;
            switch (f) {
            case Field::Path: path = v; break;
            case Field::Interface: interface = v; break;
            case Field::Member: member = v; break;
            case Field::ErrorName: errorName = v; break;
            case Field::Destination: destination = v; break;
            case Field::Sender: sender = v; break;
            default: break;
            }
        } else if (sig == "g") {
            auto v = reader.readSignature();
            if (!v) {
                fail("read g signature in header");
                return;
            }

            if (f == Field::Signature)
                signature = *v;
        } else if (sig == "u") {
            auto v = reader.readUInt32();
            if (!v) {
                fail("read uint32");
                return;
            }

            switch (f) {
            case Field::ReplySerial: replySerial = *v; break;
            case Field::UnixFDs: unixFds = *v; break;
            default: break;
            }
        }
    }
    valid = true;
}

void MessagePrivate::DynamicHeader::serialize(Writer &writer) const
{
    auto writeString = [&writer] (Field field, const std::string &value, const std::string &signature = "s") {
        writer.align(8);
        writer.writeUInt8(static_cast<uint8_t>(field));
        writer.writeSignature(signature);
        writer.writeString(value);
    };
    auto writeUInt32 = [&writer] (Field field, uint32_t value) {
        writer.align(8);
        writer.writeUInt8(static_cast<uint8_t>(field));
        writer.writeSignature("u");
        writer.writeUInt32(value);
    };

    if (path)
        writeString(Field::Path, *path, "o");
    if (interface)
        writeString(Field::Interface, *interface);
    if (member)
        writeString(Field::Member, *member);
    if (errorName)
        writeString(Field::ErrorName, *errorName);
    if (destination)
        writeString(Field::Destination, *destination);
    if (sender)
        writeString(Field::Sender, *sender);
    if (replySerial)
        writeUInt32(Field::ReplySerial, *replySerial);
    if (unixFds)
        writeUInt32(Field::UnixFDs, *unixFds);

    if (signature) {
        writer.align(8);
        writer.writeUInt8(static_cast<uint8_t>(Field::Signature));
        writer.writeSignature("g");
        writer.writeSignature(*signature);
    }
}

bool MessagePrivate::Reader::check(size_t n)
{
    return pos + n <= data.size();
}

bool MessagePrivate::Reader::align(size_t align)
{
    size_t rem = pos % align;
    return rem == 0 ? true : seek(pos + align - rem);
}

bool MessagePrivate::Reader::seek(size_t newPos)
{
    if (newPos > data.size())
        return false;

    pos = newPos;
    return true;
}

uint16_t MessagePrivate::Reader::getUInt16()
{
    uint16_t v;
    std::memcpy(&v, data.data() + pos, 2);
    pos += 2;
    if (endian == Endianness::Big)
        v = bswap16(v);
    return v;
}

uint32_t MessagePrivate::Reader::getUInt32()
{
    uint32_t v;
    std::memcpy(&v, data.data() + pos, 4);
    pos += 4;
    if (endian == Endianness::Big)
        v = bswap32(v);
    return v;
}

uint64_t MessagePrivate::Reader::getUInt64()
{
    uint64_t v;
    std::memcpy(&v, data.data() + pos, 8);
    pos += 8;
    if (endian == Endianness::Big)
        v = bswap64(v);
    return v;
}

std::optional<uint8_t> MessagePrivate::Reader::readUInt8()
{
    if (!check(1))
        return {};

    return static_cast<uint8_t>(data[pos++]);
}

std::optional<uint16_t> MessagePrivate::Reader::readUInt16()
{
    if (!align(2) || !check(2))
        return {};

    return getUInt16();
}

std::optional<uint32_t> MessagePrivate::Reader::readUInt32()
{
    if (!align(4) || !check(4))
        return {};

    return getUInt32();
}

std::optional<uint64_t> MessagePrivate::Reader::readUInt64()
{
    if (!align(8) || !check(8))
        return {};

    return getUInt64();
}

std::optional<int16_t> MessagePrivate::Reader::readInt16()
{
    auto v = readUInt16();
    if (!v)
        return {};
 
    return static_cast<int16_t>(*v);
}

std::optional<int32_t> MessagePrivate::Reader::readInt32()
{
    auto v = readUInt32();
    if (!v)
        return {};
 
    return static_cast<int32_t>(*v);
}

std::optional<int64_t> MessagePrivate::Reader::readInt64()
{
    auto v = readUInt64();
    if (!v)
        return {};
 
    return static_cast<int64_t>(*v);
}

std::optional<double> MessagePrivate::Reader::readDouble()
{
    align(8);
    check(8);
    uint64_t bits = getUInt64();
    double d;
    std::memcpy(&d, &bits, sizeof(d));
    return d;
}

std::optional<bool> MessagePrivate::Reader::readBool()
{
    auto v = readUInt32();
    if (!v)
        return {};

    return (bool)(v != 0);
}

std::optional<std::string> MessagePrivate::Reader::readString()
{
    auto len = readUInt32();
    if (!len || !check(*len + 1))
        return {};

    std::string s(reinterpret_cast<const char*>(data.data() + pos), *len);
    pos += *len;
    if (static_cast<uint8_t>(data[pos]) != 0)
        return {};

    pos += 1;
    return s;
}

std::optional<std::string> MessagePrivate::Reader::readSignature()
{
    auto len = readUInt8();
    if (!len || !check(*len + 1))
        return {};

    std::string s(reinterpret_cast<const char*>(data.data() + pos), *len);
    pos += *len;
    if (static_cast<uint8_t>(data[pos]) != 0)
        return {};

    pos += 1;
    return s;
}

void MessagePrivate::Writer::align(size_t align)
{
    size_t rem = data.size() % align;
    if (rem != 0)
        data.resize(data.size() + align - rem);
}

void MessagePrivate::Writer::writeUInt8(uint8_t value)
{
    data.push_back(static_cast<std::byte>(value));
}

void MessagePrivate::Writer::writeUInt16(uint16_t value)
{
    align(2);
    if (endian == Endianness::Big)
        value = bswap16(value);

    std::byte b[2];
    std::memcpy(b, &value, 2);
    data.insert(data.end(), b, b + 2);
}

void MessagePrivate::Writer::writeUInt32(uint32_t value)
{
    align(4);
    if (endian == Endianness::Big)
        value = bswap32(value);

    std::byte b[4];
    std::memcpy(b, &value, 4);
    data.insert(data.end(), b, b + 4);
}

void MessagePrivate::Writer::writeUInt64(uint64_t value)
{
    align(8);
    if (endian == Endianness::Big)
        value = bswap64(value);

    std::byte b[8];
    std::memcpy(b, &value, 8);
    data.insert(data.end(), b, b + 8);
}

void MessagePrivate::Writer::writeInt16(int16_t value)
{
    writeUInt16(static_cast<uint16_t>(value));
}

void MessagePrivate::Writer::writeInt32(int32_t value)
{
    writeUInt32(static_cast<uint32_t>(value));
}

void MessagePrivate::Writer::writeInt64(int64_t value)
{
    writeUInt64(static_cast<uint64_t>(value));
}

void MessagePrivate::Writer::writeDouble(double value)
{
    uint64_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    writeUInt64(bits);
}

void MessagePrivate::Writer::writeBool(bool value)
{
    writeUInt32(value ? 1u : 0u);
}

void MessagePrivate::Writer::writeString(const std::string &value)
{
    writeUInt32(static_cast<uint32_t>(value.size()));
    auto *start = reinterpret_cast<const std::byte *>(value.data());
    data.insert(data.end(), start, start + value.size());
    data.push_back(static_cast<std::byte>(0));
}

void MessagePrivate::Writer::writeSignature(const std::string &value)
{
    writeUInt8(static_cast<uint8_t>(value.size()));
    auto *start = reinterpret_cast<const std::byte *>(value.data());
    data.insert(data.end(), start, start + value.size());
    data.push_back(static_cast<std::byte>(0));
}

std::vector<std::byte> MessagePrivate::serialize()
{
    Writer dynamicHeaderWriter, fixedHeaderWriter;
    dynamicHeader.serialize(dynamicHeaderWriter);
    fixedHeader.bodyLen = writer.data.size();
    fixedHeader.fieldsLen = dynamicHeaderWriter.data.size();
    fixedHeader.serialize(fixedHeaderWriter);
    auto data = std::move(fixedHeaderWriter.data);
    data.insert(data.end(), dynamicHeaderWriter.data.begin(), dynamicHeaderWriter.data.end());
    size_t padding = (8 - (dynamicHeaderWriter.data.size() % 8)) % 8;
    if (padding != 0)
        data.resize(data.size() + padding);
    data.insert(data.end(), writer.data.begin(), writer.data.end());
    return data;
}

MessagePrivate::ReadFdRes MessagePrivate::readFd(int fd)
{
    auto unsureHasData = [this, fd] (size_t n) {
        if (reader.data.size() >= n)
            return ReadFdRes::Success;

        auto data = readExact(fd, n - reader.data.size());
        if (!data)
            return ReadFdRes::Failure;

        reader.data.insert(reader.data.end(), data->begin(), data->end());
        return reader.data.size () < n ? ReadFdRes::WouldBlock : ReadFdRes::Success;
    };

    auto fail = [this] (ReadFdRes res) {
        if (res == ReadFdRes::Failure)
            reader = Reader();
        return res;
    };

    auto hasHeader = unsureHasData(16);
    if (hasHeader != ReadFdRes::Success)
        return fail(hasHeader);

    if (!fixedHeader.valid) {
        fixedHeader = FixedHeader(reader);
        if (!fixedHeader.valid)
            return fail(ReadFdRes::Failure);
    }

    size_t headerTotal = 16 + fixedHeader.fieldsLen;
    size_t padding = (8 - (headerTotal % 8)) % 8;
    size_t totalSize = headerTotal + padding + fixedHeader.bodyLen;
    auto hasBody = unsureHasData(totalSize);
    if (hasBody != ReadFdRes::Success)
        return fail(hasBody);

    if (!dynamicHeader.valid) {
        dynamicHeader = DynamicHeader(reader, headerTotal);
        reader.align(8);
        if (!dynamicHeader.valid)
            return fail(ReadFdRes::Failure);
    }

    return ReadFdRes::Success;
}

std::string MessagePrivate::toString() const
{
    std::stringstream res;
    res << "Message(" << std::endl;
    res << " endian=" << fixedHeader.endian << std::endl;
    res << " type=" << fixedHeader.type << std::endl;
    res << " flags=" << (int)fixedHeader.flags << std::endl;
    res << " version=" << (int)fixedHeader.version << std::endl;
    res << " bodyLen=" << fixedHeader.bodyLen << std::endl;
    res << " serial=" << fixedHeader.serial << std::endl;
    res << " fieldsLen=" << fixedHeader.fieldsLen << std::endl;
    if (dynamicHeader.path)
        res << " path=" << *dynamicHeader.path << std::endl;
    if (dynamicHeader.interface)
        res << " interface=" << *dynamicHeader.interface << std::endl;
    if (dynamicHeader.member)
        res << " member=" << *dynamicHeader.member << std::endl;
    if (dynamicHeader.errorName)
        res << " errorName=" << *dynamicHeader.errorName << std::endl;
    if (dynamicHeader.replySerial)
        res << " replySerial=" << *dynamicHeader.replySerial << std::endl;
    if (dynamicHeader.destination)
        res << " destination=" << *dynamicHeader.destination << std::endl;
    if (dynamicHeader.sender)
        res << " sender=" << *dynamicHeader.sender << std::endl;
    if (dynamicHeader.signature)
        res << " signature=" << *dynamicHeader.signature << std::endl;
    if (dynamicHeader.unixFds)
        res << " unixFds=" << *dynamicHeader.unixFds << std::endl;
    res << ")\n";
    return res.str();
}

MessagePrivate &MessagePrivate::get(const Message &msg)
{
    return *(msg.pImpl);
}

Message::Message()
    : pImpl(new MessagePrivate)
{
}

Message::Message(Message &&other)
    : pImpl(new MessagePrivate(std::move(*other.pImpl)))
{
}

Message::Message(const Message &other)
    : pImpl(new MessagePrivate(*other.pImpl))
{
}

Message::~Message()
{
    delete pImpl;
}

Message &Message::operator=(Message &&other)
{
    if (this != &other)
        std::swap(*pImpl, *(other.pImpl));
    return *this;
}

Message &Message::operator=(const Message &other)
{
    if (this != &other)
        *pImpl = *(other.pImpl);
    return *this;
}

Message::Type Message::getType() const
{
    return pImpl->fixedHeader.type;
}

void Message::setType(Type value)
{
    pImpl->fixedHeader.type = value;
}

std::string Message::getDestination() const
{
    auto &res = pImpl->dynamicHeader.destination;
    return res ? *res : "";
}

void Message::setDestination(const std::string &value)
{
    pImpl->dynamicHeader.destination = value;
}

std::string Message::getPath() const
{
    auto &res = pImpl->dynamicHeader.path;
    return res ? *res : "";
}

void Message::setPath(const std::string &value)
{
    pImpl->dynamicHeader.path = value;
}

std::string Message::getInterface() const
{
    auto &res = pImpl->dynamicHeader.interface;
    return res ? *res : "";
}

void Message::setInterface(const std::string &value)
{
    pImpl->dynamicHeader.interface = value;
}

std::string Message::getMember() const
{
    auto &res = pImpl->dynamicHeader.member;
    return res ? *res : "";
}

void Message::setMember(const std::string &value)
{
    pImpl->dynamicHeader.member = value;
}

std::string Message::getErrorName() const
{
    auto &res = pImpl->dynamicHeader.errorName;
    return res ? *res : "";
}

void Message::setErrorName(const std::string &value)
{
    pImpl->dynamicHeader.errorName = value;
}

std::string Message::toString() const
{
    return pImpl->toString();
}

bool Message::valid() const
{
    return pImpl->valid;
}

Message &Message::operator>>(std::string &item)
{
    auto res = pImpl->reader.readString();
    if (!res) {
        pImpl->valid = false;
        return *this;
    }

    std::swap(*res, item);
    return *this;
}

Message &Message::operator<<(const std::string &)
{
    return *this;
}

std::ostream &operator<<(std::ostream &os, Firmir::DBus::Message::Type type)
{
    using Type = Firmir::DBus::Message::Type;
    switch (type) {
    case Type::MethodCall: os << "MethodCall"; break;
    case Type::MethodReturn: os << "MethodReturn"; break;
    case Type::Error: os << "Error"; break;
    case Type::Signal: os << "Signal"; break;
    default: os << "Unknown"; break;
    }
    return os;
}

std::ostream &operator<<(std::ostream &os, Firmir::DBus::MessagePrivate::ReadFdRes readFdRes)
{
    using ReadFdRes = Firmir::DBus::MessagePrivate::ReadFdRes;
    switch (readFdRes) {
    case ReadFdRes::Success: os << "Success"; break;
    case ReadFdRes::Failure: os << "Failure"; break;
    case ReadFdRes::WouldBlock: os << "WouldBlock"; break;
    default: os << "Unknown"; break;
    }

    return os;
}

std::ostream &operator<<(std::ostream &os, Firmir::DBus::MessagePrivate::Endianness endian)
{
    using Endianness = Firmir::DBus::MessagePrivate::Endianness;
    switch (endian) {
    case Endianness::Little: os << "Little"; break;
    case Endianness::Big: os << "Big"; break;
    default: os << "Unknown"; break;
    }

    return os;
}

std::ostream &operator<<(std::ostream &os, Firmir::DBus::MessagePrivate::DynamicHeader::Field field)
{
    using Field = Firmir::DBus::MessagePrivate::DynamicHeader::Field;
    switch (field) {
    case Field::Path: os << "Path"; break;
    case Field::Interface: os << "Interface"; break;
    case Field::Member: os << "Member"; break;
    case Field::ErrorName: os << "ErrorName"; break;
    case Field::ReplySerial: os << "ReplySerial"; break;
    case Field::Destination: os << "Destination"; break;
    case Field::Sender: os << "Sender"; break;
    case Field::Signature: os << "Signature"; break;
    case Field::UnixFDs: os << "UnixFDs"; break;
    default: os << "Unknown"; break;
    }

    return os;
}

} // namespace DBus
} // namespace Firmir
