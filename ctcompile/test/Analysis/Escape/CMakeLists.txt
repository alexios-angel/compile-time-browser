# Escape-analysis unit-test executables, one per topic. The array and object
# contents tests live in Arrays/.

ctcompile_test_sources(ctcompile-test-escape-cycle Cycle.cpp)

ctcompile_test_sources(ctcompile-test-escape-analysis-sinks Sinks.cpp Harness.h)

ctcompile_test_sources(ctcompile-test-escape-analysis-completion Completion.cpp)

ctcompile_test_sources(ctcompile-test-escape-analysis-storage Storage.cpp)

ctcompile_test_sources(ctcompile-test-escape-claims Claims.cpp)

ctcompile_test_sources(ctcompile-test-escape-oracle-aot AOTOracle.cpp)

ctcompile_test_sources(ctcompile-test-escape-oracle-aot-return AOTReturnOracle.cpp)

add_subdirectory(Arrays)
