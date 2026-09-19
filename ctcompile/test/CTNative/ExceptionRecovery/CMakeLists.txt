if(TARGET ctcompile-test-exception-recovery)
  target_sources(ctcompile-test-exception-recovery PRIVATE
    Main.cpp
    Completion.cpp
    Guards.cpp
    Tests.h
  )
endif()

add_subdirectory(Completion)
