#!/usr/bin/env python3
"""Measure the native C++ backend's output: emitted bytes, compile time, binary size and run time.

Per program, per spelling, per toolchain: emitted bytes, C++ compile seconds,
stripped binary bytes, run time.  Measured, never estimated, on the devbox the
project actually builds on.

    flock /tmp/ctbrowser-devbox-build.lock \\
      python3 tools/check/native-ab.py --build build --out ctcompile/docs/native-ab.md

What is measured, and why each number is shaped the way it is
-------------------------------------------------------------

*   **Emitted bytes** are the bytes `ctjs-translate --mlir-to-cpp` writes for
    the module the native pipeline produced.  Three counts per program: the
    plain module (every type spelled), the deduced module (`auto` plus a
    `static_assert` pin per deduced declaration), and the deduced module with
    the pins, their macro block and the `<type_traits>` they need removed.
    The third is a synthetic count, computed by the same rule as
    ctcompile/test/CTNative/Checks/print-deduced.py, so the two agree by
    construction.

*   **Compile seconds** are wall time for the command compile-clean.py runs
    (GATE_FLAGS below), on both g++ and clang++, since the generated C++ must
    compile clean on both.  One untimed compile warms the page cache, then N
    timed runs are reported as `median [min-max]`; the devbox is shared, so a
    single sample would mostly measure other builds.

*   **Stripped binary bytes** are `strip -s` on the linked executable.  This is
    the one column with no measurement noise in it at all.

*   **Run time** is dominated by process start-up: these fixtures are a few
    dozen top-level statements, so the process spends nearly all of its life
    in `execve`, the dynamic loader and libc start-up.  The script therefore
    measures the floor explicitly (an empty `int main() { return 0; }` built by
    the same compiler with the same flags) and reports it beside the programs.
    A wall-clock number inside the floor's own spread says nothing about the
    program.

    The number worth reading is the callgrind instruction count: `valgrind
    --tool=callgrind` counts instructions deterministically, so other load on
    the machine does not affect it, and the devbox VM has no hardware
    performance counters.  Two counts are reported: the whole process, and
    `main` and its callees alone (`--collect-atstart=no
    --toggle-collect=main`).  The second is the program's own work; the first
    is mostly the dynamic loader.  Subtracting an empty binary's total does not
    isolate the program: g++ links libstdc++ only when the program uses it
    (--as-needed), so the difference measures dynamic linking.

This script does not vary the specialisation cap (`ctnative-specialize
--max-variants`); every program is measured with the default pipeline.
"""

import argparse
import os
import re
import shutil
import statistics
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "ctcompile/test"))
from Target.Cpp.harness import FLAGS as GATE_FLAGS, native_link_command

PROGRAMS = ["numeric", "functions", "structs", "arrays"]


def run(cmd, **kw):
    """Run `cmd`, capturing stdout and stderr as text."""
    return subprocess.run(cmd, capture_output=True, text=True, **kw)


def emit(translate, module):
    """The C++ the forked emitter writes for one EmitC module."""
    got = run([str(translate), "--mlir-to-cpp", str(module)])
    if got.returncode != 0:
        sys.exit("native-ab: %s refused %s:\n%s" % (translate, module, got.stderr))
    if not got.stdout:
        sys.exit("native-ab: %s produced nothing for %s" % (translate, module))
    return got.stdout


def without_pins(text):
    """The deduced file with its pins removed.

    Uses the same rule as ctcompile/test/CTNative/Checks/print-deduced.py so the
    byte counts are comparable: drop the `#ifndef CTCOMPILE_NO_TYPE_PINS ...
    #endif` block, the `#include <type_traits>` the pins need, and every
    `CTCOMPILE_PIN(` line.
    """
    out = []
    in_macro = False
    for line in text.splitlines(keepends=True):
        if line.startswith("#ifndef CTCOMPILE_NO_TYPE_PINS"):
            in_macro = True
        if in_macro:
            if line.startswith("#endif"):
                in_macro = False
            continue
        if line.startswith("#include <type_traits>"):
            continue
        if re.match(r"^[ \t]*CTCOMPILE_PIN\(", line):
            continue
        out.append(line)
    return "".join(out)


