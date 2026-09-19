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
  set(_values_module "${CMAKE_CURRENT_SOURCE_DIR}/CTNative/Fixtures/Scalars/values.emitc.mlir")
  set(_values_cpp "${CMAKE_CURRENT_BINARY_DIR}/native-values-fixture.generated.cpp")
  add_custom_command(
    OUTPUT "${_values_cpp}"
    COMMAND $<TARGET_FILE:ctjs-translate> --mlir-to-cpp "${_values_module}" -o "${_values_cpp}"
    DEPENDS "${_values_module}" ctjs-translate
    COMMENT "Emitting native-values-fixture.emitc.mlir to C++ for the VM-linked control"
    VERBATIM)
  add_executable(ctcompile-test-native-values-vm-linked Runtime/Reference/VmLinked.cpp "${_values_cpp}")
  target_link_libraries(ctcompile-test-native-values-vm-linked PRIVATE ctbrowser::ctbrowser)
  target_compile_features(ctcompile-test-native-values-vm-linked PRIVATE cxx_std_23)
  target_compile_options(ctcompile-test-native-values-vm-linked PRIVATE
    -O2 -Wall -Wextra -Werror -pedantic -ffp-contract=off)
endif()

# ============================================================================
# THE REFACTOR GOLDENS ARE A PROCEDURE, NOT A TEST. docs/refactor-goldens.md:
# copy the post-pipeline modules and the census JSONs out of this build tree
# before a change (`cp build/ctcompile/test/*.mlir build/ctcompile/test/
# native-claims-*.json /tmp/before`), rebuild, copy again, `diff -r` the two.
# For a PURE REFACTOR not one byte may move. Nothing in ctest can run it: the
# artefacts embed the absolute paths the build passed to ctjs-translate, and a
# golden of derived output has no regeneration rule that cannot be abused. A
# 271-line snapshot.sh with a selftest wrapped exactly that cp and cmp until
# 2026-09-15; the pipeline's determinism is what a noisy diff over an unchanged
# tree would report, and no separate instrument is needed to see it.

# Source invocation recovery is structural only until native admission and
# emission consume both completions. Keep its original checks available for rollback.
if(CTCOMPILE_ENABLE_MLIR)
  add_executable(ctcompile-test-exception-recovery
    CTNative/ExceptionRecovery/Main.cpp
    CTNative/ExceptionRecovery/Completion.cpp
    CTNative/ExceptionRecovery/Completion/DOMURI.cpp
    CTNative/ExceptionRecovery/Completion/ClassTransactions.cpp
    CTNative/ExceptionRecovery/Guards.cpp)
  target_link_libraries(ctcompile-test-exception-recovery
    PRIVATE ctcompile::ctnative-lowering ctcompile::ctjs-lowering ctcompile::ctjs-import
    # Guards.cpp parses its fixtures itself; the lowering library no longer
    # carries MLIRParser for it (its PDL patterns were the only parser user).
    MLIRParser)
  ctcompile_target(ctcompile-test-exception-recovery)
  add_test(NAME ctcompile_exception_recovery
    COMMAND ctcompile-test-exception-recovery
      "${CMAKE_CURRENT_SOURCE_DIR}/CTJS/Import/invocation-state.mlir")
endif()
