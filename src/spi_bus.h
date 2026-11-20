/*
 * kisimavr - ATmega co-simulation for ngspice / KiCad
 * Copyright (C) 2026 Paul
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file spi_bus.h
 * @brief SPI master of the AVR on the real pins: SCK, MOSI out, MISO in.
 *
 * simavr only reports the byte written to SPDR at the end of the transfer.
 * This module watches the write to SPDR and rebuilds the frame on the pins
 * (SCK = PB5, MOSI = PB3, at the SPCR / SPSR rate and mode, DORD order),
 * samples MISO (PB4) from the circuit at the sampling edges, and gives the
 * received byte to simavr when the transfer ends. Slaves are circuit parts
 * (e.g. a 74HC595 model); slave select is an ordinary GPIO of the firmware.
 *
 * Log, one line per burst of bytes: "SPI> MOSI A5 3C | MISO 00 A5".
 * Slave mode of the AVR (MSTR = 0) is not emulated.
 */

#ifndef KISIMAVR_SPI_BUS_H
#define KISIMAVR_SPI_BUS_H

#include <cstdint>
#include <vector>

#include <sim_avr.h>
#include <avr_spi.h>

#include "config.h"
#include "spi_frame.h"

namespace kisimavr
{

class SpiBus
{
public:
    /** dirty is set whenever SCK or MOSI changes. */
    explicit SpiBus(bool &dirty) : m_dirty(dirty) {}
    ~SpiBus();
    SpiBus(const SpiBus &) = delete;
    SpiBus &operator=(const SpiBus &) = delete;

    /** Hook the SPI module of avr (nothing to do on an MCU without SPI). */
    void start(avr_t *avr, const Config &cfg);

    /** True while SPE and MSTR are set: the SPI then owns SCK and MOSI. */
    bool master() const;

    const PinRef &sckPin() const { return m_sckPin; }
    const PinRef &mosiPin() const { return m_mosiPin; }
    const PinRef &misoPin() const { return m_misoPin; }

    /** Levels to drive on SCK and MOSI while master() is true. */
    bool sckLine() const;
    bool mosiLine() const { return m_mosi; }

    /** Level of a pin read from the circuit (only MISO matters). */
    void pinInput(const PinRef &pin, bool level);

private:
    static void onWrite(avr_t *avr, avr_io_addr_t addr, uint8_t value, void *param);
    static void onOutput(avr_irq_t *irq, uint32_t value, void *param);
    static avr_cycle_count_t onTimer(avr_t *avr, avr_cycle_count_t when, void *param);

    void transfer(uint8_t value);
    avr_cycle_count_t runEvents(avr_cycle_count_t when);
    void finish();
    void flushLog();
    double now() const;
    uint8_t reg(avr_io_addr_t addr) const;

    bool                  &m_dirty;
    avr_t                 *m_avr = nullptr;
    avr_spi_t             *m_spi = nullptr;
    bool                   m_log = true;

    PinRef                 m_sckPin{ 'B', 5 };
    PinRef                 m_mosiPin{ 'B', 3 };
    PinRef                 m_misoPin{ 'B', 4 };

    bool                   m_busy  = false;
    bool                   m_sck   = false;
    bool                   m_mosi  = false;
    bool                   m_miso  = true;    // Idle high until the circuit says otherwise.
    uint8_t                m_spcr  = 0;       // SPCR latched at the start of the byte.
    uint8_t                m_rx    = 0;
    avr_cycle_count_t      m_start = 0;
    size_t                 m_next  = 0;
    std::vector<SpiEvent>  m_events;

    std::vector<uint8_t>   m_logOut;
    std::vector<uint8_t>   m_logIn;
    double                 m_logTime = 0.0;
    avr_cycle_count_t      m_lastEnd = 0;
    uint32_t               m_period  = 4;
};

} // namespace kisimavr

#endif // KISIMAVR_SPI_BUS_H
