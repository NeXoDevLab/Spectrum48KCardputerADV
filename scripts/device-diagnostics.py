#!/usr/bin/env python3
"""Read performance counters or run the 5-second audio probe (macOS/Linux)."""
import argparse
import os
import select
import termios
import time
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port")
    parser.add_argument("command", choices=("?", "b"), nargs="?", default="?")
    parser.add_argument("--seconds", type=float, default=8)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    captured = bytearray()
    fd = os.open(args.port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    try:
        settings = termios.tcgetattr(fd)
        settings[0] = settings[1] = settings[3] = 0
        settings[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
        settings[4] = settings[5] = termios.B115200
        settings[6][termios.VMIN] = settings[6][termios.VTIME] = 0
        termios.tcsetattr(fd, termios.TCSANOW, settings)
        termios.tcflush(fd, termios.TCIFLUSH)
        start = time.monotonic()
        next_query = start
        while time.monotonic() - start < args.seconds:
            now = time.monotonic()
            if now >= next_query:
                os.write(fd, args.command.encode())
                next_query = now + (1 if args.command == "?" else args.seconds + 1)
            if select.select([fd], [], [], 0.05)[0]:
                chunk = os.read(fd, 65536)
                captured.extend(chunk)
                print(chunk.decode("utf-8", errors="replace"), end="", flush=True)
                if args.command == "b" and b"ZX AUDIO TEST " in captured and captured.endswith(b"\n"):
                    break
    finally:
        os.close(fd)
    if args.output:
        args.output.write_bytes(captured)
    if b"ZX" not in captured:
        raise SystemExit("No ZX diagnostics received. Close the game selector and check the USB port.")


if __name__ == "__main__":
    main()
