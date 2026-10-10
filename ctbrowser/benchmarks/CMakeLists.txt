# Benchmarks: measurement, not pass/fail. `ctbrowser_bench()` comes from
# cmake/modules/CTTest.cmake and builds these EXCLUDE_FROM_ALL. They are not
# ctest tests because the numbers vary with the machine, and a performance
# regression should be read by a person rather than fail a threshold. Build and
# run them by hand; docs/performance.md describes the method.
ctbrowser_bench(bench_script)
# `--csv`, parsed by tools/support/options.hpp.
target_link_libraries(ctbrowser-test-bench_script PRIVATE ctbrowser-options)
ctbrowser_bench(bench_style)
# ctcss is not an engine dependency. Only this opt-in benchmark includes it,
# as the other implementation in its timing comparison.
target_include_directories(ctbrowser-test-bench_style PRIVATE
  "${CTBROWSER_MONOREPO_ROOT}/third-party/compile-time-css/include")
ctbrowser_bench(bench_interaction)
ctbrowser_bench(bench_layout)
ctbrowser_bench(bench_raster)
