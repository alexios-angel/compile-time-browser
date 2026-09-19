if(TARGET ctcompile-test-type-oracle)
  target_sources(ctcompile-test-type-oracle PRIVATE
    Oracle.cpp
  )
endif()

if(TARGET ctcompile-test-type-claims)
  target_sources(ctcompile-test-type-claims PRIVATE
    Claims.cpp
  )
endif()

if(TARGET ctcompile-test-ctnative-lattice)
  target_sources(ctcompile-test-ctnative-lattice PRIVATE
    Lattice.cpp
  )
endif()

add_subdirectory(TypeInference)
add_subdirectory(TypeOracle)
