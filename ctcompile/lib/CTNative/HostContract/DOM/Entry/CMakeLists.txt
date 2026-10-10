# Sources of the DOM entry-function proof: Body (the per-operation walk) and
# the files that handle its control flow, browser calls and Maps.
target_sources(${CTNATIVE_ANALYSIS_SOURCE_TARGET} PRIVATE
  Body.cpp
  ControlFlow.cpp
  BrowserOperations.cpp
  Maps.cpp
  Body.hpp
)
