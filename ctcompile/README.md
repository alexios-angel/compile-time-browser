# ctcompile

The whole-application compiler: an application directory in, a self-contained
native executable out.

The application driver is still incomplete. With MLIR enabled, `ctjs-translate`
and `ctjs-opt` import JavaScript and compile it through boxed or native EmitC
pipelines. The native subset emits standalone C++ and diagnoses unsupported
functions. Full native Bootstrap execution is not yet established.

Recent native work includes [tagged optional scalars](docs/native-optional-scalars.md)
and opt-in [partial evaluation with heap residualisation](docs/native-partial-evaluation.md).
The partial evaluator reuses ctbrowser's primitive semantics inside the compiler;
generated native programs retain no VM dependency.

## What it is for

A ctbrowser application is an HTML document, JavaScript, CSS and assets that
today are parsed and compiled every time the application starts. ctcompile does
that work once, at build time, and ships the result:

| input | today, at startup | after ctcompile |
|---|---|---|
| HTML | tokenised and tree-built | a relocatable document blueprint |
| CSS | tokenised, parsed, cascaded | a compiled style program |
| JavaScript | parsed, compiled to bytecode, interpreted | native code |
| assets | read from disk | packaged in the image |

What it must **not** do is freeze anything the viewport decides. The SDL3 window
can be any size and can be resized while running, so style, layout, wrapping,
paint geometry and raster stay runtime work. Structure is compiled; resolution
is not.

## Where it sits

```
compile-time-browser/     the monorepo
├── ctbrowser/            the engine and runtime
└── ctcompile/            this project
```

The dependency runs one way: ctcompile depends on ctbrowser, LLVM and MLIR;
ctbrowser depends on none of them. A machine with no LLVM installed must still
build and run the engine, and `ctbrowser/CMakeLists.txt` fails the configure if
LLVM or MLIR targets ever appear in its own scope rather than trusting the rule.

## Building

The compiler implementation is organized by responsibility; see the
[source layout](docs/source-layout.md) for the import, analysis, lowering and
test directories.

Inside the monorepo:

```bash
cmake --preset browser+compiler      # from ctbrowser/
cmake --build --preset browser+compiler
```

Standalone, against an installed engine:

```bash
cmake -S ctcompile -B build-ctcompile -Dctbrowser_DIR=<prefix>/lib/cmake/ctbrowser
```

The standalone form is not a convenience — it is what proves the project
boundary is real rather than a directory name.

MLIR is behind `CTCOMPILE_ENABLE_MLIR`; the full devbox gate enables it.
Engine-only builds do not require LLVM. The version is pinned in
`cmake/LLVMVersion.cmake`; see `docs/LLVMUpgrade.md`.

## What lands next

Native Bootstrap still needs component and host object representations, typed
host publication and its library/error paths. Partial evaluation needs explicit
effect contracts and region splitting to retain runtime work within otherwise
static initialization. See the [Bootstrap Data probe](docs/bootstrap-data-probe.md)
for the current source-derived execution boundary.
