# The ctjs dialect library. The DEPENDS edges are required: a library that
# includes generated headers without depending on their generator builds
# serially but fails on a cold parallel build, in a file nobody edited.
#
# MLIRCTJSOpsIncGen carries the MLIR prefix because add_mlir_dialect creates it;
# the three hand-written targets do not.
add_mlir_library(CTJSDialect
  CTJSABIShape.cpp
  CTJSAttrs.cpp
  CTJSInterfaces.cpp
  CTJSDialect.cpp
  CTJSOps.cpp
  CTJSTypes.cpp
  Exceptions.cpp

  DEPENDS
    MLIRCTJSOpsIncGen
    CTJSEnumsIncGen
    CTJSAttrsIncGen
    CTJSInterfacesIncGen

  LINK_LIBS PUBLIC
    # The engine's AOT headers, because CTJS_RuntimeOp names a
    # ctbrowser::aot::helper_id enumerator, so an operation cannot claim a
    # helper the runtime does not declare. A narrow link: ctbrowser::script
    # brings the ABI table and the bytecode it describes, not a windowing
    # system.
    ctbrowser::script
    MLIRIR
    MLIRSideEffectInterfaces
    MLIRControlFlowDialect
    MLIRFuncDialect
    MLIRArithDialect
)

# The two include roots (source and generated) go on the target rather than the
# directory, so the drivers that link this library inherit them instead of each
# repeating the pair. add_mlir_library knows nothing about ctcompile_target's
# conventions, so they are set here.
#
# Both roots use the same spelling: a generated header is included exactly like
# a hand-written one (`ctcompile/CTJS/IR/...`), because mlir_tablegen mirrors
# the source tree under the binary directory. That keeps every generated
# include off relative paths.
ctcompile_includes(CTJSDialect)
