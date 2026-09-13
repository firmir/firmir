/**
 * SPDX-FileCopyrightText: 2026 Mikhail Firsov <firsov62121@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "firmir/dbus/connection.h"

#include <string>
#include <unordered_map>

namespace Firmir {
namespace DBus {

struct ConnectionPrivate
{
    using State = Connection::State;
    using Type = Connection::Type;
    using ReplyPtr = std::shared_ptr<PendingReply>;

    ConnectionPrivate(Connection *parent, Type type);
    ~ConnectionPrivate();

    void startConnect();
    std::string getPath();
    void updateState(State value);
    void readData();
    void doAuth();
    void doGetName();
    ReplyPtr send(const Message &msg);

    static ConnectionPrivate &get(const Connection &connection);

    Connection *parent;
    Type type;
    State state = State::Connecting;
    int sock = -1;
    std::string authData;
    uint32_t nextSerial = 1;
    std::unordered_map<uint32_t, ReplyPtr> replies;
};

} // namespace DBus
} // namespace Firmir
