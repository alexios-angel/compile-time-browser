# Helper captures and negative-scale overwrites — 2026-09-20 UTC

Resumed `d24dd479` with seven dirty compiler paths after the loop stopped at
01:34:30 UTC. The latest JOURNAL and the failed native gate in
`/tmp/ctcompile-inherited-captures-20260920/native-gate.log` identified the
unfinished inherited constructor capture and negative-scale threads. Both
required commit histories and unmerged branches were reviewed;
`codex-wip-20260907` is already an ancestor, so no recovery merge was needed.
Abandoned predecessor claims were reclaimed through `agent-sync.py`.

Linux executable/argv checks found no Claude CLI or loop, but some system
executable reads were denied. Windows `Get-CimInstance Win32_Process` succeeded
with 344 processes and no matching identities. Availability remained uncertain;
concurrent-agent restrictions stayed in force. No browser/shared implementation
file was edited. Recent Shell/Script splits and WPT history were reviewed;
existing compliance measurements remain historical.

## Landed

Earlier completed commits had not reached the handoff documents:

- `d24dd479` proves immutable capture-free sibling helpers used by class methods
  and constructors. Four positives added 32 main native executions. Its focused
  host test passed 1/1 and class initialization/DOM lit passed 2/2 in 352.28s:
  220 class observations, 596 main native executions, 440 unprepared and 248
  preparation refusals. DOM remained 632 observations/eight executions/4,910
  refusals. Nine source hashes matched. Evidence is in
  `/tmp/ctcompile-helper-captures-20260920/resumed-native-gate.log`.
- `b85f8c14` proves reversed affine subtraction overwrite indices. Arrays 1/1
  and reversed/composed/scaled/quotient lit 4/4 passed; the new reversed oracle
  measured 54 sites, eight sound claims and 8/11 confined precision, with zero
  violations, partial, pending or unclaimed sites.

This recovery landed two separate changes:

- `1ef56773` carries each constructor's proved helper identity through
  explicit-super expansion. Original capture slots are proved before cloning;
  copied reads retain their exact target functions, independently of the leaf's
  slot numbers. Only records belonging to the replaced derived body are removed,
  preserving bases shared by multiple leaves and already expanded base chains.
  Proved helper calls retain their original position before or after super;
  receiver, source-effect, root and budget checks remain.
- `bd099c65` proves bounded negative multiplication in affine own-index
  overwrites. It reuses bounded Number products for a positive stride magnitude
  and reverses the ordered endpoint range for negative factors. Every original
  intermediate operation remains checked; final keys must still be own elements.
  Complete reload-overlap checks, aliases, saved children and cycles remain.
  Added CFG/SCF controls and a 22-function source oracle.

Native fixtures cover distinct helper slots, three-level inheritance, shared
bases and primitive dependencies through before-super/base/post-super calls.
Four new executable cases plus the existing inherited constructor case add
40 main native executions. Five new preparation refusals preserve changing
captures, observed helper identity, ambient effects, receiver and new.target.
Original fixture parts 01–06 and all nine interrupted source bodies remain.

The original object-mutating order fixture (1012123) successfully prepares but
still refuses native lowering because mutable object arguments lack the required
ownership proof. It is preserved in `PREPARED_ONLY` with the exact diagnostic.
The new primitive companion runs with 1010013. The previous gate failed because
it had incorrectly classified the object-mutating case as executable; no
ownership restriction was weakened. Root and exact budget controls cover the
new distinct-helper case. Independent review found no blocker in the capture
remapping or private-candidate refusal behavior.

## Measured focused validation

All builds and devbox commands held `/tmp/ctbrowser-devbox-build.lock`.

- `tools/remote-build.sh ctjs-opt ctjs-translate ctcompile-test-host-contract
  ctcompile-test-native-reference ctcompile-test-escape-analysis-arrays
  ctcompile-test-escape-claims ctcompile-test-type-oracle` passed. The same
  explicit target set rebuilt the corrected test below.
