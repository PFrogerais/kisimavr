/*
 * kisimavr - ATmega co-simulation for ngspice / KiCad
 * Copyright (C) 2026 Paul
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file paths.h
 * @brief File names coming from the netlist: case restoration (KiCad
 *        lower-cases them) and guarded creation of the output files.
 */

#ifndef KISIMAVR_PATHS_H
#define KISIMAVR_PATHS_H

#include <cstdio>
#include <initializer_list>
#include <string>

namespace kisimavr
{

/**
 * KiCad's simulator lower-cases the strings of the netlist, sim_args
 * included: "/home/me/Arduino/Blink.hex" arrives as ".../arduino/blink.hex".
 * Rebuild the real path, one component at a time, ignoring case. A path
 * that cannot be found is returned unchanged.
 */
std::string resolvePath(const std::string &path);

/** Same for a file to create: resolve its folder, keep its name. */
std::string resolveOutputPath(const std::string &path);

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

/** VCD written by the plug-in: starts with "$timescale". */
bool looksLikeVcd(const std::string &head);

/** UART log: printable ASCII lines of at most 200 characters. */
bool looksLikeUartLog(const std::string &head);

/** LCD log: starts with "# kisimavr LCD". */
bool looksLikeLcdLog(const std::string &head);

/** Case-insensitive extension test, e.g. {".txt", ".log"}. */
bool hasExtension(const std::string &path, std::initializer_list<const char *> extensions);

/** Open (and truncate) an output file, or explain why not and return null. */
std::FILE *openOutputFile(const std::string &path, const char *what,
                          std::initializer_list<const char *> extensions, ContentCheck check);

} // namespace kisimavr

#endif // KISIMAVR_PATHS_H
