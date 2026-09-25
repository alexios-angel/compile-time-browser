# Inert helper completions and paired arithmetic tables, 2026-09-24 UTC

Resumed clean `2b854bf8` from the latest HANDOFF, Current native work, shared
journal and both areas' commits. `codex-wip-20260907` is already an ancestor.
There was no dirty predecessor implementation. Three agents investigated native
sources, reviewed the protocol boundary and found the next escape witness;
interruption and rate limits required parent resumption. The candidate did not
receive a completed independent agent review; the parent reviewed and gated it.

## Landed

`04d8eaf0` lets DOM helper expansion consume an observed invocation's normal tuple
only after the existing complete inert-body proof succeeds. The original return
payload and successful SSA values retain their positions; unwind snapshots do not
replace them. Existing helper binding then supplies the actual return value.
Coercions, throws, effects and effectful continuation bodies remain refused.
The unchanged tuple projector moved from lowering into HostContract Analysis so
both consumers can link it without a reverse library dependency. Existing unused
suppression restrictions remain intact.

This is a reusable completion-consumer prerequisite. **The original observing
outer getter `1a7fb166` and method `7acf503b` still refuse.** No new source-level
Bootstrap admission is claimed. Their complete original sources and baselines
were retained; no source was trimmed to obtain a pass.

`05312d0b` lets Add/Sub/Mul/Div/Mod use the same bounded right-operand singleton
demand already used by Pow. Each operand still needs independent Number evidence;
the existing binary64 transfers, depth limits, work charges, mutation census and
replay remain authoritative. Two varying lookup tables can now supply an index
without borrowing converted bits as an arithmetic value or property key.

Frozen source SHA-256
`6780b1c6eb3525b3142e680183bf9a02a07706ee75954214642042b25d4bcdd2`,
program `c02f2a0e3255db18`, computes
`a[(keys[i % 2] - offsets[i % 2]) | 0] = 0`.
Its child changes Stored -> **Confined** and both lookup tables change Passed ->
**Confined**: **three of five total allocation sites**, zero soundness violations.
The returned array still escapes. Node returns `[0,0,0]`; the VM observes the child
confined once, with zero unresolved/unchecked instances. All 173 historical escape
function bodies remain unchanged; three new source cases cover the original,
a saved child and mutation of the right table.

## Measured focused validation

- `ctcompile_exception_recovery`: **1/1 PASS**, 5.18 s.
- `ctcompile_host_contract`: **1/1 PASS**, 2.81 s; the two selected tests took
  8.00 s total. The new raw-IR regression checks normal payload/state ordering,
  helper removal, effect refusals and incomplete budgets.
- Two selected `native_dom_caught_node.py` sources, `invocation-return` and
  `write-boolean-snapshot`: **32 native executions**. The separate control
  continuation passes **92 refusals**; together they complete **eight Node/VM
  observations**, including both original outer sources. **No whole caught-node
  lit pass is claimed.** All sixteen generated C++ files contain no Script/AOT
  symbols; all native executables passed the harness symbol checks.
- `ctcompile_escape_analysis_arrays`: final **1/1 PASS**, 4.11 s, 4.12 s total.
  Raw cases cover every newly enabled arithmetic operation, saved children,
  mutations, unknown operands, String operands and property-key refusal.
- `Analysis/Escape/escape-claims/table-index-overwrite.test`: **1/1 PASS**,
  0.18 s, 357 excluded.
- Frozen paired-table source: before **zero of five** total sites confined;
  after **three of five**, zero violations in both runs. The original baseline
  was retained without replay.
- `tools/format.sh --check`: **1126 C++, 158 Python, 114 web files PASS**;
  `git diff --check` passes. Nine final code/test hashes match the devbox.

All builds used locked `tools/remote-build.sh` with explicit affected targets:
`ctcompile-test-exception-recovery`, `ctcompile-test-host-contract`, `ctjs-opt`,
`ctjs-translate`, `ctcompile-tool`, `ctcompile-test-native-reference`,
`ctcompile-test-escape-analysis-arrays`, `ctcompile-test-escape-claims` and
`ctcompile-test-type-oracle`.

The first native build exposed the projector's library placement; moving its
unchanged implementation fixed linking. The temporary native selector removed
keys that refusal generation also needed, after all 32 successful executions;
only the remaining controls were resumed. The first escape build caught a test
String-to-pointer mismatch. Two arrays runs exposed fixture display labels;
using the harness's `storage_test_id` attributes fixed them without changing
production logic. Successful gates were retained without replay.

Skipped: full CTest/compiler lit, complete caught-node/custom-iterator fixtures,
unaffected native replay, broad corpus/matrices, full Bootstrap, browser WPT/test262,
Windows and sanitizers. No browser/runtime/shared implementation, local C++ build,
push or history rewrite occurred. Linux inspection read eleven executable
identities with 63 access failures; Windows Get-CimInstance returned 343 records
with no actual Claude match. Status remained uncertain; concurrent-area rules applied.

## Exact next boundary

The original outer sources still require coordinated recovered open/next/close
handling. `DOMIteratorClose.cpp` rejects the observing catch before general
completion handling; `DOMCustomIteration.cpp` still needs root-local open and
record alias proofs; protected helper expansion only consumes observed tuples
for independently inert bodies. The real next/close methods have DOM effects.
Connect protocol discovery and helper expansion to the original tuples with
independent effects, exhaustion, cleanup, saved returns and caught-node identity.
The inert tuple consumer alone supplies none of that authority. Escaping nodes
still require exception-lifetime ownership.

The measured paired-table escape witness is complete. Start further precision work
from another measured refusal; general powers remain unproved beyond the bounded
identities already implemented. Broader iterators, unguarded Bootstrap defaults,
the application driver and full native Bootstrap remain unfinished.

Evidence: `../test-results/2026-09-24-inert-helper-paired-tables/`, with original
sources/baselines, focused logs, generated C++, final hashes and a checksum manifest.
