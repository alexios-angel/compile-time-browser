# v2 tests. Every one is an executable; a non-zero exit fails ctest, matching
# v1's convention so both suites read the same in CI.

add_custom_target(ctbrowser-v2-test ALL)

function(ctbrowser_v2_test name)
  add_executable(ctbrowser-v2-${name} ${name}.cpp)
  target_link_libraries(ctbrowser-v2-${name} PRIVATE ctbrowser::core ctbrowser::dom ctbrowser::script ctbrowser::style ctbrowser::layout ctbrowser::paint ctbrowser::raster ctbrowser::shell ctbrowser::v2)
  # the VM tests compare against v1's interpreter (ctjs.hpp), which needs its headers
  target_include_directories(ctbrowser-v2-${name} PRIVATE
    "${PROJECT_SOURCE_DIR}/external/compile-time-javascript/include"
    "${PROJECT_SOURCE_DIR}/external/compile-time-javascript/external/compile-time-containers/include"
    "${PROJECT_SOURCE_DIR}/external/compile-time-css/include")
  target_compile_options(ctbrowser-v2-${name} PRIVATE ${CTBROWSER_V2_WARNINGS})
  target_include_directories(ctbrowser-v2-${name} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
  add_dependencies(ctbrowser-v2-test ctbrowser-v2-${name})
  # the source root, so goldens and other repo-relative paths resolve the same
  # way v1's tests do
  add_test(NAME v2-${name} COMMAND ctbrowser-v2-${name}
           WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
endfunction()

ctbrowser_v2_test(api_surface)
ctbrowser_v2_test(core_basics)
ctbrowser_v2_test(stress_slab)
ctbrowser_v2_test(dom_basics)
ctbrowser_v2_test(html_parse)
ctbrowser_v2_test(stress_dom)
ctbrowser_v2_test(vm_basics)
ctbrowser_v2_test(page_scripts)
ctbrowser_v2_test(net_basics)
ctbrowser_v2_test(image_basics)
ctbrowser_v2_test(style_basics)
ctbrowser_v2_test(layout_basics)
ctbrowser_v2_test(paint_basics)
ctbrowser_v2_test(raster_basics)
ctbrowser_v2_test(pipeline_basics)
ctbrowser_v2_test(shell_basics)
ctbrowser_v2_test(bindings_basics)
ctbrowser_v2_test(widgets_basics)

# The GPU test needs SDL3 to BUILD; it does not need a GPU to PASS. Where there
# is no device it skips the hardware half and still checks the fallback, which
# is the path such a machine actually takes.
if(SDL3_FOUND)
  ctbrowser_v2_test(gpu_basics)
  target_link_libraries(ctbrowser-v2-gpu_basics PRIVATE ctbrowser::gpu SDL3::SDL3)
  # `offscreen` rather than `dummy`: dummy has no Vulkan surface support, so a
  # device cannot be created under it even where the driver is fine.
  # The Vulkan driver is not TSan-instrumented, so its internal locking is
  # reported as races. Suppressed by library only - see tests-v2/tsan.supp.
  set_tests_properties(v2-gpu_basics PROPERTIES ENVIRONMENT
    "SDL_VIDEODRIVER=offscreen;TSAN_OPTIONS=suppressions=${CMAKE_CURRENT_SOURCE_DIR}/tsan.supp")
endif()

# The stress test is the one that must never be skipped: it is the evidence
# that the lock-free read path is actually sound. Give it room under TSan.
set_tests_properties(v2-stress_slab PROPERTIES TIMEOUT 300)

# The GPU benchmark is the one v2 target that MUST be run as a Windows .exe to
# mean anything on a WSL2 machine: Linux binaries there see no Vulkan adapter
# but lavapipe, so a "GPU" number measured under WSL is two CPU implementations
# racing. Build it with `cmake --preset windows` (or ./tools/remote-build.sh
# windows) and run the .exe from Windows, where the driver reaches the GPU.
#
# Benchmarks are NOT ctest gates - the numbers move with the machine, and a
# perf regression should be read, not silently failed. Build and run by hand.
function(ctbrowser_v2_bench name)
  add_executable(ctbrowser-v2-${name} EXCLUDE_FROM_ALL ${name}.cpp ${ARGN})
  target_link_libraries(ctbrowser-v2-${name}
    PRIVATE ctbrowser::core ctbrowser::dom ctbrowser::style ctbrowser::layout ctbrowser::paint ctbrowser::raster ctbrowser::shell ctbrowser::v2)
  target_compile_options(ctbrowser-v2-${name} PRIVATE ${CTBROWSER_V2_WARNINGS})
  # the layout benchmark drives v1's layout() for comparison, which needs v1's
  # headers and the bricks underneath them
  target_include_directories(ctbrowser-v2-${name} PRIVATE
    "${PROJECT_SOURCE_DIR}/include"
    "${PROJECT_SOURCE_DIR}/external/compile-time-html/include"
    "${PROJECT_SOURCE_DIR}/external/compile-time-javascript/include"
    "${PROJECT_SOURCE_DIR}/external/compile-time-javascript/external/compile-time-containers/include"
    "${PROJECT_SOURCE_DIR}/external/compile-time-css/include")
endfunction()

ctbrowser_v2_bench(bench_reads)
ctbrowser_v2_bench(bench_style)
ctbrowser_v2_bench(bench_layout bench_layout_v1.cpp)
ctbrowser_v2_bench(bench_raster bench_layout_v1.cpp)
if(SDL3_FOUND)
  ctbrowser_v2_bench(bench_gpu)
  target_link_libraries(ctbrowser-v2-bench_gpu PRIVATE ctbrowser::gpu SDL3::SDL3)
endif()
