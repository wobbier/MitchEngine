#!/usr/bin/env python3
"""Runs every editor regression script (*.edscript) in the editor and reports which pass.

Each script names its scene in its header (the usage line: --scene <file>). From the project root:

    Engine/Tools/RunEditorFlows.py --editor .build/Editor_Debug/Havana --wrapper Engine/Tools/Headless.sh
    Engine/Tools/RunEditorFlows.py ... --only Nav          # scripts whose path contains "Nav"

A flow passes when the editor logs "[editor-exec] PASSED" and exits cleanly. Logs land in
.tmp/EditorFlows/<script>.log. Flows that edit files (replace-in-file) are skipped while another
Havana is running, since that editor would hot reload the edits; --force runs them anyway.
Exits 1 when any flow fails.
"""
import argparse
import glob
import os
import re
import shlex
import subprocess
import sys
import time


def scene_of(script):
    with open(script, encoding="utf-8") as f:
        head = "".join(f.readline() for _ in range(6))
    match = re.search(r"--scene\s+(\S+)", head)
    return match.group(1) if match else None


def editor_running():
    try:
        return subprocess.run(["pgrep", "-x", "Havana"], capture_output=True).returncode == 0
    except FileNotFoundError:
        return False


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--editor", required=True, help="editor executable")
    parser.add_argument("--wrapper", default="", help="command prefix, e.g. Engine/Tools/Headless.sh")
    parser.add_argument("--root", default="Assets/Scenes", help="where to look for *.edscript")
    parser.add_argument("--only", default="", help="only scripts whose path contains this")
    parser.add_argument("--timeout", type=int, default=900, help="seconds per flow")
    parser.add_argument("--logs", default=".tmp/EditorFlows")
    parser.add_argument("--force", action="store_true", help="run file-editing flows even with another editor open")
    args = parser.parse_args()

    os.makedirs(args.logs, exist_ok=True)
    scripts = sorted(glob.glob(os.path.join(args.root, "**", "*.edscript"), recursive=True))
    scripts = [s for s in scripts if args.only in s]
    if not scripts:
        print("No editor scripts found under", args.root)
        return 1

    other_editor = editor_running()
    failures = 0
    for script in scripts:
        name = os.path.splitext(os.path.basename(script))[0]
        scene = scene_of(script)
        if not scene:
            print(f"SKIP  {name}: no --scene in its header")
            continue
        with open(script, encoding="utf-8") as f:
            edits_files = "replace-in-file" in f.read()
        if edits_files and other_editor and not args.force:
            print(f"SKIP  {name}: edits files while another Havana is open (--force to run)")
            continue
        log_path = os.path.join(args.logs, name + ".log")
        command = shlex.split(args.wrapper) + [args.editor, "--scene", scene, "--editor-exec", script]
        start = time.time()
        try:
            with open(log_path, "w") as log:
                code = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, timeout=args.timeout).returncode
        except subprocess.TimeoutExpired:
            code = None
        seconds = time.time() - start
        with open(log_path, errors="replace") as log:
            text = log.read()
        passed = "[editor-exec] PASSED" in text and code == 0
        if passed:
            print(f"ok    {name} ({seconds:.0f} s)")
        else:
            failures += 1
            reason = "timed out" if code is None else f"exit {code}"
            print(f"FAIL  {name} ({reason}, {seconds:.0f} s): see {log_path}")
            for line in re.findall(r"\[editor-exec\] FAIL[^\n]*", text)[:8]:
                print("      " + line)
    print(f"{len(scripts) - failures}/{len(scripts)} editor flows passed" if failures else f"All {len(scripts)} editor flows passed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
