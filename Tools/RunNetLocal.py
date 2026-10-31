#!/usr/bin/env python3
"""Play a networked scene locally (ME-289): a dedicated server and N client windows on this machine.

Starts the headless Server build on a UDP port, waits until it hosts, then opens the clients side
by side (game builds, --connect 127.0.0.1:<port>; each window's title says which client it is).
Close the client windows when you're done: the server leaves once the last one has gone
(--net-exit-when-empty). Ctrl+C stops everything. At the end each side's --net-report is
summarised: RTT, loss, snapshots per second, timeouts.

  Engine/Tools/RunNetLocal.py                                   # NetTest.lvl, 2 clients, port 7777
  Engine/Tools/RunNetLocal.py --scene Assets/Scenes/X.lvl --clients 3
  Engine/Tools/RunNetLocal.py --net-args "--net-lag 50 --net-loss 2%"    # a bad link, both ways
  Engine/Tools/RunNetLocal.py --build                           # build the server and game first
  Engine/Tools/RunNetLocal.py --client-args "--name P{i}"       # {i}: each client's number, from 1

Run it from the project root. Outside the dev shell it re-runs itself through Engine/Tools/NixEnv.sh.
VS Code: the "Net: server + 2 clients" tasks. Logs and reports land in --out (.tmp/NetLocal).
Tiling window managers (and native Wayland) place the windows themselves.

A C#-only project (Project/Game.meproj, no C++: --project <folder>, or run it from that folder) plays
on the Player of the engine checkout this script belongs to: the headless .build/Server_linux_Debug/Player
hosts, .build/Game_linux_Debug/Player clients join, the scene defaults to the project's StartupScene,
and --build builds both Players (Tools/BuildProject.sh --project <checkout>).

  <engine>/Tools/RunNetLocal.py --project ~/Projects/ME/MyGame --server-args "--bots 2"
"""
import argparse
import json
import os
import re
import shlex
import signal
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ENGINE = os.path.dirname(HERE)
DEFAULT_SCENE = "Assets/Scenes/Tests/NetTest.lvl"


def in_dev_shell():
    return bool(os.environ.get("MITCH_PROJECT_ROOT") or os.environ.get("IN_NIX_SHELL"))


def project_name():
    try:
        with open("Project/Game.meproj", encoding="utf-8") as meproj:
            return json.load(meproj).get("ProjectName", "MitchGame")
    except (OSError, ValueError):
        return "MitchGame"


def is_script_only_project(folder):
    """A project with no C++ of its own (no Game.sharpmake.cs): it runs on the Player."""
    return (os.path.isfile(os.path.join(folder, "Project", "Game.meproj"))
            and not os.path.isfile(os.path.join(folder, "Game.sharpmake.cs")))


def has_player_target(folder):
    try:
        names = os.listdir(folder)
    except OSError:
        return False
    for name in names:
        if name.endswith(".make"):
            try:
                with open(os.path.join(folder, name), encoding="utf-8", errors="replace") as makefile:
                    if re.search(r"^Player:", makefile.read(), re.MULTILINE):
                        return True
            except OSError:
                pass
    return False


def engine_checkout():
    """Where the Players are built: the nearest folder at or above the engine whose solution makefile
    has a Player target (the game around a submodule engine, or a standalone engine clone itself).
    Not generated yet: the game around the engine, else the engine."""
    folder = ENGINE
    while True:
        if has_player_target(folder):
            return folder
        parent = os.path.dirname(folder)
        if parent == folder:
            break
        folder = parent
    around = os.path.dirname(ENGINE)
    return around if os.path.isfile(os.path.join(around, "Game.sharpmake.cs")) else ENGINE


def startup_scene(root):
    try:
        with open(os.path.join(root, "Assets", "Config", "ProjectSettings.json"), encoding="utf-8") as settings:
            return json.load(settings).get("Product", {}).get("StartupScene", "")
    except (OSError, ValueError, AttributeError):
        return ""


def client_env(index, wrapped):
    """Wrapped clients each get their own HEADLESS_DISPLAY (:99, :100, ... from yours): Headless.sh
    stops the Xvfb it started when its client ends, which would cut the others off."""
    if not wrapped:
        return None
    env = dict(os.environ)
    base = os.environ.get("HEADLESS_DISPLAY", ":99").lstrip(":")
    env["HEADLESS_DISPLAY"] = f":{(int(base) if base.isdigit() else 99) + index}"
    return env


def wait_for_line(log_path, needle, process, timeout):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        try:
            with open(log_path, encoding="utf-8", errors="replace") as log:
                if needle in log.read():
                    return True
        except FileNotFoundError:
            pass
        if process.poll() is not None:
            return False
        time.sleep(0.2)
    return False


