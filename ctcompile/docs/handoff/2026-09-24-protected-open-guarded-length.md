# Protected iterator open and guarded length provenance, 2026-09-24 UTC

Resumed clean `f0a30d11` from HANDOFF/detail158, Current native work, both areas'
commit history and the synchronization journal. The old `codex-wip-20260907` was
already an ancestor. No abandoned patch remained. Source tracing, escape work
and independent review ran in parallel; rate-limited agents resumed from their
checkpoints. Both implementation changes are committed separately.

## Landed

**7b5ec2d0** permits one protected open directly inside a root-local Try body.
The existing fresh-holder, unique own-slot and closed, effect-free iterator
identity proofs use the outer Try as their source-order anchor. Before-open
holder observers and alias transport refuse; only exact initialization, roots
and the original invocation's saved registers are exempt. Observers strictly
after the invocation remain unchanged for the later complete proof. This is
an open-effect proof, not a general holder-confinement proof.

The shared verified continuation projector retains every normal tuple position.
The original CallOp moves out of its wrapper at the same position, retaining
its arguments and actual result, including any undefined fast-path result.
Only its independently impossible JavaScript failure edge disappears. Next
and both close(false) invocations retain their payloads, flags and 15 saved
registers; no close is converted to suppression. Independent review found that
the interpreter's string `@@iterator` shares its well-known Symbol slot. The
final own-slot proof rejects a colliding Number initializer and tests the refusal.
The runtime was not changed.

The original getter `1a7fb166` and method `7acf503b` with lifted helper bodies
now refuse at `DOM iterator close must follow its complete traversal` because
record aliases are still carried through completion selections. Raw helper CFGs
stop earlier at `DOM iterator getter requires a terminal throw` or
`DOM Symbol.iterator must be a closed identity method`. Their original bodies
are unchanged. Recovery still retains four calls, 33 checks and 15 saved slots.
New tests inspect every open-result use and every operand within the three
retained invocations, allowing only the exact successful projection. Direct and
tuple early escapes, a colliding slot, an effectful identity and a post-open
escape remain refusal controls. Complete preparation rolls back the entire
module and contract, including after successful local projection. No new
JavaScript source admission or full-Bootstrap coverage is claimed.

**cd6c8bb0** extends cached loaded-array length provenance along ordinary CFG
single-predecessor paths ending at the independently held array allocation.
Both read paths spend the existing budget and retain a depth-64 ceiling.
Current loop header/body blocks, joins, foreign regions and longer paths refuse.
The existing visited-block traversal, allocation census, immutable Number/current
extent check, unchanged backedge and exact replay remain mandatory. No state
tracking subsystem or new dependency was added.

Frozen source SHA-256
`65c1a06f71d2239ae00875568ef62b5f07bc84639dc659968f5ffb647ab0d5c7`,
program `52dd2978d4ce19c2`, changes child Stored and holder Passed to Confined:
**2/4 total allocation sites**, all four observed, zero violations, unlimited VM
recording, three frame pops. Node returns `[[0,0,0],0]`; saved-child, changed-bound
and replaced-receiver controls retain their escapes. The original straight
split-block test is promoted with its source expression unchanged; joins and
an overlong predecessor chain remain negative controls.

## Exact focused validation

All build and devbox commands held `/tmp/ctbrowser-devbox-build.lock`.
Explicit remote-build targets across this session were `ctjs-opt`,
`ctjs-translate`, `ctcompile-test-exception-recovery`,
`ctcompile-test-host-contract`, `ctcompile-test-native-reference`,
`ctcompile-test-escape-analysis-arrays`, `ctcompile-test-escape-claims` and
`ctcompile-test-type-oracle`.

- Final `ctcompile_exception_recovery`: **1/1 PASS, 5.53 s**.
- Final formatted `ctcompile_escape_analysis_arrays`: **1/1 PASS, 4.81 s**.
  These two final selected tests took **10.35 s total**. Earlier arrays passed
  in 4.79 s. Final `ctcompile_host_contract` passes **1/1, 2.89 s**, **2.90 s
  total**, after relinking the final library; its earlier check passed in 2.96 s.
- Generated build-tree lit selection
  `^ctcompile :: Analysis/Escape/escape-claims/cached-guarded-length[.]test$`:
  **1/1 PASS, 0.12 s, 360 excluded**. The frozen source separately passed the
  compiler-claims/VM-recorder oracle before and after the change.
- Selected native sources `invocation-return`, `write-boolean-snapshot` and
  `helper-next-record`, retaining every refusal and original outer control:
  **PASS**. Both DOM providers, optimization and printing options, GCC/Clang
  and Node/VM comparisons remain enabled. This is not a whole-fixture/lit pass.
  The selector is preserved as `native-selection.py` in the evidence.
- `tools/format.sh --check`: **1126 C++, 158 Python, 114 web files PASS**;
  `git diff --check` passes. Six final source hashes match both the devbox and
  independent clean static review. Twenty-four generated C++ files contain no
  `ctbrowser::script`, `ctbrowser::aot` or `ctjs::` namespace; the harness's
  binary-symbol checks pass.
- Independent preservation checks verify all 34 frozen native artifacts, eight
  unchanged C++ raw strings and the historical escape validation source bytes.
  Of 302 Python string constants, only the intended refusal diagnostic changes.
  The frozen escape function is byte-identical to the new lit function.

The first recovery run exposed the raw/lifted helper distinction and stale
boundary expectations. A subsequent run caught a duplicate block in the new
SCF alias-control fixture; the final corrected fixture passes. The first native
command used an absent `/usr/bin/node`; the final command uses the generated
lit configuration's `/home/ubuntu/tools/node-v26.8.1/bin/node` and passes. The
first build completed but consumed the following here-document command; tests
were then explicitly run, and subsequent build commands redirect stdin. These
are recorded failures/corrections, not full-suite results.

Skipped: full CTest/compiler lit, whole native fixtures, unaffected native replay,
broad corpus/matrices, full Bootstrap, WPT/test262, Windows and sanitizers.
No browser/shared implementation, local C++ build, push or history rewrite occurred.

## Exact next boundary

Open's normal tuple is now available without guessing effects. Follow its actual
record aliases through remaining completion selections, preserving the original
result and rejecting unknown/failure origins. Then thread next/exhaustion and
both unsuppressed close failures through retained Try/result-bearing Invoke
state, including saved returns and caught-node identity. The original close
methods mutate the DOM and throw; they must not use the suppressed-close path.
The narrow open proof grants no later mutation, escape or record identity proof.
Raw helper CFGs need existing normalization before their getter/identity proofs.

The guarded-length witness is complete. Joined or structured origins need wider
execution provenance; no further witness was measured. Escaping-node exception
owners, broader iterators, unguarded Bootstrap defaults, the application driver
and full native Bootstrap remain unfinished.

Availability inspection read 17 Linux executable identities with 61 permission
failures and 343 Windows process records. No actual Claude identity matched, but
the incomplete Linux scan means **uncertain**; concurrent-agent rules remained
in force. No broader browser authorization was used.

Evidence, frozen sources, before/after claims, logs and review are at
`../test-results/2026-09-24-protected-open-guarded-length` relative to the
monorepo root.
The directory includes verified `SHA256SUMS`.
