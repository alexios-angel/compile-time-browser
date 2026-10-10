ctcompile_test_sources(ctcompile-test-owned-method-table-slots MethodTableSlots.cpp)

ctcompile_test_sources(ctcompile-test-owned-global-roots GlobalRoots.cpp)

ctcompile_test_sources(ctcompile-test-owned-global-methods
  GlobalMethods.cpp
  GlobalMethodsFixtures.h
)

add_subdirectory(GlobalSharedMap)
