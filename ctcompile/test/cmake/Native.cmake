# === Native-target tests: build-side support for the lit checks ===
#
# "Native" means a program that compiles and runs with no ctbrowser engine
# linked in. The lit check CTNative/Checks/compilation-unit.py tests exactly
# that (emit C++, compile standalone, nm for engine symbols, run, compare with
# the interpreter), driven by RUN lines in each fixture's .test or hand-written
# .emitc.mlir. CMake owns only what needs the build: the interpreter reference
# binary, the VM-linked negative controls, the nm and the clean-compile
# toolchains passed to lit (Lit.cmake), and the pipeline modules the fixture
# .test files read from the build tree.
#
# The reference needs no MLIR. It is the interpreter's answer for a JavaScript
# file, built wherever the type oracle is: the answer side of a comparison
# should not depend on the compiler side existing, and it is useful on a
# machine with no LLVM too.
add_executable(ctcompile-test-native-reference)
# PROGRAM.js or --page PAGE.html, parsed by ctbrowser/tools/support/options.hpp.
target_link_libraries(ctcompile-test-native-reference PRIVATE ctbrowser::ctbrowser
  ctbrowser-options)
ctcompile_target(ctcompile-test-native-reference)

# The native runtime header, include/ctcompile/CTNative/Runtime/ctnative.hpp,
# compiled by the build. Every native program includes it, and nothing else
# compiles it under -Werror; a header that only generated programs compile
# would break unnoticed. Needs no MLIR, like the reference above; needs
# ctbrowser for its DOM section.
ctcompile_unit_test(native-runtime LIBS ctbrowser::ctbrowser)

# A VM-linked negative control: the C++ of <name>-fixture.emitc.mlir, emitted at
# build time by the same emitter the native check uses, plus one object that
# reaches the interpreter, linked against the engine; it must fail the
# no-engine-symbols check. It uses the native check's own compile flags, not
# ctcompile_target's, so it differs from a native build only by that object
# and library.
function(ctcompile_vm_linked_control name module)
  set(_cpp "${CMAKE_CURRENT_BINARY_DIR}/${name}-fixture.generated.cpp")
  add_custom_command(
    OUTPUT "${_cpp}"
    COMMAND $<TARGET_FILE:ctjs-translate> --mlir-to-cpp "${module}" -o "${_cpp}"
    DEPENDS "${module}" ctjs-translate
    COMMENT "Emitting ${name}-fixture.emitc.mlir to C++ for the VM-linked control"
    VERBATIM)
  add_executable(ctcompile-test-${name}-vm-linked "${_cpp}")
  target_link_libraries(ctcompile-test-${name}-vm-linked PRIVATE ctbrowser::ctbrowser)
  target_compile_features(ctcompile-test-${name}-vm-linked PRIVATE cxx_std_23)
  target_compile_options(ctcompile-test-${name}-vm-linked PRIVATE
    -O2 -Wall -Wextra -Werror -pedantic -ffp-contract=off)
endfunction()

if(CTCOMPILE_ENABLE_MLIR AND TARGET ctjs-translate)
  # nm is the one CMake found beside the compiler at configure time (llvm-nm
  # for clang). A missing nm is a configure error rather than a skip: the
  # no-engine-symbols check is what defines native, and the suite without it
  # would pass while testing nothing.
  set(_native_nm "${CMAKE_NM}")
  if(NOT _native_nm)
    find_program(_native_nm NAMES llvm-nm nm)
  endif()
  if(NOT _native_nm)
    message(FATAL_ERROR "ctcompile: no nm found - the Phase 62½-D native gate cannot run, and it IS the definition of native")
  endif()

  # Negative control for the VM-LINKED RUN line in functions.emitc.mlir: the
  # fixture's program linked against the engine, so the no-engine-symbols check
  # must fail on it.
  ctcompile_vm_linked_control(native
    "${CMAKE_CURRENT_SOURCE_DIR}/CTNative/Fixtures/ControlFlow/functions.emitc.mlir")
endif()

