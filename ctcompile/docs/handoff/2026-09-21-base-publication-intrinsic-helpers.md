# Base publication and intrinsic helpers, 2026-09-21 UTC

Continued clean `d45c82d4`, resuming its retained file-30 inherited-publication
boundary. No dirty predecessor work or unmerged `codex-wip-20260907` was present.
Windows CIM inspection covered 357 process identities without a Claude match;
Linux executable inspection hit permission failures, so availability remained
uncertain and concurrent-agent restrictions applied. No browser files changed.

## Landed

- `9e0c7f05` admits unchanged `class-map-record-constructor-inherited.js`.
  One unconstructed base can publish into a confined local Map before its sole
  leaf finishes constant own-field writes. The exact base receiver edge remains
  deferred until super normalization and the leaf proof discharge it. Both
  original and copied publication/capture operations retire together; every
  matching Map alias cell is checked. Registration then occurs immediately after
  each completed leaf construction, using existing concrete borrowed record
  pointers and enclosing stack owners. Unconsumed obligations refuse the candidate.
  Emitted registration operations are charged before allocation.
- `990b752f` admits exact uncaptured local helpers in intrinsic exports.
  Original-source preflight excludes object/prototype and initialization
  assumptions, then reuses bounded DOM helper expansion on a private clone.
  Expansion reports its consumed budget; complete typed entry proof and final
  fingerprinted reproof precede publication. Multiple calls may supply different
  primitive kinds. Argument evaluation order, Symbol identity, owning descriptions
  and frontend undefined padding remain observable. A proved undefined return
  uses the existing void export. No runtime type or browser implementation changed.

Independent agents implemented intrinsic helpers and class fixtures while a
third reviewed ownership. Two agents hit service limits; root inspected their
saved work and completed integration. The review identified the need to consume
both base/leaf environments and census all alias cells, including captures hidden
by ordinary source-use traversal. Shared bases, direct base construction, deeper
inheritance, child calls/reads/throws and nonliteral child arithmetic remain refused.

## Focused validation

All compilation and execution ran on the devbox under the shared build lock.
Explicit targets: `ctjs-opt`, `ctjs-translate`, `ctcompile-test-host-contract`,
`ctcompile-test-native-runtime` and `ctcompile-test-native-reference`. Builds
passed: initial 81 steps, six changed steps, then 79 public-header dependencies;
later fixture-only syncs required no compilation. A deprecated LLVM scope-exit
factory warning in the first build was corrected before the six-step rebuild.

Exact CTests `ctcompile_host_contract` and `ctcompile_native_runtime` passed
**2/2 in 0.56 s**. Three distinct selected lit cases passed across corrected runs:

| Case under CTNative/ | Final result |
| --- | --- |
| Browser/native-dom-symbols.test | Passed in initial three-case selection. |
| Exports/native-intrinsic-symbols.test | Passed in corrected two-case selection. |
| Lowering/Objects/class-terminal-publication.test | **1/1 in 115.68 s**. |

The class fixture checks **50 source observations, 128 main native executions,
100 unprepared refusals and 60 preparation refusals**. Four new admissions add
**32 native executions** across GCC/Clang, explicit/deduced and optimized/runtime
modes. The three new positive bodies cover two owners across replacement,
deletion with a saved alias and literal field overwrites; the fourth is the
unchanged original inherited source. Thirteen new preparation refusals cover
observation, exceptions, getters/calls/reentry, direct base construction, siblings,
deeper chains, aliases/escape, explicit returns and arithmetic. A retained String
case prepares but fails native definite-String-field admission in both modes.
Concrete pointer checks remain enabled. Auxiliary existing controls pass 16
constructed-method and eight original-r executions. First complete work budgets:
inherited **1553**, direct **625**, helper **660**. All of file 30 is unchanged.

Intrinsic exports check **152 native executions, 141 refusals and two mutations**,
with **22 Node/VM observations**. Four added entries exercise nested helpers,
branch/loop state, descriptions, multiple primitive argument categories,
left-to-right argument effects and padded undefined returns. C++ signature
assertions include the void export; native artifacts link Core without DOM or
Script. The adjacent DOM fixture retains its 40 executions, 42 refusals and one
mutation.

Corrections and failed selections are part of the record:

- Initial lit **1/3 in 100.65 s**: the new String record hit an existing final
  field-proof boundary, now retained as preparation-only; the helper transcript
  used Number/Boolean equality outside this provider's current admission, so its
  new observations now use supported less-than and truth tests.
- Next lit **0/2 in 111.73 s**: the preparation-only inherited case incorrectly
  counted imported super-guard Error constructions; it now checks the Map and leaf
  owner explicitly. A missing helper argument correctly produced undefined/void;
  that source became a positive, while reading its missing property stays refused.
- Next lit **1/2 in 122.94 s**: intrinsic exports passed. The original inherited
  source used generic MLIR, incompatible with a textual mutation helper; its
  budget/root checks now use the existing super-constructor controls.
- Final class **1/1 in 115.68 s** passed. No C++ proof was relaxed for these
  fixture corrections.

All **13 final code/test SHA-256 hashes** match the devbox. Seven changed C++ files
and four Python files pass scoped formatting; Python syntax, original-source
preservation and whitespace checks pass. Required `tools/format.sh --check`
retains **16 existing diagnostics** in untouched `ctbrowser/tools/ctdrive/ctdrive.cpp`,
`HostContract/ProviderPaths.h`, `PartialEvaluation/Heap.h` and `Symbolic/Facts.cpp`.
It stops before repository-wide Python/web formatting.

Skipped: full CTest/compiler lit, broad corpus/native matrices, full Bootstrap,
WPT/test262, Windows and new sanitizer runs. No local build or push. Evidence:
`/tmp/ctcompile-base-helpers-{build1,gate2,gate3,gate4,gate5}.log`,
`-format-final.log`, `-hashes.log` and `/tmp/ctcompile-base-helpers.sha256`.

## Exact next boundaries

Original Bootstrap B/Data+B still needs complete captured outer/nested Data Map
origins, helper publication, dynamic DATA_KEY/conflict checks, nullable gets,
deletion/empty cleanup, reentry/exception and enclosing-owner lifetime proofs.
Preserve `e.set`, `e.remove`, `P.off`, configuration and disposal bodies.
File 33 retains the immediate narrower controls: arithmetic after base publication,
shared/deeper families and the prepared String-field refusal. No full-Bootstrap
admission or coverage gain is claimed.

For intrinsic exports, global/captured helper identity is still a boundary;
the original top-level helper refusal remains. Number/Boolean equality in this
provider, description String-method narrowing, nullable/union/object inputs,
registry/keyed-field/hook proofs and the recorded VM custom-hook gap remain.
String ordering, document views and the application driver are also unfinished.
