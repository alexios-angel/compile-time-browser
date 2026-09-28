target_sources(${CTNATIVE_ANALYSIS_SOURCE_TARGET} PRIVATE
  Contract.cpp
  Values.cpp
  CapturedMapBody.cpp
  ScalarCallbacks.cpp
  Slots.cpp
  Analysis.cpp
  Analysis.h
)

target_sources(ctcompile-ctnative-lowering PRIVATE
  Pass.cpp
  ClassInitialization.cpp
  Preparation.h
)

add_subdirectory(CapturedMapBody)
add_subdirectory(ClassInitialization)
add_subdirectory(DOM)
add_subdirectory(Prefix)
add_subdirectory(Provider)
add_subdirectory(Values)
