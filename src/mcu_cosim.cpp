/*
 * kisimavr - ATmega co-simulation for ngspice / KiCad
 * Copyright (C) 2026 Paul
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file mcu_cosim.cpp
 * @brief ngspice d_cosim plug-in running AVR firmware (.hex) with simavr.
 *
 * The shared library is loaded by the XSPICE d_cosim code model:
 *
 *   amcu [pd2_d] [pb5_d pb0_d] null mcu
 *   .model mcu d_cosim simulation="mcu_cosim.so"
 *   + sim_args=["hex" "blink.hex" "mcu" "atmega328p" "freq" "16000000"
 *   +           "in" "PD2" "out" "PB5" "PB0"]
 *
 * Each key is followed by its value(s) as separate strings (see config.h).
 *
 * Keys:
 *   hex   Intel HEX firmware file (required)
 *   mcu   simavr core name, e.g. atmega328p (default atmega328p)
 *   freq  CPU clock in Hz (default 16000000)
 *   in    pins driven by the circuit, e.g. "PD2" "PD3"
 *   out   pins driven by the MCU, e.g. "PB5" "PB0"
 *   io    bidirectional pins, direction set by the firmware (DDRx): each
 *         one has 1 input (pin voltage read back) and 3 outputs
 *         (level = PORT bit, oe = DDR bit, pu = pull-up enabled)
 *   adc   analog channels measured by the SAR model, e.g. "0" "1"
 *   vref  DAC full scale in volts (default 5), i.e. AVCC
 *   adcbit  duration of one SAR step in seconds (default 1e-6)
 *   txpin   output pin that carries the UART0 TX frames (default PD1)
 *   uartlog file receiving the UART0 text, .txt or .log (default: none)
 *   uartin  timed text sent to the UART0 receiver (see uart_script.h)
 *   rxpin   pin whose frames are decoded by the receiver (default PD0)
 *   vcd     VCD file of the GPIO pins, .vcd, for GTKWave / PulseView
 *           (output files are never written through a symbolic link, and an
 *           existing file is only replaced if mcu_cosim wrote it)
 *   i2c     virtual I2C slaves: "lm75" ["0x48"] "eeprom" ["0x50"]
 *   lm75temp / lm75rate  LM75 temperature at t = 0 (degC) and slope (degC/s)
 *   i2clog  0 to hide the I2C transactions (default 1)
 *   spilog  0 to hide the SPI transfers (default 1); SPI master on
 *           SCK = PB5, MOSI = PB3, MISO = PB4 ("io" pins), slaves in the circuit
 *   sda / scl  TWI pins rebuilt as open-drain outputs (default PC4 / PC5)
 *   log   1 to print pin activity on stderr (default 0)
 *
 * With "mcu" "rp2040" the instance is a Raspberry Pi Pico (rp2040js run by
 * QuickJS, see rp2040_device.h and rp2040_config.h), with "mcu" "rp2350" a
 * Raspberry Pi Pico 2 (rp2350js, Hazard3 RISC-V or Cortex-M33 cores).
 *
 * With "device" "i2c" the instance is a set of I2C slaves on SDA / SCL
 * (LM75, 24C02, LCD with a PCF8574 backpack; see i2c_slave_device.h).
 * With "device" "hcsr04" / "servo": an HC-SR04 ultrasonic sensor or an RC
 * servomotor (see sensor_devices.h).
 * With "device" "hd44780" the instance is a character LCD instead of an
 * MCU (see lcd_device.h): 11 inputs RS RW E D0..D7, keys cols, rows,
 * lcdlog, name.
 *
 * Modules:
 *   config.*      sim_args parsing, port counts
 *   paths.*       KiCad lower-case path recovery, guarded output files
 *   mcu_bridge.*  simavr instance <-> d_cosim ports, timing, GPIO
 *   sar_adc.*     SAR converter rebuilt across the d_cosim boundary
 *   uart0.*       UART0 console, log file, TXD frames and reception
 *   uart_script.* "uartin" file parser (simavr independent)
 *   i2c_bus.*     TWI master: virtual slaves, transaction log, SDA/SCL
 *   i2c_devices.* LM75 and 24C02 protocol logic (simavr independent)
 *   vcd.*         VCD trace of the pins
 *   spi_bus.*     SPI master on the pins; spi_frame.* edge timing
 *   lcd_device.*  HD44780 LCD as a circuit part; hd44780.* controller logic
 * This file only holds the d_cosim entry point.
 */

