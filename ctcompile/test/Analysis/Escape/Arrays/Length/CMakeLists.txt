# Dense array `length` tests, added to the escape-analysis arrays test binary.
ctcompile_test_sources(ctcompile-test-escape-analysis-arrays
  Setup.cpp
  IndexSnapshots.cpp
  Arithmetic.cpp
  ShrinkArithmetic.cpp
  ShrinkRetention.cpp
  LiveMutations.cpp
  Finish.cpp
  Cases.cpp
  Cases.hpp
)
