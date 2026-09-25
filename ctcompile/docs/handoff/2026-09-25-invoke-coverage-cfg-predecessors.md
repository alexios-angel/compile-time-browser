# Observed iterator coverage and exact CFG predecessors

Continued the read-only iterator trace interrupted in iteration 162, identified
in the synchronization journal and the September 24 guarded-record handoff.
The checkout was clean on main a59638ba after release publication and DOM
source organization. The requested ctcompile-v1 branch fast-forwarded to that
commit before editing; main and all other branches are unchanged. The old
codex-wip-20260907 branch is already an ancestor.

## Landed changes

`ad9ef3f3` extends structural traversal proof through census-verified observing
Invoke bodies. It finds the innermost shared continuation block for next and
close, validates all ancestry back to open, and requires open dominance and
strict traversal-before-close ordering. Arbitrary regions and revisiting loops
cannot supply ancestry, including a foreign region wrapping the whole subtree.

The existing close-coverage walk specializes exact eq/ne comparisons of an
actual traversal result, optionally through lossless i32-to-index conversion.
Every If/IndexSwitch tag leaf is checked separately. A leaf may skip closing
only inside the exact next-unwind arm: its tested result belongs to that next
Invoke, and the normal/unwind projections have opposite literal Boolean values.
A constant tag or unrelated condition cannot supply failure authority.

The original structured getter 1a7fb166 and method 7acf503b now reach
`DOM iterator observed completions require state transport`. This explicit
refusal precedes mutable-state rewriting. The next and both close invocations,
false close flags, payloads, 15 saved registers, source effects and saved return
remain unchanged. Complete preparation still restores the source and contract.
No new native JavaScript admission or full-Bootstrap coverage gain is claimed.
Raw helper bodies retain their prior getter/identity body-proof refusals.

`70de7046` replaces the escape path's visited-block set with a first-predecessor
map. Every ordinary branch/default/case transfer records its actual source
block before simultaneous value transport. Alternative paths copy that map
alongside their independent held values under the existing copy budget.
Ordinary repeated blocks still refuse; certified loop revisits retain the first
edge. Cached receiver/length provenance follows this recorded path instead of
requiring a unique already-visited syntactic predecessor. Current-loop blocks,
foreign/structured regions, unknown origins and chains beyond depth 64 refuse.
The exact receiver, read-time bound, extent, mutation and replay checks remain.

The former shortcut refusal expression is preserved as a positive exact-state
test. Unknown receivers and stale cached extents remain negative controls.
The new lit fixture includes the frozen source function unchanged, plus retained
child, changed bound and different receiver cases.

## Measured validation

All C++ builds and devbox work held `/tmp/ctbrowser-devbox-build.lock`. Explicit
remote-build targets across this session were `ctjs-opt`, `ctjs-translate`,
`ctcompile-test-exception-recovery`, `ctcompile-test-host-contract`,
`ctcompile-test-native-reference`, `ctcompile-test-escape-analysis-arrays`,
`ctcompile-test-escape-claims` and `ctcompile-test-type-oracle`.

Final focused checks:

- Exact CTests `ctcompile_escape_analysis_arrays` **1/1, 4.87 s** and
  `ctcompile_exception_recovery` **1/1, 5.38 s**, **10.26 s combined**.
- Exact `ctcompile_host_contract` **1/1, 2.77 s**, 2.78 s total.
- Generated build-tree lit filter
  `^ctcompile :: Analysis/Escape/escape-claims/cached-shortcut-length[.]test$`:
  **1/1 PASS, 0.10 s, 363 excluded**.
- Native source selection `invocation-return`, `write-boolean-snapshot` and
  `helper-next-record`, retaining all original outer/refusal controls: **PASS**.
  Both DOM providers, optimization/printing options, GCC/Clang and Node/VM
  comparisons stay enabled. This is not a whole fixture or lit-suite pass.
- All **24 generated C++ files** contain no ctbrowser::script, ctbrowser::aot or
  ctjs namespace; native harness binary-symbol checks pass. All eight final
  source/test hashes match the devbox.
- `tools/format.sh --check`: **1126 C++, 159 Python, 114 web files PASS**;
  `git diff --check` passes. The final new-lit change only corrected its expected
  existing escape reason; its source and production hashes were unchanged.
- All original raw C++ JavaScript strings remain byte-identical. The Python
  source driver changes only its refusal diagnostic. The source agent verified
  all 34 frozen source/oracle/IR artifacts and the projected/helper hashes.

Frozen escape source SHA-256:
`d1302d9a1f8856fd007f90dfd305fcd31bf267c9fa008e2fe4648230f4cf028e`.
Before and after use program **502bee7b1676c84d**: **0/4 -> 2/4 confined sites**,
with child and private holder newly confined. All four sites are observed,
unlimited recording, three frame pops, zero soundness violations. Node executes
both branch choices and returns `[[0,0,0],[0,0,0]]`. The final multi-function lit
controls also have zero oracle violations.

Initial attempts are not passing gates: the first native build rejected a const
ODS operation wrapper; removing const fixed it. The next recovery run had three
stale refusal expectations for each structured source, corrected to the newly
reached coverage boundary. The new escape extent control initially shrank before
the length read, which is valid; it now mutates after the cached read. The first
new-lit FileCheck expected Returned for a child whose existing first witness is
Stored; the unchanged source correctly keeps Stored, and that expectation was
corrected. Relevant checks were rerun after these corrections.

Source tracing completed independently. The escape agent hit a rate limit after
its read-only design checkpoint; the parent implemented and tested the change.
The reviewer caught a test use-list mutation and missing ancestry validation
above the common block; both were fixed, with a whole-subtree regression. That
agent then hit a rate limit before final signoff. The parent completed review;
this is not represented as a completed independent clean review.

Skipped: full CTest/compiler lit, whole native fixtures, unaffected native
replays, broad corpus/matrices, full Bootstrap, WPT/test262, Windows, sanitizers
and performance runs. No browser/shared implementation, local C++ build, push
or history rewrite occurred.

## Next boundary

Original structured sources have close coverage for exhaustion tag 0 and saved
return tag 1. Only the exact next-failure arm with tag 2 bypasses that obligation.
Their next result still travels through InvokeExit and an 18-value completion
tuple; the current direct item-user proof cannot consume that transport.

Prove exact normal item consumers under the not-done continuation, then thread
next success/exhaustion/failure and both observing close outcomes through Try
state. Next failure marks the record done; close marks done before getter/method
lookup, and exhausted calls must skip the close effect. Preserve the saved
return, original exception payload, caught anchor identity, all saved registers
and every non-call effect. Keep the explicit refusal until this proof and the
rewriter agree. Raw helper CFG normalization remains independently necessary.

Escape shortcut provenance is complete for the frozen witness. Structured
origins need region correspondence; escaping-node owners, broader iterators,
unguarded Bootstrap defaults, the native application driver and full native
Bootstrap remain unfinished. Continue on ctcompile-v1.

Linux inspection read 11 executable identities with 60 permission failures;
Windows Get-CimInstance returned 348 records. No actual Claude identity matched,
but the incomplete Linux inspection means uncertain. Concurrent-agent rules
remained active; no broader browser authorization was used.

Evidence: `../test-results/2026-09-25-invoke-coverage-cfg-predecessors` relative to
the monorepo root, including exact logs, frozen sources, trace, parent review,
selection script, generated text artifacts, before/after oracle files and
verified SHA256SUMS.
