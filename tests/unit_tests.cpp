/*
 * kisimavr - ATmega co-simulation for ngspice / KiCad
 * Copyright (C) 2026 Paul
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file unit_tests.cpp
 * @brief Unit tests of the simavr-independent modules (config, paths, vcd).
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
#include "paths.h"
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
