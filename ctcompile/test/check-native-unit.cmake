# THE COMPILATION-UNIT GATE - ctcompile Phase 62½-D. The test that DEFINES
# "native".
#
# Part 24 §1.2: a program compiles to native only when the output links neither
# the interpreter nor the collector. This script is that sentence as a test,
# run over ONE EmitC module and the JavaScript program it is the lowering of:
#
#   (a) the module goes through the FORKED emitter to C++
#       (ctjs-translate --mlir-to-cpp, in its default mode - see below)
#   (b) that C++ is compiled by the configured compiler STANDALONE: no include
#       path into ctbrowser/, no library on the link line, -Wall -Wextra -Werror
#       -pedantic, and -ffp-contract=off because JavaScript has no fused
#       multiply-add and the interpreter's separate opcodes never fuse one
#   (c) `nm -C` on the binary shows no `ctbrowser::script::` symbol (nor a
#       `ctbrowser::aot::` one - that is the boxed tier's helper ABI, which
#       links the interpreter). AND THE CHECK IS PROVED LOAD-BEARING FIRST: the
#       same nm invocation is run against a binary that does link the
#       interpreter and must find such a symbol there, or a wrong nm spelling,
#       a stripped binary or a wrong pattern would pass this gate vacuously
#   (d) the binary runs and prints its globals
#   (e) the interpreter runs the same JavaScript and prints its globals
#       (ctcompile-test-native-reference) - after a probe program with known
#       answers has shown the reference formats, sorts and CLASSIFIES as the
#       convention says, one of every kind, so that a reference which has lost
#       the ability to print a boolean cannot agree with a binary that never
#       printed one either
#   (f) both texts are checked against the convention's own grammar, then
#       compared line by line; a difference NAMES the global
#   (g) the counters are asserted and one line is printed:
#         native unit (<name>): N globals agree, 0 ctbrowser symbols
#
# ============================================================================
# THE OUTPUT CONVENTION, which both sides print and this script checks.
# ============================================================================
#
# One line per global the program created or changed, ascending bytewise by
# name, `<name>=<value>`, and the value is one of five forms:
#
#     Number      printf("%.17g", d)          1   -0   2.5   inf   -inf   nan
#     Boolean     true | false
#     undefined   undefined
#     null        null
#     String      "<percent-encoded bytes>"   "text"   ""   "a%3D1"
#
# THE NUMBER FORM IS UNCHANGED, to the byte, from when it was the only form.
# %.17g round-trips a double exactly, so the comparison is on the double's
# VALUE and not on JavaScript's shortest-representation string
# (Number-to-String is a runtime concern for a later phase), and every fixture
# that predates the other four kinds expects exactly that text.
#
# THE FIVE FORMS ARE PAIRWISE DISJOINT, which is what lets a Number stay bare
# while the other four are still unambiguous. Every %.17g output is a digit
# string with an optional sign, point and exponent, or `inf`, `-inf`, `nan`,
# `-nan`; none of those is `true`, `false`, `undefined` or `null`, and none of
# them contains a `"`. So the kind of a value is recoverable from its text.
# `nan` and `null` are a one-glyph pair to a reader, but they are different
# tokens and no %.17g output spells `null`.
#
# WHY PERCENT-ENCODING FOR STRINGS. A JavaScript string in this engine is
# BYTES, not text:
#
#   * A lone surrogate is stored as WTF-8 and is NOT VALID UTF-8. `"\uD800"`
#     with no low surrogate after it becomes the three bytes ED A0 80
#     (ctbrowser/lib/Script/compile/strings.cpp, encode_code_point). Any
#     escaping that decoded to code points first would have to agree with the
#     other side about how to repair those bytes. Percent-encoding is per byte,
#     so ED A0 80 is `%ED%A0%80` on both sides with no decoder in the loop.
#   * EMBEDDED NUL IS A LEGAL STRING CHARACTER, so nothing may be
#     NUL-terminated: both sides walk the length and print 0x00 as `%00`.
#   * The encoding is TOTAL AND INJECTIVE: every one of the 256 byte values is
#     in the domain and has exactly one representation. RFC 3986's unreserved
#     set (A-Z a-z 0-9 - . _ ~) passes through; every other byte becomes `%`
#     and exactly two UPPERCASE hex digits. Fixed width, no shorthands, no
#     second spelling of any byte - so decoding needs no lookahead past two
#     characters and the encoder is a function.
#   * THE RESULT IS PRINTABLE ASCII WITH NO WHITESPACE, and that is the part
#     this script depends on. The two texts do not reach each other directly:
#     they are captured by execute_process into CMake variables, cut on
#     newlines with string(REPLACE) and indexed with list(GET). A `;` in a
#     value would silently split one line into two list elements and shift
#     every comparison after it; `\` is CMake's escape character; a raw newline
#     would forge an extra global; a NUL would truncate the capture; `$` would
#     invite expansion. `%3B`, `%5C`, `%0A`, `%00` and `%24` remove all of them
#     by construction rather than by hoping no fixture contains one. `=` is
#     escaped too, so the first `=` on a line is always the separator.
#
# THE GRAMMAR ABOVE IS CHECKED, on both texts, before they are compared - see
# `_grammar` below. Comparing two texts only proves they agree; the grammar
# check is what proves they agree IN THE CONVENTION, so a shared mistake in the
# escaping cannot pass by corrupting both sides the same way.
#
# The native side prints all of this from `main` after the last top-level
# statement (native-values-fixture.emitc.mlir is the specification of that
# code, down to the helper functions it has to define); the reference prints
# the interpreter's answer for the same program the same way. `-nan` and `nan`
# compare equal: a NaN's sign is not observable in JavaScript, x86 arithmetic
# produces negative NaNs and constant folding produces positive ones, and the
# interpreter and the binary get one each.
#
# ============================================================================
#
# WHY NOT --declare-variables-at-top, which the boxed pipeline needs: the
# emitter declares a variable for the `emitc.expression` that feeds a `do`
# loop's condition and never assigns it (it is printed inline in the `while`),
# which is an unused variable, which -Wall -Werror rejects. Native functions are
# single-block - their control flow is emitc.if/for/do, not cf branches - so
# the flag is not needed here, and its absence is what keeps that emitter
# quirk out of this gate. A lowering that emits multi-block functions would
# need the flag and would meet the quirk; that is a one-hunk fix in the fork.
#
#   -DTRANSLATE=  ctjs-translate (hosts the forked emitter under -mlir-to-cpp)
#   -DMODULE=     the EmitC module (.mlir)
#   -DJS=         the JavaScript program the module is the lowering of
#   -DCXX=        the C++ compiler
#   -DNM=         nm or llvm-nm
#   -DREFERENCE=  ctcompile-test-native-reference
#   -DVM_LINKED=  a binary that links the interpreter (ctcompile-test-type-oracle)
#   -DWORK=       a writable directory; this uses WORK/native-unit-NAME/
#   -DNAME=       what to call this unit in the report
#
# NEGATIVE PROOFS, so the gate's teeth stay in the suite:
#   -DMUTATE=<global>   after (a), the emitted C++ is perturbed so that ONE
#                       global prints a different value - the gate must then
#                       FAIL naming <global>
#   -DMUTATE_AS=<kind>  which perturbation, because `<g> = <g> + 1` is a no-op
#                       on everything that is not a number and would make the
#                       proof VACUOUS for exactly the kinds this convention
#                       added. Defaults to `number`, which is the historical
#                       behaviour. See the block that implements it.
#   -DPREBUILT=<exe>    skip (a) and (b) and gate this binary instead; used
#                       with ctcompile-test-native-vm-linked, which is the
#                       fixture's own C++ plus one object that reaches the
#                       interpreter - the gate must FAIL on its nm check
#   -DEXPECT_FAILURE=<regex>
#                       run the gate as a child process and PASS only if it
#                       FAILED and its output matches the regex. ctest's
#                       WILL_FAIL cannot do the second half, and a negative
#                       test that passes on any failure at all - a missing
#                       compiler, say - has stopped proving anything.
cmake_minimum_required(VERSION 3.20)

