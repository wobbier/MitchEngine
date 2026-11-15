#!/usr/bin/env python3
"""Lists direct writes to a material's draw-state fields (ME-399), Linux only.

Retained mesh commands skip re-reading a material while its revision has not moved, so every write
to a field a mesh command takes from it (Material.h / StandardMaterial.h / FoliageMaterial.h, the
fields marked ME_MATERIAL_FIELD) must go through a setter or be followed by MarkChanged(). This
compiles every C++ file that mentions a material with -fsyntax-only and ME_MATERIAL_FIELD=const, so
each direct write is a compile error. Nothing is built or written.

Usage (from the game or engine root, after GenerateProject, inside the dev shell):
    ./Engine/Tools/NixEnv.sh env -u NIX_CC python3 Engine/Tools/AuditMaterialWrites.py [config] [jobs]
config defaults to editor_debug. Known exception: RenderCore's terrain patch material (never a
retained command). Material classes write their own fields through Material::Writable, so they
don't show; assigning one whole material over another shows as a deleted operator= (that is
tracked: MaterialRevision's assignment bumps).
"""
import concurrent.futures
import os
import re
import subprocess
import sys


def main():
    config = sys.argv[1] if len(sys.argv) > 1 else "editor_debug"
    jobs = int(sys.argv[2]) if len(sys.argv) > 2 else max(1, (os.cpu_count() or 4) // 2)
    root = os.getcwd()
    solutions = [f for f in os.listdir(root) if f.endswith(".make") and not f.endswith("_Linux.make")]
    if not solutions:
        sys.exit("No <Solution>.make here: run from the project root after GenerateProject.")
    projects = set()
    for solution in solutions:
        for line in open(os.path.join(root, solution)):
            match = re.search(r'-C "([^"]+)" -f "([^"]+)"', line)
            if match:
                projects.add(match.groups())
    commands = []
    for directory, makefile in sorted(projects):
        if "ThirdParty" in directory:
            continue
        cwd = os.path.join(root, directory)
        dry = subprocess.run(["make", "-n", "-B", "-f", makefile, f"config={config}"], cwd=cwd, capture_output=True, text=True)
        for line in dry.stdout.splitlines():
            source = re.search(r' -c "([^"]+\.cpp)"', line)
            if not source or not re.match(r'\s*(g\+\+|c\+\+|clang\+\+) ', line):
                continue
            path = os.path.normpath(os.path.join(cwd, source.group(1)))
            try:
                text = open(path, errors="ignore").read()
            except OSError:
                continue
            if not re.search(r"Material|Foliage", text):
                continue
            command = line.replace(f' -c "{source.group(1)}"', f' -fsyntax-only "{source.group(1)}"')
            command = re.sub(r' -o ("[^"]+"|\S+)', "", command)
            command = re.sub(r" -MMD| -MP| -MF (\"[^\"]+\"|\S+)", "", command)
            command = re.sub(r"^(\s*\S+)", r"\1 -DME_MATERIAL_FIELD=const", command, count=1)
            commands.append((cwd, command))

    def run(entry):
        cwd, command = entry
        result = subprocess.run(command, shell=True, cwd=cwd, capture_output=True, text=True)
        found = []
        for line in result.stderr.splitlines():
            if ": error:" in line:
                path, rest = line.split(":", 1)
                found.append(os.path.relpath(os.path.normpath(os.path.join(cwd, path)), root) + ":" + rest)
        return found

    errors = set()
    with concurrent.futures.ThreadPoolExecutor(jobs) as pool:
        for found in pool.map(run, commands):
            errors.update(found)
    for error in sorted(errors):
        print(error)
    print(f"{len(commands)} files checked, {len(errors)} direct writes", file=sys.stderr)


if __name__ == "__main__":
    main()
