/*
 * kisimavr - ATmega co-simulation for ngspice / KiCad
 * Copyright (C) 2026 Paul
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file cosim_device.h
 * @brief What one d_cosim instance runs: an MCU, or a circuit device (LCD).
 */

#ifndef KISIMAVR_COSIM_DEVICE_H
#define KISIMAVR_COSIM_DEVICE_H

#include "cosim_abi.h"

namespace kisimavr
{

class CosimDevice
{
public:
    CosimDevice() = default;
    virtual ~CosimDevice() = default;
    CosimDevice(const CosimDevice &) = delete;
    CosimDevice &operator=(const CosimDevice &) = delete;

    /** d_cosim step(): advance to info->vtime (lower it on an output change). */
    virtual void step(co_info *info) = 0;

    /** d_cosim in_fn(): input index changed at time info->vtime. */
    virtual void input(co_info *info, unsigned int index, const Digital_t *value) = 0;
};

} // namespace kisimavr

#endif // KISIMAVR_COSIM_DEVICE_H
