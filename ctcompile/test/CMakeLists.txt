# Test registration by subsystem. Includes share this directory scope so
# generated outputs, test commands and helper functions keep their meaning.
# Handwritten sources are registered by the directories that own them below.

# Every test executable here uses ctbrowser/test/support/check.hpp (check(),
# CHECK, CHECK_EQ, REPORT and the terminate/signal trace), so this include is
# directory-wide. When cpptrace is found (CTTest.cmake), check.hpp uses it for
# symbolized stack traces; without it, traces print raw frames and addresses.
include_directories("${CTBROWSER_MONOREPO_ROOT}/ctbrowser/test/support")
if(CTBROWSER_CPPTRACE AND CTBROWSER_CPPTRACE_INCLUDE)
  include_directories(SYSTEM "${CTBROWSER_CPPTRACE_INCLUDE}")
  # ponytail: the shared library only. ctcompile builds only on Linux; the
  # static/mingw dependency chain lives in CTTest.cmake if that changes.
  link_libraries("${CTBROWSER_CPPTRACE}")
endif()

# Declares a test executable and its test: ctcompile-test-<name>, linked
# against LIBS, compiled with DEFS, and registered as ctcompile_<name> (with
# `-` spelled `_`) run with ARGS. Its sources are added by the directory that
# owns them (ctcompile_test_sources).
function(ctcompile_unit_test name)
  cmake_parse_arguments(_unit "" "" "LIBS;ARGS;DEFS" ${ARGN})
  add_executable(ctcompile-test-${name})
  if(_unit_DEFS)
    target_compile_definitions(ctcompile-test-${name} PRIVATE ${_unit_DEFS})
  endif()
  target_link_libraries(ctcompile-test-${name} PRIVATE ${_unit_LIBS})
  ctcompile_target(ctcompile-test-${name})
  string(REPLACE "-" "_" _test "${name}")
  add_test(NAME ctcompile_${_test} COMMAND ctcompile-test-${name} ${_unit_ARGS})
endfunction()

# Adds handwritten sources to test target TARGET from the directory that owns
# them, if that target is configured at all.
function(ctcompile_test_sources target)
  if(TARGET ${target})
    target_sources(${target} PRIVATE ${ARGN})
  endif()
endfunction()

# Generated code does not meet the warning flags that keep the hand-written
# sources honest. The entry's signature is fixed by ct_aot_entry_fn, so a
# function that never looks at `argc` has an unused parameter by construction;
# a JavaScript expression whose value is discarded leaves a variable that is
# set and never read; and the backend does no dead-code elimination. -Werror
# would make every such program a build failure, so these three warnings,
# inherent to emitted code, are disabled rather than -Werror as a whole.
set(CTCOMPILE_GENERATED_WARNINGS
    "-Wno-unused-parameter;-Wno-unused-variable;-Wno-unused-but-set-variable")

# The boxed JS -> C++ pipeline. File names are relative to this binary
# directory.
#
# ctcompile_embed_js: `js` as the raw string a driver reads (embed-js.cmake),
# so the driver runs the same bytes the pipeline compiled.
function(ctcompile_embed_js out js)
  get_filename_component(_name "${out}" NAME_WLE)
  add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${out}"
    COMMAND "${CMAKE_COMMAND}" -DSOURCE=${js} -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/${out}
            -P "${CMAKE_CURRENT_SOURCE_DIR}/Support/embed-js.cmake"
    DEPENDS "${js}" "${CMAKE_CURRENT_SOURCE_DIR}/Support/embed-js.cmake"
    COMMENT "Embedding ${_name} for the driver"
    VERBATIM)
endfunction()

