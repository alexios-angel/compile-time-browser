# Boolean attribute writes and finite unit-base powers, 2026-09-24 UTC

Resumed clean `201595c5` from the latest HANDOFF, Current native work, journal and
both areas' recent commits. `codex-wip-20260907` is already an ancestor; there was
no dirty predecessor implementation. Three agents handled escape analysis,
native source tests and independent review. Interruptions and rate limits required
parent resumption of the escape proof from its checkpoint.

## Landed

`8fcf2405` proves protected `setAttribute` writes with Boolean literals or values
from independently proved `hasAttribute` calls. It retains the original contract
Element, exact method/receiver and validated literal String name. Recovered normal
tuples carry only their own read payload or already proved Boolean saved values.
Branch results require Boolean evidence from both original yields. This tracks
the original read snapshot across subsequent mutations without replacing it with
a later read. Existing tuple projection and complete DOM reproof still precede
publication; the existing typed emitter supplies Boolean-to-String conversion.

Four sources cover literal true/false writes, a recovered read/write/normal return,
a saved read across mutation, and conditional saved state. Refusal controls cover
unknown values, a forged read method, a Boolean/object join and invalid names.
All 27 prior local sources and both original outer iterator sources are unchanged.
**The observing outer iterators remain refused.** This completes their Boolean-write
prerequisite, not their protocol completion consumer.

`2ce0740f` retains independent Number snapshots for `1 ** finiteExponent`.
Loop analysis now sends a computed singleton exponent through its existing
arithmetic-demand proof. The power transfer independently checks Number origins,
finiteness and the 64-operation depth; converted bits remain separate from property
keys. Both operands retain their provenance, work charges and mutation census.
Raw controls include nonfinite exponents, saved children, unrelated indices,
computed unit bases, unknown/String/unary origins, mutation and depth limits.
Three source cases were appended; all 170 prior function bodies are unchanged.

## Measured focused validation

- Exact `ctcompile_exception_recovery`: **1/1 PASS**, 5.33 s, retained from the
  first selected CTest run.
- Exact `ctcompile_escape_analysis_arrays`: final **1/1 PASS**, 4.11 s,
  4.12 s total.
- `Analysis/Escape/escape-claims/table-index-overwrite.test`: **1/1 PASS**,
  0.18 s, 357 excluded.
- Six sources selected from `native_dom_caught_node.py`: `invocation-return`,
  `write-return` and the four `write-boolean-*` sources. The existing runner also
  checked every refusal and both original outer sources: **96 native executions,
  92 refusals, 16 Node/VM observations**. **No whole caught-node lit pass is claimed.**
- Unchanged source SHA-256
  `39cf843e15157a50737d64b0b1106d1fb278e58afd2f08dc3a98ca0c639709c6`,
  program `2672f8901b7e3947`: child Stored -> **Confined**, key table Passed ->
  **Confined**, **two of four total sites**, zero violations. Node returns
  `[0,0,0]`; the VM observes the child once, confined, with zero unresolved/unchecked
  instances. The prior measured baseline was retained without replay.
- `tools/format.sh --check`: **1126 C++, 158 Python, 114 web files PASS**.
  `git diff --check` passes. All six final code/test hashes match the devbox and
  completed clean independent review. All **48 emitted C++ files** contain no
  Script/AOT symbols; compiled executables also passed the harness symbol checks.

All builds used locked `tools/remote-build.sh` with explicit targets:
`ctcompile-test-exception-recovery`, `ctcompile-test-escape-analysis-arrays`,
`ctjs-opt`, `ctjs-translate`, `ctcompile-tool`, `ctcompile-test-native-reference`,
`ctcompile-test-escape-claims` and `ctcompile-test-type-oracle`.

The first arrays run failed 52 new assertions because LoopProof required an
invariant right operand and never reached the new transfer for computed exponents.
The bounded singleton-demand fix resolved that root cause. Formatting corrected
one new test line. The temporary native runner initially named a missing Node
before any execution; the final run used `/home/ubuntu/tools/node-v26.8.1/bin/node`
from the generated lit configuration. A build wrapper consumed subsequent shell
input, so the remaining escape checks were issued separately and completed above.
Successful recovery checks were not replayed.

Skipped: full CTest/compiler lit, complete caught-node/custom-iterator fixtures,
unaffected native replay, broad corpus/matrices, full Bootstrap, browser WPT/test262,
Windows and sanitizers. No browser/runtime/shared implementation, local C++ build,
push or history rewrite occurred. Linux inspection found 15 readable executable
identities and 61 read failures; Windows Get-CimInstance returned 344 records with
no actual Claude match. Status remained uncertain and concurrent area rules applied.

## Exact next boundary

Resume unchanged outer getter `1a7fb166` and method `7acf503b`. Recovery, inert tuple
projection and the next method's primitive Boolean-write prerequisite now exist.
`DOMIteratorClose.cpp`, `DOMCustomIteration.cpp` and `DOMSource/Expansion.cpp` still
need a coordinated consumer: discover the open inside recovered control flow,
follow original result/state aliases, and preserve normal/failure payloads across
open, next and both close sites. Current discovery requires a root-local open;
protected helper expansion accepts unused, state-free suppression. Tuple projection
alone cannot discharge either restriction or the original non-call status edges.
Prove effects independently and retain exhaustion, cleanup, saved returns and
caught-node identity. Escaping nodes still require an exception-lifetime owner.

The measured unit-base escape thread is complete. General powers still need their
own binary64 result proof; no new post-unit-base source was measured. Broader
iterators, unguarded Bootstrap defaults, the application driver and full native
Bootstrap remain unfinished. No full-Bootstrap coverage gain is claimed.

Evidence: `../test-results/2026-09-24-boolean-writes-finite-unit-bases/`, including
frozen sources/baselines, focused logs, generated C++ without executables, review,
final hashes and a checksum manifest.
