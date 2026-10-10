# The ordinary differential pipeline, with opt-in compile-time heap evaluation.
# Numeric observers receive heap arguments, so they remain runtime functions.
ctcompile_native_fixture(object_values Objects/values.js)
ctcompile_native_fixture(object_fields Objects/fields.js)
ctcompile_native_fixture(partial_evaluation Optimization/partial/evaluation.js PARTIAL_EVALUATE)
ctcompile_native_fixture(partial_prefix Optimization/partial/prefix.js PARTIAL_EVALUATE)
ctcompile_native_fixture(partial_closures Optimization/partial/closures.js PARTIAL_EVALUATE)
ctcompile_native_fixture(symbolic Optimization/symbolic.js PRECOMPUTE)
ctcompile_native_fixture(deforestation Optimization/deforestation.js DEFOREST)
ctcompile_native_fixture(specialization Optimization/specialization.js
                         SPECIALIZE PRECOMPUTE PARTIAL_EVALUATE)
ctcompile_native_fixture(reachability Optimization/reachability.js
                         SPECIALIZE PARTIAL_EVALUATE PRUNE_UNREACHABLE)
ctcompile_native_fixture(supercompilation Optimization/supercompilation.js SUPERCOMPILE)
ctcompile_native_fixture(supercompilation_generalization
                         Optimization/supercompilation-generalization.js SUPERCOMPILE)

# Native variants retain the original callable value for boxed dispatch. The
# generic body must still accept tuples used only by those redirected calls.
if(TARGET ctjs-translate AND TARGET ctjs-opt AND MLIR_TRANSLATE_EXE)
  set(_dispatch_js "${CMAKE_CURRENT_SOURCE_DIR}/CTNative/Specialization/dispatch.js")
  set(_dispatch_inc "${CMAKE_CURRENT_BINARY_DIR}/specialization-dispatch.js.inc")
  set(_dispatch_cpp "${CMAKE_CURRENT_BINARY_DIR}/specialization-dispatch.generated.cpp")
  ctcompile_embed_js(specialization-dispatch.js.inc "${_dispatch_js}")
  add_custom_command(OUTPUT "${_dispatch_cpp}"
    COMMAND ${CMAKE_COMMAND}
      -DTRANSLATE=$<TARGET_FILE:ctjs-translate> -DOPT=$<TARGET_FILE:ctjs-opt>
      -DMLIR_TRANSLATE=${MLIR_TRANSLATE_EXE} -DSOURCE=${_dispatch_js}
      -DOUTPUT=${_dispatch_cpp}
      -P "${CMAKE_CURRENT_SOURCE_DIR}/CTNative/Specialization/compile-dispatch.cmake"
    DEPENDS "${_dispatch_js}" ctjs-translate ctjs-opt
      "${CMAKE_CURRENT_SOURCE_DIR}/CTNative/Specialization/compile-dispatch.cmake" VERBATIM)
  set_source_files_properties("${_dispatch_cpp}" PROPERTIES
    COMPILE_OPTIONS "${CTCOMPILE_GENERATED_WARNINGS}")
  add_executable(ctcompile-test-specialization-dispatch "${_dispatch_cpp}" "${_dispatch_inc}")
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
