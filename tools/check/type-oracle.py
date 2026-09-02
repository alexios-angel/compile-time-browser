#!/usr/bin/env python3
"""Check a static type claim against what the interpreter actually saw.

ctcompile Phase 54B, the checking half.  `--record-types` on the interpreter
writes a recording: per (program, function, register), the SET of types that
register actually held while a corpus ran.  This reads one of those and one
set of static claims and answers two questions that must never be conflated:

  SOUNDNESS   did the inference ever claim a type NARROWER than what was
              observed?  `i32` where the interpreter saw 0.5 is a DEFECT.  The
              tool names the program, the function and the register.
  PRECISION   how often did the claim beat "boxed"?  Low precision is a
              backlog item; it is not a bug and it is not reported as one.

And a third number that is neither, kept apart from both:

  UNOBSERVED  a register no execution ever reached has NO observation.
              "Unobserved" is NOT "any type".  Counting it as sound inflates
              the soundness number; counting it as imprecise deflates the
              precision one.  Neither is done: it gets its own line.

There is no static inference yet - Phase 54A is not written - so this carries
two stubs, and running them is what proves the checker measures anything:

  --infer all-i32     deliberately WRONG: "every register is an i32".  It must
                      produce violations, and name them.  A checker that has
                      never caught anything is not known to work.
  --infer all-boxed   deliberately trivial: "every register is boxed".  It must
                      produce ZERO violations and near-zero precision.

A real inference passes its claims in a file instead:

  claim <program-hash> <function-index> <register> <atom>[,<atom>...]

with the atoms below.  `boxed` is the top of the lattice and an empty claim is
the bottom - claiming bottom for a register the interpreter reached is a
violation like any other, which is the point.
"""

from __future__ import annotations

import argparse
import sys
from dataclasses import dataclass, field

# --- the observation encoding, which MUST match type_record.hpp --------------
#
# Duplicated on purpose and asserted on, not shared: this is the checker, and a
# checker that reads its own definitions out of the thing it is checking cannot
# fail for the one reason that matters.  The recording's header line carries the
# opcode count and the writer count so a drift shows up as a mismatch rather
# than as a quietly different answer.
OBS_UNDEFINED = 1 << 0
OBS_NULL = 1 << 1
OBS_BOOLEAN = 1 << 2
OBS_NUMBER = 1 << 3
OBS_HEAP_SHIFT = 4

# `heap_kind`, in enum order - value.hpp.
HEAP_KINDS = [
    "str",
    "obj",
    "arr",
    "fn",
    "native",
    "cell",
    "sym",
    "proxy",
    "bigint",
    "coroutine",
]

NUM_SEEN = 1 << 0
NUM_FRACTIONAL = 1 << 1
NUM_WIDE = 1 << 2
NUM_NEGATIVE_ZERO = 1 << 3
NUM_NAN = 1 << 4
NUM_INFINITE = 1 << 5
NUM_NOT_I32 = NUM_FRACTIONAL | NUM_WIDE | NUM_NEGATIVE_ZERO | NUM_NAN | NUM_INFINITE

# Every atom a claim may name.  `i32` and `f64` both denote numbers; they differ
# in what they can HOLD, which is exactly the distinction the oracle exists to
# police.
ATOMS = ["undefined", "null", "bool", "i32", "f64"] + HEAP_KINDS
BOXED = frozenset(a for a in ATOMS if a != "i32")  # i32 is subsumed by f64

NUM_FLAG_NAMES = [
    (NUM_FRACTIONAL, "fractional"),
    (NUM_WIDE, "wide"),
    (NUM_NEGATIVE_ZERO, "-0"),
    (NUM_NAN, "NaN"),
    (NUM_INFINITE, "Inf"),
]


@dataclass
class Register:
    reg: int
    defs: int
    kinds: int
    numbers: int

    def observed_atoms(self) -> frozenset[str]:
        """The SMALLEST set of atoms that covers everything this register held.

        This is the whole comparison.  A number is `i32` only when every number
        observed in it was integral, inside int32, and never -0, NaN or an
        infinity; otherwise the smallest thing that covers it is `f64`.
        """
        out = set()
        if self.kinds & OBS_UNDEFINED:
            out.add("undefined")
        if self.kinds & OBS_NULL:
            out.add("null")
        if self.kinds & OBS_BOOLEAN:
            out.add("bool")
        if self.kinds & OBS_NUMBER:
            out.add("f64" if self.numbers & NUM_NOT_I32 else "i32")
        for i, name in enumerate(HEAP_KINDS):
            if self.kinds & (1 << (OBS_HEAP_SHIFT + i)):
                out.add(name)
        return frozenset(out)

    def why(self) -> str:
        """A human-readable reason, for the violation report."""
        bits = sorted(self.observed_atoms())
        if self.kinds & OBS_NUMBER and self.numbers & NUM_NOT_I32:
            flags = [n for f, n in NUM_FLAG_NAMES if self.numbers & f]
            bits = [b + "(" + ",".join(flags) + ")" if b == "f64" else b for b in bits]
        return "{" + ",".join(bits) + "}"


