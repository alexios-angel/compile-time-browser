# Cross-compile ctbrowser for Windows x86_64 with the std::embed
# llvm-mingw toolchain (alexios-angel/llvm-mingw, release
# *-stdembed-ucrt-*) and libsdl's official SDL3-devel mingw package.
#
#   cmake --preset windows          # or windows-fetch (compile-time HTTP)
#   cmake --build --preset windows
#   cmake --build --preset windows --target windows-dist   # -> examples-windows/
#
# Locations (first hit wins; set the env var when yours differ):
#   toolchain  $LLVM_MINGW, ~/projects/llvm-mingw/install/llvm-mingw-native,
#              <repo>/tools/llvm-mingw
#   SDL3       $SDL3_MINGW (the package root holding x86_64-w64-mingw32/),
#              ~/projects/sdl3-mingw
#   GLM        $GLM_INC, ~/projects/glm-inc - an ISOLATED directory holding
#              glm/ only. Never a general /usr/include: that would drag the
#              host's glibc headers into the mingw compile.
#   Boost      $BOOST_INC, ~/projects/boost-inc - the same idea, holding
#              boost/ only. v2 uses Boost HEADER-ONLY (containers, unordered,
#              asio), so headers are the whole dependency; a symlink to the
#              host's boost/ inside an otherwise empty directory is enough.

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

# --- the std::embed mingw clang
set(_ctb_mingw_roots "$ENV{LLVM_MINGW}"
    "$ENV{HOME}/projects/llvm-mingw/install/llvm-mingw-native"
    "${CMAKE_CURRENT_LIST_DIR}/../tools/llvm-mingw")
foreach(_root IN LISTS _ctb_mingw_roots)
  if(_root AND EXISTS "${_root}/bin/x86_64-w64-mingw32-clang++")
    set(CMAKE_C_COMPILER "${_root}/bin/x86_64-w64-mingw32-clang")
    set(CMAKE_CXX_COMPILER "${_root}/bin/x86_64-w64-mingw32-clang++")
    set(CMAKE_RC_COMPILER "${_root}/bin/x86_64-w64-mingw32-windres")
    break()
  endif()
endforeach()
if(NOT CMAKE_CXX_COMPILER)
  message(FATAL_ERROR
    "x86_64-w64-mingw32-clang++ not found. Install the std::embed llvm-mingw "
    "toolchain (github.com/alexios-angel/llvm-mingw releases, the "
    "linux-x86_64 archive) and point LLVM_MINGW at its root, or unpack it "
    "as tools/llvm-mingw/.")
endif()

# --- SDL3. The TOOLCHAIN'S OWN SYSROOT FIRST: llvm-mingw's build-sdl3.sh
# installs a STATIC SDL3 (and SDL3_ttf) there, and a static one is what makes
# an application a single .exe rather than a folder with DLLs in it. The
# sysroot is already on the compiler's search path, so nothing more is needed
# when it is present.
#
# libsdl's official mingw devel package is the FALLBACK (brew SDL3 is ELF -
# useless for PE). It carries only an import library and SDL3.dll, so a build
# that lands here ships that DLL beside every exe.
get_filename_component(_ctb_toolchain_root "${CMAKE_CXX_COMPILER}" DIRECTORY)
get_filename_component(_ctb_toolchain_root "${_ctb_toolchain_root}" DIRECTORY)
# The sysroot has to be on CMAKE_FIND_ROOT_PATH explicitly, or find_package
# escapes to the HOST's packages - which for SDL3 means linuxbrew's ELF build,
# found and then unusable ("IMPORTED_IMPLIB not set").
list(APPEND CMAKE_FIND_ROOT_PATH "${_ctb_toolchain_root}/x86_64-w64-mingw32")
if(NOT EXISTS "${_ctb_toolchain_root}/x86_64-w64-mingw32/lib/cmake/SDL3")
  set(_ctb_sdl3_roots "$ENV{SDL3_MINGW}" "$ENV{HOME}/projects/sdl3-mingw")
  foreach(_root IN LISTS _ctb_sdl3_roots)
    if(_root AND EXISTS "${_root}/x86_64-w64-mingw32/include/SDL3/SDL.h")
      list(APPEND CMAKE_FIND_ROOT_PATH "${_root}/x86_64-w64-mingw32")
      break()
    endif()
  endforeach()
endif()

# --- GLM (header-only): the isolated include dir
set(_ctb_glm_roots "$ENV{GLM_INC}" "$ENV{HOME}/projects/glm-inc")
foreach(_root IN LISTS _ctb_glm_roots)
  if(_root AND EXISTS "${_root}/glm/glm.hpp")
    set(CTBROWSER_GLM_INCLUDE_DIR "${_root}" CACHE PATH
        "isolated GLM include dir for the mingw cross build")
    break()
  endif()
endforeach()

# --- Boost (header-only): the isolated include dir. There is no BoostConfig
# for the cross target, and there does not need to be - v2 links Boost::headers
# and nothing else, so src/CMakeLists.txt builds that target from this path.
set(_ctb_boost_roots "$ENV{BOOST_INC}" "$ENV{HOME}/projects/boost-inc")
foreach(_root IN LISTS _ctb_boost_roots)
  if(_root AND EXISTS "${_root}/boost/version.hpp")
    set(CTBROWSER_BOOST_INCLUDE_DIR "${_root}" CACHE PATH
        "isolated Boost include dir for the mingw cross build")
    break()
  endif()
endforeach()

# --- static everything. With a static SDL3 in the sysroot the produced exes
# depend on nothing but the system DLLs: no SDL3.dll, no libc++.dll, no
# libunwind.dll. Falling back to the shared devel package still works - its
# CMake config links the import library by full path, immune to -static's -l
# search behaviour - and then SDL3.dll has to ship alongside.
# -static also rides CXX flags so the PCH and every TU agree on the
# __STATIC__ predefines (a mismatch is a hard PCH error).
set(CMAKE_CXX_FLAGS_INIT "-static")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static")

# find_*: programs from the host, everything else from the cross roots
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
