if(TARGET ctcompile-test-launcher-vm)
  target_sources(ctcompile-test-launcher-vm PRIVATE
    Application.cpp
  )
endif()

if(TARGET ctcompile-test-launcher-aot)
  target_sources(ctcompile-test-launcher-aot PRIVATE
    Application.cpp
  )
endif()

if(TARGET ctcompile-test-launcher-page-vm)
  target_sources(ctcompile-test-launcher-page-vm PRIVATE
    Page.cpp
  )
endif()

if(TARGET ctcompile-test-launcher-page-aot)
  target_sources(ctcompile-test-launcher-page-aot PRIVATE
    Page.cpp
  )
endif()
