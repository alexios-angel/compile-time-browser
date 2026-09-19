# Shared assertions and probes; no object code or runtime library.
add_library(ctbrowser-test-support INTERFACE)
target_include_directories(ctbrowser-test-support INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")
target_sources(ctbrowser-test-support INTERFACE
  "${CMAKE_CURRENT_SOURCE_DIR}/check.hpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/dom_probe.hpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/js_expect.hpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/ratchet.hpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/api_pawl.hpp"
)
