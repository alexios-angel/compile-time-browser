list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")
include(TableGen)
set(LLVM_TABLEGEN_EXE "$<TARGET_FILE:llvm-tblgen>")
set(LLVM_TARGET_DEFINITIONS Options.td)
tablegen(LLVM Options.inc -gen-opt-parser-defs EXTRA_INCLUDES ${LLVM_INCLUDE_DIRS})

add_executable(ctcompile-tool ctcompile.cpp ${TABLEGEN_OUTPUT})
# Links the whole engine, because packaging an application means loading it:
# the compiler asks the browser which scripts a page compiles and which
# resources it requested, rather than re-deriving either. A packager with its
# own copy of those rules could drift from the one that decides at run time,
# and the application would then silently compile from source.
target_link_libraries(ctcompile-tool PRIVATE ctcompile::support ctbrowser::ctbrowser
                                             LLVMOption)
target_include_directories(ctcompile-tool SYSTEM PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
ctcompile_target(ctcompile-tool)
# The target is ctcompile-tool and the binary is ctcompile: a target cannot
# share a name with the directory-level target CMake generates for the
# project, and `ctcompile` is what someone types.
set_target_properties(ctcompile-tool PROPERTIES OUTPUT_NAME ctcompile)
