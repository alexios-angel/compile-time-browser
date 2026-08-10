#!/usr/bin/env python3
"""Does ctdrive return a large reply intact? Two cases, because only one bites.

ctdrive's peer socket is non-blocking (`set_non_blocking(peer_)` at accept in
examples/cli/ctdrive.cpp), so `send()` is entitled to accept a PREFIX. `reply()`
loops until every byte is written; this measures whether it needs to.

    tools/check/ctdrive-reply.py                 # both cases
    tools/check/ctdrive-reply.py --case drain    # just the one that passes anyway

THE POINT OF HAVING TWO. A client that reads continuously keeps the kernel
buffer draining as fast as it fills, so a single unlooped `send()` gets a 262 KB
reply out in one call and looks correct. Only a client that does NOT read
promptly fills the buffer mid-reply. Measured on the devbox, GCC 13, loopback:

    case    payload   looped reply()   single send()
    drain     262 KB   intact           intact          <- proves nothing
    stall       4 MB   intact           2,588,672 bytes, no newline

So "the reply got big" is not the trigger, and the parity dump - which chunks
and reads promptly - would probably never have found this. It is a latent bug
fixed on its own terms. Keep both cases: deleting `stall` would leave a test
that cannot fail, and deleting `drain` would lose the reason the bug hid.
"""
import argparse
import json
import re
import socket
import subprocess
import sys
import time
from pathlib import Path

# Doubling (`s += s`), never `while (s.length < N) s += 'x'`: the latter is
# quadratic in a VM whose string concat may copy, and an early draft of this
# script asking for 900 KB that way was reckless on a 7.5 GiB box.
BUILD_STRING = "var s = 'x'; while (s.length < {n}) {{ s += s; }} console.log(s.slice(0, {n}));"


def start(repo: Path, page: Path):
    exe = repo / "build/examples/ctdrive"
    if not exe.exists():
        sys.exit(f"ctdrive-reply: {exe} not built - cmake --build build --target ctdrive")
    proc = subprocess.Popen(
        [str(exe), str(page), "--port", "0", "--size", "1024", "768"],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, cwd=repo,
        env={"CTBROWSER_FONTS": "font8x8", "CTBROWSER_NETWORK": "0", "PATH": "/usr/bin:/bin"},
    )
    deadline = time.time() + 30
    while time.time() < deadline:
        line = proc.stdout.readline()
        if not line:
            break
        hit = re.search(r"listening on 127\.0\.0\.1:(\d+)", line)
        if hit:
            return proc, int(hit.group(1))
    proc.kill()
    sys.exit("ctdrive-reply: ctdrive never printed a port")


def run_case(port: int, want: int, read_delay: float, rcvbuf: int | None):
    s = socket.create_connection(("127.0.0.1", port), timeout=20)
    if rcvbuf:
        # A small receive buffer closes the sender's window sooner, so the
        # short-write window is reached with less data in flight.
        s.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, rcvbuf)
    s.sendall((json.dumps({"cmd": "eval", "script": BUILD_STRING.format(n=want)}) + "\n").encode())
    if read_delay:
        time.sleep(read_delay)
    s.settimeout(30)
    buf = b""
    try:
        while not buf.endswith(b"\n"):
            got = s.recv(65536)
            if not got:
                break              # peer closed before a newline: the tail was lost
            buf += got
    except socket.timeout:
        pass                       # never completed a line: the tail was never sent
    s.close()

    if not buf.endswith(b"\n"):
        return False, f"TRUNCATED at {len(buf)} bytes, no newline"
    logged = "".join(json.loads(buf.decode()).get("console", []))
    if len(logged) != want:
        return False, f"WRONG LENGTH: logged {len(logged)} of {want}"
    return True, f"intact, {len(buf)} bytes"


CASES = {
    # name:    (payload,  read delay, SO_RCVBUF)
    "drain": (262_144, 0.0, None),
    "stall": (4_000_000, 3.0, 8192),
}


def main() -> int:
    ap = argparse.ArgumentParser(prog="ctdrive-reply.py", description=__doc__.splitlines()[0])
    ap.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[2])
    ap.add_argument("--case", choices=sorted(CASES) + ["both"], default="both")
    args = ap.parse_args()

    page = args.repo / "examples/pages/bootstrap-box.html"
    names = sorted(CASES) if args.case == "both" else [args.case]
    failures = 0
    for name in names:
        want, delay, rcvbuf = CASES[name]
        proc, port = start(args.repo, page)
        try:
            ok, detail = run_case(port, want, delay, rcvbuf)
        finally:
            proc.kill()
        failures += 0 if ok else 1
        print(f"{'ok  ' if ok else 'FAIL'} {name:6} payload={want:>9,}  {detail}")
    if failures:
        print(f"\n{failures} case(s) failed: reply() is losing bytes on a short write.")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
