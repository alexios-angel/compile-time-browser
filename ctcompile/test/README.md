# Compiler tests

Tests are grouped by the compiler subsystem or runtime behavior they exercise.
Keep fixtures beside their tests; shared pipeline fixtures and build helpers have
separate homes.

| Directory | Contents |
| --- | --- |
| `Analysis/Escape/` | Escape proofs, claims, fixtures and differential oracles |
| `Analysis/Types/` | Type inference, lattice, claims and type oracles |
| `Analysis/Ownership/` | Global roots, method tables and captured Map owners |
| `CTJS/` | Dialect IR, bytecode import and transformations |
| `CTNative/` | Native IR and pass tests in `IR/`, `BindingTime/`, `Optimization/`, `PartialEvaluation/`, `Precomputation/`, `Specialization/` and `Supercompilation/` |
| `CTNative/Lowering/` | Native admission and emission tests grouped into `Maps/`, `Objects/`, `Closures/`, `Scalars/`, `Exceptions/`, `Admission/` and `Emission/` |
| `CTNative/Ownership/` | Owned global execution checks and the `native_owned_global_maps/` source and lifetime modules |
| `CTNative/Exports/` | Published native entry points and their source specimen |
| `CTNative/HostContract/` | Host proof unit tests, source checks and `Provider/` state, read and mutation cases |
| `CTNative/ExceptionRecovery/` | Exception-region recovery unit tests |
| `CTNative/Fixtures/` | Pipeline inputs grouped into `Maps/`, `Objects/`, `Closures/`, `Scalars/`, `ControlFlow/` and `Optimization/` |
| `CTNative/Checks/` | Shared native pipeline, standalone compilation and snapshot checks |
| `Runtime/` | Runtime library checks and `Differential/`, `Modules/`, `GC/`, `Linking/`, `Reference/` and `Launcher/` tests |
| `Lowering/` | CTJS-to-runtime and boxed EmitC lowering |
| `Target/Cpp/` | C++ emission and upstream translation cases |
| `Packaging/` | Application bundles, manifests, program images and page fixtures |
| `Comparison/` | HTML and CSS comparison |
| `Core/` | Declarative inventories and specification citations |
| `PDLL/` | Declarative pattern guards |
| `Support/` | Shared source embedding, extraction and compilation scripts |
| `cmake/` | CTest registration, included in the root directory scope |

Large C++ suites use a named subfolder with ordinary translation units and a
shared header. Python execution suites use ordinary modules. Lit cases remain
with their pass or target; `.mlir`, `.td` and `.test` files are discovered by lit,
including the two handwritten EmitC fixtures under `CTNative/Fixtures/`.

Name files for the behavior they check; omit prefixes already supplied by the
directory. Use PascalCase for C++ files, kebab-case for scripts and lit cases,
and underscores for importable Python modules. Give a test and its dedicated
helper the same stem. Keep standard tool filenames and upstream fixture names.

CMake target names and generated filenames are independent of source locations.
Update registrations and relative references when moving a test, and use
`git mv` under the repository Git lock. Build and run the affected CTests on the
devbox through `tools/remote-build.sh`, following the repository synchronization
protocol. Run the complete compiler lit suite with
`ctest --preset default -R '^ctcompile_lit$'` from `ctbrowser/`. Its CMake
registration allows 2,400 seconds for the native ownership matrix.
