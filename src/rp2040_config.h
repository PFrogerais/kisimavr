/*
 * kisimavr - ATmega co-simulation for ngspice / KiCad
 * Copyright (C) 2026 Paul
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file rp2040_config.h
 * @brief sim_args of an RP2040 or RP2350 instance ("mcu" "rp2040" or
 *        "mcu" "rp2350"), without QuickJS.
 *
 * Keys (each followed by its value(s) as separate strings):
 *   mcu      rp2040 (Pico, rp2040js) or rp2350 (Pico 2, rp2350js)
 *   fw       firmware: .uf2, .bin (flash image) or .hex (required)
 *   arch     RP2350 only: riscv or arm, when the firmware does not tell
 *            (UF2 family ID, IMAGE_DEF block); default riscv
 *   io       bidirectional GPIOs, e.g. "GP0" "GP1" "GP25" (or "0" "1" "25")
 *   adc      ADC channels measured by the SAR model: 0..3 = GP26..GP29
 *   uartlog  file receiving the UART0 / UART1 / USB text, .txt or .log
 *   pty      UART0 on a pseudo-terminal (pty_port.h): "1", or the path of a
 *            symbolic link to it, e.g. "~/kisimavr_uart0"
 *   ptyusb   the USB serial port (Serial of Arduino-pico) on a pseudo-terminal
 *   bridge   rp2040/bridge.js or rp2040/rp2350_bridge.js (default: next
 *            to the plug-in, ../rp2040/)
 *   log      1 to print the time of each run on stderr
 * Ports: inputs = io voltages read back, then one comparator per ADC
 * channel; outputs = (level, oe, pu, pd) per io pin, then 12 DAC bits.
 */

#ifndef KISIMAVR_RP2040_CONFIG_H
#define KISIMAVR_RP2040_CONFIG_H

#include <string>
#include <vector>

namespace kisimavr
{

struct Rp2040Config
{
    std::string      chip = "rp2040";   // "rp2040" or "rp2350"
    std::string      arch;              // RP2350: "riscv", "arm" or "" (from the firmware)
    std::string      fwPath;
    std::vector<int> ios;        // GPIO numbers 0..29
    std::vector<int> adc;        // Channels 0..3
    std::string      uartLog;
    std::string      pty;               // UART0 on a pseudo-terminal
    std::string      ptyUsb;            // USB CDC on a pseudo-terminal
    std::string      bridgePath;
    bool             log = false;
};

constexpr int kRp2040DacBits = 12;

/** True when sim_args contain "mcu" "rp2040" or "mcu" "rp2350". */
bool isRp2040Config(const std::vector<std::string> &args);

/** "GP5", "gp5" or "5" -> 5; -1 if invalid. */
int parseGpio(const std::string &text);

/** Parse the keys above; errors on stderr. */
bool parseRp2040Config(const std::vector<std::string> &args, Rp2040Config &cfg);

/** d_cosim port counts. */
unsigned int rp2040InputCount(const Rp2040Config &cfg);
unsigned int rp2040OutputCount(const Rp2040Config &cfg);

} // namespace kisimavr

#endif // KISIMAVR_RP2040_CONFIG_H
