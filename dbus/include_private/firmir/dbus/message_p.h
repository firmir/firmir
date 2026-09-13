/**
 * SPDX-FileCopyrightText: 2026 Mikhail Firsov <firsov62121@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "firmir/dbus/message.h"

#include <cstddef>
#include <vector>

namespace Firmir {
namespace DBus {

struct MessagePrivate
{
    using Type = Message::Type;

    MessagePrivate(Message *parent, Type type);
    ~MessagePrivate();

    std::vector<std::byte> serialize();

    static MessagePrivate &get(const Message &msg);

    Message *parent;
    Type type;
    std::string service;
    std::string path;
    std::string interface;
    std::string member;
    int32_t serial = 0;
    bool deserializeResult = true;
};

} // namespace DBus
} // namespace Firmir
