# DOM host-contract support. The entry and source checks build into the analysis
# library; Preparation.cpp builds into the lowering library.

target_sources(${CTNATIVE_ANALYSIS_SOURCE_TARGET} PRIVATE
  Entry.cpp
  Source.cpp
)

target_sources(ctcompile-ctnative-lowering PRIVATE
  Preparation.cpp
)

add_subdirectory(Entry)
add_subdirectory(Iteration)
add_subdirectory(Snapshot)
add_subdirectory(Source)
