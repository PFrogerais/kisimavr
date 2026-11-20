/*
 * kisimavr - ATmega co-simulation for ngspice / KiCad
 * Copyright (C) 2026 Paul
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "mcu_bridge.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <utility>

#include <sim_hex.h>
#include <avr_ioport.h>

#include "common.h"
#include "paths.h"

namespace kisimavr
{

namespace
{

void noSleep(avr_t * /*avr*/, avr_cycle_count_t /*howLong*/)
{
    // simavr sleeps in real time by default: SPICE drives time here.
}

void onPortWrite(avr_irq_t * /*irq*/, uint32_t value, void *param)
{
    auto *watch = static_cast<PortWatch *>(param);
    watch->port = static_cast<uint8_t>(value);
    *watch->dirty = true;
}

void onDdrWrite(avr_irq_t * /*irq*/, uint32_t value, void *param)
{
    auto *watch = static_cast<PortWatch *>(param);
    watch->ddr = static_cast<uint8_t>(value);
    *watch->dirty = true;
}

} // namespace

AvrHandle::~AvrHandle()
{
    if (m_avr != nullptr)
    {
        avr_terminate(m_avr);
        std::free(m_avr);
    }
}

McuBridge::McuBridge(Config cfg)
    : m_cfg(std::move(cfg))
{
}

bool McuBridge::start()
{
    m_avr.reset(avr_make_mcu_by_name(m_cfg.mcuName.c_str()));
    if (m_avr.get() == nullptr)
    {
        std::fprintf(stderr, "%s unknown MCU '%s'\n", kTag, m_cfg.mcuName.c_str());
        return false;
    }

    avr_init(m_avr.get());
    m_avr->frequency = m_cfg.frequency;
    m_avr->sleep     = noSleep;

    const std::string hexPath = resolvePath(m_cfg.hexPath);
    if (hexPath != m_cfg.hexPath)
    {
        std::fprintf(stderr, "%s firmware path case restored: %s\n", kTag, hexPath.c_str());
        m_cfg.hexPath = hexPath;
    }
    m_cfg.uartLog = resolveOutputPath(m_cfg.uartLog);
    m_cfg.vcdPath = resolveOutputPath(m_cfg.vcdPath);

    if (!loadHex())
    {
        return false;
    }

    // Register every port used by the configuration.
    for (const auto *pins : { &m_cfg.inputs, &m_cfg.outputs, &m_cfg.ios })
    {
        for (const PinRef &pin : *pins)
        {
            if (!watchPort(pin.port))
            {
                return false;
            }
        }
    }

    if (!m_cfg.adcChannels.empty() && !m_sar.start(m_avr.get(), m_cfg))
    {
        return false;
    }
    if (!m_uart.start(m_avr.get(), m_cfg))
    {
        return false;
    }
    m_i2c.start(m_avr.get(), m_cfg);
    m_spi.start(m_avr.get(), m_cfg);

    // VCD signals: "in", "io" (voltage read back), then "out" pins.
    std::vector<PinRef> traced = m_cfg.inputs;
    traced.insert(traced.end(), m_cfg.ios.begin(), m_cfg.ios.end());
    traced.insert(traced.end(), m_cfg.outputs.begin(), m_cfg.outputs.end());
    m_vcd.open(m_cfg.vcdPath, m_cfg.mcuName, traced, m_cfg.frequency);

    m_lastOut.assign(outputPortCount(m_cfg), Digital_t{ ZERO, STRONG });
    // No publication at t = 0: d_cosim starts every output at ZERO and
    // a change reported at vtime 0 would arrive "in the past".
    // Only real changes, from the first instructions on, are sent.
    m_dirty = false;

    std::fprintf(stderr, "%s %s @ %u Hz running %s (%zu in, %zu out, %zu io, %zu ADC)\n",
                 kTag, m_cfg.mcuName.c_str(), m_cfg.frequency, m_cfg.hexPath.c_str(),
                 m_cfg.inputs.size(), m_cfg.outputs.size(), m_cfg.ios.size(),
                 m_cfg.adcChannels.size());
    return true;
}

void McuBridge::step(co_info *info)
{
    const double target = info->vtime;

    // Changes left over from a previous call: keep vtime unchanged.
    if (m_dirty && publishOutputs(info))
    {
        return;
    }

    const auto targetCycle =
        static_cast<avr_cycle_count_t>(std::floor(target * m_cfg.frequency));

    while (m_running && m_avr->cycle < targetCycle)
    {
        const int state = avr_run(m_avr.get());
        if (state == cpu_Done || state == cpu_Crashed)
        {
            std::fprintf(stderr, "%s MCU stopped (%s) at t=%.9f s\n", kTag,
                         state == cpu_Done ? "done" : "crashed", now());
            m_running = false;
        }

        if (m_dirty && publishOutputs(info))
        {
            info->vtime = std::min(now(), target);
            return;
        }
    }

    // CPU halted (e.g. main() returned: "cli; rjmp ."): peripherals keep
    // running, so the UART can finish its frames and the SAR keeps going.
    while (!m_running && m_avr->cycle < targetCycle)
    {
        const avr_cycle_count_t next = avr_cycle_timer_process(m_avr.get());
        m_avr->cycle += std::max<avr_cycle_count_t>(
            1, std::min<avr_cycle_count_t>(next, targetCycle - m_avr->cycle));

        if (m_dirty && publishOutputs(info))
        {
            info->vtime = std::min(now(), target);
            return;
        }
    }
}

