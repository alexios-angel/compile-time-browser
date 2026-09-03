#!/usr/bin/env python3
"""THE FOUR NUMBERS - ctcompile part 24 Phase 63 Step 4.

Per program, per spelling, per toolchain: emitted bytes, C++ compile seconds,
stripped binary bytes, run time.  Measured, never estimated, on the devbox the
project actually builds on.

    flock /tmp/ctbrowser-devbox-build.lock \\
      python3 tools/check/native-ab.py --build build --out ctcompile/docs/native-ab.md

WHAT IS MEASURED, AND WHY EACH ONE IS SHAPED THE WAY IT IS
----------------------------------------------------------

*   **Emitted bytes** are the bytes `ctjs-translate --mlir-to-cpp` writes for
    the module the native pipeline produced.  Three counts per program: the
    plain module (every type spelled), the deduced module (`auto` plus a
    `static_assert` pin per deduced declaration), and the deduced module with
    the pins, their macro block and the `<type_traits>` they need removed.
    The third is a synthetic count and is computed by the SAME rule
    check-print-deduced.cmake uses, so the two agree by construction rather
    than by coincidence.

*   **Compile seconds** are wall time for the exact command the Phase 63 Step 7
    gate runs - `-std=c++23 -O2 -pedantic -Wall -Wextra -Werror -Wconversion
    -ffp-contract=off` - on BOTH g++ and clang++, because Step 7 requires both
    and a compile-time number from one of them is half an answer.  One untimed
    compile warms the page cache first; then N timed repetitions, and the
    report is `median [min-max]`.  THE BOX IS SHARED by several agents
    building at once, so a single number here would be a number about who else
    was compiling.

*   **Stripped binary bytes** are `strip -s` on the linked executable.  This is
    the one column with no measurement noise in it at all.

*   **Run time** is where an honest report costs something.  These fixtures are
    a few dozen top-level statements; the process spends essentially all of its
    life in `execve`, the dynamic loader and libc start-up.  So this script
    measures the FLOOR explicitly - an empty `int main() { return 0; }`
    compiled and linked by the same compiler with the same flags - and reports
    the program against it.  A wall-clock number that is inside the floor's own
    spread is a statement about `fork` and not about the program, and the
    report says so rather than printing it as a result.

    Beside it, and this is the number worth reading, is a CALLGRIND
    instruction count: `valgrind --tool=callgrind` counts instructions
    deterministically, so it is immune to the other agents on this box.  Part
    24 Step 4 asks for callgrind by name for this reason: "No hardware
    performance counters exist in the devbox VM".  Two counts are reported -
    the whole process, and `main` and its callees alone
    (`--collect-atstart=no --toggle-collect=main`).  THE SECOND IS THE
    PROGRAM'S OWN WORK and the first is mostly the dynamic loader.  Isolating
    the program by subtracting an empty binary's total does NOT work, and the
    first run of this script proved it: g++ drops libstdc++ from an empty
    translation unit under --as-needed and keeps it for one that says
    `std::vector`, so the `arrays` fixture's "body" came out at 1.73 M
    instructions against 16 K for `structs`.  That was a number about dynamic
    linking wearing a program's name.

WHAT THIS SCRIPT DOES NOT REPORT, AND WHY
------------------------------------------

Step 4 also asks for the numbers "for the specialisation cap at 1 (never
specialise) and at 4".  THAT AXIS DOES NOT EXIST YET: Phase 62 1/2-B is at
option 3 (the join) only - there is no specialisation, no `dyn<...>`, and no
cap knob on any pass in `CTNative/Transforms/Passes.td`.  Reporting a cap
column filled with the same number twice would be a measurement of nothing.
It is left out and said out loud instead, which is what the corrections log in
part 24 exists to encourage.
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

# THE GATE'S OWN FLAGS. Spelled once here, and check-compile-clean.cmake and
# check-native-unit.cmake spell the same list; a compile-time number measured
# under different flags is a number about a different compile.
GATE_FLAGS = [
    "-std=c++23",
    "-O2",
    "-pedantic",
    "-Wall",
    "-Wextra",
    "-Werror",
    "-Wconversion",
    "-ffp-contract=off",
]

PROGRAMS = ["numeric", "functions", "structs", "arrays"]


def run(cmd, **kw):
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

    THE SAME RULE AS check-print-deduced.cmake, which is what makes the byte
    count here comparable with the one that gate prints: drop the
    `#ifndef CTCOMPILE_NO_TYPE_PINS ... #endif` macro block, the
    `#include <type_traits>` the pins need, and every `CTCOMPILE_PIN(` line.
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


def compile_once(cxx, src, exe, defines):
    cmd = [cxx] + GATE_FLAGS + list(defines) + [str(src), "-o", str(exe)]
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


def time_compiles(cxx, src, exe, defines, reps):
    compile_once(cxx, src, exe, defines)  # warm the cache; untimed on purpose
    return [compile_once(cxx, src, exe, defines) for _ in range(reps)]


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

    TWO NUMBERS, AND THE SECOND IS THE ONE TO READ. `main_only` passes
    `--collect-atstart=no --toggle-collect=main`, so the count is the
    instructions executed INSIDE `main` and its callees and nothing else.

    THE FIRST MEASUREMENT OF THIS SCRIPT NEEDED THAT FIX. Subtracting an empty
    binary's total from a program's total looks like it isolates the program,
    and it does not: `g++` drops libstdc++ from an empty translation unit under
    --as-needed and keeps it for one that says `std::vector`, so the `arrays`
    fixture's "body" came out at 1.73 M instructions against 16 K for
    `structs` - a number about dynamic linking wearing a program's name. The
    toggle needs no baseline at all and is comparable across both toolchains.
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
    return (min(samples), statistics.median(samples), max(samples))


def fmt_seconds(s):
    lo, mid, hi = s
    return "%.3f [%.3f-%.3f]" % (mid, lo, hi)


def fmt_ms(s):
    lo, mid, hi = s
    return "%.2f [%.2f-%.2f]" % (mid * 1e3, lo * 1e3, hi * 1e3)


def find_translate(build):
    found = [f for f in build.rglob("ctjs-translate") if f.is_file() and os.access(f, os.X_OK)]
    if not found:
        sys.exit("native-ab: no ctjs-translate under %s" % build)
    found.sort(key=lambda p: len(str(p)))
    return found[0]


def measure(rows, floor, name, label, src, defines, bytes_, cxx, workdir, args):
    tag = re.sub(r"[^A-Za-z0-9]+", "_", "%s.%s.%s" % (name, label, cxx))
    exe = Path(workdir) / tag
    compiles = time_compiles(cxx, src, exe, defines, args.reps)
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
    # WHOSE MEASUREMENT IS THIS. Several agents build on the shared devbox at
    # once, and one of them has already read another's test output out of a
    # fixed /tmp path. A timing table with no provenance line is a table that
    # could be about somebody else's tree, so the host, the build directory
    # and the tool that emitted the C++ are printed INSIDE the artefact.
    w("**Provenance.** host `%s`, build tree `%s`, emitter `%s`, %s UTC."
      % (provenance["host"], provenance["build"], provenance["translate"], provenance["when"]))
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

    build = args.build.resolve()
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

    # THE NOISE FLOOR, per compiler: an empty program through the same flags.
    # Everything the wall clock says about a fixture has to be read against
    # this, and the callgrind counts are differences from it.
    floor = {}
    empty = workdir / "empty.cpp"
    empty.write_text("int main() { return 0; }\n")
    for cxx in compilers:
        exe = workdir / ("empty_" + re.sub(r"[^A-Za-z0-9]+", "_", cxx))
        compiles = time_compiles(cxx, empty, exe, [], args.reps)
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

        # THE THREE SPELLINGS THAT GET COMPILED. `deduced` and
        # `deduced -DCTCOMPILE_NO_TYPE_PINS` are the SAME FILE: the define is
        # what the plan asks to be separated from the `auto` saving, because a
        # static_assert is compile time and (possibly) object code that `auto`
        # alone does not buy. Its emitted-byte column is the synthetic count -
        # what the file would be if the pins were not printed at all.
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
