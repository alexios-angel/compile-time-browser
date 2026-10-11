# Compiler tests

Tests are grouped by the compiler subsystem or runtime behavior they exercise.
Keep fixtures beside their tests; shared pipeline fixtures and build helpers have
separate homes.

| Directory | Contents |
| --- | --- |
| `Analysis/Escape/` | Escape proofs, the `claims/` corpus, the `Arrays/` suite and differential oracles |
| `Analysis/Types/` | Type inference, lattice, claims and type oracles |
| `Analysis/Ownership/` | Global roots, method tables and captured Map owners |
| `CTJS/` | Dialect IR, bytecode import and transformations |
| `CTNative/` | Native IR and pass tests in `IR/`, `BindingTime/`, `Optimization/`, `PartialEvaluation/`, `Precomputation/`, `Specialization/` and `Supercompilation/` |
| `CTNative/Browser/` | DOM entry drivers in `dom/`, `class_dom/` and `bootstrap/`: each case is a `.test` beside its same-stem `.py` driver |
| `CTNative/Lowering/` | Native admission and emission tests grouped into `Maps/`, `Objects/`, `Closures/`, `Scalars/`, `Exceptions/`, `Admission/` and `Emission/` |
| `CTNative/Ownership/` | Owned global execution checks, and `global_maps/`: the owned-global-maps package (`driver/`, `harness/`, `sources/`) beside its tests |
| `CTNative/Exports/` | Published native entry points and their source specimen |
| `CTNative/HostContract/` | Host proof unit tests and their `DOM/` fixtures, the `SeededMaps/` suite, source checks and `Provider/` state, read and mutation cases |
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
directory. Files whose names start with the same word share a folder named
for it, recursively (`claims/primitive/`, `class_dom/boolean/`); a
singular/plural pair (`nested-map-proof`, `nested-maps`) or one compound term
(`lift-to-scf`) is not a family, and a family's own case is `basics` inside its
folder when that folder is a Python package. Every folder that holds C++ owns a
CMakeLists.txt, which adds only its immediate subfolders
(tools/check/check-cmake-hygiene.py checks the first half). Use PascalCase for C++ files, kebab-case for scripts and lit cases,
and underscores for importable Python modules. Give a test and its dedicated
helper the same stem. Keep standard tool filenames and upstream fixture names.

CMake target names and generated filenames are independent of source locations.
Update registrations and relative references when moving a test, and use
`git mv`. Build only the targets a change needs and run only the affected
tests. For example, one CTest:

```bash
cmake --build build --target ctcompile-test-native-runtime
ctest --test-dir build --output-on-failure --no-tests=error -R "^ctcompile_native_runtime$"
```

For a native DOM assignment change, build `ctjs-opt`, `ctjs-translate`,
`ctcompile-tool` and `ctcompile-test-native-reference`, then run just that lit
case:

```bash
.venv/bin/lit -sv build/ctcompile/test \
  --filter='^ctcompile :: CTNative/Browser/dom/assignment[.]test$'
```

Use the generated build-tree lit configuration so substitutions and features
are available. Adjust the targets and test selection to the
change; a selection that runs zero tests is not a pass. Full lit runs through
`ctcompile_lit` or `check-ctcompile` take a long time; save them for releases. The full
lit CTest allows an hour, and so does each lit case: `lit.cfg.py` sets the limit,
and a case that runs past it reports TIMEOUT and fails the run. lit needs `psutil`
in its environment to enforce that; pyproject.toml's `test` group installs it
beside lit (`uv sync --group test`).
