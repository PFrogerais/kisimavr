/*
 * kisimavr - ATmega co-simulation for ngspice / KiCad
 * Copyright (C) 2026 Paul
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "sar_adc.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "common.h"

namespace kisimavr
{

bool SarAdc::start(avr_t *avr, const Config &cfg)
{
    m_avr      = avr;
    m_channels = cfg.adcChannels;
    m_log      = cfg.log;

    m_stepCycles = static_cast<avr_cycle_count_t>(std::llround(cfg.adcBit * cfg.frequency));
    if (m_stepCycles == 0u)
    {
        std::fprintf(stderr, "%s adcbit shorter than one CPU cycle\n", kTag);
        return false;
    }

    for (int ch : m_channels)
    {
        avr_irq_t *irq = avr_io_getirq(m_avr, AVR_IOCTL_ADC_GETIRQ, ADC_IRQ_ADC0 + ch);
        if (irq == nullptr)
        {
            std::fprintf(stderr, "%s no ADC channel %d on %s\n", kTag, ch, cfg.mcuName.c_str());
            return false;
        }
        m_irq.push_back(irq);
    }
    m_cmp.assign(m_channels.size(), false);

    for (avr_io_t *io = m_avr->io_port; io != nullptr; io = io->next)
    {
        if (io->kind != nullptr && std::strcmp(io->kind, "adc") == 0)
        {
            m_adc = reinterpret_cast<avr_adc_t *>(io);
        }
    }

    // AVCC, VCC and AREF all equal to the DAC full scale.
    m_vrefMv = static_cast<uint32_t>(std::lround(cfg.vref * 1000.0));
    m_avr->vcc  = m_vrefMv;
    m_avr->avcc = m_vrefMv;
    m_avr->aref = m_vrefMv;

    m_channel = 0;
    m_idle    = true;    // DAC stays at 0, as d_cosim assumes at t = 0.
    m_dacCode = 0;
    avr_cycle_timer_register(m_avr, m_stepCycles, onTimer, this);
    return true;
}

void SarAdc::setComparator(size_t ch, bool vinAboveVdac)
{
    if (ch < m_cmp.size())
    {
        m_cmp[ch] = vinAboveVdac;
    }
}

avr_cycle_count_t SarAdc::onTimer(avr_t * /*avr*/, avr_cycle_count_t when, void *param)
{
    auto *adc = static_cast<SarAdc *>(param);
    adc->tick();
    return when + adc->m_stepCycles;    // Periodic timer.
}

/**
 * One SAR step: keep or drop the bit under test according to the
 * comparator of the current channel, then try the next bit.
 */
void SarAdc::tick()
{
    const size_t ch = m_channel;

    // ADC off (ADEN = 0): no conversion, no DAC activity, no SPICE events.
    if (m_adc != nullptr && !avr_regbit_get(m_avr, m_adc->aden))
    {
        m_idle = true;
        return;
    }

    if (m_idle)
    {
        // First tick: start the first conversion from code 0.
        m_idle    = false;
        m_bit     = kDacBits - 1;
        m_dacCode = 1u << m_bit;
        m_dirty   = true;
        return;
    }

    if (m_bit >= 0)
    {
        if (!m_cmp[ch])
        {
            m_dacCode &= ~(1u << m_bit);    // Vin < Vdac: drop the bit.
        }
        --m_bit;
    }

    if (m_bit >= 0)
    {
        m_dacCode |= (1u << m_bit);         // Try the next bit.
    }
    else
    {
        deliverConversion(ch, m_dacCode);
        m_channel = (ch + 1) % m_channels.size();
        m_bit     = kDacBits - 1;           // Start the next conversion.
        m_dacCode = 1u << m_bit;
    }
    m_dirty = true;
}

/**
 * Hand a finished conversion to simavr, in millivolts. The value is
 * rounded up so that simavr's own scaling (mV * 1023 / Vref) gives back
 * exactly this code when the AVCC reference is selected.
 */
void SarAdc::deliverConversion(size_t ch, uint32_t code)
{
    const uint32_t mv = static_cast<uint32_t>(
        (static_cast<uint64_t>(code) * m_vrefMv + 1022u) / 1023u);
    avr_raise_irq(m_irq[ch], mv);

    if (m_log)
    {
        std::fprintf(stderr, "%s t=%.9f adc ADC%d = %u (%u mV)\n", kTag,
                     static_cast<double>(m_avr->cycle) / m_avr->frequency, m_channels[ch], code,
                     mv);
    }
}

} // namespace kisimavr
