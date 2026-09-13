/**
 * SPDX-FileCopyrightText: 2026 Mikhail Firsov <firsov62121@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "firmir/timer.h"

namespace Firmir {

struct TimerPrivate
{
    TimerPrivate(Timer *parent);
    ~TimerPrivate();

    void setSingleShot(bool value);
    void setIntervalMs(uint32_t value);

    void setActive(bool value);

    void updateParams();

    Timer *parent;
    bool singleShot = false;
    uint32_t intervalMs = 1000;
    bool active = false;
    int fd = -1;
};

} // namespace Firmir
