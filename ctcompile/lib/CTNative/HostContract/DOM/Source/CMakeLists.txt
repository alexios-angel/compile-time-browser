# The DOM helper source proof (DOMSource, see Proof.hpp); ../Source.cpp is its driver.
target_sources(${CTNATIVE_ANALYSIS_SOURCE_TARGET} PRIVATE
  Captures.cpp
  Entry.cpp
  Completion.cpp
  Bodies.cpp
  Expansion.cpp
  LocalState.cpp
  Proof.hpp
)
