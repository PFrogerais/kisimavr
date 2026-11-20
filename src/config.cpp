/*
 * kisimavr - ATmega co-simulation for ngspice / KiCad
 * Copyright (C) 2026 Paul
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "config.h"

#include <cstdio>
#include <cstdlib>

#include "common.h"

namespace kisimavr
{

namespace
{

bool parsePinList(const std::string &list, std::vector<PinRef> &pins)
{
    size_t start = 0;
    while (start <= list.size())
    {
        const size_t comma = list.find(',', start);
        const std::string item = list.substr(start, comma - start);
        if (!item.empty())
        {
            PinRef pin{};
            if (!parsePin(item, pin))
            {
                std::fprintf(stderr, "%s invalid pin name '%s'\n", kTag, item.c_str());
                return false;
            }
            pins.push_back(pin);
        }
        if (comma == std::string::npos)
        {
            break;
        }
        start = comma + 1;
    }
    return true;
}

bool isKey(const std::string &word)
{
    const std::string k = toLower(word);
    return k == "hex" || k == "mcu" || k == "freq" || k == "in" || k == "out" || k == "io" ||
           k == "adc" || k == "vref" || k == "adcbit" || k == "txpin" ||
           k == "uartlog" || k == "vcd" || k == "log";
}

bool isMultiValued(const std::string &key)
{
    return key == "in" || key == "out" || key == "io" || key == "adc";
}

bool parseChannelList(const std::string &list, std::vector<int> &channels)
{
    size_t start = 0;
    while (start <= list.size())
    {
        const size_t comma = list.find(',', start);
        std::string item = toLower(list.substr(start, comma - start));
        if (item.rfind("adc", 0) == 0)
        {
            item.erase(0, 3);    // Accept "ADC0" as well as "0".
        }
        if (!item.empty())
        {
            char *end = nullptr;
            const long ch = std::strtol(item.c_str(), &end, 10);
            if (*end != '\0' || ch < 0 || ch > 15)
            {
                std::fprintf(stderr, "%s invalid ADC channel '%s'\n", kTag, item.c_str());
                return false;
            }
            channels.push_back(static_cast<int>(ch));
        }
        if (comma == std::string::npos)
        {
            break;
        }
        start = comma + 1;
    }
    return true;
}

/** Store one value for a key. Multi-valued keys may receive several values. */
bool applyValue(const std::string &key, const std::string &value, Config &cfg)
{
    if (key == "hex")
    {
        cfg.hexPath = value;
    }
    else if (key == "mcu")
    {
        cfg.mcuName = toLower(value);
    }
    else if (key == "freq")
    {
        cfg.frequency = static_cast<uint32_t>(std::strtoul(value.c_str(), nullptr, 10));
    }
    else if (key == "in")
    {
        return parsePinList(value, cfg.inputs);
    }
    else if (key == "out")
    {
        return parsePinList(value, cfg.outputs);
    }
    else if (key == "io")
    {
        return parsePinList(value, cfg.ios);
    }
    else if (key == "adc")
    {
        return parseChannelList(value, cfg.adcChannels);
    }
    else if (key == "vref")
    {
        cfg.vref = std::strtod(value.c_str(), nullptr);
    }
    else if (key == "adcbit")
    {
        cfg.adcBit = std::strtod(value.c_str(), nullptr);
    }
    else if (key == "txpin")
    {
        cfg.txPin = value;
    }
    else if (key == "uartlog")
    {
        cfg.uartLog = value;
    }
    else if (key == "vcd")
    {
        cfg.vcdPath = value;
    }
    else if (key == "log")
    {
        const std::string v = toLower(value);
        cfg.log = (v == "1" || v == "yes" || v == "true");
    }
    return true;
}

} // namespace

bool parsePin(const std::string &text, PinRef &pin)
{
    std::string s = text;
    if (s.size() == 3 && (s[0] == 'P' || s[0] == 'p'))
    {
        s.erase(0, 1);
    }
    if (s.size() != 2)
    {
        return false;
    }

    const char port = static_cast<char>(std::toupper(static_cast<unsigned char>(s[0])));
    const char bit  = s[1];
    if (port < 'A' || port > 'L' || bit < '0' || bit > '7')
    {
        return false;
    }

    pin.port = port;
    pin.bit  = static_cast<uint8_t>(bit - '0');
    return true;
}

bool parseConfig(const std::vector<std::string> &args, Config &cfg)
{
    std::string currentKey;    // Key waiting for its value(s).

    for (const std::string &arg : args)
    {
        if (arg.empty())
        {
            continue;
        }

        // Preferred netlist form: "key" "value" [...].
        if (isKey(arg))
        {
            currentKey = toLower(arg);
            continue;
        }

        // Compact form "key=value" or "key:value".
        const size_t sep = arg.find_first_of(":=");
        if (sep != std::string::npos && isKey(arg.substr(0, sep)))
        {
            currentKey.clear();
            if (!applyValue(toLower(arg.substr(0, sep)), arg.substr(sep + 1), cfg))
            {
                return false;
            }
            continue;
        }

        if (currentKey.empty())
        {
            // A lone value is taken as the firmware path.
            cfg.hexPath = arg;
            continue;
        }

        if (!applyValue(currentKey, arg, cfg))
        {
            return false;
        }
        if (!isMultiValued(currentKey))
        {
            currentKey.clear();    // Single-valued key.
        }
    }

    if (cfg.hexPath.empty())
    {
        std::fprintf(stderr,
                     "%s missing firmware: add \"hex\" \"file.hex\" to sim_args"
                     " (needs ngspice >= 44: older versions do not pass sim_args)\n", kTag);
        return false;
    }
    if (cfg.frequency == 0u)
    {
        std::fprintf(stderr, "%s invalid clock frequency\n", kTag);
        return false;
    }
    if (cfg.vref <= 0.0 || cfg.adcBit <= 0.0)
    {
        std::fprintf(stderr, "%s vref and adcbit must be positive\n", kTag);
        return false;
    }
    return true;
}

unsigned int inputPortCount(const Config &cfg)
{
    return static_cast<unsigned int>(cfg.inputs.size() + cfg.ios.size() +
                                     cfg.adcChannels.size());
}

unsigned int outputPortCount(const Config &cfg)
{
    return static_cast<unsigned int>(cfg.outputs.size() + 3u * cfg.ios.size() +
                                     (cfg.adcChannels.empty() ? 0 : kDacBits));
}

} // namespace kisimavr
