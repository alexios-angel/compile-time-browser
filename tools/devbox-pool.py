#!/usr/bin/env python3
"""Run a command on whichever build box is free.

The boxes, their locks and how to start them are in tools/devbox-settings.json,
which is machine-local and gitignored: copy devbox-settings.example.json to
start. A box runs up to `slots` jobs at once (each build limited to the box's
`build_jobs` when set, through CT_BUILD_JOBS), each under a flock(2) lock: slot 0
is the box's `lock` file itself, the one the agents' `flock
/tmp/ctbrowser-devbox-*.lock ...` commands take, so jobs started either way
share it, and slot N is that path with `.N` appended.

    tools/devbox-pool.py run [--box NAME] [--round] [--wait] -- COMMAND...
    tools/devbox-pool.py status

`run` takes the first free slot (boxes in settings order) on a box that is
not marked by a validation round, starts it if it is stopped, and runs COMMAND
with DEVBOX_HOST set to the box's ssh alias (tools/remote-build.sh honors it).
In COMMAND, `{box}` is replaced by that alias, so a test step can say
`ssh {box} 'cd ~/projects/... && ctest ...'`. The lock is held until COMMAND
exits, and its exit status is returned.

--box     only this box
--round   a validation round: may use a marked box, and marks the box it takes
          until COMMAND exits, so ordinary jobs go elsewhere
--wait    wait for a box instead of exiting 75 (EX_TEMPFAIL) when none is free

Run it from any worktree by its absolute path: it finds its settings beside
itself, and COMMAND runs in the caller's working directory.
"""

import argparse
import fcntl
import json
import os
import re
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent


def load_settings():
    path = HERE / "devbox-settings.json"
    if not path.exists():
        sys.exit(f"devbox-pool: no {path}; copy devbox-settings.example.json beside it")
    with open(path) as f:
        settings = json.load(f)
    for box in settings["boxes"]:
        box["server"] = str((HERE / box["server"]).resolve())
    return settings


def slot_locks(box):
    """The lock file of each of `box`'s job slots; slot 0 is `lock` itself."""
    lock = box["lock"]
    return [lock] + [f"{lock}.{n}" for n in range(1, box.get("slots", 1))]


def try_lock(path):
    """A descriptor holding flock(2) on `path`, or None if another job holds it."""
    fd = os.open(path, os.O_WRONLY | os.O_CREAT, 0o666)
    try:
        fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except BlockingIOError:
        os.close(fd)
        return None
    return fd


def is_running(box):
    out = subprocess.run([box["server"], "status"], check=False, capture_output=True, text=True)
    return out.returncode == 0 and re.search(box["running"], out.stdout) is not None


def ensure_running(box):
    if is_running(box):
        return
    print(f"devbox-pool: starting {box['name']}", file=sys.stderr)
    subprocess.run([box["server"], "start"], check=True)


def acquire(settings, args):
    boxes = [b for b in settings["boxes"] if args.box in (None, b["name"])]
    if not boxes:
        sys.exit(f"devbox-pool: no box named {args.box!r} in devbox-settings.json")
    while True:
        for box in boxes:
            if not args.round and os.path.exists(box["round_marker"]):
                continue
            for path in slot_locks(box):
                lock = try_lock(path)
                if lock is not None:
                    return box, lock
        if not args.wait:
            print("devbox-pool: every box is busy (use --wait to queue)", file=sys.stderr)
            sys.exit(75)
        time.sleep(settings.get("poll_seconds", 10))


def run(settings, args):
    if not args.command:
        sys.exit("devbox-pool: run needs a command after --")
    box, lock = acquire(settings, args)
    marker = Path(box["round_marker"]) if args.round else None
    try:
        if marker:
            marker.touch()
        ensure_running(box)
        command = [part.replace("{box}", box["name"]) for part in args.command]
        print(f"devbox-pool: {box['name']}: {' '.join(command)}", file=sys.stderr)
        env = dict(os.environ, DEVBOX_HOST=box["name"])
        # A box shared by several slots gives each job's build a share of the
        # cores (tools/remote-build.sh's -j); an explicit CT_BUILD_JOBS wins.
        if "build_jobs" in box:
            env.setdefault("CT_BUILD_JOBS", str(box["build_jobs"]))
        return subprocess.run(command, check=False, env=env).returncode
    finally:
        if marker:
            marker.unlink(missing_ok=True)
        os.close(lock)


def status(settings):
    for box in settings["boxes"]:
        busy = 0
        for path in slot_locks(box):
            lock = try_lock(path)
            if lock is None:
                busy += 1
            else:
                os.close(lock)
        round_ = " round" if os.path.exists(box["round_marker"]) else ""
        power = "running" if is_running(box) else "stopped"
        print(f"{box['name']:12s} {power:8s} {busy}/{len(slot_locks(box))} slots busy{round_}")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = parser.add_subparsers(dest="action", required=True)
    run_p = sub.add_parser("run", help="run a command on a free box")
    run_p.add_argument("--box")
    run_p.add_argument("--round", action="store_true")
    run_p.add_argument("--wait", action="store_true")
    run_p.add_argument("command", nargs=argparse.REMAINDER)
    sub.add_parser("status", help="show each box's power, lock and round state")
    args = parser.parse_args()
    settings = load_settings()
    if args.action == "status":
        return status(settings)
    if args.command[:1] == ["--"]:
        args.command = args.command[1:]
    return run(settings, args)


if __name__ == "__main__":
    sys.exit(main())
