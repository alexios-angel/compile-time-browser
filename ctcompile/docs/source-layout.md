# Compiler source layout

The large compiler implementation files are split by responsibility. Pass
entry points coordinate the work; private headers share the state required
by their implementation files. Public APIs remain under `include/ctcompile/`.

The [test directory guide](../test/README.md) describes the test folders and
where fixtures and shared build helpers belong.

| Area | Location | Contents |
|---|---|---|
| Partial evaluation | `lib/CTNative/PartialEvaluation/` | Factory admission, static entry prefixes, bounded evaluation, call-target and heap-equivalence proofs, primitive adapter and residualisation |
| Binding time | `lib/CTNative/Analysis/BindingTime/` and `BindingTime/` | Argument/callee facts, conditional effect summaries and queries, heap flow and annotation pass |
| Host contracts | `lib/CTNative/HostContract/` | Fingerprinted closed-source manifests, fresh allocation identity, own-data slot flow, bounded proof reports and entry-prefix specialization |
| Private provider state | `lib/CTNative/HostContract/Provider/State.*`, `Objects.*`, `Paths.h` and `Prefix/Mutations.cpp` | Transactional Maps, live object identities/scalar fields, allocation/call provenance and bounded normal-return path summaries |
| Precomputation | `lib/CTNative/Symbolic/` | Primitive and normal-result facts, bounded propagation, scalar constant replacement patterns and native region splicing |
| Specialization | `lib/CTNative/Specialization/` | Closed direct-call candidates, exact argument tuples and bounded residual variants |
| Reachability | `lib/CTNative/Reachability/` | Bounded graph proof and transactional removal of private helpers |
| Supercompilation | `lib/CTNative/Supercompilation/` | Transactional process graphs, recursive promises, finite-shape embedding, scalar child generalization and budget control |
| Deforestation | `lib/CTNative/Deforestation/` | Snapshot producer/consumer constraints, effect barriers and scalar projection helpers |
| Native pass | `lib/CTNative/Lowering/LowerToEmitC.cpp` | Default optimization policy, pass setup, inference, admission fixpoint and lowering order |
| Closure lifting | `lib/CTNative/Lowering/ClosureLifting/` | Calls, callbacks, bindings, cells, captures, returned closures and method tables, constructors, diagnostics and rewriting |
| Provider paths | `lib/CTNative/HostContract/Provider/` and `Prefix/Mutations.cpp` | Private Map transactions, ordered diagnostic snapshots, bounded primitive callback effects and atomic global post-state |
| Native admission | `lib/CTNative/Lowering/Admission/` | Value, object, operation and function checks |
| Native exceptions | `lib/CTNative/Lowering/Exceptions/` | Bounded handler recovery using LLVM CFG-to-SCF, homogeneous primitive payload/effect admission, transactional rollback and owning catch-state storage |
| Native emission | `lib/CTNative/Lowering/EmitC/` | Shapes, types, owning environments and method tables, Maps, expressions, operations, functions and module declarations |
| Native runtime | `include/ctcompile/CTNative/Runtime/ctnative.hpp` | The one header every generated program includes: nullable scalars and strings, dense arrays, object values, Map storage and helpers, owned-global and method-table accessors, and the DOM section behind `CTNATIVE_DOM` |
| Object value carriers | `lib/CTNative/Lowering/ObjectValues/` | Identity/scalar union classification, fixed scalar field layouts and owning runtime helpers |
| String value carriers | `lib/CTNative/Lowering/StringValues/` | Nullable owning strings, scalar consumers and ordered string snapshot helpers |
| Object identity flow | `lib/CTNative/Analysis/NativeObject/` | Map payload and structured schema edges, closed field/environment proof and bounded inert-slot proof |
| Shape inference | `lib/CTNative/Analysis/Inference/` | Receiver/cell groups, closed shapes and dense-vector proofs |
| Owning Map proofs | `lib/CTNative/Analysis/` and `Analysis/NativeMap/` | Shared closed value flow, Map schemas, conditional presence/effects, object identities and standard snapshot-copy proof |
| Owning table slots | `lib/CTNative/Analysis/OwnedMethodTableSlots.*` | Bounded confined-owner and initialization proof, explicitly consumed by returned-table flow and rebuilt for native admission |
| Boxed emission | `lib/CTJS/Lowering/EmitC/` | Admission, function setup, operation dispatch, status handling, roots and constants |
| C++ literals | `include/ctcompile/Support/CppLiterals.hpp` and `lib/Target/Cpp/ReadableFloat.*` | Shared byte-preserving string escaping and native shortest round-trip float spelling |
| C++ const bindings | `lib/Target/Cpp/Const/` and `lib/CTNative/Lowering/EmitC/Calls.cpp` | Backward binding-mutability data flow, lvalue/capture aliases and native helper operand ABI contracts |
| C++ callable bodies | `lib/Target/Cpp/Callables/` | Final-IR lambda bodies at creation sites, validated bounded expansion, independent nested printer state and retained helper/function references |
| C++ exception regions | `lib/Target/Cpp/Exceptions/` | Typed native try/throw printing with scoped catch declarations and collision-safe names |
| C++ parameter suppression | `lib/Target/Cpp/UnusedParameters.h` | Final printed-use analysis for compiler-generated unused-parameter casts |
| Global effects | `lib/CTJS/Lowering/Globals/` | Reflection, global identity and effects retained for refused bodies |
| Bytecode import | `lib/CTJS/Import/Bytecode/` | Per-function state, instruction dispatch, operator tables and support routines |
| Operation definitions | `include/ctcompile/CTJS/IR/Ops/` | TableGen records grouped by bindings, properties, runtime, operators, containers, modules, calls, functions, structured exceptions, suspension and frames |
| Test registration | `test/cmake/` | Core/boxed, lit, runtime, analysis, native, native guards and native fixtures |
| Oracle fixtures | `test/Analysis/Types/TypeOracle/` | JavaScript programs with hand-computed observations |

