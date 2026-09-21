target_sources(${CTNATIVE_ANALYSIS_SOURCE_TARGET} PRIVATE
  Contract.cpp
  DOMEntry.cpp
  DOMSource.cpp
  DOMIteration.cpp
  DOMCustomIteration.cpp
  DOMSnapshotLengths.cpp
  Values.cpp
  CapturedMapBody.cpp
  ScalarCallbacks.cpp
  Slots.cpp
  Analysis.cpp
  PrefixAnalysis.cpp
  PrefixValues.cpp
  PrefixIdentity.cpp
  PrefixFactories.cpp
  PrefixReads.cpp
  PrefixMutations.cpp
  ProviderState.cpp
  ProviderObjects.cpp
  ProviderRecords.cpp
  ProviderDiagnostics.cpp
  ProviderCallbacks.cpp
  Analysis.h
  Prefix.h
  ProviderCallbacks.h
  ProviderDiagnostics.h
  ProviderObjects.h
  ProviderPaths.h
  ProviderState.h
)

target_sources(ctcompile-ctnative-lowering PRIVATE
  Pass.cpp
  ClassInitialization.cpp
  DOMPreparation.cpp
  PrefixPass.cpp
  Preparation.h
)

add_subdirectory(CapturedMapBody)
add_subdirectory(ClassInitialization)
add_subdirectory(DOMEntry)
add_subdirectory(DOMSource)
add_subdirectory(Values)
