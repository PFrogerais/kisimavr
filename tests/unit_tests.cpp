/*
 * kisimavr - ATmega co-simulation for ngspice / KiCad
 * Copyright (C) 2026 Paul
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file unit_tests.cpp
 * @brief Unit tests of the simavr-independent modules (config, I2C devices,
 *        UART script, paths, vcd).
 *
 * Usage: unit_tests <scratch directory>
 */

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "config.h"
#include "i2c_devices.h"
#include "paths.h"
#include "hd44780.h"
#include "lcd_device.h"
#include "rp2040_config.h"
#include "spi_frame.h"
#include "uart_script.h"
#include "vcd.h"

namespace fs = std::filesystem;
using namespace kisimavr;

namespace
{

int g_failures = 0;

#define CHECK(cond)                                                              \
    do                                                                           \
    {                                                                            \
        if (!(cond))                                                             \
        {                                                                        \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, \
                         #cond);                                                 \
            ++g_failures;                                                        \
        }                                                                        \
    } while (0)

bool parse(const std::vector<std::string> &args, Config &cfg)
{
    cfg = Config{};
    return parseConfig(args, cfg);
}

std::string readFile(const fs::path &path)
{
    std::ifstream in(path);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

void testParsePin()
{
    PinRef pin{};
    CHECK(parsePin("PB5", pin) && pin.port == 'B' && pin.bit == 5);
    CHECK(parsePin("pd2", pin) && pin.port == 'D' && pin.bit == 2);
    CHECK(parsePin("C0", pin) && pin.port == 'C' && pin.bit == 0);
    CHECK(!parsePin("PB8", pin));
    CHECK(!parsePin("PZ1", pin));
    CHECK(!parsePin("PB", pin));
    CHECK(!parsePin("", pin));
}

void testParseConfig()
{
    Config cfg;

    // Netlist form: one string per key and per value, multi-valued keys.
    CHECK(parse({ "hex", "fw.hex", "mcu", "ATmega328P", "freq", "8000000", "in", "PD2", "PD3",
                  "out", "PB5", "io", "PC0", "adc", "0", "ADC1", "vref", "3.3", "log", "yes" },
                cfg));
    CHECK(cfg.hexPath == "fw.hex");
    CHECK(cfg.mcuName == "atmega328p");
    CHECK(cfg.frequency == 8000000u);
    CHECK(cfg.inputs.size() == 2u && cfg.inputs[1] == (PinRef{ 'D', 3 }));
    CHECK(cfg.outputs.size() == 1u && cfg.ios.size() == 1u);
    CHECK(cfg.adcChannels == (std::vector<int>{ 0, 1 }));
    CHECK(cfg.vref == 3.3);
    CHECK(cfg.log);
    CHECK(inputPortCount(cfg) == 2u + 1u + 2u);
    CHECK(outputPortCount(cfg) == 1u + 3u + static_cast<unsigned>(kDacBits));

    // Compact forms (runner) and a lone value taken as the firmware.
    CHECK(parse({ "blink.hex", "out=PB5,PB0", "in:PD2" }, cfg));
    CHECK(cfg.hexPath == "blink.hex");
    CHECK(cfg.outputs.size() == 2u && cfg.inputs.size() == 1u);
    CHECK(outputPortCount(cfg) == 2u);    // No DAC without "adc".

    // Errors.
    CHECK(!parse({ "out", "PB5" }, cfg));                       // No firmware.
    CHECK(!parse({ "hex", "a.hex", "out", "PZ9" }, cfg));       // Bad pin.
    CHECK(!parse({ "hex", "a.hex", "adc", "16" }, cfg));        // Bad channel.
    CHECK(!parse({ "hex", "a.hex", "freq", "0" }, cfg));
    CHECK(!parse({ "hex", "a.hex", "vref", "-1" }, cfg));
}

void testI2cConfig()
{
    Config cfg;
    CHECK(parse({ "hex", "fw.hex", "io", "PC4", "PC5", "i2c", "lm75", "eeprom", "0x51",
                  "lm75temp", "-10.5", "lm75rate", "200", "i2clog", "0" }, cfg));
    CHECK(cfg.i2cSlaves.size() == 2u);
    CHECK(cfg.i2cSlaves[0].type == I2cDevice::Type::Lm75);
    CHECK(cfg.i2cSlaves[0].address == 0x48);                  // Default address.
    CHECK(cfg.i2cSlaves[1].type == I2cDevice::Type::Eeprom24c02);
    CHECK(cfg.i2cSlaves[1].address == 0x51);
    CHECK(cfg.lm75Temp == -10.5);
    CHECK(cfg.lm75Rate == 200.0);
    CHECK(!cfg.i2cLog);
    CHECK(cfg.sdaPin == "PC4" && cfg.sclPin == "PC5");
    CHECK(inputPortCount(cfg) == 2u && outputPortCount(cfg) == 6u);    // No I2C port.

    CHECK(parse({ "hex", "fw.hex", "i2c", "lm75", "72" }, cfg));    // Decimal address.
    CHECK(cfg.i2cSlaves.size() == 1u && cfg.i2cSlaves[0].address == 0x48);

    CHECK(!parse({ "hex", "fw.hex", "i2c", "foo" }, cfg));             // Unknown device.
    CHECK(!parse({ "hex", "fw.hex", "i2c", "0x48" }, cfg));            // Address first.
    CHECK(!parse({ "hex", "fw.hex", "i2c", "lm75", "0x80" }, cfg));    // Not 7 bits.
    CHECK(!parse({ "hex", "fw.hex", "i2c", "lm75", "eeprom", "0x48" }, cfg));    // Clash.
}

void testLm75()
{
    // Register encoding: 9 bits, 0.5 degC steps, left-justified.
    CHECK(I2cDevice::lm75Word(25.0) == 0x1900);
    CHECK(I2cDevice::lm75Word(25.5) == 0x1980);
    CHECK(I2cDevice::lm75Word(-0.5) == 0xFF80);
    CHECK(I2cDevice::lm75Word(-25.0) == 0xE700);
    CHECK(I2cDevice::lm75Word(125.0) == 0x7D00);
    CHECK(I2cDevice::lm75Word(200.0) == 0x7D00);    // Clamped to +125.
    CHECK(I2cDevice::lm75Word(-80.0) == 0xC900);    // Clamped to -55.

    // Pointer 0 then repeated START: temperature = 20 + 1000 * t.
    I2cDevice lm75(I2cDevice::Type::Lm75, 0x48, 20.0, 1000.0);
    lm75.start();
    lm75.write(0x00);
    lm75.start();
    CHECK(lm75.read(0.0015) == 0x15);    // 21.5 degC
    CHECK(lm75.read(0.0015) == 0x80);

    // Without a new pointer, the next read starts again at the MSB.
    lm75.start();
    CHECK(lm75.read(0.0) == 0x14);       // 20.0 degC
    CHECK(lm75.read(0.0) == 0x00);

    // Tos (pointer 3): 80 degC at power-up, writable.
    lm75.start();
    lm75.write(0x03);
    lm75.start();
    CHECK(lm75.read(0.0) == 80);
    lm75.start();
    lm75.write(0x03);
    lm75.write(60);
    lm75.write(0x00);
    lm75.start();
    CHECK(lm75.read(0.0) == 60);

    // The temperature register is read-only.
    lm75.start();
    lm75.write(0x00);
    lm75.write(0x7F);
    lm75.start();
    CHECK(lm75.read(0.0) == 0x14);
}

void testEeprom()
{
    I2cDevice ee(I2cDevice::Type::Eeprom24c02, 0x50);
    ee.start();
    ee.write(0x10);
    CHECK(ee.read(0.0) == 0xFF);    // Erased.

    ee.start();
    ee.write(0xFE);                 // Sequential write wraps around at 256.
    ee.write('S');
    ee.write('T');
    ee.write('I');
    ee.start();
    ee.write(0xFE);
    ee.start();
    CHECK(ee.read(0.0) == 'S');
    CHECK(ee.read(0.0) == 'T');
    CHECK(ee.read(0.0) == 'I');     // Address 0x00.

    ee.start();
    ee.write(0x00);
    ee.start();
    CHECK(ee.read(0.0) == 'I');
}

void testUartScript()
{
    double t = 0.0;
    CHECK(parseSpiceTime("1m", t) && t == 1e-3);
    CHECK(parseSpiceTime("2.5e-3", t) && t == 2.5e-3);
    CHECK(parseSpiceTime("250u", t) && t == 250e-6);
    CHECK(parseSpiceTime("3ms", t) && t == 3e-3);
    CHECK(parseSpiceTime("0", t) && t == 0.0);
    CHECK(!parseSpiceTime("1meg", t));    // Far beyond a simulation.
    CHECK(!parseSpiceTime("1k", t));
    CHECK(!parseSpiceTime("abc", t));
    CHECK(!parseSpiceTime("-1m", t));

    std::vector<UartEvent> ev;
    std::string err;
    CHECK(parseUartScript("# commands\r\n"
                          "\n"
                          "3m   LED OFF\\n   \r\n"      // Trailing blanks dropped
                          "1m\tLED ON\\r\\n\n"
                          "2m   \\sA\\x42\\\\\\t\n"
                          "1m   second at 1m\n",
                          ev, err));
    CHECK(ev.size() == 4u);
    if (ev.size() == 4u)
    {
        CHECK(ev[0].time == 1e-3 && ev[0].bytes == "LED ON\r\n");
        CHECK(ev[1].time == 1e-3 && ev[1].bytes == "second at 1m");    // Stable order.
        CHECK(ev[2].time == 2e-3 && ev[2].bytes == " AB\\\t");
        CHECK(ev[3].time == 3e-3 && ev[3].bytes == "LED OFF\n");
    }

    CHECK(!parseUartScript("1m ok\n2x bad\n", ev, err));
    CHECK(err == "line 2: invalid time '2x'");
    CHECK(!parseUartScript("1m\n", ev, err));
    CHECK(err == "line 1: no text after the time");
    CHECK(!parseUartScript("1m bad \\q\n", ev, err));
    CHECK(err == "line 1: unknown escape '\\q'");
    CHECK(!parseUartScript("1m \\x4\n", ev, err));
    CHECK(err == "line 1: \\x needs two hexadecimal digits");
    CHECK(!parseUartScript("1m end\\\n", ev, err));
}

void testResolvePath(const fs::path &dir)
{
    const fs::path real = dir / "Arduino" / "Blink.hex";
    fs::create_directories(real.parent_path());
    std::ofstream(real) << ":00000001FF\n";

    // KiCad lower-cases the netlist: the real case is restored.
    const std::string lower = (dir / "arduino" / "blink.hex").string();
    CHECK(resolvePath(lower) == real.string());
    CHECK(resolvePath(real.string()) == real.string());
    CHECK(resolvePath((dir / "missing.hex").string()) == (dir / "missing.hex").string());
    CHECK(resolveOutputPath((dir / "arduino" / "new.txt").string()) ==
          (dir / "Arduino" / "new.txt").string());
}

void testContentChecks()
{
    CHECK(hasExtension("a/b/uart_log.TXT", { ".txt", ".log" }));
    CHECK(!hasExtension("main.c", { ".txt", ".log" }));
    CHECK(!hasExtension("noext", { ".vcd" }));

    CHECK(looksLikeVcd("$timescale 1ns $end\n"));
    CHECK(!looksLikeVcd("not a trace\n"));
    CHECK(looksLikeUartLog("Hello\nADC0 = 512 (2500 mV)\n"));
    CHECK(!looksLikeUartLog("tab\there\n"));
    CHECK(!looksLikeUartLog(std::string(201, 'x')));
    CHECK(looksLikeUartLog(std::string(200, 'x') + "\n"));
}

void testVcd(const fs::path &dir)
{
    const fs::path path = dir / "trace.vcd";
    {
        VcdWriter vcd;
        vcd.open(path.string(), "atmega328p", { { 'B', 5 }, { 'D', 2 } }, 16e6);
        CHECK(vcd.isOpen());
        vcd.write(0, 16, Digital_t{ ONE, STRONG });            // 16 cycles = 1000 ns.
        vcd.write(1, 16, Digital_t{ UNKNOWN, HI_IMPEDANCE });  // Same time stamp.
        vcd.write(0, 32, Digital_t{ ZERO, STRONG });
    }
    const std::string text = readFile(path);
    CHECK(text.rfind("$timescale 1ns $end\n", 0) == 0);
    CHECK(text.find("$var wire 1 ! PB5 $end\n$var wire 1 \" PD2 $end\n") != std::string::npos);
    CHECK(text.find("#1000\n1!\nz\"\n#2000\n0!\n") != std::string::npos);

    VcdWriter tooMany;
    tooMany.open((dir / "big.vcd").string(), "m",
                 std::vector<PinRef>(VcdWriter::kMaxSignals + 1, PinRef{ 'B', 0 }), 16e6);
    CHECK(!tooMany.isOpen());
    CHECK(!fs::exists(dir / "big.vcd"));
}

} // namespace

/** Replays a frame: SCK / MOSI levels, MISO sampled from a byte "sent back". */
struct SpiReplay
{
    int     sckEdges  = 0;
    uint8_t mosiByte  = 0;
    uint8_t received  = 0;
    bool    endsIdle  = false;
    uint32_t lastOffset = 0;
};

SpiReplay replaySpi(uint8_t value, uint8_t spcr, uint32_t period, uint8_t slaveByte)
{
    SpiReplay r;
    const bool cpol = (spcr & spi_reg::kCpol) != 0u;
    bool sck  = cpol;
    bool mosi = false;
    int  bit  = 0;
    for (const SpiEvent &e : spiFrame(value, spcr, period))
    {
        if (e.mosi >= 0)
        {
            mosi = e.mosi != 0;
        }
        if (e.sample)
        {
            // The master samples MOSI's own bit too, to check the order.
            r.mosiByte = spiSetBit(r.mosiByte, bit, mosi, spcr);
            r.received = spiSetBit(r.received, bit, spiBit(slaveByte, bit, spcr), spcr);
            ++bit;
        }
        if (e.sck >= 0 && (e.sck != 0) != sck)
        {
            sck = e.sck != 0;
            ++r.sckEdges;
        }
        r.lastOffset = e.offset;
    }
    r.endsIdle = (sck == cpol);
    return r;
}

void testSpiFrame()
{
    // Divider: SPR1:0 = 00 -> /4, 01 -> /16, 10 -> /64, 11 -> /128; SPI2X halves.
    CHECK(spiPeriod(0x00, 0x00) == 4u);
    CHECK(spiPeriod(0x01, 0x00) == 16u);
    CHECK(spiPeriod(0x03, 0x00) == 128u);
    CHECK(spiPeriod(0x00, spi_reg::kSpi2x) == 2u);
    CHECK(spiPeriod(0x01, spi_reg::kSpi2x) == 8u);

    // MSB first by default, LSB first with DORD.
    CHECK(spiBit(0x80, 0, 0) && !spiBit(0x80, 7, 0));
    CHECK(spiBit(0x01, 0, spi_reg::kDord) && !spiBit(0x01, 0, 0));

    for (uint8_t mode = 0; mode < 4; ++mode)
    {
        for (uint8_t dord : { uint8_t{ 0 }, spi_reg::kDord })
        {
            const uint8_t spcr = static_cast<uint8_t>(spi_reg::kSpe | spi_reg::kMstr | dord |
                                                      (mode << 2));    // CPHA, CPOL
            const SpiReplay r = replaySpi(0xA5, spcr, 16, 0x3C);
            CHECK(r.sckEdges == 16);           // 8 leading + 8 trailing edges.
            CHECK(r.mosiByte == 0xA5);         // MOSI is valid at each sample.
            CHECK(r.received == 0x3C);
            CHECK(r.endsIdle);                 // SCK back to CPOL.
            CHECK(r.lastOffset <= 8u * 16u);   // Within simavr's 8 periods.
        }
    }

    // Mode 0: MOSI is set half a period before the first rising edge.
    const std::vector<SpiEvent> f = spiFrame(0x80, spi_reg::kSpe | spi_reg::kMstr, 4);
    CHECK(f[0].offset == 0u && f[0].mosi == 1 && f[0].sck == -1);
    CHECK(f[1].offset == 2u && f[1].sck == 1 && f[1].sample);

    CHECK(spiLogText({ 0xA5, 0x3C }, { 0x00, 0xA5 }) == "MOSI A5 3C | MISO 00 A5");
}

/** Sends one byte as LiquidCrystal does in 4-bit mode (two strobes). */
void lcdWrite4(Hd44780 &lcd, bool rs, uint8_t value, double &t)
{
    lcd.strobe(rs, false, static_cast<uint8_t>(value & 0xF0u), t);
    lcd.strobe(rs, false, static_cast<uint8_t>(value << 4), t + 2e-6);
    t += (value <= 0x03u && !rs) ? 2e-3 : 100e-6;
}

/** LiquidCrystal::begin(16, 2): 50 ms, 0x3 x 3, 0x2, then 4-bit commands. */
void lcdBegin(Hd44780 &lcd, double &t)
{
    t = 50e-3;
    for (const double wait : { 4.5e-3, 4.5e-3, 150e-6, 100e-6 })
    {
        const uint8_t nibble = (wait == 100e-6) ? 0x20u : 0x30u;    // Last one: 4-bit.
        lcd.strobe(false, false, nibble, t);
        t += wait;
    }
    for (const uint8_t cmd : { uint8_t{ 0x28 }, uint8_t{ 0x0C }, uint8_t{ 0x01 }, uint8_t{ 0x06 } })
    {
        lcdWrite4(lcd, false, cmd, t);
    }
}

void lcdPrint(Hd44780 &lcd, const char *text, double &t)
{
    for (const char *p = text; *p != '\0'; ++p)
    {
        lcdWrite4(lcd, true, static_cast<uint8_t>(*p), t);
    }
}

void testHd44780()
{
    Hd44780 lcd(16, 2);
    double t = 0.0;
    lcdBegin(lcd, t);
    CHECK(lcd.fourBit());
    CHECK(lcd.displayOn());
    CHECK(lcd.warnings().empty());

    lcdPrint(lcd, "Bonjour STI2D", t);
    lcdWrite4(lcd, false, 0xC0, t);                  // setCursor(0, 1)
    lcdPrint(lcd, "T = 23", t);
    lcdWrite4(lcd, true, 0xDF, t);                   // Degree sign
    lcdPrint(lcd, "C", t);
    std::vector<std::string> rows = lcd.lines();
    CHECK(rows.size() == 2u);
    CHECK(rows[0] == "Bonjour STI2D   ");
    CHECK(rows[1] == "T = 23\xC2\xB0" "C        ");
    CHECK(lcd.warnings().empty());

    // Display shift left by one: the first character disappears.
    lcdWrite4(lcd, false, 0x18, t);
    CHECK(lcd.lines()[0] == "onjour STI2D    ");
    // Clear, then a command right after it (busy for 1.52 ms): warning.
    lcd.strobe(false, false, 0x00, t);
    lcd.strobe(false, false, 0x10, t + 1e-6);        // 0x01 = clear
    lcd.strobe(false, false, 0x80, t + 10e-6);
    lcd.strobe(false, false, 0x00, t + 11e-6);       // 0x80, too early
    CHECK(lcd.lines()[0] == std::string(16, ' '));
    CHECK(lcd.warnings().size() == 1u && lcd.warnings()[0].find("busy") != std::string::npos);

    // Too early after power-up, and a read cycle.
    Hd44780 early(20, 4);
    early.strobe(false, false, 0x30, 1e-3);
    early.strobe(false, true, 0x00, 2e-3);
    CHECK(early.warnings().size() == 2u);
    CHECK(early.lines().size() == 4u && early.lines()[3].size() == 20u);

    // 20x4: row 2 continues row 0 in DDRAM (address 0x14).
    Hd44780 big(20, 4);
    lcdBegin(big, t);
    lcdWrite4(big, false, 0x80 | 0x14, t);
    lcdPrint(big, "ligne 3", t);
    CHECK(big.lines()[2].rfind("ligne 3", 0) == 0);

    // Device keys.
    LcdConfig cfg;
    CHECK(isDeviceConfig({ "device", "hd44780" }));
    CHECK(!isDeviceConfig({ "hex", "a.hex" }));
    CHECK(parseLcdConfig({ "device", "hd44780", "cols", "20", "rows", "4" }, cfg) &&
          cfg.cols == 20 && cfg.rows == 4);
    CHECK(!parseLcdConfig({ "device", "ssd1306" }, cfg));
    CHECK(!parseLcdConfig({ "device", "hd44780", "rows", "5" }, cfg));
}

/** The LCD device decodes E falling edges from its 11 inputs. */
void testLcdDevice()
{
    LcdConfig cfg;
    LcdDevice dev(cfg);
    co_info info{};
    double t = 50e-3;
    auto set = [&](unsigned int pin, bool level) {
        Digital_t v{ level ? ONE : ZERO, STRONG };
        info.vtime = t;
        dev.input(&info, pin, &v);
    };
    // RS=0, 8-bit transfer 0x38 (8-bit, 2 lines) then 0x0C (display on).
    for (const uint8_t cmd : { uint8_t{ 0x38 }, uint8_t{ 0x0C } })
    {
        for (unsigned int k = 0; k < 8; ++k)
        {
            set(3 + k, ((cmd >> k) & 1u) != 0u);
        }
        t += 1e-6;
        set(2, true);
        t += 1e-6;
        set(2, false);
        t += 100e-6;
    }
    // Data 'A' (0x41) with RS = 1.
    set(0, true);
    for (unsigned int k = 0; k < 8; ++k)
    {
        set(3 + k, ((0x41 >> k) & 1u) != 0u);
    }
    t += 1e-6;
    set(2, true);
    t += 1e-6;
    set(2, false);
    CHECK(dev.controller().displayOn());
    CHECK(dev.controller().lines()[0][0] == 'A');
}

void testRp2040Config()
{
    CHECK(parseGpio("GP25") == 25 && parseGpio("gp0") == 0 && parseGpio("29") == 29);
    CHECK(parseGpio("GP30") == -1 && parseGpio("PB5") == -1 && parseGpio("") == -1);
    CHECK(isRp2040Config({ "mcu", "rp2040", "fw", "a.uf2" }));
    CHECK(!isRp2040Config({ "mcu", "atmega328p" }));

    Rp2040Config cfg;
    CHECK(parseRp2040Config({ "mcu", "rp2040", "fw", "a.uf2", "io", "GP0", "GP25", "adc", "0",
                              "2", "uartlog", "u.txt" },
                            cfg));
    CHECK(cfg.fwPath == "a.uf2" && cfg.uartLog == "u.txt");
    CHECK(cfg.ios == (std::vector<int>{ 0, 25 }) && cfg.adc == (std::vector<int>{ 0, 2 }));
    CHECK(rp2040InputCount(cfg) == 4u && rp2040OutputCount(cfg) == 8u + 12u);

    Rp2040Config bad;
    CHECK(!parseRp2040Config({ "mcu", "rp2040", "io", "GP1" }, bad));              // No firmware
    CHECK(!parseRp2040Config({ "mcu", "rp2040", "fw", "a", "io", "GP1", "GP1" }, bad));
    CHECK(!parseRp2040Config({ "mcu", "rp2040", "fw", "a", "adc", "4" }, bad));
    CHECK(!parseRp2040Config({ "mcu", "rp2040", "fw", "a", "foo", "1" }, bad));
}

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        std::fprintf(stderr, "usage: %s <scratch directory>\n", argv[0]);
        return 2;
    }
    const fs::path dir = fs::absolute(argv[1]);
    fs::remove_all(dir);
    fs::create_directories(dir);

    testParsePin();
    testParseConfig();
    testI2cConfig();
    testLm75();
    testEeprom();
    testUartScript();
    testSpiFrame();
    testHd44780();
    testLcdDevice();
    testRp2040Config();
    testResolvePath(dir);
    testContentChecks();
    testVcd(dir);

    if (g_failures != 0)
    {
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("all unit tests passed\n");
    return 0;
}