# === JavaScript fixtures through the native pipeline ===
#
# CTNative/Checks/pipeline.cmake runs the closed-world analysis, the closure
# lift and the lowering over a JavaScript file and refuses to write a module
# while any function carries a diagnostic. The fixture's .test then compiles
# the module with no ctbrowser library, runs it, and compares every numeric
# global with the interpreter (plus the deduced-type variant, the
# two-toolchain clean compile and the printing check; see
# CTNative/Fixtures/*/*.test). The module, its printing-policy variant and the
# pin mutation are built here because lit cannot build, and
# docs/refactor-goldens.md compares a refactor against the same files.
if(CTCOMPILE_ENABLE_MLIR AND TARGET ctjs-translate AND TARGET ctjs-opt)
  # compile-clean.py checks that generated C++ compiles warning-free under both
  # g++ and clang++. Both are required: a check that silently skips a missing
  # compiler would pass without testing it.
  find_program(CTCOMPILE_CLEAN_GXX NAMES g++-13 g++)
  find_program(CTCOMPILE_CLEAN_CLANGXX NAMES clang++-18 clang++)
  if(NOT CTCOMPILE_CLEAN_GXX OR NOT CTCOMPILE_CLEAN_CLANGXX)
    message(FATAL_ERROR "Phase 63 Step 7 needs both g++ and clang++ on PATH; found g++='${CTCOMPILE_CLEAN_GXX}' clang++='${CTCOMPILE_CLEAN_CLANGXX}'")
  endif()
  function(ctcompile_add_native_pipeline name js)
    set(_module "${CMAKE_CURRENT_BINARY_DIR}/${name}.pipeline.emitc.mlir")
    cmake_parse_arguments(_pipeline "PARTIAL_EVALUATE;PRECOMPUTE;SPECIALIZE;SUPERCOMPILE;DEFOREST;PRUNE_UNREACHABLE;NO_DEFAULT_OPTIMIZATIONS" "" "" ${ARGN})
    set(_optimization_args)
    foreach(_stage PRECOMPUTE PRUNE_UNREACHABLE)
      if(_pipeline_${_stage})
        list(APPEND _optimization_args "-D${_stage}=TRUE")
      endif()
    endforeach()
    if(_pipeline_NO_DEFAULT_OPTIMIZATIONS)
      list(APPEND _optimization_args "-DOPTIMIZE=FALSE")
    endif()
    add_custom_command(
      OUTPUT "${_module}"
      COMMAND ${CMAKE_COMMAND}
              -DTRANSLATE=$<TARGET_FILE:ctjs-translate>
              -DOPT=$<TARGET_FILE:ctjs-opt>
              -DSOURCE=${js}
              -DOUTPUT=${_module}
              -DPARTIAL_EVALUATE=${_pipeline_PARTIAL_EVALUATE}
              ${_optimization_args}
              -DSPECIALIZE=${_pipeline_SPECIALIZE}
              -DSUPERCOMPILE=${_pipeline_SUPERCOMPILE}
              -DDEFOREST=${_pipeline_DEFOREST}
              -P "${CMAKE_CURRENT_SOURCE_DIR}/CTNative/Checks/pipeline.cmake"
      DEPENDS "${js}" "${CMAKE_CURRENT_SOURCE_DIR}/CTNative/Checks/pipeline.cmake" ctjs-translate ctjs-opt
      COMMENT "Lowering ${name} through the native pipeline")
    # The same module with --ctnative-print-deduced applied, plus a mutated copy
    # with one deduced type pin made wrong; print-deduced.py checks that the
    # mutated copy fails and names the JavaScript source site.
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
    add_custom_target(ctcompile-native-pipeline-${name} ALL DEPENDS "${_module}" "${_deduced}" "${_mutated}")
    set_property(GLOBAL APPEND PROPERTY CTCOMPILE_NATIVE_PIPELINES ctcompile-native-pipeline-${name})
  endfunction()
endif()

# Registers one fixture, CTNative/Fixtures/<rel>: always the native pipeline
# (remaining arguments are pipeline options), and with CLAIMS <claimed>
# <resolved> <direct> <lifted> also the native-claims census. The two need
# different tools (the census also needs Python3), so each is added only when
# its helper function exists. Most fixtures are registered in
# NativeFixtures.cmake.
function(ctcompile_native_fixture name rel)
  cmake_parse_arguments(_fx "" "" "CLAIMS" ${ARGN})
  set(_js "${CMAKE_CURRENT_SOURCE_DIR}/CTNative/Fixtures/${rel}")
  if(COMMAND ctcompile_add_native_pipeline)
    ctcompile_add_native_pipeline(${name} "${_js}" ${_fx_UNPARSED_ARGUMENTS})
  endif()
  if(DEFINED _fx_CLAIMS AND COMMAND ctcompile_add_native_claims)
    ctcompile_add_native_claims(${name} "${_js}" ${_fx_CLAIMS})
  endif()
endfunction()

