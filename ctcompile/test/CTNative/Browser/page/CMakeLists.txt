# The driver dom-reads.test runs twice, on the interpreter and on the native
# build; cmake/NativeWorkloads.cmake creates both targets.
ctcompile_test_sources(ctcompile-test-dom-reads dom_reads.cpp)
ctcompile_test_sources(ctcompile-test-dom-reads-native dom_reads.cpp)
