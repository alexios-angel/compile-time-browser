# The ordinary differential pipeline, with opt-in compile-time heap evaluation.
# Numeric observers receive heap arguments, so they remain runtime functions.
if(COMMAND ctcompile_add_native_pipeline)
  ctcompile_add_native_pipeline(partial_evaluation
    "${CMAKE_CURRENT_SOURCE_DIR}/native-partial-evaluation-fixture.js"
    lifetime42 PARTIAL_EVALUATE)
endif()
