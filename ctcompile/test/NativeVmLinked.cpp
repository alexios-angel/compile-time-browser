// THE NEGATIVE CONTROL FOR THE NATIVE GATE'S SYMBOL CHECK - Phase 62½-D.
//
// Compiled into one binary beside the fixture's own emitted C++ (the same
// text the gate compiles standalone) and linked against ctbrowser::ctbrowser,
// so the result is the fixture's program in every respect but one: it reaches
// the interpreter. check-native-unit.cmake must reject that binary by naming
// a `ctbrowser::script::` symbol in it, and ctcompile_native_unit_fixture_vm_linked
// asserts that it does. A gate that could not tell this binary from the real
// thing would be a gate on nothing.
//
// NOT static AND NOT CALLED. External linkage keeps it in the object without
// --gc-sections, and the reference to context::to_string is what makes the
// linker pull the interpreter's objects out of the archive.
#include <ctbrowser/script/vm.hpp>

#include <string>

std::string ctcompile_native_gate_reaches_the_vm() {
    ctbrowser::script::context cx;
    return cx.to_string(cx.global("fib20"));
}
