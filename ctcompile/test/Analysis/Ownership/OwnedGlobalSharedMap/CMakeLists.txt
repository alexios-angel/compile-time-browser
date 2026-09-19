if(TARGET ctcompile-test-owned-global-shared-map)
  target_sources(ctcompile-test-owned-global-shared-map PRIVATE
    Main.cpp
    SavedScalarReads.cpp
    NumericEntry.cpp
    CapturedClear.cpp
    CapturedZeroSize.cpp
    CapturedExactSize.cpp
    CapturedDeleteSize.cpp
    DefiniteAbsence.cpp
    LeafReadbacks.cpp
    LeafObjects.cpp
    NestedCalls.cpp
    ObjectKeyArguments.cpp
    RetainedObjectKeyFamily.cpp
    DOMKeyInputs.cpp
    SharedMap.cpp
    Tests.h
  )
endif()

add_subdirectory(LeafObjects)
add_subdirectory(LeafReadbacks)
add_subdirectory(SharedMap)
