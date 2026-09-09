# THE ESCAPE ORACLE, CLOSED - Phase 55A checked by Phase 55O.
#
# check-type-claims.cmake's shape, over allocation sites instead of registers:
#
#   1. the INTERPRETER runs the corpus with the recorder installed and, at
#      every frame pop, asks its own collector whether each object the frame
#      allocated is still reachable         (ctcompile-test-type-oracle --script)
#   2. the COMPILER imports the same corpus, runs EscapeAnalysis, and writes
#      one claim per allocation site         (ctcompile-test-escape-claims)
#   3. tools/check/escape-oracle.py compares the two.
#
# ZERO SOUNDNESS VIOLATIONS IS THE GATE: a site claimed `confined` that the
# collector found reachable after its frame returned is a defect, and the
# checker names it. Precision is printed, never gated. The vacuous passes are
# guarded the same way as the type oracle's: something must have been
# observed, and at least one confined claim must have been proved SOUND - an
# analysis that says `escapes` everywhere is right the way a stopped clock is.
#
#   -DORACLE=  ctcompile-test-type-oracle     -DCLAIMS= ctcompile-test-escape-claims
#   -DPYTHON=  the interpreter                -DSCRIPT= tools/check/escape-oracle.py
#   -DCORPUS=  the JavaScript file            -DPREFIX= optional file prepended to it
#   -DWORK=    a writable directory           -DNAME=   the corpus's name in the report
#   -DBUDGET=  optional: checks per function before the recorder stops adjudicating
#   -DSTRICT=  ON for the fixture: partial and pending must both be zero

execute_process(COMMAND "${ORACLE}" OUTPUT_VARIABLE _self ERROR_VARIABLE _selferr RESULT_VARIABLE _rc)
if(_self MATCHES "SKIPPED")
  message(STATUS "escape claims (${NAME}): skipped - this build has no recording hook")
  return()
endif()
if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "the type oracle's self-test failed before the escape claims ran:\n${_self}${_selferr}")
endif()

set(_corpus "${CORPUS}")
if(PREFIX)
  file(READ "${PREFIX}" _prefix)
  file(READ "${CORPUS}" _driver)
  set(_corpus "${WORK}/escape-claims-${NAME}.js")
  file(WRITE "${_corpus}" "${_prefix}\n${_driver}")
endif()
set(_rec "${WORK}/escape-claims-${NAME}.rec")
set(_claims "${WORK}/escape-claims-${NAME}.claims")

set(_budget_flag "")
if(BUDGET)
  set(_budget_flag --escape-budget "${BUDGET}")
endif()
execute_process(
  COMMAND "${ORACLE}" --script "${_corpus}" --out "${_rec}" ${_budget_flag}
  OUTPUT_VARIABLE _out ERROR_VARIABLE _err RESULT_VARIABLE _rc)
message(STATUS "${_out}${_err}")
if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "recording ${NAME} failed (exit ${_rc})")
endif()

execute_process(
  COMMAND "${CLAIMS}" --script "${_corpus}" --out "${_claims}"
  OUTPUT_VARIABLE _out ERROR_VARIABLE _err RESULT_VARIABLE _rc)
message(STATUS "${_out}${_err}")
if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "claiming ${NAME} failed (exit ${_rc})")
endif()
# A TRACKED SITE OR A SINK OPERAND IN A LIVE BLOCK THE SOLVER NEVER VISITED is
# an analysis gap hidden behind `escapes` - sound, and asserted at zero.
if(NOT "${_out}${_err}" MATCHES "([0-9]+) unvisited live sites, ([0-9]+) unvisited operands")
  message(FATAL_ERROR "the claims emitter did not report its unvisited counts:\n${_out}${_err}")
endif()
if(NOT CMAKE_MATCH_1 EQUAL 0 OR NOT CMAKE_MATCH_2 EQUAL 0)
  message(FATAL_ERROR "${NAME}: ${CMAKE_MATCH_1} unvisited live site(s) and ${CMAKE_MATCH_2} unvisited operand(s) - a reachability gap in the analysis")
endif()

# The Stored backlog now reports the direct target of each first witness.
# This is not a contents proof: even local-confined targets can expose children
# through a spread call, and subsequent stores can retain elsewhere. Require a
# complete partition of the unchanged Stored claims, without gating precision.
if(NOT "${_out}${_err}" MATCHES "stored first-witness targets: ([0-9]+) sites, ([0-9]+) local-confined, ([0-9]+) local-escaping, ([0-9]+) local-mixed, ([0-9]+) external-or-mixed, ([0-9]+) primitive, ([0-9]+) unresolved")
  message(FATAL_ERROR "the claims emitter did not report its storage target evidence:\n${_out}${_err}")
endif()
set(_stored_targets "${CMAKE_MATCH_1}")
math(EXPR _classified_targets "${CMAKE_MATCH_2} + ${CMAKE_MATCH_3} + ${CMAKE_MATCH_4} + ${CMAKE_MATCH_5} + ${CMAKE_MATCH_6} + ${CMAKE_MATCH_7}")
file(STRINGS "${_claims}" _stored_claims REGEX "^escape .* escapes:stored$")
list(LENGTH _stored_claims _stored_claim_count)
if(NOT _stored_targets EQUAL _classified_targets OR NOT _stored_targets EQUAL _stored_claim_count)
  message(FATAL_ERROR "${NAME}: ${_stored_targets} storage witnesses, ${_classified_targets} classified targets, ${_stored_claim_count} Stored claims - incomplete storage evidence")
endif()

# The all-write census must cover each first witness, including joins where one
# write stores several sites. Additional writes and other first reasons are
# precision evidence, not new confinement claims. Unsupported targets/regions
# retain partial records with an explicit incomplete marker.
if(NOT "${_out}${_err}" MATCHES "stored direct-write census: ([0-9]+) writes, ([0-9]+) site edges, ([0-9]+) multiple-store sites, ([0-9]+) other-first sites, ([0-9]+) unresolved values, ([0-9]+) unresolved targets")
  message(FATAL_ERROR "the claims emitter did not report its all-write census:\n${_out}${_err}")
endif()
if(CMAKE_MATCH_2 LESS _stored_claim_count OR NOT CMAKE_MATCH_5 EQUAL 0)
  message(FATAL_ERROR "${NAME}: missing Stored site edges or unresolved stored values in the all-write census")
endif()
if(NOT "${_out}${_err}" MATCHES "stored direct-write coverage: ([0-9]+) first witnesses of ([0-9]+) Stored sites, ([0-9]+) complete and ([0-9]+) incomplete of ([0-9]+) functions")
  message(FATAL_ERROR "the claims emitter did not report all-write completeness:\n${_out}${_err}")
endif()
math(EXPR _storage_functions "${CMAKE_MATCH_3} + ${CMAKE_MATCH_4}")
if(NOT CMAKE_MATCH_1 EQUAL _stored_claim_count OR NOT CMAKE_MATCH_2 EQUAL _stored_claim_count OR NOT _storage_functions EQUAL CMAKE_MATCH_5)
  message(FATAL_ERROR "${NAME}: the all-write census did not cover every first Stored witness or classify every function")
endif()

# Property reads and shared-local-site write links are diagnostic evidence.
# Gate coverage and link integrity, never a precision count or new confinement.
if(NOT "${_out}${_err}" MATCHES "direct-load candidates: ([0-9]+) links across ([0-9]+) reads, ([0-9]+) stored-site edges, ([0-9]+) external-or-mixed bases, ([0-9]+) unresolved bases, ([0-9]+) invalid links")
  message(FATAL_ERROR "the claims emitter did not report direct-load candidates:\n${_out}${_err}")
endif()
if(NOT CMAKE_MATCH_5 EQUAL 0 OR NOT CMAKE_MATCH_6 EQUAL 0)
  message(FATAL_ERROR "${NAME}: unresolved bases or invalid write links in the direct-load census")
endif()
if(NOT "${_out}${_err}" MATCHES "direct-load coverage: ([0-9]+) records, ([0-9]+) covered of ([0-9]+) live reads, ([0-9]+) complete and ([0-9]+) incomplete of ([0-9]+) functions")
  message(FATAL_ERROR "the claims emitter did not report direct-load coverage:\n${_out}${_err}")
endif()
math(EXPR _load_functions "${CMAKE_MATCH_4} + ${CMAKE_MATCH_5}")
if(NOT CMAKE_MATCH_1 EQUAL CMAKE_MATCH_3 OR NOT CMAKE_MATCH_2 EQUAL CMAKE_MATCH_3 OR NOT _load_functions EQUAL CMAKE_MATCH_6)
  message(FATAL_ERROR "${NAME}: the direct-load census did not cover every live property read or classify every function")
endif()

# The separately bounded closure must preserve all original candidates and
# cover every direct read and sink even when its work limit is exhausted.
# Convergence and candidate counts do not authorize new escape claims.
if(NOT "${_out}${_err}" MATCHES "load-provenance candidates: ([0-9]+) read-site edges, ([0-9]+) stored-site edges, ([0-9]+) exposure-site edges, ([0-9]+) propagated exposure edges, ([0-9]+) invalid records")
  message(FATAL_ERROR "the claims emitter did not report candidate provenance:\n${_out}${_err}")
endif()
if(NOT CMAKE_MATCH_5 EQUAL 0)
  message(FATAL_ERROR "${NAME}: invalid or narrowed candidate provenance")
endif()
if(NOT "${_out}${_err}" MATCHES "load-provenance coverage: ([0-9]+) reads of ([0-9]+) live reads, ([0-9]+) exposures of ([0-9]+) live sinks, ([0-9]+) converged and ([0-9]+) exhausted of ([0-9]+) functions, ([0-9]+) complete and ([0-9]+) incomplete inputs")
  message(FATAL_ERROR "the claims emitter did not report provenance coverage:\n${_out}${_err}")
endif()
math(EXPR _provenance_functions "${CMAKE_MATCH_5} + ${CMAKE_MATCH_6}")
math(EXPR _provenance_inputs "${CMAKE_MATCH_8} + ${CMAKE_MATCH_9}")
if(NOT CMAKE_MATCH_1 EQUAL CMAKE_MATCH_2 OR NOT CMAKE_MATCH_3 EQUAL CMAKE_MATCH_4 OR NOT _provenance_functions EQUAL CMAKE_MATCH_7 OR NOT _provenance_inputs EQUAL CMAKE_MATCH_7)
  message(FATAL_ERROR "${NAME}: candidate provenance lost live reads, sinks or function classification")
endif()

execute_process(
  COMMAND "${PYTHON}" "${SCRIPT}" --recording "${_rec}" --claims "${_claims}"
          --name "${NAME}" --max-report 0 --expect-violations 0
  OUTPUT_VARIABLE _pyout ERROR_VARIABLE _pyerr RESULT_VARIABLE _pyrc)
message(STATUS "${_pyout}${_pyerr}")

if(NOT _pyout MATCHES "observed sites +([0-9]+)")
  message(FATAL_ERROR "the checker did not report observed sites:\n${_pyout}")
