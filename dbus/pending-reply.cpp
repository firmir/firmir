/**
 * SPDX-FileCopyrightText: 2026 Mikhail Firsov <firsov62121@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "firmir/dbus/pending-reply_p.h"
#include "firmir/logging.h"

namespace Firmir {
namespace DBus {

PendingReplyPrivate::PendingReplyPrivate(PendingReply *parent)
    : parent(parent)
{
}

void PendingReplyPrivate::setMessage(Message &&msg)
{
    if (result) {
        Error() << __func__ << "for second time";
        return;
    }

    result = std::make_shared<Message>(std::move(msg));
    parent->emitEvent(PendingReply::Event::Finished);
}

PendingReplyPrivate &PendingReplyPrivate::get(const PendingReply &reply)
{
    return *(reply.pImpl);
}

PendingReply::PendingReply()
    : pImpl(new PendingReplyPrivate(this))
{
}

PendingReply::~PendingReply()
{
    delete pImpl;
}

std::shared_ptr<Message> PendingReply::getMessage()
{
    return pImpl->result;
}

} // namespace DBus
} // namespace Firmir
