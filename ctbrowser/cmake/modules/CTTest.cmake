# How a test or a benchmark is built: the library probes both need and the
# functions that use them. It is a module rather than a CMakeLists.txt because
# the suite lives in three sibling directories: unittests/ (focused, per
# subsystem), test/ (system-level: corpus ratchets, lint, stress, and the golden
# and baseline data) and benchmarks/. It is included once from
# ctbrowser/CMakeLists.txt and used by all three.
#
# Every test is an executable; a non-zero exit fails ctest.

add_custom_target(ctbrowser-tests ALL)

# cpptrace, for the tests only. The llvm-mingw build has no <stacktrace>, so
# without it a test that dies on Windows prints no trace. cpptrace supports
# mingw and gives the same output on both platforms.
# tools/mingw/build-libs-mingw.sh puts it in the cross sysroot.
#
# Optional, unlike mimalloc and simdutf: a missing trace makes a failure harder
# to read, not wrong, and a suite that will not build because a diagnostic is
# absent is worse than one that prints fewer frames.
find_library(CTBROWSER_CPPTRACE NAMES cpptrace HINTS ${CTBROWSER_BREW_HINTS} PATH_SUFFIXES lib)
find_path(CTBROWSER_CPPTRACE_INCLUDE cpptrace/cpptrace.hpp
  HINTS ${CTBROWSER_BREW_HINTS} PATH_SUFFIXES include)
find_library(CTBROWSER_CPPTRACE_DWARF NAMES dwarf HINTS ${CTBROWSER_BREW_HINTS}
  PATH_SUFFIXES lib)
find_library(CTBROWSER_CPPTRACE_ZSTD NAMES zstd HINTS ${CTBROWSER_BREW_HINTS} PATH_SUFFIXES lib)
if(NOT CTBROWSER_CPPTRACE OR NOT CTBROWSER_CPPTRACE_INCLUDE)
  message(STATUS "cpptrace not found - tests will print no stack traces. "
                 "brew install cpptrace, or tools/mingw/build-libs-mingw.sh for the cross build.")
endif()

# Where check.hpp, dom_probe.hpp, js_expect.hpp and the sanitizer suppression
# files live. Test data and test support are system-level, so they stay in
# test/ and everything else points at them.
set(CTBROWSER_TEST_SUPPORT_DIR "${PROJECT_SOURCE_DIR}/test/support")

