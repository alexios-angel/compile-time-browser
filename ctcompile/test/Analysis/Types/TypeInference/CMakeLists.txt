if(TARGET ctcompile-test-type-inference)
  target_sources(ctcompile-test-type-inference PRIVATE
    Main.cpp
    Helpers.cpp
    IdentityFields.cpp
    MapPresence.cpp
    MapDeletePresence.cpp
    FieldEffects.cpp
    ComparisonIdentity.cpp
    ScalarGlobals.cpp
    Tests.h
  )
endif()
