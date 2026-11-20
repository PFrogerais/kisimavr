/*
 * kisimavr - ATmega co-simulation for ngspice / KiCad
 * Copyright (C) 2026 Paul
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file i2c_devices.cpp
 * @brief Virtual I2C slaves: LM75 temperature sensor and 24C02 EEPROM.
 */

#include "i2c_devices.h"

#include <algorithm>
#include <cmath>

#include "common.h"

namespace kisimavr
{

I2cDevice::I2cDevice(Type type, int address, double temp0, double rate)
    : m_type(type), m_address(address), m_temp0(temp0), m_rate(rate)
{
    if (m_type == Type::Eeprom24c02)
    {
        m_mem.fill(0xFF);    // Erased EEPROM.
    }
    else
    {
        m_mem[1] = 0x00;     // Configuration.
        m_mem[2] = 75;       // Thyst = 75 degC.
        m_mem[3] = 80;       // Tos   = 80 degC.
    }
}

const char *I2cDevice::name() const
{
    return (m_type == Type::Lm75) ? "lm75" : "eeprom";
}

int I2cDevice::defaultAddress(Type type)
{
    return (type == Type::Lm75) ? 0x48 : 0x50;
}

bool I2cDevice::typeFromName(const std::string &name, Type &type)
{
    const std::string n = toLower(name);
    if (n == "lm75")
    {
        type = Type::Lm75;
        return true;
    }
    if (n == "eeprom" || n == "24c02")
    {
        type = Type::Eeprom24c02;
        return true;
    }
    return false;
}

uint16_t I2cDevice::lm75Word(double celsius)
{
    const double t = std::max(-55.0, std::min(125.0, celsius));
    const long halves = std::lround(t * 2.0);    // 0.5 degC steps.
    return static_cast<uint16_t>(static_cast<uint16_t>(halves) << 7);
}

void I2cDevice::start()
{
    m_index = 0;
}

void I2cDevice::write(uint8_t value)
{
    if (m_index == 0)
    {
        m_pointer = value;    // First byte: register / memory address.
    }
    else if (m_type == Type::Eeprom24c02)
    {
        m_mem[m_pointer++] = value;    // uint8_t: wraps at 256.
    }
    else
    {
        const uint8_t reg = m_pointer & 3u;
        if (reg == 1)
        {
            m_mem[1] = value;
        }
        else if (reg >= 2 && (m_index % 2) == 1)
        {
            m_mem[reg] = value;    // Integer part of Thyst / Tos.
        }
        // Temperature register is read-only.
    }
    ++m_index;
}

uint8_t I2cDevice::read(double timeSeconds)
{
    if (m_type == Type::Eeprom24c02)
    {
        return m_mem[m_pointer++];
    }

    const int byteIndex = m_index++;
    const uint8_t reg = m_pointer & 3u;
    if (reg == 1)
    {
        return m_mem[1];
    }
    const uint16_t word = (reg == 0)
                              ? lm75Word(m_temp0 + m_rate * timeSeconds)
                              : static_cast<uint16_t>(m_mem[reg] << 8);
    return static_cast<uint8_t>((byteIndex % 2 == 0) ? (word >> 8) : (word & 0xFFu));
}

} // namespace kisimavr
