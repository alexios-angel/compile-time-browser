# Measurement, not pass/fail. `ctbrowser_bench()` comes from
# cmake/modules/CTTest.cmake and builds these EXCLUDE_FROM_ALL: benchmarks are
# not ctest gates, because the numbers move with the machine and a perf
# regression should be READ rather than silently failed. Build and run by hand;
# docs/performance.md is the method.
ctbrowser_bench(bench_script)
ctbrowser_bench(bench_style)
# ctcss is not an engine dependency. Only this opt-in benchmark includes it,
# as the previous implementation on the other side of the timing comparison.
target_include_directories(ctbrowser-test-bench_style PRIVATE
  "${CTBROWSER_MONOREPO_ROOT}/third-party/compile-time-css/include")
ctbrowser_bench(bench_interaction)
ctbrowser_bench(bench_layout)
ctbrowser_bench(bench_raster)
