#!/usr/bin/env python3
"""Check a static escape claim against what the interpreter actually saw.

ctcompile Phase 55O, the checking half.  The recorder (`type-oracle --script`,
header `ctbrowser-type-recording 2`) writes, per (program, function, pc, kind)
allocation SITE, what happened to every object born there by the time the
frame that made it ended: confined (unreachable from every GC root), escaped
(reachable - and through which root category first), unresolved (swept before
the frame ended) or unchecked (the frame ended through a path with no hook, or
the per-function budget was spent).  This reads one recording and one set of
static claims and answers, per site, one line of the verdict table:

  VIOLATION      claimed confined, observed escaped.  A DEFECT - the analysis
                 would have given an RAII lifetime to an object that outlived
                 its frame.  Named by program, function, pc and kind.
  SOUND          claimed confined, every observation confined.
  PARTIAL        claimed confined, some observations confined and the rest
                 unresolved or unchecked.  Not sound; its own line.
  PENDING        claimed confined, no observation resolved either way.
  UNOBSERVED     a claim on a site no execution reached.  Never sound.
  IMPRECISE      claimed escapes, observed only confined.  The backlog, by
                 reason.  It is not a bug.
  EXACT          claimed escapes, observed escaped.
  INCONCLUSIVE   claimed escapes, no clean observation either way (nothing
                 escaped, and nothing was confined without an unresolved or
                 unchecked sibling).  Neither the backlog nor a proof.
  UNCLAIMED      an observed site with no claim: a `construct` result, a
                 native's allocation on the call's pc, a refused function.
                 Never sound.
  KIND MISMATCH  a claim at a (fn, pc) where the interpreter observed a
                 DIFFERENT kind and not this one.  A hard error: the
                 coordinate is wrong and the comparison meaningless.
  UNCHECKABLE    a claim of `owned` or `shared`.  A hard error until a
                 recording version carries reference counts.

PRECISION = SOUND / (SOUND + IMPRECISE), and the IMPRECISE histogram by reason
is the roadmap.

WHAT THE ORACLE CANNOT SEE, said plainly: it observes RETENTION at frame exit,
not transit.  An object handed to a callee that dropped it before the caller
returned reads confined here.  So it can MISS an escape and can never invent
one; a violation is always real, and "zero violations" is a floor, not a proof.

Two stubs prove the instrument, over every static site the recording lists:

  --infer all-confined   deliberately WRONG.  Must produce violations and
                         name them.
  --infer all-escapes    deliberately trivial and sound.  Zero violations,
                         zero precision.

A real analysis passes its claims in a file instead:

  escape <program-hash> <function-index> <pc> <kind> confined|escapes:<reason>
  function <program-hash> <function-index> captures_all_arguments|refused:<reason>
"""

from __future__ import annotations

import argparse
import sys
from dataclasses import dataclass, field

# --- the vocabulary, which MUST match type_record.hpp ------------------------
#
# Duplicated on purpose and asserted on, not shared: a checker that reads its
# definitions out of the thing it is checking cannot fail for the one reason
# that matters.  The recording's header carries the opcode and writer counts so
# a drift shows up as a mismatch rather than as a quietly different answer.
OPCODES = 93
WRITERS = 68

KINDS = ["obj", "arr", "fn", "cell"]

# One per row of ctcompile's GCRoots.def, in `context::each_root` order.
ROOT_LABELS = [
    "globals",
    "registers",
    "current_this",
    "pending_new_target",
    "pending_closure",
    "frame_closure",
    "frame_receiver",
    "frame_arguments",
    "frame_async_promise",
    "frame_new_target",
    "microtasks",
    "module_exports",
    "module_namespace",
    "thrown",
    "temporaries",
    "prototypes",
    "string_cache",
    "bigint_cache",
    "external",
]

# CTNative_EscapeReason, in enum order (plan part 25 §1.3).
REASONS = [
    "returned",
    "thrown",
    "stored",
    "stored_global",
    "captured",
    "passed",
    "converted",
    "proto_mutated",
    "accessor_defined",
    "arguments",
    "runtime_array",
    "phase59",
    "not_tracked",
    "suspended",
    "arguments_late",
    "unknown_op",
    "unvisited",
    "unvisited_operand",
]

UNCHECKABLE_CLAIMS = {"owned", "shared"}

PROLOGUE = -1


@dataclass
class Site:
    pc: int
    kind: str
    made: int
    confined: int
    escaped: int
    unresolved: int
    unchecked: int
    routes: dict[str, int] = field(default_factory=dict)

    def clean(self) -> bool:
        return self.unresolved == 0 and self.unchecked == 0

    def via(self) -> str:
        return ",".join(f"{k}:{n}" for k, n in self.routes.items()) or "-"


