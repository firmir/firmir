/**
 * SPDX-FileCopyrightText: 2026 Mikhail Firsov <firsov62121@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "firmir/event-loop.h"
#include "firmir/timer.h"
#include "firmir/logging.h"
#include "firmir/dbus/connection.h"
#include "firmir/dbus/message.h"
#include "firmir/dbus/pending-reply.h"

namespace {

namespace FBus = Firmir::DBus;

struct Handler : public Firmir::Object
{
    Handler(FBus::Connection::Type type);

    void onConnected();
    void listNames();

    FBus::Connection connection;
    std::shared_ptr<FBus::PendingReply> onListNamesFinished;
};

Handler::Handler(FBus::Connection::Type type)
    : connection(type)
{
    connection.connect(FBus::Connection::Event::StateChanged, connection, [this] () {
        if (connection.getState() == FBus::Connection::State::Connected)
            onConnected();
    });
}

void Handler::onConnected()
{
    Firmir::Warning() << "Connected to" << connection.getType() << "bus";
    listNames();
}

void Handler::listNames()
{
    Firmir::Info() << "start list names";

    FBus::Message msg;
    msg.setDestination("org.freedesktop.DBus");
    msg.setPath("/org/freedesktop/DBus");
    msg.setInterface("org.freedesktop.DBus");
    msg.setMember("ListNames");
    onListNamesFinished = connection.send(msg);
    onListNamesFinished->connect(FBus::PendingReply::Event::Finished, *this, [this] () {
        auto response = onListNamesFinished->getMessage();
        Firmir::Info() << "List names res:" << response->toString();
    });
}

} // anonymous namespace

int main()
{
    Firmir::EventLoop loop;
    Handler handler(FBus::Connection::Type::System);
    Firmir::Timer::singleShot(3000, [] () {
        Firmir::Info() << "Exit by 3 seconds timeout";
        Firmir::EventLoop::instance().quit();
    });
    return loop.exec();
}
