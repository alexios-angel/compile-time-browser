# Adds Dispatch.cpp to the boxed-dispatch test target that
# test/cmake/PartialEvaluation.cmake defines, when that target exists.
ctcompile_test_sources(ctcompile-test-specialization-dispatch Dispatch.cpp)
