/**
 * SPDX-FileCopyrightText: 2026 Mikhail Firsov <firsov62121@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "firmir/dbus/connection_p.h"
#include "firmir/dbus/pending-reply_p.h"
#include "firmir/dbus/message_p.h"
#include "firmir/logging.h"
#include "firmir/event-loop_p.h"

#include <cstring>

#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

namespace Firmir {
namespace DBus {

namespace {

constexpr size_t DataBlockSizeForRead = 4096;

std::string uidToHex(uid_t uid) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%u", uid);
    std::string s = buf;
    std::string hex;
    for (char c : s) {
        char h[3];
        std::snprintf(h, sizeof(h), "%02x", static_cast<unsigned char>(c));
        hex += h;
    }
    return hex;
}

} // anonymous namespace

ConnectionPrivate::ConnectionPrivate(Connection *parent, Type type)
    : parent(parent)
    , type(type)
{
    startConnect();
}

ConnectionPrivate::~ConnectionPrivate()
{
    if (sock >= 0) {
        EventLoopPrivate::get(EventLoop::instance()).removeEvent(sock);
        close(sock);
        sock = -1;
    }
}

void ConnectionPrivate::startConnect()
{
    auto path = getPath();
    Debug() << __func__ << path;

    auto fail = [this, &path] (const std::string &err) {
        Error() << "startConnect to path" << path << "error:" << err;
        updateState(State::Error);
    };

    sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock == -1) {
        fail("create socket");
        return;
    }

    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);

    sockaddr_un addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        fail("connect to socket");
        return;
    }

    EventLoopPrivate::get(EventLoop::instance()).addEvent(EventLoopPrivate::EventInfo {
        .fd = sock,
        .events = EPOLLIN,
        .onRead = [this] () {
            readData();
        },
        .onWrite = nullptr,
    });

    Debug() << __func__ << "[auth] sending NUL byte";
    char nul = '\0';
    if (write(sock, &nul, 1) != 1) {
        fail("write nul");
        return;
    }

    updateState(State::Auth);
    std::string authCmd = "AUTH EXTERNAL " + uidToHex(getuid()) + "\r\n";
    Debug() << __func__ << "[auth] >>>" << authCmd.substr(0, authCmd.size() - 2);
    if (write(sock, authCmd.data(), authCmd.size()) == -1) {
        fail("write auth");
        return;
    }
}

std::string ConnectionPrivate::getPath()
{
    auto getDefaultPath = [this] () -> std::string {
        return type == Type::Session
               ? std::string("/run/user/") + std::to_string(getuid()) + "/bus"
               : "/run/dbus/system_bus_socket";
    };

    const char *addrEnv = std::getenv(type == Type::Session ? "DBUS_SESSION_BUS_ADDRESS" : "DBUS_SYSTEM_BUS_ADDRESS");
    if (!addrEnv)
        return getDefaultPath();

    std::string addr = addrEnv;
    auto pos = addr.find("path=");
    if (pos == std::string::npos)
        return getDefaultPath();

    auto path = addr.substr(pos + 5);
    auto end = path.find(',');
    if (end != std::string::npos)
        path = path.substr(0, end);
    return path;
}

void ConnectionPrivate::updateState(State value)
{
    if (state == value)
        return;

    Debug() << __func__ << value;
    state = value;
    parent->emitEvent(Connection::Event::StateChanged);
}

void ConnectionPrivate::readData()
{
    if (state == State::Auth) {
        doAuth();
        return;
    }

    if (state != State::GettingName && state != State::Connected)
        return;

    auto &readMsgPrivate = MessagePrivate::get(readMsg);
    auto res = readMsgPrivate.readFd(sock);

    using ReadFdRes = MessagePrivate::ReadFdRes;
    switch (res) {
    case ReadFdRes::Success:
        handleBusMsg(std::move(readMsg));
        readMsg = Message();
        break;
    case ReadFdRes::Failure:
        updateState(State::Error);
        break;
    default:
        break;
    }
}

void ConnectionPrivate::doAuth()
{
    std::string line;
    char c;
    ssize_t n = read(sock, &c, 1);
    if (n <= 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return;

        Error() << __func__ << "Failed to read data";
        updateState(State::Error);
        return;
    }

    if (c != '\n') {
        if (c != '\r')
            authData += c;
        doAuth();
        return;
    }

    if (authData.rfind("OK", 0) != 0) {
        Error() << "Authentication failed: " << authData;
        updateState(State::Error);
        return;
    }

    Debug() << __func__ << "[auth] >>> BEGIN";
    const char *beginCmd = "BEGIN\r\n";
    if (write(sock, beginCmd, std::strlen(beginCmd)) == -1) {
        Error() << "Failed to send begin command";
        updateState(State::Error);
        return;
    }

    doGetName();
}

void ConnectionPrivate::doGetName()
{
    updateState(State::GettingName);
    Message msg;
    msg.setDestination("org.freedesktop.DBus");
    msg.setPath("/org/freedesktop/DBus");
    msg.setInterface("org.freedesktop.DBus");
    msg.setMember("Hello");
    auto pendingReply = parent->send(msg);
    if (!pendingReply) {
        Error() << __func__ << "Failed to send Hello msg";
        updateState(State::Error);
        return;
    }

    pendingReply->connect(PendingReply::Event::Finished, *parent, [this, reply = pendingReply.get()] () {
        if (state != State::GettingName)
            return;

        auto msg = reply->getMessage();
        if (!msg) {
            Error() << "GetName response is null";
            updateState(State::Error);
            return;
        }

        handleGetNameRespone(*msg);
    });
}

void ConnectionPrivate::handleGetNameRespone(Message &msg)
{
    if (msg.getType() == Message::Type::Error) {
        Error() << __func__ << "dbus call error:" << msg.getErrorName();
        updateState(State::Error);
        return;
    }

    msg >> serviceName;
    if (msg.valid()) {
        Debug() << __func__ << "Connect finished with name:" << serviceName;
        updateState(State::Connected);
        return;
    }

    Error() << __func__ << "failed to deserialize response";
    updateState(State::Error);
}

void ConnectionPrivate::handleBusMsg(Message &&msg)
{
    auto &pImpl = MessagePrivate::get(msg);
    using MType = Message::Type;
    switch (pImpl.fixedHeader.type) {
    case MType::MethodReturn:
    case MType::Error:
    {
        auto replySerial = pImpl.dynamicHeader.replySerial;
        if (!replySerial) {
            Error() << __func__ << "MethodReturn/Error without replySerial:" << msg.toString();
            break;
        }

        if (!replies.contains(*replySerial))
            break;

        auto reply = replies[*replySerial];
        auto &replyPrivate = PendingReplyPrivate::get(*reply);
        replyPrivate.setMessage(std::move(msg));
        replies.erase(*replySerial);
        break;
    }
    default:
        // Debug() << __func__ << msg.toString();
        break;
    }
}

std::shared_ptr<PendingReply> ConnectionPrivate::send(const Message &msg)
{
    if (state != State::GettingName && state != State::Connected) {
        Error() << "Trying to send msg while connection not ready:" << static_cast<int>(state);
        return {};
    }

    auto &pMsg = MessagePrivate::get(msg);
    while (replies.contains(nextSerial))
        ++nextSerial;

    pMsg.fixedHeader.serial = nextSerial++;
    auto bytes = pMsg.serialize();
    if (bytes.empty()) {
        Error() << "Failed to serialize message";
        return {};
    }

    if (write(sock, bytes.data(), bytes.size()) == -1) {
        Error() << "Failed to write data to socket";
        return {};
    }

    auto reply = std::make_shared<PendingReply>();
    replies[pMsg.fixedHeader.serial] = reply;
    return reply;
}

ConnectionPrivate &ConnectionPrivate::get(const Connection &connection)
{
    return *(connection.pImpl);
}

Connection::Connection(Type type)
    : pImpl(new ConnectionPrivate(this, type))
{
}

Connection::~Connection()
{
    delete pImpl;
}

Connection::State Connection::getState() const
{
    return pImpl->state;
}

Connection::Type Connection::getType() const
{
    return pImpl->type;
}

std::shared_ptr<PendingReply> Connection::send(const Message &msg)
{
    return pImpl->send(msg);
}

std::ostream &operator<<(std::ostream &os, Firmir::DBus::Connection::State state)
{
    using State = Firmir::DBus::Connection::State;
    switch (state) {
    case State::Connecting: os << "Connecting"; break;
    case State::Auth: os << "Auth"; break;
    case State::GettingName: os << "GettingName"; break;
    case State::Connected: os << "Connected"; break;
    case State::Error: os << "Error"; break;
    default: os << "Unknown"; break;
    }
    return os;
}

std::ostream &operator<<(std::ostream &os, Firmir::DBus::Connection::Type type)
{
    using Type = Firmir::DBus::Connection::Type;
    switch (type) {
    case Type::Session: os << "Session"; break;
    case Type::System: os << "System"; break;
    default: os << "Unknown"; break;
    }
    return os;
}

} // namespace DBus
} // namespace Firmir
