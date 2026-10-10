# Class/DOM and DOM URI transaction tests, linked into the exception-recovery
# test binary.
ctcompile_test_sources(ctcompile-test-exception-recovery
  DOMURI.cpp
  ClassTransactions.cpp
  ClassTransactions.hpp
  DOMURI.hpp
)
