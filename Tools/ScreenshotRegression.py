#!/usr/bin/env python3
"""Screenshot regression for showcase / QA scenes.

Runs the game on each scene for a fixed number of frames with a fixed frame time (--frame-time makes
the simulation repeatable), captures the backbuffer, and compares it with a reference image.

  # check (exit 1 when any scene drifts past the threshold)
  Engine/Tools/ScreenshotRegression.py --game .build/Game_linux_Debug/Game_EntryPoint_x64 \\
      --reference Assets/Scenes/Showcase/Reference Assets/Scenes/Showcase/*.lvl
  # accept the current look as the new reference
  ... --update

Run it from the game project root (the game resolves Assets/ from there). --wrapper prefixes the
command (e.g. a script that points DISPLAY at a virtual X server). References are stored downscaled;
the comparison happens at that size, with a per-pixel tolerance so particles and dithering don't
count as regressions. Differences are written as amplified diff images to --out.
"""
import argparse
import os
import shlex
import struct
import subprocess
import sys
import zlib

# --------------------------------------------------------------------------------------------- PNG

def read_png(path):
    """Returns (width, height, rows) with rows as RGB byte arrays (8-bit RGB / RGBA, non-interlaced)."""
    with open(path, "rb") as f:
        data = f.read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"{path}: not a PNG")
    pos, idat, width, height, color_type = 8, [], 0, 0, 0
    while pos < len(data):
        length, kind = struct.unpack(">I4s", data[pos:pos + 8])
        chunk = data[pos + 8:pos + 8 + length]
        pos += 12 + length
        if kind == b"IHDR":
            width, height, depth, color_type, _, _, interlace = struct.unpack(">IIBBBBB", chunk)
            if depth != 8 or color_type not in (2, 6) or interlace:
                raise ValueError(f"{path}: only 8-bit RGB/RGBA non-interlaced PNGs are supported")
        elif kind == b"IDAT":
            idat.append(chunk)
        elif kind == b"IEND":
            break
    channels = 4 if color_type == 6 else 3
    raw = zlib.decompress(b"".join(idat))
    stride = width * channels
    rows, previous = [], bytearray(stride)
    for y in range(height):
        start = y * (stride + 1)
        kind = raw[start]
        line = bytearray(raw[start + 1:start + 1 + stride])
        if kind == 1:
            for i in range(channels, stride):
                line[i] = (line[i] + line[i - channels]) & 0xFF
        elif kind == 2:
            for i in range(stride):
                line[i] = (line[i] + previous[i]) & 0xFF
        elif kind == 3:
            for i in range(stride):
                left = line[i - channels] if i >= channels else 0
                line[i] = (line[i] + ((left + previous[i]) >> 1)) & 0xFF
        elif kind == 4:
            for i in range(stride):
                a = line[i - channels] if i >= channels else 0
                b = previous[i]
                c = previous[i - channels] if i >= channels else 0
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else (b if pb <= pc else c))) & 0xFF
        previous = line
        if channels == 4:
            rgb = bytearray(width * 3)
            rgb[0::3], rgb[1::3], rgb[2::3] = line[0::4], line[1::4], line[2::4]
            rows.append(rgb)
        else:
            rows.append(line)
    return width, height, rows


def write_png(path, width, height, rows):
    raw = b"".join(b"\x00" + bytes(row) for row in rows)
    def chunk(kind, payload):
        return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(raw, 9)))
        f.write(chunk(b"IEND", b""))


