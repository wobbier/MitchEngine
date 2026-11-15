#!/usr/bin/env python3
"""CPU vs GPU particle parity check (ME-377): Assets/Scenes/Tests/GpuParticlesCompare.lvl.

The scene holds one row per case, the same additive emitter twice: CPU simulation on the left,
GPU (compute shader) on the right. Random values differ between the paths by design, so the check
never compares pixels: per row it compares coverage (the share of lit pixels) and the mean colour of
the two halves, loosely. Two runs, both on lavapipe through Headless.sh:

  1. compute on: the log must say "Compute shaders: supported" and "GPU particles: first compute
     dispatch", and the halves must agree. A ParticleSim.comp that writes nothing leaves the right
     half black: the dispatch is logged and the halves still differ.
  2. a hardware profile with "Compute": false (default "2004 PC"), enforced: no dispatch may be
     logged (the GPU systems took the CPU path) and the halves must agree again, which catches a
     broken fallback.

ADDING A CASE: append an entry to CASES below (a name, and the ParticleSystem fields that differ
from DEFAULTS, plus "Parent" settings if the emitter should sit on a moving parent), then run
  Engine/Tools/GpuParticlesCompare.py --write-scene
and commit the regenerated Assets/Scenes/Tests/GpuParticlesCompare.lvl. The checker tests that the
scene matches this table, so a forgotten regeneration fails. Nothing else changes: the rows, the
camera and the comparison follow the table. A case that must differ between the paths on purpose
can set "Loose": a factor that widens its tolerances (see CASES).

  Engine/Tools/GpuParticlesCompare.py --game .build/Game_linux_Debug/Game_EntryPoint_x64 --wrapper Engine/Tools/Headless.sh
Run it from the project root, like ScreenshotRegression.py. Exits non-zero on any failure.
"""
import argparse
import json
import math
import os
import shlex
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ScreenshotRegression import read_png  # noqa: E402

SCENE = "Assets/Scenes/Tests/GpuParticlesCompare.lvl"      # as the game names it (a VFS path)
SCENE_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", SCENE)     # where it lives in the engine
DEFAULT_PROFILE = "2004 PC"          # a built-in profile with "Compute": false

FRAMES = 100                         # at 1/60 s: 1.67 s into the emitters' lives
WIDTH, HEIGHT = 576, 960
FOV = 55.0                           # the camera's FieldOfView: vertical field of view in degrees
ROW_PITCH = 3.6                      # metres between rows (and the height of one row's band)
COLUMN_X = 2.7                       # CPU emitters at -COLUMN_X, GPU at +COLUMN_X

# Every case's starting point. Fields are the ParticleSystem's own (Engine/Source/Components/Effects).
DEFAULTS = {
    "Looping": True, "EmissionRate": 300, "BurstCount": 0, "MaxParticles": 6000,
    "SimulationSpace": "World", "Shape": "Cone", "ShapeRadius": 0.25, "ConeAngle": 20, "EmitFromShell": False,
    "Lifetime": [1.0, 1.5], "Speed": [1.5, 2.5], "Size": [0.08, 0.14], "Rotation": [0, 0], "AngularVelocity": [0, 0],
    "StartColor": [1.0, 0.5, 0.15], "StartColor2": [1.0, 0.8, 0.3], "StartAlpha": 1.0, "Intensity": 1.0,
    "EndSize": 0.6, "EndColor": [0.6, 0.15, 0.05], "EndAlpha": 0.0, "FadeIn": 0.1,
    "Gravity": 0.0, "Drag": 0.2, "NoiseStrength": 0.3, "NoiseFrequency": 1.0,
    "Blend": "Additive", "Alignment": "Billboard", "StretchFactor": 0.1, "SoftParticleDistance": 0.0,
}

