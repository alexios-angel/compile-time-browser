# Explicit iterator throws and zero-factor indices, 2026-09-22

Resumed clean **45cb1fc1** and exact saved `body-throw`, `f1b3f6b8`, from
HANDOFF/current00 and iteration 75's final journal. No dirty drafts remained;
`codex-wip-20260907` was not unmerged. Other branches were preserved. The user
continuation retained the investigation, claims and temporary evidence.
Parallel agents supplied browser regressions, escape proofs and read-only
native review; frozen drafts survived their rate limits.

## Landed

**3d404e67** adds shared explicit throw/rethrow emission to the source compiler.
It closes synchronous iterator records only up to the nearest active handler.
An inner catch/finally receives the exception first; a subsequent rethrow closes
the records it then exits. Each close suppresses getter/call failures while
preserving the evaluated thrown value and inner-to-outer order. The explicit
throw statement, finally dispatch and async-loop rethrow use the helper.
Separate catch landings preserve the importer's exception-edge contract.
Normal loop bodies retain their existing bytecode shape.

Twenty-seven new Node-backed browser regressions cover direct/nested throws,
catch rethrows, finally replacement and return/break/continue, saved values,
close-error precedence, exhaustion and protocol failures that must not close.
Every historical browser test remains byte-identical. The original native
source remains byte-identical and now agrees with Node on both saved states.
This corrects the source compiler toward JavaScript semantics; no browser API
implementation is duplicated in native output.

**3878ce1e** admits proved zero multiplication factors in counted own-index
ranges, representing the singleton with positive stride 1. Existing bounded
Number endpoint transfers, complete reload/store census, actual-write replay
and budgets remain unchanged. Raw CFG/SCF witnesses cover signed/commuted
zero, translated indices, retained children, snapshots, disjoint reloads and
overlap refusals. Two historical CFG and one SCF constructions now admit
unchanged. Seven appended source functions preserve all 18 earlier bodies
and expectations, including zero-factor sources whose second child escapes.

## Focused validation

- Final `vm_control_flow`: **1/1 PASS, 0.03 s**, including 27 new regressions.
- Final `vm_async`: **1/1 PASS, 0.02 s**.
- Final `ctcompile_host_contract`: **1/1 PASS, 2.30 s**.
- Final `ctcompile_escape_analysis_arrays`: **1/1 PASS, 2.14 s test / 2.15 s total**.
- Exact `Analysis/Escape/escape-claims/scaled-index-overwrite.test` and
  `composed-index-overwrite.test`: **2/2 PASS, 0.11 s**.
- Existing `normal`, `body-return-ordered` and
  `body-return-branch-element-selector-mixed-roots` sources:
  **48 native executions, 58 refusals, zero nonexecuted admissions and
  24 Node/VM observations PASS**. Only their intrinsic/budget controls and
  the original body-throw refusal ran; other positive/refusal sources were omitted.
- Complete `tools/format.sh --check`: **1124 C++, 157 Python, 114 web files PASS**.

All nine final code/test hashes match the devbox. All 24 generated C++ files
have no Script/VM dependency names; standalone checks also gate linked symbols.
Local supplemental checks passed: 27 scoped Node regressions; preserved original
28-row oracle; 25 escape source syntax checks, eight exact outputs, historical
write traces and 3,804 zero-product lattice observations.

The initial 28-row VM probe disagreed with Node in 15 rows. Eleven explicit
completion cases are repaired; four implicit cases remain below. The final
original-source probe matches Node in both states. The first browser gate
passed 3/3 in 2.38 s, but native preflight then exposed the new suppression
handler's normal fallthrough into a catch landing. Separate landing blocks
fixed that importer constraint. No source was weakened or skipped to pass.

The first arrays gate failed four assertions in one historical composed-index
refusal; new rows passed. Node confirms writes `[1,1]`, reads at indices 0/2,
final `[one,zero,x,one]` and retained `x`. Its unchanged construction now has
the corresponding positive expectation. Production/source/SCF did not change
after that failure. Lit was skipped after the failed arrays command and passed
in the final focused gate. The browser and host tests passed in that same
four-test command; its aggregate result was a failure, not a suite pass.

Build targets: `ctjs-opt`, `ctjs-translate`, `ctcompile-tool`,
`ctcompile-test-native-reference`, `ctcompile-test-host-contract`,
`ctbrowser-test-vm_control_flow`, `ctbrowser-test-vm_async`,
`ctcompile-test-escape-analysis-arrays`, `ctcompile-test-escape-claims` and
`ctcompile-test-type-oracle`. All C++ builds/tests used the devbox and shared
build lock. Commands, logs, hashes and sources are in
`../../../../test-results/2026-09-22-iterator-throw-close/`.

Full CTest/compiler lit, complete custom-iterator execution, broad corpus/native
matrices, full Bootstrap, WPT/test262, other browser suites, Windows, sanitizers
and local C++ builds were skipped. Nothing was pushed. Initial process checks
confirmed Claude stopped: Linux WSL-root 82 actual executable identities and
Windows CIM 349 processes, zero errors/matches. Checks before browser edit
batches and landing again found none; pre-landing counts were 84/358.

## Exact next boundary

Original `body-throw`, SHA-256
`f1b3f6b87564e88d0a47ab525f0e5f3d187819285b6d62645c145d309f1a7551`,
remains in the unchanged native fixture's `refusals()` and artifact
`next-boundary/`. It now imports completely. Both native policies return status 1:
**native DOM source: DOM URI requires its fingerprinted initial provider binding**.
No native execution of this source is claimed.

With a body invocation, Node and the VM both throw `1` after
`data-next=false;data-yielded=yes;data-closed=yes;`. Already-exhausted iteration
returns false after `data-next=true;data-yielded=yes;`, with no close.

`DOMPreparation.cpp` currently routes every handler-owning entry through
`normalizeDOMURI` and excludes it from custom-iterator normalization. The next
proof must recognize the close-only suppression region, preserve close effects
and the original primitive throw, then carry abrupt completion through the
existing DOM control flow and C++ throw emission. Generic exception recovery
and DOM completion currently impose additional structured/terminal constraints.
Do not grant URI authority or erase a handler merely to admit this source.

General implicit cleanup remains unimplemented. Exact saved Node/VM mismatches
are an exception from a body call, destructuring default, assignment setter and
an inner iterator's `next` while an outer iterator remains open. Blanket body
guards would change all current native loop sources before this handler proof
exists. Their witnesses are retained as known boundaries, not passing tests.
Broader finally/async close failures, nested custom opens, unguarded Bootstrap
defaults, literal range-for printing, the application driver and full native
Bootstrap remain unfinished; difficult index subranges remain budget-limited.
