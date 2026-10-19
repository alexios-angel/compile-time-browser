// A native program prints only what its JavaScript prints.
//
// globals.js sets three globals and calls no console method, so its binary
// writes nothing to stdout: main has no observation code, and the program
// built with GCC and with Clang prints nothing. The test-only option
// observe-globals makes main print each global as name=value before it
// returns, the convention the interpreter comparisons read, and the
// interpreter prints the same lines.
//
// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/globals.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc -o %t/silent.mlir
// RUN: FileCheck %s --check-prefix=SILENT --implicit-check-not=ctnative.not_native --implicit-check-not=printf --implicit-check-not=ctnative::print_ < %t/silent.mlir
// RUN: ctjs-opt %t/silent.mlir '--pass-pipeline=builtin.module(emitc.func(canonicalize,convert-scf-to-emitc,convert-arith-to-emitc,canonicalize,ctnative-prune-dead-stores,canonicalize))' | ctjs-translate --mlir-to-cpp > %t/silent.cpp
// RUN: %gxx -O2 %t/silent.cpp -o %t/silent-gcc
// RUN: %t/silent-gcc > %t/silent-gcc.out
// RUN: test ! -s %t/silent-gcc.out
// RUN: %clangxx -O2 %t/silent.cpp -o %t/silent-clang
// RUN: %t/silent-clang > %t/silent-clang.out
// RUN: test ! -s %t/silent-clang.out
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/globals.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=observe-globals=true -o %t/observed.mlir
// RUN: ctjs-opt %t/observed.mlir '--pass-pipeline=builtin.module(emitc.func(canonicalize,convert-scf-to-emitc,convert-arith-to-emitc,canonicalize,ctnative-prune-dead-stores,canonicalize))' | ctjs-translate --mlir-to-cpp > %t/observed.cpp
// RUN: %gxx -O2 %t/observed.cpp -o %t/observed-gcc
// RUN: %t/observed-gcc | FileCheck %s --check-prefix=OBSERVED --match-full-lines
// RUN: %clangxx -O2 %t/observed.cpp -o %t/observed-clang
// RUN: %t/observed-clang | FileCheck %s --check-prefix=OBSERVED --match-full-lines
// RUN: %native_reference %t/globals.js | FileCheck %s --check-prefix=OBSERVED --match-full-lines

// SILENT: emitc.func @main(

// OBSERVED: n=5
// OBSERVED-NEXT: ok=true
// OBSERVED-NEXT: r=12

//--- globals.js
var n = 2 + 3;
var ok = n > 4;
var r = n * 2 + 2;
