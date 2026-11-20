/*
 * kisimavr - ATmega co-simulation for ngspice / KiCad
 * Copyright (C) 2026 Paul
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file uart_script.h
 * @brief Timed text sent to the UART receiver of the MCU ("uartin" file).
 *
 * One event per line: a time, blanks, then the text to send.
 *
 *   # Comment lines and empty lines are ignored
 *   1m      LED ON\n          <- at 1 ms: "LED ON" then a line feed
 *   2.5e-3  1                 <- at 2.5 ms: the character '1'
 *   4m      \x1B[2J\r\n       <- escapes: \n \r \t \\ \s (space) \xHH
 *
 * Times are in seconds, with the SPICE suffixes f p n u m (k, meg and g
 * are refused: far beyond a simulation). Trailing blanks are dropped, so a
 * leading or trailing space is written \s. Events are sorted by time (equal
 * times keep their file order).
 */

#ifndef KISIMAVR_UART_SCRIPT_H
#define KISIMAVR_UART_SCRIPT_H

#include <string>
#include <vector>

namespace kisimavr
{

struct UartEvent
{
    double      time;     // Seconds.
    std::string bytes;    // Escapes already decoded.
};

/** Parse "1m", "2.5e-3", "250u"... into seconds. False if not a time. */
bool parseSpiceTime(const std::string &text, double &seconds);

/**
 * Parse the content of a "uartin" file. On error, returns false and sets
 * error to "line N: reason".
 */
bool parseUartScript(const std::string &content, std::vector<UartEvent> &events,
                     std::string &error);

} // namespace kisimavr

#endif // KISIMAVR_UART_SCRIPT_H
