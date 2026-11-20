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
 * Each key is followed by its value(s) as separate strings. Avoid '=' and
 * ':' in netlists: ngspice numparam evaluates "key=value" inside sim_args.
 * The compact forms "key=value" / "key:value" are still accepted (runner).
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
 *   vcd     VCD file of the GPIO pins, .vcd, for GTKWave / PulseView
 *           (output files are never written through a symbolic link, and an
 *           existing file is only replaced if mcu_cosim wrote it)
 *   log   1 to print pin activity on stderr (default 0)
 *
 * UART0: every byte sent by the firmware is printed on stderr, one line at
 * a time ("UART0> ..."). simavr does not drive the TXD pin, so when the TX
 * pin (PD1) is listed in "out", the plug-in rebuilds the serial frames on
 * it at the baud rate programmed in UBRR0 (start, data LSB first, stop).
 *
 * Analog inputs (SAR model): d_cosim only carries digital values, so the
 * successive-approximation converter is rebuilt across the boundary:
 *   - the plug-in drives a 10-bit DAC code (10 extra outputs, LSB first),
 *     turned into Vdac = vref * code / 1024 by a behavioural source;
 *   - one comparator per channel returns (Vin > Vdac) as an extra input;
 *   - the plug-in runs a continuous SAR loop, channel after channel, and
 *     hands each result to the simavr ADC as millivolts.
 * Port order:
 *   inputs  = "in" pins, then "io" pin reads, then one comparator per "adc"
 *   outputs = "out" pins, then (level, oe, pu) per "io" pin, then b0..b9
 *
 * Timing model: the MCU runs cycle by cycle until it reaches the SPICE
 * target time (vtime). When an output pin changes, execution pauses and
 * vtime is lowered to the cycle time of the change, as required by d_cosim.
 */

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <initializer_list>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <sim_avr.h>
#include <sim_hex.h>
#include <avr_ioport.h>
#include <avr_adc.h>
#include <avr_uart.h>

#include "cosim_abi.h"

#define COSIM_EXPORT extern "C" __attribute__((visibility("default")))

