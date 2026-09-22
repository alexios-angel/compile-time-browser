# Iterator terminal dispatch and primitive factors, 2026-09-22

Resumed clean **3a77bcdd** and the exact `body-throw` source `f1b3f6b8`
from HANDOFF/current00 and iteration 77's final journal. No uncommitted drafts
or unmerged `codex-wip-20260907` remained. Other branches were preserved.
User continuations and rate limits interrupted the parallel agents; their
baselines, drafts and evidence were retained and completed.

## Landed

**fd97e48d** structures the mixed terminal CFG after the existing close-handler
normalization. Upstream CFG-to-SCF handles the shared graph. An exact two-way
`cf.cond_br` or `cf.switch` must reach distinct throw and return tails, with no
other root blocks or predecessors. Incoming values and complete tail bodies
move to their corresponding SCF arms without duplication.

The original `ctjs.throw` terminates a zero-result, `no_inline`
`scf.execute_region`. Its arm has a structurally required, unreachable poison
yield. The other arm yields the original normal return value; the root retains
one real return. Saved payloads, normal-only effects and the suppressed close
stay in their source arms. No source throw becomes a placeholder return.

The upstream transformation is charged before mutation using a divided
quadratic bound. Rewriting and final verification occur on the existing
disposable clone. Unsupported shapes and exhausted budgets leave the candidate
unchanged. The analysis target links the upstream CFG-to-SCF utility directly.
No new IR operation, runtime value model or change to `CppThrowOp::verify`
was needed. This is structural progress, **not native throw admission**.

Raw tests cover reversed branch directions under both DOM providers, saved
payload identity, close suppression, normal-only writes, complete exit census
and low-budget rollback. Four new source refusals cover conditional Boolean
and Number snapshots, throw versus return, and an abrupt close returning a
primitive. All **229** historical native source bodies and **86** positive
metadata rows remain unchanged.

**c16ee82d** lets invariant multiplication factors reach the existing bounded
primitive Number conversion. Other index operators retain their Number guard;
String addition still concatenates. Canonical signed Strings, Boolean and null
factors preserve endpoint bounds, original primitive identities, complete
reload/store census, actual-write replay and budgets. Objects, undefined,
BigInt, unsupported spellings and overlapping reloads still refuse.

The historical String-factor CFG construction and source function are unchanged;
only their former refusal expectations were promoted. Added CFG, SCF and ten
source witnesses cover commuted multiplication, zero and negative factors,
retained/saved children and factor reloads. All **30** previous source bodies
remain; 29 historical CHECKs are unchanged and source 12 now reports confined.

## Focused validation

- `ctcompile_host_contract`: **1/1 PASS, 2.29 s test / 2.30 s total**.
- `ctcompile_escape_analysis_arrays`: **1/1 PASS, 2.22 s test / 2.23 s total**.
- Exact `Analysis/Escape/escape-claims/scaled-index-overwrite.test`:
  **1/1 PASS, 0.11 s**.
- Selected existing sources `normal`, `body-return-ordered` and
  `body-return-branch-number`, with the original body throw and four new
  refusal controls: **48 native executions, 82 refusals, zero nonexecuted
  admissions and 24 Node/VM observations PASS**. The complete custom-iterator
  fixture was not replayed.
- Five selected throw sources: **20 exact observations per engine PASS** in
  Node and VM, covering fresh, stopped, alternate-return and exhausted inputs.
  These compare completions and ordered writes, including the saved value
  before close. They are oracle measurements, not native throw executions.
- Complete `tools/format.sh --check`: **1126 C++, 157 Python and 114 web files
  PASS**, including the final repeat after scoped formatting.

All **eight** final code/test hashes match the devbox. All **24** generated
C++ files contain no Script/VM protocol names; the source harness also passed
its linked-symbol checks. Supplemental local checks passed: 233 native source
syntax checks, historical source preservation, 11 escape outputs and original
raw traces, and 366 primitive-lattice enclosure points.

The first build passed but its shell input was consumed before the following
CTest command, so no test result was claimed for that invocation. The explicit
host run then failed four new assertions because the simple raw CFG retained
`cf.cond_br`; handling that exact two-way form fixed them. The next run failed
only the new arbitrary 1024-step exhaustion expectation: that budget was
sufficient. The low-budget test now uses 512; the final host run passes.
Existing hostile-verifier diagnostics were expected. Escape and selected-source
gates passed on their first completed runs.

Build targets were `ctjs-opt`, `ctjs-translate`, `ctcompile-tool`,
`ctcompile-test-host-contract`, `ctcompile-test-native-reference`,
`ctcompile-test-escape-analysis-arrays`, `ctcompile-test-escape-claims` and
`ctcompile-test-type-oracle`. Builds used `tools/remote-build.sh` on the devbox
under `/tmp/ctbrowser-devbox-build.lock`; CTests were selected by anchored exact
name and lit used the generated build-tree configuration with one exact filter.
Commands, logs, source baselines, oracle scripts, generated C++ and hashes are in
`../../../../test-results/2026-09-22-iterator-terminal-dispatch/`.

Full CTest/compiler lit, complete custom-iterator fixtures, broad corpus/native
matrices, full Bootstrap, WPT/test262, browser suites, Windows, sanitizers and
local C++ builds were skipped. Nothing was pushed or rewritten. Linux executable
and Windows CIM checks confirmed Claude stopped: initially **84/354** processes,
and before documentation **86/352**, with no matches or errors. No browser or
shared implementation file changed.

## Exact next boundary

Unchanged `body-throw`, SHA-256
`f1b3f6b87564e88d0a47ab525f0e5f3d187819285b6d62645c145d309f1a7551`,
now passes terminal structuring. Both native policies return status 1:
**native DOM source: DOM custom iterator abrupt completion needs a handler proof**.
No native execution of this source is claimed.

Next, prove the close invocation and exact throw-only region in
`DOMCustomIteration.cpp`. Its completion census still refuses `ThrowOp` and
`InvokeOp`; its clone walker does not consume the new standard SCF wrapper.
`DOMSource::normalizeCompletion` must discharge the unreachable yield only from
the non-returning throw proof, and `DOMEntry` must prove the saved primitive
payload and complete host effects. Preserve prefix/global/reentry reproof before
trusting the moved initial close lookup. Existing typed C++ throw emission can
then place `CppThrowOp` immediately before `emitc.yield`; its verifier stays.

Multiple protected regions, implicit exception cleanup, nested custom iterators,
unguarded Bootstrap defaults, the application driver and full native Bootstrap
remain unfinished.
