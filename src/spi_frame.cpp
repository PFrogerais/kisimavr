/*
 * kisimavr - ATmega co-simulation for ngspice / KiCad
 * Copyright (C) 2026 Paul
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file spi_frame.cpp
 * @brief SPI master timing of the AVR, without simavr.
 */

#include "spi_frame.h"

#include <cstdio>

namespace kisimavr
{

uint32_t spiPeriod(uint8_t spcr, uint8_t spsr)
{
    static const uint32_t kDividers[4] = { 4u, 16u, 64u, 128u };
    uint32_t period = kDividers[spcr & (spi_reg::kSpr0 | spi_reg::kSpr1)];
    if ((spsr & spi_reg::kSpi2x) != 0u)
    {
        period >>= 1;    // Double speed.
    }
    return period;
}

bool spiBit(uint8_t value, int k, uint8_t spcr)
{
    const int bit = ((spcr & spi_reg::kDord) != 0u) ? k : 7 - k;    // DORD = 1: LSB first.
    return ((value >> bit) & 1u) != 0u;
}

uint8_t spiSetBit(uint8_t value, int k, bool bit, uint8_t spcr)
{
    const int pos = ((spcr & spi_reg::kDord) != 0u) ? k : 7 - k;
    const auto mask = static_cast<uint8_t>(1u << pos);
    return static_cast<uint8_t>(bit ? (value | mask) : (value & ~mask));
}

std::vector<SpiEvent> spiFrame(uint8_t value, uint8_t spcr, uint32_t period)
{
    const bool   cpol = (spcr & spi_reg::kCpol) != 0u;
    const bool   cpha = (spcr & spi_reg::kCpha) != 0u;
    const auto   idle = static_cast<int8_t>(cpol ? 1 : 0);
    const auto   active = static_cast<int8_t>(cpol ? 0 : 1);
    const uint32_t half = period / 2u;

    std::vector<SpiEvent> events;
    for (int k = 0; k < 8; ++k)
    {
        const uint32_t start = static_cast<uint32_t>(k) * period;
        const auto     data  = static_cast<int8_t>(spiBit(value, k, spcr) ? 1 : 0);
        if (!cpha)
        {
            // Data first, sampled on the leading edge.
            events.push_back(SpiEvent{ start, -1, data, false });
            events.push_back(SpiEvent{ start + half, active, -1, true });
            events.push_back(SpiEvent{ start + period, idle, -1, false });
        }
        else
        {
            // Data changes on the leading edge, sampled on the trailing one.
            events.push_back(SpiEvent{ start, active, data, false });
            events.push_back(SpiEvent{ start + half, idle, -1, true });
        }
    }
    return events;
}

std::string spiLogText(const std::vector<uint8_t> &mosi, const std::vector<uint8_t> &miso)
{
    std::string text = "MOSI";
    char hex[4];
    for (uint8_t b : mosi)
    {
        std::snprintf(hex, sizeof hex, " %02X", b);
        text += hex;
    }
    text += " | MISO";
    for (uint8_t b : miso)
    {
        std::snprintf(hex, sizeof hex, " %02X", b);
        text += hex;
    }
    return text;
}

} // namespace kisimavr
