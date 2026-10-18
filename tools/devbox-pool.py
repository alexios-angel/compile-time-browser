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
    tools/devbox-pool.py sync [--box NAME]
    tools/devbox-pool.py status

`run` takes the first free slot (boxes in settings order) on a box that is
not marked by a validation round, starts it if it is stopped, and runs COMMAND
with DEVBOX_HOST set to the box's ssh alias (tools/remote-build.sh honors it).
In COMMAND, `{box}` is replaced by that alias, so a test step can say
`ssh {box} 'cd ~/projects/... && ctest ...'`. The lock is held until COMMAND
exits, and its exit status is returned.

--box     only this box
--round   a validation round: may use a marked box, and marks a one-slot box it takes
          until COMMAND exits, so ordinary jobs go elsewhere
--wait    wait for a box instead of exiting 75 (EX_TEMPFAIL) when none is free

`sync` copies the caller's worktree to $DEVBOX_DIR on every running box (or
only --box), through `tools/remote-build.sh sync`, so a job can then build on
whichever box is free. It waits for a slot on each box, so it never rewrites
sources under a build in progress there. DEVBOX_DIR must be set: syncing to the
shared default directory from an author's worktree would replace the
validator's tree. Stopped boxes are skipped, not started.

`status` shows each box's power state and free slots and, for a running box,
its CPU (sampled over one second), memory and disk use, read with psutil
on the box over Fabric. A box that does not answer is looked up through its
cloud's SDK (boto3, google-cloud-compute) with the ids in its terraform state.

