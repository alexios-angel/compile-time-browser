if(TARGET ctcompile-test-escape-analysis-arrays)
  target_sources(ctcompile-test-escape-analysis-arrays PRIVATE
    main.cpp
    Harness.cpp
    Contents.cpp
    Objects.cpp
    Selectors.cpp
    Primitives.cpp
    BigIntErrors.cpp
    BigIntProducers.cpp
    BigIntStrings.cpp
    ControlFlow.cpp
    Harness.h
    Induction.h
    Length.h
    PrimitiveBinary.h
    Structured.h
  )
endif()

add_subdirectory(Induction)
add_subdirectory(Length)
add_subdirectory(Primitives)
add_subdirectory(Structured)
