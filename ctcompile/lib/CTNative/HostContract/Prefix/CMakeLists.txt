# The host entry prefix interpreter (HostEntryPrefixAnalysis).
target_sources(${CTNATIVE_ANALYSIS_SOURCE_TARGET} PRIVATE
  Analysis.cpp
  Values.cpp
  Identity.cpp
  Factories.cpp
  Reads.cpp
  Mutations.cpp
  Analysis.h
)

# The prefix pass is built into the lowering library, not the analysis one.
target_sources(ctcompile-ctnative-lowering PRIVATE
  Pass.cpp
)
