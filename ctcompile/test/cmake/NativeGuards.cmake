# === THE DIFFERENTIAL GATE COMPARES VALUES THAT ARE NOT NUMBERS ===
#
# Scalars/values.emitc.mlir is the hand-written EmitC module that is the
# SPECIFICATION of what the lowering has to emit for a program with a Number,
# a Boolean, a String, `null` and `undefined` in its globals - including the
# `ctnative::print_*` helpers. Its RUN lines carry the gate over all five
# kinds, one negative proof per kind, the vacuous-mutation guard and the
# two-toolchain clean compile. What lit cannot do is LINK: the symbol check's
# control for this module is its own C++ plus one object that reaches the
# interpreter, built here and named on the module's VM-LINKED line.
if(CTCOMPILE_ENABLE_MLIR AND TARGET ctjs-translate)
  ctcompile_vm_linked_control(native-values
    "${CMAKE_CURRENT_SOURCE_DIR}/CTNative/Fixtures/Scalars/values.emitc.mlir")
endif()

# The refactor goldens are a manual procedure, not a test: docs/refactor-goldens.md.

# Source invocation recovery is structural only until native admission and
# emission consume both completions. Keep its original checks available for rollback.
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
