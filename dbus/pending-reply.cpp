/**
 * SPDX-FileCopyrightText: 2026 Mikhail Firsov <firsov62121@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "firmir/dbus/pending-reply_p.h"

namespace Firmir {
namespace DBus {

PendingReplyPrivate::PendingReplyPrivate(PendingReply *parent)
    : parent(parent)
{
}

PendingReplyPrivate::~PendingReplyPrivate()
{
}

PendingReplyPrivate &PendingReplyPrivate::get(const PendingReply &reply)
{
    return *(reply.pImpl);
}

PendingReply::PendingReply()
    : pImpl(new PendingReplyPrivate(this))
{
}

bool PendingReply::finished()
{
    return pImpl->result.get();
}

std::shared_ptr<Message> PendingReply::getMessage()
{
    return pImpl->result;
}

} // namespace DBus
} // namespace Firmir
