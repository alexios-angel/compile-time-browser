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
