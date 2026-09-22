# Helper throw joins and primitive remainder divisors, 2026-09-22

Resumed clean **444fc1bd** and original `body-throw`, `f1b3f6b8`, from HANDOFF,
current00 and iteration 79's final journal. No uncommitted drafts or unmerged
`codex-wip-20260907` remained. Other branches were preserved. User continuations
and rate limits interrupted the parallel workflows; saved drafts, source scripts
and baselines were retained and completed.

## Landed

**843f3a4f** extends the helper body proof to the existing zero-result,
`no_inline` SCF region containing exactly one saved throw. Its operand must have
a preceding local definition and dominate the throw; the region cannot observe
a shadow frame. Only the required structural yield can follow a non-returning
path. A branch joins frame state from its reachable sibling, while two returning
arms still require identical frame state. Nested throw-only branches propagate
that fact; non-returning loop regions remain unsupported.

Three raw positive fixtures cover either throw-arm orientation and nested
throw-only arms. Expansion retains each original throw and remaps its saved
payload to the invocation's actual String. Six negative fixtures cover an
unmarked wrapper, extra wrapped or post-throw effects, a live returning frame,
mismatched normal frame exits and effects after a nested non-returning branch.
Three incomplete budget checks refuse. The unused-body census and complete
DOM entry proof remain required. No new IR operation, runtime carrier, browser
implementation or relaxed invocation/C++ throw verifier was added.

**b1186fc5** permits remainder's invariant divisor through the existing bounded
primitive conversion, alongside multiplication and division. The remainder
transfer is unchanged: nonzero divisors, signed endpoint bounds, congruence
ranges, reload/store census, actual-write replay and work budgets remain.
Historical String and Boolean raw constructions are preserved with exact
array/read observations. Historical source checks 14 (`stringDivisor`) and 33
(`negativeStringDivisor`) now admit unchanged; all 68 previous source bodies and
other expectations survive. Seven new source cases and CFG/SCF rows cover reload
gaps, retained and saved children, overlapping/later writes and refused primitive
or object conversions.

## Focused validation

- `ctcompile_host_contract`: **1/1 PASS, 2.31 s test / 2.32 s total**.
- `ctcompile_escape_analysis_arrays`: **1/1 PASS, 2.16 s test / 2.17 s total**.
- Exact `Analysis/Escape/escape-claims/remainder-index-overwrite.test`:
  **1/1 PASS, 0.12 s**.
- Existing `normal`, `body-return-ordered`, `body-return-branch-number`, plus
  original body throw and four existing refusal controls: **48 native executions,
  82 refusals, zero nonexecuted admissions, 24 Node/VM observations PASS**.
- Five existing throw sources: **20 exact completion/effect observations per
  engine PASS** in Node and VM. These are oracle checks, not native throw runs.
- Complete `tools/format.sh --check`: **1126 C++, 157 Python, 114 web files PASS**.

All seven final code/test hashes match the devbox. All 24 generated C++ files
contain no Script/VM protocol names; the source harness passed linked-symbol
checks. All 233 prior iterator source bodies and 86 positive metadata rows remain
byte-identical. Local checks also passed 75 escape source syntax checks, nine
exact Node outputs, two historical raw array/read witnesses, historical source
preservation, Python syntax, shell syntax and `git diff --check`.

The initial native build, host CTest and selected source checks passed without
repairs. An interruption stopped the escape gate during configuration and the
formatter before completion; neither partial run was counted as passing. Only
those unfinished checks resumed, and both passed. The final source preflight
still reports the same protected callable boundary after the escape rebuild.
Native execution checks preceded that independent escape rebuild; no broad replay
was run.

Build targets: `ctjs-opt`, `ctjs-translate`, `ctcompile-test-host-contract`,
`ctcompile-test-native-reference`, `ctcompile-test-escape-analysis-arrays`,
`ctcompile-test-escape-claims`, `ctcompile-test-type-oracle`. Builds used
`tools/remote-build.sh` on the devbox under `/tmp/ctbrowser-devbox-build.lock`;
CTest names and the generated lit configuration were selected exactly.
Commands, logs, baselines, oracle scripts, hashes and generated C++ are in
`../../../../test-results/2026-09-22-iterator-helper-throw/`.

Full CTest/compiler lit, the complete custom-iterator fixture, broad corpus/native
matrices, full Bootstrap, WPT/test262, browser suites, Windows, sanitizers and
local C++ builds were skipped. Nothing was pushed or rewritten. Actual Linux
executable and Windows CIM checks confirmed Claude stopped: initially 82/354
processes, before documentation 90/351, no matches/errors. No browser or shared
implementation file changed. The external plan directory has no Git repository;
its current-work journal was updated alongside this committed handoff.

## Exact next boundary

Unchanged `body-throw`, SHA-256
`f1b3f6b87564e88d0a47ab525f0e5f3d187819285b6d62645c145d309f1a7551`,
now passes helper branch and shadow-frame proof. Both native policies return
status 1: **native DOM source: DOM helper object has nonlocal or unordered uses**.
No native execution of this source is claimed.

`Analysis/ClosedCallable.cpp` orders holder uses only through If/While regions;
the original return method's protected receiver use reaches an Invoke region.
Prove that exact immutable own-slot use without granting authority to arbitrary
exception continuations. `DOMSource/Expansion.cpp` currently substitutes a helper
body at its call, which would violate the invocation verifier's required single
call followed by its exit. Preserve that protected call shape, or discharge
suppression only with a complete no-throw proof. Do not simply whitelist Invoke
in the ordering helper and flatten the method body into its region.

`DOMEntry` then needs complete typed effects for zero-result suppression and the
saved primitive throw, followed by existing typed C++ emission. Keep complete
prefix/global/reentry proof before trusting moved initial lookups. Mutable state
across close exceptions, multiple protected regions, implicit exception cleanup,
nested custom iterators, unguarded Bootstrap defaults, the application driver
and full native Bootstrap remain unfinished.