def compile_once(cxx, src, exe, defines, build):
    """Compile `src` to `exe` under GATE_FLAGS; return wall seconds, exit on failure."""
    cmd = native_link_command([cxx, *GATE_FLAGS, *defines, str(src), "-o", str(exe)], build)
    start = time.perf_counter()
    got = subprocess.run(cmd, capture_output=True, text=True)
    elapsed = time.perf_counter() - start
    if got.returncode != 0:
        sys.exit(
            "native-ab: the generated file did not compile clean, which is a "
            "Phase 63 Step 7 failure and not a measurement:\n"
            + " ".join(cmd)
            + "\n"
            + got.stdout
            + got.stderr
        )
    return elapsed


def time_compiles(cxx, src, exe, defines, reps, build):
    compile_once(cxx, src, exe, defines, build)  # warm the cache; untimed on purpose
    return [compile_once(cxx, src, exe, defines, build) for _ in range(reps)]


def stripped_bytes(exe, workdir):
    copy = Path(workdir) / (exe.name + ".stripped")
    shutil.copy2(str(exe), str(copy))
    got = run(["strip", "-s", str(copy)])
    if got.returncode != 0:
        sys.exit("native-ab: strip failed on %s:\n%s" % (exe, got.stderr))
    return copy.stat().st_size


def time_runs(exe, reps):
    with open(os.devnull, "wb") as sink:
        subprocess.run([str(exe)], stdout=sink, stderr=sink)  # warm
        out = []
        for _ in range(reps):
            start = time.perf_counter()
            got = subprocess.run([str(exe)], stdout=sink, stderr=sink)
            out.append(time.perf_counter() - start)
            if got.returncode != 0:
                sys.exit("native-ab: %s exited %d" % (exe, got.returncode))
    return out


def callgrind_ir(exe, workdir, main_only):
    """Instructions executed, deterministically. None when valgrind is absent.

    With `main_only`, passes `--collect-atstart=no --toggle-collect=main`, so
    the count is the instructions executed inside `main` and its callees only;
    that is the number to read.

    Do not subtract an empty binary's total instead: g++ links libstdc++ only
    when the program uses it (--as-needed), so the difference measures dynamic
    linking, not the program. The toggle needs no baseline and is comparable
    across both toolchains.
    """
    if shutil.which("valgrind") is None:
        return None
    out = Path(workdir) / (exe.name + (".main" if main_only else "") + ".callgrind")
    cmd = ["valgrind", "--tool=callgrind", "--callgrind-out-file=" + str(out)]
    if main_only:
        cmd += ["--collect-atstart=no", "--toggle-collect=main"]
    cmd.append(str(exe))
    got = run(cmd)
    # valgrind prints the summary on stderr: "==pid== Collected : 1234567"
    found = re.search(r"Collected\s*:\s*([0-9,]+)", got.stderr)
    if found is None and out.exists():
        found = re.search(r"^summary:\s*([0-9]+)", out.read_text(), re.M)
    if found is None:
        return None
    return int(found.group(1).replace(",", ""))


def spread(samples):
    """(min, median, max) of `samples`."""
    return (min(samples), statistics.median(samples), max(samples))


def fmt_seconds(s):
    lo, mid, hi = s
    return "%.3f [%.3f-%.3f]" % (mid, lo, hi)


def fmt_ms(s):
    lo, mid, hi = s
    return "%.2f [%.2f-%.2f]" % (mid * 1e3, lo * 1e3, hi * 1e3)


def find_translate(build):
    """The shortest-path executable named ctjs-translate under `build`."""
    found = [f for f in build.rglob("ctjs-translate") if f.is_file() and os.access(f, os.X_OK)]
    if not found:
        sys.exit("native-ab: no ctjs-translate under %s" % build)
    found.sort(key=lambda p: len(str(p)))
    return found[0]


