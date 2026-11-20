/*
 * kisimavr - ATmega co-simulation for ngspice / KiCad
 * Copyright (C) 2026 Paul
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file rp2040_device.cpp
 * @brief RP2040 / RP2350 (Raspberry Pi Pico / Pico 2) co-simulated with
 *        rp2040js / rp2350js in QuickJS.
 */

#include "rp2040_device.h"

#include <dlfcn.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <sstream>
#include <utility>

#include <quickjs.h>

#include "common.h"
#include "paths.h"

namespace kisimavr
{

struct Rp2040Device::Js
{
    JSRuntime *rt = nullptr;
    JSContext *ctx = nullptr;
    JSValue    bridge = JS_UNDEFINED;
};

namespace
{

bool readFile(const std::string &path, std::string &out)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        return false;
    }
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

/** Folder of this plug-in (mcu_cosim.so). */
std::string pluginDir()
{
    Dl_info info{};
    if (dladdr(reinterpret_cast<void *>(&pluginDir), &info) != 0 && info.dli_fname != nullptr)
    {
        std::string path = info.dli_fname;
        const size_t slash = path.rfind('/');
        if (slash != std::string::npos)
        {
            return path.substr(0, slash);
        }
    }
    return ".";
}

/** ES module loader: plain files, resolved by QuickJS from the importer. */
JSModuleDef *loadModule(JSContext *ctx, const char *name, void * /*opaque*/)
{
    std::string source;
    if (!readFile(name, source))
    {
        JS_ThrowReferenceError(ctx, "cannot load module '%s'", name);
        return nullptr;
    }
    JSValue func = JS_Eval(ctx, source.c_str(), source.size(), name,
                           JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
    if (JS_IsException(func))
    {
        return nullptr;
    }
    auto *module = static_cast<JSModuleDef *>(JS_VALUE_GET_PTR(func));
    JS_FreeValue(ctx, func);
    return module;
}

/** kisimPrint(text): message on stderr with the plug-in tag. */
JSValue jsPrint(JSContext *ctx, JSValueConst /*self*/, int argc, JSValueConst *argv)
{
    std::string text;
    for (int i = 0; i < argc; ++i)
    {
        const char *s = JS_ToCString(ctx, argv[i]);
        if (s != nullptr)
        {
            text += (i > 0 ? " " : "");
            text += s;
            JS_FreeCString(ctx, s);
        }
    }
    std::fprintf(stderr, "%s %s\n", kTag, text.c_str());
    return JS_UNDEFINED;
}

/** Print the pending JS exception (message and stack). */
void printException(JSContext *ctx, const char *chip, const char *what)
{
    JSValue exc = JS_GetException(ctx);
    const char *msg = JS_ToCString(ctx, exc);
    std::fprintf(stderr, "%s %s: %s: %s\n", kTag, chip, what, msg != nullptr ? msg : "?");
    JS_FreeCString(ctx, msg);
    JSValue stack = JS_GetPropertyStr(ctx, exc, "stack");
    if (!JS_IsUndefined(stack))
    {
        const char *s = JS_ToCString(ctx, stack);
        if (s != nullptr && *s != '\0')
        {
            std::fprintf(stderr, "%s\n", s);
        }
        JS_FreeCString(ctx, s);
    }
    JS_FreeValue(ctx, stack);
    JS_FreeValue(ctx, exc);
}

bool endsWith(const std::string &s, const char *suffix)
{
    const std::string lower = toLower(s);
    const std::string suf = suffix;
    return lower.size() >= suf.size() && lower.compare(lower.size() - suf.size(), suf.size(), suf) == 0;
}

} // namespace

Rp2040Device::Rp2040Device(Rp2040Config cfg) : m_cfg(std::move(cfg))
{
}

Rp2040Device::~Rp2040Device()
{
    if (m_ok)
    {
        printLines(true);
    }
    if (m_js != nullptr)
    {
        JS_FreeValue(m_js->ctx, m_js->bridge);
        JS_FreeContext(m_js->ctx);
        JS_FreeRuntime(m_js->rt);
        delete m_js;
    }
    if (m_log != nullptr)
    {
        std::fclose(m_log);
    }
}

const char *Rp2040Device::label() const
{
    return m_cfg.chip == "rp2350" ? "RP2350" : "RP2040";
}

void Rp2040Device::fail(const char *what)
{
    printException(m_js->ctx, label(), what);
    m_ok = false;
}

bool Rp2040Device::start()
{
    m_cfg.fwPath = resolvePath(m_cfg.fwPath);
    const bool rp2350 = m_cfg.chip == "rp2350";
    const char *defaultBridge = rp2350 ? "/../rp2040/rp2350_bridge.js" : "/../rp2040/bridge.js";
    std::string bridge = m_cfg.bridgePath.empty() ? pluginDir() + defaultBridge
                                                  : resolvePath(m_cfg.bridgePath);
    std::string firmware;
    if (!readFile(m_cfg.fwPath, firmware) || firmware.empty())
    {
        std::fprintf(stderr, "%s %s: cannot read firmware '%s'\n", kTag, label(),
                     m_cfg.fwPath.c_str());
        return false;
    }
    std::string bridgeSource;
    if (!readFile(bridge, bridgeSource))
    {
        std::fprintf(stderr, "%s %s: cannot read '%s' (key \"bridge\")\n", kTag, label(),
                     bridge.c_str());
        return false;
    }
    if (!m_cfg.uartLog.empty())
    {
        m_log = openOutputFile(resolveOutputPath(m_cfg.uartLog), "UART log", { ".txt", ".log" },
                               looksLikeUartLog);
    }

    m_js = new Js;
    m_js->rt = JS_NewRuntime();
    m_js->ctx = JS_NewContext(m_js->rt);
    JSContext *ctx = m_js->ctx;
    JS_SetModuleLoaderFunc(m_js->rt, nullptr, loadModule, nullptr);

    // kisimPrint() and a minimal console for the emulator.
    JSValue global = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, global, "kisimPrint", JS_NewCFunction(ctx, jsPrint, "kisimPrint", 1));
    JSValue console = JS_NewObject(ctx);
    for (const char *name : { "log", "warn", "error", "info", "debug" })
    {
        JS_SetPropertyStr(ctx, console, name, JS_NewCFunction(ctx, jsPrint, name, 1));
    }
    JS_SetPropertyStr(ctx, global, "console", console);

