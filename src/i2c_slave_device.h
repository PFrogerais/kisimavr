/*
 * kisimavr - ATmega co-simulation for ngspice / KiCad
 * Copyright (C) 2026 Paul
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file i2c_slave_device.h
 * @brief I2C slaves as a circuit part: sim_args "device" "i2c".
 *
 * A d_cosim instance wired on the SDA / SCL lines of the circuit, like the
 * real modules: it decodes the bus bit by bit and pulls SDA low (open
 * drain) for its ACK bits and the bits it sends. Any master works: an
 * RP2040 / RP2350 (Pico), an ATmega, or sources of the schematic.
 * 2 inputs (SDA, SCL), 1 output (1 = pull SDA low).
 *
 * Keys (each followed by one value):
 *   device   i2c (required)
 *   lm75     7-bit address of an LM75 temperature sensor (0x48..0x4F)
 *   eeprom   7-bit address of a 24C02 EEPROM (0x50..0x57)
 *   lcd      7-bit address of a character LCD with a PCF8574 backpack
 *            (0x20..0x27, 0x38..0x3F): P0 = RS, P1 = RW, P2 = E,
 *            P3 = backlight, P4..P7 = D4..D7, as LiquidCrystal_I2C
 *   temp     LM75 temperature at t = 0 (degC, default 25)
 *   rate     LM75 temperature slope (degC per second, default 0)
 *   cols / rows / lcdlog / name   as for "device" "hd44780"
 *   log      1: print every transaction (I2C> 0x48 W 00 / R 19 80)
 * At least one of lm75, eeprom, lcd; each may be repeated.
 */

#ifndef KISIMAVR_I2C_SLAVE_DEVICE_H
#define KISIMAVR_I2C_SLAVE_DEVICE_H

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "cosim_device.h"
#include "i2c_devices.h"
#include "lcd_device.h"

namespace kisimavr
{

/** One slave on the bus (protocol level). */
class I2cTarget
{
public:
    virtual ~I2cTarget() = default;
    virtual int address() const = 0;
    virtual std::string label() const = 0;
    /** Addressed by a START (read = R/W bit). */
    virtual void start(bool read) = 0;
    /** Byte written by the master: true = ACK. */
    virtual bool write(uint8_t value, double t) = 0;
    /** Byte read by the master. */
    virtual uint8_t read(double t) = 0;
    /** STOP condition, or the end of the simulation (final = true). */
    virtual void stop(double t, bool final) = 0;
};

/**
 * Bit-level I2C slave side: fed with the levels of SDA and SCL, it tells
 * when to pull SDA low. Independent of ngspice (unit-tested).
 */
class I2cSlaveBus
{
public:
    explicit I2cSlaveBus(std::vector<std::unique_ptr<I2cTarget>> targets);

    void sda(bool level, double t);
    void scl(bool level, double t);

    /** Level wanted on SDA by the slaves: true = pull low. */
    bool pullLow() const { return m_pull; }

    /** End of the simulation. */
    void finish(double t);

    /** Transactions finished since the last call (log lines). */
    std::vector<std::string> takeLog();

    std::vector<std::unique_ptr<I2cTarget>> &targets() { return m_targets; }

private:
    enum class State
    {
        Idle,       // Waiting for a START.
        Address,    // Receiving the address byte.
        AckAddr,    // Address ACK clock.
        Write,      // Receiving a data byte.
        AckWrite,   // Data ACK clock.
        Read,       // Sending a data byte.
        ReadAck,    // Master ACK clock after a sent byte.
        Ignore,     // Not addressed, or master NACK: until START / STOP.
    };

    void risingScl();
    void fallingScl(double t);
    void sendBit();
    void endTransaction(double t);

    std::vector<std::unique_ptr<I2cTarget>> m_targets;
    I2cTarget *m_target = nullptr;
    State   m_state = State::Idle;
    bool    m_sda = true;
    bool    m_scl = true;
    bool    m_pull = false;
    bool    m_read = false;
    bool    m_masterAck = false;
    uint8_t m_shift = 0;
    int     m_bits = 0;
    std::string m_line;
    std::vector<std::string> m_log;
};

struct I2cBusConfig
{
    struct Slave
    {
        std::string kind;    // "lm75", "eeprom", "lcd"
        int address = 0;
    };
    std::vector<Slave> slaves;
    double temp0 = 25.0;
    double rate = 0.0;
    LcdConfig lcd;
    bool log = false;
};

/** True when sim_args contain "device" "i2c". */
bool isI2cSlaveConfig(const std::vector<std::string> &args);

/** Parse the keys above; errors on stderr. */
bool parseI2cSlaveConfig(const std::vector<std::string> &args, I2cBusConfig &cfg);

/** Targets described by a configuration. */
std::vector<std::unique_ptr<I2cTarget>> makeI2cTargets(const I2cBusConfig &cfg);

class I2cSlaveDevice : public CosimDevice
{
public:
    static constexpr unsigned int kInputs = 2;     // SDA SCL
    static constexpr unsigned int kOutputs = 1;    // SDA pull-down

    explicit I2cSlaveDevice(const I2cBusConfig &cfg);
    ~I2cSlaveDevice() override;

    void step(co_info *info) override;
    void input(co_info *info, unsigned int index, const Digital_t *value) override;

private:
    void printLog();

    I2cSlaveBus m_bus;
    bool   m_log;
    bool   m_out = false;          // Published output.
    bool   m_pending = false;      // An output change waits for its time.
    double m_pendingTime = 0.0;
    double m_last = 0.0;
};

} // namespace kisimavr

#endif // KISIMAVR_I2C_SLAVE_DEVICE_H