@dataclass
class Function:
    index: int
    entries: int
    params: int
    frame: int
    name: str
    regs: dict[int, Register] = field(default_factory=dict)


@dataclass
class Program:
    source_hash: str
    size: int
    nfunctions: int
    label: str
    functions: dict[int, Function] = field(default_factory=dict)


@dataclass
class Recording:
    opcodes: int = 0
    writers: int = 0
    recorded: int = 0
    dropped: int = 0
    orphans: int = 0
    programs: dict[str, Program] = field(default_factory=dict)


def read_recording(path: str) -> Recording:
    rec = Recording()
    program = None
    function = None
    with open(path, "r", encoding="utf-8") as handle:
        header = handle.readline().split()
        if header[:1] != ["ctbrowser-type-recording"]:
            raise SystemExit(f"{path}: not a type recording")
        # VERSION 2 IS VERSION 1 PLUS THE ESCAPE HALF (ctcompile Phase 55O):
        # an `escape` header line, `alloc` and `site` lines under each `fn`.
        # This checker is about registers and reads past them; the escape
        # lines have their own checker in escape-oracle.py.
        if header[1] not in ("1", "2"):
            raise SystemExit(f"{path}: recording version {header[1]}, this reads 1 and 2")
        for line in handle:
            parts = line.split()
            if not parts:
                continue
            tag = parts[0]
            if tag in ("escape", "alloc", "site"):
                continue
            if tag == "opcodes":
                rec.opcodes, rec.writers = int(parts[1]), int(parts[3])
            elif tag == "defs":
                rec.recorded, rec.dropped, rec.orphans = (
                    int(parts[2]),
                    int(parts[4]),
                    int(parts[6]),
                )
            elif tag == "programs":
                pass
            elif tag == "program":
                program = Program(parts[1], int(parts[3]), int(parts[5]), parts[7])
                rec.programs[program.source_hash] = program
                function = None
            elif tag == "fn":
                assert program is not None, "an `fn` line before any `program` line"
                function = Function(
                    int(parts[1]), int(parts[3]), int(parts[5]), int(parts[7]), parts[9]
                )
                program.functions[function.index] = function
            elif tag == "r":
                assert function is not None, "an `r` line before any `fn` line"
                reg = Register(
                    int(parts[1]), int(parts[3]), int(parts[5], 16), int(parts[7], 16)
                )
                function.regs[reg.reg] = reg
            else:
                raise SystemExit(f"{path}: unknown line `{tag}`")
    # THE FILE MUST ACCOUNT FOR ITSELF.  A truncated recording reads as a
    # program that simply ran less, and every number below would be a smaller
    # true-looking fraction of a smaller true-looking total.
    for prog in rec.programs.values():
        if len(prog.functions) != prog.nfunctions:
            raise SystemExit(
                f"{path}: program {prog.source_hash} declares {prog.nfunctions} "
                f"functions and carries {len(prog.functions)}"
            )
    return rec


# --- the stub inferences -----------------------------------------------------
#
# Neither is a real analysis and neither pretends to be.  They exist so that the
# checker can be shown to catch something and shown not to invent something,
# which are the only two ways a measuring instrument can be wrong.


def stub_all_i32(_program, _function, _reg) -> frozenset[str]:
    """Deliberately WRONG.  Every register is an i32."""
    return frozenset({"i32"})


def stub_all_boxed(_program, _function, _reg) -> frozenset[str]:
    """Deliberately trivial, and SOUND by construction.  Everything is boxed."""
    return BOXED


STUBS = {"all-i32": stub_all_i32, "all-boxed": stub_all_boxed}


def read_claims(path: str) -> dict[tuple[str, int, int], frozenset[str]]:
    claims: dict[tuple[str, int, int], frozenset[str]] = {}
    with open(path, "r", encoding="utf-8") as handle:
        for number, line in enumerate(handle, 1):
            line = line.split("#", 1)[0].strip()
            if not line:
                continue
            parts = line.split()
            if len(parts) != 5 or parts[0] != "claim":
                raise SystemExit(f"{path}:{number}: expected `claim <hash> <fn> <reg> <atoms>`")
            atoms = frozenset(a for a in parts[4].split(",") if a)
            if "boxed" in atoms:
                atoms = BOXED
            unknown = atoms - set(ATOMS)
            if unknown:
                raise SystemExit(f"{path}:{number}: unknown atom(s) {sorted(unknown)}")
            claims[(parts[1], int(parts[2]), int(parts[3]))] = atoms
    return claims


