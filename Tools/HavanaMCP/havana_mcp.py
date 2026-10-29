#!/usr/bin/env python3
"""Havana MCP server (NEXT-1): lets an agent (Claude Code, any MCP client) build and check games
with the editor, through the automation the engine already has.

It speaks MCP over stdio (newline-delimited JSON-RPC 2.0, protocol 2024-11-05) with no dependencies.
Every tool runs from the project root (the folder with Project/Game.meproj, or the working
directory). Editor sessions are headless (Engine/Tools/Headless.sh: a private Xvfb display), one
per call, unattended, so nothing touches the user's own editor, layout or preferences.

Tools:
  run_edscript      run an edscript (Engine/Docs/Editor-Havana.md, "Automation") in a scene: edits,
                    play/stop, asserts, save-as, screenshots; returns pass/fail, failures, the log
  screenshot        a PNG of a scene in the editor (optionally after play for some frames)
  scene_summary     a scene file's entities and components (no editor needed)
  list_assets       project files under a folder, filtered by extension
  edscript_commands every edscript command with its usage comment
  build_game        Havana --build-game [profile]: a shippable build; returns the result and log tail
  read_log          the tail of a log file the tools wrote (.tmp/...)

Register it with Claude Code from the project root:  claude mcp add havana -- python3 Engine/Tools/HavanaMCP/havana_mcp.py
(the repo's .mcp.json does the same). HAVANA_EDITOR overrides the editor binary.
"""
import base64
import json
import os
import re
import subprocess
import sys
import tempfile

PROTOCOL = "2024-11-05"
HERE = os.path.dirname(os.path.abspath(__file__))
ENGINE = os.path.normpath(os.path.join(HERE, "..", ".."))
ROOT = os.path.normpath(os.path.join(ENGINE, "..")) if os.path.basename(ENGINE) == "Engine" else os.getcwd()
EDITOR = os.environ.get("HAVANA_EDITOR", os.path.join(ROOT, ".build", "Editor_Debug", "Havana"))
HEADLESS = os.path.join(ENGINE, "Tools", "Headless.sh")
SCRATCH = os.path.join(ROOT, ".tmp", "MCP")


def editor_command(args):
    command = [EDITOR] + args
    if sys.platform.startswith("linux") and os.path.exists(HEADLESS):
        command = [HEADLESS] + command
        # An MCP client rarely starts servers inside the dev shell the editor's libraries come
        # from: enter it for the session (nix caches the evaluation, so it's quick after the first).
        flake = os.path.join(ENGINE, "nix")
        if not os.environ.get("IN_NIX_SHELL") and os.path.exists(os.path.join(flake, "flake.nix")):
            command = ["nix", "develop", flake, "--command"] + command
    return command


def run_editor(args, timeout):
    env = dict(os.environ)
    env.pop("WAYLAND_DISPLAY", None)
    try:
        done = subprocess.run(editor_command(args), cwd=ROOT, env=env, capture_output=True, text=True, timeout=timeout)
        return done.returncode, done.stdout + done.stderr
    except subprocess.TimeoutExpired as e:
        out = (e.stdout or b"").decode(errors="replace") if isinstance(e.stdout, bytes) else (e.stdout or "")
        return None, out + "\n(timed out)"
    except FileNotFoundError:
        return -1, f"No editor at {EDITOR}: build it (make -f MitchGame.make Havana config=editor_debug) or set HAVANA_EDITOR"


def interesting(log, limit=120):
    keep = [line for line in log.splitlines()
            if re.search(r"editor-exec|Error|Warning|\[Debug\]|Saved screenshot|SaveGame:|Loading:|Settings:|UI: |Layout:|Tour:|Help:", line)]
    return "\n".join(keep[-limit:])


