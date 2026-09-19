# ctbrowser.script - JS -> bytecode -> a register machine over NaN-boxed values, + stdlib.
#
# Reuses ctjs's Pratt parser (vparse.hpp) and replaces everything after it.
# Depends on core only for the warning set - the VM shares no state with the
# DOM by design (one agent per thread). Dependencies are found in
# cmake/dependencies.cmake. Each subdirectory's internal.hpp is private to it
# and is registered only as a private source.
add_library(ctbrowser-script)
target_sources(ctbrowser-script PUBLIC FILE_SET HEADERS BASE_DIRS "${CTBROWSER_INCLUDE_DIR}")
target_sources(ctbrowser-script PRIVATE
  aot_contract.cpp
  bigint.cpp
  builtins.cpp
  compile.cpp
  dispatch.cpp
  regex_properties.cpp
  regex_properties.inc
  source_lines.cpp
  type_record.cpp
)

add_subdirectory(aot_bridge)
add_subdirectory(builtins)
add_subdirectory(compile)
add_subdirectory(program_image)
add_subdirectory(regex_properties)
add_subdirectory(vm)

target_compile_features(ctbrowser-script PUBLIC cxx_std_23)

# Both PRIVATE, and they must be: neither changes the shape of a public
# header. The dispatch strategy is run_loop's alone; the debug side tables are
# read through `debug_names_enabled()`. Gating a member on a flag would compile
# this library against one `context` and every consumer against another -
# which links, and then reads the wrong offsets.
if(CTBROWSER_COMPUTED_GOTO)
  target_compile_definitions(ctbrowser-script PRIVATE CTBROWSER_COMPUTED_GOTO)
endif()
if(NOT CTBROWSER_SCRIPT_DEBUG_NAMES)
  target_compile_definitions(ctbrowser-script PRIVATE CTBROWSER_SCRIPT_DEBUG_NAMES=0)
endif()

ctbrowser_target(ctbrowser-script)
target_include_directories(ctbrowser-script PRIVATE
  "${CTBROWSER_CTJS}/include")
target_link_libraries(ctbrowser-script PUBLIC ctbrowser-core Boost::headers)

add_library(ctbrowser::script ALIAS ctbrowser-script)
