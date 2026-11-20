/*
 * kisimavr - ATmega co-simulation for ngspice / KiCad
 * Copyright (C) 2026 Paul
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file mcu_bridge.h
 * @brief One simavr MCU driven by one d_cosim instance.
 *
 * Timing model: the MCU runs cycle by cycle until it reaches the SPICE
 * target time (vtime). When an output changes, execution pauses and vtime
 * is lowered to the cycle time of the change, as required by d_cosim.
 *
 * GPIO: "in" pins are driven by the circuit, "out" pins by the MCU, and an
 * "io" pin has 1 input (pin voltage read back) and 3 outputs (level = PORT
 * bit, oe = DDR bit, pu = pull-up enabled), so that its direction follows
 * DDRx and the pull-up is modelled electrically in SPICE.
 */

#ifndef KISIMAVR_MCU_BRIDGE_H
#define KISIMAVR_MCU_BRIDGE_H

#include <cstdint>
#include <map>
#include <vector>

#include <sim_avr.h>

#include "config.h"
#include "cosim_abi.h"
#include "sar_adc.h"
#include "uart0.h"
#include "vcd.h"

namespace kisimavr
{

/** Owns an avr_t: terminated and freed last (declared first in McuBridge). */
class AvrHandle
{
public:
    AvrHandle() = default;
    ~AvrHandle();
    AvrHandle(const AvrHandle &) = delete;
    AvrHandle &operator=(const AvrHandle &) = delete;

    void reset(avr_t *avr) { m_avr = avr; }
    avr_t *get() const { return m_avr; }
    avr_t *operator->() const { return m_avr; }

private:
    avr_t *m_avr = nullptr;
};

/** Mirror of the PORTx / DDRx registers of one I/O port. */
struct PortWatch
{
    bool       *dirty = nullptr;
    char        name  = 0;
    uint8_t     port  = 0;
    uint8_t     ddr   = 0;
    uint8_t     extMask  = 0;    // Pins whose level comes from the circuit.
    uint8_t     extValue = 0;
    avr_irq_t  *pinIrq[8] = {};
};

class McuBridge
{
public:
    explicit McuBridge(Config cfg);
    McuBridge(const McuBridge &) = delete;
    McuBridge &operator=(const McuBridge &) = delete;

    /** Create the MCU, load the firmware and hook the peripherals. */
    bool start();

    /** d_cosim step(): run until vtime, or until the first output change. */
    void step(co_info *info);

    /** d_cosim in_fn(): an input node changed. */
    void input(unsigned int index, const Digital_t *value);

private:
    double now() const;
    bool loadHex();
    bool watchPort(char name);
    Digital_t pinValue(const PinRef &pin) const;
    Digital_t outputValue(size_t i) const;
    bool publishOutputs(co_info *info);

    // Declared first: destroyed last, after the peripherals that use it.
    AvrHandle                  m_avr;
    Config                     m_cfg;
    bool                       m_running = true;
    bool                       m_dirty   = false;
    std::map<char, PortWatch>  m_ports;      // std::map: stable addresses.
    std::vector<Digital_t>     m_lastOut;
    SarAdc                     m_sar{ m_dirty };
    Uart0                      m_uart{ m_dirty };
    VcdWriter                  m_vcd;
};

} // namespace kisimavr

#endif // KISIMAVR_MCU_BRIDGE_H
