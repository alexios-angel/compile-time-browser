# Embeds a JavaScript file as a C++ raw string, so a test that both compiles a
# fixture and runs it in the interpreter uses the exact same source. A
# hand-copied string can drift, and compiled closures bake in function indices
# that are only valid for their own program. A raw string needs no escaping;
# the only forbidden content is the delimiter, which is checked below.
#
# Required: -DSOURCE= -DOUTPUT=
cmake_minimum_required(VERSION 3.20)
file(READ "${SOURCE}" content)
if(content MATCHES "\\)CTCJS\"")
  message(FATAL_ERROR "embed-js.cmake: ${SOURCE} contains the raw-string delimiter")
endif()
file(WRITE "${OUTPUT}" "R\"CTCJS(\n${content}\n)CTCJS\"\n")
