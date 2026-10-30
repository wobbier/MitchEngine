#!/usr/bin/env python3
"""Networking smoke test (NET-5): a dedicated server and a client on one machine, over real UDP.

Starts the headless server build on a free port (--server 0, read back from its log), waits until it hosts, then runs a client
for a fixed number of frames against it and captures the client's view. Passes when:
  - the client connected and was never rejected or timed out, and the server timed nobody out;
  - the client stayed connected for (nearly) its whole run, and neither side hit an assert;
  - the client received snapshots at the server's tick rate (within --rate-tolerance);
  - the client left cleanly (the server counts a Disconnect) and the server then exited on its own
    (--net-exit-when-empty);
  - the client's capture matches its reference (as ScreenshotRegression compares them);
  - every --expect-server / --expect-client line shows up in that side's log, and not in the other's
    (NET-6: NetProbe.cs logs "ServerRpc ok" on the server, "dash: 0 corrections" on the client).

  Engine/Tools/RunNetSmoke.py --server-game .build/Server_linux_Debug/Game_EntryPoint_x64 \\
      --client-game .build/Game_linux_Debug/Game_EntryPoint_x64 --wrapper Engine/Tools/Headless.sh \\
      --scene Assets/Scenes/Tests/NetTest.lvl --reference Assets/Scenes/Tests/Reference
  # accept the client's capture as the new reference
  ... --update

Run it from the game project root. Only the client goes through --wrapper (it renders); the server
has no window. Logs, reports and the capture land in --out (.tmp/NetSmoke).
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

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ScreenshotRegression import compare, downscale, read_png, write_png  # noqa: E402


def wait_for_line(log_path, needle, process, timeout):
    """True once needle shows up in the log; False if the process exits first or time runs out."""
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


def read_text(path):
    try:
        with open(path, encoding="utf-8", errors="replace") as text:
            return text.read()
    except OSError:
        return ""


def stop_group(process):
    """Ends a process started in its own session, with everything under it (Headless.sh's Xvfb too)."""
    for sig in (signal.SIGTERM, signal.SIGKILL):
        try:
            os.killpg(process.pid, sig)
        except ProcessLookupError:
            return
        try:
            process.wait(timeout=10)
            return
        except subprocess.TimeoutExpired:
            pass


def load_report(path):
    try:
        with open(path, encoding="utf-8") as report:
            return json.load(report)
    except (OSError, ValueError):
        return None


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--server-game", required=True, help="headless server executable (config=server_linux_debug)")
    parser.add_argument("--client-game", required=True, help="game executable for the client")
    parser.add_argument("--scene", default="Assets/Scenes/Tests/NetTest.lvl")
    parser.add_argument("--reference", default="Assets/Scenes/Tests/Reference", help="folder with <scene>.png")
    parser.add_argument("--wrapper", default="", help="command prefix for the client (e.g. Engine/Tools/Headless.sh)")
    parser.add_argument("--frames", type=int, default=900, help="client frames before it captures and leaves")
    parser.add_argument("--max-fps", type=float, default=60.0, help="client frame cap (frames are wall time)")
    parser.add_argument("--width", type=int, default=960)
    parser.add_argument("--height", type=int, default=540)
    parser.add_argument("--scale", type=int, default=2, help="references are stored downscaled by this")
    parser.add_argument("--tolerance", type=int, default=24)
    parser.add_argument("--max-changed", type=float, default=0.03)
    parser.add_argument("--rate-tolerance", type=float, default=0.05, help="snapshot rate vs tick rate")
    parser.add_argument("--net-args", default="", help="extra arguments for both sides (e.g. '--net-lag 40')")
    parser.add_argument("--host-timeout", type=float, default=300.0, help="seconds to wait for the server to host (first runs build scripts)")
    parser.add_argument("--client-timeout", type=float, default=900.0, help="seconds the client may take before it counts as hung")
    parser.add_argument("--min-connected", type=float, default=0.8, help="share of the run's frames (at --max-fps) the client must stay connected")
    parser.add_argument("--expect-server", action="append", default=[], help="text the server's log must contain (and the client's must not); repeatable")
    parser.add_argument("--expect-client", action="append", default=[], help="text the client's log must contain (and the server's must not); repeatable")
    parser.add_argument("--out", default=".tmp/NetSmoke")
    parser.add_argument("--update", action="store_true", help="write the client's capture as the reference")
    args = parser.parse_args()

    os.makedirs(args.out, exist_ok=True)
    name = os.path.splitext(os.path.basename(args.scene))[0]
    server_log_path = os.path.join(args.out, "server.log")
    client_log_path = os.path.join(args.out, "client.log")
    server_report = os.path.join(args.out, "server.json")
    client_report = os.path.join(args.out, "client.json")
    shot = os.path.join(args.out, name + ".png")
    for stale in (server_report, client_report, shot):
        if os.path.exists(stale):
            os.remove(stale)
    net_args = shlex.split(args.net_args)
    failures = []

    # The server: no window, so no wrapper, and any free port (it logs which). --frames is only a
    # backstop past the client's whole budget (it caps itself at one frame per tick, about 60 Hz);
    # it should leave as soon as the client does.
    backstop_frames = int(60 * (args.host_timeout + args.client_timeout + 60))
    server_command = [args.server_game, "--scene", args.scene, "--server", "0", "--net-exit-when-empty",
                      "--net-report", server_report, "--no-audio", "--frames", str(backstop_frames)] + net_args
    print("server: " + " ".join(shlex.quote(part) for part in server_command))
    with open(server_log_path, "w") as server_log:
        server = subprocess.Popen(server_command, stdout=server_log, stderr=subprocess.STDOUT)
    try:
        if not wait_for_line(server_log_path, "Net: hosting on UDP port", server, args.host_timeout):
            print(f"FAIL  {name}: the server never started hosting (see {server_log_path})")
            return 1
        hosting = re.search(r"Net: hosting on UDP port (\d+)", read_text(server_log_path))
        port = int(hosting.group(1))

        client_command = shlex.split(args.wrapper) + [
            args.client_game, "--scene", args.scene, "--connect", f"127.0.0.1:{port}", "--frames", str(args.frames),
            "--max-fps", str(args.max_fps), "--width", str(args.width), "--height", str(args.height), "--no-audio",
            "--no-profile-enforcement", "--screenshot", shot, "--net-report", client_report, "--exit"] + net_args
        print("client: " + " ".join(shlex.quote(part) for part in client_command))
        with open(client_log_path, "w") as client_log:
            client = subprocess.Popen(client_command, stdout=client_log, stderr=subprocess.STDOUT, start_new_session=True)
            try:
                client_code = client.wait(timeout=args.client_timeout)
            except subprocess.TimeoutExpired:
                stop_group(client)
                client_code = None
                failures.append(f"the client hung (stopped after {args.client_timeout:.0f} s; see {client_log_path})")
        if client_code not in (0, None):
            failures.append(f"the client exited with {client_code} (see {client_log_path})")

        try:
            server.wait(timeout=30)
        except subprocess.TimeoutExpired:
            failures.append("the server didn't exit after its client left (--net-exit-when-empty)")
    finally:
        if server.poll() is None:
            server.kill()
            server.wait()

    client_stats = load_report(client_report)
    server_stats = load_report(server_report)
    if client_stats is None:
        failures.append(f"no client report ({client_report}; see {client_log_path})")
    if server_stats is None:
        failures.append(f"no server report ({server_report}; see {server_log_path})")

    if client_stats is not None:
        tick_rate = client_stats.get("TickRate", 0) or 0
        rate = client_stats.get("SnapshotRate", 0.0)
        print(f"      client: {client_stats.get('State')}, {client_stats.get('ConnectedSeconds', 0):.1f} s connected, "
              f"{client_stats.get('SnapshotsReceived', 0)} snapshots ({rate:.2f}/s at {tick_rate} Hz), "
              f"RTT {client_stats.get('Connection', {}).get('RoundTripMs', 0):.1f} ms, "
              f"{client_stats.get('Resyncs', 0)} resyncs, {client_stats.get('Mirrored', 0)} mirrored")
        if not client_stats.get("Connected"):
            failures.append(f"the client never connected (state {client_stats.get('State')!r}, "
                            f"rejected: {client_stats.get('RejectReason')!r})")
        if client_stats.get("State") != "connected":
            failures.append(f"the client wasn't connected at the end (state {client_stats.get('State')!r})")
        if client_stats.get("Timeouts", 0) != 0:
            failures.append("the client timed out")
        if tick_rate <= 0 or abs(rate / tick_rate - 1.0) > args.rate_tolerance:
            failures.append(f"snapshot rate {rate:.2f}/s is more than {args.rate_tolerance:.0%} off the {tick_rate} Hz tick rate")
        # Frames run at most at --max-fps, so the session can only be longer than this.
        connected_seconds = client_stats.get("ConnectedSeconds", 0.0) or 0.0
        expected_seconds = args.frames / max(args.max_fps, 1.0)
        if connected_seconds < args.min_connected * expected_seconds:
            failures.append(f"the client was connected for only {connected_seconds:.1f} s of a ~{expected_seconds:.0f} s run")

    if server_stats is not None:
        clients = server_stats.get("Clients", [])
        print(f"      server: {server_stats.get('Accepted', 0)} accepted, {server_stats.get('Disconnects', 0)} left, "
              f"{server_stats.get('Timeouts', 0)} timed out, {server_stats.get('SnapshotsSent', 0)} snapshots sent, "
              f"{server_stats.get('Networked', 0)} networked")
        if server_stats.get("Accepted", 0) < 1:
            failures.append(f"the server accepted no client (last rejection {server_stats.get('LastRejection')!r})")
        if server_stats.get("Timeouts", 0) != 0:
            failures.append(f"the server timed out {server_stats.get('Timeouts')} client(s)")
        if server_stats.get("Disconnects", 0) < 1 or not any(entry.get("Status") == "left" for entry in clients):
            failures.append("the client didn't leave cleanly (no Disconnect reached the server)")

    logs = {"server": read_text(server_log_path), "client": read_text(client_log_path)}
    for label, log_path in (("server", server_log_path), ("client", client_log_path)):
        if "--- ASSERT ---" in logs[label] or "[bgfx] FATAL" in logs[label]:
            failures.append(f"the {label} hit an assert (see {log_path})")
    # Lines that belong to one side only (C# logs, e.g. NetProbe.cs).
    for side, other, expected in (("server", "client", args.expect_server), ("client", "server", args.expect_client)):
        for needle in expected:
            if needle not in logs[side]:
                failures.append(f"the {side} never logged {needle!r}")
            elif needle in logs[other]:
                failures.append(f"{needle!r} showed up in the {other}'s log too")
            else:
                print(f"      {side}: {needle}")

    if not os.path.exists(shot):
        failures.append(f"no client capture (see {client_log_path})")
    else:
        width, height, rows = read_png(shot)
        small = downscale(width, height, rows, args.scale) if args.scale > 1 else (width, height, rows)
        reference = os.path.join(args.reference, name + ".png")
        if args.update:
            os.makedirs(args.reference, exist_ok=True)
            write_png(reference, *small)
            print(f"WROTE {name}: {reference} ({small[0]}x{small[1]})")
        elif not os.path.exists(reference):
            failures.append(f"no reference {reference} (run with --update)")
        else:
            expected = read_png(reference)
            if (expected[0], expected[1]) != (small[0], small[1]):
                failures.append(f"capture {small[0]}x{small[1]} vs reference {expected[0]}x{expected[1]}")
            else:
                mean, changed, diff_rows, _ = compare(small, expected, args.tolerance)
                write_png(os.path.join(args.out, name + ".diff.png"), small[0], small[1], diff_rows)
                print(f"      capture: {changed:.2%} of pixels changed (limit {args.max_changed:.1%}), mean difference {mean:.2%}")
                if changed > args.max_changed:
                    failures.append(f"the client capture is {changed:.2%} off its reference")

    if failures:
        for failure in failures:
            print(f"FAIL  {name}: {failure}")
        return 1
    print(f"ok    {name}: server and client over UDP port {port}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
