/*
 * kisimavr - ATmega co-simulation for ngspice / KiCad
 * Copyright (C) 2026 Paul
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file i2c_slave_device.cpp
 * @brief I2C slaves (LM75, 24C02, PCF8574 LCD) as a circuit part.
 */

#include "i2c_slave_device.h"

#include <cstdio>
#include <cstdlib>
#include <utility>

#include "common.h"

namespace kisimavr
{

namespace
{

constexpr double kHold = 300e-9;    // SDA changed 300 ns after SCL falls (tHD;DAT).

/** LM75 / 24C02 of i2c_devices.h. */
class DeviceTarget : public I2cTarget
{
public:
    explicit DeviceTarget(I2cDevice dev) : m_dev(std::move(dev)) {}

    int address() const override { return m_dev.address(); }
    std::string label() const override
    {
        return m_dev.type() == I2cDevice::Type::Lm75 ? "LM75" : "24C02";
    }
    void start(bool /*read*/) override { m_dev.start(); }
    bool write(uint8_t value, double /*t*/) override
    {
        m_dev.write(value);
        return true;
    }
    uint8_t read(double t) override { return m_dev.read(t); }
    void stop(double /*t*/, bool /*final*/) override {}

private:
    I2cDevice m_dev;
};

/** Character LCD behind a PCF8574 (LiquidCrystal_I2C wiring). */
class LcdTarget : public I2cTarget
{
public:
    LcdTarget(int address, LcdConfig cfg) : m_address(address)
    {
        char wiring[96];
        std::snprintf(wiring, sizeof wiring, "PCF8574 I2C 0x%02X: P0=RS P1=RW P2=E P4..P7=D4..D7",
                      address);
        cfg.wiring = wiring;
        m_lcd = std::make_unique<LcdDevice>(std::move(cfg));
    }

