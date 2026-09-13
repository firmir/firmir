/**
 * SPDX-FileCopyrightText: 2026 Mikhail Firsov <firsov62121@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "firmir/object.h"

#include <memory>

namespace Firmir {
namespace DBus {

struct Message;
struct PendingReplyPrivate;
struct FIRMIR_EXPORT_SYMBOL PendingReply : public Object
{
    enum class Event { Finished };

    PendingReply();
    ~PendingReply();

    std::shared_ptr<Message> getMessage();

private:
    friend class PendingReplyPrivate;
    PendingReplyPrivate *pImpl;
};

} // namespace DBus
} // namespace Firmir
