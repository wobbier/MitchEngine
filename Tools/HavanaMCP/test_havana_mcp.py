#!/usr/bin/env python3
"""Drives havana_mcp.py over stdio the way an MCP client does (NEXT-1): handshake, the tool list,
then each tool once against MitchGame's test scenes. Run from the project root, in the dev shell
(the editor sessions need its libraries). Exits 1 on the first failure."""
import base64
import json
import os
import subprocess
import sys

server = subprocess.Popen([sys.executable, os.path.join(os.path.dirname(os.path.abspath(__file__)), "havana_mcp.py")],
                          stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
next_id = 0


def call(method, params=None):
    global next_id
    next_id += 1
    server.stdin.write(json.dumps({"jsonrpc": "2.0", "id": next_id, "method": method, "params": params or {}}) + "\n")
    server.stdin.flush()
    reply = json.loads(server.stdout.readline())
    if "error" in reply:
        raise SystemExit(f"FAIL {method}: {reply['error']}")
    return reply["result"]


def tool(name, **arguments):
    return call("tools/call", {"name": name, "arguments": arguments})


def check(condition, what):
    print(("ok    " if condition else "FAIL  ") + what)
    if not condition:
        server.kill()
        sys.exit(1)


init = call("initialize", {"protocolVersion": "2024-11-05", "capabilities": {}, "clientInfo": {"name": "test", "version": "0"}})
check(init["serverInfo"]["name"] == "havana" and "tools" in init["capabilities"], "initialize")
server.stdin.write(json.dumps({"jsonrpc": "2.0", "method": "notifications/initialized"}) + "\n")
names = [t["name"] for t in call("tools/list")["tools"]]
check({"run_edscript", "screenshot", "scene_summary", "list_assets", "edscript_commands", "build_game", "read_log"} <= set(names), f"tools/list ({len(names)} tools)")

summary = tool("scene_summary", scene="Assets/Scenes/Tests/OptionsTest.lvl")["content"][0]["text"]
check("Options" in summary and "UIDocument" in summary, "scene_summary")
assets = tool("list_assets", folder="Assets/Scenes/Tests", extensions=["edscript"])["content"][0]["text"]
check("OptionsFlows.edscript" in assets, "list_assets")
commands = tool("edscript_commands")["content"][0]["text"]
check("save-game" in commands and "assert-field" in commands, "edscript_commands")

ok = tool("run_edscript", scene="Assets/Scenes/Tests/OptionsTest.lvl", script="wait 10\nassert-exists Options\nsettings-assert Audio.Music | 1.0")
check(not ok["isError"] and '"passed": true' in ok["content"][0]["text"], "run_edscript (passing)")
bad = tool("run_edscript", scene="Assets/Scenes/Tests/OptionsTest.lvl", script="wait 5\nassert-exists Nobody Here")
check(bad["isError"] and "Nobody Here" in bad["content"][0]["text"], "run_edscript (failing, reported)")

shot = tool("screenshot", scene="Assets/Scenes/Tests/OptionsTest.lvl")["content"][0]
check(shot["type"] == "image" and base64.b64decode(shot["data"])[:8] == b"\x89PNG\r\n\x1a\n", "screenshot (a PNG)")
log = tool("read_log", path="../etc/passwd")
check(log["isError"], "read_log stays inside .tmp/")

server.stdin.close()
server.wait(timeout=10)
print("All MCP checks passed")
