/**
 * SPDX-FileCopyrightText: 2026 Mikhail Firsov <firsov62121@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "visibility.h"

#include <string>
#include <iostream>

namespace Firmir {

class FIRMIR_EXPORT_SYMBOL Logger
{
public:
    enum class Category { Debug, Warning, Error, Info };

    Logger(Category category);
    ~Logger();

    template <typename T>
    inline Logger &operator<<(T value)
    {
        if (enabled()) {
            auto &out = m_category == Category::Error ? std::cerr : std::cout;
            out << value << ' ';
        }
        return *this;
    }

private:
    bool enabled();

    Category m_category;
};

class FIRMIR_EXPORT_SYMBOL Debug : public Logger
{
public:
    Debug();
};

class FIRMIR_EXPORT_SYMBOL Warning : public Logger
{
public:
    Warning();
};

class FIRMIR_EXPORT_SYMBOL Error : public Logger
{
public:
    Error();
};

class FIRMIR_EXPORT_SYMBOL Info : public Logger
{
public:
    Info();
};

} // namespace Firmir
