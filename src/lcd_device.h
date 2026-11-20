/*
 * kisimavr - ATmega co-simulation for ngspice / KiCad
 * Copyright (C) 2026 Paul
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file lcd_device.h
 * @brief Character LCD (HD44780) as a circuit part: sim_args "device" "hd44780".
 *
 * A second d_cosim instance of the same plug-in, wired to the circuit like
 * the real module. 11 inputs, no output:
 *   RS  R/W  E  D0 D1 D2 D3 D4 D5 D6 D7
 * Keys:
 *   device  hd44780 (required)
 *   cols / rows  display size (default 16 x 2)
 *   lcdlog  file receiving every displayed screen, .txt or .log
 *   name    label of the messages (default LCD)
 *
 * When the screen stays unchanged for 1 ms after a write, it is printed:
 *   [mcu_cosim] t=0.021000 s LCD> [Bonjour STI2D   ] [A0 = 2.50 V     ]
 * Timing checks (each kind once): E pulse shorter than 230 ns, data set up
 * less than 80 ns before the falling edge of E, and those of Hd44780.
 */

#ifndef KISIMAVR_LCD_DEVICE_H
#define KISIMAVR_LCD_DEVICE_H

#include <array>
#include <cstdio>
#include <string>
#include <vector>

#include "cosim_device.h"
#include "hd44780.h"

namespace kisimavr
{

struct LcdConfig
{
    int         cols = 16;
    int         rows = 2;
    std::string logPath;
    std::string name = "LCD";
};

/** True when sim_args select a device instead of an MCU ("device" key). */
bool isDeviceConfig(const std::vector<std::string> &args);

/** Parse the LCD keys; errors on stderr. */
bool parseLcdConfig(const std::vector<std::string> &args, LcdConfig &cfg);

class LcdDevice : public CosimDevice
{
public:
    static constexpr unsigned int kInputs = 11;    // RS RW E D0..D7

    explicit LcdDevice(LcdConfig cfg);
    ~LcdDevice() override;

    void step(co_info *info) override;
    void input(co_info *info, unsigned int index, const Digital_t *value) override;

    /** Print the screen if it changed and is stable at time t. */
    void flush(double t, bool force);

    const Hd44780 &controller() const { return m_lcd; }

private:
    void fallingEdge(double t);
    void report(const std::vector<std::string> &warnings);
    void warnOnce(int kind, const std::string &text);

    LcdConfig      m_cfg;
    Hd44780        m_lcd;
    std::FILE     *m_file = nullptr;
    std::array<bool, kInputs> m_level{};
    double         m_eRise      = -1.0;
    double         m_dataChange = 0.0;
    double         m_lastWrite  = 0.0;
    unsigned long  m_shownVersion = 0;
    std::string    m_shown;
    unsigned int   m_warned = 0;
};

} // namespace kisimavr

#endif // KISIMAVR_LCD_DEVICE_H
