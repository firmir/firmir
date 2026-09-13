/**
 * SPDX-FileCopyrightText: 2026 Mikhail Firsov <firsov62121@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "firmir/dbus/message_p.h"
#include "firmir/logging.h"

namespace Firmir {
namespace DBus {

MessagePrivate::MessagePrivate(Message *parent, Type type)
    : parent(parent)
    , type(type)
{
}

MessagePrivate::~MessagePrivate()
{
}

std::vector<std::byte> MessagePrivate::serialize()
{
    // TODO
    return {};
}

MessagePrivate &MessagePrivate::get(const Message &msg)
{
    return *(msg.pImpl);
}

Message::Message(Type type)
    : pImpl(new MessagePrivate(this, type))
{
}

Message::~Message()
{
    delete pImpl;
}

Message::Type Message::getType() const
{
    return pImpl->type;
}

void Message::setType(Type value)
{
    pImpl->type = value;
}

std::string Message::getService() const
{
    return pImpl->service;
}

void Message::setService(const std::string &value)
{
    pImpl->service = value;
}

std::string Message::getPath() const
{
    return pImpl->path;
}

void Message::setPath(const std::string &value)
{
    pImpl->path = value;
}

std::string Message::getInterface() const
{
    return pImpl->interface;
}

void Message::setInterface(const std::string &value)
{
    pImpl->interface = value;
}

std::string Message::getMember() const
{
    return pImpl->member;
}

void Message::setMember(const std::string &value)
{
    pImpl->member = value;
}

bool Message::deserializeResult() const
{
    return pImpl->deserializeResult;
}

Message &Message::operator>>(std::string &item)
{
    
}

} // namespace DBus
} // namespace Firmir