def load_report(path):
    try:
        with open(path, encoding="utf-8") as report:
            return json.load(report)
    except (OSError, ValueError):
        return None


def stop(process, grace=10.0):
    """SIGTERM (the engine quits cleanly: its report and goodbye), then SIGKILL."""
    if process.poll() is not None:
        return
    process.send_signal(signal.SIGTERM)
    try:
        process.wait(timeout=grace)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait()


def summarise(label, report):
    if report is None:
        return f"{label}: no report"
    if report.get("Error"):
        return f"{label}: {report['Error']}"
    if report.get("Role") == "server":
        line = (f"{label}: {report.get('Accepted', 0)} joined, {report.get('Disconnects', 0)} left, "
                f"{report.get('Timeouts', 0)} timed out, {report.get('Rejected', 0)} turned away, "
                f"{report.get('SnapshotsSent', 0)} snapshots sent")
        for client in report.get("Clients", []):
            line += (f"\n    #{client.get('Id')}: {client.get('Status')}, {client.get('ConnectedSeconds', 0):.1f} s, "
                     f"{client.get('SnapshotRate', 0):.1f} snapshots/s, {client.get('MissedCommands', 0)} missed commands")
        return line
    connection = report.get("Connection", {})
    return (f"{label}: {report.get('State')}"
            + (f" as #{report.get('ClientId')}" if report.get("Connected") else "")
            + f", {report.get('ConnectedSeconds', 0):.1f} s, RTT {connection.get('RoundTripMs', 0):.1f} ms, "
            f"loss {connection.get('PacketLoss', 0) * 100:.1f}%, {report.get('SnapshotRate', 0):.1f} snapshots/s, "
            f"{connection.get('KbpsOut', 0):.1f} kbps out, {report.get('Resyncs', 0)} resyncs, {report.get('Timeouts', 0)} timeouts"
            + (f" (turned away: {report['RejectReason']})" if report.get("RejectReason") else ""))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--project", default="", help="the project's folder (default: the working directory); a C#-only one plays on the Player")
    parser.add_argument("--scene", default="", help=f"default {DEFAULT_SCENE}, else the project's StartupScene")
    parser.add_argument("--clients", type=int, default=2)
    parser.add_argument("--port", type=int, default=7777, help="UDP port (0: any free one)")
    parser.add_argument("--net-args", default="", help="for both sides, e.g. '--net-lag 50 --net-loss 2%%'")
    parser.add_argument("--server-args", default="", help="for the server only")
    parser.add_argument("--client-args", default="", help="for every client, {i} = its number from 1 (e.g. '--frames 600' to close them by themselves, '--name P{i}')")
    parser.add_argument("--wrapper", default="", help="command prefix for the clients, {i} as above (e.g. Engine/Tools/Headless.sh; each client gets its own HEADLESS_DISPLAY)")
    parser.add_argument("--width", type=int, default=800, help="client window size")
    parser.add_argument("--height", type=int, default=450)
    parser.add_argument("--server-game", default="", help="default .build/Server_linux_Debug/Game_EntryPoint_x64 (C#-only: the checkout's Server_linux_Debug/Player)")
    parser.add_argument("--client-game", default="", help="default .build/Game_linux_Debug/Game_EntryPoint_x64 (C#-only: the checkout's Game_linux_Debug/Player)")
    parser.add_argument("--build", action="store_true", help="make the server and game builds first")
    parser.add_argument("--host-timeout", type=float, default=300.0, help="seconds to wait for the server (first runs build scripts)")
    parser.add_argument("--out", default="", help="logs and reports (default .tmp/NetLocal in the project)")
    args = parser.parse_args()

    root = os.path.abspath(args.project) if args.project else os.getcwd()
    if not os.path.isfile(os.path.join(root, "Project", "Game.meproj")) and args.project:
        print(f"FAIL  {root} isn't a project (no Project/Game.meproj)")
        return 1
    # C#-only: no game build of its own, so the Players of this engine's checkout run it.
    player = is_script_only_project(root)

    if not in_dev_shell():
        nix_env = os.path.join(HERE, "NixEnv.sh")
        os.execv(nix_env, [nix_env, "--project", root, "python3", os.path.abspath(__file__)] + sys.argv[1:])

    if player:
        checkout = engine_checkout()
        project_args = ["--project", root]
        server_game = args.server_game or os.path.join(checkout, ".build", "Server_linux_Debug", "Player")
        client_game = args.client_game or os.path.join(checkout, ".build", "Game_linux_Debug", "Player")
        # The Players change into the project folder, so their file arguments are absolute.
        out = os.path.abspath(args.out or os.path.join(root, ".tmp", "NetLocal"))
        builds = [["bash", os.path.join(HERE, "BuildProject.sh"), "--project", checkout, "Player", config]
                  for config in ("server_linux_debug", "game_linux_debug")]
    else:
        if args.project:
            os.chdir(root)
        project_args = []
        server_game = args.server_game or ".build/Server_linux_Debug/Game_EntryPoint_x64"
        client_game = args.client_game or ".build/Game_linux_Debug/Game_EntryPoint_x64"
        out = args.out or ".tmp/NetLocal"
        builds = [["make", "-f", f"{project_name()}.make", "Game_EntryPoint", f"config={config}", f"-j{os.cpu_count() or 4}"]
                  for config in ("server_linux_debug", "game_linux_debug")]
    scene = args.scene
    if not scene:
        scene = DEFAULT_SCENE
        if not os.path.exists(os.path.join(root, DEFAULT_SCENE)) and startup_scene(root):
            scene = startup_scene(root)

    if args.build:
        for command in builds:
            target = " ".join(command[-2:]) if player else " ".join(command[3:5])
            print(f"build: {target}", flush=True)
            if subprocess.call(command) != 0:
                print(f"FAIL  the build failed ({target})")
                return 1
    for game in (server_game, client_game):
        if not os.path.exists(game):
            print(f"FAIL  {game} isn't built (run with --build, or the VS Code build tasks)")
            return 1

    os.makedirs(out, exist_ok=True)
    for stale in os.listdir(out):
        if stale.endswith((".json", ".log")):
            os.remove(os.path.join(out, stale))
    net_args = shlex.split(args.net_args)
    server_report = os.path.join(out, "server.json")
    server_log_path = os.path.join(out, "server.log")
    server_command = [server_game] + project_args + ["--scene", scene, "--server", str(args.port), "--net-exit-when-empty",
                      "--net-report", server_report] + net_args + shlex.split(args.server_args)
    print("server: " + " ".join(shlex.quote(part) for part in server_command), flush=True)
    server_log = open(server_log_path, "w")
    server = subprocess.Popen(server_command, stdout=server_log, stderr=subprocess.STDOUT)
    clients = []
    interrupted = False
    try:
        if not wait_for_line(server_log_path, "Net: hosting on UDP port", server, args.host_timeout):
            print(f"FAIL  the server didn't start hosting (see {server_log_path})")
            with open(server_log_path, encoding="utf-8", errors="replace") as log:
                for line in log.read().splitlines():
                    if "Net:" in line:
                        print("      " + line)
            if args.port != 0:
                print("      (another server on that port? try --port 0)")
            return 1
        with open(server_log_path, encoding="utf-8", errors="replace") as log:
            hosting = next(line for line in log.read().splitlines() if "Net: hosting on UDP port" in line)
        port = int(hosting.split("Net: hosting on UDP port ")[1].split()[0])
        print(f"server: hosting on UDP port {port}", flush=True)

        # Side by side, two to a row.
        for index in range(args.clients):
            column, row = index % 2, index // 2
            x = 40 + column * (args.width + 24)
            y = 40 + row * (args.height + 64)
            number = str(index + 1)
            report = os.path.join(out, f"client{number}.json")
            log_path = os.path.join(out, f"client{number}.log")
            command = shlex.split(args.wrapper.replace("{i}", number)) + [client_game] + project_args + [
                       "--scene", scene, "--connect", f"127.0.0.1:{port}",
                       "--width", str(args.width), "--height", str(args.height), "--window-x", str(x), "--window-y", str(y),
                       "--window-mode", "windowed", "--net-report", report] + net_args + shlex.split(args.client_args.replace("{i}", number))
            print(f"client {number}: " + " ".join(shlex.quote(part) for part in command), flush=True)
            process = subprocess.Popen(command, stdout=open(log_path, "w"), stderr=subprocess.STDOUT, env=client_env(index, bool(args.wrapper)))
            clients.append((process, report, log_path))
            # One at a time: the clients share their script build (the exe folder, or the project's
            # .tmp/Scripts), so the next starts once this one is past it and on the network.
            if index + 1 < args.clients:
                wait_for_line(log_path, "Net: ", process, args.host_timeout)
            time.sleep(0.5)
        print("Close the client windows to finish (Ctrl+C stops everything).", flush=True)
        for process, _, _ in clients:
            process.wait()
        try:
            server.wait(timeout=15)
        except subprocess.TimeoutExpired:
            print("      the server didn't leave by itself after its clients: stopping it")
            stop(server)
    except KeyboardInterrupt:
        interrupted = True
        print("\nstopping...")
    finally:
        for process, _, _ in clients:
            stop(process)
        stop(server)
        server_log.close()

    print()
    print(summarise("server", load_report(server_report)))
    for index, (_, report, _) in enumerate(clients):
        print(summarise(f"client {index + 1}", load_report(report)))
    print(f"(logs and reports: {out})")
    return 130 if interrupted else 0


if __name__ == "__main__":
    sys.exit(main())
