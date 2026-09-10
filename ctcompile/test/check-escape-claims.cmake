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
  set(_primitive_plus_rows "")
  set(_primitive_plus_error_rows "")
  set(_primitive_plus_literal_pcs "")
  set(_primitive_plus_error_pc_Early "24")
  set(_primitive_plus_error_pc_Retained "28")
  set(_primitive_plus_error_pc_Opaque "17")
  set(_primitive_mixed_sub_rows "")
  set(_primitive_mixed_sub_error_rows "")
  set(_primitive_mixed_sub_literal_pcs "")
  set(_primitive_mixed_sub_error_pc_Early "25")
  set(_primitive_mixed_sub_error_pc_Retained "29")
  set(_primitive_mixed_sub_error_pc_Opaque "18")
  set(_primitive_mixed_mul_rows "")
  set(_primitive_mixed_mul_error_rows "")
  set(_primitive_mixed_mul_literal_pcs "")
  set(_primitive_mixed_mul_error_pc_Early "25")
  set(_primitive_mixed_mul_error_pc_Retained "29")
  set(_primitive_mixed_mul_error_pc_Opaque "18")
  set(_primitive_mixed_div_rows "")
  set(_primitive_mixed_div_error_rows "")
  set(_primitive_mixed_div_literal_pcs "")
  set(_primitive_mixed_div_error_pc_Early "25")
  set(_primitive_mixed_div_error_pc_Retained "29")
  set(_primitive_mixed_div_error_pc_Opaque "18")
  set(_primitive_mixed_mod_rows "")
  set(_primitive_mixed_mod_error_rows "")
  set(_primitive_mixed_mod_literal_pcs "")
  set(_primitive_mixed_mod_error_pc_Early "25")
  set(_primitive_mixed_mod_error_pc_Retained "29")
  set(_primitive_mixed_mod_error_pc_Opaque "18")
  set(_object_bigint_pow_rows "")
  set(_object_bigint_pow_error_rows "")
  set(_object_bigint_pow_literal_pcs "")
  set(_pow_error_pc_Negative "26")
  set(_pow_error_pc_Cap "25")
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
  # Pow errors come from exact negative and VM-capped exponent operations.
  # Pin source bodies and measured coordinates separately from source literals.
  string(REGEX MATCH "function objectFrameBigIntPowNegativeEarly\\(choice\\) \\{[^\n]*\n(    [^\n]*\n)*\\}" _pow_error_source "${_fixture_source}")
  string(SHA256 _pow_error_hash "${_pow_error_source}")
  if(NOT _pow_error_hash STREQUAL "a5ac019be0eb44465c848414e51d732c6b291a99cbd2db37151f5558127d4073")
    message(FATAL_ERROR "the BigInt Pow Negative source changed; remeasure its bytecode coordinate before updating this case")
  endif()
  string(REGEX MATCH "function objectFrameBigIntPowCapEarly\\(choice\\) \\{[^\n]*\n(    [^\n]*\n)*\\}" _pow_error_source "${_fixture_source}")
  string(SHA256 _pow_error_hash "${_pow_error_source}")
  if(NOT _pow_error_hash STREQUAL "31462dc9b17e9c47e4ad4255559bbffda5aea3dc9100e7044c25a7fbb93bbea1")
    message(FATAL_ERROR "the BigInt Pow Cap source changed; remeasure its bytecode coordinate before updating this case")
  endif()
  # Every String/BigInt producer, mixed comparison and promoted historical body
  # is pinned independently; retention never authorizes a comparison value.
  foreach(_source_pair IN ITEMS
      "primitiveMixedModOpaque 26cee5f5791cf093ccb87f32195188d4d7db500a2f1ded5e5ab7c8fd6d04b97a"
      "primitiveMixedModRetained 8d20b9e09e8a1e7ed6cd7b13a94efdbefc9f2fb5c83bfdf239358f7999c4654c"
      "primitiveMixedModEarly e9691f03829b6a13b0c5c7ebafa1c6f1178b7c0cbf282cd1dffc88c0e1601931"
      "primitiveMixedDivEarly dda864dd95688fe8e9d137c765aa573efe376db4bf16a7a29bb94a26ae30d4b6"
      "primitiveMixedDivRetained da1ed960e38cdabbfb6dcb53f7500296d5eff6825ac3ec18a6cf64b434f1b8c4"
      "primitiveMixedDivOpaque bb9b6277e0f251adf9a786894c4e5a0b84e04d6bf867bfb06c639d316baa94d5"
      "primitiveMixedMulEarly 7403df6e44566a6ff8f0d26d644f0d583f493f8cecdc78681851349e7c2c3849"
      "primitiveMixedMulRetained 5e855810ff011d17f96f35b97cd6e5e34f6124671e49012fdac6c3194a8ddd22"
      "primitiveMixedMulOpaque 898dd89965e9785b044791d6e693f01d83fab359405b96bac2f3a5c86c6a675f"
      "primitiveMixedSubEarly 58c83fac64c817cd634dc603024f0c0dc7d33b3552e0f6fa03b34441e7705745"
      "primitiveMixedSubRetained ebc894848c37d18b829e16231c5281ad96526b9d76eec0c4da09f221dddf8242"
      "primitiveMixedSubOpaque 192d76fa4ee14b19564e0f3642ca95a71ed500730789d69c2744cc2811c8f90a"
      "primitivePlusEarly 8c223906ec365dd8ab5a06b8168b17a93cdf81c42afa5803fa82fc24d18afaaa"
      "primitivePlusRetained 75e3e72fa61e33182913ec9022bbc84f138f9070cce3712753ebe1e99742398b"
      "primitivePlusOpaque 2dacd3acb40ee8863d417f927b47db976dce0253a6adf5a0eb8b06778573928b"
      "objectFrameStringBigIntSaved aec68c23e5f9bf8bfa9e5ab52c5ede7b1b8c9822da89811035d45d1ac3a6369e"
      "objectFrameStringBigIntPaths 2dc79c09d7cbc6a9d8e5d065665b77ff0eb33085c3c4fd9955a5e54ddb8168ff"
      "objectFrameStringBigIntTemplate cb31d3c6a5118ab6b13b36d6808ea45026d20cac641034055c7f28c0df0c3ea5"
      "objectFrameStringBigIntOpaqueAdd 835de91a4f59b61a11fc748a7913ba7494eef65bd9308f3adda8474f868dddcf"
      "objectFrameStringBigIntOpaqueTemplate 88f98df0ce48c181f8721038543339eac0c07a5b46684c19fcc61e576d541250"
      "objectFrameStringBigIntObject ad31a100dc70a5e182d7fe01bdf6fd37874e991a03d6fa8d03e47e7dcbcbe7cf"
      "objectFrameStringBigIntMixed 93be77d12ea361ed413761a2c919cb9d5e1d9e0dd569cb5ba5a5650558a7f88b"
      "objectFrameStringBigIntRetained de5ad600d8d125470e50e0ae2c8ea40c0d88f5ccdd6ce90f1de9f7c9e6e6af32"
      "objectFrameBigIntMixedSaved 551ddbb520f3943021ea0f9d62fa00da544d9197541b2c7af5d93c36f84706f2"
      "objectFrameBigIntMixedPrimitives 426ff5237d803c81f50895ad55ed76bf8e3957978287a63cdd3ebfbd96ff47df"
      "objectFrameBigIntMixedPaths 07be46f1898b3bd6770b745f89986f527c85048de407fc2d6877e70fd031669f"
      "objectFrameBigIntMixedNumbers c7e7c59c2a1f4521246d45c2f6ba6d1443de77f694d34bd785ba894634778087"
      "objectFrameBigIntMixedOpaque 006c8c7c8446a11d6ee37cd656110cc133ff8303434ca70f1a8e5bfd7d3581b9"
      "objectFrameBigIntMixedObject 60e510407c187de4856933b7d5888d569ace5184c727eae852686442cd298282"
      "objectFrameBigIntMixedRetained bce40177f3c45573538dcf5f570ca89c820cf733f36eb63c3884b9234f9da000"
      "objectFrameBigIntMixedStrings d0b6f0b99362df38d9c93e787520153c9daee6c204037b16f7c04130c42b8b3b"
      "objectFrameLooseEqualityBigInt d39859d4356e8e61f4d67b9ad30e5ea6df547430738747bff84b97d7ab4fc5c5"
      "objectFrameBigIntEqualityMixed 8cdd5b79d1a3b8c0502152f839fc2ba1f6bad98736de66a36be67f516e5b5667"
      "objectFrameBigIntRelationalMixed b8efef815d87e1deec7728aed6d0efb24c4d19fb12e74653625e1aa4315872b6"
      "objectFrameBigIntUnaryMixed 7869f7391dc47cf015cc661fe66ab7762e91bbdc18455aa04dfdb681c68de820"
      "objectFrameBigIntBinaryMixed 60842a13c6a7a7364b1d3af8651ff50a2624e69863826daa3dbb3be5b27ba9a3"
      "objectFrameBigIntStaticMixed a9dae7f7353425dee72b437a3ae7e6f44c7a8559480381679a2ee2466c8d1983"
      "objectFrameBigIntShiftMixed 6061e3812db39af626f1ff685133c92c3d7f00f5cc2fd0a95beac18c217fb67e"
      "objectFrameBigIntDivModMixed 80682cde55e777b9e6b233311457b8abdad92ec3fed87b6f77b50752b6268232"
      "objectFrameBigIntPowMixed 826b4e604c15a2e1437856e0b2e7c9183916efba6556cd5a65cb105f9774a1c1"
)
    string(REPLACE " " ";" _source_fields "${_source_pair}")
    list(GET _source_fields 0 _source_name)
    list(GET _source_fields 1 _source_hash)
    string(REGEX MATCH "function ${_source_name}\\([^\n]*\\) \\{[^\n]*\n(    [^\n]*\n)*\\}" _source_body "${_fixture_source}")
    string(SHA256 _observed_source_hash "${_source_body}")
    if(NOT _observed_source_hash STREQUAL _source_hash)
      message(FATAL_ERROR "${_source_name}: primitive BigInt source changed; preserve or remeasure its evidence")
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
    elseif(_function_name MATCHES "^primitivePlus(Early|Retained|Opaque)$" AND _line MATCHES "^alloc ")
      if(NOT _line MATCHES "^alloc ([0-9]+) kind obj$")
        message(FATAL_ERROR "${_function_name}: unexpected BigInt Plus source allocation: ${_line}")
      endif()
      list(APPEND _primitive_plus_literal_pcs "${_function_name} ${CMAKE_MATCH_1}")
    elseif(_function_name MATCHES "^primitivePlus(Early|Retained|Opaque)$" AND _line MATCHES "^site ")
      string(REGEX REPLACE "^primitivePlus" "" _plus_suffix "${_function_name}")
      set(_plus_error_pc "${_primitive_plus_error_pc_${_plus_suffix}}")
      if(NOT _line MATCHES "^site ([0-9]+) kind obj made ([0-9]+) confined ([0-9]+) escaped ([0-9]+) unresolved ([0-9]+) unchecked ([0-9]+) routes ([^ ]+)$")
        message(FATAL_ERROR "${_function_name}: unexpected BigInt Plus observation: ${_line}")
      endif()
      set(_pc "${CMAKE_MATCH_1}")
      set(_row "${_function_name} ${CMAKE_MATCH_2} ${CMAKE_MATCH_3} ${CMAKE_MATCH_4} ${CMAKE_MATCH_5} ${CMAKE_MATCH_6} ${CMAKE_MATCH_7}")
      if(_pc STREQUAL _plus_error_pc)
        if(NOT _line STREQUAL "site ${_plus_error_pc} kind obj made 1 confined 0 escaped 1 unresolved 0 unchecked 0 routes thrown:1")
          message(FATAL_ERROR "${_function_name}: the independent Plus Error was not retained through unwinding: ${_line}")
        endif()
        if(_claim_text MATCHES "escape ${_program_hash} ${_function_index} ${_plus_error_pc} [^\n]+")
          message(FATAL_ERROR "${_function_name}: the implicit Plus Error acquired a source allocation claim")
        endif()
        list(APPEND _primitive_plus_error_rows "${_row} pc${_pc} unclaimed")
      else()
        string(REGEX MATCHALL "escape ${_program_hash} ${_function_index} ${_pc} obj [^\n]+" _plus_claims "${_claim_text}")
        list(LENGTH _plus_claims _plus_claim_count)
        if(NOT _plus_claim_count EQUAL 1)
          message(FATAL_ERROR "${_function_name}: missing or duplicate BigInt Plus claim at pc ${_pc}")
        endif()
        if(NOT _claim_text MATCHES "escape ${_program_hash} ${_function_index} ${_pc} obj ([^\n]+)")
          message(FATAL_ERROR "${_function_name}: no compiler claim for observed object at pc ${_pc}")
        endif()
        list(APPEND _primitive_plus_rows "${_row} ${CMAKE_MATCH_1} pc${_pc}")
      endif()
    elseif(_function_name MATCHES "^primitiveMixedSub(Early|Retained|Opaque)$" AND _line MATCHES "^alloc ")
      if(NOT _line MATCHES "^alloc ([0-9]+) kind obj$")
        message(FATAL_ERROR "${_function_name}: unexpected mixed BigInt Sub source allocation: ${_line}")
      endif()
      list(APPEND _primitive_mixed_sub_literal_pcs "${_function_name} ${CMAKE_MATCH_1}")
    elseif(_function_name MATCHES "^primitiveMixedSub(Early|Retained|Opaque)$" AND _line MATCHES "^site ")
      string(REGEX REPLACE "^primitiveMixedSub" "" _mixed_sub_suffix "${_function_name}")
      set(_mixed_sub_error_pc "${_primitive_mixed_sub_error_pc_${_mixed_sub_suffix}}")
      if(NOT _line MATCHES "^site ([0-9]+) kind obj made ([0-9]+) confined ([0-9]+) escaped ([0-9]+) unresolved ([0-9]+) unchecked ([0-9]+) routes ([^ ]+)$")
        message(FATAL_ERROR "${_function_name}: unexpected mixed BigInt Sub observation: ${_line}")
      endif()
      set(_pc "${CMAKE_MATCH_1}")
      set(_row "${_function_name} ${CMAKE_MATCH_2} ${CMAKE_MATCH_3} ${CMAKE_MATCH_4} ${CMAKE_MATCH_5} ${CMAKE_MATCH_6} ${CMAKE_MATCH_7}")
      if(_pc STREQUAL _mixed_sub_error_pc)
        if(NOT _line STREQUAL "site ${_mixed_sub_error_pc} kind obj made 1 confined 0 escaped 1 unresolved 0 unchecked 0 routes thrown:1")
          message(FATAL_ERROR "${_function_name}: the independent mixed Sub Error was not retained through unwinding: ${_line}")
        endif()
        if(_claim_text MATCHES "escape ${_program_hash} ${_function_index} ${_mixed_sub_error_pc} [^\n]+")
          message(FATAL_ERROR "${_function_name}: the implicit mixed Sub Error acquired a source allocation claim")
        endif()
        list(APPEND _primitive_mixed_sub_error_rows "${_row} pc${_pc} unclaimed")
      else()
        string(REGEX MATCHALL "escape ${_program_hash} ${_function_index} ${_pc} obj [^\n]+" _mixed_sub_claims "${_claim_text}")
        list(LENGTH _mixed_sub_claims _mixed_sub_claim_count)
        if(NOT _mixed_sub_claim_count EQUAL 1)
          message(FATAL_ERROR "${_function_name}: missing or duplicate mixed BigInt Sub claim at pc ${_pc}")
        endif()
        if(NOT _claim_text MATCHES "escape ${_program_hash} ${_function_index} ${_pc} obj ([^\n]+)")
          message(FATAL_ERROR "${_function_name}: no compiler claim for observed object at pc ${_pc}")
        endif()
        list(APPEND _primitive_mixed_sub_rows "${_row} ${CMAKE_MATCH_1} pc${_pc}")
      endif()
    elseif(_function_name MATCHES "^primitiveMixed(Mul|Div|Mod)(Early|Retained|Opaque)$" AND _line MATCHES "^alloc ")
      string(REGEX REPLACE "^primitiveMixed(Mul|Div|Mod).*$" "\\1" _mixed_kind "${_function_name}")
      string(TOLOWER "${_mixed_kind}" _mixed_operation)
      if(NOT _line MATCHES "^alloc ([0-9]+) kind obj$")
        message(FATAL_ERROR "${_function_name}: unexpected mixed BigInt ${_mixed_kind} source allocation: ${_line}")
      endif()
      list(APPEND _primitive_mixed_${_mixed_operation}_literal_pcs "${_function_name} ${CMAKE_MATCH_1}")
    elseif(_function_name MATCHES "^primitiveMixed(Mul|Div|Mod)(Early|Retained|Opaque)$" AND _line MATCHES "^site ")
      string(REGEX REPLACE "^primitiveMixed(Mul|Div|Mod).*$" "\\1" _mixed_kind "${_function_name}")
      string(TOLOWER "${_mixed_kind}" _mixed_operation)
      string(REGEX REPLACE "^primitiveMixed${_mixed_kind}" "" _mixed_suffix "${_function_name}")
      set(_mixed_error_pc "${_primitive_mixed_${_mixed_operation}_error_pc_${_mixed_suffix}}")
      if(NOT _line MATCHES "^site ([0-9]+) kind obj made ([0-9]+) confined ([0-9]+) escaped ([0-9]+) unresolved ([0-9]+) unchecked ([0-9]+) routes ([^ ]+)$")
        message(FATAL_ERROR "${_function_name}: unexpected mixed BigInt ${_mixed_kind} observation: ${_line}")
      endif()
      set(_pc "${CMAKE_MATCH_1}")
      set(_row "${_function_name} ${CMAKE_MATCH_2} ${CMAKE_MATCH_3} ${CMAKE_MATCH_4} ${CMAKE_MATCH_5} ${CMAKE_MATCH_6} ${CMAKE_MATCH_7}")
      if(_pc STREQUAL _mixed_error_pc)
        if(NOT _line STREQUAL "site ${_mixed_error_pc} kind obj made 1 confined 0 escaped 1 unresolved 0 unchecked 0 routes thrown:1")
          message(FATAL_ERROR "${_function_name}: the independent mixed ${_mixed_kind} Error was not retained through unwinding: ${_line}")
        endif()
        if(_claim_text MATCHES "escape ${_program_hash} ${_function_index} ${_mixed_error_pc} [^\n]+")
          message(FATAL_ERROR "${_function_name}: the implicit mixed ${_mixed_kind} Error acquired a source allocation claim")
        endif()
        list(APPEND _primitive_mixed_${_mixed_operation}_error_rows "${_row} pc${_pc} unclaimed")
      else()
        string(REGEX MATCHALL "escape ${_program_hash} ${_function_index} ${_pc} obj [^\n]+" _mixed_claims "${_claim_text}")
        list(LENGTH _mixed_claims _mixed_claim_count)
        if(NOT _mixed_claim_count EQUAL 1)
          message(FATAL_ERROR "${_function_name}: missing or duplicate mixed BigInt ${_mixed_kind} claim at pc ${_pc}")
        endif()
        if(NOT _claim_text MATCHES "escape ${_program_hash} ${_function_index} ${_pc} obj ([^\n]+)")
          message(FATAL_ERROR "${_function_name}: no compiler claim for observed object at pc ${_pc}")
        endif()
        list(APPEND _primitive_mixed_${_mixed_operation}_rows "${_row} ${CMAKE_MATCH_1} pc${_pc}")
      endif()
    elseif(_function_name MATCHES "^objectFrameBigIntMixed(Saved|Primitives|Paths|Numbers|Opaque|Object|Retained|Strings)$" AND _line MATCHES "^alloc ")
      if(NOT _line MATCHES "^alloc ([0-9]+) kind obj$")
        message(FATAL_ERROR "${_function_name}: unexpected mixed BigInt comparison source allocation: ${_line}")
      endif()
      list(APPEND _object_mixed_bigint_literal_pcs "${_function_name} ${CMAKE_MATCH_1}")
    elseif(_function_name MATCHES "^objectFrameBigIntMixed(Saved|Primitives|Paths|Numbers|Opaque|Object|Retained|Strings)$" AND _line MATCHES "^site ")
      if(NOT _line MATCHES "^site ([0-9]+) kind obj made ([0-9]+) confined ([0-9]+) escaped ([0-9]+) unresolved ([0-9]+) unchecked ([0-9]+) routes ([^ ]+)$")
        message(FATAL_ERROR "${_function_name}: unexpected mixed BigInt comparison observation: ${_line}")
      endif()
      set(_pc "${CMAKE_MATCH_1}")
      set(_row "${_function_name} ${CMAKE_MATCH_2} ${CMAKE_MATCH_3} ${CMAKE_MATCH_4} ${CMAKE_MATCH_5} ${CMAKE_MATCH_6} ${CMAKE_MATCH_7}")
      string(REGEX MATCHALL "escape ${_program_hash} ${_function_index} ${_pc} obj [^\n]+" _mixed_bigint_claims "${_claim_text}")
      list(LENGTH _mixed_bigint_claims _mixed_bigint_claim_count)
      if(NOT _mixed_bigint_claim_count EQUAL 1)
        message(FATAL_ERROR "${_function_name}: missing or duplicate mixed BigInt comparison claim at pc ${_pc}")
      endif()
      if(NOT _claim_text MATCHES "escape ${_program_hash} ${_function_index} ${_pc} obj ([^\n]+)")
        message(FATAL_ERROR "${_function_name}: no compiler claim for observed object at pc ${_pc}")
      endif()
      list(APPEND _object_mixed_bigint_rows "${_row} ${CMAKE_MATCH_1} pc${_pc}")
    elseif(_function_name MATCHES "^objectFrameStringBigInt(Saved|Paths|Template|OpaqueAdd|OpaqueTemplate|Object|Mixed|Retained)$" AND _line MATCHES "^alloc ")
      if(NOT _line MATCHES "^alloc ([0-9]+) kind obj$")
        message(FATAL_ERROR "${_function_name}: unexpected String BigInt source allocation: ${_line}")
      endif()
      list(APPEND _object_string_bigint_literal_pcs "${_function_name} ${CMAKE_MATCH_1}")
    elseif(_function_name MATCHES "^objectFrameStringBigInt(Saved|Paths|Template|OpaqueAdd|OpaqueTemplate|Object|Mixed|Retained)$" AND _line MATCHES "^site ")
      if(NOT _line MATCHES "^site ([0-9]+) kind obj made ([0-9]+) confined ([0-9]+) escaped ([0-9]+) unresolved ([0-9]+) unchecked ([0-9]+) routes ([^ ]+)$")
        message(FATAL_ERROR "${_function_name}: unexpected String BigInt observation: ${_line}")
      endif()
      set(_pc "${CMAKE_MATCH_1}")
      set(_row "${_function_name} ${CMAKE_MATCH_2} ${CMAKE_MATCH_3} ${CMAKE_MATCH_4} ${CMAKE_MATCH_5} ${CMAKE_MATCH_6} ${CMAKE_MATCH_7}")
      string(REGEX MATCHALL "escape ${_program_hash} ${_function_index} ${_pc} obj [^\n]+" _string_bigint_claims "${_claim_text}")
      list(LENGTH _string_bigint_claims _string_bigint_claim_count)
      if(NOT _string_bigint_claim_count EQUAL 1)
        message(FATAL_ERROR "${_function_name}: missing or duplicate String BigInt claim at pc ${_pc}")
      endif()
      if(NOT _claim_text MATCHES "escape ${_program_hash} ${_function_index} ${_pc} obj ([^\n]+)")
        message(FATAL_ERROR "${_function_name}: no compiler claim for observed object at pc ${_pc}")
      endif()
      list(APPEND _object_string_bigint_rows "${_row} ${CMAKE_MATCH_1} pc${_pc}")
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
    elseif(_function_name MATCHES "^objectFrameBigIntPow(Negative|Cap)Early$" AND _line MATCHES "^alloc ")
      if(NOT _line MATCHES "^alloc ([0-9]+) kind obj$")
        message(FATAL_ERROR "${_function_name}: unexpected BigInt Pow literal allocation: ${_line}")
      endif()
      list(APPEND _object_bigint_pow_literal_pcs "${_function_name} ${CMAKE_MATCH_1}")
    elseif(_function_name MATCHES "^objectFrameBigIntPow(Saved|Paths|Opaque|Mixed|NegativeEarly|CapEarly|Retained)$" AND _line MATCHES "^site ")
      if(NOT _line MATCHES "^site ([0-9]+) kind obj made ([0-9]+) confined ([0-9]+) escaped ([0-9]+) unresolved ([0-9]+) unchecked ([0-9]+) routes ([^ ]+)$")
        message(FATAL_ERROR "${_function_name}: unexpected BigInt Pow observation: ${_line}")
      endif()
      set(_pc "${CMAKE_MATCH_1}")
      set(_row "${_function_name} ${CMAKE_MATCH_2} ${CMAKE_MATCH_3} ${CMAKE_MATCH_4} ${CMAKE_MATCH_5} ${CMAKE_MATCH_6} ${CMAKE_MATCH_7}")
      set(_pow_error_pc "")
      if(_function_name MATCHES "^objectFrameBigIntPow(Negative|Cap)Early$")
        set(_pow_error_pc "${_pow_error_pc_${CMAKE_MATCH_1}}")
      endif()
      if(_pc STREQUAL _pow_error_pc)
        if(NOT _line STREQUAL "site ${_pow_error_pc} kind obj made 2 confined 0 escaped 2 unresolved 0 unchecked 0 routes thrown:2")
          message(FATAL_ERROR "${_function_name}: the independent Errors were not retained through unwinding: ${_line}")
        endif()
        if(_claim_text MATCHES "escape ${_program_hash} ${_function_index} ${_pow_error_pc} [^\n]+")
          message(FATAL_ERROR "${_function_name}: the implicit BigInt Pow Error acquired a source allocation claim")
        endif()
        list(APPEND _object_bigint_pow_error_rows "${_row} pc${_pow_error_pc} unclaimed")
      else()
        if(NOT _claim_text MATCHES "escape ${_program_hash} ${_function_index} ${_pc} obj ([^\n]+)")
          message(FATAL_ERROR "${_function_name}: no compiler claim for observed object at pc ${_pc}")
        endif()
        list(APPEND _object_bigint_pow_rows "${_row} ${CMAKE_MATCH_1}")
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
      "objectFrameLooseEqualityBigInt 1 1 0 0 0 - confined"
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
  # The exact BigInt control now has separate retention categories for every
  # producer. Opaque inputs keep Stored; the saved child retains its own identity.
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
      "objectFrameArithmeticBinaryBigInt 2 2 0 0 0 - confined"
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
      "objectFrameAddConcatBigInt 2 2 0 0 0 - confined"
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
      "objectFrameBigIntEqualityMixed 2 2 0 0 0 - confined"
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
      "objectFrameBigIntRelationalMixed 2 2 0 0 0 - confined"
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
      "objectFrameBigIntUnaryMixed 2 2 0 0 0 - confined"
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
      "objectFrameBigIntBinaryMixed 2 2 0 0 0 - confined"
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
      "objectFrameBigIntStaticMixed 2 2 0 0 0 - confined"
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
      "objectFrameBigIntShiftMixed 2 2 0 0 0 - confined"
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
      "objectFrameBigIntDivModMixed 2 2 0 0 0 - confined"
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

  set(_expected_object_bigint_pow_literal_pcs "")
  set(_expected_object_bigint_pow_error_rows "")
  foreach(_pc 5 9 13 34)
    list(APPEND _expected_object_bigint_pow_literal_pcs "objectFrameBigIntPowNegativeEarly ${_pc}")
  endforeach()
  list(APPEND _expected_object_bigint_pow_error_rows "objectFrameBigIntPowNegativeEarly 2 0 2 0 0 thrown:2 pc26 unclaimed")
  foreach(_pc 5 9 13 33)
    list(APPEND _expected_object_bigint_pow_literal_pcs "objectFrameBigIntPowCapEarly ${_pc}")
  endforeach()
  list(APPEND _expected_object_bigint_pow_error_rows "objectFrameBigIntPowCapEarly 2 0 2 0 0 thrown:2 pc25 unclaimed")
  list(SORT _object_bigint_pow_literal_pcs)
  list(SORT _expected_object_bigint_pow_literal_pcs)
  if(NOT _object_bigint_pow_literal_pcs STREQUAL _expected_object_bigint_pow_literal_pcs)
    message(FATAL_ERROR "the BigInt Pow literal coordinates changed or included an implicit Error: ${_object_bigint_pow_literal_pcs}")
  endif()
  list(SORT _object_bigint_pow_error_rows)
  list(SORT _expected_object_bigint_pow_error_rows)
  if(NOT _object_bigint_pow_error_rows STREQUAL _expected_object_bigint_pow_error_rows)
    message(FATAL_ERROR "missing or duplicate independent BigInt Pow exception evidence: ${_object_bigint_pow_error_rows}")
  endif()
  message(STATUS "imported BigInt Pow Errors: two negative and two VM-cap objects retained through unwinding at exact source coordinates, no source allocation claims")

  set(_expected_object_mixed_bigint_literal_pcs
      "objectFrameBigIntMixedSaved 10"
      "objectFrameBigIntMixedSaved 14"
      "objectFrameBigIntMixedSaved 18"
      "objectFrameBigIntMixedSaved 77"
      "objectFrameBigIntMixedPrimitives 9"
      "objectFrameBigIntMixedPrimitives 13"
      "objectFrameBigIntMixedPrimitives 17"
      "objectFrameBigIntMixedPrimitives 54"
      "objectFrameBigIntMixedPaths 7"
      "objectFrameBigIntMixedPaths 11"
      "objectFrameBigIntMixedPaths 15"
      "objectFrameBigIntMixedPaths 46"
      "objectFrameBigIntMixedNumbers 8"
      "objectFrameBigIntMixedNumbers 12"
      "objectFrameBigIntMixedNumbers 16"
      "objectFrameBigIntMixedNumbers 52"
      "objectFrameBigIntMixedOpaque 5"
      "objectFrameBigIntMixedOpaque 9"
      "objectFrameBigIntMixedOpaque 13"
      "objectFrameBigIntMixedOpaque 31"
      "objectFrameBigIntMixedObject 6"
      "objectFrameBigIntMixedObject 10"
      "objectFrameBigIntMixedObject 14"
      "objectFrameBigIntMixedObject 38"
      "objectFrameBigIntMixedRetained 6"
      "objectFrameBigIntMixedRetained 10"
      "objectFrameBigIntMixedRetained 14"
      "objectFrameBigIntMixedRetained 37"
      "objectFrameBigIntMixedStrings 8"
      "objectFrameBigIntMixedStrings 12"
      "objectFrameBigIntMixedStrings 16"
      "objectFrameBigIntMixedStrings 48"
)
  list(SORT _expected_object_mixed_bigint_literal_pcs)
  list(SORT _object_mixed_bigint_literal_pcs)
  if(NOT _object_mixed_bigint_literal_pcs STREQUAL _expected_object_mixed_bigint_literal_pcs)
    message(FATAL_ERROR "mixed BigInt comparison source allocation coordinates changed: ${_object_mixed_bigint_literal_pcs}")
  endif()
  set(_expected_object_mixed_bigint_rows
      "objectFrameBigIntMixedSaved 2 2 0 0 0 - confined pc10"
      "objectFrameBigIntMixedSaved 2 0 2 0 0 temporaries:2 escapes:stored pc14"
      "objectFrameBigIntMixedSaved 2 0 2 0 0 temporaries:2 escapes:stored pc18"
      "objectFrameBigIntMixedSaved 2 0 2 0 0 temporaries:2 escapes:returned pc77"
      "objectFrameBigIntMixedPrimitives 2 2 0 0 0 - confined pc9"
      "objectFrameBigIntMixedPrimitives 2 0 2 0 0 temporaries:2 escapes:stored pc13"
      "objectFrameBigIntMixedPrimitives 2 0 2 0 0 temporaries:2 escapes:stored pc17"
      "objectFrameBigIntMixedPrimitives 2 0 2 0 0 temporaries:2 escapes:returned pc54"
      "objectFrameBigIntMixedPaths 2 2 0 0 0 - confined pc7"
      "objectFrameBigIntMixedPaths 2 0 2 0 0 temporaries:2 escapes:stored pc11"
      "objectFrameBigIntMixedPaths 2 0 2 0 0 temporaries:2 escapes:stored pc15"
      "objectFrameBigIntMixedPaths 2 0 2 0 0 temporaries:2 escapes:returned pc46"
      "objectFrameBigIntMixedNumbers 2 2 0 0 0 - confined pc8"
      "objectFrameBigIntMixedNumbers 2 0 2 0 0 temporaries:2 escapes:stored pc12"
      "objectFrameBigIntMixedNumbers 2 0 2 0 0 temporaries:2 escapes:stored pc16"
      "objectFrameBigIntMixedNumbers 2 0 2 0 0 temporaries:2 escapes:returned pc52"
      "objectFrameBigIntMixedOpaque 2 2 0 0 0 - escapes:stored pc5"
      "objectFrameBigIntMixedOpaque 2 0 2 0 0 temporaries:2 escapes:stored pc9"
      "objectFrameBigIntMixedOpaque 2 0 2 0 0 temporaries:2 escapes:stored pc13"
      "objectFrameBigIntMixedOpaque 2 0 2 0 0 temporaries:2 escapes:returned pc31"
      "objectFrameBigIntMixedObject 2 2 0 0 0 - escapes:stored pc6"
      "objectFrameBigIntMixedObject 2 0 2 0 0 temporaries:2 escapes:converted pc10"
      "objectFrameBigIntMixedObject 2 0 2 0 0 temporaries:2 escapes:converted pc14"
      "objectFrameBigIntMixedObject 2 0 2 0 0 temporaries:2 escapes:returned pc38"
      "objectFrameBigIntMixedRetained 2 0 2 0 0 temporaries:2 escapes:stored pc6"
      "objectFrameBigIntMixedRetained 2 0 2 0 0 temporaries:2 escapes:stored pc10"
      "objectFrameBigIntMixedRetained 2 0 2 0 0 temporaries:2 escapes:stored pc14"
      "objectFrameBigIntMixedRetained 2 0 2 0 0 temporaries:2 escapes:returned pc37"
      "objectFrameBigIntMixedStrings 2 2 0 0 0 - confined pc8"
      "objectFrameBigIntMixedStrings 2 0 2 0 0 temporaries:2 escapes:stored pc12"
      "objectFrameBigIntMixedStrings 2 0 2 0 0 temporaries:2 escapes:stored pc16"
      "objectFrameBigIntMixedStrings 2 0 2 0 0 temporaries:2 escapes:returned pc48"
)
  list(SORT _expected_object_mixed_bigint_rows)
  list(SORT _object_mixed_bigint_rows)
  if(NOT _object_mixed_bigint_rows STREQUAL _expected_object_mixed_bigint_rows)
    message(FATAL_ERROR "mixed BigInt comparison evidence mismatch:\nexpected: ${_expected_object_mixed_bigint_rows}\nobserved: ${_object_mixed_bigint_rows}")
  endif()
  message(STATUS "imported mixed BigInt comparisons: thirty-two literal sites, sixty-four instances, fifty retained; live claims agree")
  set(_expected_object_string_bigint_literal_pcs
      "objectFrameStringBigIntSaved 10"
      "objectFrameStringBigIntSaved 14"
      "objectFrameStringBigIntSaved 31"
      "objectFrameStringBigIntSaved 91"
      "objectFrameStringBigIntPaths 8"
      "objectFrameStringBigIntPaths 12"
      "objectFrameStringBigIntPaths 16"
      "objectFrameStringBigIntPaths 50"
      "objectFrameStringBigIntTemplate 6"
      "objectFrameStringBigIntTemplate 10"
      "objectFrameStringBigIntTemplate 14"
      "objectFrameStringBigIntTemplate 56"
      "objectFrameStringBigIntOpaqueAdd 4"
      "objectFrameStringBigIntOpaqueAdd 8"
      "objectFrameStringBigIntOpaqueAdd 12"
      "objectFrameStringBigIntOpaqueAdd 26"
      "objectFrameStringBigIntOpaqueTemplate 4"
      "objectFrameStringBigIntOpaqueTemplate 8"
      "objectFrameStringBigIntOpaqueTemplate 12"
      "objectFrameStringBigIntOpaqueTemplate 31"
      "objectFrameStringBigIntObject 5"
      "objectFrameStringBigIntObject 9"
      "objectFrameStringBigIntObject 13"
      "objectFrameStringBigIntObject 36"
      "objectFrameStringBigIntMixed 7"
      "objectFrameStringBigIntMixed 11"
      "objectFrameStringBigIntMixed 15"
      "objectFrameStringBigIntMixed 43"
      "objectFrameStringBigIntRetained 7"
      "objectFrameStringBigIntRetained 11"
      "objectFrameStringBigIntRetained 15"
      "objectFrameStringBigIntRetained 43"
)
  list(SORT _expected_object_string_bigint_literal_pcs)
  list(SORT _object_string_bigint_literal_pcs)
  if(NOT _object_string_bigint_literal_pcs STREQUAL _expected_object_string_bigint_literal_pcs)
    message(FATAL_ERROR "String BigInt source allocation coordinates changed: ${_object_string_bigint_literal_pcs}")
  endif()
  set(_expected_object_string_bigint_rows
      "objectFrameStringBigIntSaved 2 2 0 0 0 - confined pc10"
      "objectFrameStringBigIntSaved 2 0 2 0 0 temporaries:2 escapes:stored pc14"
      "objectFrameStringBigIntSaved 2 0 2 0 0 temporaries:2 escapes:stored pc31"
      "objectFrameStringBigIntSaved 2 0 2 0 0 temporaries:2 escapes:returned pc91"
      "objectFrameStringBigIntPaths 2 2 0 0 0 - confined pc8"
      "objectFrameStringBigIntPaths 2 0 2 0 0 temporaries:2 escapes:stored pc12"
      "objectFrameStringBigIntPaths 2 0 2 0 0 temporaries:2 escapes:stored pc16"
      "objectFrameStringBigIntPaths 2 0 2 0 0 temporaries:2 escapes:returned pc50"
      "objectFrameStringBigIntTemplate 2 2 0 0 0 - confined pc6"
      "objectFrameStringBigIntTemplate 2 0 2 0 0 temporaries:2 escapes:stored pc10"
      "objectFrameStringBigIntTemplate 2 0 2 0 0 temporaries:2 escapes:stored pc14"
      "objectFrameStringBigIntTemplate 2 0 2 0 0 temporaries:2 escapes:returned pc56"
      "objectFrameStringBigIntOpaqueAdd 2 2 0 0 0 - escapes:stored pc4"
      "objectFrameStringBigIntOpaqueAdd 2 0 2 0 0 temporaries:2 escapes:stored pc8"
      "objectFrameStringBigIntOpaqueAdd 2 0 2 0 0 temporaries:2 escapes:stored pc12"
      "objectFrameStringBigIntOpaqueAdd 2 0 2 0 0 temporaries:2 escapes:returned pc26"
      "objectFrameStringBigIntOpaqueTemplate 2 2 0 0 0 - escapes:stored pc4"
      "objectFrameStringBigIntOpaqueTemplate 2 0 2 0 0 temporaries:2 escapes:stored pc8"
      "objectFrameStringBigIntOpaqueTemplate 2 0 2 0 0 temporaries:2 escapes:stored pc12"
      "objectFrameStringBigIntOpaqueTemplate 2 0 2 0 0 temporaries:2 escapes:returned pc31"
      "objectFrameStringBigIntObject 2 2 0 0 0 - escapes:stored pc5"
      "objectFrameStringBigIntObject 2 0 2 0 0 temporaries:2 escapes:stored pc9"
      "objectFrameStringBigIntObject 2 0 2 0 0 temporaries:2 escapes:stored pc13"
      "objectFrameStringBigIntObject 2 0 2 0 0 temporaries:2 escapes:returned pc36"
      "objectFrameStringBigIntMixed 2 2 0 0 0 - confined pc7"
      "objectFrameStringBigIntMixed 2 0 2 0 0 temporaries:2 escapes:stored pc11"
      "objectFrameStringBigIntMixed 2 0 2 0 0 temporaries:2 escapes:stored pc15"
      "objectFrameStringBigIntMixed 2 0 2 0 0 temporaries:2 escapes:returned pc43"
      "objectFrameStringBigIntRetained 2 0 2 0 0 temporaries:2 escapes:stored pc7"
      "objectFrameStringBigIntRetained 2 0 2 0 0 temporaries:2 escapes:stored pc11"
      "objectFrameStringBigIntRetained 2 0 2 0 0 temporaries:2 escapes:stored pc15"
      "objectFrameStringBigIntRetained 2 0 2 0 0 temporaries:2 escapes:returned pc43"
  )
  list(SORT _expected_object_string_bigint_rows)
  list(SORT _object_string_bigint_rows)
  if(NOT _object_string_bigint_rows STREQUAL _expected_object_string_bigint_rows)
    message(FATAL_ERROR "String BigInt evidence mismatch:\nexpected: ${_expected_object_string_bigint_rows}\nobserved: ${_object_string_bigint_rows}")
  endif()
  message(STATUS "imported String BigInt: thirty-two literal sites, sixty-four instances, fifty retained; live claims agree")

  set(_expected_object_bigint_pow_rows
      "objectFrameBigIntPowSaved 2 2 0 0 0 - confined"
      "objectFrameBigIntPowSaved 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntPowSaved 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntPowSaved 2 0 2 0 0 temporaries:2 escapes:returned"
      "objectFrameBigIntPowPaths 2 2 0 0 0 - confined"
      "objectFrameBigIntPowPaths 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntPowPaths 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntPowPaths 2 0 2 0 0 temporaries:2 escapes:returned"
      "objectFrameBigIntPowOpaque 2 2 0 0 0 - escapes:stored"
      "objectFrameBigIntPowOpaque 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntPowOpaque 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntPowOpaque 2 0 2 0 0 temporaries:2 escapes:returned"
      "objectFrameBigIntPowMixed 2 2 0 0 0 - confined"
      "objectFrameBigIntPowMixed 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntPowMixed 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntPowMixed 2 0 2 0 0 temporaries:2 escapes:returned"
      "objectFrameBigIntPowNegativeEarly 3 3 0 0 0 - confined"
      "objectFrameBigIntPowNegativeEarly 3 2 1 0 0 temporaries:1 escapes:stored"
      "objectFrameBigIntPowNegativeEarly 3 2 1 0 0 temporaries:1 escapes:stored"
      "objectFrameBigIntPowNegativeEarly 1 0 1 0 0 temporaries:1 escapes:returned"
      "objectFrameBigIntPowCapEarly 3 3 0 0 0 - confined"
      "objectFrameBigIntPowCapEarly 3 2 1 0 0 temporaries:1 escapes:stored"
      "objectFrameBigIntPowCapEarly 3 2 1 0 0 temporaries:1 escapes:stored"
      "objectFrameBigIntPowCapEarly 1 0 1 0 0 temporaries:1 escapes:returned"
      "objectFrameBigIntPowRetained 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntPowRetained 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntPowRetained 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntPowRetained 2 0 2 0 0 temporaries:2 escapes:returned")
  list(SORT _object_bigint_pow_rows)
  list(SORT _expected_object_bigint_pow_rows)
  if(NOT _object_bigint_pow_rows STREQUAL _expected_object_bigint_pow_rows)
    message(FATAL_ERROR "imported BigInt Pow evidence mismatch:\nexpected: ${_expected_object_bigint_pow_rows}\nobserved: ${_object_bigint_pow_rows}")
  endif()
  message(STATUS "imported BigInt Pow: twenty-eight literal sites, sixty instances, thirty-eight retained; live claims agree")
  # Separate source literals from independent thrown Errors, including the
  # opaque actuals whose observed TypeError never proves their future category.
  set(_expected_primitive_plus_literal_pcs
      "primitivePlusEarly 5"
      "primitivePlusEarly 9"
      "primitivePlusEarly 13"
      "primitivePlusEarly 32"
      "primitivePlusRetained 6"
      "primitivePlusRetained 10"
      "primitivePlusRetained 14"
      "primitivePlusRetained 36"
      "primitivePlusOpaque 4"
      "primitivePlusOpaque 8"
      "primitivePlusOpaque 12"
      "primitivePlusOpaque 25"
  )
  list(SORT _expected_primitive_plus_literal_pcs)
  list(SORT _primitive_plus_literal_pcs)
  if(NOT _primitive_plus_literal_pcs STREQUAL _expected_primitive_plus_literal_pcs)
    message(FATAL_ERROR "BigInt Plus literal_pcs mismatch:\nexpected: ${_expected_primitive_plus_literal_pcs}\nobserved: ${_primitive_plus_literal_pcs}")
  endif()
  set(_expected_primitive_plus_rows
      "primitivePlusEarly 2 2 0 0 0 - confined pc5"
      "primitivePlusEarly 2 1 1 0 0 temporaries:1 escapes:stored pc9"
      "primitivePlusEarly 2 1 1 0 0 temporaries:1 escapes:stored pc13"
      "primitivePlusEarly 1 0 1 0 0 temporaries:1 escapes:returned pc32"
      "primitivePlusRetained 2 1 1 0 0 temporaries:1 escapes:stored pc6"
      "primitivePlusRetained 2 1 1 0 0 temporaries:1 escapes:stored pc10"
      "primitivePlusRetained 2 1 1 0 0 temporaries:1 escapes:stored pc14"
      "primitivePlusRetained 1 0 1 0 0 temporaries:1 escapes:returned pc36"
      "primitivePlusOpaque 2 2 0 0 0 - escapes:stored pc4"
      "primitivePlusOpaque 2 1 1 0 0 temporaries:1 escapes:stored pc8"
      "primitivePlusOpaque 2 1 1 0 0 temporaries:1 escapes:stored pc12"
      "primitivePlusOpaque 1 0 1 0 0 temporaries:1 escapes:returned pc25"
  )
  list(SORT _expected_primitive_plus_rows)
  list(SORT _primitive_plus_rows)
  if(NOT _primitive_plus_rows STREQUAL _expected_primitive_plus_rows)
    message(FATAL_ERROR "BigInt Plus rows mismatch:\nexpected: ${_expected_primitive_plus_rows}\nobserved: ${_primitive_plus_rows}")
  endif()
  set(_expected_primitive_plus_error_rows
      "primitivePlusEarly 1 0 1 0 0 thrown:1 pc24 unclaimed"
      "primitivePlusRetained 1 0 1 0 0 thrown:1 pc28 unclaimed"
      "primitivePlusOpaque 1 0 1 0 0 thrown:1 pc17 unclaimed"
  )
  list(SORT _expected_primitive_plus_error_rows)
  list(SORT _primitive_plus_error_rows)
  if(NOT _primitive_plus_error_rows STREQUAL _expected_primitive_plus_error_rows)
    message(FATAL_ERROR "BigInt Plus error_rows mismatch:\nexpected: ${_expected_primitive_plus_error_rows}\nobserved: ${_primitive_plus_error_rows}")
  endif()
  message(STATUS "imported BigInt Plus: twelve literal sites, twenty-one instances, ten retained; three independent Errors and live claims agree")
  # Separate source literals from independent thrown Errors, including the
  # opaque actuals whose observed TypeError never proves their future category.
  set(_expected_primitive_mixed_sub_literal_pcs
      "primitiveMixedSubEarly 5"
      "primitiveMixedSubEarly 9"
      "primitiveMixedSubEarly 13"
      "primitiveMixedSubEarly 33"
      "primitiveMixedSubRetained 6"
      "primitiveMixedSubRetained 10"
      "primitiveMixedSubRetained 14"
      "primitiveMixedSubRetained 37"
      "primitiveMixedSubOpaque 4"
      "primitiveMixedSubOpaque 8"
      "primitiveMixedSubOpaque 12"
      "primitiveMixedSubOpaque 26"
  )
  list(SORT _expected_primitive_mixed_sub_literal_pcs)
  list(SORT _primitive_mixed_sub_literal_pcs)
  if(NOT _primitive_mixed_sub_literal_pcs STREQUAL _expected_primitive_mixed_sub_literal_pcs)
    message(FATAL_ERROR "mixed BigInt Sub literal_pcs mismatch:\nexpected: ${_expected_primitive_mixed_sub_literal_pcs}\nobserved: ${_primitive_mixed_sub_literal_pcs}")
  endif()
  set(_expected_primitive_mixed_sub_rows
      "primitiveMixedSubEarly 2 2 0 0 0 - confined pc5"
      "primitiveMixedSubEarly 2 1 1 0 0 temporaries:1 escapes:stored pc9"
      "primitiveMixedSubEarly 2 1 1 0 0 temporaries:1 escapes:stored pc13"
      "primitiveMixedSubEarly 1 0 1 0 0 temporaries:1 escapes:returned pc33"
      "primitiveMixedSubRetained 2 1 1 0 0 temporaries:1 escapes:stored pc6"
      "primitiveMixedSubRetained 2 1 1 0 0 temporaries:1 escapes:stored pc10"
      "primitiveMixedSubRetained 2 1 1 0 0 temporaries:1 escapes:stored pc14"
      "primitiveMixedSubRetained 1 0 1 0 0 temporaries:1 escapes:returned pc37"
      "primitiveMixedSubOpaque 2 2 0 0 0 - escapes:stored pc4"
      "primitiveMixedSubOpaque 2 1 1 0 0 temporaries:1 escapes:stored pc8"
      "primitiveMixedSubOpaque 2 1 1 0 0 temporaries:1 escapes:stored pc12"
      "primitiveMixedSubOpaque 1 0 1 0 0 temporaries:1 escapes:returned pc26"
  )
  list(SORT _expected_primitive_mixed_sub_rows)
  list(SORT _primitive_mixed_sub_rows)
  if(NOT _primitive_mixed_sub_rows STREQUAL _expected_primitive_mixed_sub_rows)
    message(FATAL_ERROR "mixed BigInt Sub rows mismatch:\nexpected: ${_expected_primitive_mixed_sub_rows}\nobserved: ${_primitive_mixed_sub_rows}")
  endif()
  set(_expected_primitive_mixed_sub_error_rows
      "primitiveMixedSubEarly 1 0 1 0 0 thrown:1 pc25 unclaimed"
      "primitiveMixedSubRetained 1 0 1 0 0 thrown:1 pc29 unclaimed"
      "primitiveMixedSubOpaque 1 0 1 0 0 thrown:1 pc18 unclaimed"
  )
  list(SORT _expected_primitive_mixed_sub_error_rows)
  list(SORT _primitive_mixed_sub_error_rows)
  if(NOT _primitive_mixed_sub_error_rows STREQUAL _expected_primitive_mixed_sub_error_rows)
    message(FATAL_ERROR "mixed BigInt Sub error_rows mismatch:\nexpected: ${_expected_primitive_mixed_sub_error_rows}\nobserved: ${_primitive_mixed_sub_error_rows}")
  endif()
  message(STATUS "imported mixed BigInt Sub: twelve literal sites, twenty-one instances, ten retained; three independent Errors and live claims agree")
  set(_expected_primitive_mixed_mul_literal_pcs
      "primitiveMixedMulEarly 5"
      "primitiveMixedMulEarly 9"
      "primitiveMixedMulEarly 13"
      "primitiveMixedMulEarly 33"
      "primitiveMixedMulRetained 6"
      "primitiveMixedMulRetained 10"
      "primitiveMixedMulRetained 14"
      "primitiveMixedMulRetained 37"
      "primitiveMixedMulOpaque 4"
      "primitiveMixedMulOpaque 8"
      "primitiveMixedMulOpaque 12"
      "primitiveMixedMulOpaque 26"
  )
  set(_expected_primitive_mixed_mul_rows
      "primitiveMixedMulEarly 2 2 0 0 0 - confined pc5"
      "primitiveMixedMulEarly 2 1 1 0 0 temporaries:1 escapes:stored pc9"
      "primitiveMixedMulEarly 2 1 1 0 0 temporaries:1 escapes:stored pc13"
      "primitiveMixedMulEarly 1 0 1 0 0 temporaries:1 escapes:returned pc33"
      "primitiveMixedMulRetained 2 1 1 0 0 temporaries:1 escapes:stored pc6"
      "primitiveMixedMulRetained 2 1 1 0 0 temporaries:1 escapes:stored pc10"
      "primitiveMixedMulRetained 2 1 1 0 0 temporaries:1 escapes:stored pc14"
      "primitiveMixedMulRetained 1 0 1 0 0 temporaries:1 escapes:returned pc37"
      "primitiveMixedMulOpaque 2 2 0 0 0 - escapes:stored pc4"
      "primitiveMixedMulOpaque 2 1 1 0 0 temporaries:1 escapes:stored pc8"
      "primitiveMixedMulOpaque 2 1 1 0 0 temporaries:1 escapes:stored pc12"
      "primitiveMixedMulOpaque 1 0 1 0 0 temporaries:1 escapes:returned pc26"
  )
  set(_expected_primitive_mixed_mul_error_rows
      "primitiveMixedMulEarly 1 0 1 0 0 thrown:1 pc25 unclaimed"
      "primitiveMixedMulRetained 1 0 1 0 0 thrown:1 pc29 unclaimed"
      "primitiveMixedMulOpaque 1 0 1 0 0 thrown:1 pc18 unclaimed"
  )
  foreach(_mixed_kind IN ITEMS Mul Div Mod)
    string(TOLOWER "${_mixed_kind}" _mixed_operation)
    foreach(_table IN ITEMS literal_pcs rows error_rows)
      string(REPLACE "primitiveMixedMul" "primitiveMixed${_mixed_kind}" _expected
          "${_expected_primitive_mixed_mul_${_table}}")
      set(_observed_rows "${_primitive_mixed_${_mixed_operation}_${_table}}")
      list(SORT _expected)
      list(SORT _observed_rows)
      if(NOT _observed_rows STREQUAL _expected)
        message(FATAL_ERROR "mixed BigInt ${_mixed_kind} ${_table} mismatch:\nexpected: ${_expected}\nobserved: ${_observed_rows}")
      endif()
    endforeach()
    message(STATUS "imported mixed BigInt ${_mixed_kind}: twelve literal sites, twenty-one instances, ten retained; three independent Errors and live claims agree")
  endforeach()
endif()
if(NOT _pyrc EQUAL 0)
  message(FATAL_ERROR "${NAME}: the checker exited ${_pyrc}")
endif()

message(STATUS "escape claims (${NAME}): ${_observed} sites observed (unclaimed ${_unclaimed}), "
               "${_violations} violations, sound ${_sound}, partial ${_partial}, pending ${_pending}, "
               "precision ${_precision}; ${_reasons}")
