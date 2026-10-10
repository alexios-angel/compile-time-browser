# Compiled CSS: ctcss-translate's output, built and checked against the parser.
#
# Each stylesheet below is compiled to a header at build time, and the header
# is compiled into ctcompile-test-css_emit under the project's warnings, so the
# test covers both that the emitted C++ builds and that it loads back into the
# stylesheet ctbrowser parses from the same file.
set(_css_bootstrap "${CTCOMPILE_MONOREPO_ROOT}/ctbrowser/vendor/bootstrap/bootstrap.css")
set(_css_features "${CMAKE_CURRENT_SOURCE_DIR}/features.css")

function(ctcompile_css_to_header name css)
  add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${name}.hpp"
    COMMAND $<TARGET_FILE:ctcompile-tool-ctcss-translate> --css-to-cpp "${css}" --name ${name}
            -o "${CMAKE_CURRENT_BINARY_DIR}/${name}.hpp"
    DEPENDS "${css}" ctcompile-tool-ctcss-translate
    COMMENT "Compiling ${name}.css to C++"
    VERBATIM)
endfunction()

ctcompile_css_to_header(features "${_css_features}")
ctcompile_css_to_header(bootstrap "${_css_bootstrap}")

ctcompile_unit_test(css_emit LIBS ctbrowser::style ARGS "${_css_features}" "${_css_bootstrap}")
target_sources(ctcompile-test-css_emit PRIVATE
  EmitRoundTrip.cpp
  "${CMAKE_CURRENT_BINARY_DIR}/features.hpp"
  "${CMAKE_CURRENT_BINARY_DIR}/bootstrap.hpp"
)
target_include_directories(ctcompile-test-css_emit PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
