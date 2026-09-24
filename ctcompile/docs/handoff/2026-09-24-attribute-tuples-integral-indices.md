# Observed attribute tuples and integral loop indices, 2026-09-24 UTC

Resumed clean `e706879a`, latest152 HANDOFF/Current native work, journals and both
areas' history. `codex-wip-20260907` is already an ancestor. User continuations
interrupted iteration153; agents preserved sources, implemented an escape
candidate and reviewed native work. The parent completed the narrowed escape
change after focused validation exposed excessive scope in the first candidate.

## Landed

`022c8b38` consumes observed attribute invocation tuples after helper binding and
Element guards. It reuses the existing independent receiver/method/argument
proof and the unchanged inert tuple projector. Only proved nonthrowing calls
select normal; actual payloads, successful saved state and later effects retain
their source identities. Complete DOM reproof guards publication. Raw tests check
three observable tuple slots, zero-budget rollback, both attribute methods,
independent saved returns and identity/argument/effect refusals. Emitter and
inference behavior are unchanged. No new JavaScript source admission is claimed.

`8dfa3dd1` adds an exact arithmetic array-index query over independently computed
binary64 snapshots. Only finite integral values from zero through 2^32-2 qualify;
negative zero names zero. The query is used by PropertyKey demand in the existing
bounded loop proof and by certified loop replay. Converted bits, general Number
facts, length writes and ordinary scalar contents keep their previous authority.
Two old raw unit-result property refusals become successes with identical bodies.
New controls cover actual write images, saved children, mutation, fractional,
negative, out-of-range, nonfinite and coercing inputs. All 182 earlier source
functions remain byte-identical.

Frozen plain-driver source SHA-256
`f29263f244b99fb83c679ae6f6aaa144f6cce59f6871bdb9555b90ee45fa4976`,
program `b442a881eb54c6d3`, writes `a[keys[i % 2] - 0.25] = 0` with keys
`[0.25,2.25]`. Before: child Stored, table Passed, zero of four sites confined.
After: both Confined, two of four total sites, zero violations, all four sites
observed. Node returns `[0,0,0]`; saved-child/mutation controls return `{}` and
`[0,0,{}]`. Unlimited VM recording observes the original child confined once.

## Exact focused validation

- `ctcompile_exception_recovery`: final **1/1 PASS, 5.94 s**, 5.95 s total.
  Earlier 5.04 s pass preceded the additional tuple-observer regression.
- `ctcompile_host_contract`: **1/1 PASS, 2.85 s**, retained without replay;
  initial combined recovery/host run took 7.90 s.
- Selected `native_dom_caught_node.py` sources `invocation-return` and
  `write-boolean-snapshot`, all refusals and original outer controls:
  **32 native executions, 92 refusals, eight Node/VM observations PASS**.
  No whole caught-node fixture pass. Sixteen generated C++ files contain no
  Script/AOT/ctjs namespace; harness binary-symbol checks pass.
- `ctcompile_escape_analysis_arrays`: final **1/1 PASS, 5.25 s**, 5.27 s total.
- `Analysis/Escape/escape-claims/table-index-overwrite.test`: **1/1 PASS,
  0.22 s**, 357 excluded, via the generated build-tree lit configuration.
- Frozen source before/after claims and unlimited VM recording: **zero violations**.
- `tools/format.sh --check`: **1126 C++, 158 Python, 114 web files PASS**.
  `git diff --check` passes. Ten final code/test hashes match the devbox.
  Native independent static review completed; the earlier eight-file review
  predates escape narrowing and does not certify that final change.

All builds used locked `tools/remote-build.sh` with explicit targets:
`ctcompile-test-exception-recovery`, `ctcompile-test-host-contract`, `ctjs-opt`,
`ctjs-translate`, `ctcompile-tool`, `ctcompile-test-native-reference`,
`ctcompile-test-escape-analysis-arrays`, `ctcompile-test-escape-claims`, and
`ctcompile-test-type-oracle`. No local C++ build ran.

The first escape candidate published general Number magnitudes and failed 140
checks, including unrelated scalar/length boundaries and a range regression.
Restricting the proof to certified loop keys/replay restored those checks without
broadly changing old expectations. The original `67995082` console driver could
not run in the VM (`console` is undefined before the function call); that recording
is invalid as an execution baseline. The plain driver preserves its exact function
and was measured before rebuilding. Both artifacts are retained.

Skipped: full CTest/compiler lit, complete native fixtures, unaffected native
replay, broad corpus/matrices, full Bootstrap, WPT/test262, Windows and sanitizers.
Linux inspection read 15 executable identities with 60 permission failures;
Windows Get-CimInstance returned 352 records. Neither found actual Claude, but
status was uncertain, so concurrent rules remained in force. No browser/runtime/
shared implementation, push or history rewrite occurred.

## Exact next boundary

Original observing getter `1a7fb166` and method `7acf503b` still refuse in
`DOMIteratorClose.cpp` before general recovery consumption. Protocol discovery in
`DOMCustomIteration.cpp` requires root-local open and unused, state-free close
suppression. Connect the original recovered open/next/close tuples, each carrying
fifteen saved registers, through discovery and helper expansion with independent
non-call effect proofs. Actual next writes twice and constructs a result record;
close writes then throws the anchor. Preserve exhaustion, cleanup, saved returns
and caught-node identity. The new final tuple consumer only accepts inert
continuations and does not solve those entry/protocol/effect boundaries.
Escaping nodes still need exception-lifetime owners.

The measured integral loop-index witness is complete; no next witness was measured.
General powers, broader iterators, unguarded Bootstrap defaults, the application
driver and full native Bootstrap remain unfinished.

Evidence: `../test-results/2026-09-24-attribute-tuples-integral-indices/` contains
sources, before/after measurements, raw/normal refusal snapshots, generated C++,
logs, final hashes and review checkpoints. Earlier candidate reviews are retained
with their original hashes; `SHA256SUMS` verifies the preserved files.