# `ctbrowser_test(<path> [NAME <name>])` builds <path>.cpp as a test. The
# target and the ctest name are the path's basename unless NAME overrides it.
# Names must stay stable: `ctest -R vm_` appears in docs/build.md and in what
# the ratchet tools pass through.
function(ctbrowser_test path)
  cmake_parse_arguments(_test "" "NAME" "" ${ARGN})
  get_filename_component(name "${path}" NAME_WE)
  if(_test_NAME)
    set(name "${_test_NAME}")
  endif()
  add_executable(ctbrowser-test-${name} ${path}.cpp)
  if(CTBROWSER_CPPTRACE AND CTBROWSER_CPPTRACE_INCLUDE)
    target_link_libraries(ctbrowser-test-${name} PRIVATE "${CTBROWSER_CPPTRACE}")
    target_include_directories(ctbrowser-test-${name} SYSTEM PRIVATE
      "${CTBROWSER_CPPTRACE_INCLUDE}")
    # CPPTRACE_STATIC_DEFINE when it is static. cpptrace's header declares its
    # entry points __declspec(dllimport) otherwise, and the Windows cross link
    # fails with "undefined symbol: __declspec(dllimport) cpptrace::v1::
    # generate_trace" against a perfectly good libcpptrace.a. The attribute is a
    # no-op off Windows, so keying on the file extension is enough.
    if(CTBROWSER_CPPTRACE MATCHES "\\.a$")
      target_compile_definitions(ctbrowser-test-${name} PRIVATE CPPTRACE_STATIC_DEFINE)
      # A static cpptrace does not carry its own dependencies. It reads DWARF
      # (which mingw emits) through libdwarf, and libdwarf reads compressed
      # sections through zstd. Its install puts both in the sysroot beside it;
      # the link line must name them, in this order.
      if(CTBROWSER_CPPTRACE_DWARF)
        target_link_libraries(ctbrowser-test-${name} PRIVATE "${CTBROWSER_CPPTRACE_DWARF}")
      endif()
      if(CTBROWSER_CPPTRACE_ZSTD)
        target_link_libraries(ctbrowser-test-${name} PRIVATE "${CTBROWSER_CPPTRACE_ZSTD}")
      endif()
    endif()
    # dbghelp is where StackWalk64, SymFunctionTableAccess64 and
    # SymGetModuleBase64 live. A static cpptrace does not carry them, so the
    # Windows link would fail on all three.
    if(WIN32 OR MINGW)
      target_link_libraries(ctbrowser-test-${name} PRIVATE dbghelp)
    endif()
  endif()
  # Shared test headers precede engine headers, preserving their include lookup order.
  target_link_libraries(ctbrowser-test-${name} PRIVATE ctbrowser-test-support ctbrowser::core ctbrowser::dom ctbrowser::script ctbrowser::style ctbrowser::layout ctbrowser::paint ctbrowser::raster ctbrowser::shell ctbrowser::ctbrowser)
  ctbrowser_target(ctbrowser-test-${name})
  # Keep support first even when a test adds direct reference-parser includes.
  # The directory remains owned by the support target.
  target_include_directories(ctbrowser-test-${name} BEFORE PRIVATE
    "$<TARGET_PROPERTY:ctbrowser-test-support,INTERFACE_INCLUDE_DIRECTORIES>")
  # -g, so a test that dies without reporting a failure says where: cpptrace
  # names nothing without debug info. Tests are not shipped, so the size is free.
  target_compile_options(ctbrowser-test-${name} PRIVATE -g)
  add_dependencies(ctbrowser-tests ctbrowser-test-${name})
  # The source root, so goldens and other project-relative paths resolve.
  add_test(NAME ${name} COMMAND ctbrowser-test-${name}
           WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
  # In the source tree the font faces are in resources/fonts, not in `fonts`
  # beside the executable where `font_path` looks by default (the shipped
  # layout). Without this, every test that asks for real text silently falls
  # back to the 8x8 bitmap font.
  #
  # ENVIRONMENT is one property, so a set_tests_properties(... ENVIRONMENT)
  # elsewhere replaces this rather than adding to it. Every place that does
  # (unittests/unit, the test/corpus/* directories, examples/) repeats
  # CTBROWSER_FONT_PATH itself; there is no append that works on every CMake
  # this project supports.
  set_tests_properties(${name} PROPERTIES ENVIRONMENT "CTBROWSER_FONT_PATH=${PROJECT_SOURCE_DIR}/resources/fonts")
endfunction()

# `ctbrowser_test_directory()`: every .cpp in the calling directory is a test,
# named for its path below CTBROWSER_TEST_ROOT with `/` replaced by `_`, so
# unittests/unit/aot/basics.cpp is `aot_basics`. unittests/unit and
# unittests/js set the root; each folder below has a CMakeLists.txt that calls
# this, as check-cmake-hygiene requires of a directory holding C++.
function(ctbrowser_test_directory)
  file(RELATIVE_PATH prefix "${CTBROWSER_TEST_ROOT}" "${CMAKE_CURRENT_SOURCE_DIR}")
  file(GLOB tests RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" CONFIGURE_DEPENDS *.cpp)
  foreach(test IN LISTS tests)
    get_filename_component(stem "${test}" NAME_WE)
    set(name "${stem}")
    if(prefix)
      string(REPLACE "/" "_" name "${prefix}/${stem}")
    endif()
    ctbrowser_test(${stem} NAME ${name})
  endforeach()
endfunction()

# `ctbrowser_bench(<path>)` builds <path>.cpp as a benchmark, EXCLUDE_FROM_ALL
# and not registered with ctest: the numbers vary with the machine, and a
# performance regression should be read, not fail a threshold.
function(ctbrowser_bench path)
  get_filename_component(name "${path}" NAME_WE)
  add_executable(ctbrowser-test-${name} EXCLUDE_FROM_ALL ${path}.cpp)
  target_link_libraries(ctbrowser-test-${name}
    PRIVATE ctbrowser::core ctbrowser::dom ctbrowser::style ctbrowser::layout ctbrowser::paint ctbrowser::raster ctbrowser::shell ctbrowser::ctbrowser)
  ctbrowser_target(ctbrowser-test-${name})
  # -g, because benchmarks are run under callgrind, and
  # `callgrind_annotate --auto=yes` attributes nothing to a source line without
  # debug info. docs/performance.md's method depends on line-level attribution.
  # Benchmarks are not shipped, so the size does not matter.
  target_compile_options(ctbrowser-test-${name} PRIVATE -g)
endfunction()

