# Constructor helper publication and intrinsic parameters, 2026-09-21 UTC

Continued clean `ef37639a` and resumed the same drafts across process/service
interruptions. No unmerged `codex-wip-20260907` or dirty predecessor work was
present at startup. The process audit inspected 69 Linux executable/cmdline
identities and 358 Windows CIM process records, with no Claude executable, CLI
or loop found. Only ctcompile code/tests/docs and the external master plan changed.

## Landed

- `7b2f93f8` normalizes one pure registration helper used exclusively by one
  leaf constructor, then reuses the existing terminal-publication, local Map
  and completed stack-owner proofs. The unchanged file-30 helper now compiles.
  Raw closure/cell aliases and both symbol-reference locations are checked
  before retirement. Function declaration hoisting can capture a Map cell before
  initialization; a deferred obligation is discharged only after the exact
  helper proof establishes initialization before constructor creation. All
  remaining early captures retain their refusal. The enclosing disposable
  candidate prevents publication of a partial rewrite. Existing direct-publication
  logic moved from Sources.cpp into Publication.cpp without changing its guards.
- `a35e18ef` accepts optional ordered `parameter_types` on the strict
  `ctbrowser-intrinsics-v1` contract: `boolean`, `number`, `string`, `symbol`.
  The complete fingerprint-bound body proof supplies exact argument categories
  to inference. Existing native value types preserve input Symbol identities,
  owning Strings, NaN and signed zero; no runtime carrier or browser API changed.
  Missing/extra categories, unknown categories and unsupported uses remain
  diagnostics. Helpers and nullable/union/object inputs remain separate work.

Independent review identified indirect helper-cell aliases hidden by sourceUses
and missing allocation/cleanup budget charges. Both were fixed, with an alias
refusal and exact budget checks. Tests and intrinsic implementation ran as
independent agent tasks; the root completed integration after service limits.

## Focused validation

All builds and executions ran on the devbox under the shared build lock.
Explicit targets across the corrected builds were `ctjs-opt`, `ctjs-translate`,
`ctcompile-test-host-contract`, `ctcompile-test-native-runtime` and
`ctcompile-test-native-reference`. Final explicit builds passed; no local build
was run. The initial build stopped at the new host-contract unit test's two
unqualified C++ names. Correcting their namespaces fixed compilation.

Exact CTests `ctcompile_host_contract` and `ctcompile_native_runtime` passed
**2/2 in 0.57 s**. Selected lit cases used the generated build-tree configuration:

| Case under CTNative/ | Result |
| --- | --- |
| Lowering/Objects/class-terminal-publication.test | Passed in corrected two-case selection, 90.47 s. |
| Browser/native-dom-symbols.test | Passed in initial three-case selection, 64.19 s. |
| Exports/native-intrinsic-symbols.test | Final **1/1 in 91.74 s**. |

Class measurements: **33 source observations, 96 main native executions,
66 unprepared refusals and 41 preparation refusals**. Three newly admitted
helper sources add **24 native executions** across GCC/Clang, explicit/deduced
and runtime/optimized modes. Two new positives cover overwrite/delete and saved
owner identity; ten new controls cover effects, observers, returned receivers,
shared callers, escaping Maps, post-publication work/throws, recursion and
indirect helper aliases. The original helper body and all of file 30 remain
unchanged. Exact first-complete work budgets are 611 for the existing direct
case and 646 for the original helper. Existing auxiliary controls add 16
constructed-method and eight original-r native executions. Concrete stack-record
pointer and Map-identity checks remain enabled.

Intrinsic exports: **120 native executions, 107 refusals and two mutations**.
Eighteen Node/VM Boolean observations cover identity selection, zero/odd/even
loops, same/distinct Symbols, fresh construction, owning NUL/surrogate Strings,
absent/empty descriptions, signed zero, NaN and infinities. Six new positive
entries add 48 native executions, including the unchanged former unused-parameter
refusal with an explicit Symbol contract. C++ static assertions pin all four
exact parameter/return carriers. Schema, arity/order, budget and unsupported-use
controls retain their diagnostics. The adjacent DOM Symbol fixture passes its
existing 40 executions, 42 refusals and one mutation.

Initial selected lit was **1/3 in 64.19 s**. The helper failed at the earlier
hoisted-capture boundary, now covered by the deferred proof above. The intrinsic
fixture first needed `ctnative::js_num` qualification to avoid its historical
raw global compatibility alias. The next two-case selection was **1/2 in
90.47 s**: class passed; intrinsic test inputs still supplied int/float values
to a wrapper deliberately accepting exact double inputs. Tests now use double
literals, `js_nan_t` and double infinity. No source or Number runtime behavior
was changed to satisfy these fixture errors.

The initial local shell heredoc let ssh consume the following test commands;
those logs measured builds only. Later gates use a script argument and the
separate test log above is authoritative. No test pass is inferred from a build.

All **17 final code/test SHA-256 hashes** match the devbox. All **ten changed
C++ files and four Python files** pass scoped formatting; Python syntax and
whitespace checks pass. Required `tools/format.sh --check` retains **16 existing
diagnostics** in untouched `ctbrowser/tools/ctdrive/ctdrive.cpp`,
`HostContract/ProviderPaths.h`, `PartialEvaluation/Heap.h` and `Symbolic/Facts.cpp`.
It stops before repository-wide Python/web formatting.

Skipped: full CTest/compiler lit, broad corpus/native matrices, full Bootstrap,
WPT/test262, Windows, new sanitizer runs and push. This is focused validation.
Transient evidence: `/tmp/ctcompile-helper-parameters-gate*.log`, `-tests.log`,
`-format-frozen.log`, `-hashes.log` and `.sha256`.

## Exact next boundaries

Original Bootstrap B/Data+B is still refused. The unchanged file-30
`class-map-record-constructor-inherited.js` publishes in a base constructor
before a child field write. Prove partial initialization, reentry, exceptions
and owner lifetime before admitting that form. The original Data holder also
needs captured outer element and nested DATA_KEY Map origins, conflict checks,
nullable gets, deletion/empty cleanup and enclosing owners. Shared or effectful
registration helpers keep their separate caller/observer obligations. Preserve
`e.set`, `e.remove`, `P.off`, configuration and disposal bodies. No full-Bootstrap
admission or coverage gain is claimed.

For intrinsic exports, the next slice is exact helper calls with complete
argument/return and effect proofs. Nullable/union/object entry inputs,
description narrowing, registry operations, symbol-keyed fields and hooks remain
separate. Deeper sibling relays, String ordering, collections/document views
and the application driver remain unfinished.
