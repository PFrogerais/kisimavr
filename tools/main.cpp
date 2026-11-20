/*
 * kisimavr - ATmega co-simulation for ngspice / KiCad
 * Copyright (C) 2026 Paul
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file main.cpp
 * @brief Stand-alone runner for a d_cosim plug-in (no ngspice needed).
 *
 * Loads the plug-in exactly like the XSPICE d_cosim code model does,
 * drives input 0 with an optional square wave, prints output changes,
 * writes a VCD file for GTKWave and reports the measured output periods.
 *
 * Usage:
 *   cosim_runner [-t duration_s] [-s step_s] [-p in0_period_s] [-o out.vcd]
 *                [-a vstart,vend[,vref]] plugin.so -- sim_args...
 *
 * -a emulates the analog side of the SAR ADC model for one channel: the
 * last 10 outputs are the DAC bits, the last input is the comparator, and
 * Vin ramps linearly from vstart to vend volts over the run.
 *
 * Example:
 *   cosim_runner -t 0.01 -p 0.004 -o blink.vcd ./mcu_cosim.so -- \
 *       hex blink.hex mcu atmega328p freq 16000000 in PD2 out PB5 PB0
 */

#include <dlfcn.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include "cosim_abi.h"

namespace
{

/** Per-output statistics and VCD state. */
struct Channel
{
    Digital_t            value{ ZERO, STRONG };
    std::vector<double>  risingEdges;
    unsigned int         changes = 0;
};

struct PendingOutput
{
    unsigned int index;
    Digital_t    value;
};

/** Ideal analog side of the SAR ADC: Vdac from the DAC bits, comparator. */
struct AnalogModel
{
    bool   enabled  = false;
    double vStart   = 0.0;
    double vEnd     = 0.0;
    double vref     = 5.0;
    double duration = 1.0;
    bool   cmp      = false;
};

struct Runner
{
    std::vector<Channel>       outputs;
    std::vector<PendingOutput> pending;
    AnalogModel                analog;
    std::FILE           *vcd = nullptr;
    bool                 outputSeen = false;
    double               lastVcdTime = -1.0;
};

Runner g_runner;    // out_fn has no user pointer besides co_info.

char vcdChar(const Digital_t &v)
{
    if (v.strength == HI_IMPEDANCE)
    {
        return 'z';
    }
    return v.state == ONE ? '1' : (v.state == ZERO ? '0' : 'x');
}

void vcdWrite(double time, const std::string &id, char level)
{
    if (g_runner.vcd == nullptr)
    {
        return;
    }
    if (time != g_runner.lastVcdTime)
    {
        std::fprintf(g_runner.vcd, "#%llu\n",
                     static_cast<unsigned long long>(std::llround(time * 1e9)));
        g_runner.lastVcdTime = time;
    }
    std::fprintf(g_runner.vcd, "%c%s\n", level, id.c_str());
}

/** Replaces ngspice's accept_output(): values are time-stamped later,
 *  because the plug-in lowers vtime after reporting, as d_cosim allows. */
void onOutput(co_info * /*info*/, unsigned int index, Digital_t *value)
{
    if (index < g_runner.outputs.size())
    {
        g_runner.pending.push_back({ index, *value });
        g_runner.outputSeen = true;
    }
}

void recordOutput(double now, unsigned int index, const Digital_t *value)
{
    Channel &ch = g_runner.outputs[index];
    if (ch.value.state != ONE && value->state == ONE && value->strength == STRONG)
    {
        ch.risingEdges.push_back(now);
    }
    ch.value = *value;
    ++ch.changes;

    vcdWrite(now, "o" + std::to_string(index), vcdChar(*value));
}

/** Recompute the comparator input from the DAC outputs and Vin(t). */
void updateAnalog(co_info *info, double now)
{
    AnalogModel &a = g_runner.analog;
    if (!a.enabled || info->out_count < 10 || info->in_count < 1)
    {
        return;
    }

    unsigned int code = 0;
    const unsigned int first = info->out_count - 10;
    for (unsigned int bit = 0; bit < 10; ++bit)
    {
        if (g_runner.outputs[first + bit].value.state == ONE)
        {
            code |= 1u << bit;
        }
    }
    const double vdac = a.vref * code / 1024.0;
    const double vin  = a.vStart + (a.vEnd - a.vStart) * std::min(now / a.duration, 1.0);
    const bool cmp = vin > vdac;
    if (cmp != a.cmp)
    {
        a.cmp = cmp;
        Digital_t v{ cmp ? ONE : ZERO, STRONG };
        info->in_fn(info, info->in_count - 1, &v);
    }
}

/** Advance the co-simulation to target, as d_cosim does between events. */
void runTo(co_info *info, double target)
{
    do
    {
        g_runner.outputSeen = false;
        g_runner.pending.clear();
        info->vtime = target;
        info->step(info);

        for (const PendingOutput &p : g_runner.pending)
        {
            recordOutput(info->vtime, p.index, &p.value);
        }
        updateAnalog(info, info->vtime);
    } while (g_runner.outputSeen && info->vtime < target);
}

void usage(const char *prog)
{
    std::fprintf(stderr,
                 "Usage: %s [-t duration_s] [-s step_s] [-p in0_period_s] [-o out.vcd]\n"
                 "          plugin.so -- sim_args...\n", prog);
}

} // namespace

