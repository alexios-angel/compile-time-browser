  # Runtime lifetime counts and fixture sources are unchanged by ND-3. A
  # property receiver's Passed witness now precedes many later escape reasons;
  # ordinary-object assignments no longer authorize contents refinement.
  set(_expected_publication_rows
      "globalAliasJoin 2 1 1 0 0 globals:1 escapes:passed"
      "globalAliasLoop 2 1 1 0 0 globals:1 escapes:passed"
      "globalRetainedContainer 1 0 1 0 0 globals:1 escapes:passed"
      "globalRetainedContainer 1 0 1 0 0 globals:1 escapes:passed"
      "globalReplacedPublication 1 0 1 0 0 globals:1 escapes:passed")
  list(SORT _publication_rows)
  list(SORT _expected_publication_rows)
  if(NOT _publication_rows STREQUAL _expected_publication_rows)
    message(FATAL_ERROR "global publication evidence mismatch:\nexpected: ${_expected_publication_rows}\nobserved: ${_publication_rows}")
  endif()
  message(STATUS "global publication: five sites, seven objects, five retained through globals; live claims agree")

  # Runtime confines both source and packing arrays, but the compiler marks
  # the source array Passed at Iterable. The spread receiver is also Passed.
  # Construction also allocates an instance and its lazily created prototype
  # at one unclaimed runtime site: one confined and one retained through the
  # constructor's global closure. Neither is an object-literal claim.
  set(_expected_spread_rows
      "spreadRetainedCall obj 1 0 1 0 0 globals:1 escapes:passed"
      "spreadRetainedCall arr 1 1 0 0 0 - confined"
      "spreadRetainedCall arr 1 1 0 0 0 - escapes:passed"
      "spreadRetainedConstruct obj 1 0 1 0 0 globals:1 escapes:passed"
      "spreadRetainedConstruct arr 1 1 0 0 0 - confined"
      "spreadRetainedConstruct arr 1 1 0 0 0 - escapes:passed"
      "spreadRetainedConstruct obj 2 1 1 0 0 globals:1 unclaimed"
      "spreadRetainedReceiver obj 1 0 1 0 0 globals:1 escapes:passed"
      "spreadRetainedReceiver arr 1 1 0 0 0 - confined"
      "spreadRetainedReceiver arr 1 1 0 0 0 - escapes:passed")
  list(SORT _spread_rows)
  list(SORT _expected_spread_rows)
  if(NOT _spread_rows STREQUAL _expected_spread_rows)
    message(FATAL_ERROR "spread argument evidence mismatch:\nexpected: ${_expected_spread_rows}\nobserved: ${_spread_rows}")
  endif()
  message(STATUS "spread arguments: six arrays confined at frame exit, three retained literal objects, one unclaimed constructor site; live claims agree")

  # These claims come from raw imported JavaScript, including frame_enter and
  # frame_exit. Pin observed retention alongside conservative compiler claims:
  # returning a loaded child after overwrite keeps the OLD child reachable;
  # a loaded array alias mutates the SAME array. Cycles stay conservative.
  set(_expected_array_frame_rows
      "arrayFramePrivate obj 2 2 0 0 0 - confined"
      "arrayFramePrivate arr 2 2 0 0 0 - confined"
      "arrayFrameReturned obj 1 0 1 0 0 temporaries:1 escapes:stored"
      "arrayFrameReturned arr 1 0 1 0 0 temporaries:1 escapes:returned"
      "arrayFrameSavedRead obj 1 0 1 0 0 temporaries:1 escapes:stored"
      "arrayFrameSavedRead obj 1 1 0 0 0 - confined"
      "arrayFrameSavedRead arr 1 1 0 0 0 - escapes:passed"
      "arrayFrameOverwrite obj 1 1 0 0 0 - confined"
      "arrayFrameOverwrite obj 1 0 1 0 0 temporaries:1 escapes:stored"
      "arrayFrameOverwrite arr 1 0 1 0 0 temporaries:1 escapes:passed"
      "arrayFrameLoadedAlias obj 1 1 0 0 0 - confined"
      "arrayFrameLoadedAlias arr 1 0 1 0 0 temporaries:1 escapes:stored"
      "arrayFrameLoadedAlias arr 1 0 1 0 0 temporaries:1 escapes:passed"
      "arrayFramePublished obj 1 0 1 0 0 globals:1 escapes:stored"
      "arrayFramePublished arr 1 0 1 0 0 globals:1 escapes:stored_global"
      "arrayFrameCall obj 1 0 1 0 0 globals:1 escapes:stored"
      "arrayFrameCall arr 1 0 1 0 0 globals:1 escapes:passed"
      "arrayFrameCycle arr 1 1 0 0 0 - escapes:passed"
      "arrayFrameTransientCycle arr 1 1 0 0 0 - escapes:passed")
  list(SORT _array_frame_rows)
  list(SORT _expected_array_frame_rows)
  if(NOT _array_frame_rows STREQUAL _expected_array_frame_rows)
    message(FATAL_ERROR "imported array-frame evidence mismatch:\nexpected: ${_expected_array_frame_rows}\nobserved: ${_array_frame_rows}")
  endif()
  message(STATUS "imported array frames: nineteen sites, twenty-one instances, eleven retained; live claims agree")

  # Exact original String spellings identify dense elements without changing
  # ownership: saved reads retain their child, and "00" remains a named key.
  set(_expected_canonical_string_rows
      "canonicalStringReleased obj 1 1 0 0 0 - confined"
      "canonicalStringReleased arr 1 0 1 0 0 temporaries:1 escapes:passed"
      "canonicalStringSaved obj 1 0 1 0 0 temporaries:1 escapes:stored"
      "canonicalStringSaved arr 1 1 0 0 0 - escapes:passed"
      "canonicalStringLookalike obj 1 0 1 0 0 temporaries:1 escapes:stored"
      "canonicalStringLookalike arr 1 0 1 0 0 temporaries:1 escapes:passed"
      "canonicalStringLoaded obj 1 1 0 0 0 - confined"
      "canonicalStringLoaded arr 1 0 1 0 0 temporaries:1 escapes:passed"
      "canonicalStringLoaded arr 1 1 0 0 0 - escapes:passed"
      "canonicalStringLoaded arr 1 0 1 0 0 temporaries:1 escapes:returned")
  set(_expected_canonical_string_literal_pcs
      "canonicalStringReleased obj 2" "canonicalStringReleased arr 4"
      "canonicalStringSaved obj 3" "canonicalStringSaved arr 5"
      "canonicalStringLookalike obj 2" "canonicalStringLookalike arr 4"
      "canonicalStringLoaded obj 5" "canonicalStringLoaded arr 7"
      "canonicalStringLoaded arr 11" "canonicalStringLoaded arr 27")
  list(SORT _canonical_string_rows)
  list(SORT _expected_canonical_string_rows)
  list(SORT _canonical_string_literal_pcs)
  list(SORT _expected_canonical_string_literal_pcs)
  if(NOT _canonical_string_rows STREQUAL _expected_canonical_string_rows OR
      NOT _canonical_string_literal_pcs STREQUAL _expected_canonical_string_literal_pcs)
    message(FATAL_ERROR "canonical String index evidence mismatch:\nexpected: ${_expected_canonical_string_rows}\nobserved: ${_canonical_string_rows}\nliteral PCs: ${_canonical_string_literal_pcs}")
  endif()
  message(STATUS "canonical String indices: ten literal sites, four confined, six retained; saved identity and live claims agree")

  # Original decimal BigInts reach the same dense slots after primitive key
  # conversion. The negative computed key cannot release the original child.
  set(_expected_decimal_bigint_rows
      "decimalBigIntReleased obj 1 1 0 0 0 - confined"
      "decimalBigIntReleased arr 1 0 1 0 0 temporaries:1 escapes:passed"
      "decimalBigIntSaved obj 1 0 1 0 0 temporaries:1 escapes:stored"
      "decimalBigIntSaved arr 1 1 0 0 0 - escapes:passed"
      "decimalBigIntSecond obj 1 1 0 0 0 - confined"
      "decimalBigIntSecond arr 1 0 1 0 0 temporaries:1 escapes:passed"
      "decimalBigIntLoaded obj 1 1 0 0 0 - confined"
      "decimalBigIntLoaded arr 1 0 1 0 0 temporaries:1 escapes:passed"
      "decimalBigIntLoaded arr 1 1 0 0 0 - escapes:passed"
      "decimalBigIntLoaded arr 1 0 1 0 0 temporaries:1 escapes:returned"
      "decimalBigIntNegative obj 1 0 1 0 0 temporaries:1 escapes:stored"
      "decimalBigIntNegative arr 1 0 1 0 0 temporaries:1 escapes:passed")
  set(_expected_decimal_bigint_literal_pcs
      "decimalBigIntReleased obj 2" "decimalBigIntReleased arr 4"
      "decimalBigIntSaved obj 3" "decimalBigIntSaved arr 5"
      "decimalBigIntSecond obj 2" "decimalBigIntSecond arr 4"
      "decimalBigIntLoaded obj 5" "decimalBigIntLoaded arr 7"
      "decimalBigIntLoaded arr 11" "decimalBigIntLoaded arr 27"
      "decimalBigIntNegative obj 2" "decimalBigIntNegative arr 4")
  list(SORT _decimal_bigint_rows)
  list(SORT _expected_decimal_bigint_rows)
  list(SORT _decimal_bigint_literal_pcs)
  list(SORT _expected_decimal_bigint_literal_pcs)
  if(NOT _decimal_bigint_rows STREQUAL _expected_decimal_bigint_rows OR
      NOT _decimal_bigint_literal_pcs STREQUAL _expected_decimal_bigint_literal_pcs)
    message(FATAL_ERROR "decimal BigInt index evidence mismatch:\nexpected: ${_expected_decimal_bigint_rows}\nobserved: ${_decimal_bigint_rows}\nliteral PCs: ${_decimal_bigint_literal_pcs}")
  endif()
  message(STATUS "decimal BigInt indices: twelve literal sites, five confined, seven retained; saved identity and live claims agree")

  # A length read returns an independent Number, never the child in slot zero.
  # Computed indices and writes to length retain their conservative claims.
  set(_expected_dense_length_rows
      "denseLengthReleased obj 1 1 0 0 0 - confined"
      "denseLengthReleased arr 1 0 1 0 0 temporaries:1 escapes:passed"
      "denseLengthReleased arr 1 0 1 0 0 temporaries:1 escapes:returned"
      "denseLengthSaved obj 1 0 1 0 0 temporaries:1 escapes:stored"
      "denseLengthSaved arr 1 0 1 0 0 temporaries:1 escapes:passed"
      "denseLengthSaved arr 1 0 1 0 0 temporaries:1 escapes:returned"
      "denseLengthLoaded obj 1 1 0 0 0 - confined"
      "denseLengthLoaded arr 1 0 1 0 0 temporaries:1 escapes:passed"
      "denseLengthLoaded arr 1 1 0 0 0 - escapes:passed"
      "denseLengthLoaded arr 1 0 1 0 0 temporaries:1 escapes:returned"
      "denseLengthEmpty obj 1 1 0 0 0 - confined"
      "denseLengthEmpty arr 1 1 0 0 0 - escapes:passed"
      "denseLengthEmpty arr 1 0 1 0 0 temporaries:1 escapes:passed"
      "denseLengthEmpty arr 1 0 1 0 0 temporaries:1 escapes:returned"
      "denseLengthIndexed obj 1 1 0 0 0 - escapes:stored"
      "denseLengthIndexed arr 1 0 1 0 0 temporaries:1 escapes:passed"
      "denseLengthIndexed arr 1 0 1 0 0 temporaries:1 escapes:returned"
      "denseLengthChanged obj 1 1 0 0 0 - escapes:stored"
      "denseLengthChanged arr 1 0 1 0 0 temporaries:1 escapes:passed"
      "denseLengthChanged arr 1 0 1 0 0 temporaries:1 escapes:returned")
  set(_expected_dense_length_literal_pcs
      "denseLengthReleased obj 3" "denseLengthReleased arr 5" "denseLengthReleased arr 16"
      "denseLengthSaved obj 4" "denseLengthSaved arr 6" "denseLengthSaved arr 21"
      "denseLengthLoaded obj 5" "denseLengthLoaded arr 7"
      "denseLengthLoaded arr 11" "denseLengthLoaded arr 31"
      "denseLengthEmpty obj 4" "denseLengthEmpty arr 6"
      "denseLengthEmpty arr 8" "denseLengthEmpty arr 19"
      "denseLengthIndexed obj 3" "denseLengthIndexed arr 5" "denseLengthIndexed arr 18"
      "denseLengthChanged obj 3" "denseLengthChanged arr 5" "denseLengthChanged arr 15")
  list(SORT _dense_length_rows)
  list(SORT _expected_dense_length_rows)
  list(SORT _dense_length_literal_pcs)
  list(SORT _expected_dense_length_literal_pcs)
  if(NOT _dense_length_rows STREQUAL _expected_dense_length_rows OR
      NOT _dense_length_literal_pcs STREQUAL _expected_dense_length_literal_pcs)
    message(FATAL_ERROR "dense length evidence mismatch:\nexpected: ${_expected_dense_length_rows}\nobserved: ${_dense_length_rows}\nliteral PCs: ${_dense_length_literal_pcs}")
  endif()
  message(STATUS "dense array length: twenty literal sites, seven confined, thirteen retained; saved length and child claims agree")

  # The returned object loses its child, but a saved read retains that child.
  # The compiler now refuses the preceding ordinary-object writes, including
  # the deleted self-cycle's earlier receiver exposure.
  set(_expected_object_deletion_rows
      "objectFrameDeletedChild 1 1 0 0 0 - escapes:stored"
      "objectFrameDeletedChild 1 0 1 0 0 temporaries:1 escapes:passed"
      "objectFrameDeletedSavedRead 1 0 1 0 0 temporaries:1 escapes:stored"
      "objectFrameDeletedSavedRead 1 1 0 0 0 - escapes:passed"
      "objectFrameDeletedTransientCycle 1 1 0 0 0 - escapes:passed")
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
      "objectFrameCopiedChild 1 1 0 0 0 - escapes:passed"
      "objectFrameCopiedChild 1 0 1 0 0 temporaries:1 escapes:returned"
      "objectFrameCopiedOverwrite 1 1 0 0 0 - escapes:stored"
      "objectFrameCopiedOverwrite 1 0 1 0 0 temporaries:1 escapes:stored"
      "objectFrameCopiedOverwrite 1 1 0 0 0 - escapes:passed"
      "objectFrameCopiedOverwrite 1 0 1 0 0 temporaries:1 escapes:passed"
      "objectFrameCopiedSavedRead 1 0 1 0 0 temporaries:1 escapes:stored"
      "objectFrameCopiedSavedRead 1 1 0 0 0 - escapes:passed"
      "objectFrameCopiedSavedRead 1 1 0 0 0 - escapes:passed")
  list(SORT _object_copy_rows)
  list(SORT _expected_object_copy_rows)
  if(NOT _object_copy_rows STREQUAL _expected_object_copy_rows)
    message(FATAL_ERROR "imported object copy evidence mismatch:\nexpected: ${_expected_object_copy_rows}\nobserved: ${_object_copy_rows}")
  endif()
  message(STATUS "imported object copy: ten sites, ten instances, five retained; live claims agree")

  # These unchanged alias and source-switch cases preserve every observed
  # retention path. Ordinary-object writes now refuse before selector proofs
  # can refine their claims, including the deleted replacements.
  set(_expected_object_copy_path_rows
      "objectFrameCopiedConditionalAlias 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameCopiedConditionalAlias 2 2 0 0 0 - escapes:passed"
      "objectFrameCopiedConditionalAlias 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameCopiedConditionalAlias 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameCopiedConditionalAlias 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameCopiedConditionalSource 2 1 1 0 0 temporaries:1 escapes:passed"
      "objectFrameCopiedConditionalSource 2 1 1 0 0 temporaries:1 escapes:passed"
      "objectFrameCopiedConditionalSource 2 2 0 0 0 - escapes:passed"
      "objectFrameCopiedConditionalSource 2 2 0 0 0 - escapes:passed"
      "objectFrameCopiedConditionalSource 2 0 2 0 0 temporaries:2 escapes:returned"
      "objectFrameCopiedSwitchSaved 3 0 3 0 0 temporaries:3 escapes:passed"
      "objectFrameCopiedSwitchSaved 3 1 2 0 0 temporaries:2 escapes:passed"
      "objectFrameCopiedSwitchSaved 3 2 1 0 0 temporaries:1 escapes:passed"
      "objectFrameCopiedSwitchSaved 3 2 1 0 0 temporaries:1 escapes:passed"
      "objectFrameCopiedSwitchSaved 1 0 1 0 0 temporaries:1 escapes:passed"
      "objectFrameCopiedSwitchSaved 1 0 1 0 0 temporaries:1 escapes:passed"
      "objectFrameCopiedSwitchSaved 1 0 1 0 0 temporaries:1 escapes:passed"
      "objectFrameCopiedLiteralOverwrite 1 1 0 0 0 - escapes:passed"
      "objectFrameCopiedLiteralOverwrite 1 0 1 0 0 temporaries:1 escapes:passed"
      "objectFrameCopiedLiteralOverwrite 1 1 0 0 0 - escapes:passed"
      "objectFrameCopiedLiteralOverwrite 1 1 0 0 0 - escapes:passed")
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
      "objectFrameSwitchReleased 3 3 0 0 0 - escapes:passed"
      "objectFrameSwitchReleased 3 2 1 0 0 temporaries:1 escapes:passed"
      "objectFrameSwitchReleased 3 2 1 0 0 temporaries:1 escapes:passed"
      "objectFrameSwitchReleased 1 0 1 0 0 temporaries:1 escapes:passed")
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
      "objectFrameNegatedReleased 4 4 0 0 0 - escapes:passed"
      "objectFrameNegatedReleased 4 0 4 0 0 temporaries:4 escapes:stored"
      "objectFrameNegatedReleased 4 0 4 0 0 temporaries:4 escapes:passed"
      "objectFrameNegatedReleased 4 0 4 0 0 temporaries:4 escapes:passed")
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
      "objectFrameTypeofReleased 5 5 0 0 0 - escapes:passed"
      "objectFrameTypeofReleased 5 0 5 0 0 temporaries:5 escapes:stored"
      "objectFrameTypeofReleased 5 0 5 0 0 temporaries:5 escapes:passed"
      "objectFrameTypeofReleased 5 0 5 0 0 temporaries:5 escapes:passed"
      "objectFrameVoidReleased 2 2 0 0 0 - escapes:passed"
      "objectFrameVoidReleased 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameVoidReleased 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameVoidReleased 2 0 2 0 0 temporaries:2 escapes:passed")
  list(SORT _object_total_unary_rows)
  list(SORT _expected_object_total_unary_rows)
  if(NOT _object_total_unary_rows STREQUAL _expected_object_total_unary_rows)
    message(FATAL_ERROR "imported total unary evidence mismatch:\nexpected: ${_expected_object_total_unary_rows}\nobserved: ${_object_total_unary_rows}")
  endif()
  message(STATUS "imported typeof/void: eight sites, twenty-eight instances, twenty-one retained; live claims agree")

  # Preserve the original Number/BigInt sources and observed lifetimes.
  # Primitive category proofs do not prove these writes create own properties.
  set(_expected_object_static_binary_rows
      "objectFrameStaticBinaryReleased 2 2 0 0 0 - escapes:passed"
      "objectFrameStaticBinaryReleased 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameStaticBinaryReleased 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameStaticBinaryReleased 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameStaticBinaryOpaque 2 2 0 0 0 - escapes:passed"
      "objectFrameStaticBinaryOpaque 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameStaticBinaryOpaque 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameStaticBinaryOpaque 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameStaticBinaryBigInt 1 1 0 0 0 - escapes:passed"
      "objectFrameStaticBinaryBigInt 1 0 1 0 0 temporaries:1 escapes:stored"
      "objectFrameStaticBinaryBigInt 1 0 1 0 0 temporaries:1 escapes:passed"
      "objectFrameStaticBinaryBigInt 1 0 1 0 0 temporaries:1 escapes:passed")
  list(SORT _object_static_binary_rows)
  list(SORT _expected_object_static_binary_rows)
  if(NOT _object_static_binary_rows STREQUAL _expected_object_static_binary_rows)
    message(FATAL_ERROR "imported static binary evidence mismatch:\nexpected: ${_expected_object_static_binary_rows}\nobserved: ${_object_static_binary_rows}")
  endif()
  message(STATUS "imported static binary: twelve sites, twenty instances, fifteen retained; live claims agree")

  # Saved primitive values survive a BigInt field overwrite at runtime.
  # Their categories do not authorize ordinary-object contents refinement.
  set(_expected_object_arithmetic_unary_rows
      "objectFrameArithmeticUnaryReleased 2 2 0 0 0 - escapes:passed"
      "objectFrameArithmeticUnaryReleased 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameArithmeticUnaryReleased 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameArithmeticUnaryReleased 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameArithmeticUnarySaved 2 2 0 0 0 - escapes:passed"
      "objectFrameArithmeticUnarySaved 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameArithmeticUnarySaved 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameArithmeticUnarySaved 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameArithmeticUnaryOpaque 2 2 0 0 0 - escapes:passed"
      "objectFrameArithmeticUnaryOpaque 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameArithmeticUnaryOpaque 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameArithmeticUnaryOpaque 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameArithmeticUnaryBigInt 1 1 0 0 0 - escapes:passed"
      "objectFrameArithmeticUnaryBigInt 1 0 1 0 0 temporaries:1 escapes:stored"
      "objectFrameArithmeticUnaryBigInt 1 0 1 0 0 temporaries:1 escapes:passed"
      "objectFrameArithmeticUnaryBigInt 1 0 1 0 0 temporaries:1 escapes:passed")
  list(SORT _object_arithmetic_unary_rows)
  list(SORT _expected_object_arithmetic_unary_rows)
  if(NOT _object_arithmetic_unary_rows STREQUAL _expected_object_arithmetic_unary_rows)
    message(FATAL_ERROR "imported arithmetic unary evidence mismatch:\nexpected: ${_expected_object_arithmetic_unary_rows}\nobserved: ${_object_arithmetic_unary_rows}")
  endif()
  message(STATUS "imported arithmetic unary: sixteen sites, twenty-eight instances, twenty-one retained; live claims agree")

  # The original Released witness loads global undefined; Literal changes only
  # that input to void 0. Preserve both sources and their measured lifetimes;
  # neither successful comparisons nor primitive origins prove own-data writes.
  set(_expected_object_loose_equality_rows
      "objectFrameLooseEqualityReleased 2 2 0 0 0 - escapes:passed"
      "objectFrameLooseEqualityReleased 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameLooseEqualityReleased 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameLooseEqualityReleased 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameLooseEqualitySaved 2 2 0 0 0 - escapes:passed"
      "objectFrameLooseEqualitySaved 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameLooseEqualitySaved 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameLooseEqualitySaved 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameLooseEqualityOpaque 2 2 0 0 0 - escapes:passed"
      "objectFrameLooseEqualityOpaque 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameLooseEqualityOpaque 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameLooseEqualityOpaque 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameLooseEqualityBigInt 1 1 0 0 0 - escapes:passed"
      "objectFrameLooseEqualityBigInt 1 0 1 0 0 temporaries:1 escapes:stored"
      "objectFrameLooseEqualityBigInt 1 0 1 0 0 temporaries:1 escapes:passed"
      "objectFrameLooseEqualityBigInt 1 0 1 0 0 temporaries:1 escapes:passed"
      "objectFrameLooseEqualityRelational 2 2 0 0 0 - escapes:passed"
      "objectFrameLooseEqualityRelational 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameLooseEqualityRelational 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameLooseEqualityRelational 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameLooseEqualityLiteral 2 2 0 0 0 - escapes:passed"
      "objectFrameLooseEqualityLiteral 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameLooseEqualityLiteral 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameLooseEqualityLiteral 2 0 2 0 0 temporaries:2 escapes:passed")
  list(SORT _object_loose_equality_rows)
  list(SORT _expected_object_loose_equality_rows)
  if(NOT _object_loose_equality_rows STREQUAL _expected_object_loose_equality_rows)
    message(FATAL_ERROR "imported loose equality evidence mismatch:\nexpected: ${_expected_object_loose_equality_rows}\nobserved: ${_object_loose_equality_rows}")
  endif()
  message(STATUS "imported loose equality: twenty-four sites, forty-four instances, thirty-three retained; live claims agree")

  # Saved primitive values survive later BigInt field replacement/deletion;
  # invalid numeric Strings and Undefined give unordered comparisons.
  # Returning the old child retains it after both container fields are deleted.
  # These runtime observations do not discharge the earlier property sinks.
  set(_expected_object_relational_rows
      "objectFrameRelationalSaved 2 2 0 0 0 - escapes:passed"
      "objectFrameRelationalSaved 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameRelationalSaved 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameRelationalSaved 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameRelationalUnordered 2 2 0 0 0 - escapes:passed"
      "objectFrameRelationalUnordered 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameRelationalUnordered 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameRelationalUnordered 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameRelationalOpaque 2 2 0 0 0 - escapes:passed"
      "objectFrameRelationalOpaque 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameRelationalOpaque 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameRelationalOpaque 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameRelationalBigInt 2 2 0 0 0 - escapes:passed"
      "objectFrameRelationalBigInt 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameRelationalBigInt 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameRelationalBigInt 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameRelationalRetained 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameRelationalRetained 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameRelationalRetained 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameRelationalRetained 2 0 2 0 0 temporaries:2 escapes:passed")
  list(SORT _object_relational_rows)
  list(SORT _expected_object_relational_rows)
  if(NOT _object_relational_rows STREQUAL _expected_object_relational_rows)
    message(FATAL_ERROR "imported relational evidence mismatch:\nexpected: ${_expected_object_relational_rows}\nobserved: ${_object_relational_rows}")
  endif()
  message(STATUS "imported relational comparisons: twenty sites, forty instances, thirty-two retained; live claims agree")

  # Preserve numeric/BigInt arithmetic and the saved child's observed identity.
  # Ordinary-object writes refuse independently of primitive producer categories.
  set(_expected_object_arithmetic_binary_rows
      "objectFrameArithmeticBinarySaved 2 2 0 0 0 - escapes:passed"
      "objectFrameArithmeticBinarySaved 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameArithmeticBinarySaved 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameArithmeticBinarySaved 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameArithmeticBinaryNumbers 2 2 0 0 0 - escapes:passed"
      "objectFrameArithmeticBinaryNumbers 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameArithmeticBinaryNumbers 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameArithmeticBinaryNumbers 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameArithmeticBinaryOpaque 2 2 0 0 0 - escapes:passed"
      "objectFrameArithmeticBinaryOpaque 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameArithmeticBinaryOpaque 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameArithmeticBinaryOpaque 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameArithmeticBinaryBigInt 2 2 0 0 0 - escapes:passed"
      "objectFrameArithmeticBinaryBigInt 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameArithmeticBinaryBigInt 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameArithmeticBinaryBigInt 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameArithmeticBinaryRetained 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameArithmeticBinaryRetained 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameArithmeticBinaryRetained 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameArithmeticBinaryRetained 2 0 2 0 0 temporaries:2 escapes:passed")
  list(SORT _object_arithmetic_binary_rows)
  list(SORT _expected_object_arithmetic_binary_rows)
  if(NOT _object_arithmetic_binary_rows STREQUAL _expected_object_arithmetic_binary_rows)
    message(FATAL_ERROR "imported arithmetic binary evidence mismatch:\nexpected: ${_expected_object_arithmetic_binary_rows}\nobserved: ${_object_arithmetic_binary_rows}")
  endif()
  message(STATUS "imported arithmetic binary: twenty sites, forty instances, thirty-two retained; live claims agree")

  # Number/String addition, template conversion and BigInt controls keep their
  # original observed lifetimes. An independently returned child remains retained.
  set(_expected_object_add_concat_rows
      "objectFrameAddConcatSaved 2 2 0 0 0 - escapes:passed"
      "objectFrameAddConcatSaved 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameAddConcatSaved 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameAddConcatSaved 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameAddConcatNumbers 2 2 0 0 0 - escapes:passed"
      "objectFrameAddConcatNumbers 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameAddConcatNumbers 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameAddConcatNumbers 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameAddConcatTemplate 2 2 0 0 0 - escapes:passed"
      "objectFrameAddConcatTemplate 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameAddConcatTemplate 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameAddConcatTemplate 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameAddConcatOpaqueAdd 2 2 0 0 0 - escapes:passed"
      "objectFrameAddConcatOpaqueAdd 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameAddConcatOpaqueAdd 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameAddConcatOpaqueAdd 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameAddConcatOpaqueTemplate 2 2 0 0 0 - escapes:passed"
      "objectFrameAddConcatOpaqueTemplate 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameAddConcatOpaqueTemplate 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameAddConcatOpaqueTemplate 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameAddConcatBigInt 2 2 0 0 0 - escapes:passed"
      "objectFrameAddConcatBigInt 2 0 2 0 0 temporaries:2 escapes:stored"
      "objectFrameAddConcatBigInt 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameAddConcatBigInt 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameAddConcatRetained 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameAddConcatRetained 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameAddConcatRetained 2 0 2 0 0 temporaries:2 escapes:passed"
      "objectFrameAddConcatRetained 2 0 2 0 0 temporaries:2 escapes:passed")
  list(SORT _object_add_concat_rows)
  list(SORT _expected_object_add_concat_rows)
  if(NOT _object_add_concat_rows STREQUAL _expected_object_add_concat_rows)
    message(FATAL_ERROR "imported Add/Concat evidence mismatch:\nexpected: ${_expected_object_add_concat_rows}\nobserved: ${_object_add_concat_rows}")
  endif()
  message(STATUS "imported Add/Concat: twenty-eight sites, fifty-six instances, forty-four retained; live claims agree")

  # Exact saved BigInt pairs compare without user conversion. A separately
  # returned child remains reachable after both owning fields have been deleted;
  # all preceding ordinary-object writes keep their conservative claims.
