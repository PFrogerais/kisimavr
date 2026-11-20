/*
 * kisimavr - ATmega co-simulation for ngspice / KiCad
 * Copyright (C) 2026 Paul
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file spi_frame.h
 * @brief SPI master timing of the AVR, without simavr (unit-tested).
 *
 * One byte = 8 SCK periods. With h = half a period, bit i starts at i * 2h:
 *   CPHA = 0: MOSI set at i*2h, leading edge (sample) at i*2h + h,
 *             trailing edge at (i + 1) * 2h;
 *   CPHA = 1: leading edge and MOSI set at i*2h, trailing edge (sample)
 *             at i*2h + h.
 * The leading edge goes from the idle level CPOL to !CPOL.
 */

#ifndef KISIMAVR_SPI_FRAME_H
#define KISIMAVR_SPI_FRAME_H

#include <cstdint>
#include <string>
#include <vector>

namespace kisimavr
{

/** SPCR / SPSR bits of the ATmega48/88/168/328 family. */
namespace spi_reg
{
constexpr uint8_t kSpr0  = 0x01;
constexpr uint8_t kSpr1  = 0x02;
constexpr uint8_t kCpha  = 0x04;
constexpr uint8_t kCpol  = 0x08;
constexpr uint8_t kMstr  = 0x10;
constexpr uint8_t kDord  = 0x20;
constexpr uint8_t kSpe   = 0x40;
constexpr uint8_t kSpi2x = 0x01;    // In SPSR.
} // namespace spi_reg

/** One instant of the frame: new levels (-1 = unchanged), sample MISO or not. */
struct SpiEvent
{
    uint32_t offset = 0;      // CPU cycles from the write to SPDR.
    int8_t   sck    = -1;
    int8_t   mosi   = -1;
    bool     sample = false;
};

/** SCK period in CPU cycles (fosc/2 ... fosc/128), as simavr counts it. */
uint32_t spiPeriod(uint8_t spcr, uint8_t spsr);

/** Events of one byte, sorted by offset; the frame ends at 8 * period. */
std::vector<SpiEvent> spiFrame(uint8_t value, uint8_t spcr, uint32_t period);

/** Bit k (0 = first on the wire) of value, in the DORD order of spcr. */
bool spiBit(uint8_t value, int k, uint8_t spcr);

/** Puts the received bit k (0 = first on the wire) into value. */
uint8_t spiSetBit(uint8_t value, int k, bool bit, uint8_t spcr);

/** "MOSI A5 3C | MISO 00 A5" */
std::string spiLogText(const std::vector<uint8_t> &mosi, const std::vector<uint8_t> &miso);

} // namespace kisimavr

#endif // KISIMAVR_SPI_FRAME_H