# "Y" is the emitter's height inside its row's band (0 = the band's centre). "Rotation": the emitter's
# Euler angles in degrees (default none: the cone points up); "AimAtCamera": point the cone's axis at the
# camera instead, from each emitter's own position, so the two halves stay mirror images. "Parent": a parent entity
# the emitter sits on, swinging along x between -Swing and +Swing over Seconds (a Sequence).
CASES = [
    {"Name": "Cone fire", "Y": -1.3,
     "System": {"EmissionRate": 500, "Gravity": -0.2}},
    {"Name": "Sphere shell burst", "Y": 0.0,
     "System": {"Looping": False, "Duration": 0.05, "EmissionRate": 0, "BurstCount": 2500, "Shape": "Sphere", "ShapeRadius": 0.4,
                "EmitFromShell": True, "Lifetime": [3.0, 4.0], "Speed": [0.6, 1.0], "Drag": 1.0, "NoiseStrength": 0.0,
                "StartColor": [0.3, 0.8, 1.0], "StartColor2": [0.6, 0.5, 1.0], "EndColor": [0.1, 0.2, 0.6]}},
    {"Name": "Stretched sparks", "Y": 0.4,
     "System": {"EmissionRate": 400, "ConeAngle": 45, "Speed": [2.5, 4.0], "Lifetime": [0.5, 0.9], "Size": [0.04, 0.07],
                "Gravity": 1.0, "Drag": 0.0, "NoiseStrength": 0.0, "Alignment": "Stretched", "StretchFactor": 0.12,
                "StartColor": [1.0, 0.9, 0.5], "StartColor2": [1.0, 1.0, 0.8], "EndColor": [1.0, 0.4, 0.1], "EndSize": 1.0}},
    {"Name": "Local space on a moving parent", "Y": -0.8,
     "Parent": {"Swing": 0.9, "Seconds": 3.0},
     "System": {"SimulationSpace": "Local", "EmissionRate": 400, "Speed": [1.0, 1.8], "Lifetime": [1.0, 1.4], "Gravity": -0.1,
                "StartColor": [0.5, 1.0, 0.4], "StartColor2": [0.8, 1.0, 0.5], "EndColor": [0.1, 0.5, 0.2]}},
    # ME-381: alpha blending sorted back to front. The cone points at the camera and the colour runs
    # from red (young, far) to blue (old, near), nearly opaque: sorted, the old blue particles cover the
    # young red ones; drawn in birth order (no sort), red would cover blue and the mean colour flips.
    {"Name": "Alpha toward the camera", "Y": 0.0, "AimAtCamera": True,
     "System": {"Blend": "Alpha", "EmissionRate": 250, "ConeAngle": 25, "Speed": [1.0, 1.4], "Lifetime": [1.4, 1.8],
                "Size": [0.3, 0.4], "EndSize": 1.0, "FadeIn": 0.0, "StartAlpha": 1.0, "EndAlpha": 0.9, "Drag": 0.0, "NoiseStrength": 0.0,
                "StartColor": [1.0, 0.1, 0.05], "StartColor2": [1.0, 0.2, 0.1], "EndColor": [0.05, 0.2, 1.0]}},
    # The scene gives every case a CPU and a GPU emitter: set "Simulation" only to override the GPU one.
]

# Tolerances. Both paths draw the same particles with different random draws, so coverage and mean
# colour agree to within about 10% in practice (ME-377: 8.0 vs 8.2% lit, means within 3%); these are wide enough
# not to flake and narrow enough that a missing half (0 against 10%) is far outside them.
MIN_COVERAGE = 0.01                  # the CPU half must light at least this share of its band
COVERAGE_RATIO = (0.7, 1.0 / 0.7)    # GPU coverage / CPU coverage
COLOUR_RATIO = (0.7, 1.0 / 0.7)      # GPU mean channel / CPU mean channel, each channel that matters
CENTROID = 0.04                      # the light's centre of mass may differ by this share of the image width / row height
LIT = 12                             # a pixel is lit when its brightest channel exceeds this (0-255)


# -------------------------------------------------------------------------------------- scene

def entity_json(guid, name, components, parent=None):
    entity = {"GUID": guid, "Name": name, "DestroyOnLoad": True, "Components": components}
    if parent:
        entity["Parent"] = parent
    return entity


def transform(position, rotation=(0, 0, 0)):
    return {"Type": "Transform", "Position": list(position), "Rotation": list(rotation), "Scale": [1, 1, 1]}


