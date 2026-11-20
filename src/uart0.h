/*
 * kisimavr - ATmega co-simulation for ngspice / KiCad
 * Copyright (C) 2026 Paul
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file uart0.h
 * @brief UART0 transmitter: console text, log file and TXD frames.
 *
 * Every byte sent by the firmware is printed on stderr, one line at a time
 * ("UART0> ..."), and optionally written to the "uartlog" file. simavr does
 * not drive the TXD pin, so when the TX pin ("txpin", PD1) is an "out" or
 * "io" pin, the serial frames are rebuilt on it at the baud rate programmed
 * in UBRR0 (start, data LSB first, 1 or 2 stops).
 */

#ifndef KISIMAVR_UART0_H
#define KISIMAVR_UART0_H

#include <cstdint>
#include <cstdio>
#include <deque>
#include <string>

#include <sim_avr.h>
#include <avr_uart.h>

#include "config.h"

namespace kisimavr
{

class Uart0
{
public:
    /** dirty is set whenever the TX line changes. */
    explicit Uart0(bool &dirty) : m_dirty(dirty) {}
    ~Uart0();
    Uart0(const Uart0 &) = delete;
    Uart0 &operator=(const Uart0 &) = delete;

    /** Hook UART0 of avr (nothing to do on an MCU without UART0). */
    void start(avr_t *avr, const Config &cfg);

    /** Index of the TX pin in cfg.outputs / cfg.ios, or -1. */
    int txOutIndex() const { return m_txOut; }
    int txIoIndex() const { return m_txIo; }

    /** True once the UART drives the TX pin; then txLine() is its level. */
    bool ownsTxPin() const { return m_txEnabled; }
    bool txLine() const { return m_txLine; }

private:
    static void onByte(avr_irq_t *irq, uint32_t value, void *param);
    static avr_cycle_count_t onBitTimer(avr_t *avr, avr_cycle_count_t when, void *param);

    void byte(uint8_t value);
    avr_cycle_count_t nextBit(avr_cycle_count_t when);
    void queueFrame(uint8_t value);
    void flushLine();

    bool              &m_dirty;
    avr_t             *m_avr  = nullptr;
    avr_uart_t        *m_uart = nullptr;

    // Console / log file
    std::string        m_line;
    double             m_lineTime = 0.0;
    std::FILE         *m_file     = nullptr;

    // TXD frames
    int                m_txOut       = -1;
    int                m_txIo        = -1;
    bool               m_txEnabled   = false;
    bool               m_txLine      = true;
    bool               m_txBusy      = false;
    avr_cycle_count_t  m_txBitCycles = 0;
    std::deque<bool>   m_txBits;
};

} // namespace kisimavr

#endif // KISIMAVR_UART0_H
