# The analysis that decides which emitted C++ bindings may be declared const.
target_sources(ctcompile-target-cpp PRIVATE
  Bindings.cpp
  Types.cpp
  Uses.cpp
  Bindings.h
)
