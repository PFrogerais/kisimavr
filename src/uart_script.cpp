/*
 * kisimavr - ATmega co-simulation for ngspice / KiCad
 * Copyright (C) 2026 Paul
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file uart_script.cpp
 * @brief Timed text sent to the UART receiver of the MCU ("uartin" file).
 */

#include "uart_script.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <sstream>

#include "common.h"

namespace kisimavr
{

namespace
{

bool isBlank(char c)
{
    return c == ' ' || c == '\t';
}

int hexDigit(char c)
{
    if (c >= '0' && c <= '9')
    {
        return c - '0';
    }
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return (c >= 'a' && c <= 'f') ? (c - 'a' + 10) : -1;
}

bool decodeEscapes(const std::string &in, std::string &out, std::string &why)
{
    out.clear();
    for (size_t i = 0; i < in.size(); ++i)
    {
        if (in[i] != '\\')
        {
            out.push_back(in[i]);
            continue;
        }
        if (i + 1 >= in.size())
        {
            why = "'\\' at the end of the line";
            return false;
        }
        const char e = in[++i];
        switch (e)
        {
        case 'n':  out.push_back('\n'); break;
        case 'r':  out.push_back('\r'); break;
        case 't':  out.push_back('\t'); break;
        case 's':  out.push_back(' ');  break;
        case '\\': out.push_back('\\'); break;
        case 'x':
        {
            const int hi = (i + 1 < in.size()) ? hexDigit(in[i + 1]) : -1;
            const int lo = (i + 2 < in.size()) ? hexDigit(in[i + 2]) : -1;
            if (hi < 0 || lo < 0)
            {
                why = "\\x needs two hexadecimal digits";
                return false;
            }
            out.push_back(static_cast<char>(hi * 16 + lo));
            i += 2;
            break;
        }
        default:
            why = std::string("unknown escape '\\") + e + "'";
            return false;
        }
    }
    return true;
}

} // namespace

bool parseSpiceTime(const std::string &text, double &seconds)
{
    if (text.empty())
    {
        return false;
    }
    char *end = nullptr;
    const double value = std::strtod(text.c_str(), &end);
    if (end == text.c_str() || !std::isfinite(value))
    {
        return false;
    }

    const std::string suffix = toLower(end);
    double scale = 1.0;
    if (suffix.empty() || suffix == "s")
    {
        scale = 1.0;
    }
    else if (suffix == "m" || suffix == "ms")
    {
        scale = 1e-3;
    }
    else if (suffix == "u" || suffix == "us")
    {
        scale = 1e-6;
    }
    else if (suffix == "n" || suffix == "ns")
    {
        scale = 1e-9;
    }
    else if (suffix == "p" || suffix == "ps")
    {
        scale = 1e-12;
    }
    else if (suffix == "f" || suffix == "fs")
    {
        scale = 1e-15;
    }
    else
    {
        return false;    // Includes "meg", "k", "g": not a simulation time.
    }

    seconds = value * scale;
    return seconds >= 0.0;
}

bool parseUartScript(const std::string &content, std::vector<UartEvent> &events,
                     std::string &error)
{
    events.clear();
    std::istringstream in(content);
    std::string line;
    int number = 0;

    while (std::getline(in, line))
    {
        ++number;
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();    // Windows line endings.
        }
        while (!line.empty() && isBlank(line.back()))
        {
            line.pop_back();
        }
        size_t p = 0;
        while (p < line.size() && isBlank(line[p]))
        {
            ++p;
        }
        if (p == line.size() || line[p] == '#')
        {
            continue;    // Empty or comment.
        }

        size_t q = p;
        while (q < line.size() && !isBlank(line[q]))
        {
            ++q;
        }
        UartEvent ev{};
        if (!parseSpiceTime(line.substr(p, q - p), ev.time))
        {
            error = "line " + std::to_string(number) + ": invalid time '" +
                    line.substr(p, q - p) + "'";
            return false;
        }
        while (q < line.size() && isBlank(line[q]))
        {
            ++q;
        }
        std::string why;
        if (!decodeEscapes(line.substr(q), ev.bytes, why))
        {
            error = "line " + std::to_string(number) + ": " + why;
            return false;
        }
        if (ev.bytes.empty())
        {
            error = "line " + std::to_string(number) + ": no text after the time";
            return false;
        }
        events.push_back(ev);
    }

    std::stable_sort(events.begin(), events.end(),
                     [](const UartEvent &a, const UartEvent &b) { return a.time < b.time; });
    return true;
}

} // namespace kisimavr
