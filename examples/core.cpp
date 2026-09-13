/**
 * SPDX-FileCopyrightText: 2026 Mikhail Firsov <firsov62121@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "firmir/event-loop.h"
#include "firmir/timer.h"
#include "firmir/logging.h"

int main()
{
    Firmir::EventLoop loop;
    Firmir::Timer::singleShot(3000, [] () {
        Firmir::Info() << "Exit by 3 seconds timeout";
        Firmir::EventLoop::instance().quit();
    });
    return loop.exec();
}
