/*
 * kisimavr - ATmega co-simulation for ngspice / KiCad
 * Copyright (C) 2026 Paul
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file hd44780.h
 * @brief HD44780 character LCD controller, without simavr (unit-tested).
 *
 * Fed with one bus transfer per falling edge of E (RS, R/W, D7..D0):
 * 8-bit and 4-bit interfaces (the "0x3, 0x3, 0x3, 0x2" reset sequence of
 * LiquidCrystal and of the datasheet), clear, home, entry mode, display
 * on/off, cursor and display shift, function set, CGRAM (stored, shown as
 * '#'), DDRAM. Displays of 8x1 to 40x2 and 16x4 / 20x4.
 *
 * Datasheet checks, reported as warnings (each kind once):
 *   - first instruction less than 15 ms after power-up;
 *   - instruction or data while busy (37 us, 1.52 ms for clear / home);
 *   - read cycles (R/W = 1): not modelled, tie R/W to GND.
 */

#ifndef KISIMAVR_HD44780_H
#define KISIMAVR_HD44780_H

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace kisimavr
{

class Hd44780
{
public:
    Hd44780(int cols, int rows);

    /**
     * One transfer latched by a falling edge of E at time t (seconds).
     * bus = D7..D0 (in 4-bit mode only D7..D4 are used).
     */
    void strobe(bool rs, bool rw, uint8_t bus, double t);

    /** Visible text, one string per row (UTF-8: degree sign, yen...). */
    std::vector<std::string> lines() const;

    bool displayOn() const { return m_displayOn; }
    bool fourBit() const { return !m_eightBit; }
    int  cols() const { return m_cols; }
    int  rows() const { return m_rows; }

    /** Incremented whenever the visible content may have changed. */
    unsigned long version() const { return m_version; }

    /** Warnings produced by the last strobe() calls; cleared by the caller. */
    std::vector<std::string> &warnings() { return m_warnings; }

private:
    void execute(bool rs, uint8_t value, double t);
    void instruction(uint8_t value, double t);
    void writeData(uint8_t value);
    void moveCursor(bool right);
    int  rowBase(int row) const;
    void warnOnce(int kind, const std::string &text);

    int  m_cols;
    int  m_rows;

    std::array<uint8_t, 128> m_ddram{};
    std::array<uint8_t, 64>  m_cgram{};
    uint8_t m_address   = 0;       // DDRAM or CGRAM address counter.
    bool    m_cgMode    = false;   // Last "set address" was CGRAM.
    bool    m_increment = true;    // I/D
    bool    m_shiftOn   = false;   // S: shift the display on write.
    int     m_shift     = 0;       // Display shift (DDRAM positions).
    bool    m_displayOn = false;
    bool    m_eightBit  = true;
    bool    m_twoLines  = false;
    bool    m_highNibble = true;   // 4-bit: next transfer is D7..D4.
    uint8_t m_nibble    = 0;

    bool    m_started   = false;
    double  m_busyUntil = 0.0;
    double  m_busyFor   = 0.0;     // Duration of the instruction in progress.
    unsigned long m_version = 0;
    unsigned int  m_warned  = 0;   // Bit per warning kind.
    std::vector<std::string> m_warnings;
};

} // namespace kisimavr

#endif // KISIMAVR_HD44780_H
