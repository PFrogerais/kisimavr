/*
 * kisimavr - ATmega co-simulation for ngspice / KiCad
 * Copyright (C) 2026 Paul
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file hd44780.cpp
 * @brief HD44780 character LCD controller, without simavr.
 */

#include "hd44780.h"

#include <algorithm>
#include <cstdio>

namespace kisimavr
{

namespace
{

constexpr double kPowerUp   = 15e-3;     // Wait after VCC rises above 4.5 V.
constexpr double kExecShort = 37e-6;     // Most instructions and data writes.
constexpr double kExecLong  = 1.52e-3;   // Clear display, return home.
constexpr int    kLineLen   = 40;        // DDRAM length of one line (2-line mode).

enum WarnKind
{
    kWarnPowerUp = 0,
    kWarnBusy,
    kWarnRead,
};

/** x modulo n, always in [0, n). */
int wrap(int x, int n)
{
    return ((x % n) + n) % n;
}

/** Character ROM A00 (the common one) as UTF-8; CGRAM codes as '#'. */
std::string glyph(uint8_t code)
{
    if (code < 0x10u)
    {
        return "#";                                 // User characters 0..7 (and 8..15).
    }
    switch (code)
    {
    case 0x5Cu: return "\xC2\xA5";                  // Yen sign.
    case 0x7Eu: return "\xE2\x86\x92";              // Right arrow.
    case 0x7Fu: return "\xE2\x86\x90";              // Left arrow.
    case 0xDFu: return "\xC2\xB0";                  // Degree sign.
    case 0xE4u: return "\xC2\xB5";                  // Micro sign.
    case 0xF4u: return "\xCE\xA9";                  // Omega.
    default:    break;
    }
    if (code >= 0x20u && code < 0x80u)
    {
        return std::string(1, static_cast<char>(code));
    }
    return "?";                                     // Katakana and symbols.
}

} // namespace

Hd44780::Hd44780(int cols, int rows)
    : m_cols(std::clamp(cols, 8, 40)), m_rows(std::clamp(rows, 1, 4))
{
    m_ddram.fill(0x20u);
}

void Hd44780::warnOnce(int kind, const std::string &text)
{
    const unsigned int bit = 1u << kind;
    if ((m_warned & bit) == 0u)
    {
        m_warned |= bit;
        m_warnings.push_back(text);
    }
}

void Hd44780::strobe(bool rs, bool rw, uint8_t bus, double t)
{
    if (rw)
    {
        warnOnce(kWarnRead, "read cycle (R/W = 1) not modelled: tie R/W to GND");
        return;
    }
    if (m_eightBit)
    {
        execute(rs, bus, t);
        return;
    }
    // 4-bit interface: high nibble first, on D7..D4.
    const auto nibble = static_cast<uint8_t>(bus >> 4);
    if (m_highNibble)
    {
        m_nibble     = nibble;
        m_highNibble = false;
        return;
    }
    m_highNibble = true;
    execute(rs, static_cast<uint8_t>((m_nibble << 4) | nibble), t);
}

void Hd44780::execute(bool rs, uint8_t value, double t)
{
    char text[160];
    if (!m_started)
    {
        m_started = true;
        if (t < kPowerUp)
        {
            std::snprintf(text, sizeof text,
                          "first instruction at %.3f ms: the HD44780 needs 15 ms after "
                          "power-up (40 ms if VCC = 2.7 V)", t * 1e3);
            warnOnce(kWarnPowerUp, text);
        }
    }
    else if (t < m_busyUntil)
    {
        std::snprintf(text, sizeof text,
                      "%s sent %.1f us after the previous one, which needs %.0f us "
                      "(busy): it may be lost on a real display",
                      rs ? "data" : "instruction", (t - (m_busyUntil - m_busyFor)) * 1e6,
                      m_busyFor * 1e6);
        warnOnce(kWarnBusy, text);
    }

    if (rs)
    {
        writeData(value);
        m_busyFor = kExecShort;
    }
    else
    {
        instruction(value, t);
    }
    m_busyUntil = t + m_busyFor;
    ++m_version;
}

int Hd44780::rowBase(int row) const
{
    // 16x4 / 20x4: rows 2 and 3 continue rows 0 and 1 in DDRAM.
    static const int kBase[4] = { 0x00, 0x40, 0x00, 0x40 };
    return kBase[row] + ((row >= 2) ? m_cols : 0);
}

void Hd44780::moveCursor(bool right)
{
    if (m_cgMode)
    {
        m_address = static_cast<uint8_t>((m_address + (right ? 1 : 63)) & 0x3Fu);
        return;
    }
    if (!m_twoLines)
    {
        // One line: addresses 0x00..0x4F.
        int a = m_address + (right ? 1 : -1);
        m_address = static_cast<uint8_t>((a + 80) % 80);
        return;
    }
    // Two lines: 0x00..0x27 then 0x40..0x67.
    int a = m_address;
    if (right)
    {
        a = (a == 0x27) ? 0x40 : ((a == 0x67) ? 0x00 : a + 1);
    }
    else
    {
        a = (a == 0x40) ? 0x27 : ((a == 0x00) ? 0x67 : a - 1);
    }
    m_address = static_cast<uint8_t>(a);
}

void Hd44780::writeData(uint8_t value)
{
    if (m_cgMode)
    {
        m_cgram[m_address & 0x3Fu] = value;
    }
    else
    {
        m_ddram[m_address & 0x7Fu] = value;
    }
    moveCursor(m_increment);
    if (m_shiftOn && !m_cgMode)
    {
        m_shift += m_increment ? 1 : -1;    // The display follows the cursor.
    }
}

void Hd44780::instruction(uint8_t v, double /*t*/)
{
    m_busyFor = kExecShort;
    if (v & 0x80u)                                   // Set DDRAM address.
    {
        m_address = static_cast<uint8_t>(v & 0x7Fu);
        m_cgMode  = false;
    }
    else if (v & 0x40u)                              // Set CGRAM address.
    {
        m_address = static_cast<uint8_t>(v & 0x3Fu);
        m_cgMode  = true;
    }
    else if (v & 0x20u)                              // Function set: DL N F.
    {
        m_eightBit   = (v & 0x10u) != 0u;
        m_twoLines   = (v & 0x08u) != 0u;
        m_highNibble = true;
    }
    else if (v & 0x10u)                              // Cursor / display shift.
    {
        const bool right = (v & 0x04u) != 0u;
        if (v & 0x08u)
        {
            m_shift += right ? -1 : 1;               // Display moves right.
        }
        else
        {
            moveCursor(right);
        }
    }
    else if (v & 0x08u)                              // Display on/off: D C B.
    {
        m_displayOn = (v & 0x04u) != 0u;
    }
    else if (v & 0x04u)                              // Entry mode set: I/D S.
    {
        m_increment = (v & 0x02u) != 0u;
        m_shiftOn   = (v & 0x01u) != 0u;
    }
    else if (v & 0x02u)                              // Return home.
    {
        m_address = 0;
        m_cgMode  = false;
        m_shift   = 0;
        m_busyFor = kExecLong;
    }
    else if (v & 0x01u)                              // Clear display.
    {
        m_ddram.fill(0x20u);
        m_address   = 0;
        m_cgMode    = false;
        m_shift     = 0;
        m_increment = true;
        m_busyFor   = kExecLong;
    }
}

std::vector<std::string> Hd44780::lines() const
{
    std::vector<std::string> out;
    for (int row = 0; row < m_rows; ++row)
    {
        std::string text;
        for (int col = 0; col < m_cols; ++col)
        {
            int address = 0;
            if (m_twoLines)
            {
                // Each line scrolls on its own 40 positions.
                const int base = rowBase(row);
                address = (base & 0x40) + wrap((base & 0x3F) + col + m_shift, kLineLen);
            }
            else
            {
                address = wrap(row * m_cols + col + m_shift, 2 * kLineLen);
            }
            text += glyph(m_ddram[static_cast<size_t>(address) & 0x7Fu]);
        }
        out.push_back(text);
    }
    return out;
}

} // namespace kisimavr
