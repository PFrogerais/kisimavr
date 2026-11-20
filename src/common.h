/*
 * kisimavr - ATmega co-simulation for ngspice / KiCad
 * Copyright (C) 2026 Paul
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file common.h
 * @brief Small helpers shared by every module of the plug-in.
 */

#ifndef KISIMAVR_COMMON_H
#define KISIMAVR_COMMON_H

#include <cctype>
#include <string>

namespace kisimavr
{

/** Prefix of every message printed on stderr. */
constexpr const char *kTag = "[mcu_cosim]";

inline std::string toLower(std::string s)
{
    for (char &c : s)
    {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

} // namespace kisimavr

#endif // KISIMAVR_COMMON_H
