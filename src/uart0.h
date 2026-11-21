/*
 * kisimavr - ATmega co-simulation for ngspice / KiCad
 * Copyright (C) 2026 Paul
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file uart0.h
 * @brief UART0: console text, log file, TXD frames and reception.
 *
 * Transmit: every byte sent by the firmware is printed on stderr, one line
 * at a time ("UART0> ..."), and optionally written to the "uartlog" file.
 * simavr does not drive the TXD pin, so when the TX pin ("txpin", PD1) is
 * an "out" or "io" pin, the serial frames are rebuilt on it at the baud
 * rate programmed in UBRR0 (start, data LSB first, 1 or 2 stops).
 *
 * Receive, from two sources (both may be used):
 *   - "uartin": a text file of timed lines (see uart_script.h), sent at the
 *     programmed baud rate, one frame time per byte;
 *   - the RX pin ("rxpin", PD0) when it is an "in" or "io" pin: frames
 *     coming from the circuit (another MCU, a generator) are decoded at
 *     the programmed baud rate, sampled in the middle of each bit.
 *   - "pty": a pseudo-terminal (see pty_port.h) opened by a terminal
 *     program; what is typed there is sent at the programmed baud rate,
 *     and the bytes of the firmware are written to it.
 * Received text is printed as "UART0< ...". simavr ignores bytes while the
 * receiver is off (RXEN = 0): they are counted and reported. While RXEN is
 * set the RX pin is an input (the internal pull-up still follows PORTD0).
 */

#ifndef KISIMAVR_UART0_H
#define KISIMAVR_UART0_H

#include <cstdint>
#include <cstdio>
#include <deque>
#include <string>
#include <vector>

#include <sim_avr.h>
#include <avr_uart.h>

#include "config.h"
#include "pty_port.h"
#include "uart_script.h"

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

    /** Hook UART0 of avr (nothing to do on an MCU without UART0).
     *  Returns false if the "uartin" file cannot be read. */
    bool start(avr_t *avr, const Config &cfg);

    /** A circuit pin changed: decoded when it is the RX pin. */
    void pinInput(const PinRef &pin, bool level);

    /** Index of the RX pin in cfg.ios, or -1; true while RXEN is set. */
    int rxIoIndex() const { return m_rxIo; }
    bool ownsRxPin() const;

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

    // Reception
    static avr_cycle_count_t onScriptTimer(avr_t *avr, avr_cycle_count_t when, void *param);
    static avr_cycle_count_t onPaceTimer(avr_t *avr, avr_cycle_count_t when, void *param);
    static avr_cycle_count_t onSampleTimer(avr_t *avr, avr_cycle_count_t when, void *param);
    static avr_cycle_count_t onPtyTimer(avr_t *avr, avr_cycle_count_t when, void *param);
    avr_cycle_count_t pollPty(avr_cycle_count_t when);
    void queueReceived(uint8_t value);
    bool loadScript(const std::string &path);
    void scheduleScript();
    avr_cycle_count_t scriptEvent(avr_cycle_count_t when);
    avr_cycle_count_t paceByte(avr_cycle_count_t when);
    avr_cycle_count_t sampleBit(avr_cycle_count_t when);
    void receive(uint8_t value);
    void flushRxLine();
    avr_cycle_count_t bitCycles() const;
    int dataBits() const;
    double now() const;

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

    // Reception: script
    std::vector<UartEvent> m_events;
    size_t             m_nextEvent = 0;
    std::deque<uint8_t> m_rxQueue;
    bool               m_pacing    = false;

    // Reception: RX pin decoder
    PinRef             m_rxPin{};
    bool               m_rxPinUsed = false;
    int                m_rxIo      = -1;
    bool               m_rxLevel   = true;    // Idle line is high.
    bool               m_rxBusy    = false;
    int                m_rxBit     = 0;       // -1 start, 0..n-1 data, n stop
    uint16_t           m_rxShift   = 0;

    // Reception: pseudo-terminal
    PtyPort            m_pty;

    // Received text log and dropped bytes (receiver off)
    std::string        m_rxLine;
    double             m_rxLineTime = 0.0;
    bool               m_rxLineLost = false;
    unsigned long      m_dropped    = 0;
};

} // namespace kisimavr

#endif // KISIMAVR_UART0_H