void McuBridge::input(unsigned int index, const Digital_t *value)
{
    const size_t pins = m_cfg.inputs.size() + m_cfg.ios.size();
    if (index >= pins)
    {
        // Comparator of an ADC channel: true when Vin > Vdac.
        m_sar.setComparator(index - pins, value->state == ONE);
        return;
    }

    // "in" pin, or voltage read back on an "io" pin.
    const PinRef &pin = (index < m_cfg.inputs.size())
                            ? m_cfg.inputs[index]
                            : m_cfg.ios[index - m_cfg.inputs.size()];
    const uint32_t level = (value->state == ONE) ? 1u : 0u;
    PortWatch &watch = m_ports.at(pin.port);

    // Make the circuit voltage the "external" level of this pin, so that
    // simavr's own pull-up emulation never overrides it (the pull-up is
    // modelled electrically in SPICE).
    const uint8_t mask = static_cast<uint8_t>(1u << pin.bit);
    watch.extMask  = static_cast<uint8_t>(watch.extMask | mask);
    watch.extValue = static_cast<uint8_t>(level ? (watch.extValue | mask)
                                                : (watch.extValue & ~mask));
    avr_ioport_external_t ext{};
    ext.name  = static_cast<unsigned long>(pin.port);
    ext.mask  = watch.extMask;
    ext.value = watch.extValue;
    avr_ioctl(m_avr.get(), AVR_IOCTL_IOPORT_SET_EXTERNAL(pin.port), &ext);

    avr_raise_irq(watch.pinIrq[pin.bit], level);
    m_uart.pinInput(pin, level != 0u);
    m_spi.pinInput(pin, level != 0u);
    m_vcd.write(index, m_avr->cycle, *value);

    if (m_cfg.log)
    {
        std::fprintf(stderr, "%s t=%.9f in  P%c%u = %u\n", kTag, now(), pin.port, pin.bit, level);
    }
}

double McuBridge::now() const
{
    return static_cast<double>(m_avr->cycle) / m_cfg.frequency;
}

bool McuBridge::loadHex()
{
    // read_ihex_file() does not handle a missing file gracefully.
    std::FILE *probe = std::fopen(m_cfg.hexPath.c_str(), "r");
    if (probe == nullptr)
    {
        std::fprintf(stderr, "%s cannot open firmware '%s'\n", kTag, m_cfg.hexPath.c_str());
        return false;
    }
    std::fclose(probe);

    uint32_t size  = 0;
    uint32_t base  = 0;
    uint8_t *image = read_ihex_file(m_cfg.hexPath.c_str(), &size, &base);
    if (image == nullptr || size == 0u)
    {
        std::fprintf(stderr, "%s invalid HEX file '%s'\n", kTag, m_cfg.hexPath.c_str());
        std::free(image);
        return false;
    }

    // avr_loadcode() aborts on overflow: check first.
    if (static_cast<uint64_t>(base) + size > static_cast<uint64_t>(m_avr->flashend) + 1u)
    {
        std::fprintf(stderr, "%s firmware (%u bytes at 0x%X) exceeds flash\n", kTag, size, base);
        std::free(image);
        return false;
    }

    avr_loadcode(m_avr.get(), image, size, base);
    std::free(image);
    return true;
}

bool McuBridge::watchPort(char name)
{
    if (m_ports.count(name) != 0u)
    {
        return true;
    }

    const uint32_t ioctl = AVR_IOCTL_IOPORT_GETIRQ(name);
    avr_irq_t *portIrq = avr_io_getirq(m_avr.get(), ioctl, IOPORT_IRQ_REG_PORT);
    avr_irq_t *ddrIrq  = avr_io_getirq(m_avr.get(), ioctl, IOPORT_IRQ_DIRECTION_ALL);
    if (portIrq == nullptr || ddrIrq == nullptr)
    {
        std::fprintf(stderr, "%s port %c does not exist on %s\n", kTag, name,
                     m_cfg.mcuName.c_str());
        return false;
    }

    PortWatch &watch = m_ports[name];
    watch.dirty = &m_dirty;
    watch.name  = name;
    for (int bit = 0; bit < 8; ++bit)
    {
        watch.pinIrq[bit] = avr_io_getirq(m_avr.get(), ioctl, IOPORT_IRQ_PIN0 + bit);
    }

    avr_irq_register_notify(portIrq, onPortWrite, &watch);
    avr_irq_register_notify(ddrIrq, onDdrWrite, &watch);
    return true;
}

/**
 * SPI master: SCK and MOSI follow the SPI when their DDR bit is set (as on
 * the chip), MISO is forced to an input. level / oe are changed in place;
 * returns true when the pin is an SPI pin.
 */
