# Optional Map payloads and negated primitive indices, 2026-09-21 UTC

Continued clean `72040551` and its recorded optional numeric DOM Map boundary.
The standing instructions, synchronization protocol/claims/journal, handoff,
plan 00/24/25 and both commit histories were inspected. No dirty interrupted
code was present; `codex-wip-20260907` was already an ancestor. Linux process
inspection covered 70 cmdline/comm records without errors; Windows CIM covered
363. Neither found a Claude executable, CLI or loop. No browser or shared
implementation was edited. Parallel agents supplied String work, fixtures and
review; root completed saved drafts after interruptions and service limits.

## Optional scalar Map payloads

`c3dc2da0` reuses `nullable_scalar` for closed optional scalar payloads.
Null, undefined, Number and Boolean retain separate tags. A missing entry reads
as undefined, while `has` distinguishes absence from a stored undefined value.
Payload NaN and negative zero survive writes and reads. Full scalar writes need
no narrowing. Independently proved present Number/Boolean reads use checked tag
extraction; ordinary reads copy the tagged payload. Raw borrowed Map pointers
and existing owning helper calls share the same constrained implementation.
Scalar value snapshots still refuse because their current vector carrier would
erase null/undefined tags.

Nine unchanged vendor-derived class/Data sources now execute: saved record and
constructor fields, constructor-only values, another record's write, an early
saved read, literal initialization, registered construction, Boolean fields and
repeated constructor stores. Together with six previously native sources, there
are 15 class/Data sources and 120 native class executions. The original class
computations and complete vendor Data declaration remain in each source. Boolean
publication is checked through `global_boolean`, so a Number cannot imitate it.
Both object-valued post-read field mutations still refuse ownership. Preparation,
provider evidence and the native owner census were not weakened.

Four unchanged ordinary Map refusal fixtures now execute: optional Number and
Boolean payloads, saved missing reads and joined present/missing reads. A new
source checks null, undefined, missing entries, NaN, negative zero, both Booleans,
optional values and saved reads after replacement, deletion and clearing. The
original mixed-Map fixture bytes remain unchanged; new sources were appended.

## Negated primitive String indices

`ffb18caa` extends the existing bounded literal-index proof to unary-negated
null and Boolean literals. Four exact former refusal sources execute unchanged;
aggregate and capture witnesses cover signed zero, arithmetic and saved String
ownership. Two arithmetic levels, complete use census, budgets, fingerprints
and literal-only casing/dataset authority remain required.

The first native probe exposed a missing conversion: host null is represented
by an empty optional String, and unary numeric lowering could not consume it.
The shared conversion now maps only that exact null constant to positive zero
before negation. Other optional String values gain no conversion authority.
The regression also observes the same null separately from its negation. A
subsequent test-only unqualified `js_num` ambiguity was corrected before the
final pass. No C++ build failed. An ad hoc type-inference probe used an invalid
option and supplied no validation; the failing source and actual lowering
path established the conversion defect.

## Focused validation

All builds used explicit `tools/remote-build.sh` targets under the shared build
lock: ctjs-opt, ctjs-translate, ctcompile-test-native-reference,
ctcompile-test-native-runtime and ctcompile-test-host-contract.

- Exact CTests pass 2/2: ctcompile_native_runtime 0.01 s and
  ctcompile_host_contract 0.55 s, total 0.57 s.
- Selected CTNative/Browser/native-class-dom-data.test passes in 296.24 s,
  including 120 class, 56 record and 24 object-key native executions. The class
  public-family group retains 24 executions and 23 refusals; field and
  constructor groups now have 32/18 and 64/17 executions/refusals respectively.
  Existing provider provenance, manual initializer, budget and fingerprint
  controls remain active. These sessions run GCC/Clang, both optimization
  policies and both printing forms with real document owners.
- Selected CTNative/Lowering/Maps/nullable-scalar-keys.mlir and dom-map-keys.test
  pass 2/2 in 10.83 s. The latter exercises associative/ordered storage,
  raw/owning reads and its existing Clang ASan/UBSan checks.
- Selected CTNative/Lowering/Maps/map-mixed.mlir passes in 73.63 s, including
  existing mixed/String storage and lifetime checks, forged/recomputed facts,
  the four unchanged promotions and the new tagged snapshot refusal. It retains
  35 refusal source cases and six admitted scalar key/payload controls.
- The focused scalar selector independently passes five sources in two storage
  layouts: ten Node/VM agreements and 20 GCC/Clang executions with symbol checks.
- The final String selector passes 64 native executions and 136 proof refusals,
  with six Node/VM agreements and one existing byte-index/UTF-16 difference.
  All 155 previous complete String cases and 33 general refusals are unchanged.
- All eleven final code/test hashes match the devbox. Six C++ and four Python
  files pass scoped formatting/syntax checks. `tools/format.sh --check` retains
  16 pre-existing diagnostics in untouched ctdrive.cpp, ProviderPaths.h,
  PartialEvaluation/Heap.h and Symbolic/Facts.cpp.

No full CTest/compiler lit, full intrinsic replay, broad corpus/matrix, full
Bootstrap, WPT/test262, Windows build, new sanitizer build, local native build
or push ran. String executable mutations were skipped. Existing sanitizer
executions belonging to the selected Map tests did run.

Evidence: /tmp/ctcompile-optional-map-build{0,1,2,3,4,5}.log,
/tmp/ctcompile-optional-class-final.log, /tmp/ctcompile-optional-map-keys.log,
/tmp/ctcompile-map-mixed-final.log, /tmp/ctcompile-map-scalar-focused.log,
/tmp/ctcompile-negated-primitive-final.log,
/tmp/ctcompile-optional-map-final-ctest.log and
/tmp/ctcompile-optional-map-final.sha256. Focused runners remain in /tmp as
ctcompile-map-scalar-focused.py and ctcompile-negated-primitive-focused.py.

## Next boundary

Both unchanged original composite-result publications were probed after the
carrier change. They still fail preparation with `class DOM input has an
observer outside its proved Map keys`; their full diagnostics are under
/tmp/ctcompile-optional-next-boundary on the devbox. The next proof work is the
complete local Map/record result, including Map observations and Boolean numeric
coercion. Retain all source observers and owners while extending that evidence.
Object-valued field mutations still require additional ownership work.

Multiple DOM-input alias partitions, original B/Data+B, tagged Map value
snapshots, broader Strings, conditional callees, mutable cells, String ordering,
document views and the application driver remain unfinished. These focused
measurements do not establish full Bootstrap admission or corpus coverage.
