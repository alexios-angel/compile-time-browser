# ctcompile

The whole-application compiler: an application directory in, a self-contained
native executable out.

It is a **stub today**. `ctcompile --version` reports what it was built
against and nothing else compiles yet. The project exists at this size on
purpose — the repository split (Phase -1) had to be finished and verified
before compiler work started, and a sibling project that does not build is not
evidence of anything.

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

MLIR is behind `CTCOMPILE_ENABLE_MLIR`, OFF until Phase 7 has something to do
with it. The version is pinned in the monorepo's `cmake/LLVMVersion.cmake`; see
`docs/LLVMUpgrade.md`.

## What lands next

Phase 0 — the inventories the compiler is built on, and the one that sizes
everything else is the bytecode table: every opcode with its operands,
allocation, GC, throwing, re-entrancy and suspension behaviour, as an X-macro
`.def` the VM's own decoder then consumes, so the compiler's table and the
interpreter's cannot drift.
