/*
 * kisimavr - ATmega co-simulation for ngspice / KiCad
 * Copyright (C) 2026 Paul
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file i2c_devices.h
 * @brief Virtual I2C slaves: LM75 temperature sensor and 24C02 EEPROM.
 *
 * Pure protocol logic, independent of simavr (unit-tested):
 *   - start()  : the slave has been addressed (START or repeated START);
 *   - write(b) : the master sends a byte (the first one sets the pointer);
 *   - read(t)  : the master reads a byte at simulation time t (seconds).
 *
 * LM75: pointer 0 = temperature (2 bytes, 9 bits, 0.5 degC steps, two's
 * complement, left-justified), 1 = configuration (1 byte), 2 = Thyst and
 * 3 = Tos (2 bytes, 75 / 80 degC at power-up). Temperature = temp0 +
 * rate * t, clamped to the -55 .. +125 degC range of the part.
 *
 * 24C02: 256 bytes, erased to 0xFF, first written byte = memory address,
 * then sequential write or read with wrap-around.
 */

#ifndef KISIMAVR_I2C_DEVICES_H
#define KISIMAVR_I2C_DEVICES_H

#include <array>
#include <cstdint>
#include <string>

namespace kisimavr
{

class I2cDevice
{
public:
    enum class Type
    {
        Lm75,
        Eeprom24c02
    };

    I2cDevice(Type type, int address, double temp0 = 25.0, double rate = 0.0);

    Type type() const { return m_type; }
    int address() const { return m_address; }    // 7-bit address.
    const char *name() const;

    void start();
    void write(uint8_t value);
    uint8_t read(double timeSeconds);

    /** LM75 temperature register value (16 bits) at a given temperature. */
    static uint16_t lm75Word(double celsius);

    /** Default 7-bit address of a device type. */
    static int defaultAddress(Type type);

    /** "lm75" / "eeprom" -> type; false for an unknown name. */
    static bool typeFromName(const std::string &name, Type &type);

private:
    Type                     m_type;
    int                      m_address;
    double                   m_temp0;
    double                   m_rate;
    uint8_t                  m_pointer = 0;
    int                      m_index   = 0;    // Bytes transferred since START.
    std::array<uint8_t, 256> m_mem{};
};

} // namespace kisimavr

#endif // KISIMAVR_I2C_DEVICES_H
