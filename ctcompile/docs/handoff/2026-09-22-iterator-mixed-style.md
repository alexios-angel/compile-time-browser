# Mixed-input iterator selectors and signed bitwise gaps, 2026-09-22

Resumed clean **6bfe8e48** and exact saved mixed-input selector `987dfd90`
from HANDOFF/current00 and iteration 74's final journal. Previous work was
committed; `codex-wip-20260907` was not unmerged. Other branches were preserved.
The interrupted investigation and test baselines survived user continuation.
Native regression, escape and read-only review agents worked independently;
their frozen drafts were retained across rate limits.

## Landed

**05a007b6** extends the charged Style-origin closure to multiple original
inputs. A successful complete DOM proof publishes the input roots and the
values whose selected association must travel through control flow. Unknown
origins, unproved kinds, escapes, invalidation and reentry remain refused.
Partial proofs publish neither calls nor carried Style values.

Emission retains the existing `element_ref` ABI and adds ordinary borrowed
`style::engine *` values to the corresponding SCF slots. Branch yields,
loop initialization/backedges and condition/result tuples retain their exact
correspondence, including different before/after tuple sizes. Temporary SSA
links resolve and disappear before output. Selector descendants, snapshots and
document roots preserve their original association. Preparation runs only on
the owning entry; review caught and fixed an earlier callback-order hazard.
Every required input engine validates against its own document before writes.
Owned sessions retain their owner checks and single owned engine.

The exact saved source `987dfd90`, before-close `924111e5` and prototype
`79687c1a` execute unchanged from their saved candidates. Tests cover distinct
documents/engines, reversed inputs, same-document different elements, detached
elements, exhausted loops, invalid handles and either mismatched engine before
writes. Eight raw positives cover all four selector methods, descendants,
snapshots, backedges and exhaustion. Four historical refusals now admit with
their original constructions. Hostile and budget controls retain refusal.
All 222 historical source bodies, 86 positive metadata rows and 41 original
MLIR literal blocks remain unchanged.

**f687c5d9** reuses the existing converted lattice for XOR and sign-preserving
AND/OR ranges crossing a signed conversion boundary. Transformed low input bits
give the residue; existing bounded refinement resolves gaps. Complete reload
and store census, actual-write replay and all budgets remain unchanged.
Eight CFG, eight SCF and eight source regressions cover masks, zero crossings,
retained children, saved snapshots, actual reload hits and later stores.
All 150 historical source bodies and call tails remain unchanged.

## Focused validation

- Exact `ctcompile_host_contract`: **1/1 PASS, 2.29 s test / 2.31 s total**.
- Exact `ctcompile_escape_analysis_arrays`: **1/1 PASS, 2.18 s test / 2.19 s total**.
- Exact `Analysis/Escape/escape-claims/right-shift-index-overwrite.test`:
  **1/1 PASS, 0.14 s**.
- Three mixed-input sources plus the prior single-root selector, with existing
  refusal controls: **64 native executions, 318 refusals, zero nonexecuted
  admissions and 32 Node/VM observations PASS**. Other positive executions and
  the unchanged compile-only case were omitted.
- Complete `tools/format.sh --check`: **1124 C++, 157 Python, 114 web files PASS**.

All eleven final code/test hashes match the devbox. The 32 generated C++ files
contain no Script/VM protocol; standalone tests also gate linked symbols.
Inspection confirms paired element/Style choices and the saved selector read
before the close write. Native tests passed on their first execution gate.

The initial arrays run failed 24 assertions in four historical CFG and four
SCF expectations, total 2.49 s. Exact Node traces confirm that nested AND masks
write only zero and OR masks write only one across either signed boundary;
their reload slots remain untouched and the original children remain retained.
Only those expectations were promoted. Production, original constructions and
source tests stayed unchanged. Right-shift lit was skipped after that failure
and passed in the final gate. A preexisting deprecated `make_scope_exit` warning
in `DOMEntry/ControlFlow.cpp` did not fail the affected build.

Additional local checks: 225 native source syntax checks, 12 exact mixed-input
Node traces and two semantic mutations; 158 escape functions, eight exact
outputs, four new traces, four historical promotion traces and 21,360 enclosure
checks. These supplement the devbox gates.

Affected build targets were `ctjs-opt`, `ctjs-translate`, `ctcompile-tool`,
`ctcompile-test-native-reference`, `ctcompile-test-host-contract`,
`ctcompile-test-escape-analysis-arrays`, `ctcompile-test-escape-claims` and
`ctcompile-test-type-oracle`. All C++ builds/tests used the devbox and shared
build lock. Exact commands, logs, hashes, emitted C++ and source evidence are
in `../../../../test-results/2026-09-22-iterator-mixed-style/`.

Full CTest/compiler lit, complete custom-iterator execution, broad corpus/native
matrices, full Bootstrap, WPT/test262, browser suites, Windows, sanitizers and
local C++ builds were skipped. Nothing was pushed. Initial process checks
confirmed Claude stopped: WSL-root Linux 83 readable identities with zero
errors/matches; Windows CIM 352 processes with no Claude executable/CLI/loop.
No browser/shared implementation or runtime-oracle edits occurred.

## Exact next boundary

Original `body-throw`, SHA-256
`f1b3f6b87564e88d0a47ab525f0e5f3d187819285b6d62645c145d309f1a7551`,
remains in `refusals()` and is saved in the artifact `next-boundary/` directory.
Both native policies return status 1 with **DOM custom iterator requires one
complete entry**. No native execution is claimed.

The separate Node/VM probe records two states. With a body invocation, both
throw `1`, but Node writes `data-next=false;data-yielded=yes;data-closed=yes;`
while the VM omits `data-closed=yes;`. Already-exhausted iteration agrees:
return false, writes `data-next=true;data-yielded=yes;`, no close. This is one
observed oracle disagreement, not a differential pass. It is consistent with
the previously recorded omission of general body-throw cleanup after the
explicit-return fix `380ee61d`.

Repair source-compiler body-throw IteratorClose first, preserving the original
exception, close-error precedence and catch/finally scope. Then prove native
abrupt traversal completion. Do not change the witness or make the VM match
native. Broader finally cleanup, nested custom opens, unguarded Bootstrap
defaults, literal range-for printing, the application driver and full native
Bootstrap remain unfinished; difficult index subranges remain budget-limited.
