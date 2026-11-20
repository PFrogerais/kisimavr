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
 *   uartin  timed text sent to the UART0 receiver (see uart_script.h)
 *   rxpin   pin whose frames are decoded by the receiver (default PD0)
 *   vcd     VCD file of the GPIO pins, .vcd, for GTKWave / PulseView
 *           (output files are never written through a symbolic link, and an
 *           existing file is only replaced if mcu_cosim wrote it)
 *   i2c     virtual I2C slaves: "lm75" ["0x48"] "eeprom" ["0x50"]
 *   lm75temp / lm75rate  LM75 temperature at t = 0 (degC) and slope (degC/s)
 *   i2clog  0 to hide the I2C transactions (default 1)
 *   spilog  0 to hide the SPI transfers (default 1); SPI master on
 *           SCK = PB5, MOSI = PB3, MISO = PB4 ("io" pins), slaves in the circuit
 *   sda / scl  TWI pins rebuilt as open-drain outputs (default PC4 / PC5)
 *   log   1 to print pin activity on stderr (default 0)
 *
 * With "device" "hd44780" the instance is a character LCD instead of an
 * MCU (see lcd_device.h): 11 inputs RS RW E D0..D7, keys cols, rows,
 * lcdlog, name.
 *
 * Modules:
 *   config.*      sim_args parsing, port counts
 *   paths.*       KiCad lower-case path recovery, guarded output files
 *   mcu_bridge.*  simavr instance <-> d_cosim ports, timing, GPIO
 *   sar_adc.*     SAR converter rebuilt across the d_cosim boundary
 *   uart0.*       UART0 console, log file, TXD frames and reception
 *   uart_script.* "uartin" file parser (simavr independent)
 *   i2c_bus.*     TWI master: virtual slaves, transaction log, SDA/SCL
 *   i2c_devices.* LM75 and 24C02 protocol logic (simavr independent)
 *   vcd.*         VCD trace of the pins
 *   spi_bus.*     SPI master on the pins; spi_frame.* edge timing
 *   lcd_device.*  HD44780 LCD as a circuit part; hd44780.* controller logic
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
#include "lcd_device.h"
#include "mcu_bridge.h"

#define COSIM_EXPORT extern "C" __attribute__((visibility("default")))

namespace
{

using kisimavr::CosimDevice;

CosimDevice *bridgeOf(co_info *info)
{
    return static_cast<CosimDevice *>(info->handle);
}

void cosimStep(co_info *info)
{
    bridgeOf(info)->step(info);
}

void cosimInput(co_info *info, unsigned int index, Digital_t *value)
{
    bridgeOf(info)->input(info, index, value);
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

    info->inout_count = 0;
    info->method      = Normal;

    std::unique_ptr<CosimDevice> bridge;
    bool ok = false;
    if (kisimavr::isDeviceConfig(args))
    {
        // A circuit device (character LCD) instead of an MCU.
        kisimavr::LcdConfig lcd;
        ok = kisimavr::parseLcdConfig(args, lcd);
        info->in_count  = kisimavr::LcdDevice::kInputs;
        info->out_count = 0;
        if (ok)
        {
            bridge = std::make_unique<kisimavr::LcdDevice>(std::move(lcd));
        }
    }
    else
    {
        kisimavr::Config cfg;
        const bool parsed = kisimavr::parseConfig(args, cfg);
        info->in_count  = kisimavr::inputPortCount(cfg);
        info->out_count = kisimavr::outputPortCount(cfg);
        auto mcu = std::make_unique<kisimavr::McuBridge>(std::move(cfg));
        ok = parsed && mcu->start();
        bridge = std::move(mcu);
    }
    if (!ok)
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
