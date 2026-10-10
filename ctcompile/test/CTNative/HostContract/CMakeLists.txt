ctcompile_test_sources(ctcompile-test-provider-state ProviderState.cpp)

foreach(_prefix_target IN LISTS CTCOMPILE_HOST_PREFIX_TARGETS)
  target_sources(${_prefix_target} PRIVATE PrefixDifferential.cpp)
  # Its program.js argument, parsed by ctbrowser/tools/support/options.hpp.
  target_link_libraries(${_prefix_target} PRIVATE ctbrowser-options)
endforeach()

ctcompile_test_sources(ctcompile-test-host-contract
  HostContract.cpp
  ClassIntrinsics.h
  Fingerprint.h
  Fixtures.h
)

add_subdirectory(DOM)
add_subdirectory(SeededMaps)
