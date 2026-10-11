# === Native builds of ctbrowser's workload tests ===
#
# A `<name>.native` test is the same test source and expectations as `<name>`,
# built without the VM and registered through ctbrowser/cmake/run-without-vm.cmake,
# which fails the run when nm finds a VM symbol (or another forbidden one) in
# the executable. tools/check/check-workload.py's native-run stage finds it by
# that name.

# The native runtime's page views (CTNative/Runtime/Page.hpp) and the element
# members a workload's DOM reads use, over a ctbrowser::page::static_page. A
# test of its own so ctcompile-test-native-runtime links no page library; its
# source is added by Runtime/CMakeLists.txt.
ctcompile_unit_test(native-page LIBS ctbrowser::page)

# The Array.prototype callback methods (CTNative/Runtime/Array.hpp); its source
# is added by Runtime/CMakeLists.txt.
ctcompile_unit_test(native-array-callbacks LIBS ctbrowser::core)

# The native listener table (CTNative/Runtime/Events.hpp) over a static page;
# its source is added by Runtime/CMakeLists.txt.
ctcompile_unit_test(native-events LIBS ctbrowser::page)

# The native timer table (CTNative/Runtime/Timers.hpp); its source is added by
# Runtime/CMakeLists.txt.
ctcompile_unit_test(native-timers LIBS ctbrowser::core)

# The native promises and microtask queue (CTNative/Runtime/Promise.hpp and
# Microtasks.hpp), ordered against the timer table; its source is added by
# Runtime/CMakeLists.txt.
ctcompile_unit_test(native-promise LIBS ctbrowser::core)

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