#include <cstdio>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "common.h"
#include "config.h"
#include "cosim_abi.h"
#include "i2c_slave_device.h"
#include "lcd_device.h"
#include "sensor_devices.h"
#include "mcu_bridge.h"
#include "rp2040_config.h"
#ifdef KISIMAVR_HAVE_RP2040
#include "rp2040_device.h"
#endif

#define COSIM_EXPORT extern "C" __attribute__((visibility("default")))

namespace
{

using kisimavr::CosimDevice;

/*
 * Output guard, a workaround for d_cosim (ngspice 44 to 47 at least).
 *
 * When a change is reported at a vtime well before the SPICE time, d_cosim
 * truncates the time step and sends the change at the start of its next
 * call. If, in that very call, the device reports another change close
 * enough to the SPICE time to be sent directly, d_cosim writes its outputs
 * a second time and clears the "changed" flags of the first batch: those
 * changes are lost for good (seen on a PWM pin whose output enable never
 * reached the circuit).
 *
 * The guard sits between the device and d_cosim: in a call that may follow
 * a truncation, a change that d_cosim could send directly is held back and
 * delivered at the start of the next call instead.
 *
 * Second issue: d_cosim compares each change with a copy of the outputs
 * that it never initialises. A first change to the value that happens to
 * sit there is taken for "no change" and dropped. All devices start with
 * their outputs at 0, so the guard reports 0 on every output at the first
 * step: the copy then matches the circuit.
 */
struct Instance
{
    std::unique_ptr<CosimDevice> device;
    void (*ngspiceOut)(co_info *, unsigned int, Digital_t *) = nullptr;
    std::vector<std::pair<unsigned int, Digital_t>> batch;   // Changes of this call.
    std::vector<std::pair<unsigned int, Digital_t>> held;    // Sent next call.
    bool mayFlush = false;   // d_cosim may have sent a batch on entry.
    bool synced   = false;   // Initial outputs reported.
};

/** Smallest margin to the SPICE time that surely truncates the step. */
constexpr double kDirectWindow = 1e-6;    // Larger than the d_cosim "delay".

Instance *instanceOf(co_info *info)
{
    return static_cast<Instance *>(info->handle);
}

void collectOutput(co_info *info, unsigned int index, Digital_t *value)
{
    instanceOf(info)->batch.emplace_back(index, *value);
}

void forward(co_info *info, std::vector<std::pair<unsigned int, Digital_t>> &changes)
{
    Instance *inst = instanceOf(info);
    for (auto &change : changes)
    {
        inst->ngspiceOut(info, change.first, &change.second);
    }
    changes.clear();
}

void cosimStep(co_info *info)
{
    Instance *inst  = instanceOf(info);
    const double target = info->vtime;

    if (!inst->synced)
    {
        // First step: report the initial outputs (all 0), see above.
        inst->synced = true;
        const unsigned int count = info->out_count + info->inout_count;
        for (unsigned int i = 0; i < count; ++i)
        {
            Digital_t zero{ ZERO, STRONG };
            inst->ngspiceOut(info, i, &zero);
        }
        inst->mayFlush = true;
        return;
    }
    if (!inst->held.empty())
    {
        // Held changes: sent now, at this call's time, before running on.
        forward(info, inst->held);
        inst->mayFlush = true;
        return;
    }

    info->out_fn = collectOutput;
    inst->device->step(info);
    info->out_fn = inst->ngspiceOut;

    if (inst->batch.empty())
    {
        inst->mayFlush = false;
        return;
    }
    if (inst->mayFlush && target - info->vtime <= kDirectWindow)
    {
        // Could be sent directly right after a flushed batch: hold it.
        inst->held.swap(inst->batch);
        info->vtime = target;
        inst->mayFlush = false;
        return;
    }
    forward(info, inst->batch);
    inst->mayFlush = true;
}

void cosimInput(co_info *info, unsigned int index, Digital_t *value)
{
    Instance *inst = instanceOf(info);
    inst->device->input(info, index, value);
}

void cosimCleanup(co_info *info)
{
    delete instanceOf(info);
    info->handle = nullptr;
}

void idleStep(co_info * /*info*/)
{
    // Configuration failed: keep outputs frozen.
}

void idleInput(co_info * /*info*/, unsigned int /*index*/, Digital_t * /*value*/)
{
}

} // namespace

