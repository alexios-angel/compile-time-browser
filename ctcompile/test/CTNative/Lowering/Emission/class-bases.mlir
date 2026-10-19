// `ctnative.bases` on an emitc.class prints its public base classes, as a
// registry class whose callbacks keep an instance by its owner derives
// std::enable_shared_from_this (EmitC/Module.cpp). An empty name would print
// `public` with nothing after it, so the emitter refuses it.

// RUN: split-file %s %t
// RUN: ctjs-translate --mlir-to-cpp %t/one.mlir | FileCheck %s --check-prefix=ONE
// RUN: not ctjs-translate --mlir-to-cpp %t/empty.mlir 2>&1 | FileCheck %s --check-prefix=EMPTY

// ONE: class Widget : public std::enable_shared_from_this<Widget> {
// ONE-NEXT: public:
// ONE: double size;

// EMPTY: ctnative.bases must be a list of non-empty strings

//--- one.mlir
emitc.class @Widget attributes {ctnative.bases = ["std::enable_shared_from_this<Widget>"]} {
  emitc.field @size : f64
}

//--- empty.mlir
emitc.class @Widget attributes {ctnative.bases = [""]} {
  emitc.field @size : f64
}
