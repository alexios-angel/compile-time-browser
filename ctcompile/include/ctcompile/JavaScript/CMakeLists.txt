target_sources(ctcompile-javascript PUBLIC
  FILE_SET HEADERS
    BASE_DIRS "${PROJECT_SOURCE_DIR}/include"
    FILES
      ProgramComparator.hpp
)

target_sources(ctcompile-javascript PRIVATE
  EngineContract.hpp
  FrameEnds.def
)
