# Typed protected attributes and primitive shift counts, 2026-09-23

Resumed clean **42c0502c** and unchanged `body-throw`, `f1b3f6b8`, from HANDOFF,
current00 and iteration 82's final journal. No abandoned drafts or unmerged
`codex-wip-20260907` remained; unrelated branches were preserved. Parallel escape,
source-support and read-only review agents contributed. Parent retained and
finished their drafts and evidence through rate limits and user continuations.

## Landed

**5cd8002a** admits one exact zero-result suppressed attribute call. It requires
an unnested invocation with one call and exit, no carried state, and empty normal
and unwind continuations whose payloads are unused. The callee is the initial
Element `setAttribute`, its receiver is a proved Element, and both arguments
are Strings. The name must be a literal accepted by the public
`ctbrowser::is_valid_attribute_name`; its byte scan is charged before validation.
The complete existing body proof retains method/receiver identity, dominance,
source effects, mutation epochs and the final entry contract. Incomplete proofs
publish no evidence. CTNativeAnalysis now links the public DOM library.

Only after that proof does lowering remove the unnecessary suppression wrapper.
It moves the original call before the wrapper, under the same guard, then uses
ordinary DOM call emission. Neither unobserved continuation payload needs a
native carrier. No new runtime or exception representation is introduced;
foreign C++ failures are not caught. Source coercion, invalid-name and receiver
failures must be excluded by the proof, not swallowed by generated catch-all code.

Raw execution tests use the existing harness, two Element parameters and their
identity guard. They check one write on the true arm, zero writes on the false
arm, invalid-input rejection and owning-session calls. Both `data-closed` and
`1:closed` names execute; invalid, empty and computed String names, wrong receivers
and observed normal payloads refuse. The original source throw stays a refusal.

**dad6a885** admits bounded primitive invariant counts for left, signed-right and
unsigned-right shifts through the existing conversion and modulo-32 transfer.
Input/output bounds, operand order, complete reload/store census, actual-write
replay and budgets remain unchanged. CFG/SCF controls cover String, Boolean,
null, negative/modulo counts, saved children, retained children and count reloads.
Noncanonical/out-of-bound counts, object conversion and later stores remain
conservative. The unchanged historical `stringCount` sources in the left/right
lit fixtures now admit; only their old CHECKs 18/19 were promoted. All 233
historical bodies survive, with sixteen source controls appended.

## Focused validation

- `ctcompile_host_contract`: **1/1 PASS, 2.27 s test / 2.28 s total**.
- `ctcompile_escape_analysis_arrays`: **1/1 PASS, 2.22 s test / 2.23 s total**.
- Exact `Analysis/Escape/escape-claims/left-shift-index-overwrite.test` and
  `right-shift-index-overwrite.test`: **2/2 PASS, 0.15 s total**.
- Protected raw attributes: **32 native executions and 20 refusals PASS**,
  both providers, both optimization policies, explicit/deduced C++ and GCC/Clang.
- Existing `normal`, `body-return-ordered`, `body-return-branch-number`, plus
  original body throw and four existing refusal controls: **48 native executions,
  82 refusals, zero nonexecuted admissions, 24 Node/VM observations PASS**.
- Five existing throw sources: **20 exact completion/effect observations per
  engine PASS** in Node and VM. These are not native throw executions.
- Complete `tools/format.sh --check`: **1126 C++, 157 Python, 114 web files PASS**.
- Local escape checks: **249 source syntax checks, 18 exact Node outputs and
  233 historical source bodies preserved**. Iterator preservation checks retain
  all 233 source bodies and 86 positive metadata rows; helper syntax checks pass.

All eleven final code/test hashes match the devbox. All 40 generated C++ files
contain no Script/VM protocol names; the source harness passed linked-symbol
checks. The first build and host test passed. The first raw execution exposed
a fixture error: null Element validation throws
`std::bad_expected_access<dom_error>` with `no_such_node`, while the new check
expected `std::invalid_argument`. Only that check changed. An interruption lost
the corrected run's local final status; remote artifacts alone were not called
a pass. The unfinished focused gate reran with a persistent remote log/status
and passed. An initial formatting check saw unfinished escape formatting;
scoped formatting followed by the complete check passed. Escape build and both
focused gates passed on their first run.

Build targets: `ctjs-opt`, `ctjs-translate`, `ctcompile-test-host-contract`,
`ctcompile-test-native-reference`, `ctcompile-test-escape-analysis-arrays`,
`ctcompile-test-escape-claims`, `ctcompile-test-type-oracle`. Builds used
`tools/remote-build.sh` on the devbox under `/tmp/ctbrowser-devbox-build.lock`.
CTest names and generated lit configuration were selected exactly. Commands,
logs, hashes, baselines, generated C++ and oracle evidence are in
`../../../../test-results/2026-09-23-protected-attribute-admission/`.

Full CTest/compiler lit, the complete custom-iterator fixture, broad corpus/native
matrices, full Bootstrap, WPT/test262, browser suites, Windows, sanitizers and
local C++ builds were skipped. Nothing was pushed or rewritten. WSL-root process
identity and Windows CIM checks confirmed Claude stopped: initially 81/354
processes and before docs 83/356, no matches or errors. No browser/shared
implementation changed. The external plan has no Git repository; its
current-work journal was updated alongside this committed handoff.

## Exact next boundary

Unchanged `body-throw`, SHA-256
`f1b3f6b87564e88d0a47ab525f0e5f3d187819285b6d62645c145d309f1a7551`,
now passes typed suppressed-call admission. Both native policies return status 1:
**native DOM entry: DOM entry does not admit nested control flow or a source
continuation**. No native execution of this source is claimed.

`HostContract/DOMEntry/Body.cpp` rejects the exact nonreturning `scf.execute_region`
already carried through helper normalization. Prove its no-inline, zero-result,
zero-argument region with the sole saved primitive throw; preserve local
definitions, dominance, budgets and the structural yield. Nonreturning branches
must not contribute a shadow-frame exit or an ordinary result to a reachable
sibling. Add proof-gated native operation admission and use the existing
`CppThrowOp`, with its owning primitive payload and immediately following
terminator. Merely whitelisting the region is insufficient. Complete typed
effects and prefix/global/reentry proof remain required before publication.

Mutable exceptional state, multiple protected regions, implicit cleanup, nested
custom iterators, unguarded Bootstrap defaults, the application driver and full
native Bootstrap remain unfinished.