def tool_run_edscript(args):
    os.makedirs(SCRATCH, exist_ok=True)
    script = args.get("script", "")
    if "quit" not in [line.strip() for line in script.splitlines()]:
        script = script.rstrip() + "\nquit\n"
    with tempfile.NamedTemporaryFile("w", suffix=".edscript", dir=SCRATCH, delete=False) as f:
        f.write(script)
        path = f.name
    result_path = path + ".result.json"
    cli = ["--editor-exec", path, "--editor-exec-result", result_path]
    if args.get("scene"):
        cli = ["--scene", args["scene"]] + cli
    code, log = run_editor(cli, int(args.get("timeout_s", 600)))
    result = {}
    if os.path.exists(result_path):
        with open(result_path) as f:
            result = json.load(f)
    passed = "[editor-exec] PASSED" in log
    summary = {"passed": passed, "exit": code, "failures": result.get("Failures", re.findall(r"\[editor-exec\] FAIL[^\n]*", log))}
    return [{"type": "text", "text": json.dumps(summary, indent=2) + "\n\n" + interesting(log)}], not passed


def tool_screenshot(args):
    os.makedirs(SCRATCH, exist_ok=True)
    shot = os.path.join(SCRATCH, "screenshot.png")
    if os.path.exists(shot):
        os.remove(shot)
    lines = ["wait 20"]
    if args.get("play_frames"):
        lines += ["play", f"wait {int(args['play_frames'])}"]
    lines += [f"screenshot {shot}", "wait 3", "quit"]
    content, failed = tool_run_edscript({"script": "\n".join(lines), "scene": args.get("scene"), "timeout_s": args.get("timeout_s", 600)})
    if not os.path.exists(shot):
        return content, True
    with open(shot, "rb") as f:
        data = base64.b64encode(f.read()).decode()
    return [{"type": "image", "data": data, "mimeType": "image/png"}], False


def tool_scene_summary(args):
    path = os.path.join(ROOT, args["scene"])
    with open(path) as f:
        scene = json.load(f)
    entities = scene.get("Entities") or scene.get("Scene") or []
    lines = [f"{args['scene']}: {len(entities)} entities"]
    for entity in entities:
        parent = f" (child of {entity['Parent']})" if entity.get("Parent") else ""
        components = ", ".join(c.get("Type", "?") + (f"[{c['ScriptName']}]" if c.get("ScriptName") else "") for c in entity.get("Components", []))
        lines.append(f"- {entity.get('Name', '?')} {entity.get('GUID', '')}{parent}: {components}")
    return [{"type": "text", "text": "\n".join(lines)}], False


def tool_list_assets(args):
    folder = os.path.join(ROOT, args.get("folder", "Assets"))
    extensions = [e if e.startswith(".") else "." + e for e in args.get("extensions", [])]
    found = []
    for dirpath, _, files in os.walk(folder):
        for name in files:
            if name.endswith(".meta") or (extensions and not any(name.endswith(e) for e in extensions)):
                continue
            found.append(os.path.relpath(os.path.join(dirpath, name), ROOT))
            if len(found) >= int(args.get("limit", 500)):
                break
    return [{"type": "text", "text": "\n".join(sorted(found)) or "(none)"}], False


def tool_edscript_commands(args):
    source = os.path.join(ENGINE, "Modules", "Havana", "Source", "Editor", "EditorAutomation.cpp")
    with open(source, encoding="utf-8") as f:
        lines = f.read().splitlines()
    out = []
    for i, line in enumerate(lines):
        names = re.findall(r'command == "([a-z0-9-]+)"', line)
        if not names or "if(" not in line.replace(" ", ""):
            continue
        usage = ""
        for follow in lines[i + 1:i + 6]:
            comment = re.match(r"\s*// (.*)", follow)
            if comment:
                usage += ("; " if usage else "") + comment.group(1)
            elif follow.strip() not in ("{", ""):
                break
        out.append(f"{', '.join(names)}: {usage}" if usage else ", ".join(names))
    return [{"type": "text", "text": "\n".join(out)}], False


