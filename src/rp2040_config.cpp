/*
 * kisimavr - ATmega co-simulation for ngspice / KiCad
 * Copyright (C) 2026 Paul
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file rp2040_config.cpp
 * @brief sim_args of an RP2040 / RP2350 instance.
 */

#include "rp2040_config.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "common.h"

namespace kisimavr
{

namespace
{

bool isKey(const std::string &word)
{
    const std::string k = toLower(word);
    return k == "mcu" || k == "fw" || k == "io" || k == "adc" || k == "uartlog" ||
           k == "bridge" || k == "log" || k == "arch" || k == "pty" || k == "ptyusb";
}

} // namespace

bool isRp2040Config(const std::vector<std::string> &args)
{
    for (size_t i = 0; i + 1 < args.size(); ++i)
    {
        if (toLower(args[i]) == "mcu" &&
            (toLower(args[i + 1]) == "rp2040" || toLower(args[i + 1]) == "rp2350"))
        {
            return true;
        }
    }
    return false;
}

int parseGpio(const std::string &text)
{
    std::string s = toLower(text);
    if (s.rfind("gp", 0) == 0)
    {
        s.erase(0, 2);
    }
    if (s.empty() || s.size() > 2 || !std::all_of(s.begin(), s.end(), ::isdigit))
    {
        return -1;
    }
    const int n = std::atoi(s.c_str());
    return (n >= 0 && n <= 29) ? n : -1;
}

bool parseRp2040Config(const std::vector<std::string> &args, Rp2040Config &cfg)
{
    bool ok = true;
    size_t i = 0;
    while (i < args.size())
    {
        const std::string key = toLower(args[i]);
        if (!isKey(key))
        {
            std::fprintf(stderr, "%s RP2040: unknown key '%s'\n", kTag, args[i].c_str());
            return false;
        }
        size_t j = i + 1;
        std::vector<std::string> values;
        while (j < args.size() && !isKey(args[j]))
        {
            values.push_back(args[j]);
            ++j;
            if (key != "io" && key != "adc")
            {
                break;    // Single value.
            }
        }
        if (values.empty())
        {
            std::fprintf(stderr, "%s RP2040: key '%s' has no value\n", kTag, key.c_str());
            return false;
        }
        for (const std::string &v : values)
        {
            if (key == "io")
            {
                const int n = parseGpio(v);
                if (n < 0 || std::find(cfg.ios.begin(), cfg.ios.end(), n) != cfg.ios.end())
                {
                    std::fprintf(stderr, "%s RP2040: invalid or repeated GPIO '%s'\n", kTag,
                                 v.c_str());
                    ok = false;
                }
                else
                {
                    cfg.ios.push_back(n);
                }
            }
            else if (key == "adc")
            {
                const int c = (v.size() == 1 && v[0] >= '0' && v[0] <= '3') ? v[0] - '0' : -1;
                if (c < 0 || std::find(cfg.adc.begin(), cfg.adc.end(), c) != cfg.adc.end())
                {
                    std::fprintf(stderr, "%s RP2040: invalid ADC channel '%s' (0..3)\n", kTag,
                                 v.c_str());
                    ok = false;
                }
                else
                {
                    cfg.adc.push_back(c);
                }
            }
        }
        const std::string &value = values.front();
        if (key == "mcu")
        {
            const std::string chip = toLower(value);
            if (chip == "rp2040" || chip == "rp2350")
            {
                cfg.chip = chip;
            }
            else
            {
                ok = false;
            }
        }
        else if (key == "arch")
        {
            const std::string arch = toLower(value);
            if (arch == "riscv" || arch == "arm")
            {
                cfg.arch = arch;
            }
            else
            {
                std::fprintf(stderr, "%s RP2350: invalid arch '%s' (riscv or arm)\n", kTag,
                             value.c_str());
                ok = false;
            }
        }
        else if (key == "fw")
        {
            cfg.fwPath = value;
        }
        else if (key == "uartlog")
        {
            cfg.uartLog = value;
        }
        else if (key == "pty")
        {
            cfg.pty = value;
        }
        else if (key == "ptyusb")
        {
            cfg.ptyUsb = value;
        }
        else if (key == "bridge")
        {
            cfg.bridgePath = value;
        }
        else if (key == "log")
        {
            cfg.log = value != "0";
        }
        i = j;
    }
    if (!cfg.arch.empty() && cfg.chip != "rp2350")
    {
        std::fprintf(stderr, "%s RP2040: key \"arch\" is for the RP2350 only\n", kTag);
        ok = false;
    }
    if (cfg.fwPath.empty())
    {
        std::fprintf(stderr, "%s RP2040: missing firmware: add \"fw\" \"file.uf2\"\n", kTag);
        ok = false;
    }
    return ok;
}

unsigned int rp2040InputCount(const Rp2040Config &cfg)
{
    return static_cast<unsigned int>(cfg.ios.size() + cfg.adc.size());
}

unsigned int rp2040OutputCount(const Rp2040Config &cfg)
{
    return static_cast<unsigned int>(4u * cfg.ios.size() + kRp2040DacBits);
}

} // namespace kisimavr
