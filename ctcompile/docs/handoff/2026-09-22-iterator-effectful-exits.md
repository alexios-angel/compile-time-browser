# Effectful iterator exits and low-suffix mask gaps, 2026-09-22

Resumed clean **27abbc26**, the saved `body-return-branch` source `dc6fd4c5`,
and iteration 70's HANDOFF/current00/journal. Earlier interrupted work was
committed; `codex-wip-20260907` was not unmerged. Other branches were preserved.
Iteration 71 was interrupted with two drafts. The continuation retained both,
recreated the test/escape/review agents and reused their evidence. Repeated agent
rate limits left the parent to finish the escape change and final raw test repair.

## Landed

**591db50c** extends bounded completion normalization to a single-case,
effectful post-loop switch. Each exact exit tag supplies a Boolean selecting its
case or default arm. The selector's complete use census excludes other observers;
the after region cannot observe it. Other loop results retain the existing poison
checks. Both effectful arms remain after the loop, including return reads,
IteratorClose and frame exits. The existing emitter proves their complete bodies
and retains the original operation census. No proof budget increases or runtime
completion representation are introduced. Empty raw blocks refuse safely.

The exact saved source remains SHA-256
`dc6fd4c5e43c2278d45da074c925eee50d27b5d3a8cc92c06fb7a1cf43af0e20`.
The second admitted source `c1644200` makes close read the visited attribute,
pinning the return-read/close-read/write order. Raw tests keep a live numeric
loop result through a Boolean comparison, check post-loop effect order, and
reject unknown/observed tags, inactive payloads and extra dispatch cases. Two
historical captured/receiver method-effect controls now admit with their
construction expressions unchanged; both conditional and common writes remain.
All **212** historical native sources and **75** positive metadata rows remain
unchanged. The historical nonprogressing program remains compile-only.

**2f8ad550** enables existing bounded subrange refinement when low-suffix AND/OR
rounding loses gaps from its input stride. The full reload/store census, actual
write replay and shared budgets remain. Six appended source regressions cover
AND, OR, an unwritten child, a saved child, a real reload overwrite and a later
count store. All **128** historical source bodies and call tails are unchanged.
No raw escape files needed changes. Node confirms writes **0, 8, 20, 28** and
**3, 11, 23, 31**, plus six exact final outputs; the retained/saved/overlap/later
cases keep their escaping children.

## Focused validation

- Exact `ctcompile_host_contract`: **1/1 PASS, 2.13 s test / 2.14 s total**.
- Four-source native subset: **64 executions, 368 refusals, two nonexecuted
  admissions and 32 Node/VM observations**. Selected sources: `body-return-branch`,
  `body-return-branch-ordered`, `body-return-ordered`, `effect-only-break`, plus
  all 136 current refusal sources.
- Exact `ctcompile_escape_analysis_arrays`: **1/1 PASS, 2.03 s test / 2.04 s total**.
- Exact right-shift index-overwrite lit case: **1/1 PASS, 0.13 s**.
- Complete `tools/format.sh --check`: **1124 C++, 157 Python, 114 web files PASS**.

All **five** final code/test hashes match the devbox. All **32** generated C++
files pass Script/VM-protocol scans. The ordered output was inspected: it uses
ordinary scalar values and public DOM calls, reading the saved return value
before the close read/write. Native source evidence includes **214** syntax
checks and **12** exact Node observations. The next boundary also has **eight**
Node/VM observations and both saved native-policy diagnostics.

The first host gate failed **18 assertions, 2.06 s test / 2.07 s total**:
unsupported Number+Boolean arithmetic in a new fixture, overly strict assumptions
about mutation of the private normalization candidate, and two historical
method-effect fixtures now admitted. The original caller fixture remains intact;
failed private candidates publish no DOM evidence. A later exact host run failed
only **two** new raw assertions, **2.09 s test / 2.11 s total**, because Number
setAttribute arguments are unsupported. Comparing count > 0 preserves the live
count observation through the supported Boolean argument. Production and source
programs did not change during these test repairs. A build invoked from a shell
heredoc consumed its following CTest command through SSH stdin; the missing
CTest output was noticed, then the exact test was run directly. Final build/test
commands use a script file. No missing test was treated as a pass.

Explicit targets were `ctjs-opt`, `ctjs-translate`, `ctcompile-tool`,
`ctcompile-test-native-reference`, `ctcompile-test-host-contract`,
`ctcompile-test-escape-analysis-arrays`, `ctcompile-test-escape-claims` and
`ctcompile-test-type-oracle`. Builds/tests and Git writes used the shared locks.
Commands, logs, source hashes, Node evidence and generated C++ are in
`../../../../test-results/2026-09-22-iterator-effectful-exits/`.

Full CTest/compiler lit, complete custom execution, broad corpus/native matrices,
full Bootstrap, WPT/test262, browser suites, Windows, sanitizers and local C++
builds were skipped. Nothing was pushed. Linux WSL-root inspection found **81**
readable executable identities, no errors or Claude match; Windows CIM found
**346** identities without a Claude executable, CLI or loop. No browser/shared
implementation or runtime-oracle changes were made. The idle timer remains
active/enabled.

## Exact next boundary

`body-return-branch-expression`, SHA-256
`2b611964a4d429490157397bee57f6f9e5692325fdd7d93db54300d514afc19f`,
returns `(node.setAttribute('data-visited', 'yes'),
anchor.hasAttribute('data-closed'))` from a conditional loop exit. Its saved
return payload is inactive on other loop exits. Both native policies refuse
**DOM helper completion observes an inactive value**. The newly constructed
source was retained unchanged as a refusal, not replaced by the simpler admitted
close-read witness. No native execution of this boundary is claimed.

Node and VM agree on normal completion (true, no close), stop (false, one close
writing `true`), stop without advance (same), and already exhausted (false, no
close). Extend `DOMSource/Completion.cpp` to join the selected saved payload while
keeping the effectful continuation after the loop and preserving all external
observers. Additional switch arms, general body/return-expression throws,
crossing-finally cleanup, nested custom opens, unguarded Bootstrap defaults,
literal range-for printing, the application driver and full native Bootstrap
remain unfinished. Difficult index subranges remain bounded by the shared budget.
