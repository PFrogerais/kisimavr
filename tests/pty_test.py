#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Pseudo-terminal of a simulated serial port, used as a terminal program would.

  pty_test.py <link | tcp:PORT> <send> <expect> <timeout s> -- <cosim_runner command...>

Starts the command (which must create the link or the TCP server, "pty"
key), opens it, writes <send> (\\n escapes allowed) once the firmware has
written something, then reads until <expect> appears. Prints what the
terminal received; exit status 0 if <expect> was seen.
"""

import os
import select
import socket
import subprocess
import sys
import termios
import time
import tty


def main():
    link, send, expect, timeout = sys.argv[1:5]
    cmd = sys.argv[sys.argv.index("--") + 1:]
    send = send.encode().decode("unicode_escape").encode()
    deadline = time.time() + float(timeout)
    tcp = link.startswith("tcp:")
    if not tcp and os.path.islink(link):
        os.unlink(link)
    proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    sock = None
    while True:
        if time.time() > deadline or proc.poll() is not None:
            print("no terminal port:", proc.stderr.read().decode(errors="replace"))
            proc.kill()
            return 1
        if tcp:
            try:
                sock = socket.create_connection(("127.0.0.1", int(link[4:])), timeout=1)
                break
            except OSError:
                pass
        elif os.path.exists(link):
            break
        time.sleep(0.05)
    if tcp:
        fd = sock.fileno()
    else:
        fd = os.open(link, os.O_RDWR | os.O_NOCTTY)
        tty.setraw(fd, termios.TCSANOW)
    received = b""
    sent = False
    while time.time() < deadline and expect.encode() not in received:
        ready, _, _ = select.select([fd], [], [], 0.2)
        if ready:
            try:
                received += os.read(fd, 1024)
            except OSError:
                break
        if received and not sent:
            os.write(fd, send)
            sent = True
        if proc.poll() is not None and not ready:
            break
    if tcp:
        sock.close()
    else:
        os.close(fd)
    proc.kill()
    err = proc.communicate()[1].decode(errors="replace")
    sys.stdout.write(err)
    print("--- terminal received ---")
    print(received.decode(errors="replace"))
    ok = expect.encode() in received
    print("PTY OK" if ok else f"PTY FAILED: '{expect}' not received")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