# Numbers and booleans, no functions.
ctcompile_native_fixture(numeric ControlFlow/globals.js)
# Eight functions: recursion, loops, a boolean predicate.
ctcompile_native_fixture(functions ControlFlow/functions.js)
# Object literals with closed shapes, lowered to stack structs.
ctcompile_native_fixture(structs Objects/struct.js)
# Object literals returned by value from the function that builds them.
ctcompile_native_fixture(records Objects/records.js)
# Dense, uniformly numeric array literals, as std::vector<double>.
ctcompile_native_fixture(arrays Objects/array.js)
# Array contents evidence currently admits only bounded local Number
# overwrites.
ctcompile_native_fixture(array_overwrite Objects/array/overwrite.js)
ctcompile_native_fixture(array_overwrite_unoptimized Objects/array/overwrite.js NO_DEFAULT_OPTIMIZATIONS)
ctcompile_native_fixture(array_read_overwrite Objects/array/read-overwrite.js)
ctcompile_native_fixture(array_read_overwrite_unoptimized Objects/array/read-overwrite.js NO_DEFAULT_OPTIMIZATIONS)
ctcompile_native_fixture(array_shrink Objects/array/shrink.js)
ctcompile_native_fixture(array_shrink_unoptimized Objects/array/shrink.js NO_DEFAULT_OPTIMIZATIONS)
ctcompile_native_fixture(array_overwrite_loop Objects/array/overwrite-loop.js)
ctcompile_native_fixture(array_overwrite_loop_unoptimized Objects/array/overwrite-loop.js NO_DEFAULT_OPTIMIZATIONS)

