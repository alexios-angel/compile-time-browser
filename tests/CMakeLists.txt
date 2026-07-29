# Tests. Every one is an executable; a non-zero exit fails ctest.

add_custom_target(ctbrowser-tests ALL)

function(ctbrowser_test name)
  add_executable(ctbrowser-test-${name} ${name}.cpp)
  target_link_libraries(ctbrowser-test-${name} PRIVATE ctbrowser::core ctbrowser::dom ctbrowser::script ctbrowser::style ctbrowser::layout ctbrowser::paint ctbrowser::raster ctbrowser::shell ctbrowser::ctbrowser)
  # vm_basics differentially tests the VM against ctjs's own interpreter, so
  # the brick's headers have to be reachable.
  target_include_directories(ctbrowser-test-${name} PRIVATE
    "${PROJECT_SOURCE_DIR}/external/compile-time-javascript/include"
    "${PROJECT_SOURCE_DIR}/external/compile-time-javascript/external/compile-time-containers/include"
    "${PROJECT_SOURCE_DIR}/external/compile-time-css/include")
  ctbrowser_target(ctbrowser-test-${name})
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
ctbrowser_test(net_basics)
ctbrowser_test(image_basics)
ctbrowser_test(style_basics)
ctbrowser_test(layout_basics)
ctbrowser_test(paint_basics)
ctbrowser_test(raster_basics)
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

ctbrowser_bench(bench_reads)
ctbrowser_bench(bench_style)
ctbrowser_bench(bench_interaction)
ctbrowser_bench(bench_layout)
ctbrowser_bench(bench_raster)
if(SDL3_FOUND)
  ctbrowser_bench(bench_gpu)
  target_link_libraries(ctbrowser-test-bench_gpu PRIVATE ctbrowser::gpu SDL3::SDL3)
endif()
