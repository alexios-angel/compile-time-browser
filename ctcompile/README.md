# ctcompile

The whole-application compiler: an application directory in, a self-contained
native executable out.

The application driver is still incomplete. With MLIR enabled, `ctjs-translate`
and `ctjs-opt` import JavaScript and compile it through boxed or native EmitC
pipelines. The native subset emits standalone C++ and diagnoses unsupported
functions; a native program retains no VM dependency. Full native Bootstrap
execution is not yet established.

Native status - what the tier claims, the newest gate numbers and what lands
next - is `docs/HANDOFF.md`'s top entry, and the source-derived execution
boundary is the [Bootstrap Data probe](docs/bootstrap-data-probe.md); this file
does not keep a second copy of either.
What the default native pipeline runs (primitive precomputation and
unreachable-helper removal, both on by default with opt-outs) is
[native-optimization-defaults.md](docs/native-optimization-defaults.md); the
opt-in transforms and the research designs behind them
([modern PE](docs/native-modern-pe.md) among them) hang off the
[native optimization roadmap](docs/native-pe-roadmap.md).

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
