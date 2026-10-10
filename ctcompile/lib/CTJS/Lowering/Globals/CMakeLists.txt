# Helpers for ResolveGlobals.cpp, compiled into the CTJS lowering library.
target_sources(ctcompile-ctjs-lowering PRIVATE
  RegisterFlow.cpp
  Effects.cpp
  Effects.h
  RegisterFlow.h
)
