/*
 * kisimavr - ATmega co-simulation for ngspice / KiCad
 * Copyright (C) 2026 Paul
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file config.h
 * @brief Configuration of one MCU instance, parsed from the d_cosim sim_args.
 *
 * Each key is followed by its value(s) as separate strings. Avoid '=' and
 * ':' in netlists: ngspice numparam evaluates "key=value" inside sim_args.
 * The compact forms "key=value" / "key:value" are still accepted (runner).
 * A lone value before any key is taken as the firmware path.
 *
 * Port order on the d_cosim instance:
 *   inputs  = "in" pins, then "io" pin reads, then one comparator per "adc"
 *   outputs = "out" pins, then (level, oe, pu) per "io" pin, then b0..b9
 */

#ifndef KISIMAVR_CONFIG_H
#define KISIMAVR_CONFIG_H

#include <cstdint>
#include <string>
#include <vector>

#include "i2c_devices.h"

namespace kisimavr
{

/** Width of the DAC of the SAR model (outputs b0..b9). */
constexpr int kDacBits = 10;

/** One GPIO pin, e.g. PB5 -> {'B', 5}. */
struct PinRef
{
    char    port;
    uint8_t bit;
};

inline bool operator==(const PinRef &a, const PinRef &b)
{
    return a.port == b.port && a.bit == b.bit;
}

/** One virtual I2C slave of the "i2c" key. */
struct I2cSlaveConfig
{
    I2cDevice::Type type;
    int             address;    // 7 bits.
};

/** Configuration parsed from sim_args. */
struct Config
{
    std::string         hexPath;
    std::string         mcuName = "atmega328p";
    uint32_t            frequency = 16000000u;
    std::vector<PinRef> inputs;
    std::vector<PinRef> outputs;
    std::vector<PinRef> ios;
    std::vector<int>    adcChannels;
    double              vref   = 5.0;
    double              adcBit = 1e-6;
    std::string         txPin   = "PD1";
    std::string         uartLog;
    std::string         uartIn;               // Timed text for the receiver.
    std::string         pty;                  // UART0 on a pseudo-terminal: "1" or a link path
    std::string         rxPin    = "PD0";     // RXD frames decoded from the circuit.
    std::string         vcdPath;
    std::vector<I2cSlaveConfig> i2cSlaves;    // "i2c" "lm75" ["0x48"] "eeprom" ...
    double              lm75Temp = 25.0;      // degC at t = 0
    double              lm75Rate = 0.0;       // degC per second
    bool                i2cLog   = true;
    std::string         sdaPin   = "PC4";
    std::string         sclPin   = "PC5";
    bool                spiLog   = true;      // "SPI> MOSI .. | MISO .." lines.
    bool                log = false;
};

/** Parse "PB5" or "B5" (case insensitive). */
bool parsePin(const std::string &text, PinRef &pin);

/**
 * Fill cfg from the sim_args strings. Errors are reported on stderr;
 * returns false if the configuration cannot be used.
 */
bool parseConfig(const std::vector<std::string> &args, Config &cfg);

/** Number of d_cosim ports implied by a configuration. */
unsigned int inputPortCount(const Config &cfg);
unsigned int outputPortCount(const Config &cfg);

} // namespace kisimavr

#endif // KISIMAVR_CONFIG_H
