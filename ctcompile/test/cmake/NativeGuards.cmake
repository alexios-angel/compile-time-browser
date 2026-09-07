# ============================================================================
# THE PDLL GUARD. Appended as ONE block, per part 23's Appendix A.3.
#
# A .pdll in this tree may now name a `ctjs` operation, because
# lib/CTNative/Lowering/CMakeLists.txt puts our include roots on mlir-pdll's
# search path. That is only safe with utils/pdll-strict.sh in front of the
# tool, because mlir-pdll EXITS 0 on two things that silently change what a
# pattern matches - an attribute literal of an unregistered dialect (which it
# drops, after saying so on stderr) and an operation name ODS never heard of
# (which it does not mention at all).
#
# The guard is installed as MLIR_PDLL_TABLEGEN_EXE, so every pattern in the
# tree already goes through it and a passing build is some evidence. It is not
# enough: the guard keys off text mlir-pdll prints, so a release that reworded
# the diagnostic would turn it off silently. This test runs it over three
# fixtures under PDLL/ that are never compiled into anything, and asserts the
# RAW tool's behaviour beside the guard's - so the day upstream closes a hole,
# the gate says which one rather than passing on.
#
# It costs six mlir-pdll runs, about 70 ms.
if(CTCOMPILE_ENABLE_MLIR)
  # MLIR_PDLL_TABLEGEN_EXE is the two-element list ctcompile/CMakeLists.txt
  # made of it: the guard, then the real tool (a generator expression when MLIR
  # was found as an installed package, which add_test expands).
  list(LENGTH MLIR_PDLL_TABLEGEN_EXE _pdll_parts)
  if(NOT _pdll_parts EQUAL 2)
    message(FATAL_ERROR "ctcompile: MLIR_PDLL_TABLEGEN_EXE is '${MLIR_PDLL_TABLEGEN_EXE}', not "
                        "the guard-and-tool pair ctcompile/CMakeLists.txt sets. The PDLL guard "
                        "is not installed and no pattern in this tree is checked.")
  endif()
  list(GET MLIR_PDLL_TABLEGEN_EXE 0 _pdll_guard)
  list(GET MLIR_PDLL_TABLEGEN_EXE 1 _pdll_real)
  # THE SAME TWO ROOTS THE PATTERNS THEMSELVES GET. Comma-separated because a
  # -D value carrying semicolons is one list argument CMake would split at the
  # wrong level; check-pdll-guard.cmake splits it back.
  string(REPLACE ";" "," _pdll_includes "${PROJECT_SOURCE_DIR}/include;${MLIR_INCLUDE_DIRS}")
  add_test(NAME ctcompile_pdll_guard
           COMMAND ${CMAKE_COMMAND}
                   -DGUARD=${_pdll_guard}
                   -DPDLL=${_pdll_real}
                   -DDIR=${CMAKE_CURRENT_SOURCE_DIR}/PDLL
                   "-DINCLUDES=${_pdll_includes}"
                   -DWORK=${CMAKE_CURRENT_BINARY_DIR}
                   -P ${CMAKE_CURRENT_SOURCE_DIR}/check-pdll-guard.cmake)
endif()

