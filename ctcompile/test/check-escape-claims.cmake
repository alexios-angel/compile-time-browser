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
endif()
if(NOT _pyrc EQUAL 0)
  message(FATAL_ERROR "${NAME}: the checker exited ${_pyrc}")
endif()

message(STATUS "escape claims (${NAME}): ${_observed} sites observed (unclaimed ${_unclaimed}), "
               "${_violations} violations, sound ${_sound}, partial ${_partial}, pending ${_pending}, "
               "precision ${_precision}; ${_reasons}")
