# Manifest.hpp and Version.hpp are installed public headers; the rest are
# private to the support libraries.
target_sources(ctcompile-support PUBLIC
  FILE_SET HEADERS
    BASE_DIRS "${PROJECT_SOURCE_DIR}/include"
    FILES
      Manifest.hpp
      Version.hpp
)

target_sources(ctcompile-support PRIVATE
  CppLiterals.hpp
  Difference.hpp
)

if(CTCOMPILE_ENABLE_MLIR)
  target_sources(ctcompile-mlir-support PRIVATE MLIRContextSetup.hpp)
endif()
