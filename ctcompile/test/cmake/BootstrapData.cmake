# Source-derived Bootstrap Data lifetime/publication probes. These measure
# interpreter behavior and native coverage separately; successful reference
# execution does not claim a native Bootstrap executable.
if(CTCOMPILE_ENABLE_MLIR AND TARGET ctjs-translate AND TARGET ctjs-opt
   AND TARGET ctcompile-test-native-reference AND Python3_EXECUTABLE)
  set(_bootstrap_data_probe "${CTBROWSER_MONOREPO_ROOT}/tools/check/bootstrap-data-probe.py")
  set(_bootstrap_data_source "${CTBROWSER_MONOREPO_ROOT}/ctbrowser/vendor/bootstrap/bootstrap.bundle.js")
  foreach(_mode commonjs browser amd)
    add_test(NAME ctcompile_bootstrap_host_slots_${_mode}
      COMMAND ${Python3_EXECUTABLE} "${CTBROWSER_MONOREPO_ROOT}/tools/check/bootstrap-host-slots.py"
        --bootstrap "${_bootstrap_data_source}" --mode ${_mode}
        --work "${CMAKE_CURRENT_BINARY_DIR}/bootstrap-host-slots/${_mode}"
        --translate $<TARGET_FILE:ctjs-translate> --opt $<TARGET_FILE:ctjs-opt>)
    add_test(NAME ctcompile_bootstrap_data_${_mode}
      COMMAND ${Python3_EXECUTABLE} "${_bootstrap_data_probe}"
        --bootstrap "${_bootstrap_data_source}" --mode ${_mode}
        --work "${CMAKE_CURRENT_BINARY_DIR}/bootstrap-data/${_mode}"
        --reference $<TARGET_FILE:ctcompile-test-native-reference>
        --translate $<TARGET_FILE:ctjs-translate> --opt $<TARGET_FILE:ctjs-opt>)
  endforeach()
  foreach(_mode commonjs browser browser_this_fallback global_reentry self_reentry
      commonjs_publication browser_publication browser_this_fallback_publication
      browser_method_replacement browser_table_replacement resource_instances)
    set(_prefix_source_mode "${_mode}")
    set(_prefix_options)
    set(_prefix_script FALSE)
    if(_mode MATCHES "_publication$")
      string(REGEX REPLACE "_publication$" "" _prefix_source_mode "${_mode}")
      set(_prefix_script TRUE)
    elseif(_mode MATCHES "^browser_(method|table)_replacement$")
      set(_prefix_options --replace "${CMAKE_MATCH_1}")
      set(_prefix_source_mode browser)
      set(_prefix_script TRUE)
    elseif(_mode STREQUAL "resource_instances")
      set(_prefix_script TRUE)
    endif()
    if(_prefix_script)
      list(APPEND _prefix_options --follow-publication)
    endif()
    set(_prefix_work "${CMAKE_CURRENT_BINARY_DIR}/bootstrap-host-prefix/${_mode}")
    set(_prefix_cpp "${_prefix_work}/wrapper.cpp")
    set(_prefix_expected "${_prefix_work}/expected.inc")
    set(_prefix_source "${_prefix_work}/program.js")
    add_custom_command(
      OUTPUT "${_prefix_cpp}" "${_prefix_expected}" "${_prefix_source}"
      COMMAND ${Python3_EXECUTABLE} "${CTBROWSER_MONOREPO_ROOT}/tools/check/bootstrap-host-prefix.py"
        --bootstrap "${_bootstrap_data_source}" --mode ${_prefix_source_mode}
        ${_prefix_options} --work "${_prefix_work}"
        --translate $<TARGET_FILE:ctjs-translate> --opt $<TARGET_FILE:ctjs-opt>
      DEPENDS ctjs-translate ctjs-opt "${_bootstrap_data_source}" "${_bootstrap_data_probe}"
        "${CTBROWSER_MONOREPO_ROOT}/tools/check/bootstrap-host-prefix.py"
      COMMENT "Checking and compiling host prefix ${_mode}"
      VERBATIM)
    set_source_files_properties("${_prefix_cpp}" PROPERTIES COMPILE_OPTIONS "${CTCOMPILE_GENERATED_WARNINGS}")
    set(_prefix_target "ctcompile-test-host-prefix-${_mode}")
    add_executable(${_prefix_target} HostPrefixDifferential.cpp "${_prefix_cpp}" "${_prefix_expected}")
    target_include_directories(${_prefix_target} PRIVATE "${_prefix_work}")
    target_link_libraries(${_prefix_target} PRIVATE ctbrowser::ctbrowser)
    ctcompile_target(${_prefix_target})
    if(_prefix_source_mode STREQUAL "browser_this_fallback")
      target_compile_definitions(${_prefix_target} PRIVATE CTCOMPILE_HOST_PREFIX_REALM_SLOT=1)
    endif()
    if(_prefix_script)
      target_compile_definitions(${_prefix_target} PRIVATE CTCOMPILE_HOST_PREFIX_SCRIPT=1)
    endif()
    if(_mode MATCHES "_reentry$")
      target_compile_definitions(${_prefix_target} PRIVATE CTCOMPILE_HOST_PREFIX_FUNCTIONS=2
        CTCOMPILE_HOST_PREFIX_ENTRY=1 CTCOMPILE_HOST_PREFIX_INVOCATIONS=2)
    elseif(_mode STREQUAL "resource_instances")
      target_compile_definitions(${_prefix_target} PRIVATE CTCOMPILE_HOST_PREFIX_FUNCTIONS=5
        CTCOMPILE_HOST_PREFIX_ENTRY=1 CTCOMPILE_HOST_PREFIX_INVOCATIONS=2)
    elseif(_prefix_script)
      set(_prefix_functions 7)
      if(_mode MATCHES "_replacement$")
        set(_prefix_functions 8)
      endif()
      target_compile_definitions(${_prefix_target} PRIVATE CTCOMPILE_HOST_PREFIX_FUNCTIONS=${_prefix_functions}
        CTCOMPILE_HOST_PREFIX_ENTRY=2 CTCOMPILE_HOST_PREFIX_INVOCATIONS=2)
    else()
      target_compile_definitions(${_prefix_target} PRIVATE CTCOMPILE_HOST_PREFIX_FUNCTIONS=7
        CTCOMPILE_HOST_PREFIX_ENTRY=2 CTCOMPILE_HOST_PREFIX_INVOCATIONS=1)
    endif()
    add_test(NAME ctcompile_bootstrap_host_prefix_${_mode}
      COMMAND ${_prefix_target} "${_prefix_source}")
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

if(CTCOMPILE_ENABLE_MLIR)
  add_executable(ctcompile-test-host-contract HostContract.cpp)
  target_link_libraries(ctcompile-test-host-contract PRIVATE CTNativeAnalysis MLIRParser)
  ctcompile_target(ctcompile-test-host-contract)
  add_test(NAME ctcompile_host_contract COMMAND ctcompile-test-host-contract)
endif()
