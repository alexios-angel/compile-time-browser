# Saved primitive iterator throws and subtraction offsets, 2026-09-23

Resumed clean **9729cf46** and unchanged `body-throw`, `f1b3f6b8`, from HANDOFF,
current00 and iteration 83's final journal. No abandoned drafts or unmerged
`codex-wip-20260907` remained; unrelated branches were preserved. Parallel escape,
source-support and read-only review agents contributed. Parent retained and
finished their drafts through rate limits and user continuations.

## Landed

**87abc570** proves an exact nonreturning `scf.execute_region`: `no_inline`, no
operands/results/block arguments, and one saved `ctjs.throw`. It must sit beneath
acyclic conditional ancestry, outside loops and invocations. Its owning Number,
Boolean or String payload must precede and dominate it. Complete source-order,
effect, frame and budget checks remain; incomplete proofs publish no evidence.
Callbacks and unproved source throws remain refusals.

Throw-only branches do not contribute frame exits or values to a reachable
sibling. Complete DOM proof records primitive padding for unreachable structural
yields. Preparation applies it privately before fresh inference, retaining every
source producer and effect. This prevents an unreachable `undefined` from
widening the normal Boolean return to a nullable scalar. Only after full proof
does preparation remove the stale `ctjs.not_structured` annotation left by the
original handler CFG. Final typed DOM and entry-contract reproof still run.

Operation admission and emission require the saved proof. The emitter uses the
existing `CppThrowOp`, immediately followed by its enclosing terminator, in a
literal-true EmitC scope. The printer emits that exact single-throw scope as a
plain C++ block. The `CppThrowOp` payload and terminator verifier is unchanged.
No new runtime, exception representation or catch-all is introduced; generated
code calls the public DOM/Core libraries using ordinary ownership.

The original Number throw and historical Boolean snapshot now execute with
unchanged source bytes. New literal Boolean and String variants also execute.
Checks preserve DOM write order, saved pre-close payload, exactly one close,
repeated calls, separate documents, owning-session calls and foreign-session
rejection. The Boolean snapshot returns true on natural exhaustion and does not
close an already exhausted iterator. All 233 historical iterator source bodies
and 86 positive metadata rows remain unchanged; only the two newly executed
historical sources leave the refusal loop.

**430d9620** admits bounded primitive invariant subtraction offsets through the
existing primitive conversion and bounded difference transfer. Operand order,
endpoint bounds, complete reload/store census, actual-write replay and budgets
remain unchanged. Addition keeps the Number-only invariant rule because String
addition concatenates. CFG/SCF controls cover Strings, Booleans, null, negative
offsets, saved children, String reloads in descending gaps, and later/overlapping
stores. Object conversion and noncanonical values remain conservative. The
unchanged reversed-index `stringOffset` source now admits. All 34 historical
source bodies survive; twelve controls bring the fixture total to 46.

## Focused validation

- `ctcompile_host_contract`: **1/1 PASS, 2.32 s test / 2.33 s total**.
- `ctcompile_escape_analysis_arrays`: **1/1 PASS, 2.23 s test / 2.24 s total**.
- Exact `Analysis/Escape/escape-claims/offset-index-overwrite.test` and
  `reversed-index-overwrite.test`: **2/2 PASS, 0.11 s total**.
- Exact `Target/Cpp/exceptions.mlir`: **1/1 PASS, 1.45 s**, including its
  explicit/deduced/hoisted compile-and-run checks.
- Four saved primitive throw sources: **64 native executions, 12 refusals and
  16 Node/VM observations PASS**. Both providers, both optimization policies,
  explicit/deduced C++, GCC/Clang. This final four-source run supersedes the
  earlier three-source 48-execution run; those repeated executions are not added.
- Protected raw attributes: **32 native executions and 20 refusals PASS**.
- Existing `normal`, `body-return-ordered`, `body-return-branch-number`, plus
  selected existing refusal controls: **48 native executions, 80 refusals,
  zero nonexecuted source admissions, 24 Node/VM observations PASS**.
- Five existing throw source oracles: **20 exact completion/effect observations
  per engine PASS** in Node and VM.
- Complete `tools/format.sh --check`: **1126 C++, 157 Python, 114 web files PASS**.
  Final scoped pinned clang-format and Black checks pass after the final fixture
  edit; `git diff --check` passes.
