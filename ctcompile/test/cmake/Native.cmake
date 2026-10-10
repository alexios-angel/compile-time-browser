# === PHASE 62½-D: the native gate ===
#
# THE TEST THAT DEFINES "NATIVE" - part 24 §1.2 and Phase 62½-D - is a lit
# test since 2026-09-15: CTNative/Checks/compilation-unit.py (emit, compile
# standalone, nm, run, compare), driven by the RUN lines in each fixture's
# .test or hand-written .emitc.mlir. What CMake still owns is what needs the
# BUILD: the interpreter reference, the two VM-linked negative controls, the
# nm and the clean-compile toolchains lit is told about (Lit.cmake), and the
# pipeline modules every fixture .test reads out of the build tree.
#
# THE REFERENCE NEEDS NO MLIR. It is the interpreter's opinion of a JavaScript
# file, built wherever the type oracle is: the answer side of the gate should
# not depend on the compiler side existing, and a lowering author wants it on a
# box with no LLVM too.
add_executable(ctcompile-test-native-reference)
# PROGRAM.js or --page PAGE.html, parsed by ctbrowser/tools/support/options.hpp.
target_link_libraries(ctcompile-test-native-reference PRIVATE ctbrowser::ctbrowser
  ctbrowser-options)
ctcompile_target(ctcompile-test-native-reference)

# THE RUNTIME HEADER, COMPILED BY THE BUILD. include/ctcompile/CTNative/Runtime/
# ctnative.hpp is what every native program includes, and nothing else here
# compiles it under -Werror - a header only generated programs compile is the
# string literal it replaced. Needs no MLIR, like the reference above; needs
# ctbrowser for its DOM section.
ctcompile_unit_test(native-runtime LIBS ctbrowser::ctbrowser)

# A VM-LINKED NEGATIVE CONTROL: <name>-fixture.emitc.mlir's C++, emitted at
# build time by the same emitter the gate uses, plus one object that reaches the
# interpreter, linked against the engine - it must FAIL the symbol check. THE
# GATE'S OWN FLAGS, not ctcompile_target's: this is the same text under the same
# compile the gate performs, plus one object and one library.
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
  # nm, the one CMake found beside the compiler at configure time (llvm-nm for
  # the pinned clang). A missing one is said out loud rather than skipped: this
  # gate IS the definition of native, and a suite without it is green on nothing.
  set(_native_nm "${CMAKE_NM}")
  if(NOT _native_nm)
    find_program(_native_nm NAMES llvm-nm nm)
  endif()
  if(NOT _native_nm)
    message(FATAL_ERROR "ctcompile: no nm found - the Phase 62½-D native gate cannot run, and it IS the definition of native")
  endif()

  # NEGATIVE PROOF 2 (functions.emitc.mlir's VM-LINKED RUN line): this binary
  # is the fixture's program in every respect but the one that matters.
  ctcompile_vm_linked_control(native
    "${CMAKE_CURRENT_SOURCE_DIR}/CTNative/Fixtures/ControlFlow/functions.emitc.mlir")
endif()

# === PHASE 62½-C+D: JavaScript through the native pipeline and the gate ===
#
# The programs that go all the way. pipeline.cmake runs the closed world, the
# lift and the lowering over a JavaScript file and refuses to write a module
# while any function carries a diagnostic; the fixture's .test then compiles
# the module with no ctbrowser library, runs it, and compares every numeric
# global with the interpreter (and its deduced twin, the two-toolchain clean
# compile and the printing gate - see CTNative/Fixtures/*/*.test). One
# function, one line per program: the module, its printing-policy twin and
# the pin mutation are built here because lit cannot, and
# docs/refactor-goldens.md measures a refactor against the same files.
if(CTCOMPILE_ENABLE_MLIR AND TARGET ctjs-translate AND TARGET ctjs-opt)
  # PHASE 63 STEP 7: the generated file compiles clean on BOTH toolchains the
  # plan names (compile-clean.py). Two compilers are required, not "whatever
  # is found": a gate that silently drops a compiler passes vacuously.
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
    # PHASE 62½-E: the same module with the printing policy applied, and with
    # the pass's mutation - one pin turned wrong, which print-deduced.py shows
    # fails the build naming the JavaScript site.
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

# ONE FIXTURE, BOTH GATES: the native pipeline over CTNative/Fixtures/<rel>,
# its remaining arguments forwarded as pipeline options, and with CLAIMS the
# four census floors. The two gates have different preconditions (claims also
# needs Python3), so each is checked on its own. See NativeFixtures.cmake.
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

