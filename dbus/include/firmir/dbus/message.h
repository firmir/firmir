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

    Message(Type type = Type::MethodCall);
    ~Message();

    Type getType() const;
    void setType(Type value);

    std::string getService() const;
    void setService(const std::string &value);

    std::string getPath() const;
    void setPath(const std::string &value);

    std::string getInterface() const;
    void setInterface(const std::string &value);

    std::string getMember() const;
    void setMember(const std::string &value);

    bool deserializeResult() const;

    Message &operator>>(std::string &item);

private:
    friend class MessagePrivate;
    MessagePrivate *pImpl;
};

} // namespace DBus
} // namespace Firmir
