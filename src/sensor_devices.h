/*
 * kisimavr - ATmega co-simulation for ngspice / KiCad
 * Copyright (C) 2026 Paul
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file sensor_devices.h
 * @brief Timing-based modules as circuit parts: HC-SR04 ultrasonic sensor
 *        ("device" "hcsr04") and RC servomotor ("device" "servo").
 *
 * HC-SR04: 1 input (TRIG), 1 output (ECHO). A TRIG pulse of 10 us or more
 * starts a measurement when it falls: 250 us later (8 bursts at 40 kHz),
 * ECHO goes high for the time of flight, 58.3 us per cm (sound at 343 m/s),
 * or 38 ms when nothing is in range (2 to 400 cm).
 *   keys: dist (cm, default 20), rate (cm/s, default 0: dist + rate * t),
 *         name (label of the messages, default SR04), log (1: each measure)
 *
 * Servo: 1 input (SIG), 8 outputs (angle in degrees, binary). The width of
 * each SIG pulse sets the target angle: min us -> 0 deg, max us -> 180 deg
 * (544 / 2400 us by default, as the Arduino Servo library); the horn turns
 * at "speed" deg/s (600: 0.1 s per 60 deg, SG90) and the outputs follow it
 * degree by degree. Pulses outside 300..3000 us are ignored.
 *   keys: min, max (us), speed (deg/s), start (deg, default 90), name
 *         (default SERVO)
 * Each new position reached is printed: SERVO> 90 deg (pulse 1472 us).
 */

#ifndef KISIMAVR_SENSOR_DEVICES_H
#define KISIMAVR_SENSOR_DEVICES_H

#include <string>
#include <vector>

#include "cosim_device.h"

namespace kisimavr
{

struct HcSr04Config
{
    double dist = 20.0;     // cm
    double rate = 0.0;      // cm/s
    std::string name = "SR04";
    bool log = false;
};

struct ServoConfig
{
    double minUs = 544.0;
    double maxUs = 2400.0;
    double speed = 600.0;    // deg/s
    double start = 90.0;     // deg
    std::string name = "SERVO";
};

/** "device" value of sim_args ("" if none), lower case. */
std::string deviceKind(const std::vector<std::string> &args);

bool parseHcSr04Config(const std::vector<std::string> &args, HcSr04Config &cfg);
bool parseServoConfig(const std::vector<std::string> &args, ServoConfig &cfg);

/** HC-SR04 timing, without ngspice (unit-tested). */
class HcSr04
{
public:
    static constexpr double kMinTrig = 10e-6;     // TRIG pulse
    static constexpr double kBurst   = 250e-6;    // TRIG falls -> ECHO rises
    static constexpr double kPerCm   = 2.0 / 34300.0;
    static constexpr double kTimeout = 38e-3;     // Nothing in range

    explicit HcSr04(const HcSr04Config &cfg) : m_cfg(cfg) {}

    /** TRIG level at time t; returns false for a too short pulse (ignored). */
    bool trig(bool level, double t);

    /** Distance at time t (cm), as set by dist / rate. */
    double distance(double t) const;

    /** ECHO level at time t. */
    bool echo(double t) const;

    /** Next time after t when ECHO changes, or -1. */
    double nextChange(double t) const;

    /** Measurement started by the last TRIG: rise time, width (s); width 0: none. */
    double echoStart() const { return m_rise; }
    double echoWidth() const { return m_width; }

private:
    HcSr04Config m_cfg;
    double m_trigRise = -1.0;
    bool   m_trig = false;
    double m_rise = -1.0;
    double m_width = 0.0;
};

/** Servo position, without ngspice (unit-tested). */
class Servo
{
public:
    explicit Servo(const ServoConfig &cfg);

    /** SIG level at time t; a falling edge sets the target. */
    void sig(bool level, double t);

    /** Angle (deg) at time t, moving toward the target. */
    double angle(double t) const;

    /** Whole degree shown at time t, and the next time it changes (-1: none). */
    int degree(double t) const;
    double nextChange(double t) const;

    double target() const { return m_target; }
    double lastPulseUs() const { return m_pulseUs; }

private:
    ServoConfig m_cfg;
    double m_from;            // Angle at m_moveStart
    double m_moveStart = 0.0;
    double m_target;
    double m_rise = -1.0;
    double m_pulseUs = 0.0;
};

class HcSr04Device : public CosimDevice
{
public:
    explicit HcSr04Device(const HcSr04Config &cfg);

    void step(co_info *info) override;
    void input(co_info *info, unsigned int index, const Digital_t *value) override;

private:
    HcSr04Config m_cfg;
    HcSr04 m_sensor;
    bool   m_out = false;
    double m_last = 0.0;          // Time reached
    bool   m_warned = false;
};

class ServoDevice : public CosimDevice
{
public:
    static constexpr unsigned int kBits = 8;

    explicit ServoDevice(const ServoConfig &cfg);

    void step(co_info *info) override;
    void input(co_info *info, unsigned int index, const Digital_t *value) override;

private:
    void publish(co_info *info, int degree);

    ServoConfig m_cfg;
    Servo  m_servo;
    int    m_shown = -1;          // Degree on the outputs
    double m_last = 0.0;          // Time reached
    double m_printed = -1.0;      // Target already printed
    bool   m_first = true;
};

} // namespace kisimavr

#endif // KISIMAVR_SENSOR_DEVICES_H
