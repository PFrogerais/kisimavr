/*
 * kisimavr - ATmega co-simulation for ngspice / KiCad
 * Copyright (C) 2026 Paul
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "vcd.h"

#include <cmath>

#include "common.h"
#include "paths.h"

namespace kisimavr
{

namespace
{

char identifier(size_t signal)
{
    return static_cast<char>('!' + signal);
}

} // namespace

VcdWriter::~VcdWriter()
{
    if (m_file != nullptr)
    {
        std::fclose(m_file);
    }
}

void VcdWriter::open(const std::string &path, const std::string &scope,
                     const std::vector<PinRef> &pins, double frequency)
{
    if (path.empty())
    {
        return;
    }
    if (pins.size() > kMaxSignals)
    {
        std::fprintf(stderr, "%s too many pins for the VCD trace\n", kTag);
        return;
    }
    m_file = openOutputFile(path, "VCD", { ".vcd" }, looksLikeVcd);
    if (m_file == nullptr)
    {
        return;
    }
    m_frequency = frequency;

    std::fprintf(m_file, "$timescale 1ns $end\n$scope module %s $end\n", scope.c_str());
    for (size_t k = 0; k < pins.size(); ++k)
    {
        std::fprintf(m_file, "$var wire 1 %c P%c%u $end\n", identifier(k), pins[k].port,
                     pins[k].bit);
    }
    std::fprintf(m_file, "$upscope $end\n$enddefinitions $end\n#0\n$dumpvars\n");
    for (size_t k = 0; k < pins.size(); ++k)
    {
        std::fprintf(m_file, "0%c\n", identifier(k));
    }
    std::fprintf(m_file, "$end\n");
    m_lastCycle = 0;
}

void VcdWriter::write(size_t signal, uint64_t cycle, const Digital_t &value)
{
    if (m_file == nullptr)
    {
        return;
    }
    if (cycle != m_lastCycle)
    {
        const double ns = static_cast<double>(cycle) * 1e9 / m_frequency;
        std::fprintf(m_file, "#%llu\n", static_cast<unsigned long long>(std::llround(ns)));
        m_lastCycle = cycle;
    }
    char level = 'x';
    if (value.strength == HI_IMPEDANCE)
    {
        level = 'z';
    }
    else if (value.state == ONE)
    {
        level = '1';
    }
    else if (value.state == ZERO)
    {
        level = '0';
    }
    std::fprintf(m_file, "%c%c\n", level, identifier(signal));
}

} // namespace kisimavr
