/*
 * kisimavr - ATmega co-simulation for ngspice / KiCad
 * Copyright (C) 2026 Paul
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file sensor_devices.cpp
 * @brief HC-SR04 ultrasonic sensor and RC servomotor as circuit parts.
 */

#include "sensor_devices.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "common.h"

namespace kisimavr
{

namespace
{

bool parseNumber(const std::string &text, double &out)
{
    char *end = nullptr;
    out = std::strtod(text.c_str(), &end);
    return !text.empty() && *end == '\0';
}

std::string upper(std::string s)
{
    for (char &c : s)
    {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return s;
}

/** key / value pairs: calls set(key, value) for each; false on error. */
template <typename Setter>
bool parsePairs(const std::vector<std::string> &args, const char *what, Setter set)
{
    bool ok = true;
    for (size_t i = 0; i < args.size(); i += 2)
    {
        const std::string key = toLower(args[i]);
        if (i + 1 >= args.size())
        {
            std::fprintf(stderr, "%s %s: key '%s' has no value\n", kTag, what, key.c_str());
            return false;
        }
        if (key == "device")
        {
            continue;
        }
        const std::string &value = args[i + 1];
        const int r = set(key, value);
        if (r < 0)
        {
            std::fprintf(stderr, "%s %s: unknown key '%s'\n", kTag, what, key.c_str());
            ok = false;
        }
        else if (r == 0)
        {
            std::fprintf(stderr, "%s %s: invalid %s '%s'\n", kTag, what, key.c_str(),
                         value.c_str());
            ok = false;
        }
    }
    return ok;
}

} // namespace

std::string deviceKind(const std::vector<std::string> &args)
{
    for (size_t i = 0; i + 1 < args.size(); ++i)
    {
        if (toLower(args[i]) == "device")
        {
            return toLower(args[i + 1]);
        }
    }
    return "";
}

bool parseHcSr04Config(const std::vector<std::string> &args, HcSr04Config &cfg)
{
    return parsePairs(args, "HC-SR04", [&cfg](const std::string &key, const std::string &v) {
        if (key == "dist")
        {
            return parseNumber(v, cfg.dist) && cfg.dist >= 0.0 ? 1 : 0;
        }
        if (key == "rate")
        {
            return parseNumber(v, cfg.rate) ? 1 : 0;
        }
        if (key == "name")
        {
            cfg.name = upper(v);
            return 1;
        }
        if (key == "log")
        {
            cfg.log = v != "0";
            return 1;
        }
        return -1;
    });
}

bool parseServoConfig(const std::vector<std::string> &args, ServoConfig &cfg)
{
    const bool ok = parsePairs(args, "Servo", [&cfg](const std::string &key,
                                                     const std::string &v) {
        if (key == "min" || key == "max")
        {
            double us = 0.0;
            if (!parseNumber(v, us) || us < 300.0 || us > 3000.0)
            {
                return 0;
            }
            (key == "min" ? cfg.minUs : cfg.maxUs) = us;
            return 1;
        }
        if (key == "speed")
        {
            return parseNumber(v, cfg.speed) && cfg.speed > 0.0 ? 1 : 0;
        }
        if (key == "start")
        {
            return parseNumber(v, cfg.start) && cfg.start >= 0.0 && cfg.start <= 180.0 ? 1 : 0;
        }
        if (key == "name")
        {
            cfg.name = upper(v);
            return 1;
        }
        return -1;
    });
    if (cfg.maxUs <= cfg.minUs)
    {
        std::fprintf(stderr, "%s Servo: max (%g us) must be above min (%g us)\n", kTag,
                     cfg.maxUs, cfg.minUs);
        return false;
    }
    return ok;
}

// ---- HC-SR04 -----------------------------------------------------------------

double HcSr04::distance(double t) const
{
    return std::max(0.0, m_cfg.dist + m_cfg.rate * t);
}

bool HcSr04::trig(bool level, double t)
{
    const bool old = m_trig;
    m_trig = level;
    if (level == old)
    {
        return true;
    }
    if (level)
    {
        m_trigRise = t;
        return true;
    }
    if (m_trigRise < 0.0 || t - m_trigRise < kMinTrig)
    {
        return false;
    }
    const bool busy = m_rise >= 0.0 && t < m_rise + m_width;
    if (!busy)
    {
        const double d = distance(t);
        m_rise = t + kBurst;
        m_width = (d < 2.0 || d > 400.0) ? kTimeout : d * kPerCm;
    }
    return true;
}

bool HcSr04::echo(double t) const
{
    return m_rise >= 0.0 && t >= m_rise && t < m_rise + m_width;
}

double HcSr04::nextChange(double t) const
{
    if (m_rise < 0.0)
    {
        return -1.0;
    }
    if (t < m_rise)
    {
        return m_rise;
    }
    if (t < m_rise + m_width)
    {
        return m_rise + m_width;
    }
    return -1.0;
}

HcSr04Device::HcSr04Device(const HcSr04Config &cfg) : m_cfg(cfg), m_sensor(cfg)
{
    std::fprintf(stderr, "%s %s: HC-SR04, obstacle at %.1f cm%s\n", kTag, m_cfg.name.c_str(),
                 cfg.dist, cfg.rate != 0.0 ? " (moving)" : "");
}

void HcSr04Device::step(co_info *info)
{
    const double target = info->vtime;
    const double rise = m_sensor.echoStart();
    const double fall = rise + m_sensor.echoWidth();
    double edge = -1.0;
    if (!m_out && rise > m_last && rise <= target)
    {
        edge = rise;
    }
    else if (m_out && fall <= target)
    {
        edge = fall;
    }
    if (edge >= 0.0)
    {
        info->vtime = std::max(edge, m_last);
        m_out = !m_out;
        Digital_t v{ m_out ? ONE : ZERO, STRONG };
        info->out_fn(info, 0, &v);
    }
    m_last = info->vtime;
}

void HcSr04Device::input(co_info *info, unsigned int index, const Digital_t *value)
{
    if (index != 0)
    {
        return;
    }
    const double t = info->vtime;
    m_last = std::max(m_last, t);
    const double before = m_sensor.echoStart();
    if (!m_sensor.trig(value->state == ONE, t) && !m_warned)
    {
        m_warned = true;
        std::fprintf(stderr, "%s t=%.6f s %s: TRIG pulse shorter than 10 us, ignored\n",
                     kTag, t, m_cfg.name.c_str());
    }
    if (m_cfg.log && m_sensor.echoStart() != before)
    {
        std::fprintf(stderr, "%s t=%.6f s %s> %.1f cm (echo %.0f us)\n", kTag, t,
                     m_cfg.name.c_str(), m_sensor.distance(t), m_sensor.echoWidth() * 1e6);
    }
}

// ---- Servo -------------------------------------------------------------------

Servo::Servo(const ServoConfig &cfg) : m_cfg(cfg), m_from(cfg.start), m_target(cfg.start)
{
}

void Servo::sig(bool level, double t)
{
    if (level)
    {
        m_rise = t;
        return;
    }
    if (m_rise < 0.0)
    {
        return;
    }
    const double us = (t - m_rise) * 1e6;
    m_rise = -1.0;
    if (us < 300.0 || us > 3000.0)
    {
        return;
    }
    m_pulseUs = us;
    const double target = std::clamp((us - m_cfg.minUs) / (m_cfg.maxUs - m_cfg.minUs) * 180.0,
                                     0.0, 180.0);
    if (std::fabs(target - m_target) > 1e-6)
    {
        m_from = angle(t);
        m_moveStart = t;
        m_target = target;
    }
}

double Servo::angle(double t) const
{
    const double delta = m_target - m_from;
    const double moved = m_cfg.speed * std::max(0.0, t - m_moveStart);
    if (moved >= std::fabs(delta))
    {
        return m_target;
    }
    return m_from + (delta > 0.0 ? moved : -moved);
}

int Servo::degree(double t) const
{
    return static_cast<int>(std::lround(angle(t)));
}

double Servo::nextChange(double t) const
{
    const double a = angle(t);
    if (a == m_target)
    {
        return -1.0;
    }
    const int d = static_cast<int>(std::lround(a));
    const double dir = m_target > a ? 1.0 : -1.0;
    const double boundary = d + 0.5 * dir;
    const double end = m_moveStart + std::fabs(m_target - m_from) / m_cfg.speed;
    if ((dir > 0.0 && boundary > m_target) || (dir < 0.0 && boundary < m_target))
    {
        return std::lround(m_target) != d ? end : -1.0;
    }
    return std::min(end, t + std::fabs(boundary - a) / m_cfg.speed + 1e-9);
}

ServoDevice::ServoDevice(const ServoConfig &cfg) : m_cfg(cfg), m_servo(cfg)
{
    std::fprintf(stderr, "%s %s: servo %.0f..%.0f us -> 0..180 deg, %.0f deg/s, start %.0f deg\n",
                 kTag, m_cfg.name.c_str(), cfg.minUs, cfg.maxUs, cfg.speed, cfg.start);
}

void ServoDevice::publish(co_info *info, int degree)
{
    for (unsigned int b = 0; b < kBits; ++b)
    {
        const bool now = ((degree >> b) & 1) != 0;
        const bool was = m_shown >= 0 && ((m_shown >> b) & 1) != 0;
        if (m_shown < 0 || now != was)
        {
            Digital_t v{ now ? ONE : ZERO, STRONG };
            info->out_fn(info, b, &v);
        }
    }
    m_shown = degree;
}

void ServoDevice::step(co_info *info)
{
    const double target = info->vtime;
    if (m_first)
    {
        m_first = false;
        publish(info, m_servo.degree(target));
        m_last = target;
        return;
    }
    const double change = m_servo.nextChange(m_last);
    if (change >= 0.0 && change <= target)
    {
        info->vtime = std::max(change, m_last);
        const int degree = m_servo.degree(info->vtime);
        if (degree != m_shown)
        {
            publish(info, degree);
        }
    }
    m_last = info->vtime;
    if (m_servo.angle(m_last) == m_servo.target() && m_servo.target() != m_printed &&
        m_servo.lastPulseUs() > 0.0)
    {
        m_printed = m_servo.target();
        std::fprintf(stderr, "%s t=%.6f s %s> %d deg (pulse %.0f us)\n", kTag, m_last,
                     m_cfg.name.c_str(), m_servo.degree(m_last), m_servo.lastPulseUs());
    }
}

void ServoDevice::input(co_info *info, unsigned int index, const Digital_t *value)
{
    if (index == 0)
    {
        m_servo.sig(value->state == ONE, info->vtime);
        m_last = std::max(m_last, static_cast<double>(info->vtime));
    }
}

} // namespace kisimavr
