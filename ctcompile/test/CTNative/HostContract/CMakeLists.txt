ctcompile_test_sources(ctcompile-test-provider-state ProviderState.cpp)

foreach(_prefix_target IN LISTS CTCOMPILE_HOST_PREFIX_TARGETS)
  target_sources(${_prefix_target} PRIVATE HostPrefixDifferential.cpp)
  # Its program.js argument, parsed by ctbrowser/test/support/options.hpp.
  target_link_libraries(${_prefix_target} PRIVATE Boost::program_options)
endforeach()

ctcompile_test_sources(ctcompile-test-host-contract
  HostContract.cpp
  ClassIntrinsics.h
  DOMBranchFilter.h
  DOMDataset.h
  DOMIteration.h
  DOMIteratorClose.h
  DOMJSON.h
  DOMNullable.h
  DOMNumbers.h
  DOMSnapshotLengths.h
  DOMURI.h
  Fingerprint.h
  HostContractFixtures.h
)

add_subdirectory(HostContractSeededMaps)
