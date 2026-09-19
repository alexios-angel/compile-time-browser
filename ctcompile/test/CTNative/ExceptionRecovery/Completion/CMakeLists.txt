if(TARGET ctcompile-test-exception-recovery)
  target_sources(ctcompile-test-exception-recovery PRIVATE
    DOMURI.cpp
    ClassTransactions.cpp
    ClassTransactions.hpp
    DOMURI.hpp
  )
endif()
