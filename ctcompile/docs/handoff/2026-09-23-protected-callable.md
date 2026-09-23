# Protected callable ordering and primitive AND masks, 2026-09-23

Resumed clean **d1a4c47f** and original `body-throw`, `f1b3f6b8`, from HANDOFF,
current00 and iteration 80's final journal. No uncommitted drafts or unmerged
`codex-wip-20260907` remained. Other branches were preserved. Parallel escape,
source-support and read-only review agents contributed; their saved work was
retained through rate limits and the user's continuation.

## Landed

**8a6f33b5** permits immutable callable ordering across an Invoke's call body,
never its normal or unwind continuation. The complete unique-own-slot census,
callee identity, source order and capture checks remain required. Both ordinary
and direct calls reach the same expansion check.

The expansion check accepts only zero-result suppression: the exact call and
its exit, no unwind state, and one unused argument with an empty yield in each
continuation. It reuses `proveUnusedBody` to establish an independently inert
helper for arbitrary inputs. Only after that proof succeeds does it move the
call out, remove the suppression and inline normally. Calls, coercions, getters,
captures and explicit throws cannot borrow this authority. Failed effect proofs
retain a valid call-plus-exit invocation. No verifier or runtime was changed.

Two raw fixtures prove ordinary/direct immutable method expansion and complete
DOM admission under both providers. A third proves inert `typeof` normalization
but retains the existing complete-DOM refusal for an Element operand. Nine
negative controls retain missing/replaced slots, coercions, getters, effects,
continuation observers and unwind state; three incomplete budgets refuse.
Every consumed source operation still needs complete entry proof.

**88bd5c80** permits invariant AND masks through the existing bounded primitive
conversion and bitwise transfer. Endpoint bounds, complete reload/store census,
actual-write replay and work budgets are unchanged. The original String source
mask and String/Boolean raw constructions now admit unchanged. Seven new source
cases and CFG/SCF controls cover negative, Boolean and null masks, commuted and
reloaded operands, surviving/saved children and overlapping or later writes.
All 121 historical source bodies are preserved.

The first lit run also exposed stale expectation 87, `combinedAndUnitStride`.
Its original Number mask is `5`; positions `0,1,0,1,4,5,4,5` clear both child
references without touching the mask at index 2. The exact Node result is
`[0,0,5,0,0,0,0,0]`. Earlier **bcf17b3a** enabled direct-mask gap refinement;
this Number case already satisfies the unchanged Number guard and is not a
new primitive-conversion gain. Its expectation is now `confined`. No source
body or other historical expectation changed beyond rows 14 and 87.

## Focused validation

- `ctcompile_host_contract`: **1/1 PASS, 2.30 s test / 2.31 s total**.
- `ctcompile_escape_analysis_arrays`: **1/1 PASS, 2.19 s test / 2.20 s total**.
- Exact `Analysis/Escape/escape-claims/bitand-index-overwrite.test`:
  **1/1 PASS, 0.13 s** after correcting expectation 87.
- Existing `normal`, `body-return-ordered`, `body-return-branch-number`, plus
  original body throw and four existing refusal controls: **48 native executions,
  82 refusals, zero nonexecuted admissions, 24 Node/VM observations PASS**.
- Five existing throw sources: **20 exact completion/effect observations per
  engine PASS** in Node and VM. These are oracle checks, not native throw runs.
- Complete `tools/format.sh --check`: **1126 C++, 157 Python, 114 web files PASS**;
  scoped pinned formatting and `git diff --check` also passed after final test
  corrections.

All eight final code/test hashes match the devbox. All 24 generated C++ files
contain no Script/VM protocol names; the source harness passed linked-symbol
checks. All 233 prior iterator source bodies and 86 positive metadata rows remain
byte-identical. Local checks also passed syntax for all 128 escape source
functions, nine exact Node outputs, two historical raw array/read witnesses,
historical source preservation, helper Python syntax and shell syntax.

The first native build passed. The first new zero-result fixture used an invalid
custom type spelling; an unchecked null parse in its budget test then crashed.
A parse guard fixed that crash. The next custom spelling was also invalid;
the existing generic operation form fixed parsing. The next host run failed
only two new assertions because Element `typeof` lacks a DOM contract; that
source is retained as an explicit normalization-success/admission-refusal
control. The final rebuild was interrupted after linking; only the unfinished
host, original-source preflight and focused source/oracle checks resumed, and
all passed. Production compiled without repairs. The escape arrays test passed
initially; only its affected lit case reran after the historical expectation fix.

Build targets: `ctjs-opt`, `ctjs-translate`, `ctcompile-test-host-contract`,
`ctcompile-test-native-reference`, `ctcompile-test-escape-analysis-arrays`,
`ctcompile-test-escape-claims`, `ctcompile-test-type-oracle`. Builds used
`tools/remote-build.sh` on the devbox under `/tmp/ctbrowser-devbox-build.lock`;
CTest names and the generated lit configuration were selected exactly.
Commands, logs, baselines, oracle scripts, hashes and generated C++ are in
`../../../../test-results/2026-09-23-protected-callable/`.

Full CTest/compiler lit, the complete custom-iterator fixture, broad corpus/native
matrices, full Bootstrap, WPT/test262, browser suites, Windows, sanitizers and
local C++ builds were skipped. Nothing was pushed or rewritten. After initial
unprivileged/output-file failures, complete WSL-root executable and Windows CIM
checks confirmed Claude stopped: initially 85/353 processes and before docs
83/354, no matches/errors. No browser or shared implementation changed. The
external plan directory has no Git repository; its current-work journal was
updated alongside this committed handoff.

## Exact next boundary

Unchanged `body-throw`, SHA-256
`f1b3f6b87564e88d0a47ab525f0e5f3d187819285b6d62645c145d309f1a7551`,
now passes protected callable ordering. Both native policies return status 1:
**native DOM source: DOM protected helper needs an independent inert-body proof**.
No native execution of this source is claimed.

Its return method captures `anchor`, calls
`anchor.setAttribute('data-closed', 'yes')`, then returns a fresh empty object.
The independent inert proof correctly excludes it. Extend protected expansion
while preserving suppression, or discharge suppression only on a disposable
candidate after complete typed receiver, argument, no-throw and source-effect
proof. Do not merely add DOM calls to the inert operation list. In particular,
DOMEntry's String argument proof alone does not prove `setAttribute` cannot
throw: the VM binding checks invalid names. The existing public
`ctbrowser/dom/element.hpp::is_valid_attribute_name` supplies that check for an
exact literal without copying platform behavior.

Complete prefix/global/reentry checks must precede publication. Typed DOM
non-returning regions and saved primitive throw emission remain separate
obligations. Mutable exceptional state, multiple protected regions, implicit
exception cleanup, nested custom iterators, unguarded Bootstrap defaults, the
application driver and full native Bootstrap remain unfinished.