- `ctest --test-dir build --output-on-failure --no-tests=error
  -R '^ctcompile_escape_analysis_arrays$'`: 1/1, 1.33s (1.34s total).
- `~/.lit-venv/bin/lit -sva build/ctcompile/test
  --filter='^ctcompile :: Analysis/Escape/escape-claims/(negative-scaled|reversed|composed|scaled)-index-overwrite[.]test$'`:
  4/4, 0.14s. Negative-scaled: 66 sites, nine sound claims, 9/13 confined
  precision; reversed: 54/eight/8 of 11; composed and scaled: 54/seven/7 of 10.
  All have zero violations, partial, pending and unclaimed sites.
- The first arrays run failed two snapshot comparisons because the new negative
  factor lacked its expected `storage_test_id`. Adding that test label fixed
  both; the proof implementation was unchanged.
- `ctest --test-dir build --output-on-failure --no-tests=error
  -R '^ctcompile_host_contract$'`: 1/1, 0.48s (0.49s total).
- `~/.lit-venv/bin/lit -sva -j2 build/ctcompile/test
  --filter='^ctcompile :: CTNative/Lowering/Objects/class-(initialization|dom)[.]mlir$'`:
  2/2, 340.27s. Class: 230 observations, 636 main native executions, 460
  unprepared and 254 preparation refusals. Ancillary checks: 16 constructed
  method executions/20 refusals, eight original-r executions/four refusals,
  four prototype-key executions/six refusals and 15 prepared-source refusals.
  DOM: 632 observations, eight combined executions, 4,910 refusals. The first
  complete distinct-helper proof budget is 1,228; the last failing budget
  publishes no output.
- All eight changed source hashes match the tested devbox copy. Inspected
  distinct-helper and primitive-order C++: stack-owned typed objects, borrowed
  receiver pointers and direct helper calls; no Script/VM, prototype storage
  or closure environment. Native source checks reject any `ctbrowser::` symbol.
- Required `tools/format.sh --check` reported 20 baseline diagnostics in six
  HEAD-identical files: `ctbrowser/tools/ctdrive/ctdrive.cpp` and compiler
  `HostContract/PrefixAnalysis.cpp`, `ProviderCallbacks.cpp`, `ProviderPaths.h`,
  `PartialEvaluation/Heap.h`, `Symbolic/Facts.cpp`. Changed C++ passes the pinned
  clang-format; both changed Python files pass Black. Python/JS syntax, Node
  observations, temporary gate-script `bash -n` and `git diff --check` pass.

Evidence: `/tmp/ctcompile-resume-0142/`, including the failed and final escape
logs, native log, exact gate scripts, formatter baseline and tested hashes.
Agents independently completed escape/source fixtures and reviewed capture
soundness. Service limits interrupted some turns; resumed work was reconciled
and gated by the parent. No full CTest/compiler lit, broad corpus/native matrix,
WPT/test262, whole Bootstrap, Windows or sanitizer run was performed. Focused
passes are not full-suite results. No push, history rewrite or runtime change.

## Exact next boundary

The preserved authentic W/B specimen no longer stops at the blanket inherited
constructor capture restriction. Its next refusal is `class method capture is
not its constructor or an inert sibling helper`: B's captured `a` helper itself
captures `r`. Prove bounded immutable nested helper captures while retaining
complete original helper bodies
and every call/identity/effect use. Bootstrap's `a` also references `n` and
`document.querySelector` in its selector branch; observing `new B(null, null)`
alone grants no authority over those paths. `inherited-super-helper` separately
retains the capture-free lexical-super target restriction.

Then complete inherited DOM per-leaf receiver/getter proof. Full H/config,
selectors, events, Popper, broader ownership/control flow and the application
driver remain unfinished. The mutating object-argument order case is a concrete
separate ownership boundary. Negative divisor affine overwrites remain outside
this escape slice and are a possible next bounded extension; no admission or
measurement for them is claimed here.