# bootstrap_layout.native: unittests/unit/bootstrap_layout.cpp on a VM-free
# page (ctbrowser::page::static_page), with tools/check/css-dump.js compiled by
# the native pipeline into the C++ its engine calls. The rule is complete, but
# the target is EXCLUDE_FROM_ALL and its test is not registered yet:
# css-dump.js does not compile natively yet, so building it fails in
# pipeline.cmake, and `all` must keep working. Build it by name to try
# (ctbrowser-test-bootstrap_layout-native). Once css-dump.js compiles, drop
# EXCLUDE_FROM_ALL and register `bootstrap_layout.native` through
# ${_native_gate}, as css_syntax.native is.
if(CTCOMPILE_ENABLE_MLIR AND TARGET ctjs-translate AND TARGET ctjs-opt)
  set(_css_dump "${CTCOMPILE_MONOREPO_ROOT}/tools/check/css-dump.js")
  set(_css_dump_emitc "${CMAKE_CURRENT_BINARY_DIR}/css-dump.emitc.mlir")
  set(_css_dump_cpp "${CMAKE_CURRENT_BINARY_DIR}/css-dump.generated.cpp")
  add_custom_command(
    OUTPUT "${_css_dump_cpp}"
    COMMAND "${CMAKE_COMMAND}" -DTRANSLATE=$<TARGET_FILE:ctjs-translate>
            -DOPT=$<TARGET_FILE:ctjs-opt> -DSOURCE=${_css_dump}
            -DSTRING_ENTRIES=css_dump_table -DOUTPUT=${_css_dump_emitc}
            -P "${CMAKE_CURRENT_SOURCE_DIR}/CTNative/Checks/pipeline.cmake"
    COMMAND $<TARGET_FILE:ctjs-translate> --mlir-to-cpp "${_css_dump_emitc}" -o "${_css_dump_cpp}"
    DEPENDS "${_css_dump}" ctjs-translate ctjs-opt
            "${CMAKE_CURRENT_SOURCE_DIR}/CTNative/Checks/pipeline.cmake"
    COMMENT "Compiling css-dump.js to C++ through the native pipeline"
    VERBATIM)

  # The table native_engine.hpp reads: the script's path as the test loads it,
  # its top level, and its string entry.
  set(CTCOMPILE_WORKLOAD_SCRIPT "${_css_dump}")
  set(CTCOMPILE_WORKLOAD_ENTRY css_dump_table)
  set(CTCOMPILE_WORKLOAD_GENERATED "${_css_dump_cpp}")
  configure_file("${CMAKE_CURRENT_SOURCE_DIR}/CTNative/Workloads/native-scripts.cpp.in"
                 "${CMAKE_CURRENT_BINARY_DIR}/bootstrap_layout_scripts.cpp" @ONLY)
  unset(CTCOMPILE_WORKLOAD_SCRIPT)
  unset(CTCOMPILE_WORKLOAD_ENTRY)
  unset(CTCOMPILE_WORKLOAD_GENERATED)
  set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/bootstrap_layout_scripts.cpp"
                              PROPERTIES OBJECT_DEPENDS "${_css_dump_cpp}")

  add_executable(ctbrowser-test-bootstrap_layout-native EXCLUDE_FROM_ALL
    "${_native_ctbrowser}/unittests/unit/bootstrap_layout.cpp"
    "${CMAKE_CURRENT_BINARY_DIR}/bootstrap_layout_scripts.cpp")
  target_compile_definitions(ctbrowser-test-bootstrap_layout-native PRIVATE
    CTBROWSER_TEST_NATIVE=1)
  target_link_libraries(ctbrowser-test-bootstrap_layout-native PRIVATE ctbrowser::page)
  ctcompile_target(ctbrowser-test-bootstrap_layout-native)

  # dom-reads.test: CTNative/Browser/page/dom-reads.js over the interpreter
  # and compiled natively, through the same engine seam and wrapper as
  # bootstrap_layout.native. The test diffs the two answers.
  set(_dom_reads_dir "${CMAKE_CURRENT_SOURCE_DIR}/CTNative/Browser/page")
  set(_dom_reads_emitc "${CMAKE_CURRENT_BINARY_DIR}/dom-reads.emitc.mlir")
  set(_dom_reads_cpp "${CMAKE_CURRENT_BINARY_DIR}/dom-reads.generated.cpp")
  add_custom_command(
    OUTPUT "${_dom_reads_cpp}"
    COMMAND "${CMAKE_COMMAND}" -DTRANSLATE=$<TARGET_FILE:ctjs-translate>
            -DOPT=$<TARGET_FILE:ctjs-opt> -DSOURCE=${_dom_reads_dir}/dom-reads.js
            -DSTRING_ENTRIES=dom_reads -DOUTPUT=${_dom_reads_emitc}
            -P "${CMAKE_CURRENT_SOURCE_DIR}/CTNative/Checks/pipeline.cmake"
    COMMAND $<TARGET_FILE:ctjs-translate> --mlir-to-cpp "${_dom_reads_emitc}" -o "${_dom_reads_cpp}"
    DEPENDS "${_dom_reads_dir}/dom-reads.js" ctjs-translate ctjs-opt
            "${CMAKE_CURRENT_SOURCE_DIR}/CTNative/Checks/pipeline.cmake"
    COMMENT "Compiling dom-reads.js to C++ through the native pipeline"
    VERBATIM)
  set(CTCOMPILE_WORKLOAD_SCRIPT "${_dom_reads_dir}/dom-reads.js")
  set(CTCOMPILE_WORKLOAD_ENTRY dom_reads)
  set(CTCOMPILE_WORKLOAD_GENERATED "${_dom_reads_cpp}")
  configure_file("${CMAKE_CURRENT_SOURCE_DIR}/CTNative/Workloads/native-scripts.cpp.in"
                 "${CMAKE_CURRENT_BINARY_DIR}/dom_reads_scripts.cpp" @ONLY)
  unset(CTCOMPILE_WORKLOAD_SCRIPT)
  unset(CTCOMPILE_WORKLOAD_ENTRY)
  unset(CTCOMPILE_WORKLOAD_GENERATED)
  set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/dom_reads_scripts.cpp"
                              PROPERTIES OBJECT_DEPENDS "${_dom_reads_cpp}")
  add_executable(ctcompile-test-dom-reads "${_dom_reads_dir}/dom_reads.cpp")
  target_link_libraries(ctcompile-test-dom-reads PRIVATE ctbrowser::ctbrowser)
  ctcompile_target(ctcompile-test-dom-reads)
  add_executable(ctcompile-test-dom-reads-native "${_dom_reads_dir}/dom_reads.cpp"
    "${CMAKE_CURRENT_BINARY_DIR}/dom_reads_scripts.cpp")
  target_compile_definitions(ctcompile-test-dom-reads-native PRIVATE CTBROWSER_TEST_NATIVE=1)
  target_link_libraries(ctcompile-test-dom-reads-native PRIVATE ctbrowser::page)
  ctcompile_target(ctcompile-test-dom-reads-native)

  # A page test built twice from CTNative/Browser/page/<name>.cpp: over the
  # interpreter, and natively over <name>.js compiled through the native
  # pipeline with `entry` as its string entry. <name>.test diffs the two.
  function(ctcompile_page_workload name entry)
    set(emitc "${CMAKE_CURRENT_BINARY_DIR}/${name}.emitc.mlir")
    set(cpp "${CMAKE_CURRENT_BINARY_DIR}/${name}.generated.cpp")
    add_custom_command(
      OUTPUT "${cpp}"
      COMMAND "${CMAKE_COMMAND}" -DTRANSLATE=$<TARGET_FILE:ctjs-translate>
              -DOPT=$<TARGET_FILE:ctjs-opt> -DSOURCE=${_dom_reads_dir}/${name}.js
              -DSTRING_ENTRIES=${entry} -DOUTPUT=${emitc}
              -P "${CMAKE_CURRENT_SOURCE_DIR}/CTNative/Checks/pipeline.cmake"
      COMMAND $<TARGET_FILE:ctjs-translate> --mlir-to-cpp "${emitc}" -o "${cpp}"
      DEPENDS "${_dom_reads_dir}/${name}.js" ctjs-translate ctjs-opt
              "${CMAKE_CURRENT_SOURCE_DIR}/CTNative/Checks/pipeline.cmake"
      COMMENT "Compiling ${name}.js to C++ through the native pipeline"
      VERBATIM)
    set(CTCOMPILE_WORKLOAD_SCRIPT "${_dom_reads_dir}/${name}.js")
    set(CTCOMPILE_WORKLOAD_ENTRY ${entry})
    set(CTCOMPILE_WORKLOAD_GENERATED "${cpp}")
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/CTNative/Workloads/native-scripts.cpp.in"
                   "${CMAKE_CURRENT_BINARY_DIR}/${name}_scripts.cpp" @ONLY)
    set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/${name}_scripts.cpp"
                                PROPERTIES OBJECT_DEPENDS "${cpp}")
    add_executable(ctcompile-test-${name} "${_dom_reads_dir}/${name}.cpp")
    target_link_libraries(ctcompile-test-${name} PRIVATE ctbrowser::ctbrowser)
    ctcompile_target(ctcompile-test-${name})
    add_executable(ctcompile-test-${name}-native "${_dom_reads_dir}/${name}.cpp"
      "${CMAKE_CURRENT_BINARY_DIR}/${name}_scripts.cpp")
    target_compile_definitions(ctcompile-test-${name}-native PRIVATE CTBROWSER_TEST_NATIVE=1)
    target_link_libraries(ctcompile-test-${name}-native PRIVATE ctbrowser::page)
    ctcompile_target(ctcompile-test-${name}-native)
  endfunction()
  # Listeners run when the test clicks the page: dispatched by the browser,
  # and by the native listener table (Runtime/Events.hpp).
  ctcompile_page_workload(events events_report)
  # Timers run when the test moves the clock: by the browser's tick, and by
  # the native timer table (Runtime/Timers.hpp).
  ctcompile_page_workload(timers timers_report)
endif()
