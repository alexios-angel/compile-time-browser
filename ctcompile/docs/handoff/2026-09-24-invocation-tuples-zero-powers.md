# Invocation tuple projection and computed zero powers, 2026-09-24 UTC

Resumed clean `e13636f0` from the latest HANDOFF, Current native work, journal and
both areas' recent commits. The old `codex-wip-20260907` is already an ancestor;
there was no dirty predecessor implementation to land. Three agents investigated
escape analysis, original native sources and review. Repeated rate limits required
parent resumption from their checkpoints.

## Landed

`82b527fb` adds `projectInvocationContinuation` to the existing exception lowering.
It validates a recovered invocation and copies only its inert continuation tuple.
Normal argument zero receives the normal result; unwind argument zero receives the
caught payload and subsequent arguments receive the original pre-call registers.
It preserves successful outer SSA values and copies completion tags and padding.
Budget exhaustion, malformed state and unsupported continuation operations refuse
before changing IR. The original call and both continuations remain intact.

The existing DOM nonthrowing-call consumer now uses that projector. Its separate
Element/method/argument effect proof still authorizes selecting normal and removing
the invocation. Projection alone supplies no effect, branch-selection or ownership
authority. The unchanged original observing getter `1a7fb166` and method `7acf503b`
each test all four recovered protocol calls, both continuations and fifteen saved
registers. Their original 33 checks remain in the recovery snapshot; recovery still
uses 57,944 steps. **This is a consumer prerequisite, not new native iterator admission.**

`1025b9fd` retains independent Number snapshots for `x ** 0`, including negative-zero
exponents and NaN/infinite bases. Both operands still require independent Number
evidence. Existing 64-operation depth, replay work charges and separation from
property-key evidence remain. Raw controls cover computed zero, nonfinite bases,
children outside index one, unknown operands and mutation. Two source cases cover
the overwritten child and its saved alias; all 168 historical function bodies remain
unchanged.

## Measured focused validation

- Exact `ctcompile_exception_recovery`: **1/1 PASS**, 4.98 s.
- Exact `ctcompile_escape_analysis_arrays`: **1/1 PASS**, 3.94 s.
  These two selected CTests took **8.92 s total**.
- `Analysis/Escape/escape-claims/table-index-overwrite.test`: **1/1 PASS**,
  0.22 s, 357 excluded.
- Five source selections from `native_dom_caught_node.py`: `invocation-return`,
  `invocation-saved-state`, `invocation-conditional-return`, `invocation-multiple-reads`
  and `write-return`. The existing runner also checked all its refusal cases and
  both original outer sources: **80 native executions, 76 refusals, 14 Node/VM
  observations**. The temporary selector retained the full source dictionary for
  refusal construction. **No whole caught-node lit pass is claimed.**
- Frozen source SHA-256
  `97308b398ba5f6f405d2ec130e0bb82ac2e594d6e1d2d7796796a61737af0f6b`,
  program `fa9803402523b206`: child Stored -> **Confined**, key table Passed ->
  **Confined**, **two of four total sites**, zero violations. Node returns
  `[0,0,0]`; the VM observes the child once, confined, with zero unresolved/unchecked
  instances. The previous measured baseline was retained without replay.
- `tools/format.sh --check`: **1126 C++, 158 Python, 114 web files PASS**.
  `git diff --check` passes. Seven final code/test hashes match the devbox;
  all **40 generated C++ files** contain no Script/AOT symbols. Independent review
  completed clean for six files; the parent reviewed the final test-only amendments
  and seventh file after the reviewer hit another rate limit.

All builds used locked `tools/remote-build.sh` with explicit targets:
`ctcompile-test-exception-recovery`, `ctcompile-test-escape-analysis-arrays`,
`ctjs-opt`, `ctjs-translate`, `ctcompile-tool`, `ctcompile-test-native-reference`,
`ctcompile-test-escape-claims` and `ctcompile-test-type-oracle`.

The first build found a new test's context-reference/pointer typo. The first CTests
then exposed two expectation issues: printing an invocation renumbered its SSA
references after projection inserted constants, and 32 existing zero-power
producers now charge 96 rather than 64 steps for their independent arithmetic bits.
The native test now compares original operation/operand identities alongside the
exact tuple assertions; the escape work expectation was updated. Production code
was unchanged by these fixes. The temporary native runner initially named a missing
`/usr/bin/node` before executing any native case; it was corrected using the generated
lit configuration. Only the native portion was resumed; successful escape gates
were retained.

Skipped: full CTest/compiler lit, complete caught-node/custom-iterator fixtures,
unaffected native replay, broad corpus/matrices, full Bootstrap, browser WPT/test262,
Windows and sanitizers. No browser/runtime/shared implementation, local C++ build,
push or history rewrite occurred. Linux inspection found fourteen readable
executable identities and sixty access errors; Windows Get-CimInstance returned
344 records with no actual Claude match. Status remained uncertain and concurrent
area rules applied.

## Exact next boundaries

Resume original outer getter `1a7fb166` and method `7acf503b`. Recovery and inert
tuple projection now exist and are gated; retain their original bodies. The next
consumer must coordinate `DOMIteratorClose.cpp`, `DOMCustomIteration.cpp` and
`DOMSource/Expansion.cpp`: discover the open inside recovered control flow, follow
the original result/state aliases, and preserve normal/failure payloads through
open, next and both close sites. The current code still requires a root-local open
and unused, state-free suppressed close. Every discarded non-call status edge needs
an independent effect proof. The original next method writes a Boolean `done`
value, so the earlier literal-String write proof alone is insufficient. Final DOM
proof must retain exhaustion, cleanup, saved returns and caught-node identity;
escaping nodes still need exception-lifetime ownership.

The next measured escape source SHA-256 is
`39cf843e15157a50737d64b0b1106d1fb278e58afd2f08dc3a98ca0c639709c6`, program
`2672f8901b7e3947`. It uses `1 ** ((keys[i % 2] - 0.25 + 0.25) % 3)` followed by
`| 0`, with its child at index one. The child remains Stored and key table Passed:
zero of four total sites statically confined, zero violations. Node returns
`[0,0,0]`; the VM observes the child once, confined, with zero unresolved/unchecked
instances. Prove the unit-base identity only with independent finite Number exponent
evidence; nonfinite exponents have different semantics.

Broader iterators/protected observers, unguarded Bootstrap defaults, the application
driver and full native Bootstrap remain unfinished. No full-Bootstrap coverage gain
is claimed.

Evidence: `../test-results/2026-09-24-invocation-tuples-zero-powers/`, including
frozen sources/baselines, focused logs, generated C++ without executables, reviews,
final hashes and a checksum manifest.