    int address() const override { return m_address; }
    std::string label() const override { return "PCF8574"; }
    void start(bool /*read*/) override {}
    bool write(uint8_t value, double t) override
    {
        const bool eWas = (m_port & 0x04u) != 0u;
        const bool eNow = (value & 0x04u) != 0u;
        if (eWas && !eNow)
        {
            // E falls: the HD44780 latches D7..D4 (bus bits 7..4).
            m_lcd->write((m_port & 0x01u) != 0u, (m_port & 0x02u) != 0u,
                         static_cast<uint8_t>(m_port & 0xF0u), t);
        }
        m_port = value;
        m_lastWrite = t;
        return true;
    }
    uint8_t read(double /*t*/) override { return m_port; }    // Quasi-bidirectional port.
    void stop(double t, bool final) override { m_lcd->flush(t, final); }

private:
    int m_address;
    uint8_t m_port = 0xFF;
    double m_lastWrite = 0.0;
    std::unique_ptr<LcdDevice> m_lcd;
};

bool parseAddress(const std::string &text, int &out)
{
    char *end = nullptr;
    const long v = std::strtol(text.c_str(), &end, 0);
    if (text.empty() || *end != '\0' || v < 0x08 || v > 0x77)
    {
        return false;
    }
    out = static_cast<int>(v);
    return true;
}

bool parseNumber(const std::string &text, double &out)
{
    char *end = nullptr;
    out = std::strtod(text.c_str(), &end);
    return !text.empty() && *end == '\0';
}

} // namespace

// ---- Bit-level slave ---------------------------------------------------------

I2cSlaveBus::I2cSlaveBus(std::vector<std::unique_ptr<I2cTarget>> targets)
    : m_targets(std::move(targets))
{
}

void I2cSlaveBus::sda(bool level, double t)
{
    const bool old = m_sda;
    m_sda = level;
    if (!m_scl || level == old)
    {
        return;
    }
    if (!level)
    {
        // START (or repeated START): SDA falls while SCL is high.
        if (m_state != State::Idle)
        {
            endTransaction(t);
        }
        m_state = State::Address;
        m_shift = 0;
        m_bits = 0;
        m_pull = false;
    }
    else
    {
        // STOP: SDA rises while SCL is high.
        endTransaction(t);
        m_state = State::Idle;
        m_pull = false;
    }
}

void I2cSlaveBus::scl(bool level, double t)
{
    const bool old = m_scl;
    m_scl = level;
    if (level == old)
    {
        return;
    }
    if (level)
    {
        risingScl();
    }
    else
    {
        fallingScl(t);
    }
}

void I2cSlaveBus::risingScl()
{
    switch (m_state)
    {
    case State::Address:
    case State::Write:
        m_shift = static_cast<uint8_t>((m_shift << 1) | (m_sda ? 1u : 0u));
        ++m_bits;
        break;
    case State::ReadAck:
        m_masterAck = !m_sda;
        break;
    default:
        break;
    }
}

void I2cSlaveBus::sendBit()
{
    m_pull = ((m_shift >> (7 - m_bits)) & 1u) == 0u;
    ++m_bits;
}

void I2cSlaveBus::fallingScl(double t)
{
    char hex[8];
    switch (m_state)
    {
    case State::Address:
        if (m_bits < 8)
        {
            return;
        }
        m_target = nullptr;
        for (auto &target : m_targets)
        {
            if (target->address() == (m_shift >> 1))
            {
                m_target = target.get();
            }
        }
        m_read = (m_shift & 1u) != 0u;
        if (m_target == nullptr)
        {
            m_state = State::Ignore;    // No ACK: the master sees a NACK.
            m_pull = false;
            return;
        }
        m_target->start(m_read);
        std::snprintf(hex, sizeof hex, "0x%02X", m_shift >> 1);
        m_line = std::string(hex) + (m_read ? " R" : " W");
        m_state = State::AckAddr;
        m_pull = true;
        break;
    case State::AckAddr:
    case State::ReadAck:
        if (m_state == State::ReadAck && !m_masterAck)
        {
            m_state = State::Ignore;    // Master NACK: last byte read.
            m_pull = false;
            return;
        }
        if (m_read)
        {
            m_shift = m_target->read(t);
            std::snprintf(hex, sizeof hex, " %02X", m_shift);
            m_line += hex;
            m_bits = 0;
            m_state = State::Read;
            sendBit();
        }
        else
        {
            m_state = State::Write;
            m_shift = 0;
            m_bits = 0;
            m_pull = false;
        }
        break;
    case State::Write:
        if (m_bits < 8)
        {
            return;
        }
        std::snprintf(hex, sizeof hex, " %02X", m_shift);
        m_line += hex;
        m_pull = m_target->write(m_shift, t);
        m_state = State::AckWrite;
        break;
    case State::AckWrite:
        m_state = State::Write;
        m_shift = 0;
        m_bits = 0;
        m_pull = false;
        break;
    case State::Read:
        if (m_bits < 8)
        {
            sendBit();
        }
        else
        {
            m_pull = false;    // Master ACK / NACK.
            m_state = State::ReadAck;
        }
        break;
    default:
        break;
    }
}

void I2cSlaveBus::endTransaction(double t)
{
    if (m_target != nullptr)
    {
        m_target->stop(t, false);
        if (!m_line.empty())
        {
            m_log.push_back(m_line);
        }
    }
    m_target = nullptr;
    m_line.clear();
}

void I2cSlaveBus::finish(double t)
{
    endTransaction(t);
    for (auto &target : m_targets)
    {
        target->stop(t, true);
    }
}

std::vector<std::string> I2cSlaveBus::takeLog()
{
    std::vector<std::string> out;
    out.swap(m_log);
    return out;
}

// ---- Configuration -----------------------------------------------------------

bool isI2cSlaveConfig(const std::vector<std::string> &args)
{
    for (size_t i = 0; i + 1 < args.size(); ++i)
    {
        if (toLower(args[i]) == "device" && toLower(args[i + 1]) == "i2c")
        {
            return true;
        }
    }
    return false;
}

bool parseI2cSlaveConfig(const std::vector<std::string> &args, I2cBusConfig &cfg)
{
    bool ok = true;
    std::vector<std::string> lcdArgs = { "device", "hd44780" };
    for (size_t i = 0; i < args.size(); i += 2)
    {
        const std::string key = toLower(args[i]);
        if (i + 1 >= args.size())
        {
            std::fprintf(stderr, "%s I2C: key '%s' has no value\n", kTag, key.c_str());
            return false;
        }
        const std::string &value = args[i + 1];
        if (key == "device")
        {
            continue;
        }
        if (key == "lm75" || key == "eeprom" || key == "lcd")
        {
            I2cBusConfig::Slave s;
            s.kind = key;
            if (!parseAddress(value, s.address))
            {
                std::fprintf(stderr, "%s I2C: invalid address '%s' for %s\n", kTag,
                             value.c_str(), key.c_str());
                ok = false;
                continue;
            }
            for (const auto &other : cfg.slaves)
            {
                if (other.address == s.address)
                {
                    std::fprintf(stderr, "%s I2C: two slaves at address 0x%02X\n", kTag,
                                 s.address);
                    ok = false;
                }
            }
            cfg.slaves.push_back(s);
        }
        else if (key == "temp" || key == "rate")
        {
            double v = 0.0;
            if (!parseNumber(value, v))
            {
                std::fprintf(stderr, "%s I2C: invalid %s '%s'\n", kTag, key.c_str(),
                             value.c_str());
                ok = false;
            }
            (key == "temp" ? cfg.temp0 : cfg.rate) = v;
        }
        else if (key == "cols" || key == "rows" || key == "lcdlog" || key == "name")
        {
            lcdArgs.push_back(key);
            lcdArgs.push_back(value);
        }
        else if (key == "log")
        {
            cfg.log = value != "0";
        }
        else
        {
            std::fprintf(stderr, "%s I2C: unknown key '%s'\n", kTag, key.c_str());
            ok = false;
        }
    }
    if (!parseLcdConfig(lcdArgs, cfg.lcd))
    {
        ok = false;
    }
    if (cfg.slaves.empty())
    {
        std::fprintf(stderr, "%s I2C: no slave: add \"lm75\" \"0x48\", \"eeprom\" \"0x50\" "
                     "or \"lcd\" \"0x27\"\n", kTag);
        ok = false;
    }
    return ok;
}

std::vector<std::unique_ptr<I2cTarget>> makeI2cTargets(const I2cBusConfig &cfg)
{
    std::vector<std::unique_ptr<I2cTarget>> out;
    for (const auto &s : cfg.slaves)
    {
        if (s.kind == "lcd")
        {
            out.push_back(std::make_unique<LcdTarget>(s.address, cfg.lcd));
        }
        else
        {
            const auto type = s.kind == "lm75" ? I2cDevice::Type::Lm75
                                               : I2cDevice::Type::Eeprom24c02;
            out.push_back(std::make_unique<DeviceTarget>(
                I2cDevice(type, s.address, cfg.temp0, cfg.rate)));
        }
    }
    return out;
}

// ---- d_cosim device ----------------------------------------------------------

I2cSlaveDevice::I2cSlaveDevice(const I2cBusConfig &cfg)
    : m_bus(makeI2cTargets(cfg)), m_log(cfg.log)
{
    std::string list;
    for (const auto &target : m_bus.targets())
    {
        char item[32];
        std::snprintf(item, sizeof item, "%s%s 0x%02X", list.empty() ? "" : ", ",
                      target->label().c_str(), target->address());
        list += item;
    }
    std::fprintf(stderr, "%s I2C slaves on SDA / SCL: %s\n", kTag, list.c_str());
}

I2cSlaveDevice::~I2cSlaveDevice()
{
    m_bus.finish(m_last);
    printLog();
}

void I2cSlaveDevice::printLog()
{
    for (const std::string &line : m_bus.takeLog())
    {
        if (m_log)
        {
            std::fprintf(stderr, "%s t=%.6f s I2C> %s\n", kTag, m_last, line.c_str());
        }
    }
}

void I2cSlaveDevice::step(co_info *info)
{
    const double target = info->vtime;
    if (m_pending && m_pendingTime <= target)
    {
        m_pending = false;
        m_out = m_bus.pullLow();
        Digital_t v{ m_out ? ONE : ZERO, STRONG };
        info->vtime = m_pendingTime;
        info->out_fn(info, 0, &v);
    }
    m_last = info->vtime;
    printLog();
    for (auto &t : m_bus.targets())
    {
        t->stop(info->vtime, false);    // Lets the LCD print a stable screen.
    }
}

void I2cSlaveDevice::input(co_info *info, unsigned int index, const Digital_t *value)
{
    const double t = info->vtime;
    const bool level = value->state != ZERO;    // UNKNOWN: released line.
    if (index == 0)
    {
        m_bus.sda(level, t);
    }
    else if (index == 1)
    {
        m_bus.scl(level, t);
    }
    m_last = t;
    if (m_bus.pullLow() != m_out)
    {
        if (!m_pending)
        {
            m_pending = true;
            m_pendingTime = t + kHold;
        }
    }
    else
    {
        m_pending = false;
    }
}

} // namespace kisimavr
