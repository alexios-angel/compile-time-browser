# Confined iterator receiver state and signed OR ranges, 2026-09-22

Continued clean `0bbb961f` and the saved receiver-counter boundary from HANDOFF,
plan 00 and the iteration 47 journal. No predecessor edits were uncommitted;
`codex-wip-20260907` was absent. Recent compiler and browser commits were read.
Linux process identities and Windows CIM showed no Claude executable, CLI or
loop. No browser/shared implementation or runtime-oracle semantics changed.

Root implemented native state transport. Three agents handled source regressions,
raw proof controls/review, and the independent escape improvement. All builds and
tests ran on the devbox under the shared lock.

## Changes

**dfb27eae** admits literal Number fields on a confined root-local iterator.
The complete census proves unique initializers, constant ordinary keys, direct
root-block receiver accesses, ordinary method receiver binding and unique
unobserved method identities. All method preflight checks precede rewriting.
Arrow functions cannot substitute holder state for lexical `this`.

Receiver reads become explicit scalar formals; writes update the current SSA
value. Extra fields on the private result record transport updated values back
to the caller. Existing helper expansion projects away those records. Structured
branches and loops carry all state alongside done. Each allocation resets its
state; `return` receives the latest values from `next`, and exhaustion skips
close. Complete DOM proof retains Number types, effects, ownership and budgets.
Number greater-than uses the existing numeric lowering after both operands are
proved Number. No new runtime carrier or platform implementation was introduced.

The saved receiver-counter source is byte-identical. Its repeated native calls
verify fresh state despite an already-marked DOM element. A second source has two
ordered updates and checks the latest close value with a Boolean equality. Raw
controls separately check both scalar loop results, absence of boxed state,
twelve invalid receiver/holder shapes for both providers, a non-scalar recurrence,
and incomplete budgets. Earlier source bodies and close/order/owner checks remain.
The exact captured-counter source is now retained as a refusal, along with arrow,
dynamic-key, conditional-store and escape controls.

**8649d10d** extends OR-mask escape precision across zero and signed conversion
boundaries when the mask fixes the output sign. All unfixed input bits may vary
within one conservative negative interval; existing low-bit residues still prove
gaps. Same-band bounds stay tight. CFG/SCF tests preserve XOR, overlap and later
store refusals. Eleven source witnesses extend the unchanged original 58.

## Focused validation

| Check | Measurement |
| --- | --- |
| Exact escape arrays CTest | 1/1 PASS, **1.57 s test / 1.59 s total** |
| Exact host-contract CTest | 1/1 PASS, **0.76 s test / 0.77 s total** |
| OR/XOR and AND overwrite lit | 2/2 PASS, **0.12 s total** |
| Custom iteration lit | PASS, **246.52 s**; **144 native executions, 290 refusals, 54 Node/VM observations** |
| Nested iteration lit | PASS, **158.77 s**; **48 native executions, two previous-source checks, 94 refusals** |
| Dataset lit | PASS, **79.10 s**; **112 Node/VM observations, eight GCC/Clang binaries, lifetime sanitizer, 432 refusals** |

The three native lit cases passed together in **246.53 s**. The four final native
source hashes match their devbox gate; the four unchanged escape hashes match the
earlier escape gate. These are two selected CTests and five selected lit cases.
The devbox idle timer was restored and verified active/enabled.

Artifacts beside the monorepo at
`../test-results/2026-09-22-iterator-receiver-state/` contain logs, lit JSON,
source hashes, runners and explicit/deduced C++ examples. The original boundary
sources/IR remain in `../test-results/2026-09-22-multiple-break-exits/state-probes/`;
both saved programs are now also represented byte-identically in the source test.

Build entry point, always under `/tmp/ctbrowser-devbox-build.lock`:

```bash
tools/remote-build.sh ctjs-opt ctjs-translate ctcompile-tool \
  ctcompile-test-native-reference ctcompile-test-host-contract \
  ctcompile-test-escape-analysis-arrays ctcompile-test-escape-claims \
  ctcompile-test-type-oracle
```

Later native builds selected only the first five targets. Exact devbox tests,
from the synced repository root under the same lock:

```bash
ctest --test-dir build --output-on-failure --no-tests=error -R '^ctcompile_escape_analysis_arrays$'
ctest --test-dir build --output-on-failure --no-tests=error -R '^ctcompile_host_contract$'
~/.lit-venv/bin/lit -sv build/ctcompile/test --filter='^ctcompile :: Analysis/Escape/escape-claims/(bitor-xor|bitand)-index-overwrite[.]test$'
~/.lit-venv/bin/lit -sv build/ctcompile/test --filter='^ctcompile :: CTNative/Browser/native-dom-(custom-iteration|nested-iteration|dataset)[.]test$'
```

The initial host gate exposed a preflight check that excluded captured DOM
arguments; it now uses the existing capture-aware preflight before the unchanged
capture proof. The next gate reached unsupported Number `>` in complete DOM
analysis; its typed rule now admits that operation. Review also caught lexical
`this` on arrows before state substitution, and tests retain that guard. The
third host run found new close tests assumed Number-to-attribute coercion. Those
new observations use Boolean equality, preserving state/order assertions; the
saved source is unchanged. Production did not change after that third build.
The final host and all three native lit cases pass. Escape gates were not repeated
after their successful commit.

Required `tools/format.sh --check` still reports the same 16 pre-existing
C++ diagnostics in four untouched files. Changed C++ passes pinned clang-format;
changed Python passes Black/AST; source preservation, whitespace and temporary
runner syntax checks pass. No full CTest/compiler lit, broad corpus/native
matrix, WPT/test262, Windows, additional sanitizer, local build or push ran.
The dataset fixture includes its existing lifetime sanitizer. Documentation-only
updates need no build or CTest.

## Next boundary

Start with `refusals()["mutable-captured-counter"]` in
`ctcompile/test/CTNative/Browser/native_dom_custom_iteration.py`. It is the exact
saved source from the previous session and still refuses under both optimization
policies. The entry initializes one local cell; `next` reads and writes it through
`load_upvalue`/`store_upvalue`. Current helper expansion requires immutable
captures. A future state proof must retain the cell's identity, initialization,
all readers/writers, closure binding, effects and lifetime before replacing it
with explicit scalar transport. Do not weaken the generic capture check.

Conditional receiver writes remain a separate source refusal. Body return/throw
close behavior, nested custom opens, literal C++ range-for printing, unguarded
Bootstrap defaults and the application driver also remain open. Full Bootstrap
is not admitted. General Number-to-attribute conversion was not added. Other
OR-mask sign crossings still require a richer range representation; historical
conformance crashes and WPT event regressions remain separate follow-up work.