# numbers and booleans, no functions
ctcompile_native_fixture(numeric ControlFlow/globals.js)
# the gate's own eight functions: recursion, loops, a boolean predicate
ctcompile_native_fixture(functions ControlFlow/functions.js)
# Phase 56: object literals with closed shapes, on the stack
ctcompile_native_fixture(structs Objects/struct.js)
# Phase 57A: dense, uniformly numeric array literals, as std::vector<double>
ctcompile_native_fixture(arrays Objects/array.js)
# Current own-contents evidence permits only bounded local Number overwrites.
ctcompile_native_fixture(array_overwrite Objects/array-overwrite.js)
ctcompile_native_fixture(array_overwrite_unoptimized Objects/array-overwrite.js NO_DEFAULT_OPTIMIZATIONS)
ctcompile_native_fixture(array_read_overwrite Objects/array-read-overwrite.js)
ctcompile_native_fixture(array_read_overwrite_unoptimized Objects/array-read-overwrite.js NO_DEFAULT_OPTIMIZATIONS)
ctcompile_native_fixture(array_shrink Objects/array-shrink.js)
ctcompile_native_fixture(array_shrink_unoptimized Objects/array-shrink.js NO_DEFAULT_OPTIMIZATIONS)
ctcompile_native_fixture(array_overwrite_loop Objects/array-overwrite-loop.js)
ctcompile_native_fixture(array_overwrite_loop_unoptimized Objects/array-overwrite-loop.js NO_DEFAULT_OPTIMIZATIONS)

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
  # THE BROWSER DRIVERS - CTNative/Browser/*.py and Ownership/native_data_session.py
  # - are lit tests beside their sources since 2026-09-15, like the seventeen
  # drivers that already were. The one thing they need that lit cannot find is
  # the compiler for the DOM's public API, which needs C++23 std::expected -
  # unavailable in the older Clang/libstdc++ pair the standalone scalar tests
  # use - so it is chosen here and handed to lit.cfg.py as %dom_clang.
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set(_native_dom_clang ${CMAKE_CXX_COMPILER})
  else()
    find_program(_native_dom_clang NAMES clang++-23 clang++
                 HINTS ${CTBROWSER_MONOREPO_ROOT}/tools/clang-std-embed/bin REQUIRED)
  endif()
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
  # really does define_global (the "set" window_trap in Shell/bindings/window/window.cpp) - it says
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
  # native-claims.py over one corpus; ARGN are its remaining options.
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
  # Preserve historical admission floors independently of removing dead
  # helpers: the floors are measured with --no-default-optimizations. BOTH also
  # registers default_<name>, which measures ordinary lowering and keeps pruned
  # functions in the source denominator.
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
  # THE FIXTURE IS THE VACUITY CHECK: 9 of 9, so a run that reports nothing
  # claimed anywhere is not mistaken for a clean sheet.
  ctcompile_add_native_claims(fixture "${CMAKE_CURRENT_SOURCE_DIR}/CTNative/Fixtures/ControlFlow/functions.js" 9 8 16 0 BOTH)
  # TWO CLOSED PROGRAMS, ADDED BECAUSE THE FLOORS ABOVE ARE NOT ENOUGH.
  # differential.js and launcher.js are already compiled through the EmitC
  # backend by the blocks further up this file, so they cost nothing to fetch
  # and they are the only corpora here whose globals actually resolve: 56 of 72
  # and 9 of 10. A closed-world clause widened by one term takes those to zero
  # and this fails, which is precisely what did NOT happen when two of them
  # were - the three vendor bundles were already at zero and had nothing left
  # to lose.
  ctcompile_add_native_claims(differential "${CMAKE_CURRENT_SOURCE_DIR}/Runtime/Differential/differential.js" 2 56 10 0)
  ctcompile_add_native_claims(launcher "${CMAKE_CURRENT_SOURCE_DIR}/Runtime/Launcher/application.js" 0 9 9 0)
  ctcompile_add_native_claims(bootstrap
    "${CTBROWSER_MONOREPO_ROOT}/ctbrowser/vendor/bootstrap/bootstrap.bundle.js" 19 0 21 208 BOTH)
  ctcompile_add_native_claims(p5 "${CTBROWSER_MONOREPO_ROOT}/ctbrowser/vendor/p5/p5.js" 39 0 41 328 BOTH)
  ctcompile_add_native_claims(phaser
    "${CTBROWSER_MONOREPO_ROOT}/ctbrowser/vendor/phaser/phaser.js" 45 0 48 283 BOTH)

  # BOTH TEETH, PROVED. Each runs the same check as a child and passes only
  # when that child failed for the stated reason - not ctest's WILL_FAIL,
  # which a crash or a missing corpus would satisfy just as well.
  # 1: the floor bites. The fixture claims 9 of 9; a floor of 10 must fail.
  ctcompile_add_native_claims_negative(floor_bites
    "${CMAKE_CURRENT_SOURCE_DIR}/CTNative/Fixtures/ControlFlow/functions.js"
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
    "${CMAKE_CURRENT_SOURCE_DIR}/CTNative/Fixtures/ControlFlow/functions.js"
    --min-claimed 9 --min-resolved 9 --min-direct 16
    "--expect-failure=the CLOSED WORLD NARROWED")
  # 4: and the pass cannot report a rewrite it did not make. Every
  # ctjs.call_direct is renamed before counting, so the remark says 16 and the
  # IR holds none.
  ctcompile_add_native_claims_negative(call_direct_counter_agrees
    "${CMAKE_CURRENT_SOURCE_DIR}/CTNative/Fixtures/ControlFlow/functions.js"
    --mutate-drop-call-direct
    "--expect-failure=the counter and the rewrite disagree")
  ctcompile_add_native_claims_negative(pruned_counter_agrees
    "${CMAKE_CURRENT_SOURCE_DIR}/CTNative/Fixtures/ControlFlow/functions.js"
    --mutate-pruned-count
    "--expect-failure=source function conservation failed")
endif()
