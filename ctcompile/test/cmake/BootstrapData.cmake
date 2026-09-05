# Source-derived Bootstrap Data lifetime/publication probes. These measure
# interpreter behavior and native coverage separately; successful reference
# execution does not claim a native Bootstrap executable.
if(CTCOMPILE_ENABLE_MLIR AND TARGET ctjs-translate AND TARGET ctjs-opt
   AND TARGET ctcompile-test-native-reference AND Python3_EXECUTABLE)
  set(_bootstrap_data_probe "${CTBROWSER_MONOREPO_ROOT}/tools/check/bootstrap-data-probe.py")
  set(_bootstrap_data_source "${CTBROWSER_MONOREPO_ROOT}/ctbrowser/vendor/bootstrap/bootstrap.bundle.js")
  foreach(_mode commonjs browser amd)
    add_test(NAME ctcompile_bootstrap_data_${_mode}
      COMMAND ${Python3_EXECUTABLE} "${_bootstrap_data_probe}"
        --bootstrap "${_bootstrap_data_source}" --mode ${_mode}
        --work "${CMAKE_CURRENT_BINARY_DIR}/bootstrap-data/${_mode}"
        --reference $<TARGET_FILE:ctcompile-test-native-reference>
        --translate $<TARGET_FILE:ctjs-translate> --opt $<TARGET_FILE:ctjs-opt>)
  endforeach()
  # Mutate an executed observation and the real source boundary separately.
  # Each control accepts only its exact intended diagnostic, never a crash or
  # a missing executable. Separate output directories also permit ctest -j.
  foreach(_control trace source-boundary)
    string(REPLACE "-" "_" _test_control "${_control}")
    add_test(NAME ctcompile_bootstrap_data_${_test_control}_control
      COMMAND ${Python3_EXECUTABLE} "${_bootstrap_data_probe}"
        --bootstrap "${_bootstrap_data_source}" --mode commonjs
        --work "${CMAKE_CURRENT_BINARY_DIR}/bootstrap-data/control-${_control}"
        --reference $<TARGET_FILE:ctcompile-test-native-reference>
        --translate $<TARGET_FILE:ctjs-translate> --opt $<TARGET_FILE:ctjs-opt>
        --negative-control ${_control})
  endforeach()
endif()
