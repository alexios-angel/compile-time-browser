# ctcss-translate: a stylesheet to C++ constants that load without parsing.
# Plain C++ over ctbrowser's VM-free style library and LLVM Support for the
# command line and the SHA-256; it needs no MLIR, so it builds in every
# configuration.
add_executable(ctcompile-tool-ctcss-translate ctcss-translate.cpp)
target_link_libraries(ctcompile-tool-ctcss-translate PRIVATE LLVMSupport ctcompile::css
                                                             ctbrowser::style)
ctcompile_target(ctcompile-tool-ctcss-translate)
set_target_properties(ctcompile-tool-ctcss-translate PROPERTIES OUTPUT_NAME ctcss-translate)
