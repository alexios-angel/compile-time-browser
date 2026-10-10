# Printing of native callables (closures): the lambda bodies, their binder
# functions, and the decisions about which of the two to emit.
target_sources(ctcompile-target-cpp PRIVATE
  Body.cpp
  Creation.cpp
  References.cpp
  Body.h
  Emit.inc
)
