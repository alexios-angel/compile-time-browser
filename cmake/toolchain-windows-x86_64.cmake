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

# --- SDL3: libsdl's official mingw devel package (brew SDL3 is ELF - useless
# for PE). Its x86_64-w64-mingw32/ prefix carries headers, the import
# library, the CMake config and bin/SDL3.dll.
set(_ctb_sdl3_roots "$ENV{SDL3_MINGW}" "$ENV{HOME}/projects/sdl3-mingw")
foreach(_root IN LISTS _ctb_sdl3_roots)
  if(_root AND EXISTS "${_root}/x86_64-w64-mingw32/include/SDL3/SDL.h")
    list(APPEND CMAKE_FIND_ROOT_PATH "${_root}/x86_64-w64-mingw32")
    break()
  endif()
endforeach()

# --- GLM (header-only): the isolated include dir
set(_ctb_glm_roots "$ENV{GLM_INC}" "$ENV{HOME}/projects/glm-inc")
foreach(_root IN LISTS _ctb_glm_roots)
  if(_root AND EXISTS "${_root}/glm/glm.hpp")
    set(CTBROWSER_GLM_INCLUDE_DIR "${_root}" CACHE PATH
        "isolated GLM include dir for the mingw cross build")
    break()
  endif()
endforeach()

# --- static everything except SDL3: the CMake config links the import
# library by full path (immune to -static's -l search behavior), so the
# produced exes depend only on SDL3.dll + system DLLs - no libc++.dll,
# no libunwind.dll. -static also rides CXX flags so the PCH and every
# TU agree on the __STATIC__ predefines (a mismatch is a hard PCH error).
set(CMAKE_CXX_FLAGS_INIT "-static")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static")

# find_*: programs from the host, everything else from the cross roots
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
