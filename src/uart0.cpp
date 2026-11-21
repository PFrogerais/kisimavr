/*
 * kisimavr - ATmega co-simulation for ngspice / KiCad
 * Copyright (C) 2026 Paul
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "uart0.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <sstream>

#include "common.h"
#include "paths.h"

namespace kisimavr
{

Uart0::~Uart0()
{
    flushLine();
    flushRxLine();
    if (m_dropped != 0)
    {
        std::fprintf(stderr, "%s UART0: %lu received byte(s) lost: receiver off (RXEN = 0)\n",
                     kTag, m_dropped);
    }
    if (m_file != nullptr)
    {
        std::fclose(m_file);
    }
}

bool Uart0::start(avr_t *avr, const Config &cfg)
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
        return true;    // MCU without UART0: nothing to do.
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

    // Reception: RX pin decoded when the circuit drives it.
    if (parsePin(cfg.rxPin, m_rxPin))
    {
        for (const PinRef &p : cfg.inputs)
        {
            m_rxPinUsed = m_rxPinUsed || (p == m_rxPin);
        }
        for (size_t i = 0; i < cfg.ios.size(); ++i)
        {
            if (cfg.ios[i] == m_rxPin)
            {
                m_rxPinUsed = true;
                m_rxIo = static_cast<int>(i);
            }
        }
    }

    // Pseudo-terminal: what is typed there is read every 200 us (simulated).
    if (!cfg.pty.empty())
    {
        if (!m_pty.open(cfg.pty == "1" ? "" : cfg.pty, "UART0"))
        {
            return false;
        }
        avr_cycle_timer_register(m_avr, 1, onPtyTimer, this);
    }

    // Reception: timed text file.
    if (!cfg.uartIn.empty())
    {
        if (!loadScript(resolvePath(cfg.uartIn)))
        {
            return false;
        }
        scheduleScript();
    }
    return true;
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
    m_pty.write(value);

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
    const int dataBits = this->dataBits();
    const int stopBits = 1 + avr_regbit_get(m_avr, m_uart->usbs);
    m_txBitCycles = bitCycles();

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

/* ------------------------------------------------------------------------ */
/* Reception                                                                */
/* ------------------------------------------------------------------------ */

/** Cycles per bit at the programmed baud rate (UBRR0, U2X0). */
avr_cycle_count_t Uart0::bitCycles() const
{
    const uint32_t ubrr = avr_regbit_get(m_avr, m_uart->ubrrl) |
                          (static_cast<uint32_t>(avr_regbit_get(m_avr, m_uart->ubrrh)) << 8);
    avr_cycle_count_t cycles = (ubrr + 1u) * 8u;
    if (!avr_regbit_get(m_avr, m_uart->u2x))
    {
        cycles *= 2u;
    }
    return cycles;
}

int Uart0::dataBits() const
{
    static const int kDataBits[] = { 5, 6, 7, 8, 8, 8, 8, 9 };
    return kDataBits[avr_regbit_get(m_avr, m_uart->ucsz) |
                     (avr_regbit_get(m_avr, m_uart->ucsz2) << 2)];
}

double Uart0::now() const
{
    return static_cast<double>(m_avr->cycle) / m_avr->frequency;
}

bool Uart0::ownsRxPin() const
{
    return m_uart != nullptr && avr_regbit_get(m_avr, m_uart->rxen);
}

bool Uart0::loadScript(const std::string &path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        std::fprintf(stderr, "%s cannot open UART input '%s'\n", kTag, path.c_str());
        return false;
    }
    std::ostringstream text;
    text << in.rdbuf();
    if (text.str().size() > (1u << 20))
    {
        std::fprintf(stderr, "%s UART input '%s' is larger than 1 MiB\n", kTag, path.c_str());
        return false;
    }

    std::string error;
    if (!parseUartScript(text.str(), m_events, error))
    {
        std::fprintf(stderr, "%s UART input '%s', %s\n", kTag, path.c_str(), error.c_str());
        return false;
    }
    std::fprintf(stderr, "%s UART0 input: %zu event(s) from %s\n", kTag, m_events.size(),
                 path.c_str());
    return true;
}

void Uart0::scheduleScript()
{
    if (m_nextEvent >= m_events.size())
    {
        return;
    }
    const auto at = static_cast<avr_cycle_count_t>(
        m_events[m_nextEvent].time * static_cast<double>(m_avr->frequency));
    const avr_cycle_count_t delay = (at > m_avr->cycle) ? at - m_avr->cycle : 1;
    avr_cycle_timer_register(m_avr, delay, onScriptTimer, this);
}

avr_cycle_count_t Uart0::onScriptTimer(avr_t * /*avr*/, avr_cycle_count_t when, void *param)
{
    return static_cast<Uart0 *>(param)->scriptEvent(when);
}

avr_cycle_count_t Uart0::onPaceTimer(avr_t * /*avr*/, avr_cycle_count_t when, void *param)
{
    return static_cast<Uart0 *>(param)->paceByte(when);
}

