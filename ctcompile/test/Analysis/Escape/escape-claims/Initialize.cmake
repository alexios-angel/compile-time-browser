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
      "decimalBigIntReleased f572b6ce1760bdda19f8c9bcaab6043bf9aecb62aca33c6bedf702429d81013d"
      "decimalBigIntSaved 4ffb288368bcc26312f52a0387b0ee49327abc27279d902b1ea49d8a0a713199"
      "decimalBigIntSecond cf024a9f32b6cc1a7774107154d9c40c0c700f12225c92f28a98c0e746c35840"
      "decimalBigIntLoaded eb43913617979c03f7a2ae3394c7a93e833625b6a7d0cd78d018b90697506f1b"
      "decimalBigIntNegative 447b85cdff1f2d012161929777762175355283f7e339bdbdc8d24741fe7052ea"
      "canonicalStringReleased 54ce66cbdf8d8962681af9bd3eacf21bfb258aef66c803a2d098184e011583b8"
      "canonicalStringSaved 31ec0d6b6ea32c80b0b36f3644f7a3b2940d98cc04c8fbdd5e0cd7202e37a161"
      "canonicalStringLookalike e880b2767d14f1325c77a108a7fe0f99ab3e39e61ba2db91ad0b3c10c4a9f9eb"
      "canonicalStringLoaded d2fa699f558eaefa16321428cb916d5a7b78541cf1d16e172312089119e28902"
      "primitiveMixedAddEarly 279cdc8aa5e952c8eebf022dccf3492862a718a7ccfe1868cb5e8e0941739f43"
      "primitiveMixedAddRetained 9277b49a81538116e6e68b8631d22ee87ffd2cf4bb9611e3a905dc1a9bee2a23"
      "primitiveMixedAddOpaque 30b26142ccb7ef04e7a43e9da55131639bac2fd2b66c641dbe4475203eb1e8fe"
      "primitiveMixedStaticEarly ef3be2df7a0b7d21048156332dbfea4156ae716a2f9ba6280e35a855477b23d6"
      "primitiveMixedStaticRetained ee20ce6b82c2abe74ca91b3da8640924ff165f3ef2ca7466416521dc5f8020e8"
      "primitiveMixedStaticOpaque d5494416f08808ff7cd4ab6fe3bc59e4a13e544f1f853ab042500588a6e90799"
      "primitiveUShrEarly b6707f913a7adb73b19c23001c64461b1b32b837060af34b0623305fce69a37a"
      "primitiveUShrRetained 063e0330b399b9f596651c6bf895629bf12471a82f11ea9b1909a5cf4e758d20"
      "primitiveUShrOpaque d43613d400fcf41ca0b00ffa2673f6d32052827fd53d4ddb03e780a1dc95bc53"
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
      message(FATAL_ERROR "${_source_name}: pinned source changed; preserve or remeasure its evidence")
    endif()
  endforeach()