# === THE DIFFERENTIAL GATE COMPARES VALUES THAT ARE NOT NUMBERS ===
#
# One appended block, per part 23's Appendix A.3. Everything it needs is its
# own file: native-values-fixture.js (a program with a Number, a Boolean, a
# String, `null` and `undefined` in its globals) and
# native-values-fixture.emitc.mlir (the hand-written EmitC module that is the
# SPECIFICATION of what the lowering has to emit for it - including the
# `ctnative::print_*` helpers, which LowerToEmitC.cpp does not emit yet).
#
# WHY A HAND-WRITTEN MODULE and not the pipeline: the same reason
# native-fixture.emitc.mlir is hand-written. The gate is what defines the tier,
# so it is proved on something BEFORE there is a lowering to gate - otherwise
# the first program with a Boolean global would arrive with the gate and the
# lowering both unproven, and neither could be blamed. When the lowering learns
# these forms, its output for native-values-fixture.js should be this module up
# to value names, and nothing here has to change.
if(COMMAND ctcompile_add_native_unit AND COMMAND ctcompile_add_compile_clean)
  set(_values_module "${CMAKE_CURRENT_SOURCE_DIR}/native-values-fixture.emitc.mlir")
  set(_values_js "${CMAKE_CURRENT_SOURCE_DIR}/native-values-fixture.js")

  # THE GATE, over all five kinds at once.
  ctcompile_add_native_unit(values "${_values_module}" "${_values_js}")

  # ONE NEGATIVE PROOF PER KIND, because a mutation that suits a Number suits
  # nothing else: `<g> = <g> + 1` does not compile on a bool, has no meaning on
  # a std::string, and has nothing to act on for a global whose proven type is
  # `undefined` or `null` - those have no storage. Each of these must FAIL, and
  # must fail NAMING ITS GLOBAL; the driver's EXPECT_FAILURE is what asserts
  # the second half. See check-native-unit.cmake's MUTATE_AS block.
  #
  # `third` AND NOT `answer` FOR THE NUMBER, and that is a fact about the
  # mechanism rather than a preference: the number mutation is an insertion in
  # front of the first print, so it is invisible to a global the printing has
  # already read - which the FIRST global in sorted order always has been. The
  # driver now refuses that case by name, and the next test is the proof.
  ctcompile_add_native_unit(values_number "${_values_module}" "${_values_js}"
    -DMUTATE=third -DMUTATE_AS=number "-DEXPECT_FAILURE=global 'third' differs")
  ctcompile_add_native_unit(values_boolean "${_values_module}" "${_values_js}"
    -DMUTATE=no -DMUTATE_AS=boolean "-DEXPECT_FAILURE=global 'no' differs")
  ctcompile_add_native_unit(values_string "${_values_module}" "${_values_js}"
    -DMUTATE=greeting -DMUTATE_AS=string "-DEXPECT_FAILURE=global 'greeting' differs")
  ctcompile_add_native_unit(values_undefined "${_values_module}" "${_values_js}"
    -DMUTATE=missing -DMUTATE_AS=undefined "-DEXPECT_FAILURE=global 'missing' differs")
  ctcompile_add_native_unit(values_null "${_values_module}" "${_values_js}"
    -DMUTATE=nothing -DMUTATE_AS=null "-DEXPECT_FAILURE=global 'nothing' differs")

  # AND THE GUARD ON THE MUTATION ITSELF. Mutating the first global in sorted
  # order used to PASS - the off-by-one landed after the printing had read the
  # value, so the binary printed the right answer and the negative test could
  # say only "the gate PASSED where it had to fail". This asserts the driver
  # now refuses that mutation and says why, so the next author does not spend
  # the afternoon the last one did.
  ctcompile_add_native_unit(values_vacuous_mutation "${_values_module}" "${_values_js}"
    -DMUTATE=answer -DMUTATE_AS=number
    "-DEXPECT_FAILURE=VACUOUS")

  # THE SYMBOL CHECK, on a binary built from THIS module. The nm check knows
  # nothing about values, so one control per module is all it can prove and
  # five copies of it would be five copies of one proof; this is the module's
  # own C++ plus one object that reaches the interpreter, and the gate must
  # reject it.
  set(_values_cpp "${CMAKE_CURRENT_BINARY_DIR}/native-values-fixture.generated.cpp")
  add_custom_command(
    OUTPUT "${_values_cpp}"
    COMMAND $<TARGET_FILE:ctjs-translate> --mlir-to-cpp "${_values_module}" -o "${_values_cpp}"
    DEPENDS "${_values_module}" ctjs-translate
    COMMENT "Emitting native-values-fixture.emitc.mlir to C++ for the VM-linked control"
    VERBATIM)
  add_executable(ctcompile-test-native-values-vm-linked NativeVmLinked.cpp "${_values_cpp}")
  target_link_libraries(ctcompile-test-native-values-vm-linked PRIVATE ctbrowser::ctbrowser)
  target_compile_features(ctcompile-test-native-values-vm-linked PRIVATE cxx_std_23)
  target_compile_options(ctcompile-test-native-values-vm-linked PRIVATE
    -O2 -Wall -Wextra -Werror -pedantic -ffp-contract=off)
  ctcompile_add_native_unit(values_vm_linked "${_values_module}" "${_values_js}"
    -DPREBUILT=$<TARGET_FILE:ctcompile-test-native-values-vm-linked>
    "-DEXPECT_FAILURE=reaches the interpreter")

  # PHASE 63 STEP 7 over the same module: the value-printing helpers the
  # lowering has to emit must compile clean on BOTH toolchains, not just on the
  # one the gate happens to use, and every definition must carry its
  # provenance comment.
  ctcompile_add_compile_clean(values "${_values_module}")
