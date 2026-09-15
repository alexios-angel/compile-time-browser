  # This source-backed RangeError is allocated by the VM at the signed shift,
  # not by a source object literal. Pin the exact body and measured bytecode pc
  # independently from every literal site's mandatory compiler claim below.
  file(READ "${_corpus}" _fixture_source)
  string(REGEX MATCH "function objectFrameBigIntShiftEarly\\(choice\\) \\{[^\n]*\n(    [^\n]*\n)*\\}" _shift_error_source "${_fixture_source}")
  string(SHA256 _shift_error_hash "${_shift_error_source}")
  if(NOT _shift_error_hash STREQUAL "ad0d55f50570016466a7d91649e25508e777398c8d4e4255592acd1472fcb9eb")
    message(FATAL_ERROR "the signed-shift exception source changed; remeasure its bytecode coordinate before updating this case")
  endif()
  # Div and Mod have distinct error-producing source operations. Their fixed
  # zero-divisor errors must be recorded independently of the literal sites.
  foreach(_kind Div Mod)
    string(REGEX MATCH "function objectFrameBigIntDivMod${_kind}Early\\(choice\\) \\{[^\n]*\n(    [^\n]*\n)*\\}" _divmod_error_source "${_fixture_source}")
    string(SHA256 _divmod_error_hash "${_divmod_error_source}")
    if(_kind STREQUAL "Div")
      set(_expected_divmod_error_hash "6c68903dc496b7644562597929ae68c21e34728e9b17c4229aac42eafab23f48")
    else()
      set(_expected_divmod_error_hash "cd95b19bbca5f3678ae266a056844bf2abf70802499fbf9841992f697e231b3d")
    endif()
    if(NOT _divmod_error_hash STREQUAL _expected_divmod_error_hash)
      message(FATAL_ERROR "the BigInt ${_kind} exception source changed; remeasure its bytecode coordinate before updating this case")
    endif()
  endforeach()
  # Pow errors come from exact negative and VM-capped exponent operations.
  # Pin source bodies and measured coordinates separately from source literals.
  string(REGEX MATCH "function objectFrameBigIntPowNegativeEarly\\(choice\\) \\{[^\n]*\n(    [^\n]*\n)*\\}" _pow_error_source "${_fixture_source}")
  string(SHA256 _pow_error_hash "${_pow_error_source}")
  if(NOT _pow_error_hash STREQUAL "bcd270c0edaec69b4a00bf1b8b615094ee15efb18bc1ed2056ad503edc9d2258")
    message(FATAL_ERROR "the BigInt Pow Negative source changed; remeasure its bytecode coordinate before updating this case")
  endif()
  string(REGEX MATCH "function objectFrameBigIntPowCapEarly\\(choice\\) \\{[^\n]*\n(    [^\n]*\n)*\\}" _pow_error_source "${_fixture_source}")
  string(SHA256 _pow_error_hash "${_pow_error_source}")
  if(NOT _pow_error_hash STREQUAL "c7bb794736cdd22418caaafe2200e405ea6c174e8d0210f0f2883280e060fad1")
    message(FATAL_ERROR "the BigInt Pow Cap source changed; remeasure its bytecode coordinate before updating this case")
  endif()
  # Every String/BigInt producer, mixed comparison and promoted historical body
  # is pinned independently; retention never authorizes a comparison value.
  foreach(_source_pair IN ITEMS
      "denseLengthChanged 1e78b3b9807649f47a64219b19fd901c7fd3e02c92bf5045945edf6a6ee1d1e6"
      "denseLengthIndexed 4c3c2e02a61b47826cf710db4c6c8db697ce51f68c2d2c613add016962377c1c"
      "denseIndexSaved 8079e162d26f322849875baadb33c1bdb2eb96ae0e71d4556efa36bfa10b7ffc"
      "denseIndexLoaded b39f30b6bd61526fc624ac8e8c2b154d92b88aa5abf284dd753b060ecb8ade3a"
      "denseIndexPaths 7e6967e5788ecfb93e86c39a23da9066084147836a9c24a0a714f5186a18c83a"
      "denseIndexStringOffset c0df7840d662042de1d6d7303ae4a24e896a4ba988979b080312c96181c0faeb"
      "denseIndexChain 36147bc7c02a94f30077e3af3b4ab41bb117d3080951eb704997afc250c48332"
      "radixBigIntReleased 97e1f7e347f64d37d20671f08e5b81be7294557464528edfeb8beb67e974d7f7"
      "radixBigIntSaved e7c7753b23f82e5cd24eeaf0e213c166ba1c0202f53180a274702480c1ea4370"
      "radixBigIntLoaded 5edea408be7cd912c643ad285bb4e92049fc0008535399436b8c7a4aba18926b"
      "radixBigIntString 72166baa7515a0c1586913b240e15b19d0995c8047d1b2d6fcf9036ce8a36172"
      "radixBigIntComputed 3c98ea19db55e09fd116befca52ad159521d7c46e5a6cfc18a44935392d95051"
      "decimalBigIntReleased 914279663a7bc5e6287ce35cda705f0bb77ca7d22cca1ce55a43ab1f48c225be"
      "decimalBigIntSaved 50e976dcfe248e24f2b0df3b9ea14ee72ba93de3c50a975f7b8d268adcba1aab"
      "decimalBigIntSecond b49d40863cd7a70a06c461a01191075846abcda76d5dcf89a57d9edec8b67b30"
      "decimalBigIntLoaded a16025cf0989ac816c72e1c9dc4c2ddec4dadaa389cd3534b73ebecd643e7d6a"
      "decimalBigIntNegative 0d9838a9606e12f668a56a97673d3381c6ca83d22a893337b9158a4ccc9afd78"
      "canonicalStringReleased 24e22951addc7eb5216762324681c87e1b3769a7f9c420b78fd3a2713d109286"
      "canonicalStringSaved 4733c444cdf812643b140317e78ab082e667f1ea798f1225e4766eb5e455a5c2"
      "canonicalStringLookalike 251bc9aa4ed08015532045c04cfefcf8ecf58dd9d2380fc68bf73ce3100a3234"
      "canonicalStringLoaded 8ee79dc39e35973d6a745ad00f533cfefc1292131c8ec8c8fdc98b0c09fe2230"
      "primitiveMixedAddEarly fe25b7a99e32b3ce5c6509779c227c72a03925414a060ef3382735f353f2f038"
      "primitiveMixedAddRetained 89c4002320aa174921bc9cc64b5315fc6368a3c037574414abd4bfabecf52e46"
      "primitiveMixedAddOpaque a41e97b69d6dc2cdb44682b9dd376f577983a1d79d0208736199646d7689017f"
      "primitiveMixedStaticEarly 7e83a0d3796792b7b773a61337bf8b6a1b6de47c6a67946ed2dab1a3caad69d6"
      "primitiveMixedStaticRetained 65b3a4defa77686a34062fd0afddfde4ec0c93bb1169d5b41ea93e453283fe5c"
      "primitiveMixedStaticOpaque 551d5fddaa481986e811432530bad60f2f5f8947a510fb103c9f003ad913ae22"
      "primitiveUShrEarly afb11ce0d3a043dda6e6a150c33c2850de81f77269f8a7e519b6da8b4e291063"
      "primitiveUShrRetained 1138c74617b2497738b48d1cb980fa5cfa56aa2b9ffa100d0e318989014ff0bf"
      "primitiveUShrOpaque c4ebfb2bb1e31be64214d53da3ffa89b10d362d084471981a6a2491f8f305aae"
      "primitiveMixedModOpaque a3ed23568ed66f6f1975ede1e7175a8fadf0454fb32ca79273a4785833f6cecd"
      "primitiveMixedModRetained 7b9399eabda22f9067c5b178bf2ef6051c32604e98d77a407d9e57eadc4f6ec7"
      "primitiveMixedModEarly 3819c8237d30be90232ab71288c553a5662f66231f9d0939c1b20212bb752cf7"
      "primitiveMixedDivEarly e39cf16b5597d88cea3d89ec61baa8807a0bfa34616c7ddec72e905a09ed7c57"
      "primitiveMixedDivRetained 5b6048cca0395f437c99bbdfcc51634700149658b24d104ac094ff51512d093f"
      "primitiveMixedDivOpaque 429af3d520aaf67bdb16f78260a32ab61f354c75750aaf75b0731e3f41dadb16"
      "primitiveMixedMulEarly dae55b8bbc2882f08286ab4b44166491ef05a57a60748ce7a9e235e055fea62c"
      "primitiveMixedMulRetained 0c2db9da890fa70d87e1a6cb3c872b5fbefedb4135b78a65cc9c5284b3d17933"
      "primitiveMixedMulOpaque 2dc20f97857966611bfc4f0e58dbe72b6c92adfaab04cacca7c69ae10dc8b6a7"
      "primitiveMixedSubEarly 29a6d5a4a64bfebbd6df2c925df3cd2d7e31695a2e3374c4ebf27a158d21b937"
      "primitiveMixedSubRetained e9239bbdcdef56973b2600ef1e1fdb3af3633ed68648712bfb1cbc8cc17477df"
      "primitiveMixedSubOpaque 5e671465270d8463afca298605711d61a7226ddf17cdfbb73b1020551abffb81"
      "primitivePlusEarly 0fb234080a5880d0e70dc249dcf1869f84a109a7abb8e37351352d9b0351f245"
      "primitivePlusRetained 6664a92269834150087224c4baf22d3a9ecba6f48a382c3a50749a16da8110b8"
      "primitivePlusOpaque d1eb05647230b75b09975d3c6cfc4c9208517ba4ffd5c0f2a35f8f2696486e5a"
      "objectFrameStringBigIntSaved 1a2e7c0a1d804774b0b15821ead2780151793b5918012672851be9e951d0a643"
      "objectFrameStringBigIntPaths 2f6a573189564b54d00efe2fb1afea57eab74ccfc50a17ce380a644acf5dec93"
      "objectFrameStringBigIntTemplate 8152fe7cabe9273f204b3633509dc864cd963802cbc94ac488c13ee5f7f1e01d"
      "objectFrameStringBigIntOpaqueAdd 988ba7f5c919a7180ef0a6e46ca1c1fcc82d07815a4262982b888134b960795d"
      "objectFrameStringBigIntOpaqueTemplate 2aa2a84199c6d7fed02a84a21a7fc2aba8b8ee685244e1679a6fcd582678d2d3"
      "objectFrameStringBigIntObject 3d812b5dd00d72d0b950e2c4441d4065d97f70b37c47094175ad7b269ffb47fe"
      "objectFrameStringBigIntMixed c3691179849e139b98c55136d5687fccc8a4b1730864643891a54975668df002"
      "objectFrameStringBigIntRetained ed73af10f5a8c04372701a3e4f9f0782fcde5146f8135fad8f6cb3a1ea1aabc9"
      "objectFrameBigIntMixedSaved 1fdd344a6e0e72da4279670fa28bbc2bf01d227176cbf67eea1a406437a922fc"
      "objectFrameBigIntMixedPrimitives 42b39c5caaff02c34a30b8ea0cd6232790e9468b2fe26f6ef356f323c58dd3f9"
      "objectFrameBigIntMixedPaths 98a4e6d61422325a3cd0483c7db32381f998b71185345927ee39957798491e75"
      "objectFrameBigIntMixedNumbers 689caa156e121e5e5b010b43b76e2d6accc9ea50326c0ccc2f1d7d07c0abbc6c"
      "objectFrameBigIntMixedOpaque 679980f4705d49587e08cf035757f00ac40a967ce47102f184526ca2beb07765"
      "objectFrameBigIntMixedObject fe967af83bf43781bca1ed9acf1167af97af95e365420fe49ed0035ec4b7a6f2"
      "objectFrameBigIntMixedRetained e54918b880a33bf0b1b8590756abacf2ff97b5f728a63e59b5aac5f98781c919"
      "objectFrameBigIntMixedStrings 867a839a1d93289d794c717586421bffe4df5e940476b24273d6b8e5be2ad084"
      "objectFrameLooseEqualityBigInt 9b12443c29cecfadbd38c6510e4e19c52c1b7dd1175244627f79064b13bd533e"
      "objectFrameBigIntEqualityMixed a25b4876302f65466ca10f08322c8d569cbdf3ef52599089f8541d61258d77e9"
      "objectFrameBigIntRelationalMixed 4cef5bd507efbf6846fd5a51603f3837b6653fa1e00fd15905d8f2f06a9cc2ed"
      "objectFrameBigIntUnaryMixed d8d998eb9b7942aede14b87d3ced8bac62defc5a6c995e3e5e7923c01c210356"
      "objectFrameBigIntBinaryMixed 4475cd2c7b523fa002af90325ad4e93b54a869aac109e6a892af6771b528296f"
      "objectFrameBigIntStaticMixed 05624d5cfeeecca9f3c778f70e896bfa6f7a4afb987495ce40ccbf013fba12c0"
      "objectFrameBigIntShiftMixed b2cf97831f8a1418cb99a90ddb673f9b1bd2c31bb6c4a8cec78546b04021e552"
      "objectFrameBigIntDivModMixed c57d6600f4b0265c68ae63fdd7144b7000414e09ab05d6eff0237ccd97841744"
      "objectFrameBigIntPowMixed 62f6b50fc5fb2588c6e0d4bb98e051e5737cb5cba28df1a93cc36cc427277054"
)
    string(REPLACE " " ";" _source_fields "${_source_pair}")
    list(GET _source_fields 0 _source_name)
    list(GET _source_fields 1 _source_hash)
    string(REGEX MATCH "function ${_source_name}\\([^\n]*\\) \\{[^\n]*\n(    [^\n]*\n)*\\}" _source_body "${_fixture_source}")
    string(SHA256 _observed_source_hash "${_source_body}")
    if(NOT _observed_source_hash STREQUAL _source_hash)
      message(FATAL_ERROR "${_source_name}: pinned source changed; preserve or remeasure its evidence")
    endif()
  endforeach()
