# Fails when a native page test's clang target has a compile option the GCC
# build of the same test does not copy (NativeWorkloads.cmake,
# ctcompile_page_workload). That build takes only the target's include
# directories and definitions and sets its own warnings, so the two builds
# stay the same program only while every other option is a warning, -pedantic
# or the -O2 both use.
#
#   cmake -DTARGET=<target> -DOPTIONS=<options joined by |> -DSTAMP=<file> -P check-gcc-options.cmake

string(REPLACE "|" ";" options "${OPTIONS}")
foreach(option IN LISTS options)
  # A warning flag has no comma: -Wa, -Wl, and -Wp, pass options on.
  if(NOT option MATCHES "^(-W[A-Za-z0-9=+-]+|-pedantic|-O2)$")
    message(FATAL_ERROR
      "${TARGET} has the compile option `${option}`, which its GCC build "
      "(${TARGET}-gcc) does not copy: copy it in ctcompile_page_workload, or "
      "allow it here if it cannot change the program")
  endif()
endforeach()
file(TOUCH "${STAMP}")
