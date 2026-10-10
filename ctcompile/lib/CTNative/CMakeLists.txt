# Create the dialect and the CTNativeAnalysis and ctcompile-ctnative-lowering
# libraries first; the directories below add sources to them.

add_subdirectory(IR)
add_subdirectory(Analysis)
add_subdirectory(Lowering)

add_subdirectory(HostContract)
add_subdirectory(BindingTime)
add_subdirectory(Deforestation)
add_subdirectory(PartialEvaluation)
add_subdirectory(Reachability)
add_subdirectory(Specialization)
add_subdirectory(Supercompilation)
add_subdirectory(Symbolic)
