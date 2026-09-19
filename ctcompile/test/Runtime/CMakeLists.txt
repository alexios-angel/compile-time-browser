if(TARGET ctcompile-test-native-runtime)
  target_sources(ctcompile-test-native-runtime PRIVATE
    NativeRuntime.cpp
  )
endif()

if(TARGET ctcompile-test-stdlib_map)
  target_sources(ctcompile-test-stdlib_map PRIVATE
    StdLibMap.cpp
  )
endif()

add_subdirectory(Differential)
add_subdirectory(GC)
add_subdirectory(Launcher)
add_subdirectory(Linking)
add_subdirectory(Modules)
add_subdirectory(Reference)
