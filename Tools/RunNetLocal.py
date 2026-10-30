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

Run it from the project root. Outside the dev shell it re-runs itself through Engine/Tools/NixEnv.sh.
VS Code: the "Net: server + 2 clients" tasks. Logs and reports land in --out (.tmp/NetLocal).
Tiling window managers (and native Wayland) place the windows themselves.
"""
import argparse
import json
import os
import shlex
import signal
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))


def in_dev_shell():
    return bool(os.environ.get("MITCH_PROJECT_ROOT") or os.environ.get("IN_NIX_SHELL"))


def project_name():
    try:
        with open("Project/Game.meproj", encoding="utf-8") as meproj:
            return json.load(meproj).get("ProjectName", "MitchGame")
    except (OSError, ValueError):
        return "MitchGame"


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
    parser.add_argument("--scene", default="Assets/Scenes/Tests/NetTest.lvl")
    parser.add_argument("--clients", type=int, default=2)
    parser.add_argument("--port", type=int, default=7777, help="UDP port (0: any free one)")
    parser.add_argument("--net-args", default="", help="for both sides, e.g. '--net-lag 50 --net-loss 2%%'")
    parser.add_argument("--server-args", default="", help="for the server only")
    parser.add_argument("--client-args", default="", help="for every client (e.g. '--frames 600' to close them by themselves)")
    parser.add_argument("--wrapper", default="", help="command prefix for the clients (e.g. Engine/Tools/Headless.sh)")
    parser.add_argument("--width", type=int, default=800, help="client window size")
    parser.add_argument("--height", type=int, default=450)
    parser.add_argument("--server-game", default=".build/Server_linux_Debug/Game_EntryPoint_x64")
    parser.add_argument("--client-game", default=".build/Game_linux_Debug/Game_EntryPoint_x64")
    parser.add_argument("--build", action="store_true", help="make the server and game builds first")
    parser.add_argument("--host-timeout", type=float, default=300.0, help="seconds to wait for the server (first runs build scripts)")
    parser.add_argument("--out", default=".tmp/NetLocal")
    args = parser.parse_args()

    if not in_dev_shell():
        nix_env = os.path.join(HERE, "NixEnv.sh")
        os.execv(nix_env, [nix_env, "--project", os.getcwd(), "python3", os.path.abspath(__file__)] + sys.argv[1:])

    if args.build:
        for target in (f"Game_EntryPoint config=server_linux_debug", f"Game_EntryPoint config=game_linux_debug"):
            print(f"build: {target}", flush=True)
            if subprocess.call(["make", "-f", f"{project_name()}.make", *target.split(), f"-j{os.cpu_count() or 4}"]) != 0:
                print(f"FAIL  the build failed ({target})")
                return 1
    for game in (args.server_game, args.client_game):
        if not os.path.exists(game):
            print(f"FAIL  {game} isn't built (run with --build, or the VS Code build tasks)")
            return 1

    os.makedirs(args.out, exist_ok=True)
    for stale in os.listdir(args.out):
        if stale.endswith((".json", ".log")):
            os.remove(os.path.join(args.out, stale))
    net_args = shlex.split(args.net_args)
    server_report = os.path.join(args.out, "server.json")
    server_log_path = os.path.join(args.out, "server.log")
    server_command = [args.server_game, "--scene", args.scene, "--server", str(args.port), "--net-exit-when-empty",
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
            report = os.path.join(args.out, f"client{index + 1}.json")
            log_path = os.path.join(args.out, f"client{index + 1}.log")
            command = shlex.split(args.wrapper) + [args.client_game, "--scene", args.scene, "--connect", f"127.0.0.1:{port}",
                       "--width", str(args.width), "--height", str(args.height), "--window-x", str(x), "--window-y", str(y),
                       "--window-mode", "windowed", "--net-report", report] + net_args + shlex.split(args.client_args)
            print(f"client {index + 1}: " + " ".join(shlex.quote(part) for part in command), flush=True)
            clients.append((subprocess.Popen(command, stdout=open(log_path, "w"), stderr=subprocess.STDOUT), report, log_path))
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
    print(f"(logs and reports: {args.out})")
    return 130 if interrupted else 0


if __name__ == "__main__":
    sys.exit(main())