- Local escape evidence: **46 source syntax checks, 34 historical bodies
  preserved, 13 exact Node outputs and four raw array/read witnesses PASS**.
  Iterator preservation retains all **233 bodies and 86 metadata rows**.

All seventeen final code/test hashes match the devbox. All 72 final generated
C++ files contain no Script/VM protocol or nullable-scalar fallback; compiled
harnesses pass linked-symbol checks. The initial host run exposed a new raw
fixture typo in the Number attribute's IEEE-bit syntax; correcting the fixture
made it pass. Original-source lowering then exposed the stale structured-CFG
annotation, fixed only after complete private proof. The first generated-source
check rejected a nullable-scalar return caused by unreachable `undefined`;
proof-recorded primitive yield padding fixed it without allowing a fallback.
The final production gate passed. A focused nine-source throw census then found
the historical Boolean snapshot newly admitted. The final test-only follow-up
executed that unchanged source alongside the other three; no new admission was
left unexecuted. Production host/escape/lit checks were not repeated afterward.

Explicit build targets included `ctjs-opt`, `ctjs-translate`,
`ctcompile-test-host-contract`, `ctcompile-test-native-reference`,
`ctcompile-test-escape-analysis-arrays` and `ctcompile-test-escape-claims`.
Builds and native/VM execution used the devbox and
`/tmp/ctbrowser-devbox-build.lock`. Commands, logs, frozen baselines, hashes,
generated C++, exact refusal IR and oracle evidence are preserved in
`../../../../test-results/2026-09-23-saved-primitive-throws/` under `native84/`,
`tests84/` and `escape84/`. `native84/generated/` holds final C++; the separate
`saved-first.cpp` records the rejected intermediate nullable-scalar output.

Full CTest/compiler lit, the complete custom-iterator fixture, broad corpus/native
matrices, full Bootstrap, WPT/test262, browser suites, Windows, sanitizers and
local C++ builds were skipped. Nothing was pushed or rewritten. WSL-root process
identity and Windows CIM checks confirmed Claude stopped: initially 81/352
processes and before docs 82/353, no matches or errors. No browser/shared
implementation changed. The external plan has no Git repository; its
current-work journal was updated alongside this committed handoff.

## Exact next boundary

The original `body-throw`, SHA-256
`f1b3f6b87564e88d0a47ab525f0e5f3d187819285b6d62645c145d309f1a7551`,
and historical `body-throw-boolean-snapshot`,
`92e23bbe920aab8070d2977b3ce8c8e5439a26cfc8b8118a82de57efcd5000c9`,
admit in both policies and execute as recorded above. Seven related historical
throw sources remain refused in both policies:

| Source | SHA-256 prefix | Exact native DOM source refusal |
| --- | --- | --- |
| `body-throw-branch-boolean-snapshot` | `9c10be3d` | DOM helper completion observes an inactive value |
| `body-throw-or-return-snapshot` | `43ab2b64` | DOM helper completion observes an inactive value |
| `body-throw-number-snapshot` | `7c0874c9` | DOM iterator protected close needs a mutable-state proof |
| `body-throw-branch-number-snapshot` | `8ee2040d` | DOM iterator protected close needs a mutable-state proof |
| `body-throw-close-throws` | `fa88f9d6` | DOM iterator method must return one fresh own-field record |
| `body-throw-close-primitive` | `5c738524` | DOM iterator method must return one fresh own-field record |
| `body-throw-close-getter` | `68ea7208` | DOM helper has no complete return or yield |

Start with retained conditional Boolean snapshot `9c10be3d`, whose full hash is
`9c10be3d2195cc8f67076aa662dc494ef69547ddae351f7d84016e0cf35f88e8`.
Trace the inactive carried value through `HostContract/DOMSource/Completion.cpp`;
prove its completion without losing the saved pre-close payload. Mutable Number
snapshots next meet the explicit protected-close/state guard in
`HostContract/DOMCustomIteration.cpp`. Throwing or primitive close methods need a
separate proof of suppressed completion, retaining saved payload and source
effects; weakening the fresh-record contract would be wrong. The full hashes,
both-policy diagnostics and intermediate IR are in `native84/boundaries/` and
`native84/boundaries.log`.

Multiple protected regions, implicit cleanup, nested custom iterators, unguarded
Bootstrap defaults, the application driver and full native Bootstrap remain
unfinished.
