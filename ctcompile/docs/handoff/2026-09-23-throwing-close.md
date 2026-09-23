# Suppressed iterator close throws and two-value power indices, 2026-09-23

Resumed clean **6d6d026d** and original `body-throw-close-throws`, `fa88f9d6`,
from HANDOFF, current00 and iteration 89's final journal. No dirty drafts or
unmerged `codex-wip-20260907` remained. Unrelated branches were preserved.
Parallel agents supplied escape changes, source candidates and independent
review. The parent completed source candidates after a rate-limit interruption,
reviewed the combined changes, and owns every devbox gate and commit.

## Landed

**3d221b61** proves a terminal literal throw in the iterator's close
method is unobserved only under exact saved-throw suppression. The original
method has one closure confined to its slot, no symbolic callers and no escaped
holder or extracted method. Completion may remove its normal close only when
done proves exhaustion. A second observer census after completion excludes
surviving holder transport and indirect calls, including a method saved in a cell.

Only then does the private candidate replace the terminal throw with an unused
primitive return and close a live shadow frame. Every preceding producer and
effect remains for full helper and typed DOM reproof. Existing protected attribute
expansion needs no change. Invalid names, nonliteral throws, ordinary unsuppressed
closes, shared methods and malformed frames still refuse. Saved payload ownership,
source order, budgets and publication rollback remain; generated code uses
ordinary native values and public DOM, without Script/VM or a fallback carrier.

Original source SHA256 is
`fa88f9d6f4976de944ca569dd3a8926bbd4230478e12d247f1aa7941e6084288`.
Its body throws Number 1; close writes `data-closed=yes` and throws Number 2.
Native execution preserves Number 1 and the write. Exhaustion does not call close.
A Boolean snapshot witness, `aac46c7e`, preserves its pre-close value as well.
All 233 historical iterator bodies and 86 positive metadata rows survive.

**abddf2d3** uses the existing scalar power transfer when the varying
input lattice has at most two values. Exact endpoint results supply its ordered
output bounds and stride. Both operand orders, signed results, invariant reloads,
complete later-store census, pre-overwrite snapshots and actual-write replay
remain. This adds no general power evaluation: unproved scalar endpoints and
interior values still refuse. All 53 historical power source bodies survive;
source 13 (`nonunitExponent`) and the matching raw Number-2 case now prove
unchanged. Twelve source controls and CFG/SCF checks were added.

## Focused validation

- Fifteen saved-throw sources: **240 native executions, 116 refusals, 88 Node/VM observations PASS**, across both providers/policies, explicit/deduced C++ and GCC/Clang.
- After the final own-store/root tightening, the two affected throwing-close sources: **32 native executions, 32 refusals, eight Node/VM observations PASS**. The thirteen unaffected sources were not repeated after that last change.
- Final exact `ctcompile_host_contract`: 1/1 PASS, **2.40 s test / 2.41 s total**.
- Final exact `ctcompile_escape_analysis_arrays`: 1/1 PASS, **2.28 s test / 2.29 s total**.
- Exact `Analysis/Escape/escape-claims/power-index-overwrite.test`: 1/1 PASS, **0.12 s**, 355 other lit cases excluded.
- Selected prior `normal`, `body-return-ordered`, `body-return-branch-number` and retained refusals: **48 native executions, 82 refusals, zero nonexecuted admissions, 24 Node/VM observations PASS**.
- Five historical throw-oracle sources: **20 exact completion/effect observations per engine PASS**.
- Full `tools/format.sh --check`: **1126 C++, 157 Python, 114 web files PASS**. Final `git diff --check` PASS.
- Local preservation and syntax: all **233 historical iterator bodies and 86 metadata rows** unchanged; **44 saved-throw Node observations** and **65 escape outputs/own-key sets PASS**. All 53 historical escape source bodies and all unpromoted CHECKs survive.
- All **seven final code/test hashes match the devbox**. All **144 generated C++ files** are free of Script/VM protocol and nullable-scalar fallback; compiled harness linked-symbol checks pass.
- Final preflight records **20 admission/refusal outcomes** across both policies. Original `fa88f9d6` and Boolean `aac46c7e` admit; the getter, conditional primitive/throwing closes, normal throwing closes and nonliteral payload still refuse.

The first host run failed because the new raw helper applied the primitive
replacement twice and received an empty fixture. Corrected callers and an explicit
nonempty guard fixed the test. Subsequent host gates pass. Independent review
then found a real method-alias gap: confinement of the closure alone did not
exclude extracting its return slot into a cell. The original and post-completion
holder/method observer checks and alias regressions close that gap before
landing; final native validation ran after the correction.

The first arrays run failed two new expected provenance labels: a computed
`%two` retains origin `ctjs.binary`. Corrected only those expectations;
production and historical tests are unchanged. The final arrays/lit gate passes.

Explicit build targets were `ctjs-opt`, `ctjs-translate`,
`ctcompile-test-host-contract`, `ctcompile-test-native-reference`,
`ctcompile-test-escape-analysis-arrays`, `ctcompile-test-escape-claims` and
`ctcompile-test-type-oracle`. All builds, native/VM executions and devbox reads
held `/tmp/ctbrowser-devbox-build.lock`. Commands and evidence are in
`../../../../test-results/2026-09-23-throwing-close/`, under `native90/`,
`tests90/` and `escape90/`. `native90/final-proof-gate.sh` selects fifteen sources;
`final-observer-gate.sh` selects the final host and two affected sources.
`final-escape-gate.sh` and `final-prior-gate.sh` select the other focused checks.

Full CTest/compiler lit, the complete custom-iterator fixture, broad corpus/native
matrices, full Bootstrap, WPT/test262, browser suites, Windows, sanitizers and
local C++ builds were skipped. Nothing was pushed or rewritten. No browser or
shared implementation changed; historical WPT counts remain historical.

WSL-root /proc and Windows CIM process identity checks initially inspected
81/348 processes and later 87/356, with no matches or errors: Claude was
confirmed stopped. The final pre-publication check inspected 84/350 with no matches or errors.
The external plan has no Git repository; current00 was updated separately.

## Exact next boundary

Original getter close `68ea7208` still refuses **DOM helper has no complete
return or yield** in both policies. Its return property getter performs an
attribute write and throws during method lookup. Prove that exact lookup path
under the existing suppression while retaining the body's saved exception and
all getter effects. Preserve the original source.

Derived conditional primitive/throwing closes `1fe1a3e8`/`fd04cbc3` still need
correlation between loop completion and done state. Mutable throwing-close state,
nonterminal exceptional state, multiple protected regions, implicit cleanup,
nested custom iterators, unguarded Bootstrap defaults, the application driver
and full native Bootstrap remain unfinished. No full-Bootstrap admission or
coverage gain is claimed.
