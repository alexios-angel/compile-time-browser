# Extracts the one inline <script> of a page, as the browser will assemble it.
#
# The launcher test compiles a page's JavaScript ahead of time, and a compiled
# body bakes in the function index of every closure it builds - so the text
# compiled here must be the text `browser::run_scripts` compiles, byte for
# byte, or the two programs number their functions differently and the
# generated bodies belong to a different program.
#
# What run_scripts assembles, from ctbrowser/lib/Shell/browser/scripts.cpp: for
# a `<script>` with no `src`, the element's child text nodes concatenated, then
# one newline. That trailing newline is in the string the browser hashes and
# compiles, and a copy without it is a different program.
#
# This is a second implementation of that rule, and it is only safe because it
# is checked. `ctcompile::aot::install` refuses when a symbol matches no
# function, so a text that differs enough to renumber anything is a startup
# refusal naming the symbol rather than an application running the wrong
# bodies. A page with a `src` attribute, a second `<script>`, or a module is
# refused here, because those are the cases where the rule is more than "the
# text between the tags".
#
# Required: -DPAGE= -DOUTPUT=
cmake_minimum_required(VERSION 3.20)

foreach(required PAGE OUTPUT)
  if(NOT DEFINED ${required})
    message(FATAL_ERROR "extract-inline-script.cmake: -D${required}= is required")
  endif()
endforeach()

file(READ "${PAGE}" html)

string(REGEX MATCHALL "<script[^>]*>" opened "${html}")
list(LENGTH opened how_many)
if(NOT how_many EQUAL 1)
  message(FATAL_ERROR
    "extract-inline-script.cmake: ${PAGE} has ${how_many} <script> elements. This reproduces "
    "run_scripts' assembly rule for the single-inline-script case only; anything else has to be "
    "asked of the engine rather than guessed at here.")
endif()
list(GET opened 0 tag)
if(tag MATCHES "src=" OR tag MATCHES "type=")
  message(FATAL_ERROR
    "extract-inline-script.cmake: ${PAGE}'s script is `${tag}` - a src'd or module script is "
    "assembled differently and is not what this extracts.")
endif()

string(FIND "${html}" "${tag}" opens)
string(LENGTH "${tag}" tag_length)
math(EXPR begins "${opens} + ${tag_length}")
string(FIND "${html}" "</script>" closes)
if(closes LESS begins)
  message(FATAL_ERROR "extract-inline-script.cmake: ${PAGE} has no </script>")
endif()
math(EXPR length "${closes} - ${begins}")
string(SUBSTRING "${html}" ${begins} ${length} body)

# The trailing newline is the one run_scripts appends (see above).
file(WRITE "${OUTPUT}" "${body}\n")
message(STATUS "extract-inline-script.cmake: ${length} bytes of script from ${PAGE}")
