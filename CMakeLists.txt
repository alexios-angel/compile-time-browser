cmake_minimum_required(VERSION 3.16...4.3)

project(ctbrowser
  HOMEPAGE_URL "https://github.com/alexios-angel/compile-time-browser"
  VERSION 0.1.0
  LANGUAGES CXX)
set(PROJECT_DESCRIPTION "Compile-time browser: HTML, CSS and JS parsed at compile time, rendered with SDL3.")

include(CMakePackageConfigHelpers)
include(CMakeDependentOption)
include(GNUInstallDirs)
include(CTest)

cmake_dependent_option(CTBROWSER_BUILD_TESTS "Build ctbrowser tests" ON
  "BUILD_TESTING;CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR" OFF)
cmake_dependent_option(CTBROWSER_BUILD_EXAMPLES "Build ctbrowser examples (need SDL3)" ON
  "CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR" OFF)

# the ctlark grammar layers run Earley parses in the constant
# evaluator; the interface carries the raised limits so consumers do
# not have to know the flags
option(CTBROWSER_CONSTEXPR_LIMITS "Raise constexpr evaluation limits on the interface" ON)
option(CTBROWSER_PCH "Precompile ctbrowser.hpp for tests and examples" ON)

set(CTBROWSER_CONSTEXPR_OPTIONS
  $<$<CXX_COMPILER_ID:Clang,AppleClang>:-fconstexpr-steps=500000000;-fconstexpr-depth=1024;-fbracket-depth=2048>
  $<$<CXX_COMPILER_ID:GNU>:-fconstexpr-ops-limit=3000000000;-fconstexpr-loop-limit=10000000;-fconstexpr-depth=1024>
  $<$<CXX_COMPILER_ID:MSVC>:/constexpr:steps500000000;/constexpr:depth1024>)

# The three bricks are git submodules (git submodule update --init
# --recursive); each pins the same compile-time-lark, and the browser
# resolves ctlark/ctll through compile-time-html's copy so exactly one
# is in play. The extra .../ctlark and .../ctll entries let the
# headers' relative quoted includes resolve.
set(CTBROWSER_LARK "${PROJECT_SOURCE_DIR}/external/compile-time-html/external/compile-time-lark")
set(CTBROWSER_SUBMODULE_INCLUDE_DIRS
  "${PROJECT_SOURCE_DIR}/external/compile-time-html/include"
  "${PROJECT_SOURCE_DIR}/external/compile-time-javascript/include"
  "${PROJECT_SOURCE_DIR}/external/compile-time-css/include"
  "${CTBROWSER_LARK}/include"
  "${CTBROWSER_LARK}/include/ctlark"
  "${CTBROWSER_LARK}/include/ctll")
if(NOT EXISTS "${CTBROWSER_LARK}/include/ctlark.hpp")
  message(FATAL_ERROR
    "ctbrowser: submodules are missing - run `git submodule update --init --recursive`")
endif()

add_library(${PROJECT_NAME} INTERFACE)

target_include_directories(${PROJECT_NAME} INTERFACE
  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
  "$<BUILD_INTERFACE:${CTBROWSER_SUBMODULE_INCLUDE_DIRS}>"
  $<INSTALL_INTERFACE:include>)

target_compile_features(${PROJECT_NAME} INTERFACE cxx_std_20)
if(CTBROWSER_CONSTEXPR_LIMITS)
  target_compile_options(${PROJECT_NAME} INTERFACE ${CTBROWSER_CONSTEXPR_OPTIONS})
endif()
set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_SCAN_FOR_MODULES 0)

install(TARGETS ${PROJECT_NAME} EXPORT ${PROJECT_NAME}-targets)
install(DIRECTORY include/ DESTINATION include FILES_MATCHING PATTERN *.hpp)
# the installed tree stays flat - include/{ctbrowser,cthtml,ctjs,ctcss,
# ctlark,ctll} side by side - so relative includes keep working
install(DIRECTORY external/compile-time-html/include/ DESTINATION include
    FILES_MATCHING PATTERN *.hpp)
install(DIRECTORY external/compile-time-javascript/include/ DESTINATION include
    FILES_MATCHING PATTERN *.hpp)
install(DIRECTORY external/compile-time-css/include/ DESTINATION include
    FILES_MATCHING PATTERN *.hpp)
install(DIRECTORY "${CTBROWSER_LARK}/include/" DESTINATION include
    FILES_MATCHING PATTERN *.hpp)