endif()
set(_observed "${CMAKE_MATCH_1}")
if(NOT _pyout MATCHES "SOUNDNESS violations ([0-9]+) +sound ([0-9]+) +partial ([0-9]+) +pending ([0-9]+)")
  message(FATAL_ERROR "the checker did not report its verdict tally:\n${_pyout}")
endif()
set(_violations "${CMAKE_MATCH_1}")
set(_sound "${CMAKE_MATCH_2}")
set(_partial "${CMAKE_MATCH_3}")
set(_pending "${CMAKE_MATCH_4}")
if(NOT _pyout MATCHES "UNCLAIMED observed sites ([0-9]+)")
  message(FATAL_ERROR "the checker did not report unclaimed sites:\n${_pyout}")
endif()
set(_unclaimed "${CMAKE_MATCH_1}")
if(NOT _pyout MATCHES "PRECISION confined ([0-9]+)/([0-9]+) = ([0-9.]+)%")
  message(FATAL_ERROR "the checker did not report precision:\n${_pyout}")
endif()
set(_precision "${CMAKE_MATCH_1}/${CMAKE_MATCH_2} = ${CMAKE_MATCH_3}%")
string(REGEX MATCH "reasons: [^\n]*" _reasons "${_pyout}")

if(_observed LESS_EQUAL 0)
  message(FATAL_ERROR "${NAME}: no site was observed - the corpus did not run, so nothing was checked")
endif()
if(NOT _violations EQUAL 0)
  message(FATAL_ERROR "${NAME}: ${_violations} SOUNDNESS violation(s) - a site claimed confined was reachable after its frame returned; every one is named above")
