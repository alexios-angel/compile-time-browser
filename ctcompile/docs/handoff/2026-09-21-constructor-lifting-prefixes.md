# Proved constructor lifting and native class sessions, 2026-09-21 UTC

Continued clean `0bbed840` and its recorded constructor-lifting boundary.
The protocol, claims, journal, HANDOFF, plan 00/24/25 and both commit histories
were read; `codex-wip-20260907` was already an ancestor. No interrupted dirty
code was present. Linux cmdline/comm inspection covered 75 processes without
unreadable records; Windows CIM covered 350. Neither found a Claude executable,
CLI or loop. No browser or shared implementation changed.

## Constructor lifting

`6a333142` composes the existing host method-table preparation with the
complete local constructor graph. After wrapper, factory and captured-method
rewrites, a fresh host query supplies exact constructor identities and the
primitive-field requirement. A fresh closure census avoids retaining erased
construction pointers. The existing lift now preserves each source callable
and fresh instance receiver for host reproof; ordinary local lifting keeps its
existing guards and behavior.

The provider independently recognizes the resulting allocation and direct
initializer. It checks the actual closure and target, literal actuals, primitive
nonreplacement returns, exact receiver, unobserved new.target and return value,
a single initializer per instance, and initialization before every non-root
instance observer or store. Its complete symbol, callable, allocation, field and
Map census is reconstructed from live IR. No preparation marker or printed
report supplies authority. Final source ownership is proved again before the
speculative clone is accepted.

Six existing sources now execute unchanged: the complete vendor Data declaration
with holder/constructor/DOM-alias public families, a distinct record field,
field arithmetic, and constructor-initialized field arithmetic. The class
computations remain independently observable: 15927 or 59112; the arithmetic
constructor case publishes 20 while retaining classResult 15927. All earlier
source generators, source tables and hostile source bodies are unchanged.

The existing record executable harness is shared with these class witnesses.
It checks real document owners, repeated inputs and aliases, distinct elements,
independent sessions, invalid and stale handles before source effects, detached
nodes, 16 construction/destruction cycles, both printing forms, GCC/Clang and
both optimization policies. Class results join the observation snapshots.
The source and symbol gates exclude Script/VM dependencies and erased-callable
or shared-object graph substitutes. No full Bootstrap admission is claimed.

## String prefixes

`e2bf7729` extends the existing ASCII startsWith proof to at most two
concatenation levels. It charges every visited operation and literal byte;
ordinary String addition does the execution. Computed prefixes still grant no
literal-only dataset authority. Two exact former refusal sources now execute;
all 150 previous complete cases and 33 general refusals are unchanged.

## Validation

Explicit targets were built with tools/remote-build.sh under the shared lock:
ctjs-opt, ctjs-translate, ctcompile-test-host-contract,
ctcompile-test-native-reference, ctcompile-test-owned-global-shared-map and
ctcompile-native-pipeline-constructors.

- Exact CTests pass 2/2: ctcompile_owned_global_shared_map 246.21 s and
  ctcompile_host_contract 0.56 s, total 246.78 s.
- Final selected CTNative/Browser/native-class-dom-data.test passes in
  181.45 s (181.46 s lit total).
- The unchanged focused DOM input, constructor-refusal and generic constructor
  fixture lit cases pass in 1.76 s, 7.70 s and 7.84 s respectively. They were
  not repeated after fixture-only expectation updates. The regenerated generic
  constructor pipeline retained
  its execution, clean-compilation, printing and mutation checks.
- Final class fixture coverage includes 48 new native class executions,
  56 existing record and 24 existing object-key executions. The public-family,
  class-field and constructor-field groups respectively pass 5/3/3 source
  observations/preparations/provider proofs with 24 executions and 23 refusals;
  5/5/5 with 16 executions and 22 refusals; and 9/9/9 with 8 executions and
  31 refusals. Unpublished DOM class controls retain four source observations,
  four preparations, 30 preparation refusals and eight session refusals;
  published record controls retain nine observations and 54 refusals.
- New provider controls pass 19 prepared-IR refusals, six native provenance
  refusals, one manually lifted initializer proof and nine initializer refusals,
  including stale fingerprint/budget and forged-report reproof.
- String selection passes 56 native executions, 112 proof refusals and six
  Node/VM agreements. Full intrinsic replay and String mutations were skipped.
- All six final code/test hashes match the devbox; four C++ and two Python
  files pass scoped formatting/syntax checks. The required repository
  formatter retains 16 untouched diagnostics in ctdrive.cpp, ProviderPaths.h,
  PartialEvaluation/Heap.h and Symbolic/Facts.cpp.

The first ad hoc native probe used an incorrect pass option; it supplied no
native validation. Its corrected probe passed the initializer proof and showed
the next carrier refusal. Two intermediate fixture runs stopped because exact
old refusal sources now emitted native code. Those sources were promoted to
execution checks, without changing their bytes. Their successful output was not
counted as an execution until compiled, linked and run. No C++ build failure
occurred in this increment. Initial proof review supplied callable/lifetime and
fresh-census constraints; root completed implementation and final review after
child service limits.

No full CTest/compiler lit, broad corpus or matrices, full Bootstrap, WPT/test262,
Windows, new sanitizer build, local native build or push ran.

Evidence: /tmp/ctcompile-constructor-lift-{gate1,gate2,gate3,ctest,lit,final-gate,results}.log,
/tmp/ctcompile-class-lift-classify.log, /tmp/ctcompile-nested-prefix-gate.log,
/tmp/ctcompile-constructor-lifting-focused.py,
/tmp/ctcompile-string-nested-prefix-focused.py and /tmp/ctcompile-constructor-lift-final.sha256.

## Next boundary

The unchanged constructor-only saved-field witness now reaches native family
admission and refuses the value carrier
`!ctnative.map<!ctnative.dom_element, !ctnative.opt<!ctnative.num<i32>>>`.
It retains two exact direct initializers and three real Map constructions;
constructor identity is no longer the refusal. Extend closed optional scalar
Map storage/argument/read handling, preserving null, undefined and missing-entry
semantics, or establish a sound field-presence proof. LoweringSupport.cpp's
nullableMapSpelling currently covers String-containing alternatives only; do
not discard absence or relax the source-owner proof.

Original composite-result publication still refuses class preparation. Multiple
DOM-input alias partitions, original B/Data+B, broader Strings, conditional
callees, mutable cells, String ordering, document views and the application
driver remain. There is no full-Bootstrap or corpus coverage claim.
