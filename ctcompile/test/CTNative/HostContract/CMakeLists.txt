if(TARGET ctcompile-test-provider-state)
  target_sources(ctcompile-test-provider-state PRIVATE
    ProviderState.cpp
  )
endif()

foreach(_prefix_target IN LISTS CTCOMPILE_HOST_PREFIX_TARGETS)
  target_sources(${_prefix_target} PRIVATE HostPrefixDifferential.cpp)
endforeach()

if(TARGET ctcompile-test-host-contract)
  target_sources(ctcompile-test-host-contract PRIVATE
    HostContract.cpp
    ClassIntrinsics.h
    DOMBranchFilter.h
    DOMDataset.h
    DOMIteration.h
    DOMJSON.h
    DOMNullable.h
    DOMNumbers.h
    DOMURI.h
    Fingerprint.h
    HostContractFixtures.h
  )
endif()

add_subdirectory(HostContractSeededMaps)