foreach(required TRANSLATE MODULE JS CXX NM REFERENCE VM_LINKED WORK NAME)
  if(NOT DEFINED ${required})
    message(FATAL_ERROR "check-native-unit.cmake: -D${required}= is required")
  endif()
endforeach()

# --- the negative harness: run myself, require failure AND its reason ---------
if(DEFINED EXPECT_FAILURE)
  set(_forward)
  foreach(_v TRANSLATE MODULE JS CXX NM REFERENCE VM_LINKED WORK NAME MUTATE MUTATE_AS PREBUILT)
    if(DEFINED ${_v})
      list(APPEND _forward "-D${_v}=${${_v}}")
    endif()
  endforeach()
  execute_process(
    COMMAND "${CMAKE_COMMAND}" ${_forward} -P "${CMAKE_SCRIPT_MODE_FILE}"
    OUTPUT_VARIABLE _out ERROR_VARIABLE _err RESULT_VARIABLE _rc)
  if(_rc EQUAL 0)
    message(FATAL_ERROR
      "${NAME}: the gate PASSED where it had to fail - it has no teeth for this case\n${_out}${_err}")
  endif()
  if(NOT "${_out}${_err}" MATCHES "${EXPECT_FAILURE}")
    message(FATAL_ERROR
      "${NAME}: the gate failed, but not for the reason expected (wanted /${EXPECT_FAILURE}/):\n${_out}${_err}")
  endif()
  string(REGEX MATCH "[^\n]*${EXPECT_FAILURE}[^\n]*" _named "${_out}${_err}")
  message(STATUS "negative proof (${NAME}): the gate failed as it must, and said why: ${_named}")
  return()
