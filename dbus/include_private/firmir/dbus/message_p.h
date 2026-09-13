/**
 * SPDX-FileCopyrightText: 2026 Mikhail Firsov <firsov62121@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "firmir/dbus/message.h"

#include <cstddef>
#include <vector>
#include <optional>

namespace Firmir {
namespace DBus {

struct MessagePrivate
{
    using Type = Message::Type;
    enum class ReadFdRes { Success, Failure, WouldBlock };
    enum class Endianness : uint8_t { Little = 'l', Big = 'B' };

    struct Reader;
    struct Writer;
    struct FixedHeader
    {
        FixedHeader() = default;
        FixedHeader(Reader &reader);

        void serialize(Writer &writer) const;

        Endianness endian = Endianness::Little;
        Type type = Type::MethodCall;
        uint8_t flags = 0;
        uint8_t version = 1;
        uint32_t bodyLen = 0;
        uint32_t serial = 0;
        uint32_t fieldsLen = 0;
        bool valid = false;
    };
    struct DynamicHeader
    {
        enum class Field : uint8_t {
            Path = 1,
            Interface,
            Member,
            ErrorName,
            ReplySerial,
            Destination,
            Sender,
            Signature,
            UnixFDs,
        };

        DynamicHeader() = default;
        DynamicHeader(Reader &reader, size_t end);

        void serialize(Writer &writer) const;

        std::optional<std::string> path;
        std::optional<std::string> interface;
        std::optional<std::string> member;
        std::optional<std::string> errorName;
        std::optional<uint32_t> replySerial;
        std::optional<std::string> destination;
        std::optional<std::string> sender;
        std::optional<std::string> signature;
        std::optional<uint32_t> unixFds;
        bool valid = false;
    };

    struct Reader {
        bool check(size_t n);
        bool align(size_t align);
        bool seek(size_t newPos);

        uint16_t getUInt16();
        uint32_t getUInt32();
        uint64_t getUInt64();

        std::optional<uint8_t> readUInt8();
        std::optional<uint16_t> readUInt16();
        std::optional<uint32_t> readUInt32();
        std::optional<uint64_t> readUInt64();
        // std::optional<int8_t> readInt8();
        std::optional<int16_t> readInt16();
        std::optional<int32_t> readInt32();
        std::optional<int64_t> readInt64();
        std::optional<double> readDouble();
        std::optional<bool> readBool();
        std::optional<std::string> readString();
        std::optional<std::string> readSignature();

        std::vector<std::byte> data;
        size_t pos = 0;
        Endianness endian = Endianness::Little;
    };

    struct Writer {
        void align(size_t align);

        void writeUInt8(uint8_t value);
        void writeUInt16(uint16_t value);
        void writeUInt32(uint32_t value);
        void writeUInt64(uint64_t value);
        void writeInt16(int16_t value);
        void writeInt32(int32_t value);
        void writeInt64(int64_t value);
        void writeDouble(double value);
        void writeBool(bool value);
        void writeString(const std::string &value);
        void writeSignature(const std::string &value);

        std::vector<std::byte> data;
        Endianness endian = Endianness::Little;
    };

    std::vector<std::byte> serialize();
    bool deserialize(std::vector<std::byte> &&data);
    ReadFdRes readFd(int fd);
    std::string toString() const;

    static MessagePrivate &get(const Message &msg);

    FixedHeader fixedHeader;
    DynamicHeader dynamicHeader;
    Reader reader;
    Writer writer;
    bool valid = true;
};

std::ostream &operator<<(std::ostream &os, Firmir::DBus::MessagePrivate::ReadFdRes readFdRes);
std::ostream &operator<<(std::ostream &os, Firmir::DBus::MessagePrivate::Endianness endian);
std::ostream &operator<<(std::ostream &os, Firmir::DBus::MessagePrivate::DynamicHeader::Field field);

} // namespace DBus
} // namespace Firmir
