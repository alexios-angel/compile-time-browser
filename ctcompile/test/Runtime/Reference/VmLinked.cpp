//===- ctcompile/test/Runtime/Reference/VmLinked.cpp - VM-linked control --===//
//
// Negative control for the native symbol check.
//
// Compiled beside a fixture's emitted C++ and linked against
// ctbrowser::ctbrowser (targets ctcompile-test-native-vm-linked and
// ctcompile-test-native-values-vm-linked), so the binary is the fixture's
// program except that it reaches the interpreter. The VM-LINKED RUN line in
// functions.emitc.mlir / values.emitc.mlir must reject it for containing a
// `ctbrowser::script::` symbol (compilation-unit.py runs `nm -C`); a check
// that accepted it would prove nothing.
//
// The function below is neither static nor called. External linkage keeps it
// in the object without --gc-sections, and the reference to
// context::to_string is what makes the linker pull the interpreter's objects
// out of the archive.
//
//===----------------------------------------------------------------------===//
#include <ctbrowser/script/vm.hpp>

#include <string>

std::string ctcompile_native_gate_reaches_the_vm() {
    ctbrowser::script::context cx;
    return cx.to_string(cx.global("fib20"));
}
