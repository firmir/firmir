/**
 * SPDX-FileCopyrightText: 2026 Mikhail Firsov <firsov62121@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "firmir/visibility.h"
#include "firmir/object.h"

#include <memory>

namespace Firmir {
namespace DBus {

struct Message;
struct PendingReply;
struct ConnectionPrivate;
struct FIRMIR_EXPORT_SYMBOL Connection : public Object
{
    enum class Event { StateChanged };
    enum class State { Connecting, Auth, GettingName, Connected, Error };
    enum class Type { Session, System };

    Connection(Type type = Type::Session);
    ~Connection();

    State getState();

    std::shared_ptr<PendingReply> send(const Message &msg);
private:
    friend class ConnectionPrivate;
    ConnectionPrivate *pImpl;
};

} // namespace DBus
} // namespace Firmir