def measure(rows, floor, name, label, src, defines, bytes_, cxx, workdir, args):
    """Compile, strip, time and callgrind one (program, spelling, compiler) cell;
    append it to `rows` and print it."""
    tag = re.sub(r"[^A-Za-z0-9]+", "_", "%s.%s.%s" % (name, label, cxx))
    exe = Path(workdir) / tag
    compiles = time_compiles(cxx, src, exe, defines, args.reps, args.build)
    runs = time_runs(exe, args.run_reps)
    binary = stripped_bytes(exe, workdir)
    ir = None if args.no_callgrind else callgrind_ir(exe, workdir, False)
    ir_main = None if args.no_callgrind else callgrind_ir(exe, workdir, True)
    rows.append(
        {
            "program": name,
            "variant": label,
            "cxx": cxx,
            "emitted": bytes_,
            "compile": spread(compiles),
            "binary": binary,
            "run": spread(runs),
            "ir": ir,
            "ir_main": ir_main,
        }
    )
    print(
        "%-10s %-33s %-8s emit %6d B  compile %s s  bin %7d B  run %s ms  Ir(main) %s  Ir(total) %s"
        % (
            name,
            label,
            cxx,
            bytes_,
            fmt_seconds(spread(compiles)),
            binary,
            fmt_ms(spread(runs)),
            "-" if ir_main is None else str(ir_main),
            "-" if ir is None else str(ir),
        ),
        flush=True,
    )


def render(rows, floor, versions, args, provenance):
    out = []
    w = out.append
    w("<!-- generated by tools/check/native-ab.py - do not hand-edit the tables -->")
    w("")
    # Print host, build tree and emitter inside the report: the devbox is shared,
    # and a timing table without provenance could describe someone else's tree.
    w(
        "**Provenance.** host `%s`, build tree `%s`, emitter `%s`, %s UTC."
        % (provenance["host"], provenance["build"], provenance["translate"], provenance["when"])
    )
    w("")
    w(
        "| program | spelling | toolchain | emitted B | compile s (median [min-max]) "
        "| stripped B | run ms (median [min-max]) | Ir in `main` | Ir total |"
    )
    w("|---|---|---|---:|---|---:|---|---:|---:|")
    for r in rows:
        w(
            "| %s | `%s` | %s | %s | %s | %s | %s | %s | %s |"
            % (
                r["program"],
                r["variant"],
                r["cxx"],
                "{:,}".format(r["emitted"]),
                fmt_seconds(r["compile"]),
                "{:,}".format(r["binary"]),
                fmt_ms(r["run"]),
                "-" if r["ir_main"] is None else "{:,}".format(r["ir_main"]),
                "-" if r["ir"] is None else "{:,}".format(r["ir"]),
            )
        )
    w("")
    w("### The noise floor")
    w("")
    w("`int main() { return 0; }`, same flags, same repetitions, same box, same hour.")
    w("")
    w("| toolchain | version | compile s | stripped B | run ms | Ir in `main` | Ir total |")
    w("|---|---|---|---:|---|---:|---:|")
    for cxx in versions:
        f = floor[cxx]
        w(
            "| %s | %s | %s | %s | %s | %s | %s |"
            % (
                cxx,
                versions[cxx],
                fmt_seconds(f["compile"]),
                "{:,}".format(f["bytes"]),
                fmt_ms(f["run"]),
                "-" if f["ir_main"] is None else "{:,}".format(f["ir_main"]),
                "-" if f["ir"] is None else "{:,}".format(f["ir"]),
            )
        )
    w("")
    w(
        "Repetitions: %d timed compiles and %d timed runs per cell, after one untimed "
        "warm-up of each. `Ir in main` is callgrind under "
        "`--collect-atstart=no --toggle-collect=main`, so it counts the program's own "
        "work and nothing of the loader, the C++ runtime's static initialisation or "
        "libc start-up; `Ir total` is the whole process, which is what the wall clock "
        "is mostly measuring." % (args.reps, args.run_reps)
    )
    return "\n".join(out) + "\n"


