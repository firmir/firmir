/**
 * SPDX-FileCopyrightText: 2026 Mikhail Firsov <firsov62121@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "firmir/visibility.h"

#include <string>
#include <cstdint>

namespace Firmir {
namespace DBus {

struct MessagePrivate;
struct FIRMIR_EXPORT_SYMBOL Message
{
    enum class Type : uint8_t { MethodCall = 1, MethodReturn, Error, Signal };

    Message();
    Message(Message &&other);
    Message(const Message &other);
    ~Message();

    Message &operator=(Message &&other);
    Message &operator=(const Message &other);

    Type getType() const;
    void setType(Type value);

    std::string getDestination() const;
    void setDestination(const std::string &value);

    std::string getPath() const;
    void setPath(const std::string &value);

    std::string getInterface() const;
    void setInterface(const std::string &value);

    std::string getMember() const;
    void setMember(const std::string &value);

    std::string getErrorName() const;
    void setErrorName(const std::string &value);

    std::string toString() const;

    bool valid() const;

    Message &operator>>(std::string &item);
    Message &operator<<(const std::string &item);

private:
    friend class MessagePrivate;
    MessagePrivate *pImpl;
};

FIRMIR_EXPORT_SYMBOL std::ostream &operator<<(std::ostream &os, Firmir::DBus::Message::Type type);

} // namespace DBus
} // namespace Firmir