COSIM_EXPORT void Cosim_setup(co_info *info)
{
    std::vector<std::string> args;
    for (unsigned int i = 0; i < info->sim_argc; ++i)
    {
        args.emplace_back(info->sim_argv[i] != nullptr ? info->sim_argv[i] : "");
    }

    info->inout_count = 0;
    info->method      = Normal;

    std::unique_ptr<CosimDevice> bridge;
    bool ok = false;
    if (kisimavr::isRp2040Config(args))
    {
        // Raspberry Pi Pico / Pico 2: rp2040js / rp2350js in QuickJS.
        kisimavr::Rp2040Config rp;
        ok = kisimavr::parseRp2040Config(args, rp);
        info->in_count  = kisimavr::rp2040InputCount(rp);
        info->out_count = kisimavr::rp2040OutputCount(rp);
#ifdef KISIMAVR_HAVE_RP2040
        if (ok)
        {
            auto pico = std::make_unique<kisimavr::Rp2040Device>(std::move(rp));
            ok = pico->start();
            bridge = std::move(pico);
        }
#else
        std::fprintf(stderr, "%s RP2040 support not built (cmake -DKISIMAVR_RP2040=ON)\n",
                     kisimavr::kTag);
        ok = false;
#endif
    }
    else if (kisimavr::isI2cSlaveConfig(args))
    {
        // I2C slaves (LM75, 24C02, LCD with PCF8574) on SDA / SCL.
        kisimavr::I2cBusConfig i2c;
        ok = kisimavr::parseI2cSlaveConfig(args, i2c);
        info->in_count  = kisimavr::I2cSlaveDevice::kInputs;
        info->out_count = kisimavr::I2cSlaveDevice::kOutputs;
        if (ok)
        {
            bridge = std::make_unique<kisimavr::I2cSlaveDevice>(i2c);
        }
    }
    else if (kisimavr::deviceKind(args) == "hcsr04")
    {
        // HC-SR04 ultrasonic sensor: TRIG in, ECHO out.
        kisimavr::HcSr04Config sr;
        ok = kisimavr::parseHcSr04Config(args, sr);
        info->in_count  = 1;
        info->out_count = 1;
        if (ok)
        {
            bridge = std::make_unique<kisimavr::HcSr04Device>(sr);
        }
    }
    else if (kisimavr::deviceKind(args) == "servo")
    {
        // RC servomotor: SIG in, angle (8 bits) out.
        kisimavr::ServoConfig servo;
        ok = kisimavr::parseServoConfig(args, servo);
        info->in_count  = 1;
        info->out_count = kisimavr::ServoDevice::kBits;
        if (ok)
        {
            bridge = std::make_unique<kisimavr::ServoDevice>(servo);
        }
    }
    else if (kisimavr::isDeviceConfig(args))
    {
        // A circuit device (character LCD) instead of an MCU.
        kisimavr::LcdConfig lcd;
        ok = kisimavr::parseLcdConfig(args, lcd);
        info->in_count  = kisimavr::LcdDevice::kInputs;
        info->out_count = 0;
        if (ok)
        {
            bridge = std::make_unique<kisimavr::LcdDevice>(std::move(lcd));
        }
    }
    else
    {
        kisimavr::Config cfg;
        const bool parsed = kisimavr::parseConfig(args, cfg);
        info->in_count  = kisimavr::inputPortCount(cfg);
        info->out_count = kisimavr::outputPortCount(cfg);
        auto mcu = std::make_unique<kisimavr::McuBridge>(std::move(cfg));
        ok = parsed && mcu->start();
        bridge = std::move(mcu);
    }
    if (!ok)
    {
        std::fprintf(stderr, "%s setup failed: MCU outputs stay at their initial value\n",
                     kisimavr::kTag);
        info->handle  = nullptr;
        info->step    = idleStep;
        info->in_fn   = idleInput;
        info->cleanup = nullptr;
        return;
    }

    auto inst = std::make_unique<Instance>();
    inst->device     = std::move(bridge);
    inst->ngspiceOut = info->out_fn;
    info->handle  = inst.release();
    info->step    = cosimStep;
    info->in_fn   = cosimInput;
    info->cleanup = cosimCleanup;
}
