# The ctnative dialect library. Three generated headers, three DEPENDS edges.
#
# IT DOES NOT LINK ctbrowser::script, AND THE ABSENCE IS THE POINT. CTJSDialect
# does, because CTJS_RuntimeOp names a ctbrowser::aot::helper_id enumerator and
# that tie is what stops an operation claiming a helper the runtime does not
# declare. Nothing here names a runtime symbol: a type lattice is a statement
# about types, and the moment it needed the engine's ABI to say what
# `!ctnative.num<i32>` is, the ABI and the lattice could disagree.
#
# The tie to the engine that this phase DOES have is in the test, where it
# belongs: ctcompile-test-ctnative-lattice runs `"\u{1F600}".length` through the
# real interpreter and pins kDefaultStringEncoding against the answer.
add_mlir_library(CTNativeDialect
  CTNativeDialect.cpp
  CTNativeInterfaces.cpp
  CTNativeLattice.cpp
  CTNativeTypes.cpp

  DEPENDS
    MLIRCTNativeOpsIncGen
    CTNativeEnumsIncGen
    CTNativeInterfacesIncGen

  LINK_LIBS PUBLIC
    MLIRIR
)

# THE TWO INCLUDE ROOTS, ON THE TARGET, so drivers that link this library
# inherit them. Spelled the same way on both sides - mlir_tablegen mirrors the
# source tree under the binary directory - so a generated header is included
# exactly as a hand-written one is. See lib/CTJS/IR/CMakeLists.txt.
target_include_directories(CTNativeDialect
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>)
target_include_directories(CTNativeDialect SYSTEM
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>)