@dataclass
class Function:
    index: int
    entries: int
    params: int
    frame: int
    name: str
    allocs: list[tuple[int, str]] = field(default_factory=list)
    sites: dict[tuple[int, str], Site] = field(default_factory=dict)


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
    budget: str = ""
    pops: int = 0
    unwinds: int = 0
    checks: int = 0
    unframed: int = 0
    unresolved: int = 0
    programs: dict[str, Program] = field(default_factory=dict)


def parse_pc(text: str) -> int:
    return PROLOGUE if text == "prologue" else int(text)


def read_recording(path: str) -> Recording:
    rec = Recording()
    program = None
    function = None
    saw_escape_header = False
    with open(path, "r", encoding="utf-8") as handle:
        header = handle.readline().split()
        if header[:1] != ["ctbrowser-type-recording"]:
            raise SystemExit(f"{path}: not a type recording")
        if header[1] != "2":
            raise SystemExit(f"{path}: recording version {header[1]}, the escape half needs 2")
        for line in handle:
            parts = line.split()
            if not parts:
                continue
            tag = parts[0]
            if tag == "opcodes":
                rec.opcodes, rec.writers = int(parts[1]), int(parts[3])
            elif tag == "defs":
                rec.recorded, rec.dropped, rec.orphans = int(parts[2]), int(parts[4]), int(parts[6])
            elif tag == "escape":
                # escape budget <K|unlimited> pops P unwinds U checks C unframed W unresolved X
                if parts[1] != "budget":
                    raise SystemExit(f"{path}: malformed escape header")
                rec.budget = parts[2]
                rec.pops, rec.unwinds, rec.checks = int(parts[4]), int(parts[6]), int(parts[8])
                rec.unframed, rec.unresolved = int(parts[10]), int(parts[12])
                saw_escape_header = True
            elif tag == "programs":
                pass
            elif tag == "program":
                program = Program(parts[1], int(parts[3]), int(parts[5]), parts[7])
                rec.programs[program.source_hash] = program
                function = None
            elif tag == "fn":
                assert program is not None, "an `fn` line before any `program` line"
                function = Function(int(parts[1]), int(parts[3]), int(parts[5]), int(parts[7]), parts[9])
                program.functions[function.index] = function
            elif tag == "r":
                assert function is not None, "an `r` line before any `fn` line"
            elif tag == "alloc":
                assert function is not None, "an `alloc` line before any `fn` line"
                kind = parts[3]
                if kind not in KINDS:
                    raise SystemExit(f"{path}: unknown kind `{kind}` in an alloc line")
                function.allocs.append((int(parts[1]), kind))
            elif tag == "site":
                assert function is not None, "a `site` line before any `fn` line"
                # site <pc> kind <k> made <m> confined <c> escaped <e> unresolved <u>
                #      unchecked <x> routes <label>:<n>[,...]|-
                site = Site(parse_pc(parts[1]), parts[3], int(parts[5]), int(parts[7]), int(parts[9]),
                            int(parts[11]), int(parts[13]))
                if site.kind not in KINDS:
                    raise SystemExit(f"{path}: unknown kind `{site.kind}` in a site line")
                if parts[14] != "routes":
                    raise SystemExit(f"{path}: malformed site line: {line.strip()}")
                if parts[15] != "-":
                    for item in parts[15].split(","):
                        label, count = item.split(":")
                        if label not in ROOT_LABELS:
                            raise SystemExit(f"{path}: unknown root label `{label}` - GCRoots.def moved?")
                        site.routes[label] = int(count)
                # THE LINE MUST ACCOUNT FOR ITSELF.
                if site.made != site.confined + site.escaped + site.unresolved + site.unchecked:
                    raise SystemExit(f"{path}: site {site.pc} {site.kind} of {function.name}: made != sum")
                if sum(site.routes.values()) != site.escaped:
                    raise SystemExit(f"{path}: site {site.pc} {site.kind} of {function.name}: routes != escaped")
                if site.made == 0:
                    raise SystemExit(f"{path}: a site line with made 0 (they are not written)")
                function.sites[(site.pc, site.kind)] = site
            else:
                raise SystemExit(f"{path}: unknown line `{tag}`")
    # THE HEADER MUST MATCH WHAT THIS FILE KNOWS.  Different numbers mean the
    # interpreter's tables moved and this checker has not been told.
    if not saw_escape_header:
        raise SystemExit(f"{path}: no `escape` header line - not a version-2 recording")
    if rec.opcodes != OPCODES or rec.writers != WRITERS:
        raise SystemExit(
            f"{path}: header says opcodes {rec.opcodes} writers {rec.writers}; "
            f"this checker knows {OPCODES}/{WRITERS} - re-derive both"
        )
    total_unresolved = 0
    for prog in rec.programs.values():
        if len(prog.functions) != prog.nfunctions:
            raise SystemExit(
                f"{path}: program {prog.source_hash} declares {prog.nfunctions} "
                f"functions and carries {len(prog.functions)}"
            )
        for fn in prog.functions.values():
            total_unresolved += sum(s.unresolved for s in fn.sites.values())
    if total_unresolved != rec.unresolved:
        raise SystemExit(
            f"{path}: header says unresolved {rec.unresolved}, the site lines sum to {total_unresolved}"
        )
    return rec


