if(TARGET ctcompile-test-native-reference)
  target_sources(ctcompile-test-native-reference PRIVATE
    Interpreter.cpp
  )
endif()

if(TARGET ctcompile-test-native-vm-linked)
  target_sources(ctcompile-test-native-vm-linked PRIVATE
    VmLinked.cpp
  )
endif()

if(TARGET ctcompile-test-native-values-vm-linked)
  target_sources(ctcompile-test-native-values-vm-linked PRIVATE
    VmLinked.cpp
  )
endif()
