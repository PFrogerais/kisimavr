/*
 * kisimavr - ATmega co-simulation for ngspice / KiCad
 * Copyright (C) 2026 Paul
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file i2c_bus.cpp
 * @brief TWI (I2C) master of the AVR: virtual slaves, log and bus waveform.
 */

#include "i2c_bus.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "common.h"

namespace kisimavr
{

I2cBus::~I2cBus()
{
    flushLog();
}

void I2cBus::start(avr_t *avr, const Config &cfg)
{
    m_avr = avr;
    m_log = cfg.i2cLog;
    for (avr_io_t *io = m_avr->io_port; io != nullptr; io = io->next)
    {
        if (io->kind != nullptr && std::strcmp(io->kind, "twi") == 0)
        {
            m_twi = reinterpret_cast<avr_twi_t *>(io);
        }
    }
    if (m_twi == nullptr)
    {
        return;    // MCU without TWI: nothing to do.
    }

    for (const I2cSlaveConfig &s : cfg.i2cSlaves)
    {
        m_devices.emplace_back(s.type, s.address, cfg.lm75Temp, cfg.lm75Rate);
        std::fprintf(stderr, "%s I2C slave %s at 0x%02X\n", kTag, m_devices.back().name(),
                     s.address);
    }

    avr_irq_register_notify(avr_io_getirq(m_avr, AVR_IOCTL_TWI_GETIRQ(0), TWI_IRQ_OUTPUT),
                            onMessage, this);

    PinRef sda{};
    PinRef scl{};
    const bool okSda = parsePin(cfg.sdaPin, sda);
    const bool okScl = parsePin(cfg.sclPin, scl);
    for (size_t i = 0; i < cfg.ios.size(); ++i)
    {
        if (okSda && cfg.ios[i] == sda)
        {
            m_sdaIo = static_cast<int>(i);
        }
        if (okScl && cfg.ios[i] == scl)
        {
            m_sclIo = static_cast<int>(i);
        }
    }
}

bool I2cBus::ownsPins() const
{
    return m_twi != nullptr && avr_regbit_get(m_avr, m_twi->twen);
}

double I2cBus::now() const
{
    return static_cast<double>(m_avr->cycle) / m_avr->frequency;
}

void I2cBus::onMessage(avr_irq_t * /*irq*/, uint32_t value, void *param)
{
    static_cast<I2cBus *>(param)->message(value);
}

avr_cycle_count_t I2cBus::onTimer(avr_t * /*avr*/, avr_cycle_count_t when, void *param)
{
    return static_cast<I2cBus *>(param)->nextSegment(when);
}

/** Slave answer to the master (ACK bit or read data), as simavr expects. */
void I2cBus::reply(uint8_t msg, uint8_t addr, uint8_t data)
{
    avr_raise_irq(avr_io_getirq(m_avr, AVR_IOCTL_TWI_GETIRQ(0), TWI_IRQ_INPUT),
                  avr_twi_irq_msg(msg, addr, data));
}

/** A TWI message from the AVR master: START + address, data, STOP. */
void I2cBus::message(uint32_t value)
{
    avr_twi_msg_irq_t m;
    m.u.v = value;
    const uint8_t msg  = m.u.twi.msg;
    const uint8_t addr = m.u.twi.addr;
    const uint8_t data = m.u.twi.data;
    char buf[24];

    if (msg & TWI_COND_STOP)
    {
        waveStop();
        flushLog();
        m_selected = nullptr;
    }

    if (msg & TWI_COND_START)
    {
        waveStart();
        m_selected = nullptr;
        for (I2cDevice &d : m_devices)
        {
            if (d.address() == (addr >> 1))
            {
                m_selected = &d;
            }
        }
        if (m_text.empty())
        {
            m_textTime = now();
        }
        std::snprintf(buf, sizeof(buf), "%s0x%02X %c%s", m_text.empty() ? "" : " | ",
                      addr >> 1, (addr & 1u) ? 'R' : 'W',
                      (m_selected == nullptr) ? " (NACK)" : "");
        m_text += buf;
        if (m_selected != nullptr)
        {
            m_selected->start();
            reply(TWI_COND_ACK, addr, 1);
        }
        waveByte(addr, m_selected != nullptr);
    }

    if (msg & TWI_COND_WRITE)
    {
        std::snprintf(buf, sizeof(buf), " %02X", data);
        m_text += buf;
        if (m_selected != nullptr)
        {
            m_selected->write(data);
            reply(TWI_COND_ACK, addr, 1);
        }
        waveByte(data, m_selected != nullptr);
    }

    if (msg & TWI_COND_READ)
    {
        uint8_t out = 0xFF;    // A released bus reads as 1s.
        if (m_selected != nullptr)
        {
            out = m_selected->read(now());
            reply(TWI_COND_READ, addr, out);
        }
        std::snprintf(buf, sizeof(buf), " %02X", out);
        m_text += buf;
        waveByte(out, (msg & TWI_COND_ACK) != 0);    // ACK / NACK from the master.
    }

    if (!m_busy && !m_wave.empty() && (m_sdaIo >= 0 || m_sclIo >= 0))
    {
        m_busy = true;
        avr_cycle_timer_register(m_avr, 1, onTimer, this);
    }
    else if (m_sdaIo < 0 && m_sclIo < 0)
    {
        m_wave.clear();    // No pin to drive: keep the log only.
    }
}

void I2cBus::flushLog()
{
    if (!m_text.empty() && m_log)
    {
        std::fprintf(stderr, "%s t=%.6f s I2C> %s\n", kTag, m_textTime, m_text.c_str());
    }
    m_text.clear();
}

/* ------------------------------------------------------------------------ */
/* Waveform                                                                 */
/* ------------------------------------------------------------------------ */

/** SCL half period in CPU cycles: f_SCL = F_CPU / (16 + 2 TWBR 4^TWPS). */
avr_cycle_count_t I2cBus::halfPeriod() const
{
    static const uint32_t kPrescale[] = { 1, 4, 16, 64 };
    const uint32_t twbr = m_avr->data[m_twi->r_twbr];
    const uint32_t ps   = avr_regbit_get(m_avr, m_twi->twps) & 3u;
    const uint32_t div  = 16u + 2u * twbr * kPrescale[ps];
    return std::max<avr_cycle_count_t>(1, div / 2u);
}

void I2cBus::segment(bool sda, bool scl, avr_cycle_count_t cycles)
{
    m_wave.emplace_back(static_cast<uint8_t>((sda ? 1u : 0u) | (scl ? 2u : 0u)), cycles);
    m_qSda = sda;
    m_qScl = scl;
}

void I2cBus::waveStart()
{
    const avr_cycle_count_t h = halfPeriod();
    if (!m_qScl || !m_qSda)    // Repeated START: release SDA, then SCL.
    {
        segment(true, false, h / 2 + 1);
        segment(true, true, h / 2 + 1);
    }
    segment(false, true, h / 2 + 1);    // SDA falls while SCL is high.
    segment(false, false, h / 2 + 1);
}

/** 8 data bits (MSB first) and the acknowledge bit (low = ACK). */
void I2cBus::waveByte(uint8_t value, bool ack)
{
    // SDA only changes while SCL is low, never together with an SCL edge,
    // otherwise a decoder would see a START or a STOP.
    const avr_cycle_count_t h = halfPeriod();
    const avr_cycle_count_t q = std::max<avr_cycle_count_t>(1, h / 4);
    for (int k = 7; k >= -1; --k)
    {
        const bool bit = (k >= 0) ? (((value >> k) & 1u) != 0u) : !ack;
        segment(m_qSda, false, q);    // SCL falls first...
        segment(bit, false, h - q);   // ...then SDA takes the new bit.
        segment(bit, true, h);        // SCL high: the bit is sampled.
    }
    segment(!ack, false, h / 2 + 1);
}

void I2cBus::waveStop()
{
    const avr_cycle_count_t h = halfPeriod();
    segment(false, false, h / 2 + 1);
    segment(false, true, h / 2 + 1);    // SDA rises while SCL is high.
    segment(true, true, h);
}

avr_cycle_count_t I2cBus::nextSegment(avr_cycle_count_t when)
{
    if (m_wave.empty())
    {
        m_busy = false;
        return 0;    // Bus idle: both lines released.
    }
    const auto seg = m_wave.front();
    m_wave.pop_front();
    m_sdaLine = (seg.first & 1u) != 0u;
    m_sclLine = (seg.first & 2u) != 0u;
    m_dirty   = true;
    return when + seg.second;
}

} // namespace kisimavr