def covers(claim: frozenset[str], observed: frozenset[str]) -> bool:
    """Does `claim` admit everything that was observed?

    `f64` admits `i32`; the reverse is the violation this whole file is about.
    """
    for atom in observed:
        if atom in claim:
            continue
        if atom == "i32" and "f64" in claim:
            continue
        return False
    return True


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--recording", required=True)
    ap.add_argument("--infer", choices=sorted(STUBS), help="use a built-in stub inference")
    ap.add_argument("--claims", help="a claims file from a real inference")
    ap.add_argument("--name", default="", help="what to call this corpus in the report")
    ap.add_argument("--max-report", type=int, default=10, help="violations to name (0 = all)")
    ap.add_argument("--expect-violations", type=int, help="fail unless the count is exactly this")
    ap.add_argument("--expect-observed", type=int, help="fail unless the count is exactly this")
    ap.add_argument("--expect-unobserved", type=int, help="fail unless the count is exactly this")
    ap.add_argument("--expect-precision", type=int, help="fail unless beat-boxed is exactly this")
    args = ap.parse_args()

    if (args.infer is None) == (args.claims is None):
        raise SystemExit("exactly one of --infer and --claims")

    rec = read_recording(args.recording)
    claims = read_claims(args.claims) if args.claims else None
    stub = STUBS[args.infer] if args.infer else None

    observed = 0
    unobserved = 0
    violations = []
    beat_boxed = 0
    exact = 0
    unverifiable = 0  # a claim on a register nothing reached
    unclaimed = 0  # an observed register the inference said nothing about

    for phash, prog in sorted(rec.programs.items()):
        for index in sorted(prog.functions):
            fn = prog.functions[index]
            for reg in range(fn.frame):
                seen = fn.regs.get(reg)
                if stub is not None:
                    claim = stub(phash, index, reg)
                else:
                    claim = claims.get((phash, index, reg))
                if seen is None:
                    unobserved += 1
                    if claim is not None:
                        unverifiable += 1
                    continue
                observed += 1
                if claim is None:
                    unclaimed += 1
                    continue
                want = seen.observed_atoms()
                if not covers(claim, want):
                    violations.append((phash, fn, seen, claim, want))
                if claim != BOXED:
                    beat_boxed += 1
                if claim == want:
                    exact += 1

    label = args.name or args.recording
    which = args.infer or args.claims
    print(f"== type oracle: {label} vs `{which}`")
    print(f"   programs {len(rec.programs)}  functions "
          f"{sum(len(p.functions) for p in rec.programs.values())}")
    print(f"   defs recorded {rec.recorded}  dropped {rec.dropped}  "
          f"orphan-frames {rec.orphans}")
    print(f"   observed registers   {observed}")
    print(f"   unobserved registers {unobserved}   "
          "(never executed - NOT `any type`)")
    if unclaimed:
        print(f"   observed but unclaimed {unclaimed}")
    if unverifiable:
        print(f"   claims on unobserved registers {unverifiable}   (unverifiable, not counted)")

    denominator = observed - unclaimed
    print(f"   SOUNDNESS violations {len(violations)}"
          + (f" of {denominator} checked" if denominator else ""))
    pct = (100.0 * beat_boxed / denominator) if denominator else 0.0
    epct = (100.0 * exact / denominator) if denominator else 0.0
    print(f"   PRECISION beat-boxed {beat_boxed}/{denominator} = {pct:.1f}%   "
          f"exact {exact}/{denominator} = {epct:.1f}%")

    shown = violations if args.max_report == 0 else violations[: args.max_report]
    for phash, fn, seen, claim, want in shown:
        print(
            f"   VIOLATION program {phash} function {fn.index} "
            f"({fn.name}) register {seen.reg}: claimed "
            "{" + ",".join(sorted(claim)) + "}, observed " + seen.why()
            + f" over {seen.defs} def(s)"
        )
    if len(violations) > len(shown):
        print(f"   ... and {len(violations) - len(shown)} more")

    # --- the asserted counters -------------------------------------------
    failed = False
    for got, want, what in (
        (len(violations), args.expect_violations, "violations"),
        (observed, args.expect_observed, "observed registers"),
        (unobserved, args.expect_unobserved, "unobserved registers"),
        (beat_boxed, args.expect_precision, "beat-boxed registers"),
    ):
        if want is not None and got != want:
            print(f"   FAIL expected {what} = {want}, got {got}", file=sys.stderr)
            failed = True
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