def row_height(index, count):
    """World y of the centre of row `index` (0 = top)."""
    return (count - 1) / 2.0 * ROW_PITCH - index * ROW_PITCH


def camera_distance(count):
    visible = count * ROW_PITCH + 1.0
    return visible / 2.0 / math.tan(math.radians(FOV) / 2.0)


def aim_rotation(position, camera):
    """Euler degrees (as Transform reads them: glm's x, y, z) that turn the emitter's +Y toward camera."""
    d = [c - p for c, p in zip(camera, position)]
    length = math.sqrt(sum(v * v for v in d))
    dx, dy, dz = (v / length for v in d)
    # glm::quat(euler) with z = 0 turns +Y into (sin x sin y, cos x, sin x cos y).
    return [round(-math.degrees(math.acos(dy)), 4), round(math.degrees(math.atan2(-dx, -dz)), 4), 0]


def build_scene():
    count = len(CASES)
    entities = [
        entity_json("a000000000000001", "Camera", [
            transform((0, 0, -camera_distance(count))),
            {"Type": "Camera", "ClearType": "Color", "ClearColor": [0, 0, 0], "IsCurrent": True, "Near": 0.1, "Far": 100.0, "FieldOfView": FOV},
        ]),
    ]
    guid = 0x100
    tracks = []
    for index, case in enumerate(CASES):
        centre = row_height(index, count)
        for side, simulation in (("CPU", "CPU"), ("GPU", "GPU")):
            x = -COLUMN_X if side == "CPU" else COLUMN_X
            system = dict(DEFAULTS)
            system.update(case.get("System", {}))
            system.setdefault("Simulation", simulation)
            if side == "CPU":
                system["Simulation"] = "CPU"
            name = f"{case['Name']} {side}"
            parent_guid = None
            position = (x, centre + case.get("Y", 0.0), 0)
            if "Parent" in case:
                guid += 1
                parent_guid = f"b{guid:015x}"
                entities.append(entity_json(parent_guid, name + " parent", [transform((x, centre, 0))]))
                position = (0, case.get("Y", 0.0), 0)
                swing, seconds = case["Parent"]["Swing"], case["Parent"]["Seconds"]
                frames = int(seconds * 30)
                tracks.append({"Target": parent_guid, "Component": "Transform", "Property": "Position", "Keys": [
                    {"Frame": 0, "Value": [x - swing, centre, 0, 0], "Interpolation": "SmoothStep"},
                    {"Frame": frames // 2, "Value": [x + swing, centre, 0, 0], "Interpolation": "SmoothStep"},
                    {"Frame": frames, "Value": [x - swing, centre, 0, 0], "Interpolation": "SmoothStep"}]})
            guid += 1
            emitter = {"Type": "ParticleSystem"}
            emitter.update(system)
            rotation = case.get("Rotation", (0, 0, 0))
            if case.get("AimAtCamera"):
                rotation = aim_rotation(position, (0, 0, -camera_distance(count)))
            entities.append(entity_json(f"c{guid:015x}", name, [transform(position, rotation), emitter], parent_guid))
    if tracks:
        longest = max(int(c["Parent"]["Seconds"] * 30) for c in CASES if "Parent" in c)
        entities.append(entity_json("a000000000000002", "Parent motion", [
            transform((0, 0, 0)),
            {"Type": "Sequence", "Name": "Swing", "FrameRate": 30, "DurationFrames": longest, "PlayOnStart": True, "Loop": True, "Tracks": tracks}]))
    return {"Version": 2, "Cores": [], "Entities": entities}


def scene_text():
    return json.dumps(build_scene(), indent=4) + "\n"


# -------------------------------------------------------------------------------------- run

def run_game(args, out, name, extra):
    capture = os.path.join(out, name + ".png")
    log_path = os.path.join(out, name + ".log")
    if os.path.exists(capture):
        os.remove(capture)
    wrapper, game = shlex.split(args.wrapper), args.game
    if args.cwd:
        capture, wrapper, game = os.path.abspath(capture), [os.path.abspath(t) if os.path.exists(t) else t for t in wrapper], os.path.abspath(game)
    command = wrapper + [game, "--scene", args.scene, "--frames", str(FRAMES), "--frame-time", str(1.0 / 60.0),
                                           "--width", str(WIDTH), "--height", str(HEIGHT), "--no-audio", "--screenshot", capture, "--exit"] + shlex.split(args.game_args) + extra
    with open(log_path, "w") as log:
        result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, cwd=args.cwd or None)
    with open(log_path, errors="replace") as log:
        text = log.read()
    return capture, text, result.returncode, log_path


