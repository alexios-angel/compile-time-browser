# === PHASE 62½-D: the native gate ===
#
# THE TEST THAT DEFINES "NATIVE" - part 24 §1.2 and Phase 62½-D. One block,
# appended, per part 23's Appendix A.3. Everything it needs is its own file:
# native-fixture.js (the program), native-fixture.emitc.mlir (the hand-written
# EmitC module the Phase 62½-C lowering is expected to produce for it - the
# specification of that lowering's output shape), NativeReference.cpp (the
# interpreter's answers, printed the same way), NativeVmLinked.cpp (the
# negative control for the symbol check) and check-native-unit.cmake (the
# gate: emit, compile standalone, nm, run, compare).
#
# THE REFERENCE NEEDS NO MLIR. It is the interpreter's opinion of a JavaScript
# file, built wherever the type oracle is: the answer side of the gate should
# not depend on the compiler side existing, and a lowering author wants it on a
# box with no LLVM too.
add_executable(ctcompile-test-native-reference NativeReference.cpp)
target_link_libraries(ctcompile-test-native-reference PRIVATE ctbrowser::ctbrowser)
ctcompile_target(ctcompile-test-native-reference)

if(CTCOMPILE_ENABLE_MLIR AND TARGET ctjs-translate)
  # nm, the one CMake found beside the compiler at configure time (llvm-nm for
  # the pinned clang). A missing one is said out loud rather than skipped: this
  # gate IS the definition of native, and a suite without it is green on nothing.
  set(_native_nm "${CMAKE_NM}")
  if(NOT _native_nm)
    find_program(_native_nm NAMES llvm-nm nm)
  endif()
  if(NOT _native_nm)
    # FATAL, not WARNING: `ctcompile_add_native_unit` guards ~20 tests, and a
    # skip here unregisters all of them while ctest still reports green - a
    # suite green on nothing, which is what the comment above forbids. The
    # Phase 63 Step 7 block below treats a missing compiler the same way.
    message(FATAL_ERROR "ctcompile: no nm found - the Phase 62½-D native gate cannot be registered, and it IS the definition of native")
  else()
    # ONE FUNCTION, so a second fixture is one line: the module, the program,
    # and any of the driver's negative-proof switches after them.
    function(ctcompile_add_native_unit name module js)
      add_test(NAME ctcompile_native_unit_${name}
               COMMAND ${CMAKE_COMMAND}
                       -DTRANSLATE=$<TARGET_FILE:ctjs-translate>
                       -DMODULE=${module}
                       -DJS=${js}
                       -DCXX=${CMAKE_CXX_COMPILER}
                       -DNM=${_native_nm}
                       -DREFERENCE=$<TARGET_FILE:ctcompile-test-native-reference>
                       -DVM_LINKED=$<TARGET_FILE:ctcompile-test-type-oracle>
                       -DWORK=${CMAKE_CURRENT_BINARY_DIR}
                       -DNAME=${name}
                       ${ARGN}
                       -P ${CMAKE_CURRENT_SOURCE_DIR}/check-native-unit.cmake)
    endfunction()

    set(_native_module "${CMAKE_CURRENT_SOURCE_DIR}/native-fixture.emitc.mlir")
    set(_native_js "${CMAKE_CURRENT_SOURCE_DIR}/native-fixture.js")

    # THE GATE.
    ctcompile_add_native_unit(fixture "${_native_module}" "${_native_js}")

    # NEGATIVE PROOF 1: one global off by one must FAIL, naming the global. The
    # driver runs itself and passes only if the child failed with that name in
    # its output - see EXPECT_FAILURE in the driver for why not WILL_FAIL.
    ctcompile_add_native_unit(fixture_off_by_one "${_native_module}" "${_native_js}"
      -DMUTATE=fib20 "-DEXPECT_FAILURE=global 'fib20' differs")

    # NEGATIVE PROOF 2: the same C++, plus one object that reaches the
    # interpreter, linked against the engine - must FAIL the symbol check. The
    # C++ is emitted at build time by the same emitter the gate uses, so this
    # binary is the fixture's program in every respect but the one that matters.
    set(_native_cpp "${CMAKE_CURRENT_BINARY_DIR}/native-fixture.generated.cpp")
    add_custom_command(
      OUTPUT "${_native_cpp}"
      COMMAND $<TARGET_FILE:ctjs-translate> --mlir-to-cpp "${_native_module}" -o "${_native_cpp}"
      DEPENDS "${_native_module}" ctjs-translate
      COMMENT "Emitting native-fixture.emitc.mlir to C++ for the VM-linked control"
      VERBATIM)
    add_executable(ctcompile-test-native-vm-linked NativeVmLinked.cpp "${_native_cpp}")
    target_link_libraries(ctcompile-test-native-vm-linked PRIVATE ctbrowser::ctbrowser)
    target_compile_features(ctcompile-test-native-vm-linked PRIVATE cxx_std_23)
    # THE GATE'S OWN FLAGS, not ctcompile_target's: this is the same text under
    # the same compile the gate performs, plus one object and one library.
    target_compile_options(ctcompile-test-native-vm-linked PRIVATE
      -O2 -Wall -Wextra -Werror -pedantic -ffp-contract=off)
    ctcompile_add_native_unit(fixture_vm_linked "${_native_module}" "${_native_js}"
      -DPREBUILT=$<TARGET_FILE:ctcompile-test-native-vm-linked>
      "-DEXPECT_FAILURE=reaches the interpreter")
  endif()
