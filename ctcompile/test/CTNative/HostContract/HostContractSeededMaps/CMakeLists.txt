if(TARGET ctcompile-test-host-contract-seeded-maps)
  target_sources(ctcompile-test-host-contract-seeded-maps PRIVATE
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
endif()