endif()

set(_dir "${WORK}/native-unit-${NAME}")
file(MAKE_DIRECTORY "${_dir}")

# THE CONVENTION AS A REGEX - the five forms of the value, in one place, so
# that the paragraph at the top of this file is a thing the gate checks rather
# than a thing it hopes.
set(_grammar "^[A-Za-z_$][A-Za-z0-9_$]*=(true|false|undefined|null|-?(inf|nan)|-?[0-9]+(\\.[0-9]+)?(e[-+][0-9]+)?|\"([-A-Za-z0-9._~]|%[0-9A-F][0-9A-F])*\")$")

# --- (a) EmitC -> C++ through the forked emitter, (b) standalone compile -------
if(DEFINED PREBUILT)
  set(_exe "${PREBUILT}")
  message(STATUS "${NAME}: gating a prebuilt binary, ${_exe}")
else()
  execute_process(
    COMMAND "${TRANSLATE}" --mlir-to-cpp "${MODULE}"
    OUTPUT_VARIABLE _cpp ERROR_VARIABLE _err RESULT_VARIABLE _rc)
  if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "${NAME}: the emitter refused ${MODULE} (exit ${_rc})\n${_err}")
  endif()
  string(LENGTH "${_cpp}" _produced)
  if(_produced EQUAL 0)
    message(FATAL_ERROR "${NAME}: the emitter produced nothing for ${MODULE}")
  endif()
  # THE C++ MUST NOT REACH FOR THE ENGINE'S HEADERS EITHER: a translation unit
  # that includes ctbrowser/ and happens to link is one inline function away
  # from the interpreter, and the standalone compile below would catch the
  # include only by failing on the path - which reads as a build problem.
  if(_cpp MATCHES "#include[ \t]*[<\"]ctbrowser")
    message(FATAL_ERROR "${NAME}: the emitted C++ includes a ctbrowser header - that is not native")
  endif()

  if(DEFINED MUTATE AND NOT MUTATE STREQUAL "")
    # THE PERTURBATION IS PER KIND, and that is not tidiness.
    #
    # This used to be `<g> = <g> + 1;` for everything. Adding one is a NO-OP on
    # a NaN, and it would be a no-op - or a compile error - on every kind the
    # convention gained: `bool b = b + 1` does not compile under -Werror, a
    # std::string has no `+ 1`, and a global whose proven type is `undefined`
    # or `null` HAS NO STORAGE TO ADD TO. A single mutation would therefore
    # have left the negative proof vacuous for exactly the values this
    # convention exists to compare. Each kind gets the smallest edit that
    # changes the one line it prints:
    #
    #   number      `<g> = <g> + 1;` before the first print, as before
    #   boolean     the printed value becomes `!<g>`      - true <-> false
    #   string      the printed value gains a prefix
    #   undefined   the print becomes `print_null`        - the confusion that
    #   null        the print becomes `print_undefined`     actually matters
    #
    # The last two are edits to the CALL and not to a value, because there is
    # no value: `undefined` and `null` globals are compile-time facts with no
    # runtime storage, so the only thing a wrong compiler can get wrong about
    # one is which constant it prints. That is what these mutate.
    #
    # A GUARD THAT SILENTLY EDITS NOTHING LOOKS EXACTLY LIKE A MUTATION THAT
    # WAS CAUGHT, so every branch below asserts that its edit landed, and the
    # substituting branches assert first that the text they are replacing
    # occurs EXACTLY ONCE - twice would mean the edit was not about one global
    # after all.
    if(NOT DEFINED MUTATE_AS)
      set(MUTATE_AS "number")
    endif()
    if(MUTATE_AS STREQUAL "number")
      # THE OFF-BY-ONE, inserted where the convention says the program has
      # ended and the printing begins.
      #
      # INSIDE main, AND THAT IS ASSERTED. The search used to run over the
      # whole file, which was safe only for as long as no helper above main
      # contained a printf. The value-printing prelude
      # (native-values-fixture.emitc.mlir) is written with fputs and putchar
      # for this reason among others; if one ever grows a printf, the
      # insertion would land in the middle of a helper and the mutation would
      # test nothing. Anchoring on main is what notices.
      string(FIND "${_cpp}" "int32_t main() {" _main_at)
      if(_main_at EQUAL -1)
        message(FATAL_ERROR "${NAME}: cannot mutate - no `int32_t main() {` in the emitted C++, the output convention has changed under this script")
      endif()
      string(SUBSTRING "${_cpp}" ${_main_at} -1 _from_main)
      string(FIND "${_from_main}" "std::printf(" _relative)
      if(_relative EQUAL -1)
        message(FATAL_ERROR "${NAME}: cannot mutate - no std::printf( inside main in the emitted C++, the output convention has changed under this script")
      endif()
      math(EXPR _at "${_main_at} + ${_relative}")
      string(SUBSTRING "${_cpp}" 0 ${_at} _head)
      string(SUBSTRING "${_cpp}" ${_at} -1 _tail)
      # AND THE INSERTION MUST PRECEDE THE LOAD, or it changes nothing.
      #
      # MEASURED, not anticipated. The printing reads each global into a
      # temporary and prints the temporary, so an assignment inserted after
      # `<g>` has already been read is INVISIBLE - the binary prints the old
      # value, the gate agrees with the interpreter, and the negative test
      # reports only "the gate PASSED where it had to fail", leaving the author
      # to guess why. That is exactly what happened the first time this was
      # pointed at the FIRST global in sorted order, whose load sits above the
      # first `std::printf(`. It has always been true of this mechanism; it was
      # invisible only because `fib20` happens to sort after `clamped`.
      #
      # The load is `<type> vN = <g>;`, so "is this global still read after the
      # insertion point" is the question `= <g>;` answers.
      if(NOT _tail MATCHES "= ${MUTATE};")
        # ONE WORD IS THE ANCHOR, and that is not a stylistic choice either:
        # message() REWRAPS its text, so a multi-word EXPECT_FAILURE pattern can
        # be split across a line break and stop matching for no reason but the
        # length of ${NAME}. This one was, first time out. A single word cannot
        # be.
        message(FATAL_ERROR
          "${NAME}: VACUOUS - mutating ${MUTATE} here would change nothing the binary prints, "
          "because the printing has already read it above the insertion point. Mutate a "
          "global that sorts later than the first one printed, or use a name-directed "
          "-DMUTATE_AS=.")
      endif()
      set(_cpp "${_head}${MUTATE} = ${MUTATE} + 1;\n  ${_tail}")
      if(NOT _cpp MATCHES "${MUTATE} = ${MUTATE} \\+ 1;")
        message(FATAL_ERROR "${NAME}: the mutation of ${MUTATE} did not apply")
      endif()
      message(STATUS "${NAME}: MUTATED - ${MUTATE} is one more than the program computed")
    else()
      # THE NAME-DIRECTED EDITS. Each names the global in the text it replaces,
      # so a mutation cannot land on a different global than the one the test
      # asked for and then be reported under the wrong name.
      if(MUTATE_AS STREQUAL "boolean")
        set(_find "ctnative::print_boolean(\"${MUTATE}\", ")
        set(_into "ctnative::print_boolean(\"${MUTATE}\", !")
        set(_why "${MUTATE} prints the boolean the program did NOT compute")
      elseif(MUTATE_AS STREQUAL "string")
        set(_find "ctnative::print_string(\"${MUTATE}\", ")
        set(_into "ctnative::print_string(\"${MUTATE}\", std::string(\"ctcompile-mutation-\") + ")
        set(_why "${MUTATE} prints a string the program did not build")
      elseif(MUTATE_AS STREQUAL "undefined")
        set(_find "ctnative::print_undefined(\"${MUTATE}\")")
        set(_into "ctnative::print_null(\"${MUTATE}\")")
        set(_why "${MUTATE} is undefined but prints as null")
      elseif(MUTATE_AS STREQUAL "null")
        set(_find "ctnative::print_null(\"${MUTATE}\")")
        set(_into "ctnative::print_undefined(\"${MUTATE}\")")
        set(_why "${MUTATE} is null but prints as undefined")
      else()
        message(FATAL_ERROR "${NAME}: -DMUTATE_AS=${MUTATE_AS} is not one of the kinds this convention has (number, boolean, string, undefined, null)")
      endif()
      string(FIND "${_cpp}" "${_find}" _at)
      if(_at EQUAL -1)
        message(FATAL_ERROR "${NAME}: cannot mutate ${MUTATE} as a ${MUTATE_AS} - the emitted C++ contains no `${_find}`, so the output convention has changed under this script")
      endif()
      string(LENGTH "${_find}" _find_length)
      math(EXPR _past "${_at} + ${_find_length}")
      string(SUBSTRING "${_cpp}" ${_past} -1 _rest)
      string(FIND "${_rest}" "${_find}" _again)
      if(NOT _again EQUAL -1)
        message(FATAL_ERROR "${NAME}: `${_find}` occurs more than once in the emitted C++ - this mutation would not be about one global")
      endif()
      string(SUBSTRING "${_cpp}" 0 ${_at} _head)
      set(_cpp "${_head}${_into}${_rest}")
      string(FIND "${_cpp}" "${_into}" _landed)
      if(_landed EQUAL -1)
        message(FATAL_ERROR "${NAME}: the mutation of ${MUTATE} as a ${MUTATE_AS} did not apply")
      endif()
      message(STATUS "${NAME}: MUTATED - ${_why}")
    endif()
  endif()

  set(_src "${_dir}/unit.cpp")
  file(WRITE "${_src}" "${_cpp}")
  set(_exe "${_dir}/unit")
  # NOTHING OF ctbrowser'S ON THIS LINE. No -I, no -l, no library: the whole
  # point. -ffp-contract=off is explained at the top.
  execute_process(
  # Generated code must be clean under the same flags as the dedicated Phase
  # 63 gate. A warning here is a ctcompile bug, not something to suppress.
  COMMAND "${CXX}" -std=c++23 -O2 -Wall -Wextra -Werror -pedantic -Wconversion -ffp-contract=off
          -o "${_exe}" "${_src}"
    OUTPUT_VARIABLE _out ERROR_VARIABLE _err RESULT_VARIABLE _rc)
  if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "${NAME}: the emitted C++ does not compile standalone (exit ${_rc})\n${_out}${_err}")
  endif()
