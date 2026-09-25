# Constructor identity keys and multiplication indices, 2026-09-21 UTC

Continued clean `4b10fb96` from the recorded DOM Data ownership boundary. Both
commit histories, unmerged branches and the synchronization journal were read;
`codex-wip-20260907` is already an ancestor. There was no unfinished code to land.
Windows CIM checked 357 processes and found no Claude executable, CLI or loop.
Linux executable symlink reads were denied for 56 processes, so availability
remained **uncertain** and concurrent-agent rules applied. No browser/shared
implementation changed. Parallel agents supplied the String change and two
ownership reviews; service interruptions required retries.

## Landed

`37bfa1f5` preserves exact outer-key identity when moving a proved terminal Data
registration immediately after class construction. The first key may now be a
same-block fresh object or a declared DOM entry input; the second remains a literal
String. Each moved call retains the original SSA key, never a cloned allocation.
The constructor formal must have no direct uses except roots and that registration
argument before its actual can become undefined. Original actual evaluation stays
in place. Captures, property reads, coercions, field stores and observable suffixes
remain refusals. Complete helper expansion, nested Map routing, record ownership,
callee/symbol checks and transactional rollback remain mandatory.

The former `class-map-record-nested-constructor-object-key` refusal executes with
its complete original source unchanged. Three new complete-vendor Data witnesses
execute with fresh objects, aliases and distinct keys. The distinct-key witness
also checks absence at the original key after registering under the other object.
Two DOM constructor witnesses prepare, retaining two records and two child Maps.
**They still refuse native session emission.**

`0a9755ee` admits one multiplication of direct Number literals as a UTF-16
`charAt`/`slice` index. Existing Number emission and UTF-16 clamping handle
fractional results, overflow, underflow and produced NaN. The complete former
`0 * 1e999` refusal now executes unchanged. Dynamic/coercing/nested operands and
non-index uses remain refused; literal-only first-unit/dataset authority is unchanged.

## Focused validation

| Check | Measured result |
| --- | --- |
| Final exact CTest `ctcompile_host_contract` | 1/1; 0.55 s, 0.56 s total |
| Final `CTNative/Browser/native-class-dom-data.test` | PASS; 19.23 s |
| Constructor selector | 19 source observations, 56 main native executions, 38 unprepared and 26 preparation refusals |
| Multiplication String selector | 40 native executions, 768 proof refusals, 112 Node/VM agreements and 13 known differences |

The DOM fixture measures four Node/VM observations, four prepared entries,
30 preparation refusals, eight incomplete-session refusals and 24 native
object-key executions. Both GCC and Clang run explicit/deduced output with native
optimization enabled and disabled. The existing executable check also requires
concrete borrowed record storage and rejects runtime/prototype storage.

The constructor selector includes the complete original B/Data+B refusals and
the existing constructor observer, exception, reentry, escape and inherited
controls. Its first-complete literal-constructor budget is **3811**. Supporting
checks run 16 constructed-method executions/20 refusals and eight original-r
executions/four refusals. The String selector executes three new cases and two
old sentinels, retaining all proof controls; existing executable mutations were
skipped.

All **seven final code/test hashes** match the devbox. Three C++ and three Python
scoped formatting/syntax checks pass. All 51 earlier class source files are
untouched; all 101 previous complete intrinsic cases and 33 general refusals are
unchanged. The agent's 125 Node observations pass. Repository `tools/format.sh
--check` completes with the same 16 pre-existing diagnostics in untouched
`ctdrive.cpp`, `HostContract/ProviderPaths.h`, `PartialEvaluation/Heap.h` and
`Symbolic/Facts.cpp`.

Builds used explicit targets `ctjs-opt`, `ctjs-translate`,
`ctcompile-test-host-contract` and `ctcompile-test-native-reference`, under the
devbox lock. No full CTest/compiler lit, complete captured/export execution
replay, DOM Strings replay, broad corpus/matrix, full Bootstrap, WPT/test262,
Windows, new sanitizers, local native build or push ran.

One selector setup initially used a stale fixture directory and stopped before
compiler validation; regenerating the split source inputs fixed it. An early
combined shell invocation built successfully but consumed its trailing test
commands through stdin; only the later explicitly executed checks above count.
Root corrected the String draft's formatter configuration using repository Black;
its exact Python AST stayed unchanged, so the successful execution results remain
applicable. Final formatted files were synchronized and hash-checked.

Evidence: `/tmp/ctcompile-dom-compose-gate{1,2,3}.log`,
`/tmp/ctcompile-string-mul-gate.log`, `/tmp/ctcompile-constructor-final-format.log`,
`/tmp/ctcompile-constructor-final.sha256`,
`/tmp/ctcompile-constructor-keys-focused.py` and
`/tmp/ctcompile-string-multiplication-focused.py`.

## Exact next boundary

Compose the local class/Map proof with a **real published DOM Data family**.
The current preparation fixture has only a placeholder manifest root. Its
retained constructors still reach `native DOM Data source: unsupported provider
behavior through ctjs.construct`; simply admitting that opcode cannot supply
the missing ownership proof.

Three existing checks must compose without being bypassed:

1. `ClassInitialization/Proof.cpp` currently requires DOM inputs to have only
   root uses after local key retirement. A genuine `host.slot.set(element, value)`
   family therefore needs its own complete proof before class preparation can
   retain it and its wrapper/factory operations.
2. `HostContract/Analysis.cpp` accepts constructions through completed captured
   Map evidence. Reuse the closure constructor census/rewrite and existing
   concrete record analysis to account for retained local constructions.
3. `HostContractAnalysis` requires each input in a completed
   `HostCapturedMap::outerKeyInputs` family. `OwnedGlobalRoots` requires the
   actual factory publication, reads/calls, exact function chain and allocation
   census. A fake root or scalar-only table supplies none of these.

The next useful witness combines the genuine wrapper/factory/root shape from
`CTNative/HostContract/dom-data-inputs.py` with the complete vendor Data/class
body, initially publishing only its scalar result through
`host.slot.set(element, result)` and reading it back. That isolates composition
before attempting persistent class-record storage. Reuse the existing DOM session
client's domain-before-effects, alias, lifetime, reset and owner-interleaving
checks. This witness is a proposed next step, not an implemented admission.

Multiple DOM-input alias partitions in class preparation, original B/Data+B,
broader Strings, conditional callees, mutable captured cells, String ordering,
document views and the application driver remain unfinished. No full-Bootstrap
admission or new corpus coverage measurement is claimed.
