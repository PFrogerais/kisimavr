/*
 * kisimavr - ATmega co-simulation for ngspice / KiCad
 * Copyright (C) 2026 Paul
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file rp2040_device.h
 * @brief RP2040 / RP2350 (Raspberry Pi Pico / Pico 2) co-simulated in QuickJS.
 *
 * rp2040js (third_party/rp2040js) or rp2350js (third_party/rp2350js), in
 * JavaScript, run inside the plug-in on an embedded QuickJS engine, through
 * rp2040/bridge.js or rp2040/rp2350_bridge.js: no Node.js process,
 * so it also works with KiCad as a Flatpak. Each d_cosim step runs the
 * emulator up to vtime, or up to the first pin change.
 */

#ifndef KISIMAVR_RP2040_DEVICE_H
#define KISIMAVR_RP2040_DEVICE_H

#include <cstdio>
#include <string>
#include <vector>

#include "cosim_device.h"
#include "pty_port.h"
#include "rp2040_config.h"

struct JSRuntime;
struct JSContext;

namespace kisimavr
{

class Rp2040Device : public CosimDevice
{
public:
    explicit Rp2040Device(Rp2040Config cfg);
    ~Rp2040Device() override;

    /** Start QuickJS, load bridge.js and the firmware. */
    bool start();

    void step(co_info *info) override;
    void input(co_info *info, unsigned int index, const Digital_t *value) override;

private:
    struct Js;    // QuickJS handles (keeps quickjs.h out of this header).

    bool call(const char *method, int argc, const double *argv, std::vector<double> *result);
    void printLines(bool final);
    void exchangePty();
    void fail(const char *what);
    const char *label() const;    // "RP2040" or "RP2350"

    Rp2040Config          m_cfg;
    Js                   *m_js = nullptr;
    bool                  m_ok = false;
    bool                  m_first = true;
    std::vector<int>      m_lastOut;
    std::FILE            *m_log = nullptr;
    PtyPort               m_pty;          // UART0
    PtyPort               m_ptyUsb;       // USB serial
};

} // namespace kisimavr

#endif // KISIMAVR_RP2040_DEVICE_H
