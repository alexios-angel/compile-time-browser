# The CTNative-to-EmitC lowering, one file per concern. Emitter.h declares the
# shared `lowering` state they all extend.
target_sources(ctcompile-ctnative-lowering PRIVATE
  ArrayCallbacks.cpp
  Shapes.cpp
  OwnedGlobals.cpp
  Module.cpp
  DOM.cpp
  Expressions.cpp
  Calls.cpp
  ScalarConversions.cpp
  Types.cpp
  Vectors.cpp
  Vectors.h
  Operations.cpp
  Maps.cpp
  Functions.cpp
  SymbolTransport.cpp
  SymbolTransport.h
  Environments.cpp
  MethodTables.cpp
  Emitter.h
  Runtime.h
)
