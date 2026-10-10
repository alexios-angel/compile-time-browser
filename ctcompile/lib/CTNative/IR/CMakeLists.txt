# The ctnative dialect library. Two generated headers, two DEPENDS edges.
#
# It deliberately does not link ctbrowser::script. CTJSDialect does, because
# CTJS_RuntimeOp names a ctbrowser::aot::helper_id enumerator, and that link
# stops an operation from claiming a helper the runtime does not declare.
# Nothing here names a runtime symbol: the type lattice is a statement about
# types, and if it needed the engine's ABI to say what `!ctnative.num<i32>` is,
# the ABI and the lattice could disagree.
#
# The one tie to the engine lives in a test instead: ctcompile-test-ctnative-lattice
# runs `"\u{1F600}".length` through the real interpreter and pins
# kDefaultStringEncoding against the answer.
add_mlir_library(CTNativeDialect
  CTNativeDialect.cpp
  CTNativeLattice.cpp
  CTNativeOps.cpp
  CTNativeTypes.cpp

  DEPENDS
    MLIRCTNativeOpsIncGen
    CTNativeEnumsIncGen

  LINK_LIBS PUBLIC
    MLIREmitCDialect
    MLIRIR
    MLIRSideEffectInterfaces
)

# Both include roots are set on the target, so drivers that link this library
# inherit them. Spelled the same way on both sides - mlir_tablegen mirrors the
# source tree under the binary directory - so a generated header is included
# exactly as a hand-written one is. See lib/CTJS/IR/CMakeLists.txt.
target_include_directories(CTNativeDialect
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>)
target_include_directories(CTNativeDialect SYSTEM
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>)
