/*
 * kisimavr - ATmega co-simulation for ngspice / KiCad
 * Copyright (C) 2026 Paul
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file lcd_device.cpp
 * @brief Character LCD (HD44780) as a circuit part.
 */

#include "lcd_device.h"

#include <cctype>
#include <cstdlib>
#include <utility>

#include "common.h"
#include "paths.h"

namespace kisimavr
{

namespace
{

constexpr unsigned int kRs = 0;
constexpr unsigned int kRw = 1;
constexpr unsigned int kE  = 2;
constexpr unsigned int kD0 = 3;

constexpr double kQuiet    = 1e-3;     // Screen printed after 1 ms without writes.
constexpr double kMaxWait  = 20e-3;    // ...or at least every 20 ms while it changes.
constexpr double kPweh     = 230e-9;   // Minimum E pulse width (HD44780U, 5 V).
constexpr double kTdsw     = 80e-9;    // Data set-up time before E falls.

enum WarnKind
{
    kWarnPulse = 0,
    kWarnSetup,
};

bool parseInt(const std::string &text, int low, int high, int &out)
{
    char *end = nullptr;
    const long v = std::strtol(text.c_str(), &end, 10);
    if (text.empty() || *end != '\0' || v < low || v > high)
    {
        return false;
    }
    out = static_cast<int>(v);
    return true;
}

} // namespace

bool isDeviceConfig(const std::vector<std::string> &args)
{
    for (size_t i = 0; i + 1 < args.size(); ++i)
    {
        if (toLower(args[i]) == "device")
        {
            return true;
        }
    }
    return false;
}

bool parseLcdConfig(const std::vector<std::string> &args, LcdConfig &cfg)
{
    bool ok = true;
    bool device = false;
    for (size_t i = 0; i < args.size(); i += 2)
    {
        const std::string key = toLower(args[i]);
        if (i + 1 >= args.size())
        {
            std::fprintf(stderr, "%s LCD: key '%s' has no value\n", kTag, key.c_str());
            return false;
        }
        const std::string &value = args[i + 1];
        if (key == "device")
        {
            device = toLower(value) == "hd44780";
            if (!device)
            {
                std::fprintf(stderr, "%s unknown device '%s' (supported: hd44780)\n", kTag,
                             value.c_str());
                ok = false;
            }
        }
        else if (key == "cols" || key == "rows")
        {
            int v = 0;
            const bool cols = key == "cols";
            if (!parseInt(value, cols ? 8 : 1, cols ? 40 : 4, v))
            {
                std::fprintf(stderr, "%s LCD: invalid %s '%s'\n", kTag, key.c_str(),
                             value.c_str());
                ok = false;
            }
            (cols ? cfg.cols : cfg.rows) = v;
        }
        else if (key == "lcdlog")
        {
            cfg.logPath = value;
        }
        else if (key == "name")
        {
            // ngspice lower-cases the netlist: labels are shown in capitals.
            cfg.name = value;
            for (char &c : cfg.name)
            {
                c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            }
        }
        else
        {
            std::fprintf(stderr, "%s LCD: unknown key '%s'\n", kTag, key.c_str());
            ok = false;
        }
    }
    return ok && device;
}

LcdDevice::LcdDevice(LcdConfig cfg)
    : m_cfg(std::move(cfg)), m_lcd(m_cfg.cols, m_cfg.rows)
{
    m_level.fill(false);
    if (!m_cfg.logPath.empty())
    {
        const std::string path = resolveOutputPath(m_cfg.logPath);
        m_file = openOutputFile(path, "LCD log", { ".txt", ".log" }, looksLikeLcdLog);
        if (m_file != nullptr)
        {
            std::fprintf(m_file, "# kisimavr LCD %dx%d\n", m_cfg.cols, m_cfg.rows);
        }
    }
    std::fprintf(stderr, "%s %s: HD44780 %dx%d (RS RW E D0..D7)\n", kTag, m_cfg.name.c_str(),
                 m_cfg.cols, m_cfg.rows);
}

LcdDevice::~LcdDevice()
{
    flush(m_lastWrite + kQuiet, true);
    if (m_file != nullptr)
    {
        std::fclose(m_file);
    }
}

void LcdDevice::step(co_info *info)
{
    flush(info->vtime, false);    // No output: vtime is reached at once.
}

void LcdDevice::input(co_info *info, unsigned int index, const Digital_t *value)
{
    if (index >= kInputs)
    {
        return;
    }
    const double t = info->vtime;
    const bool level = value->state == ONE;
    const bool old = m_level[index];
    m_level[index] = level;
    if (level == old)
    {
        return;
    }
    if (index == kE)
    {
        if (level)
        {
            m_eRise = t;
        }
        else
        {
            fallingEdge(t);
        }
        return;
    }
    if (index == kRs || index >= kD0)
    {
        m_dataChange = t;
    }
}

void LcdDevice::warnOnce(int kind, const std::string &text)
{
    const unsigned int bit = 1u << kind;
    if ((m_warned & bit) == 0u)
    {
        m_warned |= bit;
        report({ text });
    }
}

void LcdDevice::fallingEdge(double t)
{
    char text[160];
    if (m_eRise >= 0.0 && t - m_eRise < kPweh)
    {
        std::snprintf(text, sizeof text, "E pulse of %.0f ns, the HD44780 needs 230 ns",
                      (t - m_eRise) * 1e9);
        warnOnce(kWarnPulse, text);
    }
    if (t - m_dataChange < kTdsw)
    {
        std::snprintf(text, sizeof text,
                      "data changed %.0f ns before E falls, the HD44780 needs 80 ns",
                      (t - m_dataChange) * 1e9);
        warnOnce(kWarnSetup, text);
    }

    uint8_t bus = 0;
    for (unsigned int k = 0; k < 8; ++k)
    {
        bus = static_cast<uint8_t>(bus | (m_level[kD0 + k] ? (1u << k) : 0u));
    }
    m_lcd.strobe(m_level[kRs], m_level[kRw], bus, t);
    report(m_lcd.warnings());
    m_lcd.warnings().clear();
    m_lastWrite = t;
}

void LcdDevice::report(const std::vector<std::string> &warnings)
{
    for (const std::string &w : warnings)
    {
        std::fprintf(stderr, "%s t=%.6f s %s: warning: %s\n", kTag, m_lastWrite,
                     m_cfg.name.c_str(), w.c_str());
    }
}

void LcdDevice::flush(double t, bool force)
{
    if (m_lcd.version() == m_shownVersion)
    {
        return;
    }
    const bool quiet = t - m_lastWrite >= kQuiet;
    if (!quiet && !force && t - m_lastWrite < kMaxWait)
    {
        return;
    }
    m_shownVersion = m_lcd.version();

    const std::vector<std::string> rows = m_lcd.lines();
    std::string line;
    if (m_lcd.displayOn())
    {
        for (const std::string &r : rows)
        {
            line += (line.empty() ? "[" : " [") + r + "]";
        }
    }
    else
    {
        line = "(display off)";
    }
    if (line == m_shown)
    {
        return;
    }
    m_shown = line;
    std::fprintf(stderr, "%s t=%.6f s %s> %s\n", kTag, m_lastWrite, m_cfg.name.c_str(),
                 line.c_str());

    if (m_file != nullptr)
    {
        const std::string border = "+" + std::string(static_cast<size_t>(m_cfg.cols), '-') + "+";
        std::fprintf(m_file, "t = %.6f s\n%s\n", m_lastWrite, border.c_str());
        for (const std::string &r : rows)
        {
            std::fprintf(m_file, "|%s|\n", m_lcd.displayOn() ? r.c_str()
                                                             : std::string(static_cast<size_t>(m_cfg.cols), ' ').c_str());
        }
        std::fprintf(m_file, "%s\n", border.c_str());
        std::fflush(m_file);
    }
}

} // namespace kisimavr
