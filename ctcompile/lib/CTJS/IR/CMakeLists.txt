# THE DEPENDENCY EDGES ARE THE POINT, and the policy says why: a library that
# consumes generated headers without depending on the generator "builds
# correctly right up until someone runs a parallel build on a cold tree, and
# then fails in a file nobody edited".
#
# MLIRCTJSOpsIncGen carries the MLIR prefix because add_mlir_dialect creates it;
# the three hand-written targets do not.
add_mlir_library(CTJSDialect
  CTJSAttrs.cpp
  CTJSDialect.cpp
  CTJSOps.cpp
  CTJSTypes.cpp

  DEPENDS
    MLIRCTJSOpsIncGen
    CTJSEnumsIncGen
    CTJSAttrsIncGen
    CTJSInterfacesIncGen

  LINK_LIBS PUBLIC
    MLIRIR
    MLIRSideEffectInterfaces
    MLIRControlFlowDialect
    MLIRFuncDialect
    MLIRArithDialect
)

# THE TWO INCLUDE ROOTS, ON THE TARGET RATHER THAN THE DIRECTORY, so that the
# drivers that link this library inherit them instead of each repeating the
# pair. add_mlir_library builds an MLIR-style target and knows nothing about
# ctcompile_target's conventions, so this is where they are said.
#
# BOTH ROOTS ARE SPELLED THE SAME WAY - a generated header is included exactly
# as a hand-written one is, `ctcompile/CTJS/IR/...`, because mlir_tablegen
# mirrors the source tree under the binary directory. The policy's rule is "do
# not #include a generated header by relative path from another project", and
# having the two roots agree is what makes that easy rather than a discipline.
target_include_directories(CTJSDialect
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>)