def main():
    ap = argparse.ArgumentParser(description="Phase 63 Step 4: the four numbers")
    ap.add_argument("--build", default="build", type=Path)
    ap.add_argument("--translate", type=Path)
    ap.add_argument("--programs", default=",".join(PROGRAMS))
    ap.add_argument("--compilers", default="g++,clang++")
    ap.add_argument("--reps", type=int, default=7, help="timed compiles per cell")
    ap.add_argument("--run-reps", type=int, default=100, help="timed runs per cell")
    ap.add_argument("--out", type=Path, help="write the Markdown report here")
    ap.add_argument("--no-callgrind", action="store_true")
    args = ap.parse_args()

    args.build = build = args.build.resolve()
    translate = args.translate or find_translate(build)
    tests = build / "ctcompile" / "test"
    provenance = {
        "host": os.uname().nodename,
        "build": str(build),
        "translate": str(translate),
        "when": time.strftime("%Y-%m-%d %H:%M", time.gmtime()),
    }
    print("native-ab: measuring %s on %s" % (build, provenance["host"]), flush=True)
    programs = [p for p in args.programs.split(",") if p]
    compilers = [c for c in args.compilers.split(",") if c]
    for cxx in compilers:
        if shutil.which(cxx) is None:
            sys.exit("native-ab: no %s on PATH - a missing toolchain is not a measurement" % cxx)

    versions = {}
    for cxx in compilers:
        versions[cxx] = run([cxx, "--version"]).stdout.splitlines()[0]

    workdir = Path(tempfile.mkdtemp(prefix="native-ab-"))

    # The noise floor, per compiler: an empty program built with the same flags.
    # Wall-clock numbers for a fixture must be read against it. Callgrind counts
    # are reported raw, not as differences (see callgrind_ir for why).
    floor = {}
    empty = workdir / "empty.cpp"
    empty.write_text("int main() { return 0; }\n")
    for cxx in compilers:
        exe = workdir / ("empty_" + re.sub(r"[^A-Za-z0-9]+", "_", cxx))
        compiles = time_compiles(cxx, empty, exe, [], args.reps, build)
        runs = time_runs(exe, args.run_reps)
        floor[cxx] = {
            "compile": spread(compiles),
            "run": spread(runs),
            "bytes": stripped_bytes(exe, workdir),
            "ir": None if args.no_callgrind else callgrind_ir(exe, workdir, False),
            "ir_main": None if args.no_callgrind else callgrind_ir(exe, workdir, True),
        }
        print(
            "%-10s %-33s %-8s               compile %s s  bin %7d B  run %s ms  "
            "Ir(main) %s  Ir(total) %s"
            % (
                "(floor)",
                "int main() { return 0; }",
                cxx,
                fmt_seconds(floor[cxx]["compile"]),
                floor[cxx]["bytes"],
                fmt_ms(floor[cxx]["run"]),
                "-" if floor[cxx]["ir_main"] is None else str(floor[cxx]["ir_main"]),
                "-" if floor[cxx]["ir"] is None else str(floor[cxx]["ir"]),
            ),
            flush=True,
        )

    rows = []
    for name in programs:
        variants = {
            "plain": tests / ("%s.pipeline.emitc.mlir" % name),
            "deduced": tests / ("%s.pipeline.deduced.emitc.mlir" % name),
        }
        for module in variants.values():
            if not module.is_file():
                sys.exit(
                    "native-ab: %s is missing - build the tree with CTCOMPILE_MLIR=ON "
                    "first; measuring a program that was never lowered is the vacuous "
                    "pass this project keeps finding" % module
                )
        text = dict((k, emit(translate, v)) for k, v in variants.items())
        nopins_text = without_pins(text["deduced"])
        sources = {}
        for key in ("plain", "deduced"):
            src = workdir / ("%s.%s.cpp" % (name, key))
            src.write_text(text[key])
            sources[key] = src

        # The three variants compiled. `deduced` and `deduced
        # -DCTCOMPILE_NO_TYPE_PINS` are the same file; the define separates the
        # cost of the static_assert pins from the saving of `auto`. Its
        # emitted-byte column is the synthetic count (the file as if the pins
        # were never printed).
        cells = [
            ("plain", sources["plain"], [], len(text["plain"].encode())),
            ("deduced", sources["deduced"], [], len(text["deduced"].encode())),
            (
                "deduced -DCTCOMPILE_NO_TYPE_PINS",
                sources["deduced"],
                ["-DCTCOMPILE_NO_TYPE_PINS"],
                len(nopins_text.encode()),
            ),
        ]
        for label, src, defines, bytes_ in cells:
            for cxx in compilers:
                measure(rows, floor, name, label, src, defines, bytes_, cxx, workdir, args)

    report = render(rows, floor, versions, args, provenance)
    if args.out:
        args.out.write_text(report)
        print("native-ab: wrote %s" % args.out)
    else:
        print(report)


if __name__ == "__main__":
    main()
