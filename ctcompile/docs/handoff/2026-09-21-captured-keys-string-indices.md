# Captured keys and literal String indices, 2026-09-21 UTC

Continued clean `641c8ee5` from HANDOFF, plan 00/24/25 and both commit histories.
The previous multi-slot/intrinsic work was committed; no interrupted native draft
or `codex-wip-20260907` remained. Complete Linux cmdline/comm (76 processes) and
Windows CIM (356 processes) checks found no Claude executable, Node CLI or loop.
Initial executable-link permission failures and an invalid Windows query were
replaced by those complete checks. No browser/runtime implementation changed.

## Changes

`e7b45436` proves one local Map plus one immutable captured String key before
existing constructor publication normalization. A parameter-initialized cell is
fixed transport; its value still needs a separate proof. A private uncaptured
wrapper qualifies only when every visible exact caller supplies the same String
literal, its original closure/declaration does not escape, and no other symbolic
caller exists. The resolver's `ctjs.globals` diagnostic table grants no authority;
it is temporarily omitted from the symbol census and restored with RAII. Other
module and operation attributes remain checked. Fixed-cell Map keys use the
existing source-value lookup. Original source effects, terminal/base publication,
complete Map observer census and concrete stack-record ownership remain required.

The original `class-map-record-constructor-dynamic-key.js` in file 30 now executes
without changing its bytes. Four new bodies cover a literal key, repeated identical
wrapper actuals, reversed capture order and saved record aliases across overwrite
and deletion. Nine new source refusals retain different/missing/Number/object/
computed keys, escaped wrappers, changing captures, unused ambient effects and
publication before field completion. Four IR controls cover module/operation
symbol references, a public wrapper and the wrong callee value. The original
witness also runs existing forged-annotation, fingerprint and rollback controls;
its first complete work budget is **802**. Files 30/33/35/36/37/38/39 remain byte
identical, including the complete original Bootstrap B/Data+B controls.

`2e6dc90a` extends already-admitted String `charAt`/`slice` to nonnegative integer
literal indices through uint32 max. Generated C++ converts through public Core
WTF-8/UTF-16 functions and clamps offsets before `substr`. Original index 0/1
facts alone authorize first-unit lowercase and dataset-key reconstruction.
Three new export bodies cover typed Strings, saved guarded Symbol descriptions
and immutable helper captures. Negative, fractional, nonfinite, oversized,
dynamic/coercing indices, unsupported arities and prototype mutation still refuse.
Two original DOM class refusal bodies, `class_utf16_char_index` and
`class_utf16_slice_index`, are promoted unchanged. All 41 preceding positive export
bodies/native assertions and 33 original base refusal bodies are preserved.

The VM's ASCII-only casing and byte indexing remain explicit differences. Native
and Node retain Unicode expectations, including NUL, supplementary pairs, lone
surrogates and out-of-range results. A stale hardcoded summary said 49 agreements
and two differences despite the successful expanded 55-observation oracle. The
report now derives those counts from the checked expectations; a focused source
oracle rerun verifies **52 agreements and three differences**. No native code or
source expectation changed for that reporting correction.

Three parallel agents were used. The intrinsic agent supplied its four-file draft
and an independent publication review; fixture/audit agents hit service limits.
Root completed the captured-key fixtures, implementation and serialized gates.

## Focused validation

| Check | Final result |
| --- | --- |
| Exact CTest `ctcompile_host_contract` | 1/1, 0.57 s; 0.58 s total |
| `CTNative/Lowering/Objects/class-captured-map-helpers.test` | 1/1, 192.30 s |
| `CTNative/Lowering/Objects/class-terminal-publication.test` | 1/1, 140.05 s |
| `CTNative/Exports/native-intrinsic-symbols.test` | PASS, 328.60 s |
| `CTNative/Browser/native-dom-strings.test` | PASS, 209.92 s |
| Existing class DOM driver, only UTF16_CASES/UTF16_REFUSALS | 92 observations, eight binaries, 404 refusals |
| Expanded Symbol source oracle, reporting correction | 52 Node/VM agreements, three known differences |

The captured fixture measures **95 source observations, 224 main native executions,
190 unprepared refusals and 118 preparation refusals**. Five new admissions add
40 executions. The adjacent terminal fixture measures 58 observations, 160 main
executions, 116 unprepared refusals and 70 preparation refusals. Both class drivers
also run existing constructed-method controls (16 executions/20 refusals) and
original helper controls (eight executions/four refusals), separate from main counts.
Intrinsic exports measure **352 native executions, 356 refusals and two mutations**.
DOM Strings measure **809 Node/VM observations and eight GCC/Clang binaries**, plus
its existing source/provenance/budget controls and replacement checks.

All native builds and executions used the devbox under the shared lock. Explicit
targets: `ctjs-opt`, `ctjs-translate`, `ctcompile-test-host-contract` and
`ctcompile-test-native-reference`. The initial 14-step build passed. Focused
corrections required three-step rebuilds; the final build passed. The first four-case
lit selection passed 3/4 in 328.61 s. Captured-only failures at 12.62 s and 12.37 s
exposed parameter-cell transport and diagnostic-symbol census restrictions.
The first captured failure had exposed a raw fixed-cell key lookup. These were
corrected through existing proof mechanisms, retaining all refusal witnesses.
One build rejected deprecated `llvm::make_scope_exit`; direct `llvm::scope_exit`
fixed it. The first ad-hoc class subset failed because a non-login shell lacked
`node`; using the generated lit tool paths passed.

All **11 final code/test SHA-256 hashes** match the devbox, including the reporting
correction. Five C++ and five Python files pass scoped formatting, Python syntax
and whitespace checks. Required repository formatting retains the same 16 existing
diagnostics in untouched `ctbrowser/tools/ctdrive/ctdrive.cpp`,
`HostContract/ProviderPaths.h`, `PartialEvaluation/Heap.h` and `Symbolic/Facts.cpp`.
No full CTest/compiler lit, broad corpus/native matrices, full Bootstrap,
WPT/test262, Windows, new sanitizers, local native builds or push occurred.

Evidence: `/tmp/ctcompile-key-index-build1.log`, `-gate{2,3,4,5,6}.log`,
`-lit2.json`, `-lit-final.json`, `-terminal-final.json`, `-utf16-final.log`,
`-oracle-final.log`, `-format-final.log`, `-local.sha256` and `-evidence.log`,
all with prefix `/tmp/ctcompile-key-index` (except the first full path above).

## Exact next boundary

Original Data's three-argument registration uses both an element key and a component
key. Prove conditional creation and nested Map origins, conflict reporting, nullable
gets and child/empty-parent cleanup against every concrete record owner, observer,
exception and reentry path. Accepting an unused third formal would not implement
that behavior. Original B also retains configuration/disposal, `e.set`, `e.remove`
and `P.off`; preserve its complete source and the Data+B body.

The new captured-key slice does not specialize differing wrapper actuals, computed
keys or helper key captures, and it does not add nested record transport or broader
constructor families. Unused holder slots and method-frame operations remain.
Broader String methods, negative/dynamic/coercing indices and slice end arguments,
conditional callees, branch-mutated boxed locals, String ordering, document views
and the application driver remain separate. No full-Bootstrap admission or coverage
gain is claimed.
