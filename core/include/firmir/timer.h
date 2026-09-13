/**
 * SPDX-FileCopyrightText: 2026 Mikhail Firsov <firsov62121@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "visibility.h"
#include "object.h"

#include <functional>

namespace Firmir {

struct TimerPrivate;
struct FIRMIR_EXPORT_SYMBOL Timer : public Object
{
    enum class Event { Timeout };

    Timer();
    ~Timer();

    void setSingleShot(bool value);
    void setIntervalMs(uint32_t value);

    void setActive(bool value);
    void stop();
    void start();

    static void singleShot(uint32_t intervalMs, std::function<void ()> callback);

private:
    friend class TimerPrivate;
    TimerPrivate *pImpl;
};

} // namespace Firmir