endif()

# --- (c) the symbol check, proved load-bearing before it is trusted -----------
execute_process(
  COMMAND "${NM}" -C "${VM_LINKED}"
  OUTPUT_VARIABLE _nm_control ERROR_VARIABLE _err RESULT_VARIABLE _rc)
if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "${NAME}: ${NM} failed on the control binary ${VM_LINKED} (exit ${_rc})\n${_err}")
endif()
string(REGEX MATCHALL "[^\n]*ctbrowser::script::[^\n]*" _control_hits "${_nm_control}")
list(LENGTH _control_hits _control_count)
if(_control_count EQUAL 0)
  message(FATAL_ERROR
    "${NAME}: the nm check is NOT load-bearing - ${VM_LINKED} links the interpreter and "
    "`${NM} -C` found no ctbrowser::script:: symbol in it. The invocation, the demangling "
    "or the pattern is wrong, and a pass on the native binary would mean nothing.")
endif()

execute_process(
  COMMAND "${NM}" -C "${_exe}"
  OUTPUT_VARIABLE _nm ERROR_VARIABLE _err RESULT_VARIABLE _rc)
if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "${NAME}: ${NM} failed on ${_exe} (exit ${_rc})\n${_err}")
endif()
if(NOT _nm MATCHES "[ \t]main\n")
  message(FATAL_ERROR "${NAME}: `${NM} -C` listed no `main` in ${_exe} - it did not read the binary\n${_nm}")
