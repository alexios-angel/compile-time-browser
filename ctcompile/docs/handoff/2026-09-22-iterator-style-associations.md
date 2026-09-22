# Iterator Style associations and direct conversion gaps, 2026-09-22

Resumed clean **5e921715** and exact source `7ea457e6` from HANDOFF/current00
and iteration 73's final journal. Earlier drafts were committed;
`codex-wip-20260907` was not unmerged. Other branches were preserved. Native
regressions, independent proof review and Part 25 work ran in parallel.

## Landed

**71a8654b** proves each selector receiver has one original document/Style
input. A charged graph walk follows both branch yields, loop initialization and
backedges, condition/result correspondence, approved selector results, indexed
snapshots and document roots. Cycles supply no authority; a real original input
must be reached, with no second input. The complete kind, effect, observer and
lifetime proof remains authoritative. Evidence is published only on full success.

The existing `HostDOMCall` carries that proved input. Signature selection requests
its Style engine, and emission uses it with the live selected element. No runtime
completion/callable representation or new element carrier is added. Raw inputs
may own different documents; both providers conservatively refuse mixed roots.

The exact saved selector `7ea457e6`, before-close selector `bf4670dc` and explicit
prototype call `655ad265` execute unchanged. Tests check normal completion,
stop, already-exhausted state, detached elements, foreign documents and session
ownership. An incompatible Style engine rejects before any DOM write. Raw tests
cover eight positive and seven hostile constructions under both providers,
including selector descendants, snapshots, document roots and incomplete budgets.
All 219 historical sources, 83 positive metadata rows and 282,653 bytes of raw
construction history are preserved.

**10a53c8b** enables the existing whole-key gap refinement for direct
ToInt32/ToUint32 conversion lattices. Singleton division retains exact endpoint
integrality without requiring its artificial stride to divide. Non-singletons
still require stride divisibility; resulting strides stay positive. Nine CFG,
nine SCF and nine source witnesses cover complement, signed/unsigned right shifts,
left shifts, retained/saved children, actual overlaps, later stores and fractional
singleton refusal. All 141 historical shift sources and checks remain unchanged.

## Focused validation

- Exact `ctcompile_host_contract`: **1/1 PASS, 2.29 s test / 2.30 s total**.
- Exact `ctcompile_escape_analysis_arrays`: **1/1 PASS, 2.16 s test / 2.17 s total**.
- Exact `Analysis/Escape/escape-claims/right-shift-index-overwrite.test` and
  `quotient-index-overwrite.test`: **2/2 PASS, 0.14 s**.
- Four selected custom sources: the three new selectors plus the original saved
  element expression, with existing refusal controls: **64 native executions, 374 refusals, zero nonexecuted admissions
  and 32 Node/VM observations PASS**.
  The unchanged compile-only case and other positive executions were omitted.
- Exact `CTNative/Browser/native-dom-query-all.test`: **1/1 PASS, 46.65 s**.
- Complete `tools/format.sh --check`: **1124 C++, 157 Python, 114 web files PASS**.

All nine final code/test hashes match the devbox. All 32 generated custom-iterator
C++ files have no Script/VM protocol; the standalone harness also checks linked
symbols. Inspection confirms the saved selector read precedes the close write.
The next-boundary probe adds eight agreeing Node/VM observations.

The first arrays run failed ten assertions, all in the new left-shift witness;
its first right-shift lit run failed the same witness. Refinement reached a
one-point range with stride one, which the shared division precondition rejected
as indivisible. The fix preserves that original witness and adds a fractional
singleton refusal. No historical expectation changed. The initial arrays total
was 2.17 s; initial right-shift lit was 0.14 s. The initially queued remainder
lit never ran because the arrays command failed first.

Additional local evidence: 223 source syntax checks (including the next candidate),
15 exact Node observations, an ordering mutation and the integrated 12-state Node
selector oracle passed. Escape checks parsed/executed 150 functions and checked
nine exact outputs and four index traces. These supplement the devbox gates.

Build targets: `ctjs-opt`, `ctjs-translate`, `ctcompile-tool`,
`ctcompile-test-native-reference`, `ctcompile-test-host-contract`,
`ctcompile-test-escape-analysis-arrays`, `ctcompile-test-escape-claims` and
`ctcompile-test-type-oracle`. All C++ builds/tests ran on the devbox under
`/tmp/ctbrowser-devbox-build.lock`. Exact commands, logs, source hashes and emitted
C++ are in `../../../../test-results/2026-09-22-iterator-style-associations/`.

Full CTest/compiler lit, complete custom-iterator execution, broad corpus/native
matrices, full Bootstrap, WPT/test262, browser suites, Windows, sanitizers and
local C++ builds were skipped. Nothing was pushed. Initial process checks:
WSL-root Linux 81 readable identities, zero errors/matches; Windows CIM 347
processes, no Claude executable/CLI/loop matches. Claude was confirmed stopped.
The final pre-documentation check also found no matches: Linux 87 readable
identities with zero errors, Windows 352 processes. No browser/shared
implementation or runtime-oracle edits occurred.

## Exact next boundary

`body-return-branch-element-selector-mixed-roots`, SHA-256
`987dfd90146592c708744be7aa9d6ac738ff86494d6174da47776af99cda2858`,
is preserved in the artifact directory's `next-boundary/` directory. The source
selects `advance ? node : other` before the saved selector return. Both native
policies refuse **DOM selector requires one original Style association**.
Eight Node/VM observations agree: stop selects the button input when advancing,
the span input otherwise, preserving which element is visited and the anchor's
close result. Normal completion and zero-trip exhaustion perform no selector.
No native execution of this boundary is claimed.

Carry the Style association with the selected element, or prove a stronger
association without assuming raw input documents share an engine. Keep the
return read before close. Broader completion shapes, body/return-expression
throws, crossing-finally cleanup, nested custom opens, unguarded Bootstrap
defaults, literal range-for printing, the application driver and full native
Bootstrap remain unfinished. Difficult index subranges remain budget-limited.
