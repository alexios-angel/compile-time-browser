ctcompile_test_sources(ctcompile-test-native-runtime NativeRuntime.cpp)

ctcompile_test_sources(ctcompile-test-native-page NativePage.cpp)

ctcompile_test_sources(ctcompile-test-native-array-callbacks NativeArrayCallbacks.cpp)

ctcompile_test_sources(ctcompile-test-native-callable NativeCallable.cpp)
ctcompile_test_sources(ctcompile-test-native-events NativeEvents.cpp)

ctcompile_test_sources(ctcompile-test-native-timers NativeTimers.cpp)

ctcompile_test_sources(ctcompile-test-native-promise NativePromise.cpp)

ctcompile_test_sources(ctcompile-test-native-handlers NativeHandlers.cpp)

ctcompile_test_sources(ctcompile-test-stdlib_map StdLibMap.cpp)

add_subdirectory(Differential)
add_subdirectory(GC)
add_subdirectory(Launcher)
add_subdirectory(Linking)
add_subdirectory(Modules)
add_subdirectory(Reference)
