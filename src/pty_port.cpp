/*
 * kisimavr - ATmega co-simulation for ngspice / KiCad
 * Copyright (C) 2026 Paul
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file pty_port.cpp
 * @brief A serial port of the simulated MCU as a Linux pseudo-terminal.
 */

#include "pty_port.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

#include "common.h"

namespace kisimavr
{

std::string expandHome(const std::string &path)
{
    if (path.rfind("~/", 0) == 0)
    {
        const char *home = std::getenv("HOME");
        if (home != nullptr && home[0] != '\0')
        {
            return std::string(home) + path.substr(1);
        }
    }
    return path;
}

namespace
{

/** True if path is a symbolic link to a pseudo-terminal (a stale one of ours). */
bool isPtyLink(const std::string &path)
{
    char target[256];
    const ssize_t n = ::readlink(path.c_str(), target, sizeof target - 1);
    if (n <= 0)
    {
        return false;
    }
    target[n] = '\0';
    return std::strncmp(target, "/dev/pts/", 9) == 0;
}

} // namespace

PtyPort::~PtyPort()
{
    if (!m_link.empty() && isPtyLink(m_link))
    {
        ::unlink(m_link.c_str());
    }
    if (m_slave >= 0)
    {
        ::close(m_slave);
    }
    if (m_client >= 0)
    {
        ::close(m_client);
    }
    if (m_master >= 0)
    {
        ::close(m_master);
    }
}

bool PtyPort::openTcp(int port)
{
    m_tcp = true;
    m_master = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (m_master < 0)
    {
        std::fprintf(stderr, "%s %s: cannot create a socket: %s\n", kTag, m_label.c_str(),
                     std::strerror(errno));
        return false;
    }
    const int one = 1;
    ::setsockopt(m_master, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);    // This computer only.
    if (::bind(m_master, reinterpret_cast<sockaddr *>(&addr), sizeof addr) != 0 ||
        ::listen(m_master, 1) != 0)
    {
        std::fprintf(stderr, "%s %s: cannot listen on 127.0.0.1:%d: %s\n", kTag,
                     m_label.c_str(), port, std::strerror(errno));
        ::close(m_master);
        m_master = -1;
        return false;
    }
    m_device = "127.0.0.1:" + std::to_string(port);
    std::fprintf(stderr, "%s %s on TCP %s (nc localhost %d, PuTTY Raw, socat...)\n", kTag,
                 m_label.c_str(), m_device.c_str(), port);
    return true;
}

void PtyPort::acceptClient()
{
    if (m_client >= 0)
    {
        return;
    }
    m_client = ::accept4(m_master, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (m_client >= 0)
    {
        m_iac = 0;
        std::fprintf(stderr, "%s %s: terminal connected\n", kTag, m_label.c_str());
        if (!m_backlog.empty())
        {
            // What the firmware sent before the connection.
            ::send(m_client, m_backlog.data(), m_backlog.size(), MSG_NOSIGNAL | MSG_DONTWAIT);
            m_backlog.clear();
        }
    }
}

std::vector<uint8_t> PtyPort::stripTelnet(const uint8_t *data, size_t length)
{
    std::vector<uint8_t> out;
    for (size_t i = 0; i < length; ++i)
    {
        const uint8_t b = data[i];
        if (m_iac > 0)
        {
            --m_iac;
        }
        else if (b == 0xFF)
        {
            m_iac = 2;    // IAC, command, option
        }
        else if (b != 0)
        {
            out.push_back(b);
        }
    }
    return out;
}

bool PtyPort::open(const std::string &link, const std::string &label)
{
    m_label = label;
    if (link.rfind("tcp:", 0) != 0 &&
        (std::getenv("FLATPAK_ID") != nullptr || ::access("/.flatpak-info", F_OK) == 0))
    {
        // Sandboxed KiCad: its pseudo-terminals are not those of the host.
        const std::string tcp = label == "UART0" ? "tcp:5555" : "tcp:5556";
        std::fprintf(stderr, "%s %s: KiCad runs in Flatpak, pseudo-terminal replaced by %s\n",
                     kTag, label.c_str(), tcp.c_str());
        return open(tcp, label);
    }
    if (link.rfind("tcp:", 0) == 0)
    {
        const int port = std::atoi(link.c_str() + 4);
        if (port < 1024 || port > 65535)
        {
            std::fprintf(stderr, "%s %s: invalid TCP port in '%s' (1024..65535)\n", kTag,
                         label.c_str(), link.c_str());
            return false;
        }
        return openTcp(port);
    }
    m_master = ::posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (m_master < 0 || ::grantpt(m_master) != 0 || ::unlockpt(m_master) != 0)
    {
        std::fprintf(stderr, "%s %s: cannot create a pseudo-terminal: %s\n", kTag,
                     label.c_str(), std::strerror(errno));
        if (m_master >= 0)
        {
            ::close(m_master);
            m_master = -1;
        }
        return false;
    }
    const char *name = ::ptsname(m_master);
    m_device = name != nullptr ? name : "";
    ::fcntl(m_master, F_SETFD, FD_CLOEXEC);

    // Raw bytes, no echo: the terminal program sets its own options.
    m_slave = ::open(m_device.c_str(), O_RDWR | O_NOCTTY | O_CLOEXEC);
    if (m_slave >= 0)
    {
        termios tio{};
        if (::tcgetattr(m_slave, &tio) == 0)
        {
            ::cfmakeraw(&tio);
            ::tcsetattr(m_slave, TCSANOW, &tio);
        }
    }

    std::string shown = m_device;
    if (!link.empty())
    {
        const std::string path = expandHome(link);
        struct stat st{};
        const bool exists = ::lstat(path.c_str(), &st) == 0;
        if (exists && !(S_ISLNK(st.st_mode) && isPtyLink(path)))
        {
            std::fprintf(stderr, "%s %s: not replacing '%s' (not a link to /dev/pts)\n", kTag,
                         label.c_str(), path.c_str());
        }
        else
        {
            if (exists)
            {
                ::unlink(path.c_str());    // Stale link of an earlier simulation.
            }
            if (::symlink(m_device.c_str(), path.c_str()) == 0)
            {
                m_link = path;
                shown = path + " -> " + m_device;
            }
            else
            {
                std::fprintf(stderr, "%s %s: cannot create the link '%s': %s\n", kTag,
                             label.c_str(), path.c_str(), std::strerror(errno));
            }
        }
    }
    std::fprintf(stderr, "%s %s on %s (picocom, CuteCom, screen...)\n", kTag, label.c_str(),
                 shown.c_str());
    return true;
}

void PtyPort::write(const uint8_t *data, size_t length)
{
    if (m_master < 0 || length == 0)
    {
        return;
    }
    if (m_tcp)
    {
        acceptClient();
        if (m_client >= 0 &&
            ::send(m_client, data, length, MSG_NOSIGNAL | MSG_DONTWAIT) < 0 && errno != EAGAIN)
        {
            ::close(m_client);    // Terminal closed.
            m_client = -1;
        }
        if (m_client < 0)
        {
            // No terminal yet: keep the last 4 kB for the first connection.
            m_backlog.insert(m_backlog.end(), data, data + length);
            if (m_backlog.size() > 4096u)
            {
                m_backlog.erase(m_backlog.begin(),
                                m_backlog.begin() + static_cast<long>(m_backlog.size() - 4096u));
            }
        }
        return;
    }
    const ssize_t n = ::write(m_master, data, length);
    if ((n < 0 || static_cast<size_t>(n) < length) && !m_full)
    {
        m_full = true;    // Nobody reads: the kernel buffer is full.
        std::fprintf(stderr, "%s %s: terminal not read, output dropped\n", kTag,
                     m_label.c_str());
    }
    else if (n == static_cast<ssize_t>(length))
    {
        m_full = false;
    }
}

std::vector<uint8_t> PtyPort::read()
{
    std::vector<uint8_t> out;
    if (m_master < 0)
    {
        return out;
    }
    uint8_t buf[256];
    if (m_tcp)
    {
        acceptClient();
        while (m_client >= 0)
        {
            const ssize_t n = ::recv(m_client, buf, sizeof buf, MSG_DONTWAIT);
            if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK))
            {
                ::close(m_client);    // Terminal closed: wait for the next one.
                m_client = -1;
                std::fprintf(stderr, "%s %s: terminal disconnected\n", kTag, m_label.c_str());
                break;
            }
            if (n < 0)
            {
                break;
            }
            const std::vector<uint8_t> clean = stripTelnet(buf, static_cast<size_t>(n));
            out.insert(out.end(), clean.begin(), clean.end());
        }
        return out;
    }
    for (;;)
    {
        const ssize_t n = ::read(m_master, buf, sizeof buf);
        if (n <= 0)
        {
            break;    // EAGAIN: nothing typed.
        }
        out.insert(out.end(), buf, buf + n);
    }
    return out;
}

} // namespace kisimavr
