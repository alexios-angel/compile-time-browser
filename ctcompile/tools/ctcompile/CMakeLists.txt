add_executable(ctcompile-tool ctcompile.cpp)
# THE WHOLE ENGINE, because packaging an application means LOADING it: the
# compiler asks the browser which scripts a page compiles and which resources it
# went looking for, rather than re-deriving either. A packager holding a second
# copy of those rules is a packager free to drift from the one that decides at
# run time, which presents as an application that quietly compiles from source.
target_link_libraries(ctcompile-tool PRIVATE ctcompile::support ctbrowser::ctbrowser
                                             Boost::program_options)
ctcompile_target(ctcompile-tool)
# The TARGET is ctcompile-tool and the BINARY is ctcompile: a target cannot
# share a name with the directory-level target CMake generates for the project,
# and `ctcompile` is what someone types.
set_target_properties(ctcompile-tool PROPERTIES OUTPUT_NAME ctcompile)
