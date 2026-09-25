# Branch-dependent callable returns and composed shift gaps, 2026-09-22

Continued clean **4e25be51** and saved source `191c2e50` from HANDOFF/current00
and the iteration 67 journal. Earlier interrupted work was already committed;
`codex-wip-20260907` was not unmerged. Other branches were preserved. A new
interruption left five drafts and their baselines; replacement agents resumed
those files and evidence without discarding work.

## Landed

**802c0d1a** summarizes complete return dependencies as formal argument
positions and fixed helper identities. Every branch arm and loop incoming edge
must reach a proved leaf; unknown leaves or ungrounded cycles invalidate the
summary. Known helper summaries compose through actual arguments. Invocation,
arity, direct-target, receiver, recursion, effect and observer checks still run
before expansion. Stable indices survive capture erasure. Generated source
retains ordinary branches and scalar snapshots; no callable runtime is added.

The exact saved source is SHA-256
`191c2e50f8b59be95494f2a5e58b90db445408502218f847c903937428d3fe98`.
It and the new zero-trip witness return **2729 normally / 3603 on stop**. The
branch/argument/effect witness returns **494861 / 547651**. Omitting its inner
branch changes this to **886698 / 980596**; omitting its effect yields
**118113 / 137615**. Giving the zero-trip witness two trips changes its result
to **15749 / 18279**. All positive sources actually traverse the iterator and
retain `data-closed=false` on stop. Raw ordinary/direct twins keep every existing
scalar/state snapshot assertion and reject unknown/scalar values in either arm,
missing actuals, publication, equality observers, recursive effects and wrong
direct targets. All 191 historical source bodies and 67 positive metadata rows
are unchanged. The nonprogressing historical source remains compile-only.

The saved source initially exhausted the existing 100,000-step budget after its
proof succeeded. Temporary counters measured **48** summary steps and exposed
repeated full SSA-map copies during entry cloning. Each source definition is
cloned once; sharing that mapping while retaining separate mutable state vectors
reduces cloning from **35,343 to 6,340 steps**, a measured saving of **29,003**.
Newly mapped region arguments remain charged. No budget was raised. Independent
read-only reviews checked both summary soundness and mapping scope. All temporary
`DEBUG68` markers were removed before final gates.

Parallel **80bd870b** preserves the existing mixed-shift refinement flag through
remainder and bit-mask enclosures. Complete reload/store census, bounded singleton
transfer and actual-write replay remain unchanged. Signed/unsigned modulo writes
**0, 2, 0**; masks write **0, 0, 5**, leaving the count at index 1 unchanged.
Unwritten children and pre-loop snapshots remain escaping. All 105 historical
right-shift source bodies and checks are unchanged.

## Focused validation

- Three new sources: admitted under both native optimization policies.
- Exact `ctcompile_host_contract`: **1/1 PASS, 2.24 s test / 2.26 s total**.
- Four-source subset: **64 native executions, 354 refusals, two nonexecuted
  source admissions and 32 Node/VM observations**.
- Exact `ctcompile_escape_analysis_arrays`: **1/1 PASS, 2.20 s test / 2.21 s total**.
- Exact right-shift index-overwrite lit case: **1/1 PASS, 0.14 s**.
- Complete formatter: **PASS, 1124 C++, 157 Python, 114 web files**.

The native source subset contains the three new branch-return witnesses and the
prior nested forwarded snapshot witness, plus all 129 refusal sources. The
complete custom execution case was not run. Source-only evidence covers 200
syntax programs, 16 exact Node traces and six mutation observations. Escape
source evidence covers 113 functions, eight exact outputs and the write traces
above. All seven final code/test hashes match the devbox. All 32 generated C++ files
pass Script/VM symbol checks; the branch/snapshot output was inspected and uses
ordinary scalars, branches and the existing public DOM helpers.

The first array gate failed ten new raw fixture parses because the enum spelling
was `bit_and` instead of `bitand` (2.28 s total). Only those two new fixture strings
changed; production and JS sources were unchanged. The final array/lit gate above
passes. Native raw assertions and source metadata passed without weakening.

Explicit devbox targets: `ctjs-opt`, `ctjs-translate`, `ctcompile-tool`,
`ctcompile-test-native-reference`, `ctcompile-test-host-contract`,
`ctcompile-test-escape-analysis-arrays`, `ctcompile-test-escape-claims` and
`ctcompile-test-type-oracle`. Exact CTests used anchored names and
`--output-on-failure --no-tests=error`. Lit used the generated configuration and
filter `^ctcompile :: Analysis/Escape/escape-claims/right-shift-index-overwrite[.]test$`.
All remote builds/tests and Git mutations used the shared locks. Exact scripts,
logs, source evidence, hashes and generated C++ are in
`../../../../test-results/2026-09-22-iterator-branch-callable-returns/`.

Full CTest/compiler lit, complete custom execution, unchanged DOM cases, broad
corpus/native matrices, WPT/test262, Windows, extra sanitizers and local C++ builds
were skipped. Nothing was pushed. No full Bootstrap coverage gain is claimed.
Linux inspection found 71 readable identities and no errors or Claude match;
Windows CIM found 351 identities and no Claude executable, Node CLI or loop.
Claude was confirmed stopped; no browser/shared implementation or runtime-oracle
changes were made. The devbox idle timer is active/enabled.

## Exact next boundary

`entry-captured-sibling-returned-loop-branch-formal-callee`, SHA-256
`ee851de60c75d3a3d9c859f5cd94b9a9295c8a7417e4dfee05ce20a9cb522fd4`, changes:

```js
const keep = (writer, chooser) => chooser(writer);
// On the existing loop backedge:
selected = keep(selected, forward);
```

Node returns **2729 normally / 3603 on stop**, `data-closed=false`.
Both native policies return status 1, `DOM iterator callable branch has an
unproved arm`; both diagnostics are saved. No native boundary execution is
claimed.
Return dependencies through formal/selected callees need their own complete
invocation proof. Nested custom opens, abrupt close, unguarded Bootstrap defaults,
literal range-for printing and the application driver remain unfinished.
Full native Bootstrap is unfinished. Larger composed shift gaps remain limited
by the existing proof budget; inverse transfer is the documented upgrade path.
