# Nested helper captures and negative divisors — 2026-09-20 UTC

Resumed the nested helper and negative-divisor work abandoned at 02:40:07 UTC
and hard-stopped at 02:41:23, from six dirty compiler paths at `76a25950`.
The latest synchronization journal and helper-capture handoff identified both
threads. Required commit histories and unmerged branches were reviewed;
September 7 WIP is absent from the unmerged list. A service interruption at
02:52:58 left nine paths; this same work resumed without replacing the drafts.
Three agents completed escape proof, source regressions and independent capture
review. Root reconciled and gated the changes in separate commits.

Linux executable/argv scanning found no Claude identity but had 54 denied
process reads. Windows Get-CimInstance succeeded (352 processes), with no Claude
identity. Availability remained uncertain and concurrent-agent restrictions
stayed in force. No browser/shared implementation file was edited. Recent Shell,
Script and WPT history was reviewed; compliance numbers remain historical.

## Landed

- `c5cd6e4f` extends the existing exact fixed-cell proof to bounded nested
  sibling helpers. Every capture keeps its original cell, source block and
  ordering. Shared targets are recorded only after their full capture proof;
  no nested helper inherits constructor or holder authority. Captured ordinary
  calls retain their argument order and complete bodies, then become direct
  calls with no environment. The existing complete source census, private
  candidate, receiver/new.target checks and work budget remain; nesting is
  capped at 64 to bound the proof stack.
- `353114ba` admits bounded exact negative Number divisors in affine own-index
  overwrites. Division uses the positive stride magnitude and swaps endpoint
  order for a negative divisor. Every original intermediate remains integral
  and bounded; final indices remain own elements. Complete reload-overlap,
  saved-child, remaining-alias, growth, cycle and work-limit restrictions remain.
  Added four CFG and three SCF positives, seven unit refusals and a 23-function
  source oracle.

Sixteen native fixtures add seven executable cases and nine refusals. They
cover constructors, methods, inherited distinct capture slots, chains, shared
helpers, a diamond and primitive argument order. Mutable bindings, uncalled
writers, identity observations, excess arguments, primitive captures, receiver,
new.target, ambient effects and recursive ordering remain refused. Parts 01–07
and authentic Bootstrap source generation are unchanged.

The helper-only root control confirms that a wrapper's original frame and root
survive direct-call conversion. It executes through the normal native harness.
The existing all-frame inherited-constructor root refusal remains. The chain
and inherited distinct-slot cases exercise exact work-budget cutoffs and no
partial output. Independent final review found no capture-proof blocker.

## Focused validation

All devbox builds/tests held `/tmp/ctbrowser-devbox-build.lock`.

- `tools/remote-build.sh ctjs-opt ctjs-translate ctcompile-test-host-contract
  ctcompile-test-native-reference ctcompile-test-escape-analysis-arrays
  ctcompile-test-escape-claims ctcompile-test-type-oracle` passed. The second
  invocation synced diagnostic-only test corrections; Ninja had no work.
- `ctest --test-dir build --output-on-failure --no-tests=error
  -R '^ctcompile_escape_analysis_arrays$'`: 1/1, 1.34s (1.35s total).
- `~/.lit-venv/bin/lit -sva build/ctcompile/test
  --filter='^ctcompile :: Analysis/Escape/escape-claims/(negative-divisor|quotient|negative-scaled|composed)-index-overwrite[.]test$'`:
  4/4, 0.14s. New oracle: 69 sites, nine sound claims, 9/13 confined precision,
  zero violations, partial, pending or unclaimed sites. Four source hashes match.
- `ctest --test-dir build --output-on-failure --no-tests=error
  -R '^ctcompile_host_contract$'`: 1/1, 0.48s (0.49s total).
- `~/.lit-venv/bin/lit -sva -j2 build/ctcompile/test
  --filter='^ctcompile :: CTNative/Lowering/Objects/class-(initialization|dom)[.]mlir$'`:
  2/2, 366.33s. Class: 246 observations, 692 main native executions (56 new),
  492 unprepared and 269 preparation refusals. Ancillary controls: 16 constructed
  method executions/20 refusals, eight original-r executions/four refusals, four
  prototype-key executions/six refusals and 15 prepared-source refusals. DOM:
  632 observations, eight combined executions, 4,910 refusals, unchanged.
  First complete budgets: nested chain 444, inherited distinct helpers 1,366.
  All five native source hashes match the tested copy, nine across both commits.
- Inspected generated diamond, inherited distinct-helper and argument-order C++:
  stack-owned typed objects, borrowed receiver pointers and direct ordinary helper
  calls; no closure environment, Script/VM symbols or prototype storage. The
  native source gate rejects any `ctbrowser::` symbol in these closed programs.
- Required `tools/format.sh --check` retains 20 diagnostics in six HEAD-identical
  files: browser `tools/ctdrive/ctdrive.cpp`; compiler
  `HostContract/PrefixAnalysis.cpp`, `ProviderCallbacks.cpp`, `ProviderPaths.h`,
  `PartialEvaluation/Heap.h`, `Symbolic/Facts.cpp`. Changed C++ passes the pinned
  formatter and both Python files pass Black. Python AST, Node syntax/observations
  (16 native fixtures; 23 escape functions), temporary shell `bash -n` and
  `git diff --check` pass. An earlier formatter invocation caught drafts in flight;
  those changed-file diagnostics were corrected before the recorded final check.

The initial native probe executed all seven new positives, then stopped on the
recursive fixture's expected diagnostic: actual refusal is `class local cell is
observed before initialization`. The source remained intact and its expectation
was corrected. A separate unchanged Bootstrap probe exposed the next setup
boundary below; that expectation was also corrected. No production proof was
weakened for either mismatch. One manual diagnostic command initially used the
wrong pass name; retry with `--ctnative-specialize-class-initialization` produced
the recorded diagnostic.

Evidence and exact scripts: `/tmp/ctcompile-nested-0249/`. No full CTest/compiler
lit, broad corpus/native matrix, WPT/test262, whole Bootstrap, Windows or sanitizer
run was performed. Focused passes are not full-suite results. No push, history
rewrite, browser/Script behavior change or new compliance measurement.

## Exact next boundary

The unchanged authentic W/B specimen passes nested `a` → `r` capture proof and
now refuses `class methods, static fields or repeated setup remain unsupported`.
B defines ordinary static `getInstance`, `getOrCreateInstance` and `eventName`.
The refusal comes from the constructor-use census in
`ClassInitialization/Inheritance.cpp::examine`. Reuse its adjacent method
definition/home/use checks, `methodCaptures` and `staticGetters` for immutable
own static slots and calls on the exact constructor. The callable-object helper
requires a fresh object and cannot be applied directly to a constructor closure.
Preserve every original body, including uncalled methods; deleting static methods from the
Bootstrap specimen would hide the real boundary. Keep duplicate method/accessor
keys, closure metadata/prototype keys, later mutation, detached identities and
`new this` refused until independently proved. `getOrCreateInstance` really uses
`new this`; inherited getter resolution must preserve the actual leaf receiver.
These later restrictions are source inspection, not measured post-fix refusals.

Inherited DOM per-leaf receiver/getter proof remains separate. Bootstrap's `a`
also references `n` and `document.querySelector`, and W/B uses H, data storage,
events, configuration and dynamic construction. The null-input observation
grants no authority to those other paths. Full Bootstrap/Popper, broader ownership
and control flow, own-data definition provenance and the application driver
remain unfinished. Mutating helper object arguments retain their separate
ownership refusal. Negative-divisor overwrite proof is now measured; it grants
no general arbitrary-index or object-property overwrite authority.
