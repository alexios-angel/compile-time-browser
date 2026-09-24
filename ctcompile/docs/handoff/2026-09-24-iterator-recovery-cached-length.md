# Observed iterator recovery and cached length guards, 2026-09-24 UTC

Resumed clean `78ff4513` using detail156, Current native work, both areas' history
and the synchronization journal. The old `codex-wip-20260907` was already an
ancestor. No abandoned repository patch remained. The original observing iterator
was the unfinished native thread; cached own-length guards ran independently.

## Landed

**41c9f87d** preserves complete handler register vectors for local iterator holder
setup, including BinaryStatic, object/slot/accessor construction and Iterable.
This only preserves the importer CFG; recovery and native admission still prove
control flow and effects. Observing catches bypass the suppression-only close
normalizer unchanged. Both original close flags remain false.

Original getter `1a7fb166` and method `7acf503b` now reach checked recovery after
both raw and resolved/lifted import. Each retains all 33 original status checks,
four protected calls and 15 saved registers per call. Existing tuple checks retain
callee, arguments, successful payload and pre-call failure state. Complete DOM
preparation now reaches `DOM custom iterator abrupt completion needs a handler
proof`. Tests require this exact boundary and whole-module/contract rollback at
normal and zero budgets. Neither original program is newly admitted.

**cd23eb9d** accepts a cached own-length guard only when the held Number retains
its exact GetProperty origin on a direct array allocation, matches the current
tracked extent and returns unchanged through loop backedges. Existing no-resize,
effect, store-target and exact per-iteration replay checks remain. Raw controls
cover retained children, changing bounds, prior shrink/append, in-loop resize and
a different allocation. Literal/computed bounds gain no new authority.

Frozen source `f1dd292de477ae73338871a12c07c6ae49a097cdef06b041e77641c3021ec8c6`,
program `ddbc8c68988f64f6`, changes only its child from Stored to Confined:
**one of three total sites**, all three observed, zero violations, unlimited VM
recording. Before the change it had zero confined sites. Node returns `[0,0,0]`.
The retained-child, changing-bound and resized controls have unchanged conservative
escape expectations. Historical source bodies and all 34 frozen native artifacts
remain byte-identical.

## Exact focused validation

- Locked devbox builds used explicit targets `ctjs-opt`, `ctjs-translate`,
  `ctcompile-test-native-reference`, `ctcompile-test-exception-recovery`,
  `ctcompile-test-host-contract`, `ctcompile-test-escape-analysis-arrays`,
  `ctcompile-test-escape-claims` and `ctcompile-test-type-oracle`.
- Final `ctcompile_exception_recovery`: **1/1 PASS, 5.48 s**, **5.49 s total**.
  The preceding candidate passed **5.76 s** before adding whole-module rollback.
- Retained `ctcompile_host_contract`: **1/1 PASS, 2.92 s**;
  `ctcompile_escape_analysis_arrays`: **1/1 PASS, 4.94 s**. These and the preceding
  recovery check passed together in **13.63 s**.
- Selected `Analysis/Escape/escape-claims/cached-length-guard.test`: **1/1 PASS,
  0.11 s**, **358 excluded**. The unchanged frozen source separately passed the
  before/after compiler-claims/VM-recorder oracle.
- Selected native sources `invocation-return`, `write-boolean-snapshot` and
  `helper-next-record`, all refusal controls and both original outer sources:
  **48 native executions, 108 refusals, ten Node/VM runs PASS**. This is not a
  whole fixture or lit pass. Both DOM providers, optimization choices and C++
  printing modes retain GCC/Clang behavior.
- `tools/format.sh --check`: **1126 C++, 158 Python, 114 web files PASS**;
  `git diff --check` passes. Independent nine-file static review is clean.
  Final hashes match the devbox. Twenty-four generated C++ files contain no
  Script/AOT/ctjs namespace; native harness binary-symbol checks pass.
  Final Python formatting only collapsed a condition onto one line; independent
  reconstruction verifies the previous hash and identical AST without replay.

Initial builds exposed a misplaced complete-preparation assertion in the smaller
host-contract target (include, arity and then linkage); that assertion now uses
its linked DOMEntryAnalysis, and full preparation rollback lives in the recovery
target. An initial raw-source text check compared attached versus detached SSA
printing; it now compares the entire unchanged module. The first routed source
probe exposed the remaining frontend snapshot loss, including unreachable
BinaryStatic operations. These were fixed before the successful checks.

An experimental object-identity forwarding extension was removed before landing:
its proposed holder helper still refused at the own-field consumer. DOMPreparation
is unchanged. The frozen holder probe is evidence only, not a new admission or
the next priority. Source/escape agents were rate-limited after preserving their
checkpoints; the final independent review completed after resumption.

Skipped: full CTest/compiler lit, whole native fixtures, unaffected native replay,
broad corpus/matrices, full Bootstrap, WPT/test262, Windows and sanitizers. No
browser/shared implementation, local C++ build, push or history rewrite occurred.

## Exact next boundary

The original sources now reach the custom protocol consumer. Its completion
census still only accepts state/result-free suppressed close Invoke; it needs
retained Try and observed open/next/close correspondence. It also requires a
root-local open and direct record users. Follow successful open aliases through
normal payload/state tuple positions without treating a failed payload as a record.
Carry next/exhaustion state, cleanup, saved returns and caught-node identity.
Both close calls pass false and must propagate failure into the observing catch.
Do not select suppression or discard non-call checks without independent proof.

The cached-length witness is complete. Non-direct cached guard origins remain
outside this bounded proof. Escaping-node exception owners, broader iterators,
unguarded Bootstrap defaults, application driver and full native Bootstrap remain.

Linux inspection read 13 executable identities with 60 permission failures;
Windows Get-CimInstance returned 342 records. Neither found actual Claude, but
status remained uncertain and concurrent rules stayed in force.

Evidence: `../test-results/2026-09-24-iterator-recovery-cached-length/`, with frozen
sources, before/after escape claims and recordings, native probes/generated C++,
focused logs, final hashes, independent review and verified `SHA256SUMS`.
