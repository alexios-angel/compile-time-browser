# The ordinary differential pipeline, with opt-in compile-time heap evaluation.
# Numeric observers receive heap arguments, so they remain runtime functions.
if(COMMAND ctcompile_add_native_pipeline)
  ctcompile_add_native_pipeline(partial_evaluation
    "${CMAKE_CURRENT_SOURCE_DIR}/native-partial-evaluation-fixture.js"
    lifetime42 PARTIAL_EVALUATE)
  ctcompile_add_native_pipeline(partial_prefix
    "${CMAKE_CURRENT_SOURCE_DIR}/native-partial-prefix-fixture.js"
    lifetime42 PARTIAL_EVALUATE)
  ctcompile_add_native_pipeline(partial_closures
    "${CMAKE_CURRENT_SOURCE_DIR}/native-partial-closures-fixture.js"
    lifetime42 PARTIAL_EVALUATE)
  ctcompile_add_native_pipeline(symbolic
    "${CMAKE_CURRENT_SOURCE_DIR}/native-symbolic-fixture.js"
    lifetime42 PRECOMPUTE)
  ctcompile_add_native_pipeline(deforestation
    "${CMAKE_CURRENT_SOURCE_DIR}/native-deforestation-fixture.js"
    zz_loop13 DEFOREST)
  ctcompile_add_native_pipeline(specialization
    "${CMAKE_CURRENT_SOURCE_DIR}/native-specialization-fixture.js"
    lifetime42 SPECIALIZE PRECOMPUTE PARTIAL_EVALUATE)
  ctcompile_add_native_pipeline(supercompilation
    "${CMAKE_CURRENT_SOURCE_DIR}/native-supercompilation-fixture.js"
    lifetime42 SUPERCOMPILE)
endif()

# Native variants retain the original callable value for boxed dispatch. The
# generic body must still accept tuples used only by those redirected calls.
if(TARGET ctjs-translate AND TARGET ctjs-opt AND MLIR_TRANSLATE_EXE)
  set(_dispatch_js "${CMAKE_CURRENT_SOURCE_DIR}/specialization-dispatch.js")
  set(_dispatch_inc "${CMAKE_CURRENT_BINARY_DIR}/specialization-dispatch.js.inc")
  set(_dispatch_cpp "${CMAKE_CURRENT_BINARY_DIR}/specialization-dispatch.generated.cpp")
  add_custom_command(OUTPUT "${_dispatch_inc}"
    COMMAND ${CMAKE_COMMAND} -DSOURCE=${_dispatch_js} -DOUTPUT=${_dispatch_inc}
      -P "${CMAKE_CURRENT_SOURCE_DIR}/embed-js.cmake"
    DEPENDS "${_dispatch_js}" "${CMAKE_CURRENT_SOURCE_DIR}/embed-js.cmake" VERBATIM)
  add_custom_command(OUTPUT "${_dispatch_cpp}"
    COMMAND ${CMAKE_COMMAND}
      -DTRANSLATE=$<TARGET_FILE:ctjs-translate> -DOPT=$<TARGET_FILE:ctjs-opt>
      -DMLIR_TRANSLATE=${MLIR_TRANSLATE_EXE} -DSOURCE=${_dispatch_js}
      -DOUTPUT=${_dispatch_cpp}
      -P "${CMAKE_CURRENT_SOURCE_DIR}/compile-specialization-dispatch.cmake"
    DEPENDS "${_dispatch_js}" ctjs-translate ctjs-opt
      "${CMAKE_CURRENT_SOURCE_DIR}/compile-specialization-dispatch.cmake" VERBATIM)
  set_source_files_properties("${_dispatch_cpp}" PROPERTIES
    COMPILE_OPTIONS "${CTCOMPILE_GENERATED_WARNINGS}")
  add_executable(ctcompile-test-specialization-dispatch
    SpecializationDispatch.cpp "${_dispatch_cpp}" "${_dispatch_inc}")
  target_include_directories(ctcompile-test-specialization-dispatch PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}")
  target_link_libraries(ctcompile-test-specialization-dispatch PRIVATE ctbrowser::script)
  ctcompile_target(ctcompile-test-specialization-dispatch)
  add_test(NAME ctcompile_specialization_boxed_dispatch
    COMMAND ctcompile-test-specialization-dispatch)
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
