ctcompile_test_sources(ctcompile-test-type-oracle Oracle.cpp)

ctcompile_test_sources(ctcompile-test-type-claims Claims.cpp)

ctcompile_test_sources(ctcompile-test-ctnative-lattice Lattice.cpp)

add_subdirectory(Inference)
add_subdirectory(Oracle)