namespace
{

constexpr const char *kTag = "[mcu_cosim]";

/** One GPIO pin, e.g. PB5 -> {'B', 5}. */
struct PinRef
{
    char    port;
    uint8_t bit;
};

class McuBridge;

/** Mirror of the PORTx / DDRx registers of one I/O port. */
struct PortWatch
{
    McuBridge  *owner = nullptr;
    char        name  = 0;
    uint8_t     port  = 0;
    uint8_t     ddr   = 0;
    uint8_t     extMask  = 0;    // Pins whose level comes from the circuit.
    uint8_t     extValue = 0;
    avr_irq_t  *pinIrq[8] = {};
};

/** Configuration parsed from sim_args. */
struct Config
{
    std::string         hexPath;
    std::string         mcuName = "atmega328p";
    uint32_t            frequency = 16000000u;
    std::vector<PinRef> inputs;
    std::vector<PinRef> outputs;
    std::vector<PinRef> ios;
    std::vector<int>    adcChannels;
    double              vref   = 5.0;
    double              adcBit = 1e-6;
    std::string         txPin   = "PD1";
    std::string         uartLog;
    std::string         vcdPath;
    bool                log = false;
};

constexpr int kDacBits = 10;

/* ------------------------------------------------------------------------ */
/* Parsing helpers                                                          */
/* ------------------------------------------------------------------------ */

bool parsePin(const std::string &text, PinRef &pin)
{
    // Accept "PB5" or "B5", case insensitive.
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

std::string toLower(std::string s)
{
    for (char &c : s)
    {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

/**
 * KiCad's simulator lower-cases the strings of the netlist, sim_args
 * included: "/home/me/Arduino/Blink.hex" arrives as ".../arduino/blink.hex".
 * Rebuild the real path, one component at a time, ignoring case.
 */
std::string resolvePath(const std::string &path)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    if (path.empty() || fs::exists(path, ec))
    {
        return path;
    }

    const fs::path in(path);
    fs::path out = in.root_path();
    for (const fs::path &part : in.relative_path())
    {
        fs::path candidate = out / part;
        if (!fs::exists(candidate, ec))
        {
            const std::string wanted = toLower(part.string());
            const fs::path dir = out.empty() ? fs::path(".") : out;
            bool found = false;
            for (const fs::directory_entry &entry : fs::directory_iterator(dir, ec))
            {
                if (toLower(entry.path().filename().string()) == wanted)
                {
                    candidate = out / entry.path().filename();
                    found = true;
                    break;
                }
            }
            if (!found)
            {
                return path;    // Really missing: report the original name.
            }
        }
        out = candidate;
    }
    return out.string();
}

/** Same for a file to create: resolve its folder, keep its name. */
std::string resolveOutputPath(const std::string &path)
{
    namespace fs = std::filesystem;
    const fs::path in(path);
    if (!in.has_parent_path())
    {
        return path;
    }
    return (fs::path(resolvePath(in.parent_path().string())) / in.filename()).string();
}

/* ------------------------------------------------------------------------ */
/* Output files (UART log, VCD): guarded against overwriting other files    */
/* ------------------------------------------------------------------------ */

/**
 * The paths of "uartlog" and "vcd" come from the netlist. A typo (or a
 * netlist from someone else) must not truncate an unrelated file, so an
 * output file is only written when:
 *   - its extension is one of the expected ones (.txt/.log, .vcd);
 *   - it is not a symbolic link and, if it exists, is a regular file;
 *   - if it exists and is not empty, its content looks like a file this
 *     plug-in wrote (see the "looksLike" checks).
 */
using ContentCheck = bool (*)(const std::string &head);

bool looksLikeVcd(const std::string &head)
{
    return head.rfind("$timescale", 0) == 0;
}

/** UART log: printable ASCII lines of at most 200 characters. */
bool looksLikeUartLog(const std::string &head)
{
    size_t lineLength = 0;
    for (const char c : head)
    {
        if (c == '\n')
        {
            lineLength = 0;
            continue;
        }
        if (c < 0x20 || c > 0x7E || ++lineLength > 200u)
        {
            return false;
        }
    }
    return true;
}

bool hasExtension(const std::string &path, std::initializer_list<const char *> extensions)
{
    const std::string ext = toLower(std::filesystem::path(path).extension().string());
    return std::any_of(extensions.begin(), extensions.end(),
                       [&ext](const char *e) { return ext == e; });
}

/** Open (and truncate) an output file, or explain why not and return null. */
std::FILE *openOutputFile(const std::string &path, const char *what,
                          std::initializer_list<const char *> extensions, ContentCheck check)
{
    auto refuse = [&](const char *reason) {
        std::fprintf(stderr, "%s not writing %s '%s': %s\n", kTag, what, path.c_str(), reason);
        return static_cast<std::FILE *>(nullptr);
    };

    if (!hasExtension(path, extensions))
    {
        return refuse("unexpected file extension");
    }

    // O_NOFOLLOW: no symbolic link; O_NONBLOCK: never hang on a FIFO.
    const int fd = ::open(path.c_str(), O_RDWR | O_CREAT | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC,
                          0644);
    if (fd < 0)
    {
        return refuse(errno == ELOOP ? "is a symbolic link" : std::strerror(errno));
    }

    struct stat st{};
    if (::fstat(fd, &st) != 0 || !S_ISREG(st.st_mode))
    {
        ::close(fd);
        return refuse("not a regular file");
    }

    if (st.st_size > 0)
    {
        std::string head(4096, '\0');
        const ssize_t n = ::read(fd, &head[0], head.size());
        head.resize(n > 0 ? static_cast<size_t>(n) : 0u);
        if (!check(head))
        {
            ::close(fd);
            return refuse("existing file was not written by mcu_cosim (delete it to reuse the name)");
        }
    }

    if (::ftruncate(fd, 0) != 0 || ::lseek(fd, 0, SEEK_SET) != 0 ||
        ::fcntl(fd, F_SETFL, ::fcntl(fd, F_GETFL) & ~O_NONBLOCK) != 0)
    {
        ::close(fd);
        return refuse(std::strerror(errno));
    }

    std::FILE *file = ::fdopen(fd, "w");
    if (file == nullptr)
    {
        ::close(fd);
        return refuse(std::strerror(errno));
    }
    return file;
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

/** Store one value for a key. "in"/"out" may receive several values. */
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

bool parseConfig(const co_info *info, Config &cfg)
{
    std::string currentKey;    // Key waiting for its value(s).

    for (unsigned int i = 0; i < info->sim_argc; ++i)
    {
        const std::string arg = info->sim_argv[i] ? info->sim_argv[i] : "";
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
        std::fprintf(stderr, "%s missing firmware: add \"hex\" \"file.hex\" to sim_args\n", kTag);
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

/** Number of d_cosim ports implied by a configuration. */
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

/* ------------------------------------------------------------------------ */
/* simavr callbacks                                                         */
/* ------------------------------------------------------------------------ */

void noSleep(avr_t * /*avr*/, avr_cycle_count_t /*howLong*/)
{
    // simavr sleeps in real time by default: SPICE drives time here.
}

void onPortWrite(avr_irq_t * /*irq*/, uint32_t value, void *param);
void onDdrWrite(avr_irq_t * /*irq*/, uint32_t value, void *param);
avr_cycle_count_t onSarTimer(avr_t *avr, avr_cycle_count_t when, void *param);
avr_cycle_count_t onTxBitTimer(avr_t *avr, avr_cycle_count_t when, void *param);
void onUartByte(avr_irq_t * /*irq*/, uint32_t value, void *param);

/* ------------------------------------------------------------------------ */
/* Bridge between one simavr instance and one d_cosim instance              */
/* ------------------------------------------------------------------------ */

class McuBridge
{
public:
    explicit McuBridge(Config cfg)
        : m_cfg(std::move(cfg))
    {
    }

    ~McuBridge()
    {
        flushUartLine();
        if (m_uartFile != nullptr)
        {
            std::fclose(m_uartFile);
        }
        if (m_vcd != nullptr)
        {
            std::fclose(m_vcd);
        }
        if (m_avr != nullptr)
        {
            avr_terminate(m_avr);
            std::free(m_avr);
        }
    }

    McuBridge(const McuBridge &) = delete;
    McuBridge &operator=(const McuBridge &) = delete;

    bool start()
    {
        m_avr = avr_make_mcu_by_name(m_cfg.mcuName.c_str());
        if (m_avr == nullptr)
        {
            std::fprintf(stderr, "%s unknown MCU '%s'\n", kTag, m_cfg.mcuName.c_str());
            return false;
        }

        avr_init(m_avr);
        m_avr->frequency = m_cfg.frequency;
        m_avr->sleep     = noSleep;

        const std::string hexPath = resolvePath(m_cfg.hexPath);
        if (hexPath != m_cfg.hexPath)
        {
            std::fprintf(stderr, "%s firmware path case restored: %s\n", kTag, hexPath.c_str());
            m_cfg.hexPath = hexPath;
        }
        m_cfg.uartLog = resolveOutputPath(m_cfg.uartLog);
        m_cfg.vcdPath = resolveOutputPath(m_cfg.vcdPath);

        if (!loadHex())
        {
            return false;
        }

        // Register every port used by the configuration.
        for (const PinRef &pin : m_cfg.inputs)
        {
            if (!watchPort(pin.port))
            {
                return false;
            }
        }
        for (const PinRef &pin : m_cfg.outputs)
        {
            if (!watchPort(pin.port))
            {
                return false;
            }
        }
        for (const PinRef &pin : m_cfg.ios)
        {
            if (!watchPort(pin.port))
            {
                return false;
            }
        }

        if (!m_cfg.adcChannels.empty() && !startAdc())
        {
            return false;
        }
        if (!startUart() || !openVcd())
        {
            return false;
        }

        m_lastOut.assign(outputPortCount(m_cfg), Digital_t{ ZERO, STRONG });
        // No publication at t = 0: d_cosim starts every output at ZERO and
        // a change reported at vtime 0 would arrive "in the past".
        // Only real changes, from the first instructions on, are sent.
        m_dirty = false;

        std::fprintf(stderr, "%s %s @ %u Hz running %s (%zu in, %zu out, %zu io, %zu ADC)\n",
                     kTag, m_cfg.mcuName.c_str(), m_cfg.frequency, m_cfg.hexPath.c_str(),
                     m_cfg.inputs.size(), m_cfg.outputs.size(), m_cfg.ios.size(),
                     m_cfg.adcChannels.size());
        return true;
    }

    void markDirty() { m_dirty = true; }

    /** A byte written to UDR0 by the firmware (UART TX). */
    void uartByte(uint8_t byte)
    {
        // Console text, one line at a time.
        if (byte == '\n')
        {
            flushUartLine();
        }
        else if (byte != '\r')
        {
            if (m_uartLine.empty())
            {
                m_uartLineTime = now();
            }
            m_uartLine.push_back((byte >= 0x20 && byte < 0x7F) ? static_cast<char>(byte) : '.');
            if (m_uartLine.size() >= 200u)
            {
                flushUartLine();
            }
        }

        if (m_txOut >= 0 || m_txIo >= 0)
        {
            queueTxFrame(byte);
        }
    }

    /** TX bit timer: put the next frame bit on the TX pin. */
    avr_cycle_count_t txBit(avr_cycle_count_t when)
    {
        if (m_txBits.empty())
        {
            m_txBusy = false;
            return 0;    // Line stays idle (high).
        }
        m_txLine = m_txBits.front();
        m_txBits.pop_front();
        m_dirty = true;
        return when + m_txBitCycles;
    }

    /**
     * One SAR step, called from a simavr cycle timer every adcbit seconds.
     * Each call keeps or drops the bit under test according to the
     * comparator of the current channel, then tries the next bit.
     */
    void sarTick()
    {
        const size_t ch = m_sarChannel;

        // ADC off (ADEN = 0): no conversion, no DAC activity, no SPICE events.
        if (m_adc != nullptr && !avr_regbit_get(m_avr, m_adc->aden))
        {
            m_sarIdle = true;
            return;
        }

        if (m_sarIdle)
        {
            // First tick: start the first conversion from code 0.
            m_sarIdle = false;
            m_sarBit  = kDacBits - 1;
            m_dacCode = 1u << m_sarBit;
            m_dirty   = true;
            return;
        }

        if (m_sarBit >= 0)
        {
            if (!m_cmp[ch])
            {
                m_dacCode &= ~(1u << m_sarBit);    // Vin < Vdac: drop the bit.
            }
            --m_sarBit;
        }

        if (m_sarBit >= 0)
        {
            m_dacCode |= (1u << m_sarBit);         // Try the next bit.
        }
        else
        {
            deliverConversion(ch, m_dacCode);
            m_sarChannel = (ch + 1) % m_cfg.adcChannels.size();
            m_sarBit  = kDacBits - 1;              // Start the next conversion.
            m_dacCode = 1u << m_sarBit;
        }
        m_dirty = true;
    }

    /** d_cosim step(): run until vtime, or until the first output change. */
    void step(co_info *info)
    {
        const double target = info->vtime;

        // Changes left over from a previous call: keep vtime unchanged.
        if (m_dirty && publishOutputs(info))
        {
            return;
        }

        const auto targetCycle =
            static_cast<avr_cycle_count_t>(std::floor(target * m_cfg.frequency));

        while (m_running && m_avr->cycle < targetCycle)
        {
            const int state = avr_run(m_avr);
            if (state == cpu_Done || state == cpu_Crashed)
            {
                std::fprintf(stderr, "%s MCU stopped (%s) at t=%.9f s\n", kTag,
                             state == cpu_Done ? "done" : "crashed", now());
                m_running = false;
            }

            if (m_dirty && publishOutputs(info))
            {
                info->vtime = std::min(now(), target);
                return;
            }
        }

        // CPU halted (e.g. main() returned: "cli; rjmp ."): peripherals keep
        // running, so the UART can finish its frames and the SAR keeps going.
        while (!m_running && m_avr->cycle < targetCycle)
        {
            const avr_cycle_count_t next = avr_cycle_timer_process(m_avr);
            m_avr->cycle += std::max<avr_cycle_count_t>(
                1, std::min<avr_cycle_count_t>(next, targetCycle - m_avr->cycle));

            if (m_dirty && publishOutputs(info))
            {
                info->vtime = std::min(now(), target);
                return;
            }
        }
    }

    /** d_cosim in_fn(): an input node changed. */
    void input(unsigned int index, const Digital_t *value)
    {
        const size_t pins = m_cfg.inputs.size() + m_cfg.ios.size();
        if (index >= pins)
        {
            // Comparator of an ADC channel: true when Vin > Vdac.
            const size_t ch = index - pins;
            if (ch < m_cmp.size())
            {
                m_cmp[ch] = (value->state == ONE);
            }
            return;
        }

        // "in" pin, or voltage read back on an "io" pin.
        const PinRef &pin = (index < m_cfg.inputs.size())
                                ? m_cfg.inputs[index]
                                : m_cfg.ios[index - m_cfg.inputs.size()];
        const uint32_t level = (value->state == ONE) ? 1u : 0u;
        PortWatch &watch = m_ports.at(pin.port);

        // Make the circuit voltage the "external" level of this pin, so that
        // simavr's own pull-up emulation never overrides it (the pull-up is
        // modelled electrically in SPICE).
        const uint8_t mask = static_cast<uint8_t>(1u << pin.bit);
        watch.extMask  = static_cast<uint8_t>(watch.extMask | mask);
        watch.extValue = static_cast<uint8_t>(level ? (watch.extValue | mask)
                                                    : (watch.extValue & ~mask));
        avr_ioport_external_t ext{};
        ext.name  = static_cast<unsigned long>(pin.port);
        ext.mask  = watch.extMask;
        ext.value = watch.extValue;
        avr_ioctl(m_avr, AVR_IOCTL_IOPORT_SET_EXTERNAL(pin.port), &ext);

        avr_raise_irq(watch.pinIrq[pin.bit], level);
        vcdWrite(index, *value);

        if (m_cfg.log)
        {
            std::fprintf(stderr, "%s t=%.9f in  P%c%u = %u\n", kTag, now(),
                         pin.port, pin.bit, level);
        }
    }

private:
    double now() const
    {
        return static_cast<double>(m_avr->cycle) / m_cfg.frequency;
    }

    bool loadHex()
    {
        // read_ihex_file() does not handle a missing file gracefully.
        std::FILE *probe = std::fopen(m_cfg.hexPath.c_str(), "r");
        if (probe == nullptr)
        {
            std::fprintf(stderr, "%s cannot open firmware '%s'\n", kTag, m_cfg.hexPath.c_str());
            return false;
        }
        std::fclose(probe);

        uint32_t size  = 0;
        uint32_t base  = 0;
        uint8_t *image = read_ihex_file(m_cfg.hexPath.c_str(), &size, &base);
        if (image == nullptr || size == 0u)
        {
            std::fprintf(stderr, "%s invalid HEX file '%s'\n", kTag, m_cfg.hexPath.c_str());
            std::free(image);
            return false;
        }

        // avr_loadcode() aborts on overflow: check first.
        if (static_cast<uint64_t>(base) + size > static_cast<uint64_t>(m_avr->flashend) + 1u)
        {
            std::fprintf(stderr, "%s firmware (%u bytes at 0x%X) exceeds flash\n",
                         kTag, size, base);
            std::free(image);
            return false;
        }

        avr_loadcode(m_avr, image, size, base);
        std::free(image);
        return true;
    }

    bool watchPort(char name)
    {
        if (m_ports.count(name) != 0u)
        {
            return true;
        }

        const uint32_t ioctl = AVR_IOCTL_IOPORT_GETIRQ(name);
        avr_irq_t *portIrq = avr_io_getirq(m_avr, ioctl, IOPORT_IRQ_REG_PORT);
        avr_irq_t *ddrIrq  = avr_io_getirq(m_avr, ioctl, IOPORT_IRQ_DIRECTION_ALL);
        if (portIrq == nullptr || ddrIrq == nullptr)
        {
            std::fprintf(stderr, "%s port %c does not exist on %s\n", kTag, name,
                         m_cfg.mcuName.c_str());
            return false;
        }

        PortWatch &watch = m_ports[name];    // std::map: stable address.
        watch.owner = this;
        watch.name  = name;
        for (int bit = 0; bit < 8; ++bit)
        {
            watch.pinIrq[bit] = avr_io_getirq(m_avr, ioctl, IOPORT_IRQ_PIN0 + bit);
        }

        avr_irq_register_notify(portIrq, onPortWrite, &watch);
        avr_irq_register_notify(ddrIrq, onDdrWrite, &watch);
        return true;
    }

    /** Electrical state of an output pin from its PORT/DDR bits. */
    Digital_t pinValue(const PinRef &pin) const
    {
        const PortWatch &watch = m_ports.at(pin.port);
        const bool isOutput = ((watch.ddr  >> pin.bit) & 1u) != 0u;
        const bool portBit  = ((watch.port >> pin.bit) & 1u) != 0u;

        if (isOutput)
        {
            return Digital_t{ portBit ? ONE : ZERO, STRONG };
        }
        if (portBit)
        {
            return Digital_t{ ONE, RESISTIVE };    // Internal pull-up (PUD ignored).
        }
        return Digital_t{ UNKNOWN, HI_IMPEDANCE };  // Floating input.
    }

    /** Prepare the SAR model: comparators, reference and step timer. */
    bool startAdc()
    {
        m_sarCycles = static_cast<avr_cycle_count_t>(std::llround(m_cfg.adcBit * m_cfg.frequency));
        if (m_sarCycles == 0u)
        {
            std::fprintf(stderr, "%s adcbit shorter than one CPU cycle\n", kTag);
            return false;
        }

        for (int ch : m_cfg.adcChannels)
        {
            avr_irq_t *irq = avr_io_getirq(m_avr, AVR_IOCTL_ADC_GETIRQ, ADC_IRQ_ADC0 + ch);
            if (irq == nullptr)
            {
                std::fprintf(stderr, "%s no ADC channel %d on %s\n", kTag, ch,
                             m_cfg.mcuName.c_str());
                return false;
            }
            m_adcIrq.push_back(irq);
        }
        m_cmp.assign(m_cfg.adcChannels.size(), false);

        for (avr_io_t *io = m_avr->io_port; io != nullptr; io = io->next)
        {
            if (io->kind != nullptr && std::strcmp(io->kind, "adc") == 0)
            {
                m_adc = reinterpret_cast<avr_adc_t *>(io);    // For the ADEN bit.
            }
        }

        // AVCC, VCC and AREF all equal to the DAC full scale.
        m_vrefMv = static_cast<uint32_t>(std::lround(m_cfg.vref * 1000.0));
        m_avr->vcc  = m_vrefMv;
        m_avr->avcc = m_vrefMv;
        m_avr->aref = m_vrefMv;

        m_sarChannel = 0;
        m_sarIdle    = true;    // DAC stays at 0, as d_cosim assumes at t = 0.
        m_dacCode    = 0;
        avr_cycle_timer_register(m_avr, m_sarCycles, onSarTimer, this);
        return true;
    }

public:
    avr_cycle_count_t sarCycles() const { return m_sarCycles; }

private:
    /**
     * Hand a finished conversion to simavr, in millivolts. The value is
     * rounded up so that simavr's own scaling (mV * 1023 / Vref) gives
     * back exactly this code when the AVCC reference is selected.
     */
    void deliverConversion(size_t ch, uint32_t code)
    {
        const uint32_t mv = static_cast<uint32_t>(
            (static_cast<uint64_t>(code) * m_vrefMv + 1022u) / 1023u);
        avr_raise_irq(m_adcIrq[ch], mv);

        if (m_cfg.log)
        {
            std::fprintf(stderr, "%s t=%.9f adc ADC%d = %u (%u mV)\n", kTag, now(),
                         m_cfg.adcChannels[ch], code, mv);
        }
    }

    /** Hook UART0: console text, optional log file, TX frame synthesis. */
    bool startUart()
    {
        for (avr_io_t *io = m_avr->io_port; io != nullptr; io = io->next)
        {
            if (io->kind != nullptr && std::strcmp(io->kind, "uart") == 0)
            {
                auto *uart = reinterpret_cast<avr_uart_t *>(io);
                if (uart->name == '0')
                {
                    m_uart = uart;
                    break;
                }
            }
        }
        if (m_uart == nullptr)
        {
            return true;    // MCU without UART0: nothing to do.
        }

        // No echo on simavr's stdout, no real-time sleep while polling UDRE.
        uint32_t flags = 0;
        avr_ioctl(m_avr, AVR_IOCTL_UART_SET_FLAGS('0'), &flags);
        avr_irq_register_notify(avr_io_getirq(m_avr, AVR_IOCTL_UART_GETIRQ('0'), UART_IRQ_OUTPUT),
                                onUartByte, this);

        if (!m_cfg.uartLog.empty())
        {
            m_uartFile = openOutputFile(m_cfg.uartLog, "UART log", { ".txt", ".log" },
                                        looksLikeUartLog);
        }

        PinRef tx{};
        if (parsePin(m_cfg.txPin, tx))
        {
            for (size_t i = 0; i < m_cfg.outputs.size(); ++i)
            {
                if (m_cfg.outputs[i].port == tx.port && m_cfg.outputs[i].bit == tx.bit)
                {
                    m_txOut = static_cast<int>(i);
                }
            }
            for (size_t i = 0; i < m_cfg.ios.size(); ++i)
            {
                if (m_cfg.ios[i].port == tx.port && m_cfg.ios[i].bit == tx.bit)
                {
                    m_txIo = static_cast<int>(i);
                }
            }
        }
        return true;
    }

    void flushUartLine()
    {
        if (m_uartLine.empty())
        {
            return;
        }
        std::fprintf(stderr, "%s t=%.6f s UART0> %s\n", kTag, m_uartLineTime, m_uartLine.c_str());
        if (m_uartFile != nullptr)
        {
            std::fprintf(m_uartFile, "%s\n", m_uartLine.c_str());
            std::fflush(m_uartFile);
        }
        m_uartLine.clear();
    }

    /** Append one serial frame (start, data LSB first, stops) to the TX line. */
    void queueTxFrame(uint8_t byte)
    {
        static const int kDataBits[] = { 5, 6, 7, 8, 8, 8, 8, 9 };

        const uint32_t ubrr = avr_regbit_get(m_avr, m_uart->ubrrl) |
                              (static_cast<uint32_t>(avr_regbit_get(m_avr, m_uart->ubrrh)) << 8);
        avr_cycle_count_t cyclesPerBit = (ubrr + 1u) * 8u;
        if (!avr_regbit_get(m_avr, m_uart->u2x))
        {
            cyclesPerBit *= 2u;
        }
        const int dataBits = kDataBits[avr_regbit_get(m_avr, m_uart->ucsz) |
                                       (avr_regbit_get(m_avr, m_uart->ucsz2) << 2)];
        const int stopBits = 1 + avr_regbit_get(m_avr, m_uart->usbs);
        m_txBitCycles = cyclesPerBit;

        if (!m_txEnabled)
        {
            m_txEnabled = true;    // TXEN: the pin becomes the idle-high line.
            m_txLine    = true;
            m_dirty     = true;
        }

        m_txBits.push_back(false);                               // Start bit.
        for (int k = 0; k < dataBits; ++k)
        {
            m_txBits.push_back(((byte >> k) & 1u) != 0u);       // LSB first.
        }
        for (int k = 0; k < stopBits; ++k)
        {
            m_txBits.push_back(true);                            // Stop bit(s).
        }

        if (!m_txBusy)
        {
            // One idle bit before the first start bit, then one bit per tick.
            m_txBusy = true;
            avr_cycle_timer_register(m_avr, m_txBitCycles, onTxBitTimer, this);
        }
    }

    /** VCD file: "in", "io" (voltage read back), then "out" pins, 1 ns. */
    bool openVcd()
    {
        if (m_cfg.vcdPath.empty())
        {
            return true;
        }
        m_vcd = openOutputFile(m_cfg.vcdPath, "VCD", { ".vcd" }, looksLikeVcd);
        if (m_vcd == nullptr)
        {
            return true;    // Not fatal.
        }

        const size_t count = m_cfg.inputs.size() + m_cfg.ios.size() + m_cfg.outputs.size();
        if (count > 90u)
        {
            std::fprintf(stderr, "%s too many pins for the VCD trace\n", kTag);
            std::fclose(m_vcd);
            m_vcd = nullptr;
            return true;
        }

        std::fprintf(m_vcd, "$timescale 1ns $end\n$scope module %s $end\n",
                     m_cfg.mcuName.c_str());
        for (size_t k = 0; k < count; ++k)
        {
            const size_t nIn = m_cfg.inputs.size();
            const size_t nIo = m_cfg.ios.size();
            const PinRef &pin = (k < nIn)       ? m_cfg.inputs[k]
                                : (k < nIn + nIo) ? m_cfg.ios[k - nIn]
                                                  : m_cfg.outputs[k - nIn - nIo];
            std::fprintf(m_vcd, "$var wire 1 %c P%c%u $end\n",
                         static_cast<char>('!' + k), pin.port, pin.bit);
        }
        std::fprintf(m_vcd, "$upscope $end\n$enddefinitions $end\n#0\n$dumpvars\n");
        for (size_t k = 0; k < count; ++k)
        {
            // d_cosim starts every node at 0.
            std::fprintf(m_vcd, "0%c\n", static_cast<char>('!' + k));
        }
        std::fprintf(m_vcd, "$end\n");
        m_vcdLastCycle = 0;
        return true;
    }

    void vcdWrite(size_t signal, const Digital_t &value)
    {
        if (m_vcd == nullptr)
        {
            return;
        }
        const avr_cycle_count_t cycle = m_avr->cycle;
        if (cycle != m_vcdLastCycle)
        {
            const double ns = static_cast<double>(cycle) * 1e9 / m_cfg.frequency;
            std::fprintf(m_vcd, "#%llu\n", static_cast<unsigned long long>(std::llround(ns)));
            m_vcdLastCycle = cycle;
        }
        char level = 'x';
        if (value.strength == HI_IMPEDANCE)
        {
            level = 'z';
        }
        else if (value.state == ONE)
        {
            level = '1';
        }
        else if (value.state == ZERO)
        {
            level = '0';
        }
        std::fprintf(m_vcd, "%c%c\n", level, static_cast<char>('!' + signal));
    }

    /** Value of output port i: GPIO pins first, then DAC bits b0..b9. */
    Digital_t outputValue(size_t i) const
    {
        if (static_cast<int>(i) == m_txOut && m_txEnabled)
        {
            return Digital_t{ m_txLine ? ONE : ZERO, STRONG };    // UART owns TXD.
        }
        if (i < m_cfg.outputs.size())
        {
            return pinValue(m_cfg.outputs[i]);
        }

        const size_t ioBase = m_cfg.outputs.size();
        if (i < ioBase + 3u * m_cfg.ios.size())
        {
            const size_t k    = (i - ioBase) / 3u;
            const size_t kind = (i - ioBase) % 3u;    // 0 level, 1 oe, 2 pu
            const PinRef &pin = m_cfg.ios[k];
            const PortWatch &watch = m_ports.at(pin.port);

            bool level = ((watch.port >> pin.bit) & 1u) != 0u;
            bool oe    = ((watch.ddr  >> pin.bit) & 1u) != 0u;
            bool pu    = !oe && level;                // PORT = 1 on an input.
            if (static_cast<int>(k) == m_txIo && m_txEnabled)
            {
                level = m_txLine;                     // UART owns TXD.
                oe    = true;
                pu    = false;
            }
            const bool bit = (kind == 0u) ? level : ((kind == 1u) ? oe : pu);
            return Digital_t{ bit ? ONE : ZERO, STRONG };
        }

        const unsigned int bit =
            static_cast<unsigned int>(i - ioBase - 3u * m_cfg.ios.size());
        return Digital_t{ ((m_dacCode >> bit) & 1u) ? ONE : ZERO, STRONG };
    }

    /** Send changed outputs to ngspice. Returns true if any changed. */
    bool publishOutputs(co_info *info)
    {
        m_dirty = false;
        bool changed = false;

        for (size_t i = 0; i < m_lastOut.size(); ++i)
        {
            Digital_t value = outputValue(i);
            if (value.state != m_lastOut[i].state || value.strength != m_lastOut[i].strength)
            {
                m_lastOut[i] = value;
                if (i < m_cfg.outputs.size())
                {
                    vcdWrite(m_cfg.inputs.size() + m_cfg.ios.size() + i, value);
                }
                info->out_fn(info, static_cast<unsigned int>(i), &value);
                changed = true;

                if (m_cfg.log && i < m_cfg.outputs.size())
                {
                    static const char *const kStates[] = { "0", "1", "X" };
                    std::fprintf(stderr, "%s t=%.9f out P%c%u = %s\n", kTag, now(),
                                 m_cfg.outputs[i].port, m_cfg.outputs[i].bit,
                                 kStates[value.state]);
                }
            }
        }
        return changed;
    }

    Config                     m_cfg;
    avr_t                     *m_avr     = nullptr;
    bool                       m_running = true;
    bool                       m_dirty   = false;
    std::map<char, PortWatch>  m_ports;
    std::vector<Digital_t>     m_lastOut;

    // SAR analog model
    std::vector<avr_irq_t *>   m_adcIrq;
    std::vector<bool>          m_cmp;
    avr_cycle_count_t          m_sarCycles  = 0;
    uint32_t                   m_vrefMv     = 5000;
    size_t                     m_sarChannel = 0;
    int                        m_sarBit     = kDacBits - 1;
    bool                       m_sarIdle    = true;
    uint32_t                   m_dacCode    = 0;

    // UART0
    avr_uart_t                *m_uart        = nullptr;
    std::string                m_uartLine;
    double                     m_uartLineTime = 0.0;
    std::FILE                 *m_uartFile    = nullptr;
    int                        m_txOut       = -1;      // "out" index of TXD.
    int                        m_txIo        = -1;      // "io" index of TXD.
    avr_adc_t                 *m_adc         = nullptr;
    bool                       m_txEnabled   = false;
    bool                       m_txLine      = true;
    bool                       m_txBusy      = false;
    avr_cycle_count_t          m_txBitCycles = 0;
    std::deque<bool>           m_txBits;

    // VCD trace
    std::FILE                 *m_vcd         = nullptr;
    avr_cycle_count_t          m_vcdLastCycle = ~avr_cycle_count_t{ 0 };
};

avr_cycle_count_t onTxBitTimer(avr_t * /*avr*/, avr_cycle_count_t when, void *param)
{
    return static_cast<McuBridge *>(param)->txBit(when);
}

void onUartByte(avr_irq_t * /*irq*/, uint32_t value, void *param)
{
    static_cast<McuBridge *>(param)->uartByte(static_cast<uint8_t>(value));
}

avr_cycle_count_t onSarTimer(avr_t * /*avr*/, avr_cycle_count_t when, void *param)
{
    auto *bridge = static_cast<McuBridge *>(param);
    bridge->sarTick();
    return when + bridge->sarCycles();    // Periodic timer.
}

void onPortWrite(avr_irq_t * /*irq*/, uint32_t value, void *param)
{
    auto *watch = static_cast<PortWatch *>(param);
    watch->port = static_cast<uint8_t>(value);
    watch->owner->markDirty();
}

void onDdrWrite(avr_irq_t * /*irq*/, uint32_t value, void *param)
{
    auto *watch = static_cast<PortWatch *>(param);
    watch->ddr = static_cast<uint8_t>(value);
    watch->owner->markDirty();
}

/* ------------------------------------------------------------------------ */
/* d_cosim entry points                                                     */
/* ------------------------------------------------------------------------ */

McuBridge *bridgeOf(co_info *info)
{
    return static_cast<McuBridge *>(info->handle);
}

void cosimStep(co_info *info)
{
    bridgeOf(info)->step(info);
}

void cosimInput(co_info *info, unsigned int index, Digital_t *value)
{
    bridgeOf(info)->input(index, value);
}

void cosimCleanup(co_info *info)
{
    delete bridgeOf(info);
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
    Config cfg;
    const bool parsed = parseConfig(info, cfg);

    info->in_count    = inputPortCount(cfg);
    info->out_count   = outputPortCount(cfg);
    info->inout_count = 0;
    info->method      = Normal;

    auto bridge = std::make_unique<McuBridge>(std::move(cfg));
    if (!parsed || !bridge->start())
    {
        std::fprintf(stderr, "%s setup failed: MCU outputs stay at their initial value\n", kTag);
        info->handle  = nullptr;
        info->step    = idleStep;
        info->in_fn   = idleInput;
        info->cleanup = nullptr;
        return;
    }

    info->handle  = bridge.release();
    info->step    = cosimStep;
    info->in_fn   = cosimInput;
    info->cleanup = cosimCleanup;
}