int main(int argc, char *argv[])
{
    double      duration = 0.01;
    double      stepSize = 10e-6;
    double      period   = 0.0;
    const char *vcdPath  = nullptr;
    const char *plugin   = nullptr;
    std::vector<const char *> simArgs;

    // ---- Command line ------------------------------------------------------
    int i = 1;
    for (; i < argc; ++i)
    {
        const std::string a = argv[i];
        if (a == "--")
        {
            ++i;
            break;
        }
        if ((a == "-t" || a == "-s" || a == "-p" || a == "-o" || a == "-a") && i + 1 < argc)
        {
            const char *v = argv[++i];
            if (a == "-a")
            {
                AnalogModel &m = g_runner.analog;
                m.enabled = std::sscanf(v, "%lf,%lf,%lf", &m.vStart, &m.vEnd, &m.vref) >= 2;
            }
            else if (a == "-t") { duration = std::atof(v); }
            else if (a == "-s") { stepSize = std::atof(v); }
            else if (a == "-p") { period = std::atof(v); }
            else { vcdPath = v; }
        }
        else if (plugin == nullptr && a[0] != '-')
        {
            plugin = argv[i];
        }
        else
        {
            usage(argv[0]);
            return EXIT_FAILURE;
        }
    }
    for (; i < argc; ++i)
    {
        simArgs.push_back(argv[i]);
    }
    g_runner.analog.duration = duration;
    if (plugin == nullptr || duration <= 0.0 || stepSize <= 0.0)
    {
        usage(argv[0]);
        return EXIT_FAILURE;
    }
    simArgs.push_back(nullptr);

    // ---- Load the plug-in like d_cosim -------------------------------------
    void *lib = dlopen(plugin, RTLD_NOW | RTLD_GLOBAL);
    if (lib == nullptr)
    {
        std::fprintf(stderr, "dlopen: %s\n", dlerror());
        return EXIT_FAILURE;
    }
    using SetupFn = void (*)(co_info *);
    auto setup = reinterpret_cast<SetupFn>(dlsym(lib, "Cosim_setup"));
    if (setup == nullptr)
    {
        std::fprintf(stderr, "Cosim_setup not found in %s\n", plugin);
        return EXIT_FAILURE;
    }

    auto *info = static_cast<co_info *>(std::calloc(1, sizeof(co_info)));
    info->out_fn = onOutput;
    info->sim_argc = static_cast<unsigned int>(simArgs.size() - 1);
    *const_cast<const char ***>(&info->sim_argv) = simArgs.data();

    setup(info);
    if (info->step == nullptr || info->in_fn == nullptr)
    {
        std::fprintf(stderr, "plug-in did not provide step/in_fn\n");
        return EXIT_FAILURE;
    }
    std::printf("plug-in: %u inputs, %u outputs\n", info->in_count, info->out_count);
    g_runner.outputs.resize(info->out_count);

    // ---- VCD header --------------------------------------------------------
    if (vcdPath != nullptr)
    {
        g_runner.vcd = std::fopen(vcdPath, "w");
        if (g_runner.vcd != nullptr)
        {
            std::fprintf(g_runner.vcd, "$timescale 1ns $end\n$scope module mcu $end\n");
            for (unsigned int k = 0; k < info->out_count; ++k)
            {
                std::fprintf(g_runner.vcd, "$var wire 1 o%u out%u $end\n", k, k);
            }
            std::fprintf(g_runner.vcd, "$var wire 1 i0 in0 $end\n$upscope $end\n"
                                       "$enddefinitions $end\n");
        }
    }

    // ---- Simulation loop ---------------------------------------------------
    Digital_t level{ ZERO, STRONG };
    for (unsigned int k = 0; k < info->in_count; ++k)
    {
        info->in_fn(info, k, &level);    // Inputs settle at t = 0.
    }
    vcdWrite(0.0, "i0", '0');
    runTo(info, 0.0);

    double nextToggle = (period > 0.0 && info->in_count > 0)
                            ? period / 2.0
                            : std::numeric_limits<double>::infinity();
    double t = 0.0;
    while (t < duration)
    {
        const double next = std::min({ t + stepSize, nextToggle, duration });
        runTo(info, next);
        if (next == nextToggle)
        {
            level.state = (level.state == ONE) ? ZERO : ONE;
            info->in_fn(info, 0, &level);
            vcdWrite(next, "i0", level.state == ONE ? '1' : '0');
            nextToggle += period / 2.0;
        }
        t = next;
    }

    // ---- Report ------------------------------------------------------------
    {
        unsigned int word = 0;
        const unsigned int gpio = g_runner.analog.enabled && info->out_count >= 10
                                      ? info->out_count - 10 : info->out_count;
        const unsigned int n = std::min(gpio, 16u);
        for (unsigned int k = 0; k < n; ++k)
        {
            if (g_runner.outputs[k].value.state == ONE)
            {
                word |= 1u << k;
            }
        }
        std::printf("final outputs word (out0 = LSB, %u bits): %u\n", n, word);
    }
    for (unsigned int k = 0; k < info->out_count; ++k)
    {
        const Channel &ch = g_runner.outputs[k];
        std::printf("out%u: %u changes, %zu rising edges", k, ch.changes, ch.risingEdges.size());
        if (ch.risingEdges.size() >= 2)
        {
            const double span = ch.risingEdges.back() - ch.risingEdges.front();
            const double mean = span / static_cast<double>(ch.risingEdges.size() - 1);
            std::printf(", mean period %.6f ms (%.3f Hz)", mean * 1e3, 1.0 / mean);
        }
        std::printf("\n");
    }

    if (info->cleanup != nullptr)
    {
        info->cleanup(info);
    }
    if (g_runner.vcd != nullptr)
    {
        std::fclose(g_runner.vcd);
    }
    std::free(info);
    dlclose(lib);
    return EXIT_SUCCESS;
}
