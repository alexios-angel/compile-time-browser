target_sources(ctcompile-ctnative-lowering PRIVATE
  Admission.cpp
  DOM.cpp
  Emit.cpp
  Recovery.h
  TypeErrors.cpp
  TypeErrors.h
)

add_subdirectory(Recovery)
