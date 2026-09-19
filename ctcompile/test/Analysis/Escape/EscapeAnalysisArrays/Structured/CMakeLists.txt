if(TARGET ctcompile-test-escape-analysis-arrays)
  target_sources(ctcompile-test-escape-analysis-arrays PRIVATE
    Setup.cpp
    Reloads.cpp
    InvariantArithmetic.cpp
    SignedArithmetic.cpp
    PowersAndProducts.cpp
    MutationRefusals.cpp
    Finish.cpp
    Cases.cpp
    Cases.hpp
  )
endif()