# ctcompile_compile_js: `js` through the EmitC backend (compile-js-to-cpp.cmake),
# the ENTRIES functions renamed to fixed symbols; KIND module compiles it as an
# ES module.
function(ctcompile_compile_js out js)
  cmake_parse_arguments(_js "" "KIND" "ENTRIES" ${ARGN})
  set(_kind)
  if(_js_KIND)
    set(_kind -DKIND=${_js_KIND})
  endif()
  get_filename_component(_name "${out}" NAME_WE)
  add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${out}"
    COMMAND "${CMAKE_COMMAND}"
            -DTRANSLATE=$<TARGET_FILE:ctjs-translate>
            -DOPT=$<TARGET_FILE:ctjs-opt>
            -DMLIR_TRANSLATE=${MLIR_TRANSLATE_EXE}
            -DSOURCE=${js}
            ${_kind}
            "-DENTRIES=${_js_ENTRIES}"
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/${out}
            -P "${CMAKE_CURRENT_SOURCE_DIR}/Support/compile-js-to-cpp.cmake"
    DEPENDS "${js}" ctjs-translate ctjs-opt
            "${CMAKE_CURRENT_SOURCE_DIR}/Support/compile-js-to-cpp.cmake"
    COMMENT "Compiling ${_name} through the EmitC backend"
    VERBATIM)
endfunction()

# ctcompile_aot_entry_table: the table that installs `generated`'s bodies, read
# out of the emitted C++ rather than written down (aot-entry-table.cmake).
function(ctcompile_aot_entry_table out generated table source_name)
  add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${out}"
    COMMAND "${CMAKE_COMMAND}"
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/${generated}
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/${out}
            -DTABLE=${table}
            -DSOURCE_NAME=${source_name}
            -P "${CMAKE_CURRENT_SOURCE_DIR}/Support/aot-entry-table.cmake"
    DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/${generated}"
            "${CMAKE_CURRENT_SOURCE_DIR}/Support/aot-entry-table.cmake"
    COMMENT "Writing the entry table for ${source_name}"
    VERBATIM)
endfunction()

# ctcompile_boxed_test: the generated `cpp` and its EMBEDded driver inputs as
# ctcompile-test-<name> (LIBS, ctbrowser::script by default), and its test.
function(ctcompile_boxed_test name cpp)
  cmake_parse_arguments(_boxed "" "" "EMBED;LIBS" ${ARGN})
  if(NOT _boxed_LIBS)
    set(_boxed_LIBS ctbrowser::script)
  endif()
  set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/${cpp}" PROPERTIES
    COMPILE_OPTIONS "${CTCOMPILE_GENERATED_WARNINGS}")
  list(TRANSFORM _boxed_EMBED PREPEND "${CMAKE_CURRENT_BINARY_DIR}/")
  add_executable(ctcompile-test-${name} "${CMAKE_CURRENT_BINARY_DIR}/${cpp}" ${_boxed_EMBED})
  if(_boxed_EMBED)
    target_include_directories(ctcompile-test-${name} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
  endif()
  target_link_libraries(ctcompile-test-${name} PRIVATE ${_boxed_LIBS})
  ctcompile_target(ctcompile-test-${name})
  string(REPLACE "-" "_" _test "${name}")
  add_test(NAME ctcompile_${_test} COMMAND ctcompile-test-${name})
endfunction()

include("${CMAKE_CURRENT_LIST_DIR}/cmake/CoreAndBoxed.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/cmake/Runtime.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/cmake/Analysis.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/cmake/Native.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/cmake/NativeGuards.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/cmake/NativeFixtures.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/cmake/BootstrapData.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/cmake/PartialEvaluation.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/cmake/NativeWorkloads.cmake")
# Last: lit.site.cfg.py carries CTCOMPILE_BOOTSTRAP_NODE (BootstrapData.cmake)
# and the reference executable (Native.cmake), so they must exist first.
include("${CMAKE_CURRENT_LIST_DIR}/cmake/Lit.cmake")

# Local source ownership; target options and generated fixtures are configured above.
add_subdirectory(Analysis)
add_subdirectory(CTJS)
add_subdirectory(CSS)
add_subdirectory(CTNative)
add_subdirectory(Comparison)
add_subdirectory(Core)
add_subdirectory(Packaging)
add_subdirectory(Runtime)
