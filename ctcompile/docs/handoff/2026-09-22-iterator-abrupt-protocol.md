# Protected iterator completion and primitive divisors, 2026-09-22

Resumed clean **81999dbb** and exact `body-throw`, `f1b3f6b8`, from the latest
HANDOFF/current00 and iteration 78 journal. No uncommitted drafts or unmerged
`codex-wip-20260907` remained. Other branches were preserved. User continuations
and agent rate limits interrupted this iteration; the parent retained and
finished the agents' production drafts, baselines and source-check artifacts.

## Landed

**a78c1b80** recognizes the exact zero-result close invocation: one abrupt
helper call, its exit, and two empty continuations with unused result/error
arguments. It requires the original iterator record and checks every traversal
exit. The unique own return slot is resolved outside the invocation; its actual
call stays protected, under the original done guard. Both outcomes leave the
record done. Mutable Number state across suppression remains a diagnostic.

The original saved throw remains in a zero-result `no_inline` SCF region.
Completion normalization accepts only a single throw followed by structural
padding and the required yield. It does not execute or duplicate the shared
continuation on that path. Defined inactive padding precedes the throw wrapper;
a real reachable source return is required, and the complete source-operation
census remains. Opaque invocations retain both continuations, with checked
captures and budget charged before verification and cloning.

Raw tests cover both providers, the saved Number payload, one protected return
call, protocol removal and valid IR. Wrong records/flags, an observed catch,
unmarked regions, source effects after throw and mutable exceptional state
refuse. This is structural progress: no native throw admission, runtime carrier,
browser implementation change or relaxed invocation/C++ throw verifier.

**530a8e4b** lets invariant division reach the existing bounded primitive
Number conversion. Nonzero divisors, exact first position and stride divisibility,
signed endpoints, complete reload/store census and actual-write replay remain.
The historical String-divisor raw construction and source body are unchanged;
only their refusal expectations were promoted. CFG/SCF tests cover Boolean and
negative String divisors, retained/saved children, reload identity, fractional
visits, overlap, later writes and unsupported conversions. Eight source cases
were added; all 17 historical bodies and the other 16 expectations remain.

## Focused validation

- Final `ctcompile_host_contract`: **1/1 PASS, 2.29 s test / 2.30 s total**.
- `ctcompile_escape_analysis_arrays`: **1/1 PASS, 2.19 s test / 2.20 s total**.
- Exact `Analysis/Escape/escape-claims/quotient-index-overwrite.test`:
  **1/1 PASS, 0.11 s**.
- Existing `normal`, `body-return-ordered`, `body-return-branch-number`, plus
  original body throw and four existing refusal controls: **48 native executions,
  82 refusals, zero nonexecuted admissions, 24 Node/VM observations PASS**.
- Five existing throw sources: **20 exact completion/effect observations per
  engine PASS** in Node and VM. These are oracle checks, not native throw runs.
- Complete `tools/format.sh --check`: **1126 C++, 157 Python, 114 web files PASS**,
  including the final repeat; the final diagnostic line also passed scoped format.

All seven final changed code/test hashes match the devbox. All 24 generated C++
files contain no Script/VM protocol names; the source harness passed its linked
symbol checks. All 233 prior iterator bodies and 86 positive metadata rows remain
byte-identical. Local checks also passed 25 escape source syntax checks, nine
exact outputs, 8,664 divisor-lattice enclosure points, historical preservation,
Python syntax, shell syntax and `git diff --check`.

The first build found three accessor errors in the new code; changing `.` to
`->` fixed them. The first host gate failed only two new positive assertions:
the attempted invocation body contained the done guard, but its verifier requires
exactly one call followed by its exit. Moving the guard around the invocation
fixed the shape; the verifier is unchanged. The next host gate passed. Self-review
then found a missing budget-exhaustion reason in the new precharge path; the final
build, host gate and exact source preflight passed after that diagnostic fix.
The selected source executions preceded this final budget-only diagnostic change.
Escape gates passed on their first completed run. No full suites were run.

Build targets: `ctjs-opt`, `ctjs-translate`, `ctcompile-test-host-contract`,
`ctcompile-test-native-reference`, `ctcompile-test-escape-analysis-arrays`,
`ctcompile-test-escape-claims`, `ctcompile-test-type-oracle`. All builds used
`tools/remote-build.sh` on the devbox under `/tmp/ctbrowser-devbox-build.lock`;
CTest names and the generated lit configuration were selected exactly.
Commands, logs, baselines, oracle scripts, hashes and generated C++ are in
`../../../../test-results/2026-09-22-iterator-abrupt-protocol/`.

Full CTest/compiler lit, the complete custom-iterator fixture, broad corpus/native
matrices, full Bootstrap, WPT/test262, browser suites, Windows, sanitizers and
local C++ builds were skipped. Nothing was pushed or rewritten. Actual Linux
executable and Windows CIM checks confirmed Claude stopped: initially 85/354
processes, before documentation 82/350, no matches/errors. No browser or shared
implementation file changed.

## Exact next boundary

Unchanged `body-throw`, SHA-256
`f1b3f6b87564e88d0a47ab525f0e5f3d187819285b6d62645c145d309f1a7551`,
now passes custom protocol and completion normalization. Both native policies
return status 1: **native DOM source: DOM helper requires complete structured
branches**. No native execution of this source is claimed.

Next prove the exact non-returning SCF region in `DOMSource/Bodies.cpp`, including
shadow-frame state on its reachable sibling. Expand the original return method
inside the protected call without losing suppression or leaving a private holder
in generated code. `DOMEntry` then needs complete typed host effects and the saved
primitive payload; the existing C++ throw emission can consume that proof while
retaining its current verifier. Keep the complete prefix/global/reentry proof
before trusting the moved initial lookup. Do not treat inactive padding as a
reachable return or simply whitelist the wrapper through these analyses.

Mutable state across close exceptions, multiple protected regions, implicit
exception cleanup, nested custom iterators, unguarded Bootstrap defaults, the
application driver and full native Bootstrap remain unfinished.
