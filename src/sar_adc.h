/*
 * kisimavr - ATmega co-simulation for ngspice / KiCad
 * Copyright (C) 2026 Paul
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file sar_adc.h
 * @brief Successive-approximation ADC rebuilt across the d_cosim boundary.
 *
 * d_cosim only carries digital values, so:
 *   - the plug-in drives a 10-bit DAC code (10 extra outputs, LSB first),
 *     turned into Vdac = vref * code / 1024 by a behavioural source;
 *   - one comparator per channel returns (Vin > Vdac) as an extra input;
 *   - a continuous SAR loop runs channel after channel, one bit every
 *     "adcbit" seconds, and hands each result to the simavr ADC (mV).
 */

#ifndef KISIMAVR_SAR_ADC_H
#define KISIMAVR_SAR_ADC_H

#include <cstdint>
#include <vector>

#include <sim_avr.h>
#include <avr_adc.h>

#include "config.h"

namespace kisimavr
{

class SarAdc
{
public:
    /** dirty is set whenever the DAC code changes. */
    explicit SarAdc(bool &dirty) : m_dirty(dirty) {}
    SarAdc(const SarAdc &) = delete;
    SarAdc &operator=(const SarAdc &) = delete;

    /** Hook the ADC channels of cfg and start the SAR step timer. */
    bool start(avr_t *avr, const Config &cfg);

    /** Comparator of channel index ch (position in cfg.adcChannels). */
    void setComparator(size_t ch, bool vinAboveVdac);
    size_t channelCount() const { return m_cmp.size(); }

    /** Code currently applied to the DAC (outputs b0..b9). */
    uint32_t dacCode() const { return m_dacCode; }

private:
    static avr_cycle_count_t onTimer(avr_t *avr, avr_cycle_count_t when, void *param);
    void tick();
    void deliverConversion(size_t ch, uint32_t code);

    bool                    &m_dirty;
    avr_t                   *m_avr = nullptr;
    avr_adc_t               *m_adc = nullptr;    // For the ADEN bit.
    std::vector<int>         m_channels;
    std::vector<avr_irq_t *> m_irq;
    std::vector<bool>        m_cmp;
    bool                     m_log       = false;
    avr_cycle_count_t        m_stepCycles = 0;
    uint32_t                 m_vrefMv    = 5000;
    size_t                   m_channel   = 0;
    int                      m_bit       = kDacBits - 1;
    bool                     m_idle      = true;
    uint32_t                 m_dacCode   = 0;
};

} // namespace kisimavr

#endif // KISIMAVR_SAR_ADC_H
