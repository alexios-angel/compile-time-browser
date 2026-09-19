if(TARGET ctcompile-test-owned-method-table-slots)
  target_sources(ctcompile-test-owned-method-table-slots PRIVATE
    MethodTableSlots.cpp
  )
endif()

if(TARGET ctcompile-test-owned-global-roots)
  target_sources(ctcompile-test-owned-global-roots PRIVATE
    GlobalRoots.cpp
  )
endif()

if(TARGET ctcompile-test-owned-global-methods)
  target_sources(ctcompile-test-owned-global-methods PRIVATE
    GlobalMethods.cpp
    GlobalMethodsFixtures.h
  )
endif()

add_subdirectory(OwnedGlobalSharedMap)
