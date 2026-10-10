# === Native checks over values that are not numbers ===
#
# Scalars/values.emitc.mlir is the hand-written EmitC module that specifies
# what the lowering must emit for a program with a Number, a Boolean, a
# String, `null` and `undefined` in its globals, including the
# `ctnative::print_*` helpers. Its RUN lines run the native check over all
# five kinds, one negative control per kind, the vacuous-mutation guard and
# the two-toolchain clean compile. What lit cannot do is link: the symbol
# check's negative control for this module is its own C++ plus one object
# that reaches the interpreter, built here and named on the module's
# VM-LINKED line.
if(CTCOMPILE_ENABLE_MLIR AND TARGET ctjs-translate)
  ctcompile_vm_linked_control(native-values
    "${CMAKE_CURRENT_SOURCE_DIR}/CTNative/Fixtures/Scalars/values.emitc.mlir")
endif()

# The refactor goldens are a manual procedure, not a test: docs/refactor-goldens.md.

# Exception-recovery unit test. Source invocation recovery is checked
# structurally until native admission and emission consume both completions.
if(CTCOMPILE_ENABLE_MLIR)
  ctcompile_unit_test(exception-recovery
    LIBS ctcompile::ctnative-lowering ctcompile::ctjs-lowering ctcompile::ctjs-import
    # Guards.cpp parses its fixtures itself; the lowering library no longer
    # carries MLIRParser for it (its PDL patterns were the only parser user).
    MLIRParser
    # Main.cpp's fixture argument, parsed by ctbrowser/tools/support/options.hpp.
    ctbrowser-options
    ARGS "${CMAKE_CURRENT_SOURCE_DIR}/CTJS/Import/invocation-state.mlir")
endif()
