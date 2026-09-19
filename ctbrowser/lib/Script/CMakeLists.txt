# ctbrowser.script - JS -> bytecode -> a register machine over NaN-boxed values, + stdlib.
#
# Reuses ctjs's Pratt parser (vparse.hpp) and replaces everything after it.
# Depends on core only for the warning set - the VM shares no state with the
# DOM by design (one agent per thread). Dependencies are found in
# cmake/dependencies.cmake. Each subdirectory's internal.hpp is private to it
# and deliberately in NO file set.
add_library(ctbrowser-script)
# The .cpp list is globbed like every other subsystem's; the header list is
# NOT, because include/ctbrowser/script/ holds dispatch.hpp, which vm.hpp
# includes but which has never been installed. Globbing would ship it, so the
# list stays what it was until someone decides that on purpose.
file(GLOB_RECURSE script-sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
target_sources(ctbrowser-script
  PRIVATE ${script-sources}
  PUBLIC
    FILE_SET HEADERS
      BASE_DIRS "${CTBROWSER_INCLUDE_DIR}"
      FILES
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/script/script.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/script/value.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/script/bytecode.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/script/program_image.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/script/bigint.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/script/compile.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/script/source_lines.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/script/vm.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/script/type_record.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/script/builtins.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/script/regex.hpp")

# Public value types and VM inline definitions are installed with their entry headers.
target_sources(ctbrowser-script PUBLIC FILE_SET HEADERS FILES
  "${CTBROWSER_INCLUDE_DIR}/ctbrowser/script/value/arrays.hpp"
  "${CTBROWSER_INCLUDE_DIR}/ctbrowser/script/value/base.hpp"
  "${CTBROWSER_INCLUDE_DIR}/ctbrowser/script/value/objects.hpp"
  "${CTBROWSER_INCLUDE_DIR}/ctbrowser/script/vm/execution_inline.hpp"
  "${CTBROWSER_INCLUDE_DIR}/ctbrowser/script/vm/frames_inline.hpp"
  "${CTBROWSER_INCLUDE_DIR}/ctbrowser/script/vm/gc_inline.hpp"
  "${CTBROWSER_INCLUDE_DIR}/ctbrowser/script/vm/objects.hpp"
  "${CTBROWSER_INCLUDE_DIR}/ctbrowser/script/vm/properties_inline.hpp"
)
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

# Public X-macro helper declarations include these table sections.
target_sources(ctbrowser-script PUBLIC FILE_SET HEADERS FILES
  "${CTBROWSER_INCLUDE_DIR}/ctbrowser/aot/aot_helpers.def"
  "${CTBROWSER_INCLUDE_DIR}/ctbrowser/aot/helpers/frames-and-bindings.def"
  "${CTBROWSER_INCLUDE_DIR}/ctbrowser/aot/helpers/values-and-properties.def"
  "${CTBROWSER_INCLUDE_DIR}/ctbrowser/aot/helpers/calls-and-modules.def"
)