endif()

# === PHASE 62½-C+D: JavaScript through the native pipeline and the gate ===
#
# The programs that go all the way. native-pipeline.cmake runs the closed
# world, the lift and the lowering over a JavaScript file and refuses to write
# a module while any function carries a diagnostic; the gate then compiles it
# with no ctbrowser library, runs it, and compares every numeric global with
# the interpreter. One function, one line per program.
if(CTCOMPILE_ENABLE_MLIR AND TARGET ctjs-translate AND TARGET ctjs-opt
   AND COMMAND ctcompile_add_native_unit)
  # PHASE 63 STEP 7: the generated file compiles clean on BOTH toolchains the
  # plan names, and every definition carries a provenance comment. Two
  # compilers are required, not "whatever is found": a gate that silently
  # drops a compiler passes vacuously (check-compile-clean.cmake).
  find_program(CTCOMPILE_CLEAN_GXX NAMES g++-13 g++)
  find_program(CTCOMPILE_CLEAN_CLANGXX NAMES clang++-18 clang++)
  if(NOT CTCOMPILE_CLEAN_GXX OR NOT CTCOMPILE_CLEAN_CLANGXX)
    message(FATAL_ERROR "Phase 63 Step 7 needs both g++ and clang++ on PATH; found g++='${CTCOMPILE_CLEAN_GXX}' clang++='${CTCOMPILE_CLEAN_CLANGXX}'")
  endif()
  function(ctcompile_add_compile_clean name module)
    add_test(NAME ctcompile_compile_clean_${name}
             COMMAND ${CMAKE_COMMAND}
                     -DTRANSLATE=$<TARGET_FILE:ctjs-translate>
                     -DMODULE=${module}
                     "-DCOMPILERS=${CTCOMPILE_CLEAN_GXX},${CTCOMPILE_CLEAN_CLANGXX}"
                     -DWORK=${CMAKE_CURRENT_BINARY_DIR}
                     -DNAME=${name}
                     ${ARGN}
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/check-compile-clean.cmake)
  endfunction()
  # the hand-written fixture module, and the negative proof
  ctcompile_add_compile_clean(fixture "${CMAKE_CURRENT_SOURCE_DIR}/native-fixture.emitc.mlir")
  ctcompile_add_compile_clean(fixture_unused_variable "${CMAKE_CURRENT_SOURCE_DIR}/native-fixture.emitc.mlir"
    -DMUTATE=1 "-DEXPECT_FAILURE=refused the generated file")
  function(ctcompile_add_native_pipeline name js)
    set(_module "${CMAKE_CURRENT_BINARY_DIR}/${name}.pipeline.emitc.mlir")
    add_custom_command(
      OUTPUT "${_module}"
      COMMAND ${CMAKE_COMMAND}
              -DTRANSLATE=$<TARGET_FILE:ctjs-translate>
              -DOPT=$<TARGET_FILE:ctjs-opt>
              -DSOURCE=${js}
              -DOUTPUT=${_module}
              -P "${CMAKE_CURRENT_SOURCE_DIR}/native-pipeline.cmake"
      DEPENDS "${js}" "${CMAKE_CURRENT_SOURCE_DIR}/native-pipeline.cmake" ctjs-translate ctjs-opt
      COMMENT "Lowering ${name} through the native pipeline")
    add_custom_target(ctcompile-native-pipeline-${name} ALL DEPENDS "${_module}")
    ctcompile_add_native_unit(pipeline_${name} "${_module}" "${js}")
    # AND THE NEGATIVE PROOF, when the caller names a global to break.
    #
    # Every gate here asserts that a GENERATED module agrees with the
    # interpreter; none of them asserted that the comparison can still fail.
    # It could not be registered before 2026-09-03: check-native-unit.cmake
    # anchored its off-by-one on the literal `std::printf(`, which only the
    # hand-written native-fixture.emitc.mlir emits, so -DMUTATE aborted at
    # generate time on every pipeline program. That abort is a FATAL_ERROR
    # rather than a red test, which is why the absence read as a gate nobody
    # had got round to rather than as a driver that could not do it.
    #
    # The global must be one the printing prelude loads AFTER the first
    # printf; the driver checks that itself and says so, because a mutation
    # inserted after the load is invisible and would pass for the wrong
    # reason.
    if(ARGN)
      list(GET ARGN 0 _mutate_global)
      ctcompile_add_native_unit(pipeline_${name}_off_by_one "${_module}" "${js}"
        -DMUTATE=${_mutate_global}
        "-DEXPECT_FAILURE=global '${_mutate_global}' differs")
    endif()
    # PHASE 62½-E: the same module with the printing policy applied goes
    # through the same gate (62½-D stays green), and the printing gate proves
    # the rest: auto and pins pair up, the two files differ only in spelling,
    # the byte counts are reported, and the pin mutation fails the build
    # naming the JavaScript site (check-print-deduced.cmake).
    set(_deduced "${CMAKE_CURRENT_BINARY_DIR}/${name}.pipeline.deduced.emitc.mlir")
    set(_mutated "${CMAKE_CURRENT_BINARY_DIR}/${name}.pipeline.mutated.emitc.mlir")
    add_custom_command(
      OUTPUT "${_deduced}"
      COMMAND $<TARGET_FILE:ctjs-opt> --ctnative-print-deduced "${_module}" --mlir-print-debuginfo -o "${_deduced}"
      DEPENDS "${_module}" ctjs-opt
      COMMENT "Applying the printing policy to ${name}")
    add_custom_command(
      OUTPUT "${_mutated}"
      COMMAND $<TARGET_FILE:ctjs-opt> "--ctnative-print-deduced=mutate=1" "${_module}" --mlir-print-debuginfo -o "${_mutated}"
      DEPENDS "${_module}" ctjs-opt
      COMMENT "Mutating one pin in ${name}")
    add_custom_target(ctcompile-native-deduced-${name} ALL DEPENDS "${_deduced}" "${_mutated}")
    ctcompile_add_native_unit(pipeline_${name}_deduced "${_deduced}" "${js}")
    ctcompile_add_compile_clean(pipeline_${name} "${_module}")
    ctcompile_add_compile_clean(pipeline_${name}_deduced "${_deduced}")
    add_test(NAME ctcompile_print_deduced_${name}
             COMMAND ${CMAKE_COMMAND}
                     -DTRANSLATE=$<TARGET_FILE:ctjs-translate>
                     -DPLAIN=${_module}
                     -DDEDUCED=${_deduced}
                     -DMUTATED=${_mutated}
                     -DCXX=${CMAKE_CXX_COMPILER}
                     -DWORK=${CMAKE_CURRENT_BINARY_DIR}
                     -DNAME=${name}
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/check-print-deduced.cmake)
  endfunction()
  # numbers and booleans, no functions
  ctcompile_add_native_pipeline(numeric "${CMAKE_CURRENT_SOURCE_DIR}/native-pipeline-fixture.js" total)
  # the gate's own eight functions: recursion, loops, a boolean predicate
  ctcompile_add_native_pipeline(functions "${CMAKE_CURRENT_SOURCE_DIR}/native-fixture.js" sum100)
  # Phase 56: object literals with closed shapes, on the stack
  ctcompile_add_native_pipeline(structs "${CMAKE_CURRENT_SOURCE_DIR}/native-struct-fixture.js" swap_answer)
  # Phase 57A: dense, uniformly numeric array literals, as std::vector<double>
  ctcompile_add_native_pipeline(arrays "${CMAKE_CURRENT_SOURCE_DIR}/native-array-fixture.js" sum)
