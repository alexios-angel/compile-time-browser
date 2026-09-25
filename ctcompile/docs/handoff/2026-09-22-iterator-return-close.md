# Iterator return close and converted shift gaps, 2026-09-22

Resumed clean **9e7ba1f9** and the exact `body-return` source `9bd2d5ba` from
HANDOFF/current00 and iteration 69's journal. Earlier interrupted work was
committed; `codex-wip-20260907` was not unmerged. Other branches were preserved.
Iteration 70 was interrupted with six drafts. The continuation retained those
drafts and saved evidence; the parent finished browser and raw tests after agents
hit rate limits. The independent escape draft was already frozen.

## Landed

**380ee61d** fixes the source compiler prerequisite: explicit returns from
synchronous `for-of` now close exited iterator scopes. The return expression is
evaluated first. Existing loop/finally scopes determine close order and which
catch handlers remain active. A finally may override a return with continue
before the enclosing iterator closes. Nested close sequences retain the first
error, including a later throwing return getter. Derived constructor validation
runs after cleanup. Break/continue arrivals also remove exited finally handlers,
preventing a return in finally from catching its own close error.

This intentionally fixes VM behavior against Node; it is not an API extraction
or a change to make the oracle match native. The previous VM disagreed on **15
of 17** initial focused witnesses. All 17 now agree, and two additional Node
witnesses pin the break/continue-finally interaction. General body throws,
return-expression throws and throwing cleanup crossing an outer iterator through
finally still require broader body-handler work. No full conformance gain is
claimed.

**1d5b4e55** admits an acyclic custom traversal with one `next` call and closes
in separate completion arms. Every close must follow the complete traversal,
and every continuation path must reach a close. Missing-arm closes, early close,
multiple next sites and suppressed-throw close flags refuse before rewriting.
The existing cyclic traversal stop proof remains. Completion normalization must
leave an acyclic next at the entry root. No runtime protocol or VM dependency is
emitted.

The exact saved source remains SHA-256
`9bd2d5baa11a458f4801bd5e103efcb445085b378c8743776a0ff1dd445c37c2`.
It returns false and closes once when its body runs; exhausted iteration does
not close. The ordered witness `6b79ba7a` writes visited, reads the still-absent
closed attribute for its return value, then closes. All **210** historical
source bodies, **73** positive metadata rows and historical raw test text remain
unchanged. The historical nonprogressing source remains compile-only.

Parallel **fdbae846** preserves the existing mixed-shift refinement flag through
the shared converted-lattice helper and all three callers. Composed complement,
right-shift and left-shift conversion jumps can reuse bounded whole-key gap
refinement. Complete reload/store census, actual-write replay and budgets are
unchanged. Paired CFG/SCF checks retain unwritten children, prior snapshots and
count-overwrite refusals. All **120** historical shift source bodies and raw
constructions remain unchanged. Saved Node evidence covers 128 functions and
eight exact new outputs; no completed evidence was replayed on continuation.

## Focused validation

- Exact `ctcompile_escape_analysis_arrays`: **1/1 PASS, 2.04 s test / 2.06 s total**.
- Exact right-shift index-overwrite lit case: **1/1 PASS, 0.14 s**.
- Exact `ctcompile_host_contract`: **1/1 PASS, 2.11 s test / 2.12 s total**.
- Final selected browser CTests `language_syntax`, `vm_async`, `vm_control_flow`
  and `vm_functions`: **4/4 PASS, 0.11 s total**.
- Four-source native subset: **64 executions, 368 refusals, two nonexecuted
  admissions and 32 Node/VM observations**. Sources are the two new return
  witnesses plus `normal` and `effect-only-break`, with all 136 refusal sources.
- Complete `tools/format.sh --check`: **1124 C++, 157 Python and 114 web files PASS**.

All **13** final code/test hashes match the devbox. All **32** generated C++ files
contain no Script symbols; ordered-return output was inspected and preserves the
return read before the public DOM close effect using ordinary owning values.
Read-only review found the stale finally handler and confirmed its fix; no
remaining blocker was reported. The first host build found only duplicate names
in the new raw fixture; those locals were renamed. The initial formatter found
an unfinished Python line wrap, then passed after Black. The initial exact
browser test passed before review added the two finally regressions; the final
four-test selection includes them.

Explicit devbox targets were `ctjs-opt`, `ctjs-translate`, `ctcompile-tool`,
`ctcompile-test-native-reference`, `ctcompile-test-host-contract`,
`ctcompile-test-escape-analysis-arrays`, `ctcompile-test-escape-claims`,
`ctcompile-test-type-oracle` and the four named browser test targets. Tests used
anchored exact CTest names and the generated build-tree lit configuration.
Builds/tests and Git writes used their shared locks. Commands, logs, hashes,
Node evidence and generated sources are in
`../../../../test-results/2026-09-22-iterator-return-close/`.

Full CTest/compiler lit, complete custom execution, broad corpus/native matrices,
full Bootstrap, WPT/test262, Windows, sanitizers and local C++ builds were skipped.
Nothing was pushed. Initial unprivileged Linux inspection was incomplete; WSL
root executable inspection resolved it. Before browser landing, Linux found 83
readable identities with no errors or Claude match; Windows CIM found 346
identities with no Claude executable, Node CLI or loop. Browser edits used exact
claims and repeated availability checks. The idle timer remains active/enabled.

## Exact next boundary

`body-return-branch`, SHA-256
`dc6fd4c5e43c2278d45da074c925eee50d27b5d3a8cc92c06fb7a1cf43af0e20`,
returns conditionally on `stop`. Its source compiler now emits both close arms
after the loop. The completion switch has effects: normal exhaustion closes
before the final visited read; return reads closed before closing. Both native
policies refuse **DOM helper completion selector is not an exact constant**.
Extend the bounded loop-completion proof to preserve these effectful arms and
their saved values; do not move the return read across close.
Start at `DOMSource/Completion.cpp`'s `exitProjection`, which currently accepts
only yield-only dispatch arms after a loop.

Node and the fixed VM agree on four states (**eight observations**): true on
normal completion without close, false on stop with exactly one close, and false
when already exhausted without close. Both diagnostics and source/IR are saved;
no native execution of this boundary is claimed. Nested custom opens, broader
abrupt close, unguarded Bootstrap defaults, literal range-for printing, the
application driver and full native Bootstrap remain unfinished. Difficult shift
subranges can still exhaust the shared proof budget.
