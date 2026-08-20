add_executable(ctcompile-tool ctcompile.cpp)
target_link_libraries(ctcompile-tool PRIVATE ctcompile::support Boost::program_options)
ctcompile_target(ctcompile-tool)
# The TARGET is ctcompile-tool and the BINARY is ctcompile: a target cannot
# share a name with the directory-level target CMake generates for the project,
# and `ctcompile` is what someone types.
set_target_properties(ctcompile-tool PROPERTIES OUTPUT_NAME ctcompile)