def measure(image, count):
    """Per row: (coverage, mean colour, centre of mass) of the left half (CPU) and the right half (GPU).
    The centre of mass is the brightness-weighted position in pixels, from the half's left and the row's top."""
    width, height, rows = image
    visible = count * ROW_PITCH + 1.0
    results = []
    for index in range(count):
        top = int(round((0.5 - (row_height(index, count) + ROW_PITCH / 2) / visible) * height))
        bottom = int(round((0.5 - (row_height(index, count) - ROW_PITCH / 2) / visible) * height))
        halves = []
        for x0, x1 in ((0, width // 2), (width // 2, width)):
            lit, total, sums, weight, mx, my = 0, 0, [0, 0, 0], 0.0, 0.0, 0.0
            for y in range(max(top, 0), min(bottom, height)):
                row = rows[y]
                for x in range(x0, x1):
                    r, g, b = row[x * 3], row[x * 3 + 1], row[x * 3 + 2]
                    lit += 1 if max(r, g, b) > LIT else 0
                    sums[0] += r
                    sums[1] += g
                    sums[2] += b
                    total += 1
                    brightness = r + g + b
                    weight += brightness
                    mx += brightness * x
                    my += brightness * (y - top)
            centre = (mx / weight, my / weight) if weight > 0 else (0.0, 0.0)
            halves.append((lit / max(total, 1), [s / max(total, 1) for s in sums], centre))
        results.append(halves)
    return results


def compare(case, halves):
    """Returns a list of problems (empty: the halves agree)."""
    (cpu_cover, cpu_mean, cpu_centre), (gpu_cover, gpu_mean, gpu_centre) = halves
    loose = case.get("Loose", 1.0)
    problems = []
    if cpu_cover < MIN_COVERAGE:
        problems.append(f"the CPU half is nearly empty ({cpu_cover * 100:.2f}% lit)")
        return problems
    ratio = gpu_cover / cpu_cover
    low, high = COVERAGE_RATIO[0] / loose, COVERAGE_RATIO[1] * loose
    if not low <= ratio <= high:
        problems.append(f"coverage GPU/CPU {ratio:.2f} outside {low:.2f}..{high:.2f} ({gpu_cover * 100:.2f}% vs {cpu_cover * 100:.2f}%)")
    for channel, label in enumerate("RGB"):
        if cpu_mean[channel] < 0.5:
            continue
        ratio = gpu_mean[channel] / cpu_mean[channel]
        low, high = COLOUR_RATIO[0] / loose, COLOUR_RATIO[1] * loose
        if not low <= ratio <= high:
            problems.append(f"mean {label} GPU/CPU {ratio:.2f} outside {low:.2f}..{high:.2f} ({gpu_mean[channel]:.2f} vs {cpu_mean[channel]:.2f})")
    # The emitters sit COLUMN_X either side of the image's centre line: measure x from each one's own column.
    column = COLUMN_X * HEIGHT / (len(CASES) * ROW_PITCH + 1.0)
    cpu_centre = (cpu_centre[0] - (WIDTH / 2 - column), cpu_centre[1])
    gpu_centre = (gpu_centre[0] - (WIDTH / 2 + column), gpu_centre[1])
    for axis, label, size in ((0, "x", WIDTH // 2), (1, "y", HEIGHT // len(CASES))):
        offset = abs(gpu_centre[axis] - cpu_centre[axis]) / size
        if offset > CENTROID * loose:
            problems.append(f"the light's centre differs by {offset * 100:.1f}% in {label} (limit {CENTROID * loose * 100:.1f}%): "
                            f"CPU {cpu_centre[axis]:.0f} px, GPU {gpu_centre[axis]:.0f} px from the emitter's column / the row's top")
    return problems


def report(title, measurements):
    for case, halves in zip(CASES, measurements):
        (cc, cm, cp), (gc, gm, gp) = halves
        print(f"      {case['Name']}: CPU {cc * 100:.1f}% lit, mean ({cm[0]:.1f},{cm[1]:.1f},{cm[2]:.1f}), centre ({cp[0]:.0f},{cp[1]:.0f}); "
              f"GPU {gc * 100:.1f}% lit, mean ({gm[0]:.1f},{gm[1]:.1f},{gm[2]:.1f}), centre ({gp[0]:.0f},{gp[1]:.0f})")


def check_phase(args, out, name, extra, expect_dispatch):
    """One run of the scene. Returns the number of failures."""
    failures = 0
    capture, text, code, log_path = run_game(args, out, name, extra)
    label = f"{name} ({'compute on' if expect_dispatch else 'Compute: false profile'})"
    if code != 0 or not os.path.exists(capture):
        print(f"FAIL  {label}: the game exited with {code} without a capture (see {log_path})")
        return 1
    dispatched = "GPU particles: first compute dispatch" in text
    if expect_dispatch:
        if "Compute shaders: supported" not in text:
            print(f"FAIL  {label}: the log doesn't say \"Compute shaders: supported\" (see {log_path})")
            failures += 1
        if not dispatched:
            print(f"FAIL  {label}: no \"GPU particles: first compute dispatch\" in the log (see {log_path})")
            failures += 1
    else:
        if dispatched:
            print(f"FAIL  {label}: a compute dispatch ran although the profile turns compute off (see {log_path})")
            failures += 1
        if "Hardware profile: " + args.profile not in text:
            print(f"FAIL  {label}: the log doesn't show the profile \"{args.profile}\" (see {log_path})")
            failures += 1
    measurements = measure(read_png(capture), len(CASES))
    bad = 0
    for case, halves in zip(CASES, measurements):
        for problem in compare(case, halves):
            print(f"FAIL  {label} / {case['Name']}: {problem}")
            bad += 1
    if bad:
        report(label, measurements)
    elif failures == 0:
        print(f"ok    {label}: {len(CASES)} cases, CPU and GPU halves agree")
        if args.verbose:
            report(label, measurements)
    return failures + bad


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--game", help="game executable")
    parser.add_argument("--scene", default=SCENE)
    parser.add_argument("--wrapper", default="", help="command prefix, e.g. Engine/Tools/Headless.sh")
    parser.add_argument("--game-args", default="", help="extra game arguments for both runs, e.g. \"--mod-dir DIR\" to swap a shader")
    parser.add_argument("--cwd", default="", help="run the game in this project directory (paths below are made absolute)")
    parser.add_argument("--profile", default=DEFAULT_PROFILE, help="hardware profile with Compute off")
    parser.add_argument("--out", default=".tmp/GpuParticlesCompare")
    parser.add_argument("--write-scene", action="store_true", help="regenerate the scene from CASES and exit")
    parser.add_argument("--verbose", action="store_true")
    args = parser.parse_args()
    if args.write_scene:
        os.makedirs(os.path.dirname(SCENE_FILE), exist_ok=True)
        with open(SCENE_FILE, "w") as f:
            f.write(scene_text())
        print(f"WROTE {os.path.normpath(SCENE_FILE)} ({len(CASES)} cases)")
        return 0
    if not args.game:
        parser.error("--game is required")
    os.makedirs(args.out, exist_ok=True)
    failures = 0
    if args.scene == SCENE:
        try:
            with open(SCENE_FILE) as f:
                current = f.read()
        except OSError:
            current = None
        if current != scene_text():
            print(f"FAIL  {os.path.normpath(SCENE_FILE)} doesn't match CASES in {os.path.basename(__file__)}: run it with --write-scene")
            failures += 1
    failures += check_phase(args, args.out, "compute", ["--no-profile-enforcement"], True)
    failures += check_phase(args, args.out, "nocompute", ["--hardware-profile", args.profile, "--enforce-profile"], False)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
