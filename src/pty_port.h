/*
 * kisimavr - ATmega co-simulation for ngspice / KiCad
 * Copyright (C) 2026 Paul
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file pty_port.h
 * @brief A serial port of the simulated MCU as a Linux pseudo-terminal.
 *
 * Any terminal program opens it like a USB serial adapter: picocom,
 * minicom, screen, CuteCom, GTKTerm, or pyserial. The bytes sent by the
 * firmware are written to it; the bytes typed in the program are read
 * back and sent to the firmware.
 *
 * The pseudo-terminal is /dev/pts/N (N chosen by the kernel); a symbolic
 * link with a stable name (e.g. ~/kisimavr_uart0) can point to it. The
 * link is created only where nothing exists, or replaces a stale link to
 * /dev/pts; a regular file is never replaced. It is removed at the end.
 *
 * "tcp:PORT" instead of a link: a TCP server on 127.0.0.1:PORT (one client
 * at a time), for a sandboxed KiCad (Flatpak: its pseudo-terminals are not
 * visible from the host). Clients: nc localhost PORT, PuTTY (Raw), or
 * socat pty,link=$HOME/kisimavr_uart0,raw,echo=0 tcp:localhost:PORT for
 * CuteCom. Telnet option sequences (IAC) are removed from the input; the
 * last 4 kB sent before a terminal connects are given to it. Inside
 * Flatpak, a pseudo-terminal request becomes tcp:5555 (UART0) or tcp:5556.
 */

#ifndef KISIMAVR_PTY_PORT_H
#define KISIMAVR_PTY_PORT_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace kisimavr
{

class PtyPort
{
public:
    PtyPort() = default;
    ~PtyPort();
    PtyPort(const PtyPort &) = delete;
    PtyPort &operator=(const PtyPort &) = delete;

    /**
     * Open the pseudo-terminal; link: "" for none, else the path of the
     * symbolic link ("~/" allowed). label names the port in the messages
     * ("UART0"). Returns false (message on stderr) on failure.
     */
    bool open(const std::string &link, const std::string &label);

    bool isOpen() const { return m_master >= 0; }

    /** /dev/pts/N, or 127.0.0.1:PORT */
    const std::string &device() const { return m_device; }

    /** Bytes to the terminal program (dropped when nobody reads them). */
    void write(const uint8_t *data, size_t length);
    void write(uint8_t byte) { write(&byte, 1); }

    /** Bytes typed in the terminal program since the last call. */
    std::vector<uint8_t> read();

private:
    bool openTcp(int port);
    void acceptClient();
    std::vector<uint8_t> stripTelnet(const uint8_t *data, size_t length);

    int m_master = -1;          // Pseudo-terminal master, or listening socket
    int m_slave = -1;           // Kept open: no EIO while no program is connected.
    int m_client = -1;          // TCP: connected client
    bool m_tcp = false;
    int m_iac = 0;              // TCP: bytes of a telnet sequence still to skip
    std::vector<uint8_t> m_backlog;    // TCP: sent before the first connection
    std::string m_device;
    std::string m_link;         // Created by us, removed at the end.
    std::string m_label;
    bool m_full = false;        // Already reported: output dropped.
};

/** "~/x" -> "$HOME/x"; other paths unchanged. */
std::string expandHome(const std::string &path);

} // namespace kisimavr

#endif // KISIMAVR_PTY_PORT_H
