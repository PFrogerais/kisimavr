/*
 * kisimavr - ATmega co-simulation for ngspice / KiCad
 * Copyright (C) 2026 Paul
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file i2c_bus.h
 * @brief TWI (I2C) master of the AVR: virtual slaves, log and bus waveform.
 *
 * simavr exchanges TWI messages (START + address, data, ACK, STOP), it does
 * not drive the pins. This module:
 *   - answers the messages as the virtual slaves of the "i2c" key (LM75,
 *     24C02); any other address is not acknowledged (NACK);
 *   - prints each transaction: "I2C> 0x48 W 00 | 0x48 R 19 80"
 *     ("|" = repeated START, "(NACK)" = no slave answered);
 *   - when SDA / SCL ("sda", "scl": PC4 / PC5) are "io" pins, rebuilds the
 *     bus waveform at the TWBR bit rate as an open-drain output: the pin is
 *     pulled low or released, the circuit needs pull-ups. SDA only changes
 *     while SCL is low, so the waveform decodes as real I2C.
 */

#ifndef KISIMAVR_I2C_BUS_H
#define KISIMAVR_I2C_BUS_H

#include <cstdint>
#include <deque>
#include <string>
#include <utility>
#include <vector>

#include <sim_avr.h>
#include <avr_twi.h>

#include "config.h"
#include "i2c_devices.h"

namespace kisimavr
{

class I2cBus
{
public:
    /** dirty is set whenever SDA or SCL changes. */
    explicit I2cBus(bool &dirty) : m_dirty(dirty) {}
    ~I2cBus();
    I2cBus(const I2cBus &) = delete;
    I2cBus &operator=(const I2cBus &) = delete;

    /** Hook the TWI module of avr (nothing to do on an MCU without TWI). */
    void start(avr_t *avr, const Config &cfg);

    /** Index of SDA / SCL in cfg.ios, or -1. */
    int sdaIoIndex() const { return m_sdaIo; }
    int sclIoIndex() const { return m_sclIo; }

    /** True while the TWI is enabled (TWEN): it then owns SDA and SCL. */
    bool ownsPins() const;

    /** Line levels to drive (false = pulled low, true = released). */
    bool sdaLine() const { return m_sdaLine; }
    bool sclLine() const { return m_sclLine; }

private:
    static void onMessage(avr_irq_t *irq, uint32_t value, void *param);
    static avr_cycle_count_t onTimer(avr_t *avr, avr_cycle_count_t when, void *param);

    void message(uint32_t value);
    avr_cycle_count_t nextSegment(avr_cycle_count_t when);
    double now() const;
    void reply(uint8_t msg, uint8_t addr, uint8_t data);
    void flushLog();

    // Waveform
    avr_cycle_count_t halfPeriod() const;
    void segment(bool sda, bool scl, avr_cycle_count_t cycles);
    void waveStart();
    void waveByte(uint8_t value, bool ack);
    void waveStop();

    bool                   &m_dirty;
    avr_t                  *m_avr = nullptr;
    avr_twi_t              *m_twi = nullptr;
    bool                    m_log = true;

    std::vector<I2cDevice>  m_devices;
    I2cDevice              *m_selected = nullptr;
    std::string             m_text;
    double                  m_textTime = 0.0;

    int                     m_sdaIo   = -1;
    int                     m_sclIo   = -1;
    bool                    m_sdaLine = true;
    bool                    m_sclLine = true;
    bool                    m_qSda    = true;    // Line state at the end of
    bool                    m_qScl    = true;    // the queued waveform.
    bool                    m_busy    = false;
    std::deque<std::pair<uint8_t, avr_cycle_count_t>> m_wave;    // (sda | scl << 1, cycles)
};

} // namespace kisimavr

#endif // KISIMAVR_I2C_BUS_H
