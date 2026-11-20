/*
 * kisimavr - ATmega co-simulation for ngspice / KiCad
 * Copyright (C) 2026 Paul
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file mcu_cosim.cpp
 * @brief ngspice d_cosim plug-in running AVR firmware (.hex) with simavr.
 *
 * The shared library is loaded by the XSPICE d_cosim code model:
 *
 *   amcu [pd2_d] [pb5_d pb0_d] null mcu
 *   .model mcu d_cosim simulation="mcu_cosim.so"
 *   + sim_args=["hex" "blink.hex" "mcu" "atmega328p" "freq" "16000000"
 *   +           "in" "PD2" "out" "PB5" "PB0"]
 *
 * Each key is followed by its value(s) as separate strings (see config.h).
 *
 * Keys:
 *   hex   Intel HEX firmware file (required)
 *   mcu   simavr core name, e.g. atmega328p (default atmega328p)
 *   freq  CPU clock in Hz (default 16000000)
 *   in    pins driven by the circuit, e.g. "PD2" "PD3"
 *   out   pins driven by the MCU, e.g. "PB5" "PB0"
 *   io    bidirectional pins, direction set by the firmware (DDRx): each
 *         one has 1 input (pin voltage read back) and 3 outputs
 *         (level = PORT bit, oe = DDR bit, pu = pull-up enabled)
 *   adc   analog channels measured by the SAR model, e.g. "0" "1"
 *   vref  DAC full scale in volts (default 5), i.e. AVCC
 *   adcbit  duration of one SAR step in seconds (default 1e-6)
 *   txpin   output pin that carries the UART0 TX frames (default PD1)
 *   uartlog file receiving the UART0 text, .txt or .log (default: none)
 *   vcd     VCD file of the GPIO pins, .vcd, for GTKWave / PulseView
 *           (output files are never written through a symbolic link, and an
 *           existing file is only replaced if mcu_cosim wrote it)
 *   i2c     virtual I2C slaves: "lm75" ["0x48"] "eeprom" ["0x50"]
 *   lm75temp / lm75rate  LM75 temperature at t = 0 (degC) and slope (degC/s)
 *   i2clog  0 to hide the I2C transactions (default 1)
 *   sda / scl  TWI pins rebuilt as open-drain outputs (default PC4 / PC5)
 *   log   1 to print pin activity on stderr (default 0)
 *
 * Modules:
 *   config.*      sim_args parsing, port counts
 *   paths.*       KiCad lower-case path recovery, guarded output files
 *   mcu_bridge.*  simavr instance <-> d_cosim ports, timing, GPIO
 *   sar_adc.*     SAR converter rebuilt across the d_cosim boundary
 *   uart0.*       UART0 console, log file and TXD frames
 *   i2c_bus.*     TWI master: virtual slaves, transaction log, SDA/SCL
 *   i2c_devices.* LM75 and 24C02 protocol logic (simavr independent)
 *   vcd.*         VCD trace of the pins
 * This file only holds the d_cosim entry point.
 */

#include <cstdio>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "common.h"
#include "config.h"
#include "cosim_abi.h"
#include "mcu_bridge.h"

#define COSIM_EXPORT extern "C" __attribute__((visibility("default")))

namespace
{

using kisimavr::McuBridge;

McuBridge *bridgeOf(co_info *info)
{
    return static_cast<McuBridge *>(info->handle);
}

void cosimStep(co_info *info)
{
    bridgeOf(info)->step(info);
}

void cosimInput(co_info *info, unsigned int index, Digital_t *value)
{
    bridgeOf(info)->input(index, value);
}

void cosimCleanup(co_info *info)
{
    delete bridgeOf(info);
    info->handle = nullptr;
}

void idleStep(co_info * /*info*/)
{
    // Configuration failed: keep outputs frozen.
}

void idleInput(co_info * /*info*/, unsigned int /*index*/, Digital_t * /*value*/)
{
}

} // namespace

COSIM_EXPORT void Cosim_setup(co_info *info)
{
    std::vector<std::string> args;
    for (unsigned int i = 0; i < info->sim_argc; ++i)
    {
        args.emplace_back(info->sim_argv[i] != nullptr ? info->sim_argv[i] : "");
    }

    kisimavr::Config cfg;
    const bool parsed = kisimavr::parseConfig(args, cfg);

    info->in_count    = kisimavr::inputPortCount(cfg);
    info->out_count   = kisimavr::outputPortCount(cfg);
    info->inout_count = 0;
    info->method      = Normal;

    auto bridge = std::make_unique<McuBridge>(std::move(cfg));
    if (!parsed || !bridge->start())
    {
        std::fprintf(stderr, "%s setup failed: MCU outputs stay at their initial value\n",
                     kisimavr::kTag);
        info->handle  = nullptr;
        info->step    = idleStep;
        info->in_fn   = idleInput;
        info->cleanup = nullptr;
        return;
    }

    info->handle  = bridge.release();
    info->step    = cosimStep;
    info->in_fn   = cosimInput;
    info->cleanup = cosimCleanup;
}