    // import { Rp2040Bridge } from bridge.js (Rp2350Bridge from rp2350_bridge.js)
    JSValue func = JS_Eval(ctx, bridgeSource.c_str(), bridgeSource.size(), bridge.c_str(),
                           JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
    if (JS_IsException(func))
    {
        JS_FreeValue(ctx, global);
        fail("cannot compile bridge.js");
        return false;
    }
    auto *module = static_cast<JSModuleDef *>(JS_VALUE_GET_PTR(func));
    JSValue done = JS_EvalFunction(ctx, func);    // Promise of the module evaluation.
    if (JS_IsException(done))
    {
        JS_FreeValue(ctx, global);
        fail("cannot load bridge.js");
        return false;
    }
    JSContext *jobCtx = nullptr;
    while (JS_ExecutePendingJob(m_js->rt, &jobCtx) > 0)
    {
    }
    if (JS_PromiseState(ctx, done) == JS_PROMISE_REJECTED)
    {
        JS_Throw(ctx, JS_PromiseResult(ctx, done));
        JS_FreeValue(ctx, done);
        JS_FreeValue(ctx, global);
        fail("cannot load bridge.js");
        return false;
    }
    JS_FreeValue(ctx, done);

    JSValue ns = JS_GetModuleNamespace(ctx, module);
    JSValue ctor = JS_GetPropertyStr(ctx, ns, rp2350 ? "Rp2350Bridge" : "Rp2040Bridge");
    JS_FreeValue(ctx, ns);
    JSValue ios = JS_NewArray(ctx);
    for (size_t i = 0; i < m_cfg.ios.size(); ++i)
    {
        JS_SetPropertyUint32(ctx, ios, static_cast<uint32_t>(i), JS_NewInt32(ctx, m_cfg.ios[i]));
    }
    JSValue adc = JS_NewArray(ctx);
    for (size_t i = 0; i < m_cfg.adc.size(); ++i)
    {
        JS_SetPropertyUint32(ctx, adc, static_cast<uint32_t>(i), JS_NewInt32(ctx, m_cfg.adc[i]));
    }
    JSValue arch = JS_NewString(ctx, m_cfg.arch.c_str());
    JSValue args[3] = { ios, adc, arch };
    m_js->bridge = JS_CallConstructor(ctx, ctor, 3, args);
    JS_FreeValue(ctx, ios);
    JS_FreeValue(ctx, adc);
    JS_FreeValue(ctx, arch);
    JS_FreeValue(ctx, ctor);
    JS_FreeValue(ctx, global);
    if (JS_IsException(m_js->bridge))
    {
        fail("cannot create the emulator");
        return false;
    }

    // load(bytes, isHex)
    JSValue bytes = JS_NewUint8ArrayCopy(ctx, reinterpret_cast<const uint8_t *>(firmware.data()),
                                         firmware.size());
    JSValue loadArgs[2] = { bytes, JS_NewBool(ctx, endsWith(m_cfg.fwPath, ".hex")) };
    JSValue load = JS_GetPropertyStr(ctx, m_js->bridge, "load");
    JSValue r = JS_Call(ctx, load, m_js->bridge, 2, loadArgs);
    JS_FreeValue(ctx, load);
    JS_FreeValue(ctx, bytes);
    if (JS_IsException(r))
    {
        fail("cannot load the firmware");
        return false;
    }
    JS_FreeValue(ctx, r);

    m_lastOut.assign(rp2040OutputCount(m_cfg), 0);
    m_ok = true;
    std::string chip = m_cfg.chip;
    if (rp2350)
    {
        JSValue a = JS_GetPropertyStr(ctx, m_js->bridge, "arch");
        const char *archName = JS_ToCString(ctx, a);
        chip += std::string(" ") + (archName != nullptr ? archName : "?");
        JS_FreeCString(ctx, archName);
        JS_FreeValue(ctx, a);
    }
    std::fprintf(stderr, "%s %s (%s in QuickJS) running %s (%zu io, %zu ADC)\n", kTag,
                 chip.c_str(), rp2350 ? "rp2350js" : "rp2040js",
                 m_cfg.fwPath.c_str(), m_cfg.ios.size(), m_cfg.adc.size());
    return true;
}

/** bridge[method](argv...) -> numbers of the returned array (if result). */
bool Rp2040Device::call(const char *method, int argc, const double *argv,
                        std::vector<double> *result)
{
    JSContext *ctx = m_js->ctx;
    JSValue fn = JS_GetPropertyStr(ctx, m_js->bridge, method);
    JSValue args[4];
    for (int i = 0; i < argc; ++i)
    {
        args[i] = JS_NewFloat64(ctx, argv[i]);
    }
    JSValue r = JS_Call(ctx, fn, m_js->bridge, argc, args);
    JS_FreeValue(ctx, fn);
    if (JS_IsException(r))
    {
        fail(method);
        return false;
    }
    if (result != nullptr)
    {
        result->clear();
        JSValue lenVal = JS_GetPropertyStr(ctx, r, "length");
        uint32_t len = 0;
        JS_ToUint32(ctx, &len, lenVal);
        JS_FreeValue(ctx, lenVal);
        for (uint32_t i = 0; i < len; ++i)
        {
            JSValue v = JS_GetPropertyUint32(ctx, r, i);
            double d = 0.0;
            JS_ToFloat64(ctx, &d, v);
            JS_FreeValue(ctx, v);
            result->push_back(d);
        }
    }
    JS_FreeValue(ctx, r);
    return true;
}

void Rp2040Device::printLines(bool final)
{
    JSContext *ctx = m_js->ctx;
    JSValue fn = JS_GetPropertyStr(ctx, m_js->bridge, "takeLines");
    JSValue arg = JS_NewBool(ctx, final);
    JSValue r = JS_Call(ctx, fn, m_js->bridge, 1, &arg);
    JS_FreeValue(ctx, fn);
    if (JS_IsException(r))
    {
        fail("takeLines");
        return;
    }
    JSValue lenVal = JS_GetPropertyStr(ctx, r, "length");
    uint32_t len = 0;
    JS_ToUint32(ctx, &len, lenVal);
    JS_FreeValue(ctx, lenVal);
    for (uint32_t i = 0; i + 2 < len; i += 3)
    {
        JSValue t = JS_GetPropertyUint32(ctx, r, i);
        JSValue name = JS_GetPropertyUint32(ctx, r, i + 1);
        JSValue text = JS_GetPropertyUint32(ctx, r, i + 2);
        double ns = 0.0;
        JS_ToFloat64(ctx, &ns, t);
        const char *n = JS_ToCString(ctx, name);
        const char *s = JS_ToCString(ctx, text);
        std::fprintf(stderr, "%s t=%.6f s %s> %s\n", kTag, ns * 1e-9, n, s);
        if (m_log != nullptr)
        {
            std::fprintf(m_log, "%s\n", s);
            std::fflush(m_log);
        }
        JS_FreeCString(ctx, n);
        JS_FreeCString(ctx, s);
        JS_FreeValue(ctx, t);
        JS_FreeValue(ctx, name);
        JS_FreeValue(ctx, text);
    }
    JS_FreeValue(ctx, r);
}

void Rp2040Device::step(co_info *info)
{
    if (!m_ok)
    {
        return;
    }
    const double target = info->vtime;
    std::vector<double> r;
    const double targetNs = target * 1e9;
    if (!call("run", 1, &targetNs, &r) || r.empty())
    {
        return;
    }
    printLines(false);
    if (r.size() == 1u)
    {
        return;    // vtime reached, no change.
    }

    // Changes at r[0] ns: quintuples (k, level, oe, pu, pd); k = -1: DAC code.
    double when = r[0] * 1e-9;
    if (m_first || when <= 0.0)
    {
        when = target;    // Power-on state: published at the first step.
    }
    m_first = false;
    const size_t ioPorts = 4u * m_cfg.ios.size();
    for (size_t i = 1; i + 5 <= r.size(); i += 5)
    {
        const int k = static_cast<int>(r[i]);
        int bits[16];
        size_t base = 0;
        size_t count = 0;
        if (k >= 0)
        {
            base = 4u * static_cast<size_t>(k);
            count = 4;
            for (int b = 0; b < 4; ++b)
            {
                bits[b] = r[i + 1 + static_cast<size_t>(b)] != 0.0 ? 1 : 0;
            }
        }
        else
        {
            base = ioPorts;
            count = kRp2040DacBits;
            const int code = static_cast<int>(r[i + 1]);
            for (int b = 0; b < kRp2040DacBits; ++b)
            {
                bits[b] = (code >> b) & 1;
            }
        }
        for (size_t b = 0; b < count; ++b)
        {
            if (bits[b] != m_lastOut[base + b])
            {
                m_lastOut[base + b] = bits[b];
                Digital_t v{ bits[b] ? ONE : ZERO, STRONG };
                info->vtime = std::min(when, target);
                info->out_fn(info, static_cast<unsigned int>(base + b), &v);
            }
        }
    }
    info->vtime = std::min(when, target);
    if (m_cfg.log)
    {
        std::fprintf(stderr, "%s t=%.9f %s pins changed\n", kTag, info->vtime, label());
    }
}

void Rp2040Device::input(co_info * /*info*/, unsigned int index, const Digital_t *value)
{
    if (!m_ok)
    {
        return;
    }
    const double args[2] = { static_cast<double>(index < m_cfg.ios.size()
                                                     ? index
                                                     : index - m_cfg.ios.size()),
                             value->state == ONE ? 1.0 : 0.0 };
    // setInput / setComparator expect a boolean: 0 / 1 converts in JS.
    call(index < m_cfg.ios.size() ? "setInput" : "setComparator", 2, args, nullptr);
}

} // namespace kisimavr
