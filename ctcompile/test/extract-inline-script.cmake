# THE ONE INLINE <script> OF A PAGE, AS THE BROWSER WILL ASSEMBLE IT.
#
# The launcher test compiles a page's JavaScript ahead of time, and a compiled
# body bakes the function INDEX of every closure it builds - so the text
# compiled here must be the text `browser::run_scripts` compiles, byte for byte,
# or the two programs number their functions differently and the generated
# bodies belong to a different program.
#
# WHAT run_scripts ASSEMBLES, from ctbrowser/lib/Shell/browser.cpp: for a
# `<script>` with no `src`, the element's child text nodes concatenated, then
# ONE newline. That trailing newline is not decoration - it is in the string the
# browser hashes and compiles, and a copy without it is a different program.
#
# THIS IS A SECOND IMPLEMENTATION OF THAT RULE AND IT IS ONLY SAFE BECAUSE IT IS
# CHECKED. `ctcompile::aot::install` refuses when a symbol matches no function,
# so a text that differs enough to renumber anything is a startup refusal naming
# the symbol rather than an application running the wrong bodies. A page with a
# `src` attribute, a second `<script>`, or a module is refused HERE, because
# those are the cases where the rule is genuinely more than "the text between
# the tags".
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

# THE TRAILING NEWLINE IS run_scripts', not this file's taste.
file(WRITE "${OUTPUT}" "${body}\n")
message(STATUS "extract-inline-script.cmake: ${length} bytes of script from ${PAGE}")
