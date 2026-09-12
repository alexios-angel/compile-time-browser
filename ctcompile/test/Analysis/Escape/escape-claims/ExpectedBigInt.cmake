  # Keep all measured lifetimes and independent Error coordinates. ND-3's
  # property sinks and own-data-write refusal precede these primitive proofs.
  set(_expected_object_bigint_equality_rows
      "objectFrameBigIntEqualitySaved 2 2 0 0 0 - escapes:passed"
      "objectFrameBigIntEqualitySaved 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameBigIntEqualitySaved 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameBigIntEqualitySaved 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameBigIntEqualityOpaque 2 2 0 0 0 - escapes:passed"
      "objectFrameBigIntEqualityOpaque 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntEqualityOpaque 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameBigIntEqualityOpaque 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameBigIntEqualityMixed 2 2 0 0 0 - escapes:passed"
      "objectFrameBigIntEqualityMixed 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntEqualityMixed 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameBigIntEqualityMixed 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameBigIntEqualityRetained 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameBigIntEqualityRetained 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameBigIntEqualityRetained 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameBigIntEqualityRetained 2 0 2 0 0 temporaries:2 escapes:passed")
  list(SORT _object_bigint_equality_rows)
  list(SORT _expected_object_bigint_equality_rows)
  if(NOT _object_bigint_equality_rows STREQUAL _expected_object_bigint_equality_rows)
    message(FATAL_ERROR "imported BigInt equality evidence mismatch:\nexpected: ${_expected_object_bigint_equality_rows}\nobserved: ${_object_bigint_equality_rows}")
  endif()
  message(STATUS "imported BigInt equality: sixteen sites, thirty-two instances, twenty-six retained; live claims agree")

  # Four exact relational kinds preserve their original BigInt-pair inputs.
  # Independently saved children remain retained. Source bytes and observation
  # counts stay fixed while ordinary-object writes keep conservative claims.
  set(_expected_object_bigint_relational_rows
      "objectFrameBigIntRelationalSaved 2 2 0 0 0 - escapes:passed"
      "objectFrameBigIntRelationalSaved 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameBigIntRelationalSaved 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameBigIntRelationalSaved 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameBigIntRelationalOpaque 2 2 0 0 0 - escapes:passed"
      "objectFrameBigIntRelationalOpaque 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntRelationalOpaque 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameBigIntRelationalOpaque 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameBigIntRelationalMixed 2 2 0 0 0 - escapes:passed"
      "objectFrameBigIntRelationalMixed 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntRelationalMixed 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameBigIntRelationalMixed 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameBigIntRelationalComputed 2 2 0 0 0 - escapes:passed"
      "objectFrameBigIntRelationalComputed 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntRelationalComputed 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameBigIntRelationalComputed 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameBigIntRelationalRetained 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameBigIntRelationalRetained 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameBigIntRelationalRetained 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameBigIntRelationalRetained 2 0 2 0 0 temporaries:2 escapes:passed")
  list(SORT _object_bigint_relational_rows)
  list(SORT _expected_object_bigint_relational_rows)
  if(NOT _object_bigint_relational_rows STREQUAL _expected_object_bigint_relational_rows)
    message(FATAL_ERROR "imported BigInt relational evidence mismatch:\nexpected: ${_expected_object_bigint_relational_rows}\nobserved: ${_object_bigint_relational_rows}")
  endif()
  message(STATUS "imported BigInt relational: twenty sites, forty instances, thirty-two retained; live claims agree")

  # Computed BigInt unary results preserve the original observed lifetimes;
  # primitive categories alone do not prove the preceding own-data writes.
  set(_expected_object_bigint_unary_rows
      "objectFrameBigIntUnarySaved 2 2 0 0 0 - escapes:passed"
      "objectFrameBigIntUnarySaved 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameBigIntUnarySaved 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameBigIntUnarySaved 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameBigIntUnaryPaths 2 2 0 0 0 - escapes:passed"
      "objectFrameBigIntUnaryPaths 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntUnaryPaths 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameBigIntUnaryPaths 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameBigIntUnaryOpaque 2 2 0 0 0 - escapes:passed"
      "objectFrameBigIntUnaryOpaque 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntUnaryOpaque 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameBigIntUnaryOpaque 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameBigIntUnaryMixed 2 2 0 0 0 - escapes:passed"
      "objectFrameBigIntUnaryMixed 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntUnaryMixed 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameBigIntUnaryMixed 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameBigIntUnaryRetained 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameBigIntUnaryRetained 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameBigIntUnaryRetained 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameBigIntUnaryRetained 2 0 2 0 0 temporaries:2 escapes:passed")
  list(SORT _object_bigint_unary_rows)
  list(SORT _expected_object_bigint_unary_rows)
  if(NOT _object_bigint_unary_rows STREQUAL _expected_object_bigint_unary_rows)
    message(FATAL_ERROR "imported BigInt unary evidence mismatch:\nexpected: ${_expected_object_bigint_unary_rows}\nobserved: ${_object_bigint_unary_rows}")
  endif()
  message(STATUS "imported BigInt unary: twenty sites, forty instances, thirty-two retained; live claims agree")

  # Computed BigInt binary results preserve the original observed lifetimes;
  # primitive categories alone do not prove the preceding own-data writes.
  set(_expected_object_bigint_binary_rows
      "objectFrameBigIntBinarySaved 2 2 0 0 0 - escapes:passed"
      "objectFrameBigIntBinarySaved 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameBigIntBinarySaved 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameBigIntBinarySaved 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameBigIntBinaryPaths 2 2 0 0 0 - escapes:passed"
      "objectFrameBigIntBinaryPaths 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntBinaryPaths 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameBigIntBinaryPaths 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameBigIntBinaryOpaque 2 2 0 0 0 - escapes:passed"
      "objectFrameBigIntBinaryOpaque 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntBinaryOpaque 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameBigIntBinaryOpaque 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameBigIntBinaryMixed 2 2 0 0 0 - escapes:passed"
      "objectFrameBigIntBinaryMixed 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntBinaryMixed 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameBigIntBinaryMixed 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameBigIntBinaryRetained 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameBigIntBinaryRetained 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameBigIntBinaryRetained 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameBigIntBinaryRetained 2 0 2 0 0 temporaries:2 escapes:passed")
  list(SORT _object_bigint_binary_rows)
  list(SORT _expected_object_bigint_binary_rows)
  if(NOT _object_bigint_binary_rows STREQUAL _expected_object_bigint_binary_rows)
    message(FATAL_ERROR "imported BigInt binary evidence mismatch:\nexpected: ${_expected_object_bigint_binary_rows}\nobserved: ${_object_bigint_binary_rows}")
  endif()
  message(STATUS "imported BigInt binary: twenty sites, forty instances, thirty-two retained; live claims agree")

  set(_expected_object_bigint_static_rows
      "objectFrameBigIntStaticSaved 2 2 0 0 0 - escapes:passed"
      "objectFrameBigIntStaticSaved 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameBigIntStaticSaved 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameBigIntStaticSaved 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameBigIntStaticPaths 2 2 0 0 0 - escapes:passed"
      "objectFrameBigIntStaticPaths 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntStaticPaths 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameBigIntStaticPaths 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameBigIntStaticOpaque 2 2 0 0 0 - escapes:passed"
      "objectFrameBigIntStaticOpaque 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntStaticOpaque 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameBigIntStaticOpaque 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameBigIntStaticMixed 2 2 0 0 0 - escapes:passed"
      "objectFrameBigIntStaticMixed 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntStaticMixed 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameBigIntStaticMixed 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameBigIntStaticShift 2 2 0 0 0 - escapes:passed"
      "objectFrameBigIntStaticShift 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntStaticShift 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameBigIntStaticShift 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameBigIntStaticRetained 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameBigIntStaticRetained 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameBigIntStaticRetained 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameBigIntStaticRetained 2 0 2 0 0 temporaries:2 escapes:passed")
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
      "objectFrameBigIntShiftSaved 2 2 0 0 0 - escapes:passed"
      "objectFrameBigIntShiftSaved 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameBigIntShiftSaved 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameBigIntShiftSaved 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameBigIntShiftPaths 2 2 0 0 0 - escapes:passed"
      "objectFrameBigIntShiftPaths 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntShiftPaths 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameBigIntShiftPaths 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameBigIntShiftOpaque 2 2 0 0 0 - escapes:passed"
      "objectFrameBigIntShiftOpaque 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntShiftOpaque 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameBigIntShiftOpaque 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameBigIntShiftMixed 2 2 0 0 0 - escapes:passed"
      "objectFrameBigIntShiftMixed 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntShiftMixed 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameBigIntShiftMixed 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameBigIntShiftEarly 2 2 0 0 0 - escapes:passed"
      "objectFrameBigIntShiftEarly 2 1 1 0 0 temporaries:1 escapes:stored"
      "objectFrameBigIntShiftEarly 2 1 1 0 0 temporaries:1 escapes:passed"
      "objectFrameBigIntShiftEarly 1 0 1 0 0 temporaries:1 escapes:passed"
      "objectFrameBigIntShiftRetained 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameBigIntShiftRetained 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameBigIntShiftRetained 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameBigIntShiftRetained 2 0 2 0 0 temporaries:2 escapes:passed")
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
      "objectFrameBigIntDivModSaved 2 2 0 0 0 - escapes:passed"
      "objectFrameBigIntDivModSaved 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameBigIntDivModSaved 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameBigIntDivModSaved 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameBigIntDivModPaths 2 2 0 0 0 - escapes:passed"
      "objectFrameBigIntDivModPaths 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntDivModPaths 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameBigIntDivModPaths 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameBigIntDivModOpaque 2 2 0 0 0 - escapes:passed"
      "objectFrameBigIntDivModOpaque 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntDivModOpaque 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameBigIntDivModOpaque 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameBigIntDivModMixed 2 2 0 0 0 - escapes:passed"
      "objectFrameBigIntDivModMixed 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntDivModMixed 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameBigIntDivModMixed 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameBigIntDivModDivEarly 2 2 0 0 0 - escapes:passed"
      "objectFrameBigIntDivModDivEarly 2 1 1 0 0 temporaries:1 escapes:stored"
      "objectFrameBigIntDivModDivEarly 2 1 1 0 0 temporaries:1 escapes:passed"
      "objectFrameBigIntDivModDivEarly 1 0 1 0 0 temporaries:1 escapes:passed"
      "objectFrameBigIntDivModModEarly 2 2 0 0 0 - escapes:passed"
      "objectFrameBigIntDivModModEarly 2 1 1 0 0 temporaries:1 escapes:stored"
      "objectFrameBigIntDivModModEarly 2 1 1 0 0 temporaries:1 escapes:passed"
      "objectFrameBigIntDivModModEarly 1 0 1 0 0 temporaries:1 escapes:passed"
      "objectFrameBigIntDivModRetained 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameBigIntDivModRetained 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameBigIntDivModRetained 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameBigIntDivModRetained 2 0 2 0 0 temporaries:2 escapes:passed")
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
      "objectFrameBigIntMixedSaved 2 2 0 0 0 - escapes:passed pc10"
      "objectFrameBigIntMixedSaved 2 0 2 0 0 temporaries:2 escapes:passed pc14"
      "objectFrameBigIntMixedSaved 2 0 2 0 0 temporaries:2 escapes:passed pc18"
      "objectFrameBigIntMixedSaved 2 0 2 0 0 temporaries:2 escapes:passed pc77"
      "objectFrameBigIntMixedPrimitives 2 2 0 0 0 - escapes:passed pc9"
      "objectFrameBigIntMixedPrimitives 2 0 2 0 0 temporaries:2 escapes:passed pc13"
      "objectFrameBigIntMixedPrimitives 2 0 2 0 0 temporaries:2 escapes:stored pc17"
      "objectFrameBigIntMixedPrimitives 2 0 2 0 0 temporaries:2 escapes:passed pc54"
      "objectFrameBigIntMixedPaths 2 2 0 0 0 - escapes:passed pc7"
      "objectFrameBigIntMixedPaths 2 0 2 0 0 temporaries:2 escapes:passed pc11"
      "objectFrameBigIntMixedPaths 2 0 2 0 0 temporaries:2 escapes:stored pc15"
      "objectFrameBigIntMixedPaths 2 0 2 0 0 temporaries:2 escapes:passed pc46"
      "objectFrameBigIntMixedNumbers 2 2 0 0 0 - escapes:passed pc8"
      "objectFrameBigIntMixedNumbers 2 0 2 0 0 temporaries:2 escapes:passed pc12"
      "objectFrameBigIntMixedNumbers 2 0 2 0 0 temporaries:2 escapes:stored pc16"
      "objectFrameBigIntMixedNumbers 2 0 2 0 0 temporaries:2 escapes:passed pc52"
      "objectFrameBigIntMixedOpaque 2 2 0 0 0 - escapes:passed pc5"
      "objectFrameBigIntMixedOpaque 2 0 2 0 0 temporaries:2 escapes:passed pc9"
      "objectFrameBigIntMixedOpaque 2 0 2 0 0 temporaries:2 escapes:stored pc13"
      "objectFrameBigIntMixedOpaque 2 0 2 0 0 temporaries:2 escapes:passed pc31"
      "objectFrameBigIntMixedObject 2 2 0 0 0 - escapes:passed pc6"
      "objectFrameBigIntMixedObject 2 0 2 0 0 temporaries:2 escapes:passed pc10"
      "objectFrameBigIntMixedObject 2 0 2 0 0 temporaries:2 escapes:converted pc14"
      "objectFrameBigIntMixedObject 2 0 2 0 0 temporaries:2 escapes:passed pc38"
      "objectFrameBigIntMixedRetained 2 0 2 0 0 temporaries:2 escapes:passed pc6"
      "objectFrameBigIntMixedRetained 2 0 2 0 0 temporaries:2 escapes:passed pc10"
      "objectFrameBigIntMixedRetained 2 0 2 0 0 temporaries:2 escapes:passed pc14"
      "objectFrameBigIntMixedRetained 2 0 2 0 0 temporaries:2 escapes:passed pc37"
      "objectFrameBigIntMixedStrings 2 2 0 0 0 - escapes:passed pc8"
      "objectFrameBigIntMixedStrings 2 0 2 0 0 temporaries:2 escapes:passed pc12"
      "objectFrameBigIntMixedStrings 2 0 2 0 0 temporaries:2 escapes:stored pc16"
      "objectFrameBigIntMixedStrings 2 0 2 0 0 temporaries:2 escapes:passed pc48"
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
      "objectFrameStringBigIntSaved 2 2 0 0 0 - escapes:passed pc10"
      "objectFrameStringBigIntSaved 2 0 2 0 0 temporaries:2 escapes:passed pc14"
      "objectFrameStringBigIntSaved 2 0 2 0 0 temporaries:2 escapes:passed pc31"
      "objectFrameStringBigIntSaved 2 0 2 0 0 temporaries:2 escapes:passed pc91"
      "objectFrameStringBigIntPaths 2 2 0 0 0 - escapes:passed pc8"
      "objectFrameStringBigIntPaths 2 0 2 0 0 temporaries:2 escapes:passed pc12"
      "objectFrameStringBigIntPaths 2 0 2 0 0 temporaries:2 escapes:stored pc16"
      "objectFrameStringBigIntPaths 2 0 2 0 0 temporaries:2 escapes:passed pc50"
      "objectFrameStringBigIntTemplate 2 2 0 0 0 - escapes:passed pc6"
      "objectFrameStringBigIntTemplate 2 0 2 0 0 temporaries:2 escapes:passed pc10"
      "objectFrameStringBigIntTemplate 2 0 2 0 0 temporaries:2 escapes:stored pc14"
      "objectFrameStringBigIntTemplate 2 0 2 0 0 temporaries:2 escapes:passed pc56"
      "objectFrameStringBigIntOpaqueAdd 2 2 0 0 0 - escapes:passed pc4"
      "objectFrameStringBigIntOpaqueAdd 2 0 2 0 0 temporaries:2 escapes:passed pc8"
      "objectFrameStringBigIntOpaqueAdd 2 0 2 0 0 temporaries:2 escapes:stored pc12"
      "objectFrameStringBigIntOpaqueAdd 2 0 2 0 0 temporaries:2 escapes:passed pc26"
      "objectFrameStringBigIntOpaqueTemplate 2 2 0 0 0 - escapes:passed pc4"
      "objectFrameStringBigIntOpaqueTemplate 2 0 2 0 0 temporaries:2 escapes:passed pc8"
      "objectFrameStringBigIntOpaqueTemplate 2 0 2 0 0 temporaries:2 escapes:stored pc12"
      "objectFrameStringBigIntOpaqueTemplate 2 0 2 0 0 temporaries:2 escapes:passed pc31"
      "objectFrameStringBigIntObject 2 2 0 0 0 - escapes:passed pc5"
      "objectFrameStringBigIntObject 2 0 2 0 0 temporaries:2 escapes:passed pc9"
      "objectFrameStringBigIntObject 2 0 2 0 0 temporaries:2 escapes:stored pc13"
      "objectFrameStringBigIntObject 2 0 2 0 0 temporaries:2 escapes:passed pc36"
      "objectFrameStringBigIntMixed 2 2 0 0 0 - escapes:passed pc7"
      "objectFrameStringBigIntMixed 2 0 2 0 0 temporaries:2 escapes:passed pc11"
      "objectFrameStringBigIntMixed 2 0 2 0 0 temporaries:2 escapes:stored pc15"
      "objectFrameStringBigIntMixed 2 0 2 0 0 temporaries:2 escapes:passed pc43"
      "objectFrameStringBigIntRetained 2 0 2 0 0 temporaries:2 escapes:passed pc7"
      "objectFrameStringBigIntRetained 2 0 2 0 0 temporaries:2 escapes:passed pc11"
      "objectFrameStringBigIntRetained 2 0 2 0 0 temporaries:2 escapes:passed pc15"
      "objectFrameStringBigIntRetained 2 0 2 0 0 temporaries:2 escapes:passed pc43"
  )
  list(SORT _expected_object_string_bigint_rows)
  list(SORT _object_string_bigint_rows)
  if(NOT _object_string_bigint_rows STREQUAL _expected_object_string_bigint_rows)
    message(FATAL_ERROR "String BigInt evidence mismatch:\nexpected: ${_expected_object_string_bigint_rows}\nobserved: ${_object_string_bigint_rows}")
  endif()
  message(STATUS "imported String BigInt: thirty-two literal sites, sixty-four instances, fifty retained; live claims agree")

  set(_expected_object_bigint_pow_rows
      "objectFrameBigIntPowSaved 2 2 0 0 0 - escapes:passed"
      "objectFrameBigIntPowSaved 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameBigIntPowSaved 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameBigIntPowSaved 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameBigIntPowPaths 2 2 0 0 0 - escapes:passed"
      "objectFrameBigIntPowPaths 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntPowPaths 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameBigIntPowPaths 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameBigIntPowOpaque 2 2 0 0 0 - escapes:passed"
      "objectFrameBigIntPowOpaque 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntPowOpaque 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameBigIntPowOpaque 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameBigIntPowMixed 2 2 0 0 0 - escapes:passed"
      "objectFrameBigIntPowMixed 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameBigIntPowMixed 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameBigIntPowMixed 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameBigIntPowNegativeEarly 3 3 0 0 0 - escapes:passed"
      "objectFrameBigIntPowNegativeEarly 3 2 1 0 0 temporaries:1 escapes:stored"
      "objectFrameBigIntPowNegativeEarly 3 2 1 0 0 temporaries:1 escapes:passed"
      "objectFrameBigIntPowNegativeEarly 1 0 1 0 0 temporaries:1 escapes:passed"
      "objectFrameBigIntPowCapEarly 3 3 0 0 0 - escapes:passed"
      "objectFrameBigIntPowCapEarly 3 2 1 0 0 temporaries:1 escapes:stored"
      "objectFrameBigIntPowCapEarly 3 2 1 0 0 temporaries:1 escapes:passed"
      "objectFrameBigIntPowCapEarly 1 0 1 0 0 temporaries:1 escapes:passed"
      "objectFrameBigIntPowRetained 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameBigIntPowRetained 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameBigIntPowRetained 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameBigIntPowRetained 2 0 2 0 0 temporaries:2 escapes:passed")
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
      "primitivePlusEarly 2 2 0 0 0 - escapes:passed pc5"
      "primitivePlusEarly 2 1 1 0 0 temporaries:1 escapes:passed pc9"
      "primitivePlusEarly 2 1 1 0 0 temporaries:1 escapes:stored pc13"
      "primitivePlusEarly 1 0 1 0 0 temporaries:1 escapes:passed pc32"
      "primitivePlusRetained 2 1 1 0 0 temporaries:1 escapes:passed pc6"
      "primitivePlusRetained 2 1 1 0 0 temporaries:1 escapes:passed pc10"
      "primitivePlusRetained 2 1 1 0 0 temporaries:1 escapes:passed pc14"
      "primitivePlusRetained 1 0 1 0 0 temporaries:1 escapes:passed pc36"
      "primitivePlusOpaque 2 2 0 0 0 - escapes:passed pc4"
      "primitivePlusOpaque 2 1 1 0 0 temporaries:1 escapes:passed pc8"
      "primitivePlusOpaque 2 1 1 0 0 temporaries:1 escapes:stored pc12"
      "primitivePlusOpaque 1 0 1 0 0 temporaries:1 escapes:passed pc25"
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
      "primitiveMixedSubEarly 2 2 0 0 0 - escapes:passed pc5"
      "primitiveMixedSubEarly 2 1 1 0 0 temporaries:1 escapes:passed pc9"
      "primitiveMixedSubEarly 2 1 1 0 0 temporaries:1 escapes:stored pc13"
      "primitiveMixedSubEarly 1 0 1 0 0 temporaries:1 escapes:passed pc33"
      "primitiveMixedSubRetained 2 1 1 0 0 temporaries:1 escapes:passed pc6"
      "primitiveMixedSubRetained 2 1 1 0 0 temporaries:1 escapes:passed pc10"
      "primitiveMixedSubRetained 2 1 1 0 0 temporaries:1 escapes:passed pc14"
      "primitiveMixedSubRetained 1 0 1 0 0 temporaries:1 escapes:passed pc37"
      "primitiveMixedSubOpaque 2 2 0 0 0 - escapes:passed pc4"
      "primitiveMixedSubOpaque 2 1 1 0 0 temporaries:1 escapes:passed pc8"
      "primitiveMixedSubOpaque 2 1 1 0 0 temporaries:1 escapes:stored pc12"
      "primitiveMixedSubOpaque 1 0 1 0 0 temporaries:1 escapes:passed pc26"
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
      "primitiveMixedMulEarly 2 2 0 0 0 - escapes:passed pc5"
      "primitiveMixedMulEarly 2 1 1 0 0 temporaries:1 escapes:passed pc9"
      "primitiveMixedMulEarly 2 1 1 0 0 temporaries:1 escapes:stored pc13"
      "primitiveMixedMulEarly 1 0 1 0 0 temporaries:1 escapes:passed pc33"
      "primitiveMixedMulRetained 2 1 1 0 0 temporaries:1 escapes:passed pc6"
      "primitiveMixedMulRetained 2 1 1 0 0 temporaries:1 escapes:passed pc10"
      "primitiveMixedMulRetained 2 1 1 0 0 temporaries:1 escapes:passed pc14"
      "primitiveMixedMulRetained 1 0 1 0 0 temporaries:1 escapes:passed pc37"
      "primitiveMixedMulOpaque 2 2 0 0 0 - escapes:passed pc4"
      "primitiveMixedMulOpaque 2 1 1 0 0 temporaries:1 escapes:passed pc8"
      "primitiveMixedMulOpaque 2 1 1 0 0 temporaries:1 escapes:stored pc12"
      "primitiveMixedMulOpaque 1 0 1 0 0 temporaries:1 escapes:passed pc26"
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

  # Static UShr with two original BigInts throws an independent TypeError.
  # Literal retention and implicit Error sites have separate measured claims.
  set(_expected_primitive_ushr_literal_pcs
      "primitiveUShrEarly 5 obj"
      "primitiveUShrEarly 7 arr"
      "primitiveUShrRetained 6 obj"
      "primitiveUShrRetained 8 arr"
      "primitiveUShrOpaque 3 obj"
      "primitiveUShrOpaque 5 arr"
  )
  set(_expected_primitive_ushr_rows
      "primitiveUShrEarly obj 2 2 0 0 0 - confined pc5"
      "primitiveUShrEarly arr 2 1 1 0 0 temporaries:1 escapes:passed pc7"
      "primitiveUShrRetained obj 2 1 1 0 0 temporaries:1 escapes:stored pc6"
      "primitiveUShrRetained arr 2 2 0 0 0 - escapes:passed pc8"
      "primitiveUShrOpaque obj 2 2 0 0 0 - escapes:stored pc3"
      "primitiveUShrOpaque arr 2 1 1 0 0 temporaries:1 escapes:passed pc5"
  )
  set(_expected_primitive_ushr_error_rows
      "primitiveUShrEarly obj 1 0 1 0 0 thrown:1 pc24 unclaimed"
      "primitiveUShrRetained obj 1 0 1 0 0 thrown:1 pc29 unclaimed"
      "primitiveUShrOpaque obj 1 0 1 0 0 thrown:1 pc11 unclaimed"
  )
  set(_expected_primitive_mixedstatic_literal_pcs
      "primitiveMixedStaticEarly 5 obj"
      "primitiveMixedStaticEarly 7 arr"
      "primitiveMixedStaticRetained 6 obj"
      "primitiveMixedStaticRetained 8 arr"
      "primitiveMixedStaticOpaque 3 obj"
      "primitiveMixedStaticOpaque 5 arr"
  )
  set(_expected_primitive_mixedstatic_rows
      "primitiveMixedStaticEarly obj 2 2 0 0 0 - confined pc5"
      "primitiveMixedStaticEarly arr 2 1 1 0 0 temporaries:1 escapes:passed pc7"
      "primitiveMixedStaticRetained obj 2 1 1 0 0 temporaries:1 escapes:stored pc6"
      "primitiveMixedStaticRetained arr 2 2 0 0 0 - escapes:passed pc8"
      "primitiveMixedStaticOpaque obj 2 2 0 0 0 - escapes:stored pc3"
      "primitiveMixedStaticOpaque arr 2 1 1 0 0 temporaries:1 escapes:passed pc5"
  )
  set(_expected_primitive_mixedstatic_error_rows
      "primitiveMixedStaticEarly obj 1 0 1 0 0 thrown:1 pc24 unclaimed"
      "primitiveMixedStaticRetained obj 1 0 1 0 0 thrown:1 pc29 unclaimed"
      "primitiveMixedStaticOpaque obj 1 0 1 0 0 thrown:1 pc11 unclaimed"
  )
  set(_expected_primitive_mixedadd_literal_pcs
      "primitiveMixedAddEarly 5 obj"
      "primitiveMixedAddEarly 7 arr"
      "primitiveMixedAddRetained 6 obj"
      "primitiveMixedAddRetained 8 arr"
      "primitiveMixedAddOpaque 3 obj"
      "primitiveMixedAddOpaque 5 arr"
  )
  set(_expected_primitive_mixedadd_rows
      "primitiveMixedAddEarly obj 2 2 0 0 0 - confined pc5"
      "primitiveMixedAddEarly arr 2 1 1 0 0 temporaries:1 escapes:passed pc7"
      "primitiveMixedAddRetained obj 2 1 1 0 0 temporaries:1 escapes:stored pc6"
      "primitiveMixedAddRetained arr 2 2 0 0 0 - escapes:passed pc8"
      "primitiveMixedAddOpaque obj 2 2 0 0 0 - escapes:stored pc3"
      "primitiveMixedAddOpaque arr 2 1 1 0 0 temporaries:1 escapes:passed pc5"
  )
  set(_expected_primitive_mixedadd_error_rows
      "primitiveMixedAddEarly obj 1 0 1 0 0 thrown:1 pc24 unclaimed"
      "primitiveMixedAddRetained obj 1 0 1 0 0 thrown:1 pc29 unclaimed"
      "primitiveMixedAddOpaque obj 1 0 1 0 0 thrown:1 pc11 unclaimed"
  )
  foreach(_family IN ITEMS ushr mixedstatic mixedadd)
    foreach(_table IN ITEMS literal_pcs rows error_rows)
      set(_expected "${_expected_primitive_${_family}_${_table}}")
      set(_observed_rows "${_primitive_${_family}_${_table}}")
      list(SORT _expected)
      list(SORT _observed_rows)
      if(NOT _observed_rows STREQUAL _expected)
        message(FATAL_ERROR "BigInt ${_family} ${_table} mismatch:\nexpected: ${_expected}\nobserved: ${_observed_rows}")
      endif()
    endforeach()
    message(STATUS "imported BigInt ${_family}: six literal sites, twelve instances, three retained; three independent Errors and live claims agree")
  endforeach()
