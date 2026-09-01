# The forked EmitC C++ emitter. See TranslateToCpp.cpp for what it is and why.
#
# NOT `ctcompile_target()`, and that is the point of this whole file. The
# project's warning set is `-Wall -Wextra -Werror -Wconversion`, and vendored
# upstream code does not build clean under it - nor should it be made to, since
# every warning fixed here is a hunk that has to be re-applied at the next LLVM
# bump. `llvm_update_compile_flags` gives it the flags LLVM builds ITSELF with,
# which is the set its authors actually test against.
add_library(ctcompile-target-cpp
  TranslateToCpp.cpp
  TranslateRegistration.cpp)
llvm_update_compile_flags(ctcompile-target-cpp)

target_link_libraries(ctcompile-target-cpp
  PUBLIC
    MLIRIR
    MLIRSupport
    MLIREmitCDialect
    MLIRControlFlowDialect
    MLIRFuncDialect
    MLIRTranslateLib)

target_include_directories(ctcompile-target-cpp
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>)

add_library(ctcompile::target-cpp ALIAS ctcompile-target-cpp)