`CTJSOps.td` remains the TableGen entry point and includes its operation groups
in their original order. `test/CMakeLists.txt` includes registration groups in
their original order and directory scope, preserving source/build paths and
helper visibility. Importer coverage reads the driver, instruction dispatch
and operator tables explicitly.

The notes beside this one, one line each; dated handoffs, plans, reviews and
per-note evidence live in the subdirectories.

- [bootstrap-data-probe.md](bootstrap-data-probe.md) — the Bootstrap Data publication probe (`tools/check/bootstrap-data-probe.py`)
- [bootstrap-native-next.md](bootstrap-native-next.md) — what native Bootstrap needs next; its steps are in `bootstrap-native-next/`
- [bootstrap-provider-next.md](bootstrap-provider-next.md) — the next Bootstrap native provider boundary; steps in `bootstrap-provider-next/`
- [ctcompile.md](ctcompile.md) — the `ctcompile` command line
- [escape-load-evidence.md](escape-load-evidence.md) — direct loads and candidate provenance in escape analysis
- [escape-oracle-aot.md](escape-oracle-aot.md) — AOT-compiled frames' allocations in the escape oracle
- [escape-storage-evidence.md](escape-storage-evidence.md) — direct storage evidence (`EscapeVerdicts::directStorage`)
- [HANDOFF.md](HANDOFF.md) — continuation state: synchronization, the current gate and next steps
- [LLVMUpgrade.md](LLVMUpgrade.md) — upgrading LLVM and MLIR, always as its own change
- [merge-main.md](merge-main.md) — the 2026-09-24 merge of ctcompile-v1 into main
- [native-ab.md](native-ab.md) — the native tier's four measured numbers (part 24 Phase 63 Step 4)
- [native-binding-time.md](native-binding-time.md) — binding-time analysis
- [native-bootstrap-host-contract.md](native-bootstrap-host-contract.md) — the Bootstrap Data host and export contract
- [native-bootstrap-script-this.md](native-bootstrap-script-this.md) — Bootstrap's classic-script receiver
- [native-callbacks.md](native-callbacks.md) — callback arguments and Bootstrap startup
- [native-conditional-effects.md](native-conditional-effects.md) — conditional heap-effect queries across calls
- [native-const-bindings.md](native-const-bindings.md) — const bindings and by-value parameters in native C++
- [native-deforestation.md](native-deforestation.md) — `--ctnative-deforest`: removing temporary Map snapshots
- [native-divergences.md](native-divergences.md) — where the native backend and the interpreter declaredly disagree
- [native-dom-entry.md](native-dom-entry.md) — DOM entries through the `ctbrowser-dom-v1` host contract
- [native-exceptions.md](native-exceptions.md) — JavaScript exceptions as C++ unwinding
- [native-export-boundary.md](native-export-boundary.md) — export ownership and calls
- [native-host-callables.md](native-host-callables.md) — the exported getter proof
- [native-host-prefix.md](native-host-prefix.md) — checked host entry-prefix specialization
- [native-host-slots.md](native-host-slots.md) — checked host roots and publication-slot flow
- [native-literals.md](native-literals.md) — readable native C++ literals
- [native-map-presence.md](native-map-presence.md) — conditional membership proofs for Maps
- [native-maps.md](native-maps.md) — standard Maps, toward Bootstrap Data
- [native-method-tables.md](native-method-tables.md) — returned method tables
- [native-modern-pe.md](native-modern-pe.md) — design review: modern partial evaluation for CTNative
- [native-nested-maps.md](native-nested-maps.md) — Maps containing Maps
- [native-object-fields.md](native-object-fields.md) — owned component fields
- [native-object-keys.md](native-object-keys.md) — object identities as Map keys
- [native-object-values.md](native-object-values.md) — object identities in Map values
- [native-optimization-defaults.md](native-optimization-defaults.md) — what `--ctnative-lower-to-emitc` optimizes by default
- [native-optional-scalars.md](native-optional-scalars.md) — tagged optional scalars
- [native-owned-global-maps.md](native-owned-global-maps.md) — captured Maps across global publication; evidence in `native-owned-global-maps/`
- [native-owned-global-methods.md](native-owned-global-methods.md) — ownership for an exported getter
- [native-owned-globals.md](native-owned-globals.md) — checked ordinary global owners
- [native-owned-method-table-slots.md](native-owned-method-table-slots.md) — owning method-table fields on confined objects
- [native-partial-evaluation.md](native-partial-evaluation.md) — partial evaluation and heap residualisation
- [native-pe-call-proof.md](native-pe-call-proof.md) — proving partial-evaluation call targets
- [native-pe-roadmap.md](native-pe-roadmap.md) — the partial-evaluation implementation plan
- [native-precomputation.md](native-precomputation.md) — `--ctnative-precompute`: symbolic precomputation
- [native-provider-diagnostics.md](native-provider-diagnostics.md) — provider diagnostics and callback effects
- [native-provider-mutations.md](native-provider-mutations.md) — private Map mutation summaries
- [native-provider-objects.md](native-provider-objects.md) — provider object payloads
- [native-reachability.md](native-reachability.md) — `--ctnative-prune-unreachable`: removing unreachable helpers
- [native-returned-closures.md](native-returned-closures.md) — returned closures
- [native-scalar-unions.md](native-scalar-unions.md) — closed boolean/number unions
- [native-source-invocations.md](native-source-invocations.md) — source calls at the native exception boundary
- [native-specialization.md](native-specialization.md) — `--ctnative-specialize`: direct-call variants
- [native-string-snapshots.md](native-string-snapshots.md) — string-key Map snapshots
- [native-strings.md](native-strings.md) — owning native strings
- [native-supercompilation.md](native-supercompilation.md) — supercompilation design and research basis
- [refactor-goldens.md](refactor-goldens.md) — proving a lowering refactor changed nothing
- [release-0.1.0.md](release-0.1.0.md) — the 0.1.0 developer preview scope and checklist

