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
# mingw explicitly and gives both the same output. tools/build-cpptrace-mingw.sh
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
                 "brew install cpptrace, or tools/build-cpptrace-mingw.sh for the cross build.")
endif()

function(ctbrowser_test name)
  add_executable(ctbrowser-test-${name} ${name}.cpp)
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
  # vm_basics differentially tests the VM against ctjs's own interpreter, so
  # the brick's headers have to be reachable.
  target_include_directories(ctbrowser-test-${name} PRIVATE
    "${PROJECT_SOURCE_DIR}/external/compile-time-javascript/include"
    "${PROJECT_SOURCE_DIR}/external/compile-time-javascript/external/compile-time-containers/include"
    "${PROJECT_SOURCE_DIR}/external/compile-time-css/include")
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
  target_include_directories(ctbrowser-test-${name} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
  add_dependencies(ctbrowser-tests ctbrowser-test-${name})
  # The source root, so goldens and other repo-relative paths resolve.
  add_test(NAME ${name} COMMAND ctbrowser-test-${name}
           WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
endfunction()

ctbrowser_test(api_surface)
ctbrowser_test(core_basics)
ctbrowser_test(stress_slab)
ctbrowser_test(dom_basics)
ctbrowser_test(html_parse)
ctbrowser_test(stress_dom)
ctbrowser_test(vm_basics)
ctbrowser_test(page_scripts)
ctbrowser_test(p5_ratchet)
ctbrowser_test(phaser_ratchet)
ctbrowser_test(phaser_api)
ctbrowser_test(phaser_invaders)
ctbrowser_test(webgl2_ratchet)
ctbrowser_test(babylon_ratchet)
ctbrowser_test(babylon_api)
ctbrowser_test(gles_basics)
ctbrowser_test(glsl_translate)
ctbrowser_test(babylon_lighting)
ctbrowser_test(babylon_interaction)
ctbrowser_test(module_ratchet)
ctbrowser_test(webgl2_api)
ctbrowser_test(p5_api)
ctbrowser_test(net_basics)
ctbrowser_test(data_url)
ctbrowser_test(url_basics)
ctbrowser_test(image_basics)
ctbrowser_test(style_basics)
ctbrowser_test(layout_basics)
ctbrowser_test(paint_basics)
ctbrowser_test(raster_basics)
ctbrowser_test(glsl_basics)
ctbrowser_test(softgl_basics)
ctbrowser_test(webgl_basics)
ctbrowser_test(spirv_basics)
ctbrowser_test(fonts_basics)
ctbrowser_test(svg_basics)
ctbrowser_test(pipeline_basics)
ctbrowser_test(shell_basics)
ctbrowser_test(bindings_basics)
ctbrowser_test(widgets_basics)
ctbrowser_test(chrome_basics)

# The GPU test needs SDL3 to BUILD; it does not need a GPU to PASS. Where there
# is no device it skips the hardware half and still checks the fallback, which
# is the path such a machine actually takes.
if(SDL3_FOUND)
  ctbrowser_test(gpu_basics)
  target_link_libraries(ctbrowser-test-gpu_basics PRIVATE ctbrowser::gpu SDL3::SDL3)
  # `offscreen` rather than `dummy`: dummy has no Vulkan surface support, so a
  # device cannot be created under it even where the driver is fine.
  # The Vulkan driver is not TSan-instrumented, so its internal locking is
  # reported as races. Suppressed by library only - see tests/tsan.supp.
  set_tests_properties(gpu_basics PROPERTIES ENVIRONMENT
    "SDL_VIDEODRIVER=offscreen;TSAN_OPTIONS=suppressions=${CMAKE_CURRENT_SOURCE_DIR}/tsan.supp")
endif()

# bindings_basics drives run_app, which initialises SDL - and SDL reaches
# libdbus, whose own lock order TSan reports as an inversion. Same suppression
# file, same rule: by LIBRARY, so a real one in our code is still caught.
# ...and its offscreen video driver initialises EGL without releasing it, which
# LeakSanitizer reports. Both suppressed by LIBRARY - see the files.
set_tests_properties(bindings_basics PROPERTIES ENVIRONMENT
  "SDL_VIDEODRIVER=offscreen;SDL_AUDIODRIVER=dummy;TSAN_OPTIONS=suppressions=${CMAKE_CURRENT_SOURCE_DIR}/tsan.supp;LSAN_OPTIONS=suppressions=${CMAKE_CURRENT_SOURCE_DIR}/lsan.supp")

# The stress test is the one that must never be skipped: it is the evidence
# that the lock-free read path is actually sound. Give it room under TSan.
set_tests_properties(stress_slab PROPERTIES TIMEOUT 300)

# The GPU benchmark is the one target that MUST be run as a Windows .exe to
# mean anything on a WSL2 machine: Linux binaries there see no Vulkan adapter
# but lavapipe, so a "GPU" number measured under WSL is two CPU implementations
# racing. Build it with `cmake --preset windows` (or ./tools/remote-build.sh
# windows) and run the .exe from Windows, where the driver reaches the GPU.
#
# Benchmarks are NOT ctest gates - the numbers move with the machine, and a
# perf regression should be read, not silently failed. Build and run by hand.
function(ctbrowser_bench name)
  add_executable(ctbrowser-test-${name} EXCLUDE_FROM_ALL ${name}.cpp ${ARGN})
  target_link_libraries(ctbrowser-test-${name}
    PRIVATE ctbrowser::core ctbrowser::dom ctbrowser::style ctbrowser::layout ctbrowser::paint ctbrowser::raster ctbrowser::shell ctbrowser::ctbrowser)
  ctbrowser_target(ctbrowser-test-${name})
  # the bricks the engine's style and script parsers come from
  target_include_directories(ctbrowser-test-${name} PRIVATE
    "${PROJECT_SOURCE_DIR}/external/compile-time-javascript/include"
    "${PROJECT_SOURCE_DIR}/external/compile-time-javascript/external/compile-time-containers/include"
    "${PROJECT_SOURCE_DIR}/external/compile-time-css/include")
endfunction()

ctbrowser_bench(bench_script)
ctbrowser_bench(bench_reads)
ctbrowser_bench(bench_style)
ctbrowser_bench(bench_interaction)
ctbrowser_bench(bench_layout)
ctbrowser_bench(bench_raster)
if(SDL3_FOUND)
  ctbrowser_bench(bench_gpu)
  target_link_libraries(ctbrowser-test-bench_gpu PRIVATE ctbrowser::gpu SDL3::SDL3)
endif()