bool McuBridge::spiOverride(const PinRef &pin, bool &level, bool &oe) const
{
    if (!m_spi.master())
    {
        return false;
    }
    if (pin == m_spi.misoPin())
    {
        oe = false;
        return true;
    }
    const bool sck = pin == m_spi.sckPin();
    if ((sck || pin == m_spi.mosiPin()) && oe)
    {
        level = sck ? m_spi.sckLine() : m_spi.mosiLine();
        return true;
    }
    return false;
}

/** Electrical state of an "out" pin from its PORT/DDR bits. */
Digital_t McuBridge::pinValue(const PinRef &pin) const
{
    const PortWatch &watch = m_ports.at(pin.port);
    bool isOutput = ((watch.ddr  >> pin.bit) & 1u) != 0u;
    bool portBit  = ((watch.port >> pin.bit) & 1u) != 0u;
    spiOverride(pin, portBit, isOutput);

    if (isOutput)
    {
        return Digital_t{ portBit ? ONE : ZERO, STRONG };
    }
    if (portBit)
    {
        return Digital_t{ ONE, RESISTIVE };    // Internal pull-up (PUD ignored).
    }
    return Digital_t{ UNKNOWN, HI_IMPEDANCE };  // Floating input.
}

/** Value of output port i: "out" pins, (level, oe, pu) per "io", DAC bits. */
Digital_t McuBridge::outputValue(size_t i) const
{
    if (static_cast<int>(i) == m_uart.txOutIndex() && m_uart.ownsTxPin())
    {
        return Digital_t{ m_uart.txLine() ? ONE : ZERO, STRONG };    // UART owns TXD.
    }
    if (i < m_cfg.outputs.size())
    {
        return pinValue(m_cfg.outputs[i]);
    }

    const size_t ioBase = m_cfg.outputs.size();
    if (i < ioBase + 3u * m_cfg.ios.size())
    {
        const size_t k    = (i - ioBase) / 3u;
        const size_t kind = (i - ioBase) % 3u;    // 0 level, 1 oe, 2 pu
        const PinRef &pin = m_cfg.ios[k];
        const PortWatch &watch = m_ports.at(pin.port);

        bool level = ((watch.port >> pin.bit) & 1u) != 0u;
        bool oe    = ((watch.ddr  >> pin.bit) & 1u) != 0u;
        bool pu    = !oe && level;                // PORT = 1 on an input.
        {
            const bool port = level;
            if (spiOverride(pin, level, oe) && !oe)
            {
                pu = port;                        // MISO: pull-up follows PORT.
            }
        }
        const bool sda = static_cast<int>(k) == m_i2c.sdaIoIndex();
        if ((sda || static_cast<int>(k) == m_i2c.sclIoIndex()) && m_i2c.ownsPins())
        {
            // TWI owns SDA / SCL: open drain, pull low or release. The
            // internal pull-up still follows the PORT bit (Wire sets it).
            pu    = level;
            oe    = !(sda ? m_i2c.sdaLine() : m_i2c.sclLine());
            level = false;
        }
        if (static_cast<int>(k) == m_uart.rxIoIndex() && m_uart.ownsRxPin())
        {
            // RXEN: RXD is an input, the pull-up still follows PORTD0.
            pu    = level;
            oe    = false;
        }
        if (static_cast<int>(k) == m_uart.txIoIndex() && m_uart.ownsTxPin())
        {
            level = m_uart.txLine();              // UART owns TXD.
            oe    = true;
            pu    = false;
        }
        const bool bit = (kind == 0u) ? level : ((kind == 1u) ? oe : pu);
        return Digital_t{ bit ? ONE : ZERO, STRONG };
    }

    const unsigned int bit = static_cast<unsigned int>(i - ioBase - 3u * m_cfg.ios.size());
    return Digital_t{ ((m_sar.dacCode() >> bit) & 1u) ? ONE : ZERO, STRONG };
}

/** Send changed outputs to ngspice. Returns true if any changed. */
bool McuBridge::publishOutputs(co_info *info)
{
    m_dirty = false;
    bool changed = false;

    for (size_t i = 0; i < m_lastOut.size(); ++i)
    {
        Digital_t value = outputValue(i);
        if (value.state != m_lastOut[i].state || value.strength != m_lastOut[i].strength)
        {
            m_lastOut[i] = value;
            if (i < m_cfg.outputs.size())
            {
                m_vcd.write(m_cfg.inputs.size() + m_cfg.ios.size() + i, m_avr->cycle, value);
            }
            info->out_fn(info, static_cast<unsigned int>(i), &value);
            changed = true;

            if (m_cfg.log && i < m_cfg.outputs.size())
            {
                static const char *const kStates[] = { "0", "1", "X" };
                std::fprintf(stderr, "%s t=%.9f out P%c%u = %s\n", kTag, now(),
                             m_cfg.outputs[i].port, m_cfg.outputs[i].bit,
                             kStates[value.state]);
            }
        }
    }
    return changed;
}

} // namespace kisimavr