add_library(${PROJECT_NAME}::${PROJECT_NAME} ALIAS ${PROJECT_NAME})

if (NOT EXISTS "${PROJECT_BINARY_DIR}/${PROJECT_NAME}-config.cmake.in")
  file(WRITE ${PROJECT_BINARY_DIR}/${PROJECT_NAME}-config.cmake.in [[
    @PACKAGE_INIT@
    include("${CMAKE_CURRENT_LIST_DIR}/@PROJECT_NAME@-targets.cmake")
  ]])
endif()

configure_package_config_file(
  "${PROJECT_BINARY_DIR}/${PROJECT_NAME}-config.cmake.in"
  "${PROJECT_BINARY_DIR}/${PROJECT_NAME}-config.cmake"
  INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}"
  NO_SET_AND_CHECK_MACRO
  NO_CHECK_REQUIRED_COMPONENTS_MACRO)

write_basic_package_version_file(ctbrowser-config-version.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion)

install(EXPORT ${PROJECT_NAME}-targets
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}"
  NAMESPACE ${PROJECT_NAME}::)
install(
  FILES
    "${PROJECT_BINARY_DIR}/${PROJECT_NAME}-config-version.cmake"
    "${PROJECT_BINARY_DIR}/${PROJECT_NAME}-config.cmake"
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME})

# One shared PCH: baking the three grammars' Earley tables (the JS one
# is tens of minutes) must happen ONCE - the anchor owns the PCH and
# every test/example REUSE_FROMs it.
if(CTBROWSER_PCH AND (CTBROWSER_BUILD_TESTS OR CTBROWSER_BUILD_EXAMPLES))
  file(WRITE "${CMAKE_BINARY_DIR}/ctbrowser-pch-anchor.cpp"
    "int ctbrowser_pch_anchor_symbol = 0;\n")
  add_library(ctbrowser-pch-anchor OBJECT "${CMAKE_BINARY_DIR}/ctbrowser-pch-anchor.cpp")
  target_link_libraries(ctbrowser-pch-anchor PRIVATE ctbrowser::ctbrowser)
  target_precompile_headers(ctbrowser-pch-anchor PRIVATE
    "${PROJECT_SOURCE_DIR}/include/ctbrowser.hpp")
endif()

# SDL3 and its satellite libraries: the satellites are optional and
# light up richer paths (SDL3_image sprites, SDL3_mixer sound,
# SDL3_ttf TrueType text); the built-in fallbacks serve without them
find_package(SDL3 CONFIG QUIET)
set(CTBROWSER_SAT_DEFS "")
set(CTBROWSER_SAT_LIBS "")
if(SDL3_FOUND)
  find_package(SDL3_image CONFIG QUIET)
  if(SDL3_image_FOUND)
    list(APPEND CTBROWSER_SAT_DEFS CTBROWSER_WITH_IMAGE)
    list(APPEND CTBROWSER_SAT_LIBS SDL3_image::SDL3_image)
  endif()
  find_package(SDL3_mixer CONFIG QUIET)
  if(SDL3_mixer_FOUND)
    list(APPEND CTBROWSER_SAT_DEFS CTBROWSER_WITH_MIXER)
    list(APPEND CTBROWSER_SAT_LIBS SDL3_mixer::SDL3_mixer)
  endif()
  find_package(SDL3_ttf CONFIG QUIET)
  if(SDL3_ttf_FOUND)
    list(APPEND CTBROWSER_SAT_DEFS CTBROWSER_WITH_TTF)
    list(APPEND CTBROWSER_SAT_LIBS SDL3_ttf::SDL3_ttf)
  endif()
  message(STATUS "ctbrowser: SDL3 found; satellites: ${CTBROWSER_SAT_DEFS}")
endif()

if(CTBROWSER_BUILD_TESTS)
  add_subdirectory(tests)
endif()

if(CTBROWSER_BUILD_EXAMPLES)
  if(SDL3_FOUND)
    add_subdirectory(examples)
  else()
    message(STATUS "ctbrowser: SDL3 not found - skipping examples (install SDL3 or set SDL3_DIR)")
  endif()
endif()

set(PKG_CONFIG_FILE_NAME "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.pc")
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/packaging/pkgconfig.pc.in" "${PKG_CONFIG_FILE_NAME}" @ONLY)
install(FILES "${PKG_CONFIG_FILE_NAME}"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig")
