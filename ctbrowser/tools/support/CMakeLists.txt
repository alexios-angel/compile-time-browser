# THE COMMAND-LINE HELPER: options.hpp, which the tools beside this directory and
# the test programs that take arguments (here and in ctcompile) parse argv
# through. Linking this target brings the header and Boost::program_options,
# found beside Boost.URL in cmake/dependencies.cmake.
add_library(ctbrowser-options INTERFACE)
target_include_directories(ctbrowser-options INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")
target_sources(ctbrowser-options INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/options.hpp")
target_link_libraries(ctbrowser-options INTERFACE Boost::program_options)
