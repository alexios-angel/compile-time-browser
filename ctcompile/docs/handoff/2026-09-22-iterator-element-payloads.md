# Saved element payloads and direct remainder gaps, 2026-09-22

Resumed clean **4c132a5b** and `body-return-branch-expression`, `2b611964`, from
HANDOFF/current00 and iteration 72's final journal. Earlier drafts were committed;
`codex-wip-20260907` was not unmerged. Other branches were preserved. Independent
native regression, read-only proof review and Part 25 work ran in parallel.
The parent finished preserved test work after agent rate limits and user continuation.

## Landed

**995bdf39** admits homogeneous element transport through DOM branches and loops.
The existing `ctbrowser::element_ref` retains both owner and node identity;
complete operation/operand checks exclude escaping handles, node reclamation and
script reentry. Final identity evidence is published only after the whole proof.
Other borrowed/callable kinds and mixed scalar/element joins still refuse.

Completion normalization records only its own inactive literal fillers. An
in-memory vector carries this provenance directly to helper expansion on the
same private module; source attributes cannot provide it. After expansion reveals
an element behind a call, a bounded same-slot branch search can substitute an
already dominating value. No producer moves. Earlier helper marker identities
are cleared before final entry normalization, preventing allocator reuse from
turning a retired value into proof authority. Homogeneous kind proof still follows.

The original saved source `2b611964` and two-read snapshot `36ff39c8` execute
unchanged: normal completion true/no close, stop false/one close, and exhausted
iteration false/no close. The identity witness `87dcd371` returns true on stop
while preserving the same element. The previously refused multiple-return body
`879dbdf6` also admits unchanged: either return path saves false before close;
fallthrough without stop/advance returns true without close; exhaustion returns
false without close. All 218 historical bodies, 79 positive metadata rows and
the compile-only case remain unchanged. The final source inventory contains 219
programs. Raw cases use two distinct element inputs, a preceding numeric counter,
exclusive saved payloads, zero-trip exhaustion, eleven hostile variants and the
existing budget sampler. Historical raw construction text is preserved.

Selector calls on carried elements remain diagnosed. The current emitter tracks
Style associations for original parameters and direct selector results, but has
no corresponding branch/loop transport. This includes explicit prototype calls.
Attributes, class operations, containment and equality retain their existing
owner/id semantics; raw DOM inputs can have distinct owners, while the session
provider enforces its existing owner restriction.

**46d716a4** enables existing whole-key refinement for direct non-affine remainder
lattices. Complete reload/store census, actual-write replay and the shared work
budget remain. Seven CFG and seven SCF rows cover signed divisors/dividends,
retained children, saved snapshots, actual overlaps and later writes. Seven new
source witnesses preserve all 61 historical remainder bodies/checks. An unchanged
historical coprime-stride case now admits with exact replay expectations.

## Focused validation

- Exact `ctcompile_host_contract`: **1/1 PASS, 2.29 s test / 2.30 s total**.
- First source subset: `body-return-branch-expression`, its snapshot,
  `body-return-element-identity` and `body-return-loop-local-number` completed
  **64 native executions, 130 successful refusal checks, two nonexecuted
  admissions and 32 Node/VM observations**. It then stopped because the historical
  `body-return-multiple` refusal compiled. Of the 130 checks, 96 were per-source
  controls and 34 were early refusal-corpus checks.
- Corrected multiple-return source alone, plus the final 135 refusal bodies:
  **16 native executions, 294 refusal checks, zero nonexecuted admissions and
  ten Node/VM observations PASS**. It adds the no-stop/no-advance path. The four
  already executed sources, compile-only case and host CTest were not repeated;
  production/raw files had not changed.
- Exact `ctcompile_escape_analysis_arrays`: **1/1 PASS, 2.11 s test / 2.12 s total**.
- Exact `Analysis/Escape/escape-claims/remainder-index-overwrite.test`:
  **1/1 PASS, 0.12 s**.
- Complete `tools/format.sh --check`: **1124 C++, 157 Python, 114 web files PASS**.

Across the two source runs, 80 native executions and 42 Node/VM observations
completed. Their 424 successful refusal checks include 34 repeated corpus checks
(390 distinct configurations). All 15 final code/test hashes match the devbox.
All 40 generated C++ files pass Script/VM-protocol scans; standalone harnesses
also check linked symbols. The saved source's emitted attribute read precedes
the close write. A documentation follow-up corrects only a stale test comment.

Additional local evidence: all 219 sources parse; 20 exact Node states and three
mutation controls distinguish read-after-close and wrong-element behavior.
The remainder work checks 68 Node functions/seven outputs and a model with
5,400 enclosures, 15,819 memberships and 63,720 gap checks. Historical source
bodies and raw strings are unchanged.

Initial transport-only preflight exposed incompatible numeric padding. The
first combined gate failed only four new raw proof assertions (2.27 s total):
private filler evidence was lost across separate normalization objects. The
second gate admitted all three new source preflights but still failed the four
raw assertions (2.25 s total), because rsync preceded the fixture's provenance
handoff update. The final gate includes that frozen update and the reviewed
retired-marker fix. No historical assertion or original source was weakened.
The next-boundary probe initially compared percent-encoded VM output to raw text;
using the existing quote helper fixed the probe, with source and compiler unchanged.

Build targets were `ctjs-opt`, `ctjs-translate`, `ctcompile-tool`,
`ctcompile-test-native-reference`, `ctcompile-test-host-contract`,
`ctcompile-test-escape-analysis-arrays`, `ctcompile-test-escape-claims` and
`ctcompile-test-type-oracle`. All builds/tests ran on the devbox under its lock.
Logs, commands, hashes, Node/model evidence and generated C++ are in
`../../../../test-results/2026-09-22-iterator-element-payloads/`.

Full CTest/compiler lit, complete custom execution, broad corpus/native matrices,
full Bootstrap, WPT/test262, browser suites, Windows, extra sanitizers and local
C++ builds were skipped. Nothing was pushed. Linux executable inspection found
15 readable identities with no matches but 61 permission errors; Windows CIM
found 349 processes with no Claude executable, CLI or loop matches. Claude status
was uncertain, so concurrent-agent restrictions stayed in force. No browser/shared
implementation or runtime-oracle edits occurred.

## Exact next boundary

`body-return-branch-element-selector`, SHA-256
`7ea457e6f6fe81e9b327636e86dff3f76b50b5e6ab0387c0f1cee9515c848977`, is saved in
`next-boundary/body-return-branch-element-selector.js` under the artifact directory.
It is the unchanged `BODY_RETURN_BRANCH_EXPRESSION_SOURCE` with only
`anchor.hasAttribute('data-closed'));` replaced by `node.matches('button'));`.
Both native policies refuse **DOM selector on a carried element needs its Style
association**. Eight Node/VM observations agree on return values, query counts
and write order. No native execution of this boundary is claimed.

Prove which document/Style association accompanies a selected element and carry
it through the same control-flow path, or prove a single original association
before reusing it. Different raw-provider input documents must not share an engine
by assumption. Keep the saved read before close and retain the full observer and
lifetime checks. Broader completion shapes, body/return-expression throws,
crossing-finally cleanup, nested custom opens, unguarded Bootstrap defaults,
literal range-for printing, the application driver and full native Bootstrap
remain unfinished. Difficult index subranges remain proof-budget limited.
