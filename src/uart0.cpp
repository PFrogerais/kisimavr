/*
 * kisimavr - ATmega co-simulation for ngspice / KiCad
 * Copyright (C) 2026 Paul
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "uart0.h"

#include <cstring>

#include "common.h"
#include "paths.h"

namespace kisimavr
{

Uart0::~Uart0()
{
    flushLine();
    if (m_file != nullptr)
    {
        std::fclose(m_file);
    }
}

void Uart0::start(avr_t *avr, const Config &cfg)
{
    m_avr = avr;
    for (avr_io_t *io = m_avr->io_port; io != nullptr; io = io->next)
    {
        if (io->kind != nullptr && std::strcmp(io->kind, "uart") == 0)
        {
            auto *uart = reinterpret_cast<avr_uart_t *>(io);
            if (uart->name == '0')
            {
                m_uart = uart;
                break;
            }
        }
    }
    if (m_uart == nullptr)
    {
        return;    // MCU without UART0: nothing to do.
    }

    // No echo on simavr's stdout, no real-time sleep while polling UDRE.
    uint32_t flags = 0;
    avr_ioctl(m_avr, AVR_IOCTL_UART_SET_FLAGS('0'), &flags);
    avr_irq_register_notify(avr_io_getirq(m_avr, AVR_IOCTL_UART_GETIRQ('0'), UART_IRQ_OUTPUT),
                            onByte, this);

    if (!cfg.uartLog.empty())
    {
        m_file = openOutputFile(cfg.uartLog, "UART log", { ".txt", ".log" }, looksLikeUartLog);
    }

    PinRef tx{};
    if (parsePin(cfg.txPin, tx))
    {
        for (size_t i = 0; i < cfg.outputs.size(); ++i)
        {
            if (cfg.outputs[i] == tx)
            {
                m_txOut = static_cast<int>(i);
            }
        }
        for (size_t i = 0; i < cfg.ios.size(); ++i)
        {
            if (cfg.ios[i] == tx)
            {
                m_txIo = static_cast<int>(i);
            }
        }
    }
}

void Uart0::onByte(avr_irq_t * /*irq*/, uint32_t value, void *param)
{
    static_cast<Uart0 *>(param)->byte(static_cast<uint8_t>(value));
}

avr_cycle_count_t Uart0::onBitTimer(avr_t * /*avr*/, avr_cycle_count_t when, void *param)
{
    return static_cast<Uart0 *>(param)->nextBit(when);
}

/** A byte written to UDR0 by the firmware. */
void Uart0::byte(uint8_t value)
{
    // Console text, one line at a time.
    if (value == '\n')
    {
        flushLine();
    }
    else if (value != '\r')
    {
        if (m_line.empty())
        {
            m_lineTime = static_cast<double>(m_avr->cycle) / m_avr->frequency;
        }
        m_line.push_back((value >= 0x20 && value < 0x7F) ? static_cast<char>(value) : '.');
        if (m_line.size() >= 200u)
        {
            flushLine();
        }
    }

    if (m_txOut >= 0 || m_txIo >= 0)
    {
        queueFrame(value);
    }
}

/** TX bit timer: put the next frame bit on the TX pin. */
avr_cycle_count_t Uart0::nextBit(avr_cycle_count_t when)
{
    if (m_txBits.empty())
    {
        m_txBusy = false;
        return 0;    // Line stays idle (high).
    }
    m_txLine = m_txBits.front();
    m_txBits.pop_front();
    m_dirty = true;
    return when + m_txBitCycles;
}

/** Append one serial frame (start, data LSB first, stops) to the TX line. */
void Uart0::queueFrame(uint8_t value)
{
    static const int kDataBits[] = { 5, 6, 7, 8, 8, 8, 8, 9 };

    const uint32_t ubrr = avr_regbit_get(m_avr, m_uart->ubrrl) |
                          (static_cast<uint32_t>(avr_regbit_get(m_avr, m_uart->ubrrh)) << 8);
    avr_cycle_count_t cyclesPerBit = (ubrr + 1u) * 8u;
    if (!avr_regbit_get(m_avr, m_uart->u2x))
    {
        cyclesPerBit *= 2u;
    }
    const int dataBits = kDataBits[avr_regbit_get(m_avr, m_uart->ucsz) |
                                   (avr_regbit_get(m_avr, m_uart->ucsz2) << 2)];
    const int stopBits = 1 + avr_regbit_get(m_avr, m_uart->usbs);
    m_txBitCycles = cyclesPerBit;

    if (!m_txEnabled)
    {
        m_txEnabled = true;    // TXEN: the pin becomes the idle-high line.
        m_txLine    = true;
        m_dirty     = true;
    }

    m_txBits.push_back(false);                               // Start bit.
    for (int k = 0; k < dataBits; ++k)
    {
        m_txBits.push_back(((value >> k) & 1u) != 0u);      // LSB first.
    }
    for (int k = 0; k < stopBits; ++k)
    {
        m_txBits.push_back(true);                            // Stop bit(s).
    }

    if (!m_txBusy)
    {
        // One idle bit before the first start bit, then one bit per tick.
        m_txBusy = true;
        avr_cycle_timer_register(m_avr, m_txBitCycles, onBitTimer, this);
    }
}

void Uart0::flushLine()
{
    if (m_line.empty())
    {
        return;
    }
    std::fprintf(stderr, "%s t=%.6f s UART0> %s\n", kTag, m_lineTime, m_line.c_str());
    if (m_file != nullptr)
    {
        std::fprintf(m_file, "%s\n", m_line.c_str());
        std::fflush(m_file);
    }
    m_line.clear();
}

} // namespace kisimavr
