# THE IMPORTER, as an ORDINARY library rather than an add_mlir_library.
#
# It is not a dialect: it generates nothing, exports no TableGen target, and is
# hand-written C++ by the plan's own instruction. Using MLIR's helper made it
# compile through an `obj.CTJSImport` object target that
# target_compile_options(CTJSImport ...) does not reach - so it built at the
# compiler's default standard and the engine's C++23 headers did not parse, with
# errors naming atom.hpp rather than a standard version.
#
# ctcompile_target() gives it the project's flags and its C++23 floor, which is
# what every other library here is built with.
add_library(ctcompile-ctjs-import BytecodeImport.cpp)
ctcompile_target(ctcompile-ctjs-import)

# THE GENERATED HEADERS IT READS, as a build edge rather than an assumption -
# the same rule the dialect's own CMakeLists follows.
add_dependencies(ctcompile-ctjs-import MLIRCTJSOpsIncGen)

target_link_libraries(ctcompile-ctjs-import
  PUBLIC
    CTJSDialect
    ctbrowser::script
    MLIRIR
    MLIRControlFlowDialect)

# The generated headers are SYSTEM - see the note in ctcompile/CMakeLists.txt.
target_include_directories(ctcompile-ctjs-import SYSTEM
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>)

add_library(ctcompile::ctjs-import ALIAS ctcompile-ctjs-import)
