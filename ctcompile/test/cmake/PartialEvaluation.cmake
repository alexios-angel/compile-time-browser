# The ordinary differential pipeline, with opt-in compile-time heap evaluation.
# Numeric observers receive heap arguments, so they remain runtime functions.
if(COMMAND ctcompile_add_native_pipeline)
  ctcompile_add_native_pipeline(partial_evaluation
    "${CMAKE_CURRENT_SOURCE_DIR}/native-partial-evaluation-fixture.js"
    lifetime42 PARTIAL_EVALUATE)
  ctcompile_add_native_pipeline(partial_prefix
    "${CMAKE_CURRENT_SOURCE_DIR}/native-partial-prefix-fixture.js"
    lifetime42 PARTIAL_EVALUATE)
endif()

# The exact vendor-derived Data initializer remains behind dynamic host effects.
if(TARGET ctjs-translate AND TARGET ctjs-opt AND TARGET ctcompile-test-native-reference AND Python3_EXECUTABLE)
  foreach(_control IN ITEMS observe negative)
    set(_control_arg "")
    if(_control STREQUAL "negative")
      set(_control_arg --negative-control)
    endif()
    add_test(NAME ctcompile_bootstrap_data_binding_time_${_control}
      COMMAND ${Python3_EXECUTABLE}
        "${CTBROWSER_MONOREPO_ROOT}/tools/check/bootstrap-data-binding-time.py"
        --bootstrap "${CTBROWSER_MONOREPO_ROOT}/ctbrowser/vendor/bootstrap/bootstrap.bundle.js"
        --work "${CMAKE_CURRENT_BINARY_DIR}/bootstrap-data-binding-time-${_control}"
        --reference $<TARGET_FILE:ctcompile-test-native-reference>
        --translate $<TARGET_FILE:ctjs-translate>
        --opt $<TARGET_FILE:ctjs-opt> ${_control_arg})
  endforeach()
endif()
