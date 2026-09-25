if(TARGET ctcompile-test-program_image)
  target_sources(ctcompile-test-program_image PRIVATE
    ProgramImage.cpp
  )
endif()

if(TARGET ctcompile-test-manifest)
  target_sources(ctcompile-test-manifest PRIVATE
    Manifest.cpp
  )
endif()

if(TARGET ctcompile-test-app_bundle)
  target_sources(ctcompile-test-app_bundle PRIVATE
    AppBundle.cpp
  )
endif()

# The preview's supported host is Linux; this also exercises /proc/self/exe
# launcher discovery after moving the installed prefix to a different path.
if(Python3_Interpreter_FOUND AND CMAKE_SYSTEM_NAME STREQUAL "Linux"
   AND NOT CMAKE_CROSSCOMPILING)
  add_test(NAME ctcompile_install
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/install.py"
      --cmake "${CMAKE_COMMAND}" --build "${CMAKE_BINARY_DIR}"
      --source "${CTCOMPILE_MONOREPO_ROOT}" --config "$<CONFIG>"
      --bindir "${CMAKE_INSTALL_BINDIR}" --docdir "${CTCOMPILE_INSTALL_DOCDIR}"
      --version "${PROJECT_VERSION}" --readelf "${CMAKE_READELF}"
      --cxx "${CMAKE_CXX_COMPILER}" --nm "${CMAKE_NM}"
      --angle-dir "$<$<BOOL:${CTBROWSER_WITH_ANGLE}>:${CTBROWSER_ANGLE_DIR}>"
      --mlir "$<BOOL:${CTCOMPILE_ENABLE_MLIR}>"
      --launcher "$<TARGET_EXISTS:ctbrowser-tool-ctrun>")
  set_tests_properties(ctcompile_install PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE)
endif()
