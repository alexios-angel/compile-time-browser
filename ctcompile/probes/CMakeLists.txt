# Optional single-compiler build of the same source that the existing lit
# deduction probe checks independently with GCC, Clang and the host compiler.
add_executable(ctcompile-deduction-probe EXCLUDE_FROM_ALL deduction.cpp)
target_compile_features(ctcompile-deduction-probe PRIVATE cxx_std_20)
target_compile_options(ctcompile-deduction-probe PRIVATE ${CTCOMPILE_WARNING_OPTIONS})
set_target_properties(ctcompile-deduction-probe PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED YES
  CXX_EXTENSIONS NO)