def tool_build_game(args):
    cli = ["--build-game"] + ([args["profile"]] if args.get("profile") else [])
    code, log = run_editor(cli, int(args.get("timeout_s", 3600)))
    tail = "\n".join(log.splitlines()[-60:])
    return [{"type": "text", "text": f"exit {code}\n{tail}"}], code != 0


def tool_read_log(args):
    path = os.path.normpath(os.path.join(ROOT, args["path"]))
    if not path.startswith(os.path.join(ROOT, ".tmp")):
        return [{"type": "text", "text": "Only logs under .tmp/ can be read"}], True
    with open(path, errors="replace") as f:
        lines = f.read().splitlines()
    return [{"type": "text", "text": "\n".join(lines[-int(args.get("lines", 200)):])}], False


TOOLS = {
    "run_edscript": (tool_run_edscript, "Run an edscript in a headless Havana (one command per frame: select, set, create, play, stop, wait, wait-log, assert-*, screenshot, save-as...). Returns pass/fail, failures and the log's key lines.",
                     {"script": {"type": "string", "description": "edscript lines"}, "scene": {"type": "string", "description": "scene to open, e.g. Assets/Scenes/Showcase/Playground.lvl"}, "timeout_s": {"type": "integer"}}, ["script"]),
    "screenshot": (tool_screenshot, "A PNG of a scene in the headless editor, optionally after playing it for some frames.",
                   {"scene": {"type": "string"}, "play_frames": {"type": "integer"}, "timeout_s": {"type": "integer"}}, ["scene"]),
    "scene_summary": (tool_scene_summary, "A scene file's entities (name, GUID, parent) and their components, read from the .lvl JSON.",
                      {"scene": {"type": "string"}}, ["scene"]),
    "list_assets": (tool_list_assets, "Project files under a folder (default Assets), optionally only some extensions.",
                    {"folder": {"type": "string"}, "extensions": {"type": "array", "items": {"type": "string"}}, "limit": {"type": "integer"}}, []),
    "edscript_commands": (tool_edscript_commands, "Every edscript command the editor understands, with its usage comment.", {}, []),
    "build_game": (tool_build_game, "Havana --build-game [profile]: builds a shippable game folder under Builds/.",
                   {"profile": {"type": "string"}, "timeout_s": {"type": "integer"}}, []),
    "read_log": (tool_read_log, "The last lines of a log under .tmp/ (editor flows, RunChecks, MCP runs).",
                 {"path": {"type": "string"}, "lines": {"type": "integer"}}, ["path"]),
}


def handle(message):
    method = message.get("method")
    if method == "initialize":
        return {"protocolVersion": PROTOCOL, "capabilities": {"tools": {}}, "serverInfo": {"name": "havana", "version": "1.0"}}
    if method == "ping":
        return {}
    if method == "tools/list":
        return {"tools": [{"name": name, "description": desc, "inputSchema": {"type": "object", "properties": props, "required": required}}
                          for name, (_, desc, props, required) in TOOLS.items()]}
    if method == "tools/call":
        name = message["params"]["name"]
        if name not in TOOLS:
            raise ValueError(f"unknown tool {name}")
        try:
            content, is_error = TOOLS[name][0](message["params"].get("arguments", {}) or {})
        except Exception as e:  # report to the agent, keep serving
            content, is_error = [{"type": "text", "text": f"{type(e).__name__}: {e}"}], True
        return {"content": content, "isError": is_error}
    raise LookupError(method)


def main():
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        message = json.loads(line)
        if "id" not in message:
            continue  # notifications (initialized, cancelled)
        try:
            reply = {"jsonrpc": "2.0", "id": message["id"], "result": handle(message)}
        except LookupError as e:
            reply = {"jsonrpc": "2.0", "id": message["id"], "error": {"code": -32601, "message": f"no method {e}"}}
        except Exception as e:
            reply = {"jsonrpc": "2.0", "id": message["id"], "error": {"code": -32603, "message": str(e)}}
        sys.stdout.write(json.dumps(reply) + "\n")
        sys.stdout.flush()


if __name__ == "__main__":
    main()
