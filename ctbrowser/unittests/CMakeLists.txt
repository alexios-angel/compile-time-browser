# The focused tests: unit/ one file per subsystem, js/ the language suites.
#
# Put the executables directly in this binary directory, where test scripts
# and local workflows look for them.
if(NOT DEFINED CMAKE_RUNTIME_OUTPUT_DIRECTORY)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
endif()
add_subdirectory(unit)
add_subdirectory(js)

# Public Core clients and the header-only listener registry run without the
# browser or interpreter.
foreach(client core_basics core_text core_uri core_json listener_registry)
  get_target_property(_core_libs ctbrowser-test-${client} LINK_LIBRARIES)
  list(FILTER _core_libs EXCLUDE REGEX "^ctbrowser::(dom|script|style|layout|paint|raster|shell|ctbrowser)$")
  set_property(TARGET ctbrowser-test-${client} PROPERTY LINK_LIBRARIES "${_core_libs}")
endforeach()

# The native DOM clients must link without the interpreter or Shell.
# Keep the test helper's configured trace libraries and DOM/Core dependencies.
foreach(client dom_token_list dom_element dom_shadow dom_dataset)
  get_target_property(_dom_libs ctbrowser-test-${client} LINK_LIBRARIES)
  list(FILTER _dom_libs EXCLUDE REGEX "^ctbrowser::(script|style|layout|paint|raster|shell|ctbrowser)$")
  set_property(TARGET ctbrowser-test-${client} PROPERTY LINK_LIBRARIES "${_dom_libs}")
endforeach()

# Public Style clients must also link without Script or Shell.
foreach(client style_easing style_closest)
  get_target_property(_style_libs ctbrowser-test-${client} LINK_LIBRARIES)
  list(FILTER _style_libs EXCLUDE REGEX "^ctbrowser::(script|layout|paint|raster|shell|ctbrowser)$")
  set_property(TARGET ctbrowser-test-${client} PROPERTY LINK_LIBRARIES "${_style_libs}")
endforeach()

# vm_operators (js/vm/operators.cpp) is the one test that still links ctjs, the
# older constexpr JavaScript interpreter: `diff_vs_v1` runs each expression
# through it as well, and ctjs's public header needs ctc's include path too.
target_include_directories(ctbrowser-test-vm_operators PRIVATE
  "${CTBROWSER_CTJS}/include"
  "${CTBROWSER_CTC}/include")

# --- Precompiled header: parse the umbrella once, not once per test ----------
#
# `<ctbrowser.hpp>` is the one-include public API, and parsing it costs ~2.9 s
# (measured; an empty TU is 0.01 s). Across ~480 test TUs that is ~1,400 s,
# over half of the suite's build. Building it once here and reusing it across
# the targets roughly halves the unittests' compile time.
#
# Left out: tests that deliberately link a subset of the libraries (the three
# foreach blocks above) and do not include the umbrella; forcing it onto them
# would reference libraries their link line drops. `selectors` is the
# arbitrary full-engine anchor that builds the shared PCH. timer_queue uses
# only its public scheduling header, and listener_registry its registration
# header, so their binaries can be checked without Script.
set(_pch_anchor ctbrowser-test-selectors)
target_precompile_headers(${_pch_anchor} PRIVATE <ctbrowser.hpp>)
set(_pch_excluded core_basics core_text core_uri core_json
                  dom_token_list dom_element dom_shadow dom_dataset
                  style_easing style_closest timer_queue listener_registry)
# A directory's targets and those of every directory below it, because tests
# live in subfolders (unit/aot/, js/vm/).
function(ctbrowser_targets_below dir out)
  get_property(targets DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)
  get_property(children DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
  foreach(child IN LISTS children)
    ctbrowser_targets_below("${child}" below)
    list(APPEND targets ${below})
  endforeach()
  set(${out} ${targets} PARENT_SCOPE)
endfunction()
ctbrowser_targets_below(unit _unit_targets)
ctbrowser_targets_below(js _js_targets)
foreach(_pch_target IN LISTS _unit_targets _js_targets)
  string(REPLACE "ctbrowser-test-" "" _pch_name "${_pch_target}")
  if(_pch_name STREQUAL selectors OR _pch_name IN_LIST _pch_excluded)
    continue()
  endif()
  target_precompile_headers(ctbrowser-test-${_pch_name} REUSE_FROM ${_pch_anchor})
endforeach()
