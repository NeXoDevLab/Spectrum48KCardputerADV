#!/usr/bin/env python3
"""Capture a real emulated LCD image over USB. Requires pyserial and Pillow."""
import argparse
import time
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port", help="USB serial port, for example /dev/cu.usbmodem...")
    parser.add_argument("output", type=Path, help="Output PNG path")
    args = parser.parse_args()
    import serial
    from PIL import Image

    with serial.Serial(args.port, 115200, timeout=1) as device:
        time.sleep(0.2)
        device.reset_input_buffer()
        device.write(b"c")
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            if device.readline().strip() == b"ZXFRAME 240 135 RGB565LE":
                break
        else:
            raise SystemExit("No se recibio ZXFRAME. Cierra el selector y comprueba el puerto.")
        payload = bytearray()
        while len(payload) < 240 * 135 * 2 and time.monotonic() < deadline:
            payload.extend(device.read(240 * 135 * 2 - len(payload)))
        if len(payload) != 240 * 135 * 2:
            raise SystemExit("Captura incompleta; no se ha escrito la imagen.")
    rgb = bytearray()
    for i in range(0, len(payload), 2):
        value = payload[i] | (payload[i + 1] << 8)
        rgb.extend((((value >> 11) & 31) * 255 // 31,
                    ((value >> 5) & 63) * 255 // 63,
                    (value & 31) * 255 // 31))
    Image.frombytes("RGB", (240, 135), bytes(rgb)).save(args.output)
    print(args.output.resolve())


if __name__ == "__main__":
    main()
