/**
 * SPDX-FileCopyrightText: 2026 Mikhail Firsov <firsov62121@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "firmir/event-loop_p.h"
#include "firmir/logging.h"

#include <memory>
#include <optional>
#include <iostream>

#include <unistd.h>
#include <sys/eventfd.h>

namespace Firmir {

namespace {

namespace Singleton {

std::unique_ptr<EventLoop> value;
std::optional<EventLoop *> extValue;

} // namespace Singleton

} // anonymous namespace

EventLoopPrivate &EventLoopPrivate::get(const EventLoop &pub)
{
    return *(pub.pImpl);
}

void EventLoopPrivate::addEvent(const EventInfo &event)
{
    events[event.fd] = event;
    epoll_event ev;
    ev.events = event.events;
    ev.data.fd = event.fd;
    epoll_ctl(epollFd, EPOLL_CTL_ADD, event.fd, &ev);
}

void EventLoopPrivate::removeEvent(int fd)
{
    if (!events.contains(fd))
        return;

    events.erase(fd);
    epoll_ctl(epollFd, EPOLL_CTL_DEL, fd, nullptr);
}

void EventLoopPrivate::addWakeFd()
{
    if (wakeFd >= 0)
        return;

    wakeFd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (wakeFd == -1) {
        Error() << "Failed to create wake fd";
        exit(EXIT_FAILURE);
    }

    addEvent(EventInfo {
        .fd = wakeFd,
        .events = EPOLLIN,
        .onRead = [this] () {
            uint64_t val;
            read(wakeFd, &val, sizeof(val));
        },
        .onWrite = nullptr,
    });
}

int EventLoopPrivate::exec()
{
    if (running)
        Error() << "Start Firmir::EventLoop while it is already running";

    addWakeFd();
    epoll_event curEvents[64];
    running = true;
    while (running) {
        int n = epoll_wait(epollFd, curEvents, 64, -1);
        for (int i = 0; i < n; i++) {
            int fd = curEvents[i].data.fd;
            if (!events.contains(fd)) {
                Error() << "Event on strange fd triggered: " << fd;
                continue;
            }

            auto &event = events[fd];
            int curState = curEvents[i].events;
            if (curState & EPOLLIN) {
                if (event.onRead)
                    event.onRead();
            }
            if (curState & EPOLLOUT) {
                if (event.onWrite)
                    event.onWrite();
            }
        }
    }
    return 0;
}

void EventLoopPrivate::quit(int code)
{
    exitCode = code;
    running = false;
    uint64_t one = 1;
    write(wakeFd, &one, sizeof(one));
}

EventLoop::EventLoop()
    : pImpl(new EventLoopPrivate)
{
    if (!Singleton::extValue)
        Singleton::extValue = this;
}

EventLoop::~EventLoop()
{
    delete pImpl;
}

EventLoop &EventLoop::instance()
{
    if (Singleton::extValue) {
        auto *value = *Singleton::extValue;
        if (value)
            return *value;
    } else {
        Singleton::extValue = nullptr;
    }

    if (!Singleton::value)
        Singleton::value = std::make_unique<EventLoop>();
    return *Singleton::value;
}

int EventLoop::exec()
{
    return pImpl->exec();
}

void EventLoop::quit(int code)
{
    pImpl->quit(code);
}

} // namespace Firmir