/**
 * SPDX-FileCopyrightText: 2026 Mikhail Firsov <firsov62121@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "visibility.h"

namespace Firmir {

struct EventLoopPrivate;
struct EventLoop
{
    EventLoop();
    ~EventLoop();

    static EventLoop &instance();
    int exec();
    void quit(int code = 0);

private:
    friend class EventLoopPrivate;
    EventLoopPrivate *pImpl;
};

} // namespace Firmir