# --- the stub inferences -----------------------------------------------------


def stub_all_confined(_program, _function, _pc, _kind) -> str:
    """Deliberately WRONG.  Every site is confined."""
    return "confined"


def stub_all_escapes(_program, _function, _pc, _kind) -> str:
    """Deliberately trivial, and SOUND by construction.  Everything escapes."""
    return "escapes:not_tracked"


STUBS = {"all-confined": stub_all_confined, "all-escapes": stub_all_escapes}


def read_claims(path: str):
    claims: dict[tuple[str, int, int, str], str] = {}
    flags: dict[tuple[str, int], str] = {}
    uncheckable = 0
    with open(path, "r", encoding="utf-8") as handle:
        for number, line in enumerate(handle, 1):
            line = line.split("#", 1)[0].strip()
            if not line:
                continue
            parts = line.split()
            if parts[0] == "function" and len(parts) == 4:
                flags[(parts[1], int(parts[2]))] = parts[3]
                continue
            if len(parts) != 6 or parts[0] != "escape":
                raise SystemExit(f"{path}:{number}: expected `escape <hash> <fn> <pc> <kind> <verdict>`")
            kind, verdict = parts[4], parts[5]
            if kind not in KINDS:
                raise SystemExit(f"{path}:{number}: unknown kind `{kind}`")
            if verdict in UNCHECKABLE_CLAIMS:
                uncheckable += 1
            elif verdict != "confined":
                if not verdict.startswith("escapes:") or verdict[8:] not in REASONS:
                    raise SystemExit(f"{path}:{number}: unknown verdict `{verdict}`")
            claims[(parts[1], int(parts[2]), parse_pc(parts[3]), kind)] = verdict
    return claims, flags, uncheckable


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--recording", required=True)
    ap.add_argument("--infer", choices=sorted(STUBS), help="use a built-in stub inference")
    ap.add_argument("--claims", help="a claims file from a real inference")
    ap.add_argument("--name", default="", help="what to call this corpus in the report")
    ap.add_argument("--max-report", type=int, default=10, help="violations to name (0 = all)")
    for what in ("violations", "observed", "unobserved", "sound", "partial", "pending",
                 "imprecise", "exact", "unclaimed", "inconclusive", "claimed"):
        ap.add_argument(f"--expect-{what}", type=int, help="fail unless the count is exactly this")
    args = ap.parse_args()

    if (args.infer is None) == (args.claims is None):
        raise SystemExit("exactly one of --infer and --claims")

    rec = read_recording(args.recording)
    stub = STUBS[args.infer] if args.infer else None
    claims: dict[tuple[str, int, int, str], str] = {}
    flags: dict[tuple[str, int], str] = {}
    uncheckable = 0
    if stub is not None:
        for phash, prog in rec.programs.items():
            for index, fn in prog.functions.items():
                for pc, kind in fn.allocs:
                    claims[(phash, index, pc, kind)] = stub(phash, index, pc, kind)
    else:
        claims, flags, uncheckable = read_claims(args.claims)

    observed = sum(len(fn.sites) for p in rec.programs.values() for fn in p.functions.values())
    counts = {k: 0 for k in ("unobserved", "violations", "sound", "partial", "pending",
                             "imprecise", "exact", "unclaimed", "inconclusive", "mismatch")}
    reasons: dict[str, int] = {}
    violations = []
    mismatches = []
    seen_keys = set()

    for (phash, index, pc, kind), verdict in sorted(claims.items()):
        prog = rec.programs.get(phash)
        fn = prog.functions.get(index) if prog else None
        if verdict in UNCHECKABLE_CLAIMS:
            continue
        if fn is None:
            counts["unobserved"] += 1
            continue
        site = fn.sites.get((pc, kind))
        if site is None:
            if any(spc == pc for (spc, _skind) in fn.sites):
                counts["mismatch"] += 1
                mismatches.append((phash, fn, pc, kind))
            else:
                counts["unobserved"] += 1
            continue
        seen_keys.add((phash, index, pc, kind))
        if verdict == "confined":
            if site.escaped > 0:
                counts["violations"] += 1
                violations.append((phash, fn, site))
            elif site.confined > 0:
                counts["sound" if site.clean() else "partial"] += 1
            else:
                counts["pending"] += 1
        else:
            reason = verdict[len("escapes:"):]
            if site.escaped > 0:
                counts["exact"] += 1
            elif site.confined > 0 and site.clean():
                counts["imprecise"] += 1
                reasons[reason] = reasons.get(reason, 0) + 1
            else:
                counts["inconclusive"] += 1

    for phash, prog in rec.programs.items():
        for index, fn in prog.functions.items():
            for (pc, kind) in fn.sites:
                if (phash, index, pc, kind) not in seen_keys:
                    counts["unclaimed"] += 1

    label = args.name or args.recording
    which = args.infer or args.claims
    print(f"== escape oracle: {label} vs `{which}`")
    print(f"   programs {len(rec.programs)}  functions "
          f"{sum(len(p.functions) for p in rec.programs.values())}")
    print(f"   budget {rec.budget}  pops {rec.pops}  unwinds {rec.unwinds}  checks {rec.checks}  "
          f"unframed {rec.unframed}  unresolved {rec.unresolved}")
    print(f"   claims {len(claims)}   observed sites {observed}   unobserved claims {counts['unobserved']}")
    if flags:
        print(f"   function flags {len(flags)}: " + ", ".join(sorted(set(flags.values()))))
    print(f"   SOUNDNESS violations {counts['violations']}   sound {counts['sound']}   "
          f"partial {counts['partial']}   pending {counts['pending']}")
    print(f"   escapes claims: exact {counts['exact']}   imprecise {counts['imprecise']}   "
          f"inconclusive {counts['inconclusive']}")
    print(f"   UNCLAIMED observed sites {counts['unclaimed']}")
    denominator = counts["sound"] + counts["imprecise"]
    pct = (100.0 * counts["sound"] / denominator) if denominator else 0.0
    print(f"   PRECISION confined {counts['sound']}/{denominator} = {pct:.1f}%")
    print("   reasons: " + " ".join(f"{r} {reasons[r]}" for r in REASONS if r in reasons))

    shown = violations if args.max_report == 0 else violations[: args.max_report]
    for phash, fn, site in shown:
        pc = "prologue" if site.pc == PROLOGUE else str(site.pc)
        print(f"   VIOLATION program {phash} function {fn.index} ({fn.name}) pc {pc} kind {site.kind}: "
              f"claimed confined, observed escaped {site.escaped}/made {site.made} via {site.via()}")
    if len(violations) > len(shown):
        print(f"   ... and {len(violations) - len(shown)} more")

    failed = False
    for phash, fn, pc, kind in mismatches[: args.max_report or None]:
        print(f"   KIND MISMATCH program {phash} function {fn.index} ({fn.name}) pc {pc}: claimed {kind}, "
              f"observed " + ",".join(k for (p, k) in fn.sites if p == pc), file=sys.stderr)
    if counts["mismatch"]:
        print(f"   FAIL {counts['mismatch']} kind mismatch(es) - the coordinate is wrong", file=sys.stderr)
        failed = True
    if uncheckable:
        print(f"   FAIL {uncheckable} UNCHECKABLE claim(s) of owned/shared - no recording carries "
              "reference counts yet", file=sys.stderr)
        failed = True

    # --- the asserted counters -------------------------------------------
    for got, want, what in (
        (counts["violations"], args.expect_violations, "violations"),
        (observed, args.expect_observed, "observed sites"),
        (counts["unobserved"], args.expect_unobserved, "unobserved claims"),
        (counts["sound"], args.expect_sound, "sound"),
        (counts["partial"], args.expect_partial, "partial"),
        (counts["pending"], args.expect_pending, "pending"),
        (counts["imprecise"], args.expect_imprecise, "imprecise"),
        (counts["exact"], args.expect_exact, "exact"),
        (counts["unclaimed"], args.expect_unclaimed, "unclaimed"),
        (counts["inconclusive"], args.expect_inconclusive, "inconclusive"),
        (len(claims), args.expect_claimed, "claims"),
    ):
        if want is not None and got != want:
            print(f"   FAIL expected {what} = {want}, got {got}", file=sys.stderr)
            failed = True
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
