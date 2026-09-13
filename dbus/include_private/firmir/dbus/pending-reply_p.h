/**
 * SPDX-FileCopyrightText: 2026 Mikhail Firsov <firsov62121@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "firmir/dbus/pending-reply.h"
#include "message_p.h"

namespace Firmir {
namespace DBus {

struct PendingReplyPrivate
{
    PendingReplyPrivate(PendingReply *parent);

    void setMessage(Message &&msg);

    static PendingReplyPrivate &get(const PendingReply &reply);

    PendingReply *parent;
    std::shared_ptr<Message> result;
};

} // namespace DBus
} // namespace Firmir