# === Native claims census, per corpus ===
# ============================================================================
# tools/check/native-claims.py runs the native pipeline over one program and
# sorts every function into claimed, refused (with its reason) or never
# imported. It fails when:
#
#   * a function that is not claimed carries no `ctnative.not_native` reason.
#     That is a silent drop, which no fixture test would notice, because
#     fixtures hold only programs the compiler already handles;
#   * the run is vacuous, e.g. a corpus that failed to parse and so reports
#     "0 refused";
#   * a count falls below its --min-* floor. Floors guard against silent
#     narrowing: a change that refuses functions that used to be claimed fails
#     here while every fixture stays green. Raise a floor in the commit that
#     earns it.
#
# The refusal reasons it prints are the list of what to implement next. The
# corpora are the ones the type and escape oracles use; the two large bundles
# take about eight seconds and 1.5 GB each.
#
# Besides `claimed`, it reads --ctjs-resolve-globals' own counters, because a
# closed-world rule that is too coarse can drop every resolved global without
# changing the claimed count (the functions that become unreachable are often
# ones that were never claimed).
if(CTCOMPILE_ENABLE_MLIR AND TARGET ctjs-translate AND TARGET ctjs-opt AND Python3_EXECUTABLE)
  # The browser drivers (CTNative/Browser/*.py, Ownership/data_session.py) are
  # lit tests. They need a compiler for the DOM's public API, which uses C++23
  # std::expected and so cannot use the older Clang/libstdc++ pair the
  # standalone scalar tests use. It is chosen here and passed to lit.cfg.py as
  # %dom_clang.
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set(_native_dom_clang ${CMAKE_CXX_COMPILER})
  else()
    find_program(_native_dom_clang NAMES clang++-23 clang++
                 HINTS ${CTBROWSER_MONOREPO_ROOT}/tools/clang-std-embed/bin REQUIRED)
  endif()
  # What each floor passed to ctcompile_add_native_claims measures (argument
  # order: claimed, resolved, direct, lifted):
  #   claimed  - functions the native backend admits;
  #   resolved - globals the closed-world census binds to one definition. It is
  #              0 on the vendor bundles: a bundle hands its factory's result to
  #              one window property and declares no global names, so there is
  #              nothing to bind;
  #   direct   - ctjs.call sites --ctjs-resolve-globals rewrote to
  #              ctjs.call_direct, including calls whose callee is a
  #              ctjs.create_closure result, which need no global name;
  #   lifted   - ctjs.call_direct sites created by the closure lift inside
  #              --ctnative-lower-to-emitc, which the resolver's count cannot
  #              see.
  # A zero floor still matters: when the count rises, raising the floor is a
  # visible change in the commit that earns it. A rising `direct` or `lifted`
  # does not imply a rising `claimed`: naming a callee comes before admission,
  # and the callee can still be refused for its own reasons, which the census
  # prints.
  #
  # Registers native_claims_<name>: native-claims.py over one corpus, with ARGN
  # as its remaining options.
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
  # Registers native_claims_<name> with --no-default-optimizations, so dead-code
  # pruning cannot lower the counts the floors protect. BOTH also registers
  # default_<name> with ordinary optimisation, which checks that pruned
  # functions are still counted in the source total.
  function(ctcompile_add_native_claims name js floor resolved direct lifted)
    cmake_parse_arguments(_claims "BOTH" "" "" ${ARGN})
    set(_floors --min-claimed ${floor} --min-resolved ${resolved} --min-direct ${direct}
                --min-lifted-direct ${lifted})
    ctcompile_add_native_claims_negative(${name} ${js} --no-default-optimizations
      --json ${CMAKE_CURRENT_BINARY_DIR}/native-claims-${name}.json ${_floors})
    if(_claims_BOTH)
      ctcompile_add_native_claims_negative(default_${name} ${js}
        --json ${CMAKE_CURRENT_BINARY_DIR}/native-claims-default_${name}.json ${_floors})
    endif()
  endfunction()
  # The fixture is the vacuity check: 9 of 9 claimed, so a run that claims
  # nothing anywhere is not mistaken for a clean result.
  ctcompile_add_native_claims(fixture "${CMAKE_CURRENT_SOURCE_DIR}/CTNative/Fixtures/ControlFlow/functions.js" 9 8 16 0 BOTH)
  # Two closed programs whose globals do resolve (56 of 72 and 9 of 10), unlike
  # the vendor bundles, which resolve none. They guard the `resolved` floor: a
  # closed-world rule widened by one term drops these to zero and fails here.
  # Both are already compiled through the EmitC backend by CoreAndBoxed.cmake
  # and Runtime.cmake.
  ctcompile_add_native_claims(differential "${CMAKE_CURRENT_SOURCE_DIR}/Runtime/Differential/differential.js" 2 56 10 0)
  ctcompile_add_native_claims(launcher "${CMAKE_CURRENT_SOURCE_DIR}/Runtime/Launcher/application.js" 0 9 9 0)
  ctcompile_add_native_claims(bootstrap
    "${CTBROWSER_MONOREPO_ROOT}/ctbrowser/vendor/bootstrap/bootstrap.bundle.js" 19 0 21 208 BOTH)
  ctcompile_add_native_claims(p5 "${CTBROWSER_MONOREPO_ROOT}/ctbrowser/vendor/p5/p5.js" 39 0 41 328 BOTH)
  ctcompile_add_native_claims(phaser
    "${CTBROWSER_MONOREPO_ROOT}/ctbrowser/vendor/phaser/phaser.js" 45 0 48 283 BOTH)

  # Negative controls for the claims check. Each runs it as a child and passes
  # only when the child fails with the stated message, unlike ctest's
  # WILL_FAIL, which a crash or a missing corpus would also satisfy.
  # 1: The claimed floor works. The fixture claims 9 of 9; a floor of 10 must
  # fail.
  ctcompile_add_native_claims_negative(floor_bites
    "${CMAKE_CURRENT_SOURCE_DIR}/CTNative/Fixtures/ControlFlow/functions.js"
    --min-claimed 10 "--expect-failure=the native tier NARROWED")
  # 2: A function left with no diagnostic is caught. One refusal reason is
  # deleted from the lowered module, and the check must report the count.
  ctcompile_add_native_claims_negative(silent_drop_caught
    "${CTBROWSER_MONOREPO_ROOT}/ctbrowser/vendor/bootstrap/bootstrap.bundle.js"
    --mutate-drop-one-reason "--expect-failure=were dropped silently")
  # 3: The closed-world floor works too. The fixture resolves 8 globals; a floor
  # of 9 must fail with the message about the closed world rather than the
  # claimed set, since the two are different numbers.
  ctcompile_add_native_claims_negative(resolved_floor_bites
    "${CMAKE_CURRENT_SOURCE_DIR}/CTNative/Fixtures/ControlFlow/functions.js"
    --min-claimed 9 --min-resolved 9 --min-direct 16
    "--expect-failure=the CLOSED WORLD NARROWED")
  # 4: The pass cannot report a rewrite it did not make. Every ctjs.call_direct
  # is renamed before counting, so the remark says 16 and the IR holds none.
  # 5 (pruned_counter_agrees): the reported pruned-function count must add up
  # to the source total.
  ctcompile_add_native_claims_negative(call_direct_counter_agrees
    "${CMAKE_CURRENT_SOURCE_DIR}/CTNative/Fixtures/ControlFlow/functions.js"
    --mutate-drop-call-direct
    "--expect-failure=the counter and the rewrite disagree")
  ctcompile_add_native_claims_negative(pruned_counter_agrees
    "${CMAKE_CURRENT_SOURCE_DIR}/CTNative/Fixtures/ControlFlow/functions.js"
    --mutate-pruned-count
    "--expect-failure=source function conservation failed")
endif()