endif()
# THE STOPPED-CLOCK GUARD IS THE FIXTURE'S. A corpus under --script executes
# an eighth of itself, and on bootstrap none of the 28 sites the analysis
# calls confined is in a function that ever runs - so "sound > 0" cannot be
# met there and would gate on execution coverage, not on the analysis. The
# fixture calls every one of its confined sites; that is where the guard has
# teeth. On a corpus the escapes claims still carry evidence (an EXACT count),
# and sound/precision are printed for the record.
if(STRICT)
  if(_sound LESS_EQUAL 0)
    message(FATAL_ERROR "${NAME}: no confined claim was proved sound - the analysis said escapes everywhere, which is right the way a stopped clock is")
  endif()
  if(NOT _partial EQUAL 0 OR NOT _pending EQUAL 0)
    message(FATAL_ERROR "${NAME}: partial ${_partial}, pending ${_pending} - the fixture must adjudicate every confined claim")
  endif()

  # R3's composed publication cases need both halves of the evidence, not just
  # the absence of a soundness violation. Assert the actual lifetime counts and
  # globals route alongside the independent claim, joining by source coordinate.
  # The two conditional sites deliberately mix retained and confined instances.
  file(STRINGS "${_rec}" _recording_lines)
  file(READ "${_claims}" _claim_text)
  set(_publication_rows "")
  set(_spread_rows "")
  set(_array_frame_rows "")
  set(_object_deletion_rows "")
  set(_object_copy_rows "")
  set(_object_copy_path_rows "")
  set(_object_switch_selector_rows "")
  set(_object_negation_rows "")
  set(_object_total_unary_rows "")
  set(_object_static_binary_rows "")
  set(_object_arithmetic_unary_rows "")
  set(_object_loose_equality_rows "")
  set(_object_relational_rows "")
  set(_object_arithmetic_binary_rows "")
  set(_object_add_concat_rows "")
  set(_object_bigint_equality_rows "")
  set(_object_bigint_relational_rows "")
  set(_object_bigint_unary_rows "")
  set(_object_bigint_binary_rows "")
  set(_object_bigint_static_rows "")
  set(_object_bigint_shift_rows "")
  set(_object_bigint_shift_error_rows "")
  set(_object_bigint_shift_literal_pcs "")
  set(_object_bigint_divmod_rows "")
  set(_object_bigint_divmod_error_rows "")
  set(_object_bigint_divmod_literal_pcs "")
  # This source-backed RangeError is allocated by the VM at the signed shift,
  # not by a source object literal. Pin the exact body and measured bytecode pc
  # independently from every literal site's mandatory compiler claim below.
  file(READ "${_corpus}" _fixture_source)
  string(REGEX MATCH "function objectFrameBigIntShiftEarly\\(choice\\) \\{[^\n]*\n(    [^\n]*\n)*\\}" _shift_error_source "${_fixture_source}")
  string(SHA256 _shift_error_hash "${_shift_error_source}")
  if(NOT _shift_error_hash STREQUAL "2a35f86f51344a271fec92b345a6b0b5e748f2295cb454d501cf26e2b79df51c")
    message(FATAL_ERROR "the signed-shift exception source changed; remeasure its bytecode coordinate before updating this case")
  endif()
  # Div and Mod have distinct error-producing source operations. Their fixed
  # zero-divisor errors must be recorded independently of the literal sites.
  foreach(_kind Div Mod)
    string(REGEX MATCH "function objectFrameBigIntDivMod${_kind}Early\\(choice\\) \\{[^\n]*\n(    [^\n]*\n)*\\}" _divmod_error_source "${_fixture_source}")
    string(SHA256 _divmod_error_hash "${_divmod_error_source}")
    if(_kind STREQUAL "Div")
      set(_expected_divmod_error_hash "c742ba470b572744485a85ac7a3cc67f67797cc245ff63d69e31f9d2d5d4678f")
    else()
      set(_expected_divmod_error_hash "5d4ec3ed2578c19fa1dfd80d5cd9ee5168625675a3c26b03b6acc46d69c1bcd6")
    endif()
    if(NOT _divmod_error_hash STREQUAL _expected_divmod_error_hash)
      message(FATAL_ERROR "the BigInt ${_kind} exception source changed; remeasure its bytecode coordinate before updating this case")
    endif()
  endforeach()
  foreach(_line IN LISTS _recording_lines)
    if(_line MATCHES "^program ([0-9a-f]+) ")
      set(_program_hash "${CMAKE_MATCH_1}")
    elseif(_line MATCHES "^fn ([0-9]+) .* name ([^ ]+)$")
      set(_function_index "${CMAKE_MATCH_1}")
      set(_function_name "${CMAKE_MATCH_2}")
    elseif(_function_name MATCHES "^global(AliasJoin|AliasLoop|RetainedContainer|ReplacedPublication)$" AND _line MATCHES "^site ")
      if(NOT _line MATCHES "^site ([0-9]+) kind obj made ([0-9]+) confined ([0-9]+) escaped ([0-9]+) unresolved ([0-9]+) unchecked ([0-9]+) routes ([^ ]+)$")
        message(FATAL_ERROR "${_function_name}: unexpected publication observation: ${_line}")
      endif()
      set(_pc "${CMAKE_MATCH_1}")
      set(_row "${_function_name} ${CMAKE_MATCH_2} ${CMAKE_MATCH_3} ${CMAKE_MATCH_4} ${CMAKE_MATCH_5} ${CMAKE_MATCH_6} ${CMAKE_MATCH_7}")
      if(NOT _claim_text MATCHES "escape ${_program_hash} ${_function_index} ${_pc} obj ([^\n]+)")
        message(FATAL_ERROR "${_function_name}: no compiler claim for observed object at pc ${_pc}")
      endif()
      list(APPEND _publication_rows "${_row} ${CMAKE_MATCH_1}")
    elseif(_function_name MATCHES "^spreadRetained(Call|Construct|Receiver)$" AND _line MATCHES "^site ")
      if(NOT _line MATCHES "^site ([0-9]+) kind (obj|arr) made ([0-9]+) confined ([0-9]+) escaped ([0-9]+) unresolved ([0-9]+) unchecked ([0-9]+) routes ([^ ]+)$")
        message(FATAL_ERROR "${_function_name}: unexpected spread observation: ${_line}")
      endif()
      set(_pc "${CMAKE_MATCH_1}")
      set(_kind "${CMAKE_MATCH_2}")
      set(_row "${_function_name} ${_kind} ${CMAKE_MATCH_3} ${CMAKE_MATCH_4} ${CMAKE_MATCH_5} ${CMAKE_MATCH_6} ${CMAKE_MATCH_7} ${CMAKE_MATCH_8}")
      if(_claim_text MATCHES "escape ${_program_hash} ${_function_index} ${_pc} ${_kind} ([^\n]+)")
        list(APPEND _spread_rows "${_row} ${CMAKE_MATCH_1}")
      else()
        list(APPEND _spread_rows "${_row} unclaimed")
      endif()
    elseif(_function_name MATCHES "^arrayFrame(Private|Returned|SavedRead|Overwrite|LoadedAlias|Published|Call|Cycle|TransientCycle)$" AND _line MATCHES "^site ")
      if(NOT _line MATCHES "^site ([0-9]+) kind (obj|arr) made ([0-9]+) confined ([0-9]+) escaped ([0-9]+) unresolved ([0-9]+) unchecked ([0-9]+) routes ([^ ]+)$")
        message(FATAL_ERROR "${_function_name}: unexpected array-frame observation: ${_line}")
      endif()
      set(_pc "${CMAKE_MATCH_1}")
      set(_kind "${CMAKE_MATCH_2}")
      set(_row "${_function_name} ${_kind} ${CMAKE_MATCH_3} ${CMAKE_MATCH_4} ${CMAKE_MATCH_5} ${CMAKE_MATCH_6} ${CMAKE_MATCH_7} ${CMAKE_MATCH_8}")
      if(NOT _claim_text MATCHES "escape ${_program_hash} ${_function_index} ${_pc} ${_kind} ([^\n]+)")
        message(FATAL_ERROR "${_function_name}: no compiler claim for observed ${_kind} at pc ${_pc}")
      endif()
      list(APPEND _array_frame_rows "${_row} ${CMAKE_MATCH_1}")
    elseif(_function_name MATCHES "^objectFrameDeleted(Child|SavedRead|TransientCycle)$" AND _line MATCHES "^site ")
      if(NOT _line MATCHES "^site ([0-9]+) kind obj made ([0-9]+) confined ([0-9]+) escaped ([0-9]+) unresolved ([0-9]+) unchecked ([0-9]+) routes ([^ ]+)$")
        message(FATAL_ERROR "${_function_name}: unexpected object deletion observation: ${_line}")
      endif()
      set(_pc "${CMAKE_MATCH_1}")
      set(_row "${_function_name} ${CMAKE_MATCH_2} ${CMAKE_MATCH_3} ${CMAKE_MATCH_4} ${CMAKE_MATCH_5} ${CMAKE_MATCH_6} ${CMAKE_MATCH_7}")
      if(NOT _claim_text MATCHES "escape ${_program_hash} ${_function_index} ${_pc} obj ([^\n]+)")
        message(FATAL_ERROR "${_function_name}: no compiler claim for observed object at pc ${_pc}")
      endif()
      list(APPEND _object_deletion_rows "${_row} ${CMAKE_MATCH_1}")
    elseif(_function_name MATCHES "^objectFrameCopied(Child|Overwrite|SavedRead)$" AND _line MATCHES "^site ")
      if(NOT _line MATCHES "^site ([0-9]+) kind obj made ([0-9]+) confined ([0-9]+) escaped ([0-9]+) unresolved ([0-9]+) unchecked ([0-9]+) routes ([^ ]+)$")
        message(FATAL_ERROR "${_function_name}: unexpected object copy observation: ${_line}")
      endif()
      set(_pc "${CMAKE_MATCH_1}")
      set(_row "${_function_name} ${CMAKE_MATCH_2} ${CMAKE_MATCH_3} ${CMAKE_MATCH_4} ${CMAKE_MATCH_5} ${CMAKE_MATCH_6} ${CMAKE_MATCH_7}")
      if(NOT _claim_text MATCHES "escape ${_program_hash} ${_function_index} ${_pc} obj ([^\n]+)")
        message(FATAL_ERROR "${_function_name}: no compiler claim for observed object at pc ${_pc}")
      endif()
      list(APPEND _object_copy_rows "${_row} ${CMAKE_MATCH_1}")
    elseif(_function_name MATCHES "^objectFrameCopied(ConditionalAlias|ConditionalSource|SwitchSaved|LiteralOverwrite)$" AND _line MATCHES "^site ")
      if(NOT _line MATCHES "^site ([0-9]+) kind obj made ([0-9]+) confined ([0-9]+) escaped ([0-9]+) unresolved ([0-9]+) unchecked ([0-9]+) routes ([^ ]+)$")
        message(FATAL_ERROR "${_function_name}: unexpected object copy path observation: ${_line}")
      endif()
      set(_pc "${CMAKE_MATCH_1}")
      set(_row "${_function_name} ${CMAKE_MATCH_2} ${CMAKE_MATCH_3} ${CMAKE_MATCH_4} ${CMAKE_MATCH_5} ${CMAKE_MATCH_6} ${CMAKE_MATCH_7}")
      if(NOT _claim_text MATCHES "escape ${_program_hash} ${_function_index} ${_pc} obj ([^\n]+)")
        message(FATAL_ERROR "${_function_name}: no compiler claim for observed object at pc ${_pc}")
      endif()
      list(APPEND _object_copy_path_rows "${_row} ${CMAKE_MATCH_1}")
    elseif(_function_name STREQUAL "objectFrameSwitchReleased" AND _line MATCHES "^site ")
      if(NOT _line MATCHES "^site ([0-9]+) kind obj made ([0-9]+) confined ([0-9]+) escaped ([0-9]+) unresolved ([0-9]+) unchecked ([0-9]+) routes ([^ ]+)$")
        message(FATAL_ERROR "${_function_name}: unexpected source switch selector observation: ${_line}")
      endif()
      set(_pc "${CMAKE_MATCH_1}")
      set(_row "${_function_name} ${CMAKE_MATCH_2} ${CMAKE_MATCH_3} ${CMAKE_MATCH_4} ${CMAKE_MATCH_5} ${CMAKE_MATCH_6} ${CMAKE_MATCH_7}")
      if(NOT _claim_text MATCHES "escape ${_program_hash} ${_function_index} ${_pc} obj ([^\n]+)")
        message(FATAL_ERROR "${_function_name}: no compiler claim for observed object at pc ${_pc}")
      endif()
      list(APPEND _object_switch_selector_rows "${_row} ${CMAKE_MATCH_1}")
    elseif(_function_name STREQUAL "objectFrameNegatedReleased" AND _line MATCHES "^site ")
      if(NOT _line MATCHES "^site ([0-9]+) kind obj made ([0-9]+) confined ([0-9]+) escaped ([0-9]+) unresolved ([0-9]+) unchecked ([0-9]+) routes ([^ ]+)$")
        message(FATAL_ERROR "${_function_name}: unexpected logical negation observation: ${_line}")
      endif()
      set(_pc "${CMAKE_MATCH_1}")
      set(_row "${_function_name} ${CMAKE_MATCH_2} ${CMAKE_MATCH_3} ${CMAKE_MATCH_4} ${CMAKE_MATCH_5} ${CMAKE_MATCH_6} ${CMAKE_MATCH_7}")
      if(NOT _claim_text MATCHES "escape ${_program_hash} ${_function_index} ${_pc} obj ([^\n]+)")
        message(FATAL_ERROR "${_function_name}: no compiler claim for observed object at pc ${_pc}")
      endif()
      list(APPEND _object_negation_rows "${_row} ${CMAKE_MATCH_1}")
    elseif(_function_name MATCHES "^objectFrame(Typeof|Void)Released$" AND _line MATCHES "^site ")
      if(NOT _line MATCHES "^site ([0-9]+) kind obj made ([0-9]+) confined ([0-9]+) escaped ([0-9]+) unresolved ([0-9]+) unchecked ([0-9]+) routes ([^ ]+)$")
        message(FATAL_ERROR "${_function_name}: unexpected total unary observation: ${_line}")
      endif()
      set(_pc "${CMAKE_MATCH_1}")
      set(_row "${_function_name} ${CMAKE_MATCH_2} ${CMAKE_MATCH_3} ${CMAKE_MATCH_4} ${CMAKE_MATCH_5} ${CMAKE_MATCH_6} ${CMAKE_MATCH_7}")
      if(NOT _claim_text MATCHES "escape ${_program_hash} ${_function_index} ${_pc} obj ([^\n]+)")
        message(FATAL_ERROR "${_function_name}: no compiler claim for observed object at pc ${_pc}")
      endif()
      list(APPEND _object_total_unary_rows "${_row} ${CMAKE_MATCH_1}")
    elseif(_function_name MATCHES "^objectFrameStaticBinary(Released|Opaque|BigInt)$" AND _line MATCHES "^site ")
      if(NOT _line MATCHES "^site ([0-9]+) kind obj made ([0-9]+) confined ([0-9]+) escaped ([0-9]+) unresolved ([0-9]+) unchecked ([0-9]+) routes ([^ ]+)$")
        message(FATAL_ERROR "${_function_name}: unexpected static binary observation: ${_line}")
      endif()
      set(_pc "${CMAKE_MATCH_1}")
      set(_row "${_function_name} ${CMAKE_MATCH_2} ${CMAKE_MATCH_3} ${CMAKE_MATCH_4} ${CMAKE_MATCH_5} ${CMAKE_MATCH_6} ${CMAKE_MATCH_7}")
      if(NOT _claim_text MATCHES "escape ${_program_hash} ${_function_index} ${_pc} obj ([^\n]+)")
        message(FATAL_ERROR "${_function_name}: no compiler claim for observed object at pc ${_pc}")
      endif()
      list(APPEND _object_static_binary_rows "${_row} ${CMAKE_MATCH_1}")
    elseif(_function_name MATCHES "^objectFrameArithmeticUnary(Released|Saved|Opaque|BigInt)$" AND _line MATCHES "^site ")
      if(NOT _line MATCHES "^site ([0-9]+) kind obj made ([0-9]+) confined ([0-9]+) escaped ([0-9]+) unresolved ([0-9]+) unchecked ([0-9]+) routes ([^ ]+)$")
        message(FATAL_ERROR "${_function_name}: unexpected arithmetic unary observation: ${_line}")
      endif()
      set(_pc "${CMAKE_MATCH_1}")
      set(_row "${_function_name} ${CMAKE_MATCH_2} ${CMAKE_MATCH_3} ${CMAKE_MATCH_4} ${CMAKE_MATCH_5} ${CMAKE_MATCH_6} ${CMAKE_MATCH_7}")
      if(NOT _claim_text MATCHES "escape ${_program_hash} ${_function_index} ${_pc} obj ([^\n]+)")
        message(FATAL_ERROR "${_function_name}: no compiler claim for observed object at pc ${_pc}")
      endif()
      list(APPEND _object_arithmetic_unary_rows "${_row} ${CMAKE_MATCH_1}")
    elseif(_function_name MATCHES "^objectFrameLooseEquality(Released|Saved|Opaque|BigInt|Relational|Literal)$" AND _line MATCHES "^site ")
      if(NOT _line MATCHES "^site ([0-9]+) kind obj made ([0-9]+) confined ([0-9]+) escaped ([0-9]+) unresolved ([0-9]+) unchecked ([0-9]+) routes ([^ ]+)$")
        message(FATAL_ERROR "${_function_name}: unexpected loose equality observation: ${_line}")
      endif()
      set(_pc "${CMAKE_MATCH_1}")
      set(_row "${_function_name} ${CMAKE_MATCH_2} ${CMAKE_MATCH_3} ${CMAKE_MATCH_4} ${CMAKE_MATCH_5} ${CMAKE_MATCH_6} ${CMAKE_MATCH_7}")
      if(NOT _claim_text MATCHES "escape ${_program_hash} ${_function_index} ${_pc} obj ([^\n]+)")
        message(FATAL_ERROR "${_function_name}: no compiler claim for observed object at pc ${_pc}")
      endif()
      list(APPEND _object_loose_equality_rows "${_row} ${CMAKE_MATCH_1}")
    elseif(_function_name MATCHES "^objectFrameRelational(Saved|Unordered|Opaque|BigInt|Retained)$" AND _line MATCHES "^site ")
      if(NOT _line MATCHES "^site ([0-9]+) kind obj made ([0-9]+) confined ([0-9]+) escaped ([0-9]+) unresolved ([0-9]+) unchecked ([0-9]+) routes ([^ ]+)$")
        message(FATAL_ERROR "${_function_name}: unexpected relational observation: ${_line}")
      endif()
      set(_pc "${CMAKE_MATCH_1}")
      set(_row "${_function_name} ${CMAKE_MATCH_2} ${CMAKE_MATCH_3} ${CMAKE_MATCH_4} ${CMAKE_MATCH_5} ${CMAKE_MATCH_6} ${CMAKE_MATCH_7}")
      if(NOT _claim_text MATCHES "escape ${_program_hash} ${_function_index} ${_pc} obj ([^\n]+)")
        message(FATAL_ERROR "${_function_name}: no compiler claim for observed object at pc ${_pc}")
      endif()
      list(APPEND _object_relational_rows "${_row} ${CMAKE_MATCH_1}")
    elseif(_function_name MATCHES "^objectFrameArithmeticBinary(Saved|Numbers|Opaque|BigInt|Retained)$" AND _line MATCHES "^site ")
      if(NOT _line MATCHES "^site ([0-9]+) kind obj made ([0-9]+) confined ([0-9]+) escaped ([0-9]+) unresolved ([0-9]+) unchecked ([0-9]+) routes ([^ ]+)$")
        message(FATAL_ERROR "${_function_name}: unexpected arithmetic binary observation: ${_line}")
      endif()
      set(_pc "${CMAKE_MATCH_1}")
      set(_row "${_function_name} ${CMAKE_MATCH_2} ${CMAKE_MATCH_3} ${CMAKE_MATCH_4} ${CMAKE_MATCH_5} ${CMAKE_MATCH_6} ${CMAKE_MATCH_7}")
      if(NOT _claim_text MATCHES "escape ${_program_hash} ${_function_index} ${_pc} obj ([^\n]+)")
        message(FATAL_ERROR "${_function_name}: no compiler claim for observed object at pc ${_pc}")
      endif()
      list(APPEND _object_arithmetic_binary_rows "${_row} ${CMAKE_MATCH_1}")
    elseif(_function_name MATCHES "^objectFrameAddConcat(Saved|Numbers|Template|OpaqueAdd|OpaqueTemplate|BigInt|Retained)$" AND _line MATCHES "^site ")
      if(NOT _line MATCHES "^site ([0-9]+) kind obj made ([0-9]+) confined ([0-9]+) escaped ([0-9]+) unresolved ([0-9]+) unchecked ([0-9]+) routes ([^ ]+)$")
        message(FATAL_ERROR "${_function_name}: unexpected Add/Concat observation: ${_line}")
      endif()
      set(_pc "${CMAKE_MATCH_1}")
      set(_row "${_function_name} ${CMAKE_MATCH_2} ${CMAKE_MATCH_3} ${CMAKE_MATCH_4} ${CMAKE_MATCH_5} ${CMAKE_MATCH_6} ${CMAKE_MATCH_7}")
      if(NOT _claim_text MATCHES "escape ${_program_hash} ${_function_index} ${_pc} obj ([^\n]+)")
        message(FATAL_ERROR "${_function_name}: no compiler claim for observed object at pc ${_pc}")
      endif()
      list(APPEND _object_add_concat_rows "${_row} ${CMAKE_MATCH_1}")
    elseif(_function_name MATCHES "^objectFrameBigIntEquality(Saved|Opaque|Mixed|Retained)$" AND _line MATCHES "^site ")
      if(NOT _line MATCHES "^site ([0-9]+) kind obj made ([0-9]+) confined ([0-9]+) escaped ([0-9]+) unresolved ([0-9]+) unchecked ([0-9]+) routes ([^ ]+)$")
        message(FATAL_ERROR "${_function_name}: unexpected BigInt equality observation: ${_line}")
      endif()
      set(_pc "${CMAKE_MATCH_1}")
      set(_row "${_function_name} ${CMAKE_MATCH_2} ${CMAKE_MATCH_3} ${CMAKE_MATCH_4} ${CMAKE_MATCH_5} ${CMAKE_MATCH_6} ${CMAKE_MATCH_7}")
      if(NOT _claim_text MATCHES "escape ${_program_hash} ${_function_index} ${_pc} obj ([^\n]+)")
        message(FATAL_ERROR "${_function_name}: no compiler claim for observed object at pc ${_pc}")
      endif()
      list(APPEND _object_bigint_equality_rows "${_row} ${CMAKE_MATCH_1}")
    elseif(_function_name MATCHES "^objectFrameBigIntRelational(Saved|Opaque|Mixed|Computed|Retained)$" AND _line MATCHES "^site ")
      if(NOT _line MATCHES "^site ([0-9]+) kind obj made ([0-9]+) confined ([0-9]+) escaped ([0-9]+) unresolved ([0-9]+) unchecked ([0-9]+) routes ([^ ]+)$")
        message(FATAL_ERROR "${_function_name}: unexpected BigInt relational observation: ${_line}")
      endif()
      set(_pc "${CMAKE_MATCH_1}")
      set(_row "${_function_name} ${CMAKE_MATCH_2} ${CMAKE_MATCH_3} ${CMAKE_MATCH_4} ${CMAKE_MATCH_5} ${CMAKE_MATCH_6} ${CMAKE_MATCH_7}")
      if(NOT _claim_text MATCHES "escape ${_program_hash} ${_function_index} ${_pc} obj ([^\n]+)")
        message(FATAL_ERROR "${_function_name}: no compiler claim for observed object at pc ${_pc}")
      endif()
      list(APPEND _object_bigint_relational_rows "${_row} ${CMAKE_MATCH_1}")
    elseif(_function_name MATCHES "^objectFrameBigIntUnary(Saved|Paths|Opaque|Mixed|Retained)$" AND _line MATCHES "^site ")
      if(NOT _line MATCHES "^site ([0-9]+) kind obj made ([0-9]+) confined ([0-9]+) escaped ([0-9]+) unresolved ([0-9]+) unchecked ([0-9]+) routes ([^ ]+)$")
        message(FATAL_ERROR "${_function_name}: unexpected BigInt unary observation: ${_line}")
      endif()
      set(_pc "${CMAKE_MATCH_1}")
      set(_row "${_function_name} ${CMAKE_MATCH_2} ${CMAKE_MATCH_3} ${CMAKE_MATCH_4} ${CMAKE_MATCH_5} ${CMAKE_MATCH_6} ${CMAKE_MATCH_7}")
      if(NOT _claim_text MATCHES "escape ${_program_hash} ${_function_index} ${_pc} obj ([^\n]+)")
        message(FATAL_ERROR "${_function_name}: no compiler claim for observed object at pc ${_pc}")
      endif()
      list(APPEND _object_bigint_unary_rows "${_row} ${CMAKE_MATCH_1}")
    elseif(_function_name MATCHES "^objectFrameBigIntBinary(Saved|Paths|Opaque|Mixed|Retained)$" AND _line MATCHES "^site ")
      if(NOT _line MATCHES "^site ([0-9]+) kind obj made ([0-9]+) confined ([0-9]+) escaped ([0-9]+) unresolved ([0-9]+) unchecked ([0-9]+) routes ([^ ]+)$")
        message(FATAL_ERROR "${_function_name}: unexpected BigInt binary observation: ${_line}")
      endif()
      set(_pc "${CMAKE_MATCH_1}")
      set(_row "${_function_name} ${CMAKE_MATCH_2} ${CMAKE_MATCH_3} ${CMAKE_MATCH_4} ${CMAKE_MATCH_5} ${CMAKE_MATCH_6} ${CMAKE_MATCH_7}")
      if(NOT _claim_text MATCHES "escape ${_program_hash} ${_function_index} ${_pc} obj ([^\n]+)")
        message(FATAL_ERROR "${_function_name}: no compiler claim for observed object at pc ${_pc}")
      endif()
      list(APPEND _object_bigint_binary_rows "${_row} ${CMAKE_MATCH_1}")
    elseif(_function_name MATCHES "^objectFrameBigIntStatic(Saved|Paths|Opaque|Mixed|Shift|Retained)$" AND _line MATCHES "^site ")
      if(NOT _line MATCHES "^site ([0-9]+) kind obj made ([0-9]+) confined ([0-9]+) escaped ([0-9]+) unresolved ([0-9]+) unchecked ([0-9]+) routes ([^ ]+)$")
        message(FATAL_ERROR "${_function_name}: unexpected static BigInt observation: ${_line}")
      endif()
      set(_pc "${CMAKE_MATCH_1}")
      set(_row "${_function_name} ${CMAKE_MATCH_2} ${CMAKE_MATCH_3} ${CMAKE_MATCH_4} ${CMAKE_MATCH_5} ${CMAKE_MATCH_6} ${CMAKE_MATCH_7}")
      if(NOT _claim_text MATCHES "escape ${_program_hash} ${_function_index} ${_pc} obj ([^\n]+)")
        message(FATAL_ERROR "${_function_name}: no compiler claim for observed object at pc ${_pc}")
      endif()
      list(APPEND _object_bigint_static_rows "${_row} ${CMAKE_MATCH_1}")
    elseif(_function_name STREQUAL "objectFrameBigIntShiftEarly" AND _line MATCHES "^alloc ")
      if(NOT _line MATCHES "^alloc ([0-9]+) kind obj$")
        message(FATAL_ERROR "unexpected signed-shift literal allocation: ${_line}")
      endif()
      list(APPEND _object_bigint_shift_literal_pcs "${CMAKE_MATCH_1}")
    elseif(_function_name MATCHES "^objectFrameBigIntShift(Saved|Paths|Opaque|Mixed|Early|Retained)$" AND _line MATCHES "^site ")
      if(NOT _line MATCHES "^site ([0-9]+) kind obj made ([0-9]+) confined ([0-9]+) escaped ([0-9]+) unresolved ([0-9]+) unchecked ([0-9]+) routes ([^ ]+)$")
        message(FATAL_ERROR "${_function_name}: unexpected signed BigInt shift observation: ${_line}")
      endif()
      set(_pc "${CMAKE_MATCH_1}")
      set(_row "${_function_name} ${CMAKE_MATCH_2} ${CMAKE_MATCH_3} ${CMAKE_MATCH_4} ${CMAKE_MATCH_5} ${CMAKE_MATCH_6} ${CMAKE_MATCH_7}")
      if(_function_name STREQUAL "objectFrameBigIntShiftEarly" AND _pc STREQUAL "25")
        if(NOT _line STREQUAL "site 25 kind obj made 1 confined 0 escaped 1 unresolved 0 unchecked 0 routes thrown:1")
          message(FATAL_ERROR "the signed shift's independent Error was not retained through unwinding: ${_line}")
        endif()
        if(_claim_text MATCHES "escape ${_program_hash} ${_function_index} 25 [^\n]+")
          message(FATAL_ERROR "the implicit signed-shift Error acquired a source allocation claim")
        endif()
        list(APPEND _object_bigint_shift_error_rows "${_row} pc25 unclaimed")
      else()
        if(NOT _claim_text MATCHES "escape ${_program_hash} ${_function_index} ${_pc} obj ([^\n]+)")
          message(FATAL_ERROR "${_function_name}: no compiler claim for observed object at pc ${_pc}")
        endif()
        list(APPEND _object_bigint_shift_rows "${_row} ${CMAKE_MATCH_1}")
      endif()
    elseif(_function_name MATCHES "^objectFrameBigIntDivMod(Div|Mod)Early$" AND _line MATCHES "^alloc ")
      if(NOT _line MATCHES "^alloc ([0-9]+) kind obj$")
        message(FATAL_ERROR "${_function_name}: unexpected BigInt Div/Mod literal allocation: ${_line}")
      endif()
      list(APPEND _object_bigint_divmod_literal_pcs "${_function_name} ${CMAKE_MATCH_1}")
    elseif(_function_name MATCHES "^objectFrameBigIntDivMod(Saved|Paths|Opaque|Mixed|DivEarly|ModEarly|Retained)$" AND _line MATCHES "^site ")
      if(NOT _line MATCHES "^site ([0-9]+) kind obj made ([0-9]+) confined ([0-9]+) escaped ([0-9]+) unresolved ([0-9]+) unchecked ([0-9]+) routes ([^ ]+)$")
        message(FATAL_ERROR "${_function_name}: unexpected BigInt Div/Mod observation: ${_line}")
      endif()
      set(_pc "${CMAKE_MATCH_1}")
      set(_row "${_function_name} ${CMAKE_MATCH_2} ${CMAKE_MATCH_3} ${CMAKE_MATCH_4} ${CMAKE_MATCH_5} ${CMAKE_MATCH_6} ${CMAKE_MATCH_7}")
      if(_function_name MATCHES "^objectFrameBigIntDivMod(Div|Mod)Early$" AND _pc STREQUAL "25")
        if(NOT _line STREQUAL "site 25 kind obj made 1 confined 0 escaped 1 unresolved 0 unchecked 0 routes thrown:1")
          message(FATAL_ERROR "${_function_name}: the independent Error was not retained through unwinding: ${_line}")
        endif()
        if(_claim_text MATCHES "escape ${_program_hash} ${_function_index} 25 [^\n]+")
          message(FATAL_ERROR "${_function_name}: the implicit BigInt Div/Mod Error acquired a source allocation claim")
        endif()
        list(APPEND _object_bigint_divmod_error_rows "${_row} pc25 unclaimed")
      else()
        if(NOT _claim_text MATCHES "escape ${_program_hash} ${_function_index} ${_pc} obj ([^\n]+)")
          message(FATAL_ERROR "${_function_name}: no compiler claim for observed object at pc ${_pc}")
        endif()
        list(APPEND _object_bigint_divmod_rows "${_row} ${CMAKE_MATCH_1}")
      endif()
    endif()
  endforeach()
  set(_expected_publication_rows
      "globalAliasJoin 2 1 1 0 0 globals:1 escapes:stored_global"
      "globalAliasLoop 2 1 1 0 0 globals:1 escapes:stored_global"
      "globalRetainedContainer 1 0 1 0 0 globals:1 escapes:stored"
      "globalRetainedContainer 1 0 1 0 0 globals:1 escapes:stored_global"
      "globalReplacedPublication 1 0 1 0 0 globals:1 escapes:stored_global")
  list(SORT _publication_rows)
  list(SORT _expected_publication_rows)
  if(NOT _publication_rows STREQUAL _expected_publication_rows)
    message(FATAL_ERROR "global publication evidence mismatch:\nexpected: ${_expected_publication_rows}\nobserved: ${_publication_rows}")
  endif()
  message(STATUS "global publication: five sites, seven objects, five retained through globals; live claims agree")

  # Both source and packing arrays are confined, but their retained object
  # elements are not. The spread receiver is an independent Passed sink.
  # Construction also allocates an instance and its lazily created prototype
  # at one unclaimed runtime site: one confined and one retained through the
  # constructor's global closure. Neither is an object-literal claim.
  set(_expected_spread_rows
      "spreadRetainedCall obj 1 0 1 0 0 globals:1 escapes:stored"
      "spreadRetainedCall arr 1 1 0 0 0 - confined"
      "spreadRetainedCall arr 1 1 0 0 0 - confined"
      "spreadRetainedConstruct obj 1 0 1 0 0 globals:1 escapes:stored"
      "spreadRetainedConstruct arr 1 1 0 0 0 - confined"
      "spreadRetainedConstruct arr 1 1 0 0 0 - confined"
      "spreadRetainedConstruct obj 2 1 1 0 0 globals:1 unclaimed"
      "spreadRetainedReceiver obj 1 0 1 0 0 globals:1 escapes:passed"
      "spreadRetainedReceiver arr 1 1 0 0 0 - confined"
      "spreadRetainedReceiver arr 1 1 0 0 0 - confined")
  list(SORT _spread_rows)
  list(SORT _expected_spread_rows)
  if(NOT _spread_rows STREQUAL _expected_spread_rows)
    message(FATAL_ERROR "spread argument evidence mismatch:\nexpected: ${_expected_spread_rows}\nobserved: ${_spread_rows}")
  endif()
  message(STATUS "spread arguments: six confined arrays, three retained literal objects, one unclaimed constructor site; live claims agree")

  # These claims come from raw imported JavaScript, including frame_enter and
  # frame_exit. Pin the precision witness as well as the retaining controls:
  # returning a loaded child after overwrite keeps the OLD child reachable;
  # a loaded array alias mutates the SAME array. Cycles stay conservative.
  set(_expected_array_frame_rows
      "arrayFramePrivate obj 2 2 0 0 0 - confined"
      "arrayFramePrivate arr 2 2 0 0 0 - confined"
      "arrayFrameReturned obj 1 0 1 0 0 temporaries:1 escapes:stored"
      "arrayFrameReturned arr 1 0 1 0 0 temporaries:1 escapes:returned"
      "arrayFrameSavedRead obj 1 0 1 0 0 temporaries:1 escapes:stored"
      "arrayFrameSavedRead obj 1 1 0 0 0 - confined"
      "arrayFrameSavedRead arr 1 1 0 0 0 - confined"
      "arrayFrameOverwrite obj 1 1 0 0 0 - confined"
      "arrayFrameOverwrite obj 1 0 1 0 0 temporaries:1 escapes:stored"
      "arrayFrameOverwrite arr 1 0 1 0 0 temporaries:1 escapes:returned"
      "arrayFrameLoadedAlias obj 1 1 0 0 0 - confined"
      "arrayFrameLoadedAlias arr 1 0 1 0 0 temporaries:1 escapes:stored"
      "arrayFrameLoadedAlias arr 1 0 1 0 0 temporaries:1 escapes:returned"
      "arrayFramePublished obj 1 0 1 0 0 globals:1 escapes:stored"
      "arrayFramePublished arr 1 0 1 0 0 globals:1 escapes:stored_global"
      "arrayFrameCall obj 1 0 1 0 0 globals:1 escapes:stored"
      "arrayFrameCall arr 1 0 1 0 0 globals:1 escapes:passed"
      "arrayFrameCycle arr 1 1 0 0 0 - escapes:stored"
      "arrayFrameTransientCycle arr 1 1 0 0 0 - escapes:stored")
  list(SORT _array_frame_rows)
  list(SORT _expected_array_frame_rows)
  if(NOT _array_frame_rows STREQUAL _expected_array_frame_rows)
    message(FATAL_ERROR "imported array-frame evidence mismatch:\nexpected: ${_expected_array_frame_rows}\nobserved: ${_array_frame_rows}")
  endif()
  message(STATUS "imported array frames: nineteen sites, twenty-one instances, eleven retained; live claims agree")

  # Deletion's precision witness must be executed and independently claimed:
  # the returned object loses its child, but a saved read retains that child.
  # A deleted self-cycle remains Stored despite observed confinement.
  set(_expected_object_deletion_rows
      "objectFrameDeletedChild 1 1 0 0 0 - confined"
      "objectFrameDeletedChild 1 0 1 0 0 temporaries:1 escapes:returned"
      "objectFrameDeletedSavedRead 1 0 1 0 0 temporaries:1 escapes:stored"
      "objectFrameDeletedSavedRead 1 1 0 0 0 - confined"
      "objectFrameDeletedTransientCycle 1 1 0 0 0 - escapes:stored")
  list(SORT _object_deletion_rows)
  list(SORT _expected_object_deletion_rows)
  if(NOT _object_deletion_rows STREQUAL _expected_object_deletion_rows)
    message(FATAL_ERROR "imported object deletion evidence mismatch:\nexpected: ${_expected_object_deletion_rows}\nobserved: ${_object_deletion_rows}")
  endif()
  message(STATUS "imported object deletion: five sites, five instances, two retained, one conservative cycle; live claims agree")

  # The copy itself does not retain its source object. Its copied child stays
  # reachable through a returned target until overwritten, or through a saved
  # read even after both the original and copied fields are deleted.
  set(_expected_object_copy_rows
      "objectFrameCopiedChild 1 0 1 0 0 temporaries:1 escapes:stored"
      "objectFrameCopiedChild 1 1 0 0 0 - confined"
      "objectFrameCopiedChild 1 0 1 0 0 temporaries:1 escapes:returned"
      "objectFrameCopiedOverwrite 1 1 0 0 0 - confined"
      "objectFrameCopiedOverwrite 1 0 1 0 0 temporaries:1 escapes:stored"
      "objectFrameCopiedOverwrite 1 1 0 0 0 - confined"
      "objectFrameCopiedOverwrite 1 0 1 0 0 temporaries:1 escapes:returned"
      "objectFrameCopiedSavedRead 1 0 1 0 0 temporaries:1 escapes:stored"
      "objectFrameCopiedSavedRead 1 1 0 0 0 - confined"
      "objectFrameCopiedSavedRead 1 1 0 0 0 - confined")
  list(SORT _object_copy_rows)
  list(SORT _expected_object_copy_rows)
  if(NOT _object_copy_rows STREQUAL _expected_object_copy_rows)
    message(FATAL_ERROR "imported object copy evidence mismatch:\nexpected: ${_expected_object_copy_rows}\nobserved: ${_object_copy_rows}")
  endif()
  message(STATUS "imported object copy: ten sites, ten instances, five retained; live claims agree")

  # Dynamic flags carry opaque raw parameter identities separately from known
  # contents. Only the alias case's deleted replacement gains confinement;
  # the other object's child stays retained on both calls. Strict source switch
  # selectors are supported, but every Stored site retains an escaping arm.
  # The literal conditional independently checks its old child's confinement.
  set(_expected_object_copy_path_rows
      "objectFrameCopiedConditionalAlias 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameCopiedConditionalAlias 2 2 0 0 0 - confined"
      "objectFrameCopiedConditionalAlias 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameCopiedConditionalAlias 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameCopiedConditionalAlias 2 0 2 0 0 temporaries:2 escapes:returned"
      "objectFrameCopiedConditionalSource 2 1 1 0 0 temporaries:1 escapes:stored"
      "objectFrameCopiedConditionalSource 2 1 1 0 0 temporaries:1 escapes:stored"
      "objectFrameCopiedConditionalSource 2 2 0 0 0 - confined"
      "objectFrameCopiedConditionalSource 2 2 0 0 0 - confined"
      "objectFrameCopiedConditionalSource 2 0 2 0 0 temporaries:2 escapes:returned"
      "objectFrameCopiedSwitchSaved 3 0 3 0 0 temporaries:3 escapes:stored"
      "objectFrameCopiedSwitchSaved 3 1 2 0 0 temporaries:2 escapes:stored"
      "objectFrameCopiedSwitchSaved 3 2 1 0 0 temporaries:1 escapes:stored"
      "objectFrameCopiedSwitchSaved 3 2 1 0 0 temporaries:1 escapes:stored"
      "objectFrameCopiedSwitchSaved 1 0 1 0 0 temporaries:1 escapes:returned"
      "objectFrameCopiedSwitchSaved 1 0 1 0 0 temporaries:1 escapes:returned"
      "objectFrameCopiedSwitchSaved 1 0 1 0 0 temporaries:1 escapes:returned"
      "objectFrameCopiedLiteralOverwrite 1 1 0 0 0 - confined"
      "objectFrameCopiedLiteralOverwrite 1 0 1 0 0 temporaries:1 escapes:stored"
      "objectFrameCopiedLiteralOverwrite 1 1 0 0 0 - confined"
      "objectFrameCopiedLiteralOverwrite 1 1 0 0 0 - confined")
  list(SORT _object_copy_path_rows)
  list(SORT _expected_object_copy_path_rows)
  if(NOT _object_copy_path_rows STREQUAL _expected_object_copy_path_rows)
    message(FATAL_ERROR "imported object copy path evidence mismatch:\nexpected: ${_expected_object_copy_path_rows}\nobserved: ${_object_copy_path_rows}")
  endif()
  message(STATUS "imported object copy paths: twenty-one sites, thirty-nine instances, twenty-three retained; live claims agree")

  # A separate source switch releases its child on every case/default path.
  # The source/target containers each remain retained on their returning arm;
  # the String default input distinguishes strict equality from coercion.
  set(_expected_object_switch_selector_rows
      "objectFrameSwitchReleased 3 3 0 0 0 - confined"
      "objectFrameSwitchReleased 3 2 1 0 0 temporaries:1 escapes:returned"
      "objectFrameSwitchReleased 3 2 1 0 0 temporaries:1 escapes:returned"
      "objectFrameSwitchReleased 1 0 1 0 0 temporaries:1 escapes:returned")
  list(SORT _object_switch_selector_rows)
  list(SORT _expected_object_switch_selector_rows)
  if(NOT _object_switch_selector_rows STREQUAL _expected_object_switch_selector_rows)
    message(FATAL_ERROR "imported source switch selector evidence mismatch:\nexpected: ${_expected_object_switch_selector_rows}\nobserved: ${_object_switch_selector_rows}")
  endif()
  message(STATUS "imported source switch selectors: four sites, ten instances, three retained; live claims agree")

  # Every negation input executes, including empty and nonempty Strings. The
  # returned graph preserves both container identities and the chosen alias,
  # while its original child is absent from every structural path.
  set(_expected_object_negation_rows
      "objectFrameNegatedReleased 4 4 0 0 0 - confined"
      "objectFrameNegatedReleased 4 0 4 0 0 temporaries:4 escapes:stored"
      "objectFrameNegatedReleased 4 0 4 0 0 temporaries:4 escapes:stored"
      "objectFrameNegatedReleased 4 0 4 0 0 temporaries:4 escapes:returned")
  list(SORT _object_negation_rows)
  list(SORT _expected_object_negation_rows)
  if(NOT _object_negation_rows STREQUAL _expected_object_negation_rows)
    message(FATAL_ERROR "imported logical negation evidence mismatch:\nexpected: ${_expected_object_negation_rows}\nobserved: ${_object_negation_rows}")
  endif()
  message(STATUS "imported logical negation: four sites, sixteen instances, twelve retained; live claims agree")

  # Primitive String/Undefined results retain neither inspected child. The
  # source void case also keeps its already-evaluated assignment observable;
  # its raw import is constant Undefined, independently of ctjs.unary Void.
  set(_expected_object_total_unary_rows
      "objectFrameTypeofReleased 5 5 0 0 0 - confined"
      "objectFrameTypeofReleased 5 0 5 0 0 temporaries:5 escapes:stored"
      "objectFrameTypeofReleased 5 0 5 0 0 temporaries:5 escapes:stored"
      "objectFrameTypeofReleased 5 0 5 0 0 temporaries:5 escapes:returned"
      "objectFrameVoidReleased 2 2 0 0 0 - confined"
      "objectFrameVoidReleased 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameVoidReleased 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameVoidReleased 2 0 2 0 0 temporaries:2 escapes:returned")
  list(SORT _object_total_unary_rows)
  list(SORT _expected_object_total_unary_rows)
  if(NOT _object_total_unary_rows STREQUAL _expected_object_total_unary_rows)
    message(FATAL_ERROR "imported total unary evidence mismatch:\nexpected: ${_expected_object_total_unary_rows}\nobserved: ${_object_total_unary_rows}")
  endif()
  message(STATUS "imported typeof/void: eight sites, twenty-eight instances, twenty-one retained; live claims agree")

  # Number and BigInt results each require their independent original category.
  # The historical literal BigInt pair now has its own category proof; opaque
  # runtime Number observations still cannot authorize retention refinement.
  set(_expected_object_static_binary_rows
      "objectFrameStaticBinaryReleased 2 2 0 0 0 - confined"
      "objectFrameStaticBinaryReleased 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameStaticBinaryReleased 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameStaticBinaryReleased 2 0 2 0 0 temporaries:2 escapes:returned"
      "objectFrameStaticBinaryOpaque 2 2 0 0 0 - escapes:stored"
      "objectFrameStaticBinaryOpaque 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameStaticBinaryOpaque 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameStaticBinaryOpaque 2 0 2 0 0 temporaries:2 escapes:returned"
      "objectFrameStaticBinaryBigInt 1 1 0 0 0 - confined"
      "objectFrameStaticBinaryBigInt 1 0 1 0 0 temporaries:1 escapes:stored"
      "objectFrameStaticBinaryBigInt 1 0 1 0 0 temporaries:1 escapes:stored"
      "objectFrameStaticBinaryBigInt 1 0 1 0 0 temporaries:1 escapes:returned")
  list(SORT _object_static_binary_rows)
  list(SORT _expected_object_static_binary_rows)
  if(NOT _object_static_binary_rows STREQUAL _expected_object_static_binary_rows)
    message(FATAL_ERROR "imported static binary evidence mismatch:\nexpected: ${_expected_object_static_binary_rows}\nobserved: ${_object_static_binary_rows}")
  endif()
  message(STATUS "imported static binary: twelve sites, twenty instances, fifteen retained; live claims agree")

  # Saved primitive origins survive a BigInt field overwrite. Original BigInt
  # Neg/BitNot results now retain a separately proved category; opaque inputs
  # cannot prove every future numeric conversion safe.
  set(_expected_object_arithmetic_unary_rows
      "objectFrameArithmeticUnaryReleased 2 2 0 0 0 - confined"
      "objectFrameArithmeticUnaryReleased 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameArithmeticUnaryReleased 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameArithmeticUnaryReleased 2 0 2 0 0 temporaries:2 escapes:returned"
      "objectFrameArithmeticUnarySaved 2 2 0 0 0 - confined"
      "objectFrameArithmeticUnarySaved 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameArithmeticUnarySaved 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameArithmeticUnarySaved 2 0 2 0 0 temporaries:2 escapes:returned"
      "objectFrameArithmeticUnaryOpaque 2 2 0 0 0 - escapes:stored"
      "objectFrameArithmeticUnaryOpaque 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameArithmeticUnaryOpaque 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameArithmeticUnaryOpaque 2 0 2 0 0 temporaries:2 escapes:returned"
      "objectFrameArithmeticUnaryBigInt 1 1 0 0 0 - confined"
      "objectFrameArithmeticUnaryBigInt 1 0 1 0 0 temporaries:1 escapes:stored"
      "objectFrameArithmeticUnaryBigInt 1 0 1 0 0 temporaries:1 escapes:stored"
      "objectFrameArithmeticUnaryBigInt 1 0 1 0 0 temporaries:1 escapes:returned")
  list(SORT _object_arithmetic_unary_rows)
  list(SORT _expected_object_arithmetic_unary_rows)
  if(NOT _object_arithmetic_unary_rows STREQUAL _expected_object_arithmetic_unary_rows)
    message(FATAL_ERROR "imported arithmetic unary evidence mismatch:\nexpected: ${_expected_object_arithmetic_unary_rows}\nobserved: ${_object_arithmetic_unary_rows}")
  endif()
  message(STATUS "imported arithmetic unary: sixteen sites, twenty-eight instances, twenty-one retained; live claims agree")

  # Both saved and structural operand origins must independently be primitive.
  # The original Released witness loads global undefined; Literal changes only
  # that input to void 0. Global/opaque/BigInt observations keep Stored claims.
  # The unchanged relational source now has an independent retention proof;
  # concrete successes alone do not replace either producer proof.
  set(_expected_object_loose_equality_rows
      "objectFrameLooseEqualityReleased 2 2 0 0 0 - escapes:stored"
      "objectFrameLooseEqualityReleased 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameLooseEqualityReleased 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameLooseEqualityReleased 2 0 2 0 0 temporaries:2 escapes:returned"
      "objectFrameLooseEqualitySaved 2 2 0 0 0 - confined"
      "objectFrameLooseEqualitySaved 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameLooseEqualitySaved 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameLooseEqualitySaved 2 0 2 0 0 temporaries:2 escapes:returned"
      "objectFrameLooseEqualityOpaque 2 2 0 0 0 - escapes:stored"
      "objectFrameLooseEqualityOpaque 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameLooseEqualityOpaque 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameLooseEqualityOpaque 2 0 2 0 0 temporaries:2 escapes:returned"
      "objectFrameLooseEqualityBigInt 1 1 0 0 0 - escapes:stored"
      "objectFrameLooseEqualityBigInt 1 0 1 0 0 temporaries:1 escapes:stored"
      "objectFrameLooseEqualityBigInt 1 0 1 0 0 temporaries:1 escapes:stored"
      "objectFrameLooseEqualityBigInt 1 0 1 0 0 temporaries:1 escapes:returned"
      "objectFrameLooseEqualityRelational 2 2 0 0 0 - confined"
      "objectFrameLooseEqualityRelational 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameLooseEqualityRelational 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameLooseEqualityRelational 2 0 2 0 0 temporaries:2 escapes:returned"
      "objectFrameLooseEqualityLiteral 2 2 0 0 0 - confined"
      "objectFrameLooseEqualityLiteral 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameLooseEqualityLiteral 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameLooseEqualityLiteral 2 0 2 0 0 temporaries:2 escapes:returned")
  list(SORT _object_loose_equality_rows)
  list(SORT _expected_object_loose_equality_rows)
  if(NOT _object_loose_equality_rows STREQUAL _expected_object_loose_equality_rows)
    message(FATAL_ERROR "imported loose equality evidence mismatch:\nexpected: ${_expected_object_loose_equality_rows}\nobserved: ${_object_loose_equality_rows}")
  endif()
  message(STATUS "imported loose equality: twenty-four sites, forty-four instances, thirty-three retained; live claims agree")

  # Saved primitive values survive later BigInt field replacement/deletion;
  # invalid numeric Strings and Undefined give unordered comparisons. Opaque
  # inputs keep Stored refusals; the exact BigInt pair now releases its child.
  # Returning the old child retains it after both container fields are deleted.
  set(_expected_object_relational_rows
      "objectFrameRelationalSaved 2 2 0 0 0 - confined"
      "objectFrameRelationalSaved 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameRelationalSaved 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameRelationalSaved 2 0 2 0 0 temporaries:2 escapes:returned"
      "objectFrameRelationalUnordered 2 2 0 0 0 - confined"
      "objectFrameRelationalUnordered 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameRelationalUnordered 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameRelationalUnordered 2 0 2 0 0 temporaries:2 escapes:returned"
      "objectFrameRelationalOpaque 2 2 0 0 0 - escapes:stored"
      "objectFrameRelationalOpaque 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameRelationalOpaque 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameRelationalOpaque 2 0 2 0 0 temporaries:2 escapes:returned"
      "objectFrameRelationalBigInt 2 2 0 0 0 - confined"
      "objectFrameRelationalBigInt 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameRelationalBigInt 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameRelationalBigInt 2 0 2 0 0 temporaries:2 escapes:returned"
      "objectFrameRelationalRetained 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameRelationalRetained 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameRelationalRetained 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameRelationalRetained 2 0 2 0 0 temporaries:2 escapes:returned")
  list(SORT _object_relational_rows)
  list(SORT _expected_object_relational_rows)
  if(NOT _object_relational_rows STREQUAL _expected_object_relational_rows)
    message(FATAL_ERROR "imported relational evidence mismatch:\nexpected: ${_expected_object_relational_rows}\nobserved: ${_object_relational_rows}")
  endif()
  message(STATUS "imported relational comparisons: twenty sites, forty instances, thirty-two retained; live claims agree")

  # Dynamic numeric arithmetic needs both original primitive non-BigInt inputs.
  # Opaque/BigInt controls keep Stored; the saved child retains its own identity.
  set(_expected_object_arithmetic_binary_rows
      "objectFrameArithmeticBinarySaved 2 2 0 0 0 - confined"
      "objectFrameArithmeticBinarySaved 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameArithmeticBinarySaved 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameArithmeticBinarySaved 2 0 2 0 0 temporaries:2 escapes:returned"
      "objectFrameArithmeticBinaryNumbers 2 2 0 0 0 - confined"
      "objectFrameArithmeticBinaryNumbers 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameArithmeticBinaryNumbers 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameArithmeticBinaryNumbers 2 0 2 0 0 temporaries:2 escapes:returned"
      "objectFrameArithmeticBinaryOpaque 2 2 0 0 0 - escapes:stored"
      "objectFrameArithmeticBinaryOpaque 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameArithmeticBinaryOpaque 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameArithmeticBinaryOpaque 2 0 2 0 0 temporaries:2 escapes:returned"
      "objectFrameArithmeticBinaryBigInt 2 2 0 0 0 - escapes:stored"
      "objectFrameArithmeticBinaryBigInt 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameArithmeticBinaryBigInt 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameArithmeticBinaryBigInt 2 0 2 0 0 temporaries:2 escapes:returned"
      "objectFrameArithmeticBinaryRetained 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameArithmeticBinaryRetained 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameArithmeticBinaryRetained 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameArithmeticBinaryRetained 2 0 2 0 0 temporaries:2 escapes:returned")
  list(SORT _object_arithmetic_binary_rows)
  list(SORT _expected_object_arithmetic_binary_rows)
  if(NOT _object_arithmetic_binary_rows STREQUAL _expected_object_arithmetic_binary_rows)
    message(FATAL_ERROR "imported arithmetic binary evidence mismatch:\nexpected: ${_expected_object_arithmetic_binary_rows}\nobserved: ${_object_arithmetic_binary_rows}")
  endif()
  message(STATUS "imported arithmetic binary: twenty sites, forty instances, thirty-two retained; live claims agree")

  # Number/String addition and template conversion require original primitives.
  # Separate opaque Add/Concat and successful BigInt controls stay conservative;
  # an independently returned child remains retained despite scalar production.
  set(_expected_object_add_concat_rows
      "objectFrameAddConcatSaved 2 2 0 0 0 - confined"
      "objectFrameAddConcatSaved 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameAddConcatSaved 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameAddConcatSaved 2 0 2 0 0 temporaries:2 escapes:returned"
      "objectFrameAddConcatNumbers 2 2 0 0 0 - confined"
      "objectFrameAddConcatNumbers 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameAddConcatNumbers 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameAddConcatNumbers 2 0 2 0 0 temporaries:2 escapes:returned"
      "objectFrameAddConcatTemplate 2 2 0 0 0 - confined"
      "objectFrameAddConcatTemplate 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameAddConcatTemplate 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameAddConcatTemplate 2 0 2 0 0 temporaries:2 escapes:returned"
      "objectFrameAddConcatOpaqueAdd 2 2 0 0 0 - escapes:stored"
      "objectFrameAddConcatOpaqueAdd 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameAddConcatOpaqueAdd 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameAddConcatOpaqueAdd 2 0 2 0 0 temporaries:2 escapes:returned"
      "objectFrameAddConcatOpaqueTemplate 2 2 0 0 0 - escapes:stored"
      "objectFrameAddConcatOpaqueTemplate 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameAddConcatOpaqueTemplate 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameAddConcatOpaqueTemplate 2 0 2 0 0 temporaries:2 escapes:returned"
      "objectFrameAddConcatBigInt 2 2 0 0 0 - escapes:stored"
      "objectFrameAddConcatBigInt 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameAddConcatBigInt 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameAddConcatBigInt 2 0 2 0 0 temporaries:2 escapes:returned"
      "objectFrameAddConcatRetained 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameAddConcatRetained 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameAddConcatRetained 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameAddConcatRetained 2 0 2 0 0 temporaries:2 escapes:returned")
  list(SORT _object_add_concat_rows)
  list(SORT _expected_object_add_concat_rows)
  if(NOT _object_add_concat_rows STREQUAL _expected_object_add_concat_rows)
    message(FATAL_ERROR "imported Add/Concat evidence mismatch:\nexpected: ${_expected_object_add_concat_rows}\nobserved: ${_object_add_concat_rows}")
  endif()
  message(STATUS "imported Add/Concat: twenty-eight sites, fifty-six instances, forty-four retained; live claims agree")

  # Exact saved BigInt pairs compare without user conversion. Concrete opaque
  # actuals and mixed Number/BigInt inputs retain Stored; a separately returned
  # child remains reachable even after both owning fields have been deleted.
  set(_expected_object_bigint_equality_rows
      "objectFrameBigIntEqualitySaved 2 2 0 0 0 - confined"
      "objectFrameBigIntEqualitySaved 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntEqualitySaved 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntEqualitySaved 2 0 2 0 0 temporaries:2 escapes:returned"
      "objectFrameBigIntEqualityOpaque 2 2 0 0 0 - escapes:stored"
      "objectFrameBigIntEqualityOpaque 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntEqualityOpaque 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntEqualityOpaque 2 0 2 0 0 temporaries:2 escapes:returned"
      "objectFrameBigIntEqualityMixed 2 2 0 0 0 - escapes:stored"
      "objectFrameBigIntEqualityMixed 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntEqualityMixed 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntEqualityMixed 2 0 2 0 0 temporaries:2 escapes:returned"
      "objectFrameBigIntEqualityRetained 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntEqualityRetained 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntEqualityRetained 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntEqualityRetained 2 0 2 0 0 temporaries:2 escapes:returned")
  list(SORT _object_bigint_equality_rows)
  list(SORT _expected_object_bigint_equality_rows)
  if(NOT _object_bigint_equality_rows STREQUAL _expected_object_bigint_equality_rows)
    message(FATAL_ERROR "imported BigInt equality evidence mismatch:\nexpected: ${_expected_object_bigint_equality_rows}\nobserved: ${_object_bigint_equality_rows}")
  endif()
  message(STATUS "imported BigInt equality: sixteen sites, thirty-two instances, twenty-six retained; live claims agree")

  # Four exact relational kinds share original BigInt-pair provenance only.
  # Opaque and mixed inputs keep Stored; proved computed Add inputs now qualify.
  # Independently saved children
  # remain retained. Historical source bytes and observation counts are unchanged.
  set(_expected_object_bigint_relational_rows
      "objectFrameBigIntRelationalSaved 2 2 0 0 0 - confined"
      "objectFrameBigIntRelationalSaved 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntRelationalSaved 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntRelationalSaved 2 0 2 0 0 temporaries:2 escapes:returned"
      "objectFrameBigIntRelationalOpaque 2 2 0 0 0 - escapes:stored"
      "objectFrameBigIntRelationalOpaque 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntRelationalOpaque 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntRelationalOpaque 2 0 2 0 0 temporaries:2 escapes:returned"
      "objectFrameBigIntRelationalMixed 2 2 0 0 0 - escapes:stored"
      "objectFrameBigIntRelationalMixed 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntRelationalMixed 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntRelationalMixed 2 0 2 0 0 temporaries:2 escapes:returned"
      "objectFrameBigIntRelationalComputed 2 2 0 0 0 - confined"
      "objectFrameBigIntRelationalComputed 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntRelationalComputed 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntRelationalComputed 2 0 2 0 0 temporaries:2 escapes:returned"
      "objectFrameBigIntRelationalRetained 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntRelationalRetained 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntRelationalRetained 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntRelationalRetained 2 0 2 0 0 temporaries:2 escapes:returned")
  list(SORT _object_bigint_relational_rows)
  list(SORT _expected_object_bigint_relational_rows)
  if(NOT _object_bigint_relational_rows STREQUAL _expected_object_bigint_relational_rows)
    message(FATAL_ERROR "imported BigInt relational evidence mismatch:\nexpected: ${_expected_object_bigint_relational_rows}\nobserved: ${_object_bigint_relational_rows}")
  endif()
  message(STATUS "imported BigInt relational: twenty sites, forty instances, thirty-two retained; live claims agree")

  # Computed BigInt unary results retain exact per-path original categories.
  # Opaque inputs and mixed comparisons still refuse; saved children stay live.
  set(_expected_object_bigint_unary_rows
      "objectFrameBigIntUnarySaved 2 2 0 0 0 - confined"
      "objectFrameBigIntUnarySaved 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntUnarySaved 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntUnarySaved 2 0 2 0 0 temporaries:2 escapes:returned"
      "objectFrameBigIntUnaryPaths 2 2 0 0 0 - confined"
      "objectFrameBigIntUnaryPaths 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntUnaryPaths 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntUnaryPaths 2 0 2 0 0 temporaries:2 escapes:returned"
      "objectFrameBigIntUnaryOpaque 2 2 0 0 0 - escapes:stored"
      "objectFrameBigIntUnaryOpaque 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntUnaryOpaque 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntUnaryOpaque 2 0 2 0 0 temporaries:2 escapes:returned"
      "objectFrameBigIntUnaryMixed 2 2 0 0 0 - escapes:stored"
      "objectFrameBigIntUnaryMixed 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntUnaryMixed 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntUnaryMixed 2 0 2 0 0 temporaries:2 escapes:returned"
      "objectFrameBigIntUnaryRetained 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntUnaryRetained 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntUnaryRetained 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntUnaryRetained 2 0 2 0 0 temporaries:2 escapes:returned")
  list(SORT _object_bigint_unary_rows)
  list(SORT _expected_object_bigint_unary_rows)
  if(NOT _object_bigint_unary_rows STREQUAL _expected_object_bigint_unary_rows)
    message(FATAL_ERROR "imported BigInt unary evidence mismatch:\nexpected: ${_expected_object_bigint_unary_rows}\nobserved: ${_object_bigint_unary_rows}")
  endif()
  message(STATUS "imported BigInt unary: twenty sites, forty instances, thirty-two retained; live claims agree")

  # Computed BigInt binary results retain exact per-path original categories.
  # Opaque inputs and mixed comparisons still refuse; saved children stay live.
  set(_expected_object_bigint_binary_rows
      "objectFrameBigIntBinarySaved 2 2 0 0 0 - confined"
      "objectFrameBigIntBinarySaved 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntBinarySaved 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntBinarySaved 2 0 2 0 0 temporaries:2 escapes:returned"
      "objectFrameBigIntBinaryPaths 2 2 0 0 0 - confined"
      "objectFrameBigIntBinaryPaths 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntBinaryPaths 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntBinaryPaths 2 0 2 0 0 temporaries:2 escapes:returned"
      "objectFrameBigIntBinaryOpaque 2 2 0 0 0 - escapes:stored"
      "objectFrameBigIntBinaryOpaque 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntBinaryOpaque 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntBinaryOpaque 2 0 2 0 0 temporaries:2 escapes:returned"
      "objectFrameBigIntBinaryMixed 2 2 0 0 0 - escapes:stored"
      "objectFrameBigIntBinaryMixed 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntBinaryMixed 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntBinaryMixed 2 0 2 0 0 temporaries:2 escapes:returned"
      "objectFrameBigIntBinaryRetained 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntBinaryRetained 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntBinaryRetained 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntBinaryRetained 2 0 2 0 0 temporaries:2 escapes:returned")
  list(SORT _object_bigint_binary_rows)
  list(SORT _expected_object_bigint_binary_rows)
  if(NOT _object_bigint_binary_rows STREQUAL _expected_object_bigint_binary_rows)
    message(FATAL_ERROR "imported BigInt binary evidence mismatch:\nexpected: ${_expected_object_bigint_binary_rows}\nobserved: ${_object_bigint_binary_rows}")
  endif()
  message(STATUS "imported BigInt binary: twenty sites, forty instances, thirty-two retained; live claims agree")

  set(_expected_object_bigint_static_rows
      "objectFrameBigIntStaticSaved 2 2 0 0 0 - confined"
      "objectFrameBigIntStaticSaved 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntStaticSaved 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntStaticSaved 2 0 2 0 0 temporaries:2 escapes:returned"
      "objectFrameBigIntStaticPaths 2 2 0 0 0 - confined"
      "objectFrameBigIntStaticPaths 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntStaticPaths 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntStaticPaths 2 0 2 0 0 temporaries:2 escapes:returned"
      "objectFrameBigIntStaticOpaque 2 2 0 0 0 - escapes:stored"
      "objectFrameBigIntStaticOpaque 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntStaticOpaque 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntStaticOpaque 2 0 2 0 0 temporaries:2 escapes:returned"
      "objectFrameBigIntStaticMixed 2 2 0 0 0 - escapes:stored"
      "objectFrameBigIntStaticMixed 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntStaticMixed 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntStaticMixed 2 0 2 0 0 temporaries:2 escapes:returned"
      "objectFrameBigIntStaticShift 2 2 0 0 0 - confined"
      "objectFrameBigIntStaticShift 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntStaticShift 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntStaticShift 2 0 2 0 0 temporaries:2 escapes:returned"
      "objectFrameBigIntStaticRetained 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntStaticRetained 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntStaticRetained 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntStaticRetained 2 0 2 0 0 temporaries:2 escapes:returned")
  list(SORT _object_bigint_static_rows)
  list(SORT _expected_object_bigint_static_rows)
  if(NOT _object_bigint_static_rows STREQUAL _expected_object_bigint_static_rows)
    message(FATAL_ERROR "imported static BigInt evidence mismatch:\nexpected: ${_expected_object_bigint_static_rows}\nobserved: ${_object_bigint_static_rows}")
  endif()
  message(STATUS "imported static BigInt: twenty-four sites, forty-eight instances, thirty-eight retained; live claims agree")

  if(NOT _object_bigint_shift_literal_pcs STREQUAL "5;9;13;33")
    message(FATAL_ERROR "the signed-shift literal coordinates changed or included its implicit Error: ${_object_bigint_shift_literal_pcs}")
  endif()
  if(NOT _object_bigint_shift_error_rows STREQUAL "objectFrameBigIntShiftEarly 1 0 1 0 0 thrown:1 pc25 unclaimed")
    message(FATAL_ERROR "missing or duplicate independent signed-shift exception evidence: ${_object_bigint_shift_error_rows}")
  endif()
  message(STATUS "imported signed-shift Error: exact source pc25, one implicit object retained through unwinding, no source allocation claim")

  set(_expected_object_bigint_shift_rows
      "objectFrameBigIntShiftSaved 2 2 0 0 0 - confined"
      "objectFrameBigIntShiftSaved 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntShiftSaved 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntShiftSaved 2 0 2 0 0 temporaries:2 escapes:returned"
      "objectFrameBigIntShiftPaths 2 2 0 0 0 - confined"
      "objectFrameBigIntShiftPaths 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntShiftPaths 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntShiftPaths 2 0 2 0 0 temporaries:2 escapes:returned"
      "objectFrameBigIntShiftOpaque 2 2 0 0 0 - escapes:stored"
      "objectFrameBigIntShiftOpaque 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntShiftOpaque 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntShiftOpaque 2 0 2 0 0 temporaries:2 escapes:returned"
      "objectFrameBigIntShiftMixed 2 2 0 0 0 - escapes:stored"
      "objectFrameBigIntShiftMixed 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntShiftMixed 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntShiftMixed 2 0 2 0 0 temporaries:2 escapes:returned"
      "objectFrameBigIntShiftEarly 2 2 0 0 0 - confined"
      "objectFrameBigIntShiftEarly 2 1 1 0 0 temporaries:1 escapes:stored"
      "objectFrameBigIntShiftEarly 2 1 1 0 0 temporaries:1 escapes:stored"
      "objectFrameBigIntShiftEarly 1 0 1 0 0 temporaries:1 escapes:returned"
      "objectFrameBigIntShiftRetained 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntShiftRetained 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntShiftRetained 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntShiftRetained 2 0 2 0 0 temporaries:2 escapes:returned")
  list(SORT _object_bigint_shift_rows)
  list(SORT _expected_object_bigint_shift_rows)
  if(NOT _object_bigint_shift_rows STREQUAL _expected_object_bigint_shift_rows)
    message(FATAL_ERROR "imported signed BigInt shift evidence mismatch:\nexpected: ${_expected_object_bigint_shift_rows}\nobserved: ${_object_bigint_shift_rows}")
  endif()
  message(STATUS "imported signed BigInt shifts: twenty-four sites, forty-seven instances, thirty-five retained; live claims agree")

  set(_expected_object_bigint_divmod_literal_pcs "")
  set(_expected_object_bigint_divmod_error_rows "")
  foreach(_kind Div Mod)
    foreach(_pc 5 9 13 33)
      list(APPEND _expected_object_bigint_divmod_literal_pcs "objectFrameBigIntDivMod${_kind}Early ${_pc}")
    endforeach()
    list(APPEND _expected_object_bigint_divmod_error_rows "objectFrameBigIntDivMod${_kind}Early 1 0 1 0 0 thrown:1 pc25 unclaimed")
  endforeach()
  list(SORT _object_bigint_divmod_literal_pcs)
  list(SORT _expected_object_bigint_divmod_literal_pcs)
  if(NOT _object_bigint_divmod_literal_pcs STREQUAL _expected_object_bigint_divmod_literal_pcs)
    message(FATAL_ERROR "the BigInt Div/Mod literal coordinates changed or included an implicit Error: ${_object_bigint_divmod_literal_pcs}")
  endif()
  list(SORT _object_bigint_divmod_error_rows)
  list(SORT _expected_object_bigint_divmod_error_rows)
  if(NOT _object_bigint_divmod_error_rows STREQUAL _expected_object_bigint_divmod_error_rows)
    message(FATAL_ERROR "missing or duplicate independent BigInt Div/Mod exception evidence: ${_object_bigint_divmod_error_rows}")
  endif()
  message(STATUS "imported BigInt Div/Mod Errors: exact source pc25 in each function, two independent objects retained through unwinding, no source allocation claims")

  set(_expected_object_bigint_divmod_rows
      "objectFrameBigIntDivModSaved 2 2 0 0 0 - confined"
      "objectFrameBigIntDivModSaved 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntDivModSaved 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntDivModSaved 2 0 2 0 0 temporaries:2 escapes:returned"
      "objectFrameBigIntDivModPaths 2 2 0 0 0 - confined"
      "objectFrameBigIntDivModPaths 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntDivModPaths 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntDivModPaths 2 0 2 0 0 temporaries:2 escapes:returned"
      "objectFrameBigIntDivModOpaque 2 2 0 0 0 - escapes:stored"
      "objectFrameBigIntDivModOpaque 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntDivModOpaque 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntDivModOpaque 2 0 2 0 0 temporaries:2 escapes:returned"
      "objectFrameBigIntDivModMixed 2 2 0 0 0 - escapes:stored"
      "objectFrameBigIntDivModMixed 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntDivModMixed 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntDivModMixed 2 0 2 0 0 temporaries:2 escapes:returned"
      "objectFrameBigIntDivModDivEarly 2 2 0 0 0 - confined"
      "objectFrameBigIntDivModDivEarly 2 1 1 0 0 temporaries:1 escapes:stored"
      "objectFrameBigIntDivModDivEarly 2 1 1 0 0 temporaries:1 escapes:stored"
      "objectFrameBigIntDivModDivEarly 1 0 1 0 0 temporaries:1 escapes:returned"
      "objectFrameBigIntDivModModEarly 2 2 0 0 0 - confined"
      "objectFrameBigIntDivModModEarly 2 1 1 0 0 temporaries:1 escapes:stored"
      "objectFrameBigIntDivModModEarly 2 1 1 0 0 temporaries:1 escapes:stored"
      "objectFrameBigIntDivModModEarly 1 0 1 0 0 temporaries:1 escapes:returned"
      "objectFrameBigIntDivModRetained 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntDivModRetained 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntDivModRetained 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntDivModRetained 2 0 2 0 0 temporaries:2 escapes:returned")
  list(SORT _object_bigint_divmod_rows)
  list(SORT _expected_object_bigint_divmod_rows)
  if(NOT _object_bigint_divmod_rows STREQUAL _expected_object_bigint_divmod_rows)
    message(FATAL_ERROR "imported BigInt Div/Mod evidence mismatch:\nexpected: ${_expected_object_bigint_divmod_rows}\nobserved: ${_object_bigint_divmod_rows}")
  endif()
  message(STATUS "imported BigInt Div/Mod: twenty-eight sites, fifty-four instances, thirty-eight retained; live claims agree")
endif()
if(NOT _pyrc EQUAL 0)
  message(FATAL_ERROR "${NAME}: the checker exited ${_pyrc}")
endif()

message(STATUS "escape claims (${NAME}): ${_observed} sites observed (unclaimed ${_unclaimed}), "
               "${_violations} violations, sound ${_sound}, partial ${_partial}, pending ${_pending}, "
               "precision ${_precision}; ${_reasons}")
