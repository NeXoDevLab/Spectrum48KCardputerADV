#!/usr/bin/env python3
"""Record genuine ZX frames and PCM from a ZX_USB_RECORDING device build."""
import argparse
import json
import os
from pathlib import Path
import select
import shutil
import struct
import subprocess
import tempfile
import termios
import time
import wave
import zlib

WIDTH, HEIGHT, RATE = 240, 135, 16000
HEADER = struct.Struct('<4sIIHHIII')
LEVELS = (0, 128, 128, 255)
COLOURS = [bytes((LEVELS[c & 3], LEVELS[c >> 2 & 3], LEVELS[c >> 4 & 3])) for c in range(64)]

def decode_rle(data, version=1):
    stride = 3 if version == 2 else 2
    if len(data) % stride:
        raise ValueError('Invalid RLE payload length')
    rgb = bytearray()
    for at in range(0, len(data), stride):
        count, colour = data[at:at + 2]
        if not count or (version == 1 and colour >= 64):
            raise ValueError('Invalid RLE run')
        if version == 2:
            colour |= data[at + 2] << 8
            value = bytes(((colour >> 11) * 255 // 31, ((colour >> 5) & 63) * 255 // 63, (colour & 31) * 255 // 31))
        else:
            value = COLOURS[colour]
        rgb.extend(value * count)
        if len(rgb) > WIDTH * HEIGHT * 3:
            raise ValueError('Oversized video frame')
    if len(rgb) != WIDTH * HEIGHT * 3:
        raise ValueError('Incomplete video frame')
    return rgb

def png(path, rgb):
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
    rows = b''.join(b'\0' + rgb[y * WIDTH * 3:(y + 1) * WIDTH * 3] for y in range(HEIGHT))
    Path(path).write_bytes(b'\x89PNG\r\n\x1a\n' +
        chunk(b'IHDR', struct.pack('>IIBBBBB', WIDTH, HEIGHT, 8, 2, 0, 0, 0)) +
        chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b''))

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('port')
    parser.add_argument('tape', help='Absolute Spectrum media path on the Cardputer SD')
    parser.add_argument('output', type=Path)
    parser.add_argument('--seconds', type=float, default=900)
    parser.add_argument('--work-dir', type=Path)
    parser.add_argument('--stop-file', type=Path)
    parser.add_argument('--continue-file', type=Path,
        help='Send key 3 once when this file appears, to leave a game title screen')
    parser.add_argument('--ffmpeg')
    args = parser.parse_args()
    if not args.tape.startswith('/SPECTRUM/GAMES/') or not args.tape.lower().endswith(('.tap','.tzx','.sna','.z80')):
        parser.error('Select a Spectrum file inside /SPECTRUM/GAMES/')
    ffmpeg = args.ffmpeg or shutil.which('ffmpeg')
    if not ffmpeg:
        import imageio_ffmpeg
        ffmpeg = imageio_ffmpeg.get_ffmpeg_exe()
    work = args.work_dir or Path(tempfile.mkdtemp(prefix='zx-recording-'))
    work.mkdir(parents=True, exist_ok=True)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    if args.output.exists():
        parser.error('Output already exists; choose a new filename')
    fd = os.open(args.port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    settings = termios.tcgetattr(fd)
    settings[0] = settings[1] = settings[3] = 0
    settings[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
    settings[4] = settings[5] = termios.B115200
    settings[6][termios.VMIN] = settings[6][termios.VTIME] = 0
    termios.tcsetattr(fd, termios.TCSANOW, settings)
    pending = bytearray()

    def read_for(seconds):
        end = time.monotonic() + seconds
        result = bytearray()
        while time.monotonic() < end:
            if select.select([fd], [], [], 0.05)[0]:
                result.extend(os.read(fd, 65536))
        return result

    def exact(count):
        deadline = time.monotonic() + 15
        while len(pending) < count:
            if time.monotonic() > deadline:
                raise TimeoutError('Device stopped sending a complete frame')
            if select.select([fd], [], [], 0.1)[0]:
                pending.extend(os.read(fd, 65536))
        data = bytes(pending[:count]); del pending[:count]
        return data

    encoder = None
    audio = None
    sequence = samples = video_frames = 0
    continue_at = None
    end_at = None
    started = time.monotonic()
    try:
        # Close the selector if it is open. q has no effect in the ZX itself.
        ready = time.monotonic() + 25
        while True:
            os.write(fd, b'q?')
            reply = read_for(0.4)
            if b'ZX-ADV ' in reply and b'frames=' in reply:
                break
            if time.monotonic() > ready:
                raise TimeoutError('The ZX did not finish booting')
        os.write(fd, ('@' + args.tape + '\n').encode('utf-8'))
        deadline = time.monotonic() + 15
        while b'ZXREC' not in pending or b'\n' not in pending[pending.find(b'ZXREC'):]:
            if time.monotonic() > deadline:
                raise TimeoutError('No recording handshake; check the special device build')
            if select.select([fd], [], [], 0.1)[0]:
                pending.extend(os.read(fd, 65536))
            if b'ZXREC-ERROR' in pending:
                raise RuntimeError('The device could not mount the selected Spectrum file')
        start = pending.index(b'ZXREC'); end = pending.index(b'\n', start)
        handshake = bytes(pending[start:end]).decode().strip()
        assert handshake in ('ZXREC2 240 135 16000 3500000 69888',), handshake
        version = 2
        del pending[:end + 1]
        print(handshake, flush=True)
        log = open(work / 'encoder.log', 'w')
        encoder = subprocess.Popen([ffmpeg, '-hide_banner', '-loglevel', 'warning', '-nostdin',
            '-f', 'rawvideo', '-pixel_format', 'rgb24', '-video_size', '240x135',
            '-framerate', '15625/624', '-i', 'pipe:0', '-vf', 'scale=1920:1080:flags=neighbor',
            '-r', '25', '-c:v', 'libx264', '-preset', 'veryfast', '-crf', '18',
            '-pix_fmt', 'yuv420p', '-an', '-y', str(work / 'video.mp4')], stdin=subprocess.PIPE, stderr=log)
        audio = wave.open(str(work / 'audio.wav'), 'wb')
        audio.setnchannels(1); audio.setsampwidth(2); audio.setframerate(RATE)
        next_report = 0
        last_rgb = None
        while samples / RATE < args.seconds:
            if args.stop_file and args.stop_file.exists():
                break
            if args.continue_file and args.continue_file.exists() and continue_at is None:
                os.write(fd, b'3')
                continue_at = samples / RATE
                print('CONTINUE KEY at %.3f seconds' % continue_at, flush=True)
            magic, seq, offset, pcm_count, field1, field2, field3, field4 = HEADER.unpack(exact(HEADER.size))
            if version == 2:
                flags, rle_count, position, size = field1, field2, field3, field4
            else:
                rle_count, position, size, flags = field1, field2, field3, field4
            if magic != b'ZXCR' or seq != sequence or offset != samples or pcm_count > 512 or rle_count > WIDTH * HEIGHT * (3 if version == 2 else 2):
                raise ValueError(f'Broken stream at frame {sequence}: {(magic, seq, offset, pcm_count)}')
            if flags & 4:
                raise RuntimeError(f'Tape read error at {position}/{size}')
            if bool(rle_count) != (seq % 2 == 0):
                raise ValueError('Missing video frame')
            pcm = exact(pcm_count * 2)
            rle = exact(rle_count)
            audio.writeframesraw(pcm)
            if rle:
                last_rgb = decode_rle(rle, version)
                encoder.stdin.write(last_rgb)
                video_frames += 1
            sequence += 1; samples += pcm_count
            elapsed = samples / RATE
            if size and position >= size and not flags & 1 and end_at is None:
                end_at = elapsed + 20
            if end_at is not None and elapsed >= end_at:
                break
            if elapsed >= next_report:
                state = dict(seconds=round(elapsed, 3), frames=sequence, video_frames=video_frames,
                    samples=samples, tape_position=position, tape_size=size, motor=bool(flags & 1),
                    loaded=bool(flags & 2), wall_seconds=round(time.monotonic() - started, 3))
                (work / 'progress.json').write_text(json.dumps(state, indent=2))
                print(json.dumps(state), flush=True)
                if last_rgb:
                    png(work / 'preview.png', last_rgb)
                    png(work / ('frame-%04d.png' % int(elapsed)), last_rgb)
                next_report += 10
    finally:
        try:
            os.write(fd, b'!'); read_for(0.6)
        finally:
            os.close(fd)
        if audio:
            audio.close()
        if encoder:
            encoder.stdin.close()
            if encoder.wait(timeout=60):
                raise RuntimeError('Video encoder failed; see encoder.log')
            log.close()
    subprocess.run([ffmpeg, '-hide_banner', '-loglevel', 'warning', '-nostdin',
        '-i', str(work / 'video.mp4'), '-i', str(work / 'audio.wav'),
        '-map', '0:v:0', '-map', '1:a:0', '-c:v', 'copy', '-c:a', 'aac', '-b:a', '96k',
        '-af', 'highpass=f=20,volume=1.8,alimiter=limit=0.95:level=false',
        '-shortest', '-movflags', '+faststart', str(args.output)], check=True)
    metadata = dict(output=str(args.output), source='Cardputer ADV USB capture', tape=args.tape,
        width=1920, height=1080, fps=25, audio_rate=RATE, seconds=samples / RATE,
        frames=sequence, video_frames=video_frames, work_dir=str(work),
        protocol=handshake, continue_key_seconds=continue_at, firmware='V0.3 USB recording build',
        full_tape_unabridged=bool(end_at), ending='20 seconds after tape EOF' if end_at else 'Duration limit', timing='Original emulated PAL clock; USB transfer may take longer than playback')
    args.output.with_suffix('.json').write_text(json.dumps(metadata, indent=2))
    print('VIDEO COMPLETE ' + json.dumps(metadata), flush=True)

if __name__ == '__main__':
    main()
