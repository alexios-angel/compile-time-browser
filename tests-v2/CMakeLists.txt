# v2 tests. Every one is an executable; a non-zero exit fails ctest, matching
# v1's convention so both suites read the same in CI.

add_custom_target(ctbrowser-v2-test ALL)

function(ctbrowser_v2_test name)
  add_executable(ctbrowser-v2-${name} ${name}.cpp)
  target_link_libraries(ctbrowser-v2-${name} PRIVATE ctbrowser::core ctbrowser::dom ctbrowser::script)
  # the VM tests compare against v1's interpreter (ctjs.hpp), which needs its headers
  target_include_directories(ctbrowser-v2-${name} PRIVATE
    "${PROJECT_SOURCE_DIR}/external/compile-time-javascript/include"
    "${PROJECT_SOURCE_DIR}/external/compile-time-javascript/external/compile-time-containers/include")
  target_compile_options(ctbrowser-v2-${name} PRIVATE ${CTBROWSER_V2_WARNINGS})
  target_include_directories(ctbrowser-v2-${name} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
  add_dependencies(ctbrowser-v2-test ctbrowser-v2-${name})
  add_test(NAME v2-${name} COMMAND ctbrowser-v2-${name})
endfunction()

ctbrowser_v2_test(core_basics)
ctbrowser_v2_test(stress_slab)
ctbrowser_v2_test(dom_basics)
ctbrowser_v2_test(stress_dom)
ctbrowser_v2_test(vm_basics)

# The stress test is the one that must never be skipped: it is the evidence
# that the lock-free read path is actually sound. Give it room under TSan.
set_tests_properties(v2-stress_slab PROPERTIES TIMEOUT 300)

# Benchmarks are NOT ctest gates - the numbers move with the machine, and a
# perf regression should be read, not silently failed. Build and run by hand.
add_executable(ctbrowser-v2-bench_reads EXCLUDE_FROM_ALL bench_reads.cpp)
target_link_libraries(ctbrowser-v2-bench_reads PRIVATE ctbrowser::core)
target_compile_options(ctbrowser-v2-bench_reads PRIVATE ${CTBROWSER_V2_WARNINGS})
