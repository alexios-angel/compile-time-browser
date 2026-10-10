# === Native builds of ctbrowser's workload tests ===
#
# A `<name>.native` test is the same test source and expectations as `<name>`,
# built without the VM and registered through ctbrowser/cmake/run-without-vm.cmake,
# which fails the run when nm finds a VM symbol (or another forbidden one) in
# the executable. tools/check/check-workload.py's native-run stage finds it by
# that name.

set(_native_ctbrowser "${CTCOMPILE_MONOREPO_ROOT}/ctbrowser")
set(_native_gate "${_native_ctbrowser}/cmake/run-without-vm.cmake")
set(_native_gate_nm "${CMAKE_NM}")
if(NOT _native_gate_nm)
  find_program(_native_gate_nm NAMES llvm-nm nm REQUIRED)
endif()

# css_syntax.native: unittests/unit/css/syntax.cpp checked against the CSS
# ctcss-translate compiled from syntax.inputs.json. It links ctbrowser's style
# library for the types and css::materialize, and must not link the tokenizer
# or the parser: the gate rejects the run if nm finds them.
set(_css_syntax_inputs "${_native_ctbrowser}/unittests/unit/css/syntax.inputs.json")
add_custom_command(
  OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/css_syntax_inputs.cpp"
  COMMAND $<TARGET_FILE:ctcompile-tool-ctcss-translate> --manifest "${_css_syntax_inputs}"
          --root "${_native_ctbrowser}" -o "${CMAKE_CURRENT_BINARY_DIR}/css_syntax_inputs.cpp"
  DEPENDS "${_css_syntax_inputs}" "${_native_ctbrowser}/vendor/bootstrap/bootstrap.css"
          ctcompile-tool-ctcss-translate
  COMMENT "Compiling css_syntax's inputs to C++"
  VERBATIM)
add_executable(ctbrowser-test-css_syntax-native
  "${_native_ctbrowser}/unittests/unit/css/syntax.cpp"
  "${CMAKE_CURRENT_BINARY_DIR}/css_syntax_inputs.cpp")
target_compile_definitions(ctbrowser-test-css_syntax-native PRIVATE CTBROWSER_TEST_NATIVE=1)
target_link_libraries(ctbrowser-test-css_syntax-native PRIVATE ctbrowser::style)
ctcompile_target(ctbrowser-test-css_syntax-native)

set(_css_parse_symbols
    "ctbrowser::style::css::(tokenize|parse_stylesheet|parse_declaration_list|parse_selector_text|supports_selector)\\(")
add_test(NAME css_syntax.native
         COMMAND "${CMAKE_COMMAND}" "-DNM=${_native_gate_nm}"
                 "-DEXE=$<TARGET_FILE:ctbrowser-test-css_syntax-native>"
                 "-DFORBID=${_css_parse_symbols}" -P "${_native_gate}"
         WORKING_DIRECTORY "${_native_ctbrowser}")
set_tests_properties(css_syntax.native PROPERTIES ENVIRONMENT "CTBROWSER_TEST_ENGINE=native")

# The gate must be able to fail on parsing alone: this control links what
# css_syntax.native links, no VM, and calls the tokenizer, so only the FORBID
# clause can refuse it. Its source is added by CSS/CMakeLists.txt.
add_executable(ctcompile-test-css_parse_control)
target_link_libraries(ctcompile-test-css_parse_control PRIVATE ctbrowser::style)
ctcompile_target(ctcompile-test-css_parse_control)
add_test(NAME css_syntax_native_gate_control
         COMMAND "${CMAKE_COMMAND}" "-DNM=${_native_gate_nm}"
                 "-DEXE=$<TARGET_FILE:ctcompile-test-css_parse_control>"
                 "-DFORBID=${_css_parse_symbols}" -P "${_native_gate}")
set_tests_properties(css_syntax_native_gate_control PROPERTIES WILL_FAIL TRUE)