endif()
string(REGEX MATCHALL "[^\n]*ctbrowser::(script|aot)::[^\n]*" _hits "${_nm}")
list(LENGTH _hits _vm_symbols)
if(NOT _vm_symbols EQUAL 0)
  list(GET _hits 0 _first)
  string(STRIP "${_first}" _first)
  message(FATAL_ERROR
    "${NAME}: the binary reaches the interpreter - ${_vm_symbols} ctbrowser symbol(s) "
    "(the control binary had ${_control_count}); the first is: ${_first}")
endif()

# --- (d) run the binary ---------------------------------------------------------
execute_process(
  COMMAND "${_exe}"
  OUTPUT_VARIABLE _native ERROR_VARIABLE _err RESULT_VARIABLE _rc)
if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "${NAME}: the native binary exited ${_rc}\n${_native}${_err}")
endif()

# --- (e) the reference: first proved on a probe, then run on the program ------
#
# THE PROBE HAS ONE OF EVERYTHING THE CONVENTION HAS TO GET RIGHT, and it is
# what stands between this gate and the failure it is easiest to have: a
# reference that prints nothing for a kind, agreeing perfectly with a binary
# that prints nothing for it either. Every kind is here, and so is every case
# in which one kind could be mistaken for another:
#
#   z, a      two numbers out of source order, so the sort is exercised
#   m         a NEGATIVE ZERO, which must print `-0` and not `0`
#   q         a NaN, either sign, compared as `nan`
#   b, c      BOTH booleans - one of them printed where the other belongs is
#             the failure a single-boolean probe cannot see
#   n         null      - and `q` above is `nan`, the token it most resembles
#   u         a `var` declared and never assigned, so its value is `undefined`
#   s         an ordinary string
#   e         THE EMPTY STRING, which must print `e=""` and not nothing at all
#   d         the string "nan" - which must not be mistaken for the number
#   p         a string holding `=`, `;`, `\`, `"` and `%`: every character that
#             would otherwise break a layer between the two programs
#   w         a NUL, a two-byte code point, and a newline, inside a string
#   g         a LONE SURROGATE - WTF-8, and not valid UTF-8
#   f         a function, which is skipped and counted
#   o         an OBJECT, which is a kind the convention has no form for: it is
#             skipped and counted under `other`, and asserting that count is 1
#             here is what keeps the `other` path alive. The gate requires that
#             same count to be ZERO for a real program, so without this probe
#             the check would never once be seen to fire.
set(_probe "${_dir}/probe.js")
file(WRITE "${_probe}" [==[
function f(x) { return x; }
var z = f(2);
var a = 1;
var m = -0;
var q = 0 / 0;
var b = true;
var c = false;
var n = null;
var u;
var s = "text";
var e = "";
var d = "nan";
var p = "a=1;b\\c\"d%e";
var w = "\u0000\u00ff\n";
var g = "\ud800";
var o = {};
]==])
execute_process(
  COMMAND "${REFERENCE}" "${_probe}"
  OUTPUT_VARIABLE _probe_out ERROR_VARIABLE _probe_err RESULT_VARIABLE _rc)
