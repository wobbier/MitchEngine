#!/usr/bin/env python3
"""Builds each tutorial's game and runs its play-mode tests (DOC-2).

For every Engine/Tests/Tutorials/<Name>/Tutorial.json:
  1. each .cs / .rml file in the folder must appear, verbatim, on the tutorial's page (Doc), so the
     page and the tested code can't drift apart;
  2. a project is created from the tutorial's Template outside the repo (Havana --new-project);
  3. the files are copied in (*Tests.cs to Assets/Scripts/Tests, other .cs to Assets/Scripts; Setup.py
     places anything else) and Setup.py makes the scene edits the page describes;
  4. the editor plays the scene and runs `console game-tests`; the log must show Expect.

Run inside the dev shell from the repo root (RunChecks does):
  Engine/Tools/RunTutorialChecks.py [--editor .build/Editor_Debug/Havana] [--only CoinHunt]
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ENGINE = os.path.dirname(HERE)


def check_page(folder, doc_path):
    with open(doc_path, encoding="utf-8") as f:
        page = f.read()
    missing = []
    for name in sorted(os.listdir(folder)):
        if os.path.splitext(name)[1] not in (".cs", ".rml"):
            continue
        with open(os.path.join(folder, name), encoding="utf-8") as f:
            text = f.read().strip()
        if text not in page:
            missing.append(name)
    return missing


def run(folder, editor, timeout):
    with open(os.path.join(folder, "Tutorial.json")) as f:
        spec = json.load(f)
    name = spec["Name"]
    missing = check_page(folder, os.path.join(ENGINE, spec["Doc"]))
    if missing:
        print(f"FAIL  {name}: {', '.join(missing)} no longer appear verbatim in {spec['Doc']}")
        return False
    log_path = os.path.abspath(os.path.join(".tmp", f"Tutorial-{name}.log"))
    os.makedirs(os.path.dirname(log_path), exist_ok=True)
    work = tempfile.mkdtemp(prefix="tutorial-")
    try:
        wrapper = os.path.join(HERE, "Headless.sh")
        with open(log_path, "w") as log:
            created = subprocess.run([wrapper, editor, "--new-project", spec["Template"], "--name", name], cwd=work, stdout=log, stderr=subprocess.STDOUT, timeout=timeout)
        project = os.path.join(work, name)
        if created.returncode != 0 or not os.path.isdir(project):
            print(f"FAIL  {name}: couldn't create it from {spec['Template']} (see {log_path})")
            return False
        for file in sorted(os.listdir(folder)):
            if file.endswith(".cs"):
                target = os.path.join(project, "Assets", "Scripts", "Tests" if file.endswith("Tests.cs") else "", file)
                os.makedirs(os.path.dirname(target), exist_ok=True)
                shutil.copy(os.path.join(folder, file), target)
        subprocess.run([sys.executable, os.path.join(folder, "Setup.py"), project], check=True)
        flow = os.path.join(work, "Tutorial.edscript")
        with open(flow, "w") as f:
            f.write("wait 20\nplay\nassert-playing 1\nwait 60\nconsole game-tests\n")
            f.write(f"wait-log {spec['Expect']} | 3000\nstop\n")
        with open(log_path, "a") as log:
            subprocess.run([wrapper, editor, "--project", project, "--editor-exec", flow], stdout=log, stderr=subprocess.STDOUT, timeout=timeout)
        with open(log_path, errors="replace") as f:
            text = f.read()
        if "[editor-exec] PASSED" not in text:
            for line in text.splitlines():
                if "[GameTest] FAIL" in line or "error CS" in line or "[editor-exec] FAIL" in line:
                    print("      " + line.strip())
            print(f"FAIL  {name}: the game tests didn't report '{spec['Expect']}' (see {log_path})")
            return False
        print(f"ok    {name} ({spec['Template']}): {spec['Expect']}")
        return True
    except subprocess.TimeoutExpired:
        print(f"FAIL  {name}: timed out (see {log_path})")
        return False
    finally:
        shutil.rmtree(work, ignore_errors=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--editor", default=".build/Editor_Debug/Havana")
    parser.add_argument("--only", default="", help="run tutorials whose folder name contains this")
    parser.add_argument("--timeout", type=int, default=1500, help="seconds per editor run")
    args = parser.parse_args()
    editor = os.path.abspath(args.editor)
    root = os.path.join(ENGINE, "Tests", "Tutorials")
    folders = [os.path.join(root, d) for d in sorted(os.listdir(root)) if os.path.isfile(os.path.join(root, d, "Tutorial.json")) and args.only in d]
    results = [run(folder, editor, args.timeout) for folder in folders]
    passed = sum(results)
    print(f"{passed}/{len(results)} tutorials passed")
    return 0 if passed == len(results) else 1


if __name__ == "__main__":
    sys.exit(main())
