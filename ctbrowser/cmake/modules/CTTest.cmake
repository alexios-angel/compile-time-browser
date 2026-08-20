# How a test or a benchmark is built here - the probes both need, and the two
# functions that use them. A MODULE rather than a CMakeLists because the suite
# lives in three sibling directories now: unittests/ (focused, per subsystem),
# test/ (system-level: corpus ratchets, the lint, stress, and the golden and
# baseline DATA) and benchmarks/. One definition, included once from
# ctbrowser/CMakeLists.txt, used by all three.
#
# Tests. Every one is an executable; a non-zero exit fails ctest.

add_custom_target(ctbrowser-tests ALL)

# std::stacktrace, PROBED rather than assumed - three toolchains, three answers.
# libstdc++ puts the implementation in a separate libstdc++_libbacktrace archive
# that lives in GCC's own library directory, which `find_library` does not search;
# libc++ needs nothing; the mingw cross build has no <stacktrace> at all, and
# check.hpp guards on __cpp_lib_stacktrace and says so at run time.
#
# So: try it plain, then try it with the archive, and take the first that links.
include(CheckCXXSourceCompiles)
set(CTBROWSER_STACKTRACE_PROBE "#include <stacktrace>
int main() { return static_cast<int>(std::stacktrace::current().size()) * 0; }")
set(CMAKE_REQUIRED_FLAGS "-std=gnu++23")
check_cxx_source_compiles("${CTBROWSER_STACKTRACE_PROBE}" CTBROWSER_STACKTRACE_PLAIN)
if(NOT CTBROWSER_STACKTRACE_PLAIN)
  set(CMAKE_REQUIRED_LIBRARIES stdc++_libbacktrace)
  check_cxx_source_compiles("${CTBROWSER_STACKTRACE_PROBE}" CTBROWSER_STACKTRACE_WITH_LIB)
  unset(CMAKE_REQUIRED_LIBRARIES)
  if(CTBROWSER_STACKTRACE_WITH_LIB)
    set(CTBROWSER_TEST_BACKTRACE_LIBRARY stdc++_libbacktrace)
  endif()
endif()
unset(CMAKE_REQUIRED_FLAGS)
if(CTBROWSER_STACKTRACE_PLAIN OR CTBROWSER_TEST_BACKTRACE_LIBRARY)
  message(STATUS "ctbrowser: tests print a stack trace when they die")
else()
  message(STATUS "ctbrowser: no std::stacktrace here; a dying test says less")
endif()

# CPPTRACE, for the tests only. Almost every expensive bug in this project has
# been the Windows-only kind, and the llvm-mingw build has no <stacktrace> at
# all - so half the platforms had no trace when a test died. cpptrace supports
# mingw explicitly and gives both the same output. tools/mingw/build-cpptrace-mingw.sh
# puts it in the cross sysroot.
#
# OPTIONAL, unlike mimalloc and simdutf: a missing trace makes a failure harder
# to read, not wrong, and a test suite that will not build because a diagnostic
# is absent is worse than one that prints fewer frames.
find_library(CTBROWSER_CPPTRACE NAMES cpptrace HINTS ${CTBROWSER_BREW_HINTS} PATH_SUFFIXES lib)
find_path(CTBROWSER_CPPTRACE_INCLUDE cpptrace/cpptrace.hpp
  HINTS ${CTBROWSER_BREW_HINTS} PATH_SUFFIXES include)
find_library(CTBROWSER_CPPTRACE_DWARF NAMES dwarf HINTS ${CTBROWSER_BREW_HINTS}
  PATH_SUFFIXES lib)
find_library(CTBROWSER_CPPTRACE_ZSTD NAMES zstd HINTS ${CTBROWSER_BREW_HINTS} PATH_SUFFIXES lib)
if(NOT CTBROWSER_CPPTRACE OR NOT CTBROWSER_CPPTRACE_INCLUDE)
  message(STATUS "cpptrace not found - tests will print no stack traces. "
                 "brew install cpptrace, or tools/mingw/build-cpptrace-mingw.sh for the cross build.")
endif()

# `ctbrowser_test(<bucket>/<name>)`. The argument is a PATH and everything
# derived from it is the BASENAME - the target, and more importantly the ctest
# name. `ctest -R vm_basics` has to keep working: it is in docs/build.md's
# command blocks and in what the ratchet tools pass through. So the suite gained
# directories on 2026-08-09 and the test names did not change at all.
# Where check.hpp, dom_probe.hpp, js_expect.hpp and the sanitizer suppression
# files live. Test DATA and test SUPPORT are system-level, so they stay in
# test/ and everything else points at them.
set(CTBROWSER_TEST_SUPPORT_DIR "${PROJECT_SOURCE_DIR}/test/support")

function(ctbrowser_test path)
  get_filename_component(name "${path}" NAME_WE)
  add_executable(ctbrowser-test-${name} ${path}.cpp)
  if(CTBROWSER_CPPTRACE AND CTBROWSER_CPPTRACE_INCLUDE)
    target_link_libraries(ctbrowser-test-${name} PRIVATE "${CTBROWSER_CPPTRACE}")
    target_include_directories(ctbrowser-test-${name} SYSTEM PRIVATE
      "${CTBROWSER_CPPTRACE_INCLUDE}")
    # CPPTRACE_STATIC_DEFINE when it IS static. cpptrace's header declares its
    # entry points __declspec(dllimport) otherwise, and the Windows cross link
    # fails with "undefined symbol: __declspec(dllimport) cpptrace::v1::
    # generate_trace" against a perfectly good libcpptrace.a. The attribute is a
    # no-op off Windows, so keying on the file extension is enough.
    if(CTBROWSER_CPPTRACE MATCHES "\\.a$")
      target_compile_definitions(ctbrowser-test-${name} PRIVATE CPPTRACE_STATIC_DEFINE)
      # A STATIC cpptrace does not carry its own dependencies. It reads DWARF -
      # which is what mingw emits - through libdwarf, and libdwarf reads
      # compressed sections through zstd. Its install puts both in the sysroot
      # beside it; the link line has to name them, in this order.
      if(CTBROWSER_CPPTRACE_DWARF)
        target_link_libraries(ctbrowser-test-${name} PRIVATE "${CTBROWSER_CPPTRACE_DWARF}")
      endif()
      if(CTBROWSER_CPPTRACE_ZSTD)
        target_link_libraries(ctbrowser-test-${name} PRIVATE "${CTBROWSER_CPPTRACE_ZSTD}")
      endif()
    endif()
    # dbghelp is where StackWalk64, SymFunctionTableAccess64 and
    # SymGetModuleBase64 live. A static cpptrace does not carry them, so the
    # Windows link fails on all three - which is the platform this whole
    # dependency exists to serve.
    if(WIN32 OR MINGW)
      target_link_libraries(ctbrowser-test-${name} PRIVATE dbghelp)
    endif()
  endif()
  target_link_libraries(ctbrowser-test-${name} PRIVATE ctbrowser::core ctbrowser::dom ctbrowser::script ctbrowser::style ctbrowser::layout ctbrowser::paint ctbrowser::raster ctbrowser::shell ctbrowser::ctbrowser)
  ctbrowser_target(ctbrowser-test-${name})
  # -g AND the backtrace library, so a test that dies without reporting a
  # failure says WHERE. std::stacktrace names nothing without debug info, and
  # libstdc++ puts its implementation in a separate archive. Tests are not
  # shipped, so the size is free; both are probed rather than assumed, because
  # the llvm-mingw cross build has <format> but no <stacktrace> at all.
  target_compile_options(ctbrowser-test-${name} PRIVATE -g)
  if(CTBROWSER_TEST_BACKTRACE_LIBRARY)
    target_link_libraries(ctbrowser-test-${name} PRIVATE ${CTBROWSER_TEST_BACKTRACE_LIBRARY})
  endif()
  # ONE support directory for all three trees, named absolutely. It used to be
  # "${CMAKE_CURRENT_SOURCE_DIR}/support", which was the same word for every
  # test because they were all in one directory; they are not any more, and a
  # unittest still says #include "check.hpp".
  target_include_directories(ctbrowser-test-${name} PRIVATE "${CTBROWSER_TEST_SUPPORT_DIR}")
  add_dependencies(ctbrowser-tests ctbrowser-test-${name})
  # The source root, so goldens and other project-relative paths resolve.
  add_test(NAME ${name} COMMAND ctbrowser-test-${name}
           WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
  # THE FACES ARE NOT WHERE A SHIPPED APPLICATION KEEPS THEM. `font_path`
  # defaults to `fonts` beside the executable, which is the shipping layout;
  # in the source tree they are resources/fonts. Without this every test that
  # asks for real text silently falls back to the 8x8 bitmap font, and the
  # first thing that notices is a golden.
  #
  # ENVIRONMENT is one property, so a set_tests_properties(... ENVIRONMENT)
  # elsewhere REPLACES this rather than adding to it. The three places that do
  # that name this variable themselves; there is no way to append that works on
  # every CMake this project supports.
  set_tests_properties(${name} PROPERTIES ENVIRONMENT "CTBROWSER_FONT_PATH=${PROJECT_SOURCE_DIR}/resources/fonts")
endfunction()

# The GPU benchmark is the one target that MUST be run as a Windows .exe to
# mean anything on a WSL2 machine: Linux binaries there see no Vulkan adapter
# but lavapipe, so a "GPU" number measured under WSL is two CPU implementations
# racing. Build it with `cmake --preset windows` (or ./tools/remote-build.sh
# windows) and run the .exe from Windows, where the driver reaches the GPU.
#
# Benchmarks are NOT ctest gates - the numbers move with the machine, and a
# perf regression should be read, not silently failed. Build and run by hand.
function(ctbrowser_bench path)
  get_filename_component(name "${path}" NAME_WE)
  add_executable(ctbrowser-test-${name} EXCLUDE_FROM_ALL ${path}.cpp ${ARGN})
  target_link_libraries(ctbrowser-test-${name}
    PRIVATE ctbrowser::core ctbrowser::dom ctbrowser::style ctbrowser::layout ctbrowser::paint ctbrowser::raster ctbrowser::shell ctbrowser::ctbrowser)
  ctbrowser_target(ctbrowser-test-${name})
  # -g, for the same reason ctbrowser_test() adds it and one more: a benchmark
  # is the thing most likely to be handed to callgrind, and
  # `callgrind_annotate --auto=yes` attributes nothing to a line without debug
  # info. docs/performance.md's whole method is line-level attribution - it is
  # what found the operator table in the lexer after the function-level number
  # said "the lexer" and stopped. These are not shipped, so the size is free.
  target_compile_options(ctbrowser-test-${name} PRIVATE -g)
endfunction()