if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "${NAME}: the reference failed on the probe (exit ${_rc})\n${_probe_err}")
endif()
string(REPLACE "=-nan" "=nan" _probe_out "${_probe_out}")
set(_probe_expected [==[
a=1
b=true
c=false
d="nan"
e=""
g="%ED%A0%80"
m=-0
n=null
p="a%3D1%3Bb%5Cc%22d%25e"
q=nan
s="text"
u=undefined
w="%00%C3%BF%0A"
z=2
]==])
if(NOT _probe_out STREQUAL "${_probe_expected}")
  message(FATAL_ERROR "${NAME}: the reference does not print the convention - on the probe it printed:\n${_probe_out}expected:\n${_probe_expected}")
endif()
# THE CLASSIFICATION, ASSERTED SEPARATELY FROM THE TEXT. The lines above prove
# what was printed; this proves the reference knows WHY, per kind - which is
# what the real program's run below relies on and cannot check for itself.
if(NOT _probe_err MATCHES "14 globals printed \\(4 number, 2 boolean, 6 string, 1 null, 1 undefined\\), 1 function globals skipped, 1 other globals skipped")
  message(FATAL_ERROR "${NAME}: the reference miscounted the probe - it must find 4 numbers, 2 booleans, 6 strings, 1 null, 1 undefined, 1 function and 1 other:\n${_probe_err}")
