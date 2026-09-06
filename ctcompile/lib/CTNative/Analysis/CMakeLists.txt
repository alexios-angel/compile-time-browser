# Phase 54A's inference. A library and not a pass: the claims emitter, the unit
# test and every phase from 55 onward are all callers, and a pass would make
# each of them reach through a pipeline to ask a question.
#
# IT LINKS CTJSDialect, WHICH THE IR LIBRARY DELIBERATELY DOES NOT. lib/CTNative/IR
# refuses that edge on the grounds that "a type lattice is a statement about
# types" and must not depend on the engine's ABI. This library is the opposite
# kind of thing: it reads ctjs operations and answers in ctnative types, so
# naming both dialects is its entire job.
add_mlir_library(CTNativeAnalysis
  TypeInference.cpp
  ClosedCallable.cpp
  ../HostContract/Contract.cpp
  ../HostContract/Values.cpp
  ../HostContract/Slots.cpp
  ../HostContract/Analysis.cpp
  ../HostContract/PrefixAnalysis.cpp
  ../HostContract/PrefixValues.cpp
  ../HostContract/PrefixIdentity.cpp
  ../HostContract/PrefixFactories.cpp
  BindingTime/Analysis.cpp
  BindingTime/Effects.cpp
  BindingTime/EffectQuery.cpp
  BindingTime/Captures.cpp
  BindingTime/Operations.cpp
  BindingTime/Flow.cpp
  BindingTime/Memory.cpp
  Inference/Shapes.cpp
  EscapeAnalysis.cpp
  NativeMap.cpp
  NativeMap/Presence.cpp
  NativeMap/SnapshotCopies.cpp
  NativeObjectIdentity.cpp
  NativeObject/ValueFlow.cpp
  NativeObject/InertSlots.cpp
  NativeObject/Fields.cpp

  LINK_LIBS PUBLIC
    CTNativeDialect
    CTJSDialect
    MLIRAnalysis
    MLIRArithDialect
    MLIRIR
    MLIRSCFDialect
)

target_include_directories(CTNativeAnalysis
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>)
target_include_directories(CTNativeAnalysis SYSTEM
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>)
