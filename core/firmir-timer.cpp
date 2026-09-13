/**
 * SPDX-FileCopyrightText: 2026 Mikhail Firsov <firsov62121@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "firmir/timer_p.h"
#include "firmir/event-loop_p.h"
#include "firmir/logging.h"

#include <iostream>
#include <cstring>

#include <sys/timerfd.h>
#include <unistd.h>

namespace Firmir {

TimerPrivate::TimerPrivate(Timer *parent)
    : parent(parent)
{
    fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (fd == -1) {
        Error() << "Failed to call timerfd_create";
        return;
    }

    EventLoopPrivate::get(EventLoop::instance()).addEvent(EventLoopPrivate::EventInfo {
        .fd = fd,
        .events = EPOLLIN,
        .onRead = [this] () {
            uint64_t expirations;
            ssize_t s = read(fd, &expirations, sizeof(expirations));
            if (s != sizeof(uint64_t))
                return;

            for (uint64_t i = 0; i < expirations; ++i)
                this->parent->emitEvent(Timer::Event::Timeout);
        },
        .onWrite = nullptr,
    });
}

TimerPrivate::~TimerPrivate()
{
    if (fd >= 0) {
        close(fd);
        fd = -1;
    }
}

void TimerPrivate::setSingleShot(bool value)
{
    singleShot = value;
    if (active)
        updateParams();
}

void TimerPrivate::setIntervalMs(uint32_t value)
{
    if (intervalMs == value)
        return;

    intervalMs = value;
    if (active)
        updateParams();
}

void TimerPrivate::setActive(bool value)
{
    if (value == active)
        return;

    active = value;
    updateParams();
}

void TimerPrivate::updateParams()
{
    itimerspec spec;
    if (active) {
        spec.it_value.tv_sec = intervalMs / 1000;
        spec.it_value.tv_nsec = (intervalMs % 1000) * 1000000;
        spec.it_interval.tv_sec = singleShot ? 0 : intervalMs / 1000;
        spec.it_interval.tv_nsec = singleShot ? 0 : (intervalMs % 1000) * 1000000;
    } else {
        std::memset(&spec, 0, sizeof(spec));
    }
    if (timerfd_settime(fd, 0, &spec, NULL) == -1)
        Error() << "Failed to timerfd_settime";
}

Timer::Timer()
    : pImpl(new TimerPrivate(this))
{
}

Timer::~Timer()
{
    delete pImpl;
}

void Timer::setSingleShot(bool value)
{
    pImpl->setSingleShot(value);
}

void Timer::setIntervalMs(uint32_t value)
{
    pImpl->setIntervalMs(value);
}

void Timer::setActive(bool value)
{
    pImpl->setActive(value);
}

void Timer::stop()
{
    setActive(false);
}

void Timer::start()
{
    setActive(true);
}

void Timer::singleShot(uint32_t intervalMs, std::function<void ()> callback)
{
    auto *timer = new Timer();
    timer->setIntervalMs(intervalMs);
    timer->setSingleShot(true);
    timer->connect(Event::Timeout, *timer, [timer, callback] () {
        delete timer;
        callback();
    });
    timer->start();
}

} // namespace Firmir