def downscale(width, height, rows, factor):
    """Box filter by an integer factor."""
    if factor <= 1:
        return width, height, rows
    out_w, out_h = width // factor, height // factor
    area = factor * factor
    out = []
    for oy in range(out_h):
        sums = [0] * (out_w * 3)
        for dy in range(factor):
            row = rows[oy * factor + dy]
            for ox in range(out_w):
                base = ox * factor * 3
                for dx in range(factor):
                    i = base + dx * 3
                    sums[ox * 3] += row[i]
                    sums[ox * 3 + 1] += row[i + 1]
                    sums[ox * 3 + 2] += row[i + 2]
        out.append(bytearray(s // area for s in sums))
    return out_w, out_h, out


def compare(a, b, tolerance):
    """Mean absolute difference (0..1) and the fraction of pixels off by more than tolerance."""
    width, height, rows_a = a
    _, _, rows_b = b
    total, bad, diff_rows = 0, 0, []
    for y in range(height):
        ra, rb = rows_a[y], rows_b[y]
        diff = bytearray(width * 3)
        for x in range(width):
            i = x * 3
            d = max(abs(ra[i] - rb[i]), abs(ra[i + 1] - rb[i + 1]), abs(ra[i + 2] - rb[i + 2]))
            total += d
            if d > tolerance:
                bad += 1
            v = min(255, d * 4)
            diff[i], diff[i + 1], diff[i + 2] = v, v // 4, v // 4
        diff_rows.append(diff)
    pixels = width * height
    return total / (pixels * 255.0), bad / pixels, diff_rows

# -------------------------------------------------------------------------------------------- main

def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("scenes", nargs="+", help="scene files (.lvl)")
    parser.add_argument("--game", required=True, help="game executable")
    parser.add_argument("--reference", required=True, help="folder of reference PNGs (<scene name>.png)")
    parser.add_argument("--out", default=".tmp/ScreenshotRegression", help="captures and diffs")
    parser.add_argument("--frames", type=int, default=480)
    parser.add_argument("--frame-time", type=float, default=1.0 / 60.0)
    parser.add_argument("--width", type=int, default=960)
    parser.add_argument("--height", type=int, default=540)
    parser.add_argument("--scale", type=int, default=2, help="downscale factor for references and comparison")
    parser.add_argument("--tolerance", type=int, default=24, help="per-pixel channel difference that counts (0-255)")
    parser.add_argument("--max-changed", type=float, default=0.03, help="fraction of changed pixels that fails a scene")
    parser.add_argument("--wrapper", default="", help="command prefix, e.g. a virtual-display launcher")
    parser.add_argument("--update", action="store_true", help="write the captures as the new references")
    args = parser.parse_args()

    os.makedirs(args.out, exist_ok=True)
    os.makedirs(args.reference, exist_ok=True)
    failures = 0
    for scene in args.scenes:
        name = os.path.splitext(os.path.basename(scene))[0]
        capture = os.path.join(args.out, name + ".png")
        if os.path.exists(capture):
            os.remove(capture)
        command = shlex.split(args.wrapper) + [args.game, "--scene", scene, "--frames", str(args.frames), "--frame-time", str(args.frame_time),
                                               "--width", str(args.width), "--height", str(args.height), "--no-audio", "--screenshot", capture, "--exit"]
        log_path = os.path.join(args.out, name + ".log")
        with open(log_path, "w") as log:
            result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT)
        if result.returncode != 0 or not os.path.exists(capture):
            print(f"FAIL  {name}: the game exited with {result.returncode} without a capture (see {log_path})")
            failures += 1
            continue
        image = read_png(capture)
        small = downscale(*image, args.scale)
        reference_path = os.path.join(args.reference, name + ".png")
        if args.update:
            write_png(reference_path, *small)
            print(f"WROTE {name}: {reference_path} ({small[0]}x{small[1]})")
            continue
        if not os.path.exists(reference_path):
            print(f"FAIL  {name}: no reference (run with --update to create {reference_path})")
            failures += 1
            continue
        reference = read_png(reference_path)
        if reference[:2] != small[:2]:
            print(f"FAIL  {name}: capture is {small[0]}x{small[1]}, reference {reference[0]}x{reference[1]}")
            failures += 1
            continue
        mean, changed, diff_rows = compare(small, reference, args.tolerance)
        write_png(os.path.join(args.out, name + ".diff.png"), small[0], small[1], diff_rows)
        ok = changed <= args.max_changed
        failures += 0 if ok else 1
        print(f"{'ok   ' if ok else 'FAIL '} {name}: {changed * 100:.2f}% of pixels changed (limit {args.max_changed * 100:.1f}%), mean difference {mean * 100:.2f}%")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
