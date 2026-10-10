# The owned-global shared-Map suite: one test executable built from these
# sources plus the ones each subdirectory adds.
ctcompile_test_sources(ctcompile-test-owned-global-shared-map
  Main.cpp
  SavedScalarReads.cpp
  NumericEntry.cpp
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

add_subdirectory(Captured)
add_subdirectory(LeafObjects)
add_subdirectory(LeafReadbacks)
add_subdirectory(SharedMap)
