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
