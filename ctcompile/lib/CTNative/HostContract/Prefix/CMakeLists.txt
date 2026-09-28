target_sources(${CTNATIVE_ANALYSIS_SOURCE_TARGET} PRIVATE
  Analysis.cpp
  Values.cpp
  Identity.cpp
  Factories.cpp
  Reads.cpp
  Mutations.cpp
  Analysis.h
)

target_sources(ctcompile-ctnative-lowering PRIVATE
  Pass.cpp
)