endif()

# ============================================================================
# THE REFACTOR GOLDENS' OWN GATE. Appended as ONE block, per part 23's
# Appendix A.3.
#
# docs/refactor-goldens.md is the procedure: snapshot the eight post-pipeline
# modules and the four census JSONs before a change, `cmp` them after it, and
# for a PURE REFACTOR not one byte may move. It is deliberately NOT a
# checked-in golden - the artefacts embed the absolute paths the build passed
# to ctjs-translate, and a golden of derived output has no regeneration rule
# that cannot be abused - so nothing in ctest can run the comparison itself.
#
# WHAT ctest CAN DO is prove the INSTRUMENT works, which is the half that
# would otherwise rot silently:
#
#   1. the pipeline is byte-deterministic - re-running it over an unchanged
#      tree reproduces numeric.pipeline.emitc.mlir exactly. Without that, a
#      before/after comparison reports differences that mean nothing and the
#      first person to meet one learns to ignore the tool.
#   2. the comparison bites - one byte appended to a saved copy is caught.
#
# A comparison nobody has watched fail is not a comparison, which is the same
# reason native-claims.py carries floor_bites and silent_drop_caught.
#
# bash EXPLICITLY, not the shebang: core.filemode is false in this checkout
# (a DrvFs working tree), so git does not carry the execute bit and a
# COMMAND that relied on it would fail with "Permission denied" on a fresh
# clone. CMAKE is passed through the environment because the selftest re-runs
# native-pipeline.cmake and `cmake` need not be on PATH.
if(CTCOMPILE_ENABLE_MLIR AND TARGET ctjs-translate AND TARGET ctjs-opt AND UNIX)
  find_program(CTCOMPILE_BASH NAMES bash)
  if(NOT CTCOMPILE_BASH)
    message(FATAL_ERROR "ctcompile: bash not found - native-snapshot.sh cannot be gated, "
                        "so the refactor goldens would have no proof they still work")
  endif()
  add_test(NAME ctcompile_native_snapshot_selftest
           COMMAND ${CTCOMPILE_BASH} ${CMAKE_CURRENT_SOURCE_DIR}/native-snapshot.sh
                   selftest ${CMAKE_BINARY_DIR})
  set_tests_properties(ctcompile_native_snapshot_selftest PROPERTIES
                       ENVIRONMENT "CMAKE=${CMAKE_COMMAND}")
endif()

# Source invocation recovery is structural only until native admission and
# emission consume both completions. Keep its original checks available for rollback.
if(CTCOMPILE_ENABLE_MLIR)
  add_executable(ctcompile-test-exception-recovery ExceptionRecovery.cpp)
  target_link_libraries(ctcompile-test-exception-recovery
    PRIVATE ctcompile::ctnative-lowering ctcompile::ctjs-lowering ctcompile::ctjs-import)
  ctcompile_target(ctcompile-test-exception-recovery)
  add_test(NAME ctcompile_exception_recovery
    COMMAND ctcompile-test-exception-recovery
      "${CMAKE_CURRENT_SOURCE_DIR}/CTJS/Import/invocation-state.mlir")
endif()
