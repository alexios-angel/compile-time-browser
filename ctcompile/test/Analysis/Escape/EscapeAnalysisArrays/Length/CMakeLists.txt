if(TARGET ctcompile-test-escape-analysis-arrays)
  target_sources(ctcompile-test-escape-analysis-arrays PRIVATE
    Setup.cpp
    IndexSnapshots.cpp
    Arithmetic.cpp
    ShrinkArithmetic.cpp
    ShrinkRetention.cpp
    LiveMutations.cpp
    Finish.cpp
    Cases.cpp
    Cases.hpp
  )
endif()