endif()

execute_process(
  COMMAND "${REFERENCE}" "${JS}"
  OUTPUT_VARIABLE _reference ERROR_VARIABLE _reference_err RESULT_VARIABLE _rc)
if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "${NAME}: the interpreter failed on ${JS} (exit ${_rc})\n${_reference_err}")
endif()
if(NOT _reference_err MATCHES "([0-9]+) globals printed \\(([0-9]+) number, ([0-9]+) boolean, ([0-9]+) string, ([0-9]+) null, ([0-9]+) undefined\\), ([0-9]+) function globals skipped, ([0-9]+) other globals skipped")
  message(FATAL_ERROR "${NAME}: the reference did not report its counts:\n${_reference_err}")
endif()
set(_printed "${CMAKE_MATCH_1}")
set(_numbers "${CMAKE_MATCH_2}")
set(_booleans "${CMAKE_MATCH_3}")
set(_strings "${CMAKE_MATCH_4}")
set(_nulls "${CMAKE_MATCH_5}")
set(_undefineds "${CMAKE_MATCH_6}")
set(_functions "${CMAKE_MATCH_7}")
set(_others "${CMAKE_MATCH_8}")
# THERE WAS A "the per-kind counts must sum to the total" CHECK HERE, AND IT IS
# GONE. It could not be made to fail. Every way of breaking the reference's
# counting - counting a kind twice, not counting it at all - is caught by the
# probe above, which pins all eight numbers exactly, and the groups in the
# regex cannot shift because each is anchored to its own literal word. A guard
# nothing can make fire is decoration; it was measured, and deleted.
if(NOT _others EQUAL 0)
  message(FATAL_ERROR "${NAME}: ${JS} left ${_others} global(s) of a kind this convention has no form for - an object, an array, a symbol or a bigint. A phase that wants one has to extend the convention in this file first:\n${_reference_err}")
