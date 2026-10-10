# ctbrowser-options: options.hpp, the argv parser used by the tools here and by
# test programs that take arguments (in ctbrowser and ctcompile). Linking it adds
# Boost::program_options, found in ctbrowser/cmake/dependencies.cmake.
add_library(ctbrowser-options INTERFACE)
target_include_directories(ctbrowser-options INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")
target_sources(ctbrowser-options INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/options.hpp")
target_link_libraries(ctbrowser-options INTERFACE Boost::program_options)