The September 2026 reorganization reduced the original entry files as follows:

| File | Before | After |
|---|---:|---:|
| Native `LowerToEmitC.cpp` | 7,133 | 300 |
| `CTJSToEmitC.cpp` | 2,396 | 61 |
| Test `CMakeLists.txt` | 2,176 | 9 |
| `CTJSOps.td` | 1,545 | 60 |
| `BytecodeImport.cpp` | 1,533 | 444 |
| `ResolveGlobals.cpp` | 1,158 | 887 |
| `TypeOracle.cpp` | 1,072 | 970 |
| `TypeInference.cpp` | 1,017 | 730 |

The vendored upstream C++ emitter retains its original file layout for LLVM
upgrade comparisons. Historical planning documents retain their section
anchors.

Refactor validation follows the focused policy in [CLAUDE.md](../../CLAUDE.md), plus
the affected generated output and, when tests move, the registered test-name set.
No public pass name or generated-program interface changes with this organization.

At the reorganization checkpoint, the devbox gate passed **290/290**, including **96/96** lit tests.
All 57 active native modules were regenerated byte-identically, and boxed
Bootstrap remains 10,976,150 bytes with SHA-256
`8dfd8e45a6a69a032c6f7b325573dc584f130989f81e49e6805e7c5faa71a6ba`.
The registered test-name set is unchanged. The pinned formatter passes all
419 C++ files, and `git diff --check` passes.
