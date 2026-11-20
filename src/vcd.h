/*
 * kisimavr - ATmega co-simulation for ngspice / KiCad
 * Copyright (C) 2026 Paul
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file vcd.h
 * @brief VCD trace of the GPIO pins, for GTKWave / PulseView (1 ns).
 */

#ifndef KISIMAVR_VCD_H
#define KISIMAVR_VCD_H

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "config.h"
#include "cosim_abi.h"

namespace kisimavr
{

class VcdWriter
{
public:
    /** Largest number of signals (one printable identifier each). */
    static constexpr size_t kMaxSignals = 90;

    VcdWriter() = default;
    ~VcdWriter();
    VcdWriter(const VcdWriter &) = delete;
    VcdWriter &operator=(const VcdWriter &) = delete;

    /**
     * Create the file and write the header: one wire per pin, all at 0
     * (d_cosim starts every node at 0). Failures are reported and leave
     * the writer closed: the trace is optional.
     */
    void open(const std::string &path, const std::string &scope,
              const std::vector<PinRef> &pins, double frequency);

    bool isOpen() const { return m_file != nullptr; }

    /** Value change of signal (index in the pin list) at a CPU cycle. */
    void write(size_t signal, uint64_t cycle, const Digital_t &value);

private:
    std::FILE *m_file      = nullptr;
    double     m_frequency = 1.0;
    uint64_t   m_lastCycle = ~uint64_t{ 0 };
};

} // namespace kisimavr

#endif // KISIMAVR_VCD_H