endif()

# === PART 24 §3.3: the deduction probe ===
#
# What the two devbox compilers accept, measured every build rather than
# tabulated once: constrained auto parameters, templated lambdas, aggregate
# CTAD, a visited variant, static_assert pins over deduced declarations, and
# the absence of two C++23 library features. Phases 56 to 62½ plan against
# this file. One appended block, per Appendix A.3.
add_test(NAME ctcompile_deduction_probe
         COMMAND ${CMAKE_COMMAND}
                 -DPROBE=${CMAKE_CURRENT_SOURCE_DIR}/../probes/deduction.cpp
                 "-DCOMPILERS=/usr/bin/g++;/usr/bin/clang++;${CMAKE_CXX_COMPILER}"
                 -DWORK=${CMAKE_CURRENT_BINARY_DIR}
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/check-deduction-probe.cmake)

# === PHASE 63 STEPS 2 AND 3: the claimed set, per corpus ===
# ============================================================================
# Part 24 Phase 63 Step 2 asks for per-function selection WITH A REASON and
# says of the refusals "that list is the roadmap"; Step 3 asks how many
# functions the backend claims. tools/check/native-claims.py runs the native
# pipeline over one program and sorts every function into claimed, refused
# (with the reason it carries) or never imported, then asserts two things the
# other gates cannot see:
#
#   * every function not claimed carries a `ctnative.not_native` reason. One
#     left behind WITHOUT a diagnostic is the silent drop part 24 SS2 forbids,
#     and no fixture gate would notice, because fixtures are programs the
#     compiler already handles.
#   * the run is not vacuous - a corpus that failed to parse would otherwise
#     report "0 refused".
#
# --min-claimed is a FLOOR AGAINST SILENT NARROWING, set at the number
# measured on 2026-09-02 (073ea1f). A change that refuses functions it used to
# claim fails here while every fixture stays green. Raising a floor belongs in
# the same commit that earns it.
#
# The corpora are the four Phase 54B and 55O already use. The two large
# bundles take about eight seconds and 1.5 GB each; that is the price of
# measuring the roadmap on real code rather than on the fixture.
#
# AND THE CEILING, WHICH THE THREE BUCKETS COULD NOT SEE. Two closed-world
# rules landed sound and far too coarse and took every global on every real
# corpus with them - bootstrap 0 of 37, p5 0 of 101, phaser 0 of 72, and not
# one ctjs.call_direct anywhere. This check did not move, because the
# functions that stopped being reachable were the ones that take a parameter
# and those were never claimed. It now reads --ctjs-resolve-globals' own
# counters, and prints the per-name refusal reasons as the roadmap for them.
if(CTCOMPILE_ENABLE_MLIR AND TARGET ctjs-translate AND TARGET ctjs-opt AND Python3_EXECUTABLE)
  # `resolved` and `direct` are floors under the closed world. They are 0 on
  # the three real corpora, and THE REASON WRITTEN HERE WAS WRONG. It said all
  # three are open programs - "bootstrap's UMD header passes `globalThis`/
  # `self` into the factory it calls, p5 constructs `Function`, phaser calls
  # `eval` with a computed string" - and named clause 5 as the ceiling. Clause
  # 5 is not the ceiling on any of them. Disabling it OUTRIGHT (returning
  # nullopt from dynamic_global_writes, 2026-09-03) leaves all three at 0
  # resolved and 0 ctjs.call_direct, with the rows reading:
  #
  #   bootstrap  37 of 37  never stored in this program - a host binding
  #   p5         96 of 101 the same, 4 bound outside the hoisting prologue,
  #                        1 bound to something other than a closure
  #   phaser     72 of 72  never stored in this program - a host binding
  #
  # A BUNDLE DECLARES NOTHING AT GLOBAL SCOPE. Each of these hands a factory's
  # result to one property of the window, so there is no global NAME for the
  # census to bind and `resolved` is 0 by clause 1 whatever clause 5 says. The
  # escape reasons these rows carried were decorations on a refusal clauses 1-3
  # make anyway. That does not relax clause 5 - the window proxy's `set` trap
  # really does define_global (Shell/bindings/window.cpp:880-892) - it says
  # clause 5 is not where a vendor bundle is lost.
  #
  # THAT OTHER HALF OF known_callee() IS NOW TAKEN, and `direct` is no longer 0
  # on any of them: --ctjs-resolve-globals rewrites a ctjs.call whose callee is
  # a ctjs.create_closure result, which needs no global name and therefore no
  # clause of the per-name census. Measured 2026-09-03: bootstrap 0 -> 21, p5
  # 0 -> 41, phaser 0 -> 48, of 21 / 42 / 50 such sites (the shortfall is the
  # arity and new.target guards, which are load-bearing).
  #
  # AND `claimed` DID NOT MOVE - 4, 37 and 43 before and after. Naming a callee
  # is upstream of admission, not admission: the functions those calls reach are
  # refused for their own reasons, which the census below prints. Said here
  # because a floor that rises while the number it exists for does not is
  # exactly the kind of green gate this file has been burned by.
  #
  # THE FOURTH FLOOR IS `lifted`, AND IT EXISTS BECAUSE THIS CHECK WAS BLIND.
  # --ctnative-lower-to-emitc's closure lift ALSO makes ctjs.call_direct - 3 on
  # bootstrap, 47 on p5, 3 on phaser - and always did. Counting only the
  # resolver's stage is what let "not one ctjs.call_direct emitted anywhere" be
  # written above about a pipeline that was emitting them.
  #
  # A zero floor still bites the day one of them earns a number: raising it is
  # then a visible act in the commit that earns it.
  #
  # PHASE 59 SLICE 1b MOVED NONE OF THESE EITHER, measured 2026-09-03 before
  # and after in one build directory: claimed 4 / 37 / 43, resolved 0 / 0 / 0,
  # direct 21 / 41 / 48, lifted 3 / 47 / 3 on bootstrap / p5 / phaser -
  # identical to the number. The slice carries a capture filled from the
  # enclosing closure only once the enclosing function has lifted, and the
  # refusal now spells the chain out to whatever stopped that. Walked closure
  # by closure over the callees a ctjs.call_direct reaches: bootstrap's 19 are
  # 3 lifted and 16 whose chain ends in a cell written EXACTLY ONCE from a
  # constant undefined - the hoisted `var`/`const` shape closure-refusals.mlir
  # pinned as HOISTED.
  #
  # THAT LEVER WAS PULLED - PART 24 PHASE 59 SLICE 2 STEP 1 - AND THE CLAIMED
  # COUNT DID NOT MOVE. The single dominating write took the reassigned-capture
  # refusal from 715 to 13 on phaser, 81 to 15 on p5 and 16 to 4 on bootstrap,
  # and the LIFT widened with it: `lifted` went 3 -> 46 on phaser and 47 -> 82
  # on p5, which is why those two floors below are raised and bootstrap's is
  # not. `claimed` stayed at 4, 37 and 43. A lifted capture is a parameter that
  # still needs a native carrier, and in a bundle those bindings are objects and
  # functions - so what the corpora are refused for NOW is where they were
  # always going to end up: on phaser, "it is stored into something that is not
  # an object literal made here" 316 -> 744, "it is passed as an argument"
  # 13 -> 136, "a method field of an object whose shape is not closed"
  # 236 -> 356. Those three are the next lever, not another clause about cells.
  # Owning UTF-8 strings (2026-09-05) raise admission to 19 / 39 / 43.
  # Resolver and lift counts stay unchanged; native-strings.md records the
  # measured scope, and the floors below now protect the additional functions.
  function(ctcompile_add_native_claims name js floor resolved direct lifted)
    add_test(NAME ctcompile_native_claims_${name}
             COMMAND ${Python3_EXECUTABLE}
                     ${CTBROWSER_MONOREPO_ROOT}/tools/check/native-claims.py
                     --translate $<TARGET_FILE:ctjs-translate>
                     --opt $<TARGET_FILE:ctjs-opt>
                     --corpus ${js}
                     --name ${name}
                     --json ${CMAKE_CURRENT_BINARY_DIR}/native-claims-${name}.json
                     --min-claimed ${floor}
                     --min-resolved ${resolved}
                     --min-direct ${direct}
                     --min-lifted-direct ${lifted})
  endfunction()
  # THE FIXTURE IS THE VACUITY CHECK: 9 of 9, so a run that reports nothing
  # claimed anywhere is not mistaken for a clean sheet.
  ctcompile_add_native_claims(fixture "${CMAKE_CURRENT_SOURCE_DIR}/native-fixture.js" 9 8 16 0)
  # TWO CLOSED PROGRAMS, ADDED BECAUSE THE FLOORS ABOVE ARE NOT ENOUGH.
  # differential.js and launcher.js are already compiled through the EmitC
  # backend by the blocks further up this file, so they cost nothing to fetch
  # and they are the only corpora here whose globals actually resolve: 56 of 72
  # and 9 of 10. A closed-world clause widened by one term takes those to zero
  # and this fails, which is precisely what did NOT happen when two of them
  # were - the three vendor bundles were already at zero and had nothing left
  # to lose.
  ctcompile_add_native_claims(differential "${CMAKE_CURRENT_SOURCE_DIR}/differential.js" 2 56 10 0)
  ctcompile_add_native_claims(launcher "${CMAKE_CURRENT_SOURCE_DIR}/launcher.js" 0 9 9 0)
  ctcompile_add_native_claims(bootstrap
    "${CTBROWSER_MONOREPO_ROOT}/ctbrowser/vendor/bootstrap/bootstrap.bundle.js" 19 0 21 208)
  ctcompile_add_native_claims(p5 "${CTBROWSER_MONOREPO_ROOT}/ctbrowser/vendor/p5/p5.js" 39 0 41 328)
  ctcompile_add_native_claims(phaser
    "${CTBROWSER_MONOREPO_ROOT}/ctbrowser/vendor/phaser/phaser.js" 45 0 48 283)

  # BOTH TEETH, PROVED. Each runs the same check as a child and passes only
  # when that child failed for the stated reason - not ctest's WILL_FAIL,
  # which a crash or a missing corpus would satisfy just as well.
  function(ctcompile_add_native_claims_negative name js)
    add_test(NAME ctcompile_native_claims_${name}
             COMMAND ${Python3_EXECUTABLE}
                     ${CTBROWSER_MONOREPO_ROOT}/tools/check/native-claims.py
                     --translate $<TARGET_FILE:ctjs-translate>
                     --opt $<TARGET_FILE:ctjs-opt>
                     --corpus ${js}
                     --name ${name}
                     ${ARGN})
  endfunction()
  # 1: the floor bites. The fixture claims 9 of 9; a floor of 10 must fail.
  ctcompile_add_native_claims_negative(floor_bites
    "${CMAKE_CURRENT_SOURCE_DIR}/native-fixture.js"
    --min-claimed 10 "--expect-failure=the native tier NARROWED")
  # 2: a function left behind with no diagnostic is caught. One refusal reason
  # is deleted from the lowered module, and the check must name the count.
  ctcompile_add_native_claims_negative(silent_drop_caught
    "${CTBROWSER_MONOREPO_ROOT}/ctbrowser/vendor/bootstrap/bootstrap.bundle.js"
    --mutate-drop-one-reason "--expect-failure=were dropped silently")
  # 3: the CLOSED-WORLD floor bites too. The fixture resolves 8 globals; a
  # floor of 9 must fail, and with the message that names the closed world
  # rather than the claimed set - the two are different numbers and a check
  # that confused them would have missed the regression this floor exists for.
  ctcompile_add_native_claims_negative(resolved_floor_bites
    "${CMAKE_CURRENT_SOURCE_DIR}/native-fixture.js"
    --min-claimed 9 --min-resolved 9 --min-direct 16
    "--expect-failure=the CLOSED WORLD NARROWED")
  # 4: and the pass cannot report a rewrite it did not make. Every
  # ctjs.call_direct is renamed before counting, so the remark says 16 and the
  # IR holds none.
  ctcompile_add_native_claims_negative(call_direct_counter_agrees
    "${CMAKE_CURRENT_SOURCE_DIR}/native-fixture.js"
    --mutate-drop-call-direct
    "--expect-failure=the counter and the rewrite disagree")
endif()
