# Helpers for ResolveGlobals.cpp, compiled into the CTJS lowering library.
target_sources(ctcompile-ctjs-lowering PRIVATE
  ClosureValues.cpp
  RegisterFlow.cpp
  Effects.cpp
  NativeRealm.cpp
  WholePage.cpp
  ClosureValues.h
  Effects.h
  RegisterFlow.h
)
