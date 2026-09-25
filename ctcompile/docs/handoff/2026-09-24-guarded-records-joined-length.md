# Guarded iterator records and joined length guards, 2026-09-24 UTC

Resumed clean `800dcb05` from HANDOFF/detail160, Current native work, both areas'
commit history and the synchronization journal. `codex-wip-20260907` was already
an ancestor; no abandoned patch remained. Source tracing, the bounded escape
follow-up and independent review ran alongside the main native work.

## Landed

**b157875d** proves the original close record together with its completion tag.
Both slots descend through the same verified If/IndexSwitch producer and exact
yield positions. Every matching leaf must carry the actual open identity; a
known different tag excludes a leaf, while an unknown tag refuses. At least one
matching leaf is required. Only an enclosing equality's then-region authorizes
the substitution, including reversed equality operands. The tag must use the
existing lossless i32-to-index unsigned cast. Other cast shapes stay conservative.

The bounded proof replaces only each dominated close call's first argument.
It retains mixed tuple results, all producers and effects, both false close
flags, every saved register and both invocation continuations. Tests corrupt
one selected leaf with poison, another identity or an unknown tag; change the
guard or its tag slot; and reverse equality operands. Existing escaped-alias,
saved-done publication, effect and budget controls remain. Whole preparation
rolls back after late refusal, including after a successful substitution.

Original getter `1a7fb166` and method `7acf503b`, with lifted helper bodies,
now reach `DOM custom next requires one direct loop test` from direct recovery
and full preparation. Both close operands become the exact open identity;
next and both closes retain their full 15-register saved states. Raw helper
CFGs retain their getter/identity body-proof refusals. All original source
bodies remain unchanged. **No new native JavaScript source admission is claimed.**

**4f1c9f12** accepts cached array-length receivers across ordinary CFG joins
when exactly one syntactic predecessor is in the current independent path's
visited set. Existing state snapshots retain the actual values and visited
blocks separately for every structural path; repeated ordinary CFG blocks
refuse. The actual predecessor must therefore be the unique visited one.
Ambiguous shortcut paths, unknown receivers and changed extents remain refused.
Every predecessor scan spends the existing budget, with the existing depth-64
limit. No merged alias state or new path model was introduced.

The existing exact allocation identity, read-time length Number, current extent,
unchanged bound/backedge transport, mutation census and exact replay remain
mandatory. Two historical join expressions are now positive tests with their
bodies unchanged. Saved-child, changed-bound, replaced-receiver and differing
branch-receiver sources retain escapes.

Frozen source SHA-256
`7c4d9365962b8ad9f55f745a7b4a117232e19006a55fdbec7aaa13e5ee233eaf`,
program `bf9fa089c74d0965`, changes child Stored and holder Passed to Confined:
**2/4 total allocation sites**, all four observed, zero violations, unlimited VM
recording, three frame pops. The unchanged source was measured before and after.
Node returns `[[0,0,0],[0,0,0]]` across the two branch choices.

## Exact focused validation

All builds and devbox commands held `/tmp/ctbrowser-devbox-build.lock`.
Explicit remote-build targets were `ctjs-opt`, `ctjs-translate`,
`ctcompile-test-exception-recovery`, `ctcompile-test-host-contract`,
`ctcompile-test-native-reference`, `ctcompile-test-escape-analysis-arrays`,
`ctcompile-test-escape-claims` and `ctcompile-test-type-oracle`.

- Final native `ctcompile_exception_recovery`: **1/1 PASS, 5.57 s**.
  `ctcompile_host_contract`: **1/1 PASS, 2.87 s**. The selected three-test
  invocation, including arrays at 4.87 s, passed in **13.32 s** total.
- After the final formatting-only array test change, rebuilt the array target
  and reran exact `ctcompile_escape_analysis_arrays`: **1/1 PASS, 4.85 s**,
  4.86 s total. All six final hashes match the devbox.
- Generated build-tree lit filter
  `^ctcompile :: Analysis/Escape/escape-claims/cached-joined-length[.]test$`:
  **1/1 PASS, 0.11 s, 362 excluded**. The frozen program separately passed
  compiler-claims/VM recording before and after the implementation.
- Selected native sources `invocation-return`, `write-boolean-snapshot` and
  `helper-next-record`, retaining every original outer/refusal control: **PASS**.
  Both DOM providers, optimization/printing options, GCC/Clang and Node/VM
  comparisons remain enabled. This is not a whole-fixture/lit pass. The exact
  selector is preserved as `native/native-selection.py` in the evidence.
- `tools/format.sh --check`: **1126 C++, 158 Python, 114 web files PASS**.
  `git diff --check` passes. Completed independent review is clean, with all
  six final source/test hashes matching. Twenty-four generated C++ files contain
  no `ctbrowser::script`, `ctbrowser::aot` or `ctjs::` namespace; the native
  harness's binary-symbol checks pass.
- All 34 frozen native artifacts and eight C++ raw source strings are unchanged.
  Of 302 Python string constants, only the intended refusal diagnostic changes.
  Restoring the two promoted join blocks reconstructs historical Validation
  byte-for-byte. The new lit fixture contains the exact frozen escape function.

The first build passed. Its trailing baseline CTest command was consumed by
SSH's inherited input; subsequent commands use redirected input. The initial
escape baseline command used wrong CLI flags; corrected `--script`/`--out`
commands produced the before/after evidence above. The first native recovery
candidate failed eight stale operand assertions in two existing mutation
controls; their expectations now allow only the proved call-operand change.
The selected native run first used a missing Node path; reading the build's lit
configuration supplied `/home/ubuntu/tools/node-v26.8.1/bin/node` and the corrected
run passed. These initial attempts are not counted as passing checks.

Skipped: full CTest/compiler lit, whole native fixtures, unaffected native replay,
broad corpus/matrices, full Bootstrap, WPT/test262, Windows and sanitizers.
No browser/shared implementation, local C++ build, push or history rewrite occurred.

## Exact next boundary

The paired close/tag identity proof is complete for the preserved original
sources. In the prior projected IR, slot `%59#4` under tag zero and `%59#16`
under tag one each have two exact open leaves; the next-failure leaf has tag two
and cannot supply either record. Only the call operands are forwarded. Their
saved register tuples still contain the original completion selections.

`continuationRoot(next)` now refuses because its parent is the retained
result-bearing Invoke. Merely accepting that ancestor is insufficient: traversal
ordering, normal/exhausted close coverage, next failure and observing close
failure all need their own state correspondence. Extend the traversal/coverage
proof and the state rewriter together. The current rewriter handles selections
and suppressed cleanup, not the original observing Try/Invoke tuples.

Preserve both unsuppressed close failures, saved returns, caught-node identity
and every non-call effect. Close writes an attribute and throws; exhaustion
must skip that getter/method on the second call. Raw helper CFG normalization
remains an earlier independent requirement. Ambiguous cached origins, escaping
node exception owners, broader iterators, unguarded Bootstrap defaults, the
application driver and full native Bootstrap remain unfinished.

Availability inspection read 13 Linux executable identities with 60 permission
failures and 343 Windows process records. No actual Claude identity matched,
but the incomplete Linux scan means **uncertain**; concurrent-agent rules stayed
in force. No broader browser authorization was used. Recent runtime/Shell history
and WPT/test262 documentation were read; historical metrics were not remeasured.

Evidence is at `../test-results/2026-09-24-guarded-records-joined-length`
relative to the monorepo root, with verified `SHA256SUMS`.
