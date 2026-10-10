# The bytecode importer, as an ordinary library rather than an add_mlir_library.
#
# It is not a dialect: it generates nothing and exports no TableGen target.
# add_mlir_library would compile it through an `obj.CTJSImport` object target
# that target_compile_options(CTJSImport ...) does not reach, so it would build
# at the compiler's default standard and the engine's C++23 headers (atom.hpp
# first) would not parse.
#
# ctcompile_target() gives it the project's flags and its C++23 floor, which is
# what every other library here is built with.
add_library(ctcompile-ctjs-import BytecodeImport.cpp)
ctcompile_target(ctcompile-ctjs-import)

# The generated headers it reads, as a build edge rather than an assumption;
# the dialect's own CMakeLists follows the same rule.
add_dependencies(ctcompile-ctjs-import MLIRCTJSOpsIncGen)

target_link_libraries(ctcompile-ctjs-import
  PUBLIC
    CTJSDialect
    ctbrowser::script
    MLIRIR
    MLIRControlFlowDialect)

add_library(ctcompile::ctjs-import ALIAS ctcompile-ctjs-import)

add_subdirectory(Bytecode)
