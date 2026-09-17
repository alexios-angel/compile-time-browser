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
| `CTNative/Checks/` | The build's native pipeline script and the python gates (compilation unit, clean compile, printing) every fixture `.test` runs |
| `Runtime/` | Runtime library checks and `Differential/`, `Modules/`, `GC/`, `Linking/`, `Reference/` and `Launcher/` tests |
| `Lowering/` | CTJS-to-runtime and boxed EmitC lowering |
| `Target/Cpp/` | C++ emission and upstream translation cases |
| `Packaging/` | Application bundles, manifests, program images and page fixtures |
| `Comparison/` | HTML and CSS comparison |
| `Core/` | Declarative inventories and specification citations |
| `Support/` | Shared source embedding, extraction and compilation scripts |
| `cmake/` | CTest registration, included in the root directory scope |

Large C++ suites use a named subfolder with ordinary translation units and a
shared header. Python execution suites use ordinary modules: lit puts `test/`
on `PYTHONPATH`, so a driver imports a sibling as `from CTNative.Exports import
boundary` and shares `CTNative/harness.py` (`run`, `find_compilers`); Node and
the interpreter reference arrive as `--node %node --reference %native_reference`
on the RUN line. A hand run needs the same `PYTHONPATH` and both flags. The former
`cmake -P` checks are `.test` files too, beside what they gate (a fixture's `.test` sits
beside its `.js` and reads the build's `<name>.pipeline*.emitc.mlir`). Lit cases remain
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
protocol and the focused validation policy in [CLAUDE.md](../../CLAUDE.md).
For example, this builds and runs one CTest under the shared devbox lock:

```bash
flock /tmp/ctbrowser-devbox-build.lock bash <<'GATE'
set -e
tools/remote-build.sh ctcompile-test-native-runtime
ssh devbox 'ctest --test-dir projects/compile-time-browser/build --output-on-failure --no-tests=error -R "^ctcompile_native_runtime$"'
GATE
```

For a native DOM assignment change, build `ctjs-opt`, `ctjs-translate`,
`ctcompile-tool` and `ctcompile-test-native-reference` with the same helper,
then run only that lit case on the devbox under the same lock:

```bash
~/.lit-venv/bin/lit -sv projects/compile-time-browser/build/ctcompile/test \
  --filter='^ctcompile :: CTNative/Browser/native-dom-assignment[.]test$'
```

Use the generated build-tree lit configuration so substitutions and features
are available. Adjust the targets, test selection, host and remote directory to
the change; a selection that runs zero tests is not a pass. Full lit runs through
`ctcompile_lit` or `check-ctcompile` require an explicit user request. The full
lit CTest allows 2,400 seconds for the native ownership matrix.
