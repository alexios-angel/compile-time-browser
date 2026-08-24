# ONE FIXTURE, READ BY BOTH TIERS.
#
# The differential test compiles a JavaScript file through the backend AND runs
# the same source through the interpreter. Transcribing it into a C++ string
# beside the file is what it did before, and the two drifted in ORDER - which
# matters far more than it sounds, because a compiled body bakes the function
# INDEX of every closure it builds and an index means nothing outside the
# program it was compiled in.
#
# A RAW STRING NEEDS NO ESCAPING, which is the whole reason this is three lines
# rather than an escaper: the content is JavaScript and cannot contain the
# delimiter below.
cmake_minimum_required(VERSION 3.20)
file(READ "${SOURCE}" content)
if(content MATCHES "\\)CTCJS\"")
  message(FATAL_ERROR "embed-js.cmake: ${SOURCE} contains the raw-string delimiter")
endif()
file(WRITE "${OUTPUT}" "R\"CTCJS(\n${content}\n)CTCJS\"\n")
