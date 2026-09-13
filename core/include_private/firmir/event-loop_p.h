/**
 * SPDX-FileCopyrightText: 2026 Mikhail Firsov <firsov62121@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "firmir/event-loop.h"

#include <unordered_map>
#include <functional>

#include <sys/epoll.h>

namespace Firmir {

struct EventLoopPrivate
{
    struct EventInfo {
        using Callback = std::function<void ()>;

        int fd = -1;
        uint32_t events = 0;
        Callback onRead = nullptr;
        Callback onWrite = nullptr;
    };

    static EventLoopPrivate &get(const EventLoop &pub);

    void addEvent(const EventInfo &event);
    void removeEvent(int fd);
    void addWakeFd();

    int exec();
    void quit(int code);

    int wakeFd = -1;
    int epollFd = epoll_create1(0);
    bool running = false;
    int exitCode = 0;
    std::unordered_map<int, EventInfo> events;
};

} // namespace Firmir
