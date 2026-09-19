if(TARGET ctcompile-test-escape-cycle)
  target_sources(ctcompile-test-escape-cycle PRIVATE
    Cycle.cpp
  )
endif()

if(TARGET ctcompile-test-escape-analysis-sinks)
  target_sources(ctcompile-test-escape-analysis-sinks PRIVATE
    Sinks.cpp
    Harness.h
  )
endif()

if(TARGET ctcompile-test-escape-analysis-completion)
  target_sources(ctcompile-test-escape-analysis-completion PRIVATE
    Completion.cpp
  )
endif()

if(TARGET ctcompile-test-escape-analysis-storage)
  target_sources(ctcompile-test-escape-analysis-storage PRIVATE
    Storage.cpp
  )
endif()

if(TARGET ctcompile-test-escape-claims)
  target_sources(ctcompile-test-escape-claims PRIVATE
    Claims.cpp
  )
endif()

if(TARGET ctcompile-test-escape-oracle-aot)
  target_sources(ctcompile-test-escape-oracle-aot PRIVATE
    AOTOracle.cpp
  )
endif()

if(TARGET ctcompile-test-escape-oracle-aot-return)
  target_sources(ctcompile-test-escape-oracle-aot-return PRIVATE
    AOTReturnOracle.cpp
  )
endif()

add_subdirectory(EscapeAnalysisArrays)
