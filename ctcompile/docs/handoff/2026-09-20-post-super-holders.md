# Post-super holder calls and left-shift indices — 2026-09-20 UTC

Continued clean `56906859` from HANDOFF/plan 00. Required histories and unmerged
branches were reviewed; no dirty interrupted work remained, and September 7 WIP
was absent from the unmerged list. Service interruptions resumed the same native
and escape drafts. Independent agents supplied fixtures, the escape proof and
read-only native review; root reconciled, gated and committed each concern.

Linux executable/argv checks found no Claude identities among 75 processes, but
59 denied executable reads prevented confirming availability. Windows
Get-CimInstance succeeded (345 processes), with no Claude identities. Concurrent
restrictions remained; there were no browser/runtime changes or new WPT/test262
measurements.

## Changes

- `4015d707`: bounded nonwrapping left-shift array indices share the existing
  exact Number/count conversion, endpoint and reload proofs. Nonnegative operands
  and results remain within signed i32; counts use ECMAScript modulo 32, and the
  positive stride scales within the existing u32 bound. Signed/wrapping bands,
  fractional positions, varying or overlapping reloaded counts, retained aliases,
  cycles and work/depth exhaustion remain refused. Seven CFG/three SCF positives,
  boundary controls and a 22-function source oracle accompany the change.
- `e055e73e`: super normalization preserves ordinary calls after the receiver
  is initialized. The existing complete receiver, callable-holder and source-body
  census runs on the resulting live IR before anything is admitted. This removes
  a redundant early method-only check and avoids caching holder operations that
  constructor cloning would invalidate. Pre-super restrictions remain. Immutable
  global holder calls retain their argument order, base/leaf effects, and direct
  helper bodies; receiver escape, changed holder identity, surplus arguments and
  unknown effects still refuse.

Five new native sources cover simple calls, shared bases, multilevel inheritance,
argument order and conditional primitive arguments. The original nested branch
candidate is preserved as a refusal: its callable/receiver travels through SCF
results around a guarded `this` read, outside the existing closed-holder proof.
The primitive-argument companion keeps the same nested conditions and observation.
All earlier split source sections remain unchanged.

The original W/B control still omits Data and must refuse its ambient `e` use.
A separate `bootstrap-base-data` witness adds the unchanged original shared Map
and all Data methods. Only its declaration separator changes from comma to
semicolon; no class/helper body or dispose effect is removed. Both missing-element
observations are 7; neither grants authority to their unexecuted registration path.

## Focused validation

All devbox commands held `/tmp/ctbrowser-devbox-build.lock`. Scripts, logs, source
hashes and generated samples are in `/tmp/ctcompile-post-super-holders-0535/`.

- Explicit native targets: `ctjs-opt ctjs-translate ctcompile-test-host-contract
  ctcompile-test-native-reference`.
- Explicit escape targets: `ctjs-opt ctjs-translate
  ctcompile-test-escape-analysis-arrays ctcompile-test-escape-claims
  ctcompile-test-type-oracle`.
- Exact `ctcompile_escape_analysis_arrays`: 1/1, 1.40s (1.41s total).
- Lit filter `^ctcompile :: Analysis/Escape/escape-claims/(left-shift|right-shift|composed)-index-overwrite[.]test$`:
  3/3, 0.14s. Left shift: 66 sites / ten sound / 10 of 16 confined precision;
  right shift: 63 / ten / 10 of 15; composed: 54 / seven / 7 of 10. All have
  zero violations, partial, pending or unclaimed sites. Six tested hashes match.
- Exact `ctcompile_host_contract`: 1/1, 0.49s (0.50s total).
- Selected constructor probe: 25 source observations / 72 main native executions
  (40 new) / 50 unprepared and 20 preparation refusals. Exact complete budgets:
  2,673 (holder chain) and 1,578 (argument order); rooted-super refusals pass.
  Selection: `inherited-post-super-*`, `bootstrap-base*`, and
  `inherited-own-fields-branch-unused-ambient`. Ancillary controls passed
  16 constructed-method executions / 20 refusals and eight original-r executions
  / four refusals. This is not a full class-initialization lit pass.
- Lit filter `^ctcompile :: CTNative/Lowering/Objects/class-dom[.]mlir$`: 1/1,
  290.83s. It retains 632 Node/interpreter observations, eight combined native
  executions and 4,922 refusals.
- All six native source hashes match; all twelve native/escape hashes match.
  Inspected the chain, argument-order and conditional generated C++: stack-owned
  records, borrowed receiver pointers, direct helper calls and ordinary branches.
  Existing nullable primitive scalars remain; no holder table, closure environment,
  prototype storage, collector or Script/VM symbol appears.
- Required `tools/format.sh --check`: the same 20 diagnostics in six
  HEAD-identical files (ctdrive, PrefixAnalysis, ProviderCallbacks, ProviderPaths,
  PartialEvaluation Heap, Symbolic Facts). Changed C++/Python formatting,
  Python AST, Node source checks, gate-script syntax and diff whitespace pass.

The first probe was interrupted after its successful build and produced no
completed probe result. The resumed probe passed its first three native cases
then exposed the nested callable-transport refusal described above. Its source
was preserved, its expectation corrected, and the simpler companion added.
Two existing refusal expectations were updated for the later own-field census;
their source bodies were unchanged. The corrected focused probe passed without
production changes.

Skipped: full CTest/compiler lit, whole class-initialization lit, broad native/
corpus matrices, full Bootstrap, WPT/test262, Windows and sanitizers. These are
focused passes only; no push.

## Exact next boundary

The unchanged `bootstrap-base` control now refuses `class own-key snapshot
requires fixed constructor fields`. It has no Data definition and cannot prove
its ambient registration call. The new authentic Data+B witness instead refuses
`class method capture is not its constructor or an inert sibling helper` at the
captured local Data holder. Extend the existing callable/Map ownership proofs to
that original shared holder; do not replace it with a stub or permit it by name.
The new missing-element observation is not native execution of registration.

Authentic Data needs a shared nested Map owner, retained receiver payloads,
receiver-selected inherited getters, and the complete duplicate-registration
console/first-key behavior. Variable per-instance field presence and original
own-key for-of clearing remain, including dispose's `e.remove`/`P.off` effects.
Static construction, inherited DOM, H/config/selectors/events/Popper, broader
ownership/control-flow proof and the application driver remain unfinished.
