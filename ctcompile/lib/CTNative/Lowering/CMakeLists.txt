# The native tier's lowering: proved CTJS -> EmitC over plain C++ types. Links
# the analyses it consults and the dialects it emits into. Partial evaluation
# also reuses compiler-side primitive helpers from ctbrowser; the generated
# C++ uses only the public platform libraries it needs, never Script.
add_library(ctcompile-ctnative-lowering
  ObjectValues/Types.cpp
  ObjectValues/Fields.cpp
  StringValues/Types.cpp
  StringValues/Operations.cpp
  ../Reachability/Analysis.cpp
  ../Reachability/Pass.cpp
  ../Supercompilation/Control.cpp
  ../Supercompilation/Driver.cpp
  ../Supercompilation/Pass.cpp
  ../Specialization/Candidates.cpp
  ../Specialization/Pass.cpp
  ../Symbolic/Facts.cpp
  ../Symbolic/Rewrite.cpp
  ../Symbolic/Pass.cpp
  ../Deforestation/Strategies.cpp
  ../Deforestation/Pass.cpp
  ../PartialEvaluation/Pass.cpp
  ../PartialEvaluation/ClosureProof.cpp
  ../PartialEvaluation/Closures.cpp
  ../PartialEvaluation/Prefix.cpp
  ../BindingTime/Pass.cpp
  ../HostContract/Pass.cpp
  ../HostContract/ClassInitialization.cpp
  ../HostContract/DOMPreparation.cpp
  ../HostContract/PrefixPass.cpp
  ../PartialEvaluation/Evaluator.cpp
  ../PartialEvaluation/Calls.cpp
  ../PartialEvaluation/CallProof.cpp
  ../PartialEvaluation/Primitives.cpp
  ../PartialEvaluation/Residualize.cpp
  LowerToEmitC.cpp
  Exceptions/Admission.cpp
  Exceptions/DOM.cpp
  Exceptions/Recovery/Inspect.cpp
  Exceptions/Recovery/Bindings.cpp
  Exceptions/Recovery/Effects.cpp
  Exceptions/Recovery/Tails.cpp
  Exceptions/Recovery/Structure.cpp
  Exceptions/Recovery/Transaction.cpp
  Exceptions/Emit.cpp
  LoweringSupport.cpp
  ClosureLifting/Calls.cpp
  ClosureLifting/Callbacks.cpp
  ClosureLifting/Cells.cpp
  ClosureLifting/Bindings.cpp
  ClosureLifting/Captures.cpp
  ClosureLifting/Methods.cpp
  ClosureLifting/Diagnostics.cpp
  ClosureLifting/Constructors.cpp
  ClosureLifting/Rewrite.cpp
  ClosureLifting/ReturnedClosures.cpp
  ClosureLifting/MethodTables.cpp
  Admission/Values.cpp
  Admission/IdentityFields.cpp
  Admission/Objects.cpp
  Admission/Operations.cpp
  Admission/Functions.cpp
  EmitC/Shapes.cpp
  EmitC/OwnedGlobals.cpp
  EmitC/Module.cpp
  EmitC/DOM.cpp
  EmitC/Expressions.cpp
  EmitC/Calls.cpp
  EmitC/ScalarConversions.cpp
  EmitC/Types.cpp
  EmitC/Operations.cpp
  EmitC/Maps.cpp
  EmitC/Functions.cpp
  EmitC/Environments.cpp
  EmitC/MethodTables.cpp
  PrintDeduced.cpp
  PruneDeadStores.cpp)
ctcompile_target(ctcompile-ctnative-lowering)
add_dependencies(ctcompile-ctnative-lowering CTNativeTransformsPassIncGen)

target_link_libraries(ctcompile-ctnative-lowering
  PRIVATE
    # Compiler-side primitive semantics only. Generated native C++ links no VM.
    ctbrowser::script
  PUBLIC
    ctcompile::ctjs-lowering
    CTNativeAnalysis
    CTNativeDialect
    CTJSDialect
    MLIRIR
    MLIRPass
    MLIRControlFlowToSCF
    MLIRUBDialect
    MLIRAnalysis
    MLIREmitCDialect
    MLIRSCFDialect
    MLIRControlFlowDialect
    # PatternApplicator and FrozenRewritePatternSet, which the symbolic
    # rewrite and the unary-plus pattern set use directly.
    MLIRRewrite
    MLIRTransforms)

add_library(ctcompile::ctnative-lowering ALIAS ctcompile-ctnative-lowering)
