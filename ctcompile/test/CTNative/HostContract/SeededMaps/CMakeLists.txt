# One test executable for the host-contract Map result proofs. Main.cpp builds
# the shared captured-Map fixture from ../Fixtures.h and runs the checks in
# Tests.h.
ctcompile_test_sources(ctcompile-test-host-contract-seeded-maps
  Main.cpp
  EntryNumericResults.cpp
  CapturedMapClear.cpp
  DefiniteMapAbsence.cpp
  LeafReadbacks.cpp
  LeafObjectPayloads.cpp
  NestedMapResults.cpp
  NullablePayloadResults.cpp
  NullableMapResults.cpp
  ConditionalMapResults.cpp
  SeededMapResults.cpp
  Tests.h
)
