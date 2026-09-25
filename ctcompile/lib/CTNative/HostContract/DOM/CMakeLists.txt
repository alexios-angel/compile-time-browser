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
