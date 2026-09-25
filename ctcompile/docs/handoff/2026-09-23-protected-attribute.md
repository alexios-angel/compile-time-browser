# Protected attribute expansion and primitive OR/XOR masks, 2026-09-23

Resumed clean **16d0dea5** and unchanged `body-throw`, `f1b3f6b8`, from HANDOFF,
current00 and iteration 81's final journal. No uncommitted drafts or unmerged
`codex-wip-20260907` remained; unrelated branches were preserved. Parallel escape,
source-support and read-only review agents contributed. Their saved drafts and
evidence survived rate limits and the user's continuation.

## Landed

**7cc06580** expands one exact protected helper: constants and immutable captures,
a literal `setAttribute` method lookup, its sole call, and an unused fresh empty
return object. The complete census excludes additional effects, getters, result
observers, fields, aliases and wrong receivers. Ordinary and direct calls share
this path. The attribute call remains inside the original zero-result Invoke;
its normal and unwind continuations remain empty and its result stays unused.
Only the confined return allocation is elided. Independent inert-body proof
remains the sole existing route that removes suppression.

Preparation is cloned at the original invocation point under the same guard.
After the charged clone succeeds, the new call moves into Invoke and the old
call retires, restoring exactly one call and exit. Hoisting the method lookup
is conditional on complete typed DOM reproof of the initial Element method.
This normalization works only on a private candidate and grants no native
admission or no-throw authority. Intrinsic preparation rejects this original
object pattern before expansion. No runtime or verifier changed.

Raw controls cover ordinary, direct and captured helpers, invalid-name
suppression retention, extra calls, object fields, getters, observed results,
wrong receivers and unknown methods. Accepted normalization retains valid IR;
complete DOM admission still refuses, including for a valid attribute name.

**bab3f790** admits invariant OR/XOR masks through the existing bounded primitive
conversion and bitwise transfer. Endpoint/lattice bounds, complete reload/store
census, actual-write replay and budgets are unchanged. Four historical raw
String/Boolean constructions now admit unchanged. Added CFG/SCF and source
controls cover String, Boolean, null, negative, commuted and reloaded masks;
saved children, overlapping reloads and later stores remain conservative.
All 179 historical source bodies are preserved.

The first exact lit run exposed stale Number expectation 134,
`combinedOrUnitStride`. Its unchanged mask is `2`, stored at index 4; writes visit
`2,3,2,3,6,7,6,7`, leaving the mask intact and removing both child references.
Node observes `[0,0,0,0,2,0,0,0]`. Earlier **bcf17b3a** enabled direct-gap
refinement. This existing Number case already passed the old Number guard,
so it is not a primitive-conversion gain. Only that historical CHECK changed;
all 189 object verdicts were compared with the focused run's actual claims.

## Focused validation

- `ctcompile_host_contract`: **1/1 PASS, 2.30 s test / 2.31 s total**.
- `ctcompile_escape_analysis_arrays`: **1/1 PASS, 2.18 s test / 2.19 s total**.
- Exact `Analysis/Escape/escape-claims/bitor-xor-index-overwrite.test`:
  **1/1 PASS, 0.15 s**, after correcting expectation 134.
- Existing `normal`, `body-return-ordered`, `body-return-branch-number`, plus
  original body throw and four existing refusal controls: **48 native executions,
  82 refusals, zero nonexecuted admissions, 24 Node/VM observations PASS**.
- Five existing throw sources: **20 exact saved-completion/effect observations
  per engine PASS** in Node and VM. These are oracle checks, not native throw runs.
- Complete `tools/format.sh --check`: **1126 C++, 157 Python, 114 web files PASS**.
  The later lit correction changed only its CHECK comment; final diff checks pass.

All six final code/test hashes match the devbox, including the corrected lit
fixture. All 24 generated C++ files contain no Script/VM protocol names; the
source harness passed linked-symbol checks. All 233 previous iterator source
bodies and 86 positive metadata rows remain byte-identical. Local checks pass
syntax for 189 escape and 233 iterator sources, eleven exact escape Node outputs,
four historical raw witnesses, historical source preservation, helper Python
syntax and shell syntax.

The first native build, host test and selected source checks passed. The first
escape build and arrays test passed; only the lit expectation failed. Production
needed no repair, and only that exact lit case reran after its comment correction.
No broad replay was run.

Build targets: `ctjs-opt`, `ctjs-translate`, `ctcompile-test-host-contract`,
`ctcompile-test-native-reference`, `ctcompile-test-escape-analysis-arrays`,
`ctcompile-test-escape-claims`, `ctcompile-test-type-oracle`. Builds used
`tools/remote-build.sh` on the devbox under `/tmp/ctbrowser-devbox-build.lock`;
CTest names and the generated lit configuration were selected exactly.
Commands, logs, baselines, oracle scripts, hashes and generated C++ are in
`../../../../test-results/2026-09-23-protected-attribute/`.

Full CTest/compiler lit, the complete custom-iterator fixture, broad corpus/native
matrices, full Bootstrap, WPT/test262, browser suites, Windows, sanitizers and
local C++ builds were skipped. Nothing was pushed or rewritten. WSL-root process
identity and Windows CIM checks confirmed Claude stopped: initially 81/351
processes and before docs 89/354, no matches or errors. No browser or shared
implementation changed. The external plan directory has no Git repository;
its current-work journal was updated alongside this committed handoff.

## Exact next boundary

Unchanged `body-throw`, SHA-256
`f1b3f6b87564e88d0a47ab525f0e5f3d187819285b6d62645c145d309f1a7551`,
now passes helper expansion. Both native policies return status 1:
**native DOM entry: DOM URI invocation requires complete unnested continuations**.
No native execution of this throw source is claimed.

`HostContract/DOMEntry/ControlFlow.cpp` currently permits one-result URI/JSON
invocations, while this is a zero-result suppressed attribute call. Complete
receiver/argument/source-effect proof must justify the initial method lookup,
retain the original guard and establish the suppressed call's failure behavior.
Keep suppression, or remove it only after complete no-throw proof. The public
`ctbrowser/dom/element.hpp::is_valid_attribute_name` supplies literal-name
validation; String argument facts alone do not exclude InvalidCharacterError.
Do not widen the invocation verifier or flatten an effectful body into it.

Typed non-returning DOM regions and saved primitive throw emission remain
separate obligations, along with prefix/global/reentry proof before publication.
Mutable exceptional state, multiple protected regions, implicit cleanup, nested
custom iterators, unguarded Bootstrap defaults, the application driver and full
native Bootstrap remain unfinished.