avr_cycle_count_t Uart0::onSampleTimer(avr_t * /*avr*/, avr_cycle_count_t when, void *param)
{
    return static_cast<Uart0 *>(param)->sampleBit(when);
}

/** A byte to send to the receiver, one frame time after the previous one. */
void Uart0::queueReceived(uint8_t value)
{
    m_rxQueue.push_back(value);
    if (!m_pacing)
    {
        m_pacing = true;
        avr_cycle_timer_register(m_avr, 1, onPaceTimer, this);
    }
}

/** A line of the script is due: queue its bytes, schedule the next line. */
avr_cycle_count_t Uart0::scriptEvent(avr_cycle_count_t /*when*/)
{
    for (char c : m_events[m_nextEvent].bytes)
    {
        queueReceived(static_cast<uint8_t>(c));
    }
    ++m_nextEvent;
    scheduleScript();
    return 0;
}

avr_cycle_count_t Uart0::onPtyTimer(avr_t * /*avr*/, avr_cycle_count_t when, void *param)
{
    return static_cast<Uart0 *>(param)->pollPty(when);
}

/** Bytes typed in the terminal program: queued for the receiver. */
avr_cycle_count_t Uart0::pollPty(avr_cycle_count_t when)
{
    for (uint8_t b : m_pty.read())
    {
        queueReceived(b);
    }
    return when + static_cast<avr_cycle_count_t>(m_avr->frequency / 5000u);    // 200 us
}

/** Hand the queued bytes to simavr, one frame time apart. */
avr_cycle_count_t Uart0::paceByte(avr_cycle_count_t when)
{
    if (m_rxQueue.empty())
    {
        m_pacing = false;
        return 0;
    }
    receive(m_rxQueue.front());
    m_rxQueue.pop_front();
    const int frameBits = 2 + dataBits() + avr_regbit_get(m_avr, m_uart->usbs);
    return when + bitCycles() * static_cast<avr_cycle_count_t>(frameBits);
}

/** RX pin edge: a falling edge on an idle line starts a frame. */
void Uart0::pinInput(const PinRef &pin, bool level)
{
    if (!m_rxPinUsed || !(pin == m_rxPin))
    {
        return;
    }
    const bool falling = m_rxLevel && !level;
    m_rxLevel = level;
    // Like the hardware, the receiver only looks at the line while RXEN is set.
    if (falling && !m_rxBusy && ownsRxPin())
    {
        m_rxBusy  = true;
        m_rxBit   = -1;    // Check the start bit in its middle.
        m_rxShift = 0;
        avr_cycle_timer_register(m_avr, std::max<avr_cycle_count_t>(1, bitCycles() / 2),
                                 onSampleTimer, this);
    }
}

/** Sample the RX line in the middle of each bit. */
avr_cycle_count_t Uart0::sampleBit(avr_cycle_count_t when)
{
    const int n = dataBits();
    if (m_rxBit < 0)
    {
        if (m_rxLevel)
        {
            m_rxBusy = false;    // Glitch, not a start bit.
            return 0;
        }
    }
    else if (m_rxBit < n)
    {
        if (m_rxLevel)
        {
            m_rxShift = static_cast<uint16_t>(m_rxShift | (1u << m_rxBit));    // LSB first.
        }
    }
    else
    {
        m_rxBusy = false;
        if (m_rxLevel)
        {
            receive(static_cast<uint8_t>(m_rxShift));
        }
        else
        {
            std::fprintf(stderr, "%s t=%.6f s UART0: framing error on RX (stop bit low)\n",
                         kTag, now());
        }
        return 0;
    }
    ++m_rxBit;
    return when + bitCycles();
}

/** One received byte: to simavr (if the receiver is on) and to the log. */
void Uart0::receive(uint8_t value)
{
    if (!avr_regbit_get(m_avr, m_uart->rxen))
    {
        ++m_dropped;    // simavr would silently ignore it.
        m_rxLineLost = true;
    }
    else
    {
        avr_raise_irq(avr_io_getirq(m_avr, AVR_IOCTL_UART_GETIRQ('0'), UART_IRQ_INPUT), value);
    }

    if (value == '\n')
    {
        flushRxLine();
    }
    else if (value != '\r')
    {
        if (m_rxLine.empty())
        {
            m_rxLineTime = now();
        }
        m_rxLine.push_back((value >= 0x20 && value < 0x7F) ? static_cast<char>(value) : '.');
        if (m_rxLine.size() >= 200u)
        {
            flushRxLine();
        }
    }
}

void Uart0::flushRxLine()
{
    if (m_rxLine.empty())
    {
        return;
    }
    std::fprintf(stderr, "%s t=%.6f s UART0< %s%s\n", kTag, m_rxLineTime, m_rxLine.c_str(),
                 m_rxLineLost ? "   (lost: receiver off, RXEN = 0)" : "");
    m_rxLine.clear();
    m_rxLineLost = false;
}

} // namespace kisimavr
