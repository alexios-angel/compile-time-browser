# Compiler source layout

The large compiler implementation files are split by responsibility. Pass
entry points coordinate the work; private headers share the state required
by their implementation files. Public APIs remain under `include/ctcompile/`.

| Area | Location | Contents |
|---|---|---|
| Partial evaluation | `lib/CTNative/PartialEvaluation/` | Factory admission, static entry prefixes, bounded evaluation, call-target and heap-equivalence proofs, primitive adapter and residualisation |
| Binding time | `lib/CTNative/Analysis/BindingTime/` and `BindingTime/` | Argument/callee facts, conditional effect summaries and queries, heap flow and annotation pass |
| Precomputation | `lib/CTNative/Symbolic/` | Primitive and normal-result facts, bounded propagation, PDLL scalar replacements and native region splicing |
| Specialization | `lib/CTNative/Specialization/` | Closed direct-call candidates, exact argument tuples and bounded residual variants |
| Reachability | `lib/CTNative/Reachability/` | Bounded graph proof and transactional removal of private helpers |
| Supercompilation | `lib/CTNative/Supercompilation/` | Transactional process graphs, recursive promises, finite-shape embedding, scalar child generalization and budget control |
| Deforestation | `lib/CTNative/Deforestation/` | Snapshot producer/consumer constraints, effect barriers and scalar projection helpers |
| Native pass | `lib/CTNative/Lowering/LowerToEmitC.cpp` | Default optimization policy, pass setup, inference, admission fixpoint and lowering order |
| Closure lifting | `lib/CTNative/Lowering/ClosureLifting/` | Calls, callbacks, bindings, cells, captures, returned closures and method tables, constructors, diagnostics and rewriting |
| Native admission | `lib/CTNative/Lowering/Admission/` | Value, object, operation and function checks |
| Native emission | `lib/CTNative/Lowering/EmitC/` | Shapes, types, owning environments and method tables, Maps, expressions, operations, functions, module declarations and runtime helper text |
| Object value carriers | `lib/CTNative/Lowering/ObjectValues/` | Identity/scalar union classification, fixed scalar field layouts and owning runtime helpers |
| String value carriers | `lib/CTNative/Lowering/StringValues/` | Nullable owning strings, scalar consumers and ordered string snapshot helpers |
| Object identity flow | `lib/CTNative/Analysis/NativeObject/` | Map payload and structured schema edges, closed field/environment proof and bounded inert-slot proof |
| Shape inference | `lib/CTNative/Analysis/Inference/` | Receiver/cell groups, closed shapes and dense-vector proofs |
| Owning Map proofs | `lib/CTNative/Analysis/` and `Analysis/NativeMap/` | Shared closed value flow, Map schemas, conditional presence/effects, object identities and standard snapshot-copy proof |
| Boxed emission | `lib/CTJS/Lowering/EmitC/` | Admission, function setup, operation dispatch, status handling, roots and constants |
| C++ literals | `include/ctcompile/Support/CppLiterals.hpp` and `lib/Target/Cpp/ReadableFloat.*` | Shared byte-preserving string escaping and native shortest round-trip float spelling |
| C++ source names | `lib/CTJS/Import/Bytecode/SourceNames.*` and `lib/Target/Cpp/Names/` | Register-scope name provenance, name propagation through native values and collision-free C++ allocation |
| Global effects | `lib/CTJS/Lowering/Globals/` | Reflection, global identity and effects retained for refused bodies |
| Bytecode import | `lib/CTJS/Import/Bytecode/` | Per-function state, instruction dispatch, operator tables and support routines |
| Operation definitions | `include/ctcompile/CTJS/IR/Ops/` | TableGen records grouped by bindings, properties, runtime, operators, containers, modules, calls, functions, suspension and frames |
| Test registration | `test/cmake/` | Core/boxed, lit, runtime, analysis, native, native guards and native fixtures |
| Oracle fixtures | `test/TypeOracle/` | JavaScript programs with hand-computed observations |

`CTJSOps.td` remains the TableGen entry point and includes its operation groups
in their original order. `test/CMakeLists.txt` includes registration groups in
their original order and directory scope, preserving source/build paths and
helper visibility. Importer coverage reads the driver, instruction dispatch
and operator tables explicitly.

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

Refactor validation uses the complete devbox build and test gate, the pinned
formatting check, the registered test-name set, and byte comparisons of the
generated native modules and boxed Bootstrap output. No public pass name or
generated-program interface changes with this organization.

At the reorganization checkpoint, the devbox gate passed **290/290**, including **96/96** lit tests.
All 57 active native modules were regenerated byte-identically, and boxed
Bootstrap remains 10,976,150 bytes with SHA-256
`8dfd8e45a6a69a032c6f7b325573dc584f130989f81e49e6805e7c5faa71a6ba`.
The registered test-name set is unchanged. The pinned formatter passes all
419 C++ files, and `git diff --check` passes.