Run it from any worktree by its absolute path: it finds its settings beside
itself, and COMMAND runs in the caller's working directory.
"""

import argparse
import fcntl
import json
import os
import re
import shlex
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor
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
    # A cloud CLI can stall (a slow `terraform output`, a token refresh); a
    # status that does not answer in time counts as not running, so `run`
    # asks the server to start it, which is harmless on a running box.
    try:
        out = subprocess.run(
            [box["server"], "status"], check=False, capture_output=True, text=True, timeout=90
        )
    except subprocess.TimeoutExpired:
        print(f"devbox-pool: {box['name']} status timed out", file=sys.stderr)
        return False
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
    # A round marks only a one-slot box: on a box with several slots, each
    # job already has its own slot and build_jobs share of the cores, so
    # keeping authors off the whole box would only idle it.
    marker = Path(box["round_marker"]) if args.round and box.get("slots", 1) == 1 else None
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


def sync(settings, args):
    if "DEVBOX_DIR" not in os.environ:
        sys.exit("devbox-pool: sync needs DEVBOX_DIR (the box directory to sync into)")
    root = subprocess.run(
        ["git", "rev-parse", "--show-toplevel"], check=True, capture_output=True, text=True
    ).stdout.strip()
    script = Path(root) / "tools" / "remote-build.sh"
    # An older checkout's remote-build.sh would read `sync` as a ninja target
    # and start a build.
    if not (script.with_name("remote_build.py").exists() or "= sync ]" in script.read_text()):
        sys.exit(f"devbox-pool: {script} has no sync mode; merge ctcompile-v1 into this worktree")
    failed = 0
    for box in settings["boxes"]:
        if args.box not in (None, box["name"]):
            continue
        if not is_running(box):
            print(f"devbox-pool: {box['name']} is not running; skipped", file=sys.stderr)
            continue
        job = argparse.Namespace(
            box=box["name"],
            round=False,
            wait=True,
            command=[str(script), "sync"],
        )
        failed |= run(settings, job) != 0
    return 1 if failed else 0


# Run on the box by uv with psutil (cached by uv after the first run): CPU use
# over one second, core count, memory, and the filesystem holding the project
# trees, as JSON.
USAGE_PROBE = """
import json, os, psutil
mem, disk = psutil.virtual_memory(), psutil.disk_usage(os.path.expanduser("~"))
print(json.dumps({
    "cpu": psutil.cpu_percent(interval=1), "cores": psutil.cpu_count(),
    "mem_used": mem.total - mem.available, "mem_total": mem.total,
    "disk_used": disk.used, "disk_total": disk.total,
}))
"""


# Where the boxes' cloud-init installs uv (brew); a non-interactive ssh has no
# brew on PATH.
BOX_UV = "/home/linuxbrew/.linuxbrew/bin/uv"


def usage(box):
    """CPU, memory and disk use of a box as one line of text, or None if it does not answer."""
    from invoke.exceptions import CommandTimedOut, UnexpectedExit
    from paramiko import SSHException
    from remote_build import open_connection

    command = (
        f"{BOX_UV} run --quiet --no-project --with psutil python -c {shlex.quote(USAGE_PROBE)}"
    )
    try:
        with open_connection(box["name"]) as conn:
            u = json.loads(conn.run(command, hide=True, timeout=60).stdout)
    except (OSError, SSHException, UnexpectedExit, CommandTimedOut, ValueError):
        return None
    gib = 1 << 30
    return (
        f"CPU {u['cpu']:3.0f}% of {u['cores']} cores, "
        f"memory {u['mem_used'] / gib:.1f}/{u['mem_total'] / gib:.1f} GiB, "
        f"disk {u['disk_used'] / gib:.0f}/{u['disk_total'] / gib:.0f} GiB used"
    )


def cloud_state(box):
    """The box's power state, lowercased, from its cloud's API.

    The instance comes from the terraform state beside the box's server.sh:
    an `instance_id` output means AWS, a `project_id` output means GCP.
    """
    from botocore.exceptions import BotoCoreError, ClientError
    from google.api_core.exceptions import GoogleAPIError
    from google.auth.exceptions import GoogleAuthError

    state = Path(box["server"]).with_name("terraform.tfstate")
    try:
        out = {k: v["value"] for k, v in json.loads(state.read_text())["outputs"].items()}
        if "instance_id" in out:
            import boto3

            ec2 = boto3.client("ec2", region_name=out["region"])
            found = ec2.describe_instances(InstanceIds=[out["instance_id"]])
            return found["Reservations"][0]["Instances"][0]["State"]["Name"]
        from google.api_core.client_options import ClientOptions
        from google.cloud import compute_v1

        # Bill the call to the box's project, as pick-machine.py does, so
        # end-user credentials need no quota project of their own.
        options = ClientOptions(quota_project_id=out["project_id"])
        instances = compute_v1.InstancesClient(client_options=options)
        found = instances.get(project=out["project_id"], zone=out["zone"], instance=out["name"])
        return found.status.lower()
    except (
        OSError,
        KeyError,
        ValueError,
        BotoCoreError,
        ClientError,
        GoogleAPIError,
        GoogleAuthError,
    ) as e:
        return f"unknown ({type(e).__name__})"


def box_status(box):
    free = 0
    for path in slot_locks(box):
        lock = try_lock(path)
        if lock is not None:
            free += 1
            os.close(lock)
    round_ = ", round" if os.path.exists(box["round_marker"]) else ""
    slots = f"{free}/{len(slot_locks(box))} slots free{round_}"
    # A box that answers is running, so its cloud is asked only about a box
    # that does not.
    if (load := usage(box)) is not None:
        return f"{box['name']:12s} running  {slots}; {load}"
    state = cloud_state(box)
    note = "; not answering over ssh" if state == "running" else ""
    return f"{box['name']:12s} {state:8s} {slots}{note}"


def status(settings):
    # usage() reaches the boxes through Fabric, which lives in the repository's
    # uv `tools` group, not in the python3 that usually runs this file. Rerun
    # there once; the marker keeps a uv environment without Fabric from looping.
    try:
        import fabric  # noqa: F401
    except ImportError:
        if os.environ.get("DEVBOX_POOL_IN_UV"):
            sys.exit("devbox-pool: no Fabric in the uv tools group; run `uv sync --group tools`")
        os.environ["DEVBOX_POOL_IN_UV"] = "1"
        uv = [
            "uv",
            "run",
            "--quiet",
            "--frozen",
            "--only-group",
            "tools",
            "--project",
            str(HERE.parent),
        ]
        os.execvp("uv", [*uv, "python", str(Path(__file__).resolve()), "status"])
    # Each box's cloud status and ssh probe take seconds, so ask them all at once.
    with ThreadPoolExecutor() as pool:
        for line in pool.map(box_status, settings["boxes"]):
            print(line)
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = parser.add_subparsers(dest="action", required=True)
    run_p = sub.add_parser("run", help="run a command on a free box")
    run_p.add_argument("--box")
    run_p.add_argument("--round", action="store_true")
    run_p.add_argument("--wait", action="store_true")
    run_p.add_argument("command", nargs=argparse.REMAINDER)
    sync_p = sub.add_parser("sync", help="copy this worktree to $DEVBOX_DIR on every running box")
    sync_p.add_argument("--box")
    sub.add_parser("status", help="show each box's power, free slots, and CPU, memory and disk use")
    args = parser.parse_args()
    settings = load_settings()
    if args.action == "status":
        return status(settings)
    if args.action == "sync":
        return sync(settings, args)
    if args.command[:1] == ["--"]:
        args.command = args.command[1:]
    return run(settings, args)


if __name__ == "__main__":
    sys.exit(main())
