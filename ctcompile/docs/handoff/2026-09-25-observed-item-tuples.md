# Observed iterator item tuples

Continued clean `ctcompile-v1` at `a3abb74e`, following the iterator thread
interrupted in iteration 162 and advanced by `ad9ef3f3`. The previous handoff
identified normal item transport as the next proof. The old WIP branch is
already an ancestor; no abandoned patch or branch merge was needed.

## Native checkpoint

`c1686b5f` follows the next call's normal result through exact InvokeExit,
InvokeYield and If/IndexSwitch result positions. Saved state operand N follows
unwind argument N; argument zero is the exception payload. Entry verification
and the existing protocol census establish these correspondences before use.
The bounded worklist visits every alias and every operand use.

The original getter `1a7fb166` and method `7acf503b` never read the item. They
spill it through exhausted and non-exhausted tuples, then save it in both close
invocations. The corresponding unwind results are dead. These transfers now
pass the item proof without requiring a not-done guard on an unused spill.

Actual observers require both the exact next-success arm and a not-done test of
the original record after next. Exhausted, unguarded, saved-unwind and alternate
tuple-slot observers refuse. Lookalike record/key guards supply no authority.
Ordinary direct-next behavior retains its original same-block ordering rule.

The explicit `DOM iterator observed completions require state transport`
refusal remains after this proof. No new native JavaScript source admission is
claimed. Raw helper bodies retain their earlier refusals. Both close(false)
flags, three retained invocations, 15 saved registers, source effects and saved
return remain unchanged. Complete preparation still rolls back the module and
contract on refusal.

## Native validation

All C++ builds and devbox commands held `/tmp/ctbrowser-devbox-build.lock`.
Explicit build targets were `ctjs-opt`, `ctjs-translate`,
`ctcompile-test-exception-recovery`, `ctcompile-test-host-contract`,
`ctcompile-test-native-reference`, `ctcompile-test-escape-claims` and
`ctcompile-test-type-oracle`.

- Baseline exception recovery: **1/1, 5.36 s**. Final exact
  `ctcompile_exception_recovery`: **1/1, 5.39 s**, including seven new controls.
- Exact `ctcompile_host_contract`: **1/1, 2.82 s**.
- Selected native sources `invocation-return`, `write-boolean-snapshot` and
  `helper-next-record`, plus all original/refusal controls: **PASS**. Both DOM
  providers, printing/optimization modes, GCC/Clang and Node/VM comparisons
  remain enabled. This is not a whole-fixture or full-lit pass.
- All **24 generated C++ files** contain no Script/AOT/ctjs namespace; the
  harness binary-symbol checks pass. Both final file hashes match the devbox.
- `tools/format.sh --check`: **1126 C++, 159 Python, 114 web files PASS**.
  `git diff --check` passes. All eight existing C++ raw literals, including five
  JavaScript literals, are byte-identical.
- Independent static review of the two-file native patch is clean. Source
  tracing verified all 34 frozen artifacts and the original helper and
  projected-IR hashes, then hit a rate limit before its final report. The parent
  retained its concrete trace findings below; final source-agent signoff is not
  claimed. No gate replay by either agent is claimed.

The first changed build was interrupted before completion and restarted. The
raw escape baseline succeeded; its separate IR-import command initially used
an invalid flag, then succeeded with `--ctbrowser-js-to-ctjs`. Neither incomplete
attempt is counted as a passing gate.

Skipped: full CTest/compiler lit, whole native fixtures, unaffected native
replays, broad corpus/matrices, full Bootstrap, WPT/test262, Windows, sanitizers
and performance runs. No browser/shared implementation, local C++ build, push
or history rewrite occurred.

## Parallel escape checkpoint

`320701bf` recognizes a saved If-result receiver using the existing exact
held-value transport and actual CFG predecessor proof. Each branch keeps its
own allocation identity, cached bound, current extent and replayed contents.
Nested allocations/reads still lack nonrepetition correspondence and refuse.
The escape agent prepared the test-only candidate; the parent finished the
implementation and controls after interruption.

Frozen source `d1302d9a`, program `502bee7b1676c84d`, retains **2/4 confined
sites**, all four observed with zero VM violations and three unlimited frame
pops, before and after. Its byte-identical lifted function previously refused
with UnsupportedControlFlow. It now proves the final array `[0,0,0]` on both paths, with
the child absent from returned-array reachability. Eight controls retain
returned children and distinct selected arrays, and refuse opaque receivers,
stale extents, unrelated bounds, mutation, wrong stores and nested allocations.
All historical Validation bytes remain unchanged.

This is a **contents proof gain only**. The legacy storage census marks region
operations incomplete, so the structured retention consumer still refuses and
no structured confinement verdict changes. The first candidate test wrongly
equated contents completion with retention eligibility; focused validation
caught this, and the expectation was corrected without changing source or
production code. Final independent review confirms that distinction.

Final exact `ctcompile_escape_analysis_arrays`: **1/1, 4.62 s**. Its additional
build target was `ctcompile-test-escape-analysis-arrays`. Existing build-tree lit
filter `^ctcompile :: Analysis/Escape/escape-claims/cached-shortcut-length[.]test$`:
**1/1, 0.10 s, 363 excluded**. The frozen raw-source oracle passes before and
after. Final formatting again passes **1126 C++, 159 Python, 114 web files**;
both escape file hashes match the devbox and final independent review.

The next escape boundary is region-aware storage/alias evidence for the retention
consumer, plus nonrepetition proof for nested allocation/read origins. Do not
turn complete contents into a confinement claim while that prerequisite refuses.

## Next native boundary

Thread next success/exhaustion/failure and both unsuppressed close outcomes
through retained Try/Invoke state. Next failure marks done before propagation;
close marks done before getter/method lookup, and exhaustion skips close effects.
Preserve saved return, caught anchor identity, original payloads, all saved
registers and every source effect.

The Invoke body must remain exactly call plus InvokeExit. Result-record
done/value/state extraction belongs in the normal continuation. The private
always-throwing return helper has already become a payload-returning helper;
its returned anchor must select the original unwind projection, not a normal
close result. A throw emitted in an Invoke normal arm is not protected by that
Invoke. Try is single-result: do not append scalar results blindly. State that
dies at Try exit needs an explicit complete use census before disposal.

Raw helper CFG normalization, escaping-node owners, broader iterators,
unguarded Bootstrap defaults, the native application driver and full native
Bootstrap remain unfinished. Continue on `ctcompile-v1`.

Availability inspection read 11 Linux executable identities with 61 permission
failures and 347 Windows Get-CimInstance process records. No Claude identity
matched, but incomplete Linux inspection means uncertain; concurrent-agent
rules remained active.

Evidence: `../test-results/2026-09-25-observed-item-tuples` relative to the
monorepo root, with focused logs, source preservation, static reviews, generated
text artifacts and SHA256SUMS.
