# windowed examples: need SDL3 (found by the top-level list). They run
# under ctest with the dummy video driver for a bounded number of
# frames via CTBROWSER_TEST_FRAMES, so CI exercises them headless.
file(GLOB example-sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

# assets sit next to the built binaries so the shell's executable-
# relative path resolution finds them no matter the launch directory
file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/assets" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")

foreach(source ${example-sources})
  get_filename_component(example "${source}" NAME_WE)
  add_executable(ctbrowser-example-${example} "${source}")
  if(TARGET ctbrowser-${example}-inc)
    add_dependencies(ctbrowser-example-${example} ctbrowser-${example}-inc)
  endif()
  if(CTBROWSER_EXAMPLES_FETCH)
    if(example STREQUAL "fetchboard")
      target_compile_options(ctbrowser-example-${example} PRIVATE
        "--fetch-allow=https://example.com/"
        "--fetch-allow=https://raw.githubusercontent.com/**")
    elseif(example STREQUAL "babylon-model")
      target_compile_options(ctbrowser-example-${example} PRIVATE
        "--fetch-allow=https://assets.babylonjs.com/**")
    endif()
  endif()
  target_link_libraries(ctbrowser-example-${example} PRIVATE ctbrowser::ctbrowser SDL3::SDL3 ${CTBROWSER_SAT_LIBS})
  target_compile_options(ctbrowser-example-${example} PRIVATE ${CTBROWSER_WARNING_OPTIONS})
  target_compile_definitions(ctbrowser-example-${example} PRIVATE ${CTBROWSER_SAT_DEFS})
  if(TARGET ctbrowser-pch-anchor)
    target_precompile_headers(ctbrowser-example-${example} REUSE_FROM ctbrowser-pch-anchor)
  endif()
  if(NOT CMAKE_CROSSCOMPILING)
    add_test(NAME example-${example} COMMAND ctbrowser-example-${example}
      WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
    set_tests_properties(example-${example} PROPERTIES
      ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy;CTBROWSER_TEST_FRAMES=30;CTBROWSER_SCREENSHOT=${CMAKE_BINARY_DIR}/shot-${example}.png")
  endif()
  list(APPEND ctbrowser-example-targets ctbrowser-example-${example})
endforeach()

# cross builds for Windows: `windows-dist` collects the .exes plus
# SDL3.dll (the ONE runtime dependency - everything else is static)
# into examples-windows/, ready to run on the Windows host
if(CMAKE_SYSTEM_NAME STREQUAL "Windows" AND CMAKE_CROSSCOMPILING)
  find_file(CTBROWSER_SDL3_DLL SDL3.dll PATH_SUFFIXES bin)
  set(dist-dir "${PROJECT_SOURCE_DIR}/examples-windows")
  set(dist-commands COMMAND ${CMAKE_COMMAND} -E make_directory "${dist-dir}")
  foreach(target IN LISTS ctbrowser-example-targets)
    # short artifact names (counter.exe, pong.exe ...) - the established
    # convention of the shipped folder
    string(REPLACE "ctbrowser-example-" "" exe-name "${target}")
    list(APPEND dist-commands
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${target}>" "${dist-dir}/${exe-name}.exe")
  endforeach()
  if(CTBROWSER_SDL3_DLL)
    list(APPEND dist-commands
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CTBROWSER_SDL3_DLL}" "${dist-dir}")
  endif()
  add_custom_target(windows-dist ${dist-commands}
    DEPENDS ${ctbrowser-example-targets}
    COMMENT "collecting Windows exes + SDL3.dll into examples-windows/"
    VERBATIM)
endif()