endif()
if(_printed LESS_EQUAL 0)
  message(FATAL_ERROR "${NAME}: the interpreter left no global to print - nothing was compared")
endif()

# --- (f) check the grammar, then compare, naming the global ---------------------
string(REPLACE "=-nan" "=nan" _native "${_native}")
string(REPLACE "=-nan" "=nan" _reference "${_reference}")
string(REGEX REPLACE "\n$" "" _native_lines "${_native}")
string(REGEX REPLACE "\n$" "" _reference_lines "${_reference}")
string(REPLACE "\n" ";" _native_lines "${_native_lines}")
string(REPLACE "\n" ";" _reference_lines "${_reference_lines}")
list(LENGTH _native_lines _native_count)
list(LENGTH _reference_lines _reference_count)
if(NOT _reference_count EQUAL _printed)
  message(FATAL_ERROR "${NAME}: the reference said ${_printed} globals and printed ${_reference_count} lines")
endif()
# EVERY LINE, ON BOTH SIDES, IN THE CONVENTION'S OWN LANGUAGE. Two texts
# agreeing proves only that they agree; this proves they agree in the shape the
# top of this file describes, so a shared mistake in the escaping - a raw `;`,
# say, which would have already corrupted the list() above - is caught here and
# named rather than passing as an agreement.
foreach(_side native reference)
  foreach(_line IN LISTS _${_side}_lines)
    if(NOT _line MATCHES "${_grammar}")
      message(FATAL_ERROR "${NAME}: the ${_side} side printed a line that is not in the output convention: '${_line}'")
    endif()
  endforeach()
endforeach()

set(_agree 0)
math(EXPR _last "${_native_count} - 1")
if(_reference_count GREATER _native_count)
  math(EXPR _last "${_reference_count} - 1")
endif()
foreach(_i RANGE 0 ${_last})
  set(_n "")
  set(_r "")
  if(_i LESS _native_count)
    list(GET _native_lines ${_i} _n)
  endif()
  if(_i LESS _reference_count)
    list(GET _reference_lines ${_i} _r)
  endif()
  if(_n STREQUAL _r)
    math(EXPR _agree "${_agree} + 1")
    continue()
  endif()
  set(_n_name "")
  set(_r_name "")
  if(_n MATCHES "^([A-Za-z_$][A-Za-z0-9_$]*)=(.*)$")
    set(_n_name "${CMAKE_MATCH_1}")
    set(_n_value "${CMAKE_MATCH_2}")
  endif()
  if(_r MATCHES "^([A-Za-z_$][A-Za-z0-9_$]*)=(.*)$")
    set(_r_name "${CMAKE_MATCH_1}")
    set(_r_value "${CMAKE_MATCH_2}")
  endif()
  if(_n_name STREQUAL _r_name AND NOT _n_name STREQUAL "")
    message(FATAL_ERROR
      "${NAME}: global '${_n_name}' differs - native printed ${_n_value}, the interpreter printed ${_r_value}\n"
      "native:\n${_native}interpreter:\n${_reference}")
  endif()
  message(FATAL_ERROR
    "${NAME}: line ${_i} differs - native printed '${_n}', the interpreter printed '${_r}' "
    "(a missing or extra global, or one out of order: the convention sorts by name)\n"
    "native:\n${_native}interpreter:\n${_reference}")
endforeach()

# --- (g) the counters, then the one line --------------------------------------
if(NOT _agree EQUAL _printed)
  message(FATAL_ERROR "${NAME}: ${_agree} lines agreed but the interpreter printed ${_printed} globals")
endif()
if(NOT _native_count EQUAL _printed)
  message(FATAL_ERROR "${NAME}: the native binary printed ${_native_count} globals, the interpreter ${_printed}")
endif()
message(STATUS "native unit (${NAME}): ${_agree} globals agree "
               "(${_numbers} number, ${_booleans} boolean, ${_strings} string, ${_nulls} null, "
               "${_undefineds} undefined), ${_vm_symbols} ctbrowser symbols "
               "(${_functions} functions; the control binary showed ${_control_count} interpreter symbols to the same nm)")
