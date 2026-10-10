# Sources of the DOM iterator proofs, including Close.cpp, which normalizes
# the handler that suppresses errors from an iterator's close call.
target_sources(${CTNATIVE_ANALYSIS_SOURCE_TARGET} PRIVATE
  Iteration.cpp
  Custom.cpp
  ExactPrimitives.cpp
  Close.cpp
)
