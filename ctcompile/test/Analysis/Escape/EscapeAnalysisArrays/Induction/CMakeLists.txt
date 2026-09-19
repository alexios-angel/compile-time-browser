if(TARGET ctcompile-test-escape-analysis-arrays)
  target_sources(ctcompile-test-escape-analysis-arrays PRIVATE
    Setup.cpp
    OverwritesAndTransport.cpp
    InvariantReads.cpp
    SignedStrides.cpp
    SignedArithmetic.cpp
    PowersAndProducts.cpp
    Validation.cpp
    Finish.cpp
    Cases.cpp
    Cases.hpp
  )
endif()
