/*
 * kisimavr - ATmega co-simulation for ngspice / KiCad
 * Copyright (C) 2026 Paul
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file spi_bus.cpp
 * @brief SPI master of the AVR on the real pins: SCK, MOSI out, MISO in.
 */

#include "spi_bus.h"

#include <cstdio>
#include <cstring>

#include "common.h"

namespace kisimavr
{

namespace
{

/** A pause longer than this many bytes ends a log line. */
constexpr uint32_t kLogGapBytes = 8u;
constexpr size_t   kLogMaxBytes = 16u;

} // namespace

SpiBus::~SpiBus()
{
    flushLog();
}

void SpiBus::start(avr_t *avr, const Config &cfg)
{
    m_avr = avr;
    m_log = cfg.spiLog;
    for (avr_io_t *io = m_avr->io_port; io != nullptr; io = io->next)
    {
        if (io->kind != nullptr && std::strcmp(io->kind, "spi") == 0)
        {
            m_spi = reinterpret_cast<avr_spi_t *>(io);
            break;
        }
    }
    if (m_spi == nullptr)
    {
        return;    // MCU without SPI: nothing to do.
    }

    // simavr keeps its own handlers: these are called after them.
    avr_register_io_write(m_avr, m_spi->r_spdr, onWrite, this);
    avr_register_io_write(m_avr, m_spi->r_spcr, onWrite, this);
    avr_irq_register_notify(avr_io_getirq(m_avr, AVR_IOCTL_SPI_GETIRQ(m_spi->name),
                                          SPI_IRQ_OUTPUT),
                            onOutput, this);
}

bool SpiBus::master() const
{
    if (m_spi == nullptr)
    {
        return false;
    }
    const uint8_t spcr = reg(m_spi->r_spcr);
    return (spcr & spi_reg::kSpe) != 0u && (spcr & spi_reg::kMstr) != 0u;
}

bool SpiBus::sckLine() const
{
    if (m_busy)
    {
        return m_sck;
    }
    return (reg(m_spi->r_spcr) & spi_reg::kCpol) != 0u;    // Idle level.
}

void SpiBus::pinInput(const PinRef &pin, bool level)
{
    if (pin == m_misoPin)
    {
        m_miso = level;
    }
}

uint8_t SpiBus::reg(avr_io_addr_t addr) const
{
    return m_avr->data[addr];
}

double SpiBus::now() const
{
    return static_cast<double>(m_avr->cycle) / m_avr->frequency;
}

void SpiBus::onWrite(avr_t *avr, avr_io_addr_t addr, uint8_t value, void *param)
{
    auto *self = static_cast<SpiBus *>(param);
    if (addr == self->m_spi->r_spdr)
    {
        self->transfer(value);    // simavr's own handler has stored SPDR.
    }
    else
    {
        // SPCR has no simavr handler: a handler replaces the plain store.
        avr_core_watch_write(avr, addr, value);
        self->m_dirty = true;     // SPE, MSTR or CPOL may have changed.
    }
}

void SpiBus::onOutput(avr_irq_t * /*irq*/, uint32_t /*value*/, void *param)
{
    static_cast<SpiBus *>(param)->finish();
}

avr_cycle_count_t SpiBus::onTimer(avr_t * /*avr*/, avr_cycle_count_t when, void *param)
{
    return static_cast<SpiBus *>(param)->runEvents(when);
}

/** Write to SPDR: start the frame on the pins (master mode only). */
void SpiBus::transfer(uint8_t value)
{
    if (!master())
    {
        return;
    }
    if (m_busy)
    {
        // Write collision (WCOL on the chip): the byte in progress goes on.
        std::fprintf(stderr, "%s t=%.6f s SPI: SPDR written during a transfer (ignored)\n",
                     kTag, now());
        return;
    }

    m_spcr   = reg(m_spi->r_spcr);
    m_period = spiPeriod(m_spcr, reg(m_spi->r_spsr));
    m_events = spiFrame(value, m_spcr, m_period);
    m_next   = 0;
    m_rx     = 0;
    m_start  = m_avr->cycle;
    m_sck    = (m_spcr & spi_reg::kCpol) != 0u;
    m_busy   = true;

    if (m_log)
    {
        const bool gap = m_logOut.empty() ||
                         m_start - m_lastEnd > static_cast<avr_cycle_count_t>(m_period) * 8u *
                                                   kLogGapBytes ||
                         m_logOut.size() >= kLogMaxBytes;
        if (gap)
        {
            flushLog();
            m_logTime = now();
        }
        m_logOut.push_back(value);
    }

    const avr_cycle_count_t next = runEvents(m_start);
    if (next != 0u)
    {
        avr_cycle_timer_register(m_avr, next - m_start, onTimer, this);
    }
}

/** Apply every event due at cycle when; returns the next due cycle or 0. */
avr_cycle_count_t SpiBus::runEvents(avr_cycle_count_t when)
{
    while (m_next < m_events.size() && m_start + m_events[m_next].offset <= when)
    {
        const SpiEvent &e = m_events[m_next];
        if (e.sample)
        {
            const int k = static_cast<int>(m_next / ((m_spcr & spi_reg::kCpha) ? 2u : 3u));
            m_rx = spiSetBit(m_rx, k, m_miso, m_spcr);
        }
        if (e.sck >= 0)
        {
            m_sck = e.sck != 0;
        }
        if (e.mosi >= 0)
        {
            m_mosi = e.mosi != 0;
        }
        m_dirty = true;
        ++m_next;
    }
    if (m_next < m_events.size())
    {
        return m_start + m_events[m_next].offset;
    }
    return 0;
}

/** simavr ends the byte (8 SCK periods after the write): hand over MISO. */
void SpiBus::finish()
{
    if (!m_busy)
    {
        return;
    }
    runEvents(m_avr->cycle);    // Last trailing edge, due this very cycle.
    m_busy    = false;
    m_lastEnd = m_avr->cycle;
    m_dirty   = true;
    if (m_log)
    {
        m_logIn.push_back(m_rx);
    }
    avr_raise_irq(avr_io_getirq(m_avr, AVR_IOCTL_SPI_GETIRQ(m_spi->name), SPI_IRQ_INPUT), m_rx);
}

void SpiBus::flushLog()
{
    if (!m_logOut.empty())
    {
        std::fprintf(stderr, "%s t=%.6f s SPI> %s\n", kTag, m_logTime,
                     spiLogText(m_logOut, m_logIn).c_str());
    }
    m_logOut.clear();
    m_logIn.clear();
}

} // namespace kisimavr
