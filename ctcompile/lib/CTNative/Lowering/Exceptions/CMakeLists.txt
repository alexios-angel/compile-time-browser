target_sources(ctcompile-ctnative-lowering PRIVATE
  Admission.cpp
  DOM.cpp
  Emit.cpp
  Recovery.h
  SuppressedCloses.cpp
  SuppressedCloses.h
  TypeErrors.cpp
  TypeErrors.h
)

add_subdirectory(Recovery)
