#!/usr/bin/env python3
"""Pixel-perfect shimmer check (LOOK-S6): captures a scene at consecutive frames and requires each
pair to be identical or an exact translation by whole texels, so a panning pixel-perfect camera
moves everything in lockstep instead of shimmering.

    Engine/Tools/PixelShimmerCheck.py --game .build/Game_linux_Debug/Game_EntryPoint_x64 \\
        --wrapper Engine/Tools/Headless.sh --scene Assets/Scenes/Tests/PixelPerfectTest.lvl --texel 4

--texel is the block size in output pixels (output / internal). Each capture is its own
deterministic run (--frame-time), at --first, --first + 1, ... (--count captures). Exit 1 when a
pair is neither identical nor a clean whole-texel shift; --expect-shimmer inverts that (the
control: the same scene without PixelSnap must fail it).
"""
import argparse
import os
import shlex
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ScreenshotRegression import read_png  # noqa: E402


def best_shift(a, b, texel, margin, search):
    """The whole-texel shift (dx, dy) that maps a onto b with the fewest differing pixels."""
    width, height, rows_a = a
    _, _, rows_b = b
    best = None
    for dy in range(-search, search + 1, texel):
        for dx in range(-search, search + 1, texel):
            bad = 0
            for y in range(margin, height - margin, 2):
                ra, rb = rows_a[y], rows_b[y + dy]
                for x in range(margin, width - margin, 2):
                    i, j = x * 3, (x + dx) * 3
                    if ra[i] != rb[j] or ra[i + 1] != rb[j + 1] or ra[i + 2] != rb[j + 2]:
                        bad += 1
            if best is None or bad < best[2]:
                best = (dx, dy, bad)
            if bad == 0:
                return best
    return best


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--game", required=True)
    parser.add_argument("--scene", required=True)
    parser.add_argument("--wrapper", default="")
    parser.add_argument("--texel", type=int, default=4)
    parser.add_argument("--first", type=int, default=120)
    parser.add_argument("--count", type=int, default=6)
    parser.add_argument("--width", type=int, default=960)
    parser.add_argument("--height", type=int, default=540)
    parser.add_argument("--frame-time", default="0.0166666667")
    parser.add_argument("--out", default=".tmp/PixelShimmer")
    parser.add_argument("--expect-shimmer", action="store_true")
    args = parser.parse_args()
    os.makedirs(args.out, exist_ok=True)

    name = os.path.splitext(os.path.basename(args.scene))[0]
    captures = []
    for frame in range(args.first, args.first + args.count):
        shot = os.path.join(args.out, "%s_%04d.png" % (name, frame))
        if os.path.exists(shot):
            os.remove(shot)
        command = shlex.split(args.wrapper) + [args.game, "--scene", args.scene, "--frames", str(frame), "--frame-time", args.frame_time,
                                              "--width", str(args.width), "--height", str(args.height), "--no-audio", "--screenshot", shot, "--exit"]
        with open(shot[:-4] + ".log", "w") as log:
            result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT)
        if result.returncode != 0 or not os.path.exists(shot):
            print(f"FAIL  frame {frame}: no capture (see {shot[:-4]}.log)")
            return 1
        captures.append((frame, read_png(shot)))

    shimmering = 0
    margin = 4 * args.texel + 2
    for (frame_a, a), (frame_b, b) in zip(captures, captures[1:]):
        dx, dy, bad = best_shift(a, b, args.texel, margin, 2 * args.texel)
        state = "identical" if (dx, dy, bad) == (0, 0, 0) else ("shift %+d, %+d px" % (dx, dy) if bad == 0 else "SHIMMER (%d pixels differ at the best whole-texel shift %+d, %+d)" % (bad, dx, dy))
        print(f"  frames {frame_a}->{frame_b}: {state}")
        shimmering += bad > 0
    if args.expect_shimmer:
        ok = shimmering > 0
        print(("ok    " if ok else "FAIL  ") + f"{name}: {shimmering} shimmering pair(s), the control expects some")
    else:
        ok = shimmering == 0
        print(("ok    " if ok else "FAIL  ") + f"{name}: every frame identical or a whole-texel shift" + ("" if ok else f" ({shimmering} pair(s) shimmer)"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
