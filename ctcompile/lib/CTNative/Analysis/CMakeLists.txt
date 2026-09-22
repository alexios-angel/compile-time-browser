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
  OwnedMethodTableSlots.cpp
  OwnedGlobalRoots.cpp
  OwnedGlobalMethods.cpp
  EscapeAnalysis.cpp
  NativeMap.cpp
  NativeObjectIdentity.cpp

  LINK_LIBS PUBLIC
    CTNativeDialect
    CTJSDialect
    MLIRAnalysis
    MLIRArithDialect
    MLIRControlFlowToSCF
    MLIRIR
    MLIRSCFDialect
)

target_include_directories(CTNativeAnalysis
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>)
target_include_directories(CTNativeAnalysis SYSTEM
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>)

# Provider diagnostics share public Core string semantics. MLIR compiles the
# sources through its object target, so that target also needs C++23.
target_compile_features(CTNativeAnalysis PRIVATE cxx_std_23)
if(TARGET obj.CTNativeAnalysis)
  target_compile_features(obj.CTNativeAnalysis PRIVATE cxx_std_23)
endif()

# MLIR compiles through an object target on supported generators. Reuse it for
# sources owned by Analysis children and the sibling HostContract directory.
set(CTNATIVE_ANALYSIS_SOURCE_TARGET CTNativeAnalysis)
if(TARGET obj.CTNativeAnalysis)
  set(CTNATIVE_ANALYSIS_SOURCE_TARGET obj.CTNativeAnalysis)
endif()
set(CTNATIVE_ANALYSIS_SOURCE_TARGET "${CTNATIVE_ANALYSIS_SOURCE_TARGET}" PARENT_SCOPE)

target_sources(${CTNATIVE_ANALYSIS_SOURCE_TARGET} PRIVATE
  ClosedValueFlow.h
  OwnedGlobalRoots.h
  OwnedMethodTableSlots.h
  PrimitiveAlternatives.h
  PrimitiveMapKey.h
)

add_subdirectory(BindingTime)
add_subdirectory(EscapeAnalysis)
add_subdirectory(Inference)
add_subdirectory(NativeMap)
add_subdirectory(NativeObject)
