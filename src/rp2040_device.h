/*
 * kisimavr - ATmega co-simulation for ngspice / KiCad
 * Copyright (C) 2026 Paul
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file rp2040_device.h
 * @brief RP2040 (Raspberry Pi Pico) co-simulated with rp2040js in QuickJS.
 *
 * rp2040js (third_party/rp2040js, JavaScript) runs inside the plug-in on an
 * embedded QuickJS engine, through rp2040/bridge.js: no Node.js process,
 * so it also works with KiCad as a Flatpak. Each d_cosim step runs the
 * emulator up to vtime, or up to the first pin change.
 */

#ifndef KISIMAVR_RP2040_DEVICE_H
#define KISIMAVR_RP2040_DEVICE_H

#include <cstdio>
#include <string>
#include <vector>

#include "cosim_device.h"
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
    void fail(const char *what);

    Rp2040Config          m_cfg;
    Js                   *m_js = nullptr;
    bool                  m_ok = false;
    bool                  m_first = true;
    std::vector<int>      m_lastOut;
    std::FILE            *m_log = nullptr;
};

} // namespace kisimavr

#endif // KISIMAVR_RP2040_DEVICE_H
