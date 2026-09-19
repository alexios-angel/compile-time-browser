from .sources_object_maps import (
    leaf_clear_cases,
    leaf_object_refusals,
    leaf_object_sources,
    leaf_readback_sources,
)
from .sources_scalar_maps import (
    hashlib,
    json,
    quote,
    re,
)

NUMERIC_ENTRY_HISTORY = {
    "local_identity_saved": (
        6,
        6,
        "8f7a762b9b19eadfc3a9cb08f1d9469dc1e7a84da8b25b2dfc735ee05ce9bee9",
    ),
    "local_identity_repeated_keys": (
        8,
        8,
        "379ccc667b2d463c5fbdc531c53a90ec01c7ba7d8ab578ef1493c3e62f4e281e",
    ),
    "local_add_preserve_calls_repair": (
        8,
        8,
        "8e4afab2d13c5d736de6865b3ca06cc5c2360fbf32d757ec1b40b9561a4ffcd8",
    ),
    "local_add_saved_results": (
        8,
        8,
        "d74ae2ee15979027a09d9ab8a786f3d2f2640617acf9979ef00ce32a226f2065",
    ),
    "local_add_number_literal": (
        6,
        6,
        "91f6c981306b32630d4d6de71009be928c346f79fbbb5b0b24202abcab8899a5",
    ),
    "local_add_result_key": (
        8,
        8,
        "431079d361e861ebc26669231751bf4199d5b2c520c729d47a1dcdf6acb9cc81",
    ),
    "local_add_string_control": (
        6,
        6,
        "b4be269c9a52dd9707d8a44164fb6fdfd93bb5e0ee20ad57091f018032a11995",
    ),
    "local_add_object_control": (
        6,
        6,
        "cf1616932bc7c61be88eb6bcd9b27bce08284fde7d30c18c7b70bd758203a000",
    ),
    "local_clear_zero_literal_repair": (
        8,
        8,
        "33aa4c4a24bba6adeec8cc2711e406190f25942f3829ee0c35efae2c9dba36a0",
    ),
    "local_clear_zero_size_key": (
        8,
        8,
        "496635583adf728c73d3a48a71a98d7e4733a2bd9a5d14d17d21b1660359b316",
    ),
    "leaf_object_number_field": (
        7,
        7,
        "80b192281418b92f773f12308bfa3b93ce5160880da6aa3ced1ae29bf1329c3c",
    ),
    "leaf_object_string_field": (
        7,
        7,
        "88d51f7dc833c6e17e1c8bf2e54096a504cc53417541f0ccdda4e66569b5f9e6",
    ),
}


def numeric_entry_cases():
    rows = {}

    def add(name, source, value, boundary, historical=False, repair=None):
        calls = 2 + len(
            re.findall(r"\b(?:state|host\.slot)\.(?:set|get|size|has|delete|clear)\(", source)
        )
        rows[name] = dict(
            source=source,
            expected_trace=value,
            expected_raw_calls=calls,
            historical=historical,
            boundary=boundary,
        )
        if repair:
            old, replacement, target = repair
            assert (
                source.count(old) == 1
                and source.replace(old, replacement) == rows[target]["source"]
            )
            rows[name].update(repair=target, removed_text=old, replacement_text=replacement)

    source, _, value = leaf_readback_sources()["local_identity_saved"]
    add("local_identity_saved", source, value, "original six-call exact one-call repair", True)
    base = source
    source, _, value = leaf_readback_sources()["local_identity_repeated_keys"]
    add(
        "local_identity_repeated_keys",
        source,
        value,
        "original eight-call numeric entry addition",
        True,
        (
            "host.slot.set('x') + host.slot.set('x') + host.slot.set('y')",
            "host.slot.set('x')",
            "local_identity_saved",
        ),
    )
    expression = "var trace = host.slot.set('x') + host.slot.set('x') + host.slot.set('y');"
    assert source.count(expression) == 1
    add(
        "local_add_preserve_calls_repair",
        source.replace(
            expression, "host.slot.set('x'); host.slot.set('x'); var trace = host.slot.set('y');"
        ),
        1,
        "all three original published calls survive; only result addition is removed",
    )
    add(
        "local_add_saved_results",
        source.replace(
            expression,
            "const first = host.slot.set('x'); const second = host.slot.set('x'); "
            "const third = host.slot.set('y'); var trace = first + second + third;",
        ),
        3,
        "separate saved results preserve the exact two-level numeric dependency",
    )
    add(
        "local_add_number_literal",
        base.replace("var trace = host.slot.set('x');", "var trace = host.slot.set('x') + 1;"),
        2,
        "independently proved Number call result plus literal Number",
    )
    add(
        "local_add_result_key",
        base.replace(
            "var trace = host.slot.set('x');",
            "var trace = host.slot.set(host.slot.set(1) + host.slot.set(2));",
        ),
        1,
        "the added Number result becomes a later method argument; host result provenance remains live",
    )
    add(
        "local_add_string_control",
        base.replace(
            "var trace = host.slot.set('x');",
            "var trace = (host.slot.set('x') + '2') === '12' ? 1 : 0;",
        ),
        1,
        "Number/String + is concatenation and must not be classified as numeric addition",
    )
    add(
        "local_add_object_control",
        base.replace(
            "var trace = host.slot.set('x');",
            "var trace = (host.slot.set('x') + {}) === '1[object Object]' ? 1 : 0;",
        ),
        1,
        "an object operand needs observable conversion and cannot supply a Number proof",
    )

    source = leaf_clear_cases()["local_clear_reseed_disjoint"]["source"]
    old = "state.clear(); state.set('other', item); " "return state.get('gone') === void 0 ? 1 : 0;"
    new = (
        "state.clear(); const zero = state.size; state.set(1, item); "
        "return state.get(zero) === void 0 ? 1 : 0;"
    )
    assert source.count(old) == 1
    source = source.replace(old, new).replace("host.slot.set('x')", "host.slot.set(7)")
    repair = source.replace("const zero = state.size;", "state.size; const zero = 0;")
    add(
        "local_clear_zero_literal_repair",
        repair,
        1,
        "retain the evaluated size read and use independent literal-zero key disjointness",
    )
    add(
        "local_clear_zero_size_key",
        source,
        1,
        "clear establishes exact zero size, but existing lower-bound evidence cannot prove zero differs from one",
        repair=(
            "const zero = state.size;",
            "state.size; const zero = 0;",
            "local_clear_zero_literal_repair",
        ),
    )

    source, value, old, new, repair_name, calls = leaf_object_refusals()["leaf_object_string_field"]
    repaired, _, repaired_value = leaf_object_sources()[repair_name]
    add(repair_name, repaired, repaired_value, "original numeric owning leaf-field repair", True)
    add(
        "leaf_object_string_field",
        source,
        value,
        "original String owning leaf-field refusal",
        True,
        (old, new, repair_name),
    )
    assert rows["leaf_object_string_field"]["expected_raw_calls"] == calls

    # Each source still evaluates both calls. The size-returning setter makes
    # subtraction/division/power sensitive to reversing their execution order.
    growing = base.replace("return saved === item ? 1 : 0;", "return state.size;")
    for kind, symbol, value in (
        ("add", "+", 3),
        ("sub", "-", -1),
        ("mul", "*", 2),
        ("div", "/", 0.5),
        ("mod", "%", 1),
        ("pow", "**", 1),
    ):
        add(
            "local_numeric_" + kind,
            growing.replace(
                "var trace = host.slot.set('x');",
                "var trace = host.slot.set('x') " + symbol + " host.slot.set('y');",
            ),
            value,
            "independently Number operands; both mutating calls retain source evaluation order",
        )
    add(
        "local_numeric_nested_key",
        growing.replace(
            "var trace = host.slot.set('x');",
            "var trace = host.slot.set((host.slot.set(1) + host.slot.set(2)) * host.slot.set(3));",
        ),
        4,
        "completed arithmetic feeds a later same-method actual only after all input categories recheck",
    )
    add(
        "local_numeric_nan_key",
        growing.replace(
            "var trace = host.slot.set('x');",
            "host.slot.set(host.slot.size() / host.slot.size()); "
            "var trace = host.slot.set((host.slot.size() - host.slot.size()) / 0);",
        ),
        1,
        "NaN keys retain SameValueZero equality; no observed Number is substituted for the real result",
    )
    add(
        "local_numeric_saved_snapshot",
        growing.replace(
            "var trace = host.slot.set('x');",
            "const first = host.slot.set('x'); const second = host.slot.set('y'); "
            "host.slot.set('z'); var trace = first * 10 + second;",
        ),
        12,
        "saved Number results keep read-time values across later Map growth",
    )
    field = base.replace(
        "set(key) { const item = {};", "set(key, value) { const item = {value: value};"
    ).replace("return saved === item ? 1 : 0;", "state.clear(); return saved.value;")
    expression = "host.slot.set('x', 2) + host.slot.set('y', 3) * host.slot.set('z', 4)"
    field = field.replace("host.slot.set('x')", expression)
    add(
        "local_numeric_saved_lifetime",
        field,
        14,
        "saved Number fields and evaluated arithmetic survive clearing and final owner release",
    )
    conditional = field.replace("set(key, value)", "set(key, value, flag)").replace(
        "state.clear(); return saved.value;",
        "if (flag) { state.clear(); } else { state.delete(key); } return saved.value;",
    )
    conditional = (
        conditional.replace("'x', 2", "'x', 2, false")
        .replace("'y', 3", "'y', 3, false")
        .replace("'z', 4", "'z', 4, false")
    )
    add(
        "local_numeric_branch_lifetime",
        conditional,
        14,
        "both future Boolean arms preserve independently numeric saved fields",
    )
    simple = field.replace(expression, "host.slot.set('x', 2) + 1")
    add(
        "local_numeric_future_number_repair",
        simple.replace("return saved.value;", "return value ? saved.value : 0;"),
        3,
        "both future Number truth arms independently return Number",
    )
    add(
        "local_numeric_future_bool",
        simple.replace("return saved.value;", "return value ? saved.value : false;"),
        3,
        "a truthy startup Number cannot hide the future Boolean result",
        repair=(
            "return value ? saved.value : false;",
            "return value ? saved.value : 0;",
            "local_numeric_future_number_repair",
        ),
    )
    add(
        "local_numeric_later_number_repair",
        simple + "host.slot.set('later', 5);\n",
        3,
        "a later independently numeric actual preserves the whole method family",
    )
    add(
        "local_numeric_later_bool",
        simple + "host.slot.set('later', false);\n",
        3,
        "a later actual invalidates provisional numeric arithmetic facts",
        repair=(
            "host.slot.set('later', false);",
            "host.slot.set('later', 5);",
            "local_numeric_later_number_repair",
        ),
    )
    number_literal = rows["local_add_number_literal"]["source"]
    for tag, literal, value in (
        ("bool", "true", 2),
        ("null", "null", 1),
        ("undefined", "void 0", "NaN"),
    ):
        add(
            "local_numeric_" + tag,
            number_literal.replace(" + 1;", " + " + literal + ";"),
            value,
            "the exact Number pair proof does not admit conversion from " + tag,
            repair=(" + " + literal + ";", " + 1;", "local_add_number_literal"),
        )
    snapshot = rows["local_numeric_saved_snapshot"]["source"]
    old = (
        "const first = host.slot.set('x'); const second = host.slot.set('y'); "
        "host.slot.set('z'); var trace = first * 10 + second;"
    )
    replacement = "var trace = host.slot.set('x') * 10 + host.slot.set('y'); host.slot.set('z');"
    assert snapshot.count(old) == 1
    add(
        "local_numeric_inline_snapshot_repair",
        snapshot.replace(old, replacement),
        12,
        "same arithmetic and x/y/z call order without extra scalar global reads",
    )
    rows["local_numeric_saved_snapshot"].update(
        repair="local_numeric_inline_snapshot_repair",
        removed_text=old,
        replacement_text=replacement,
    )
    old = (
        "const first = host.slot.set('x'); const second = host.slot.set('x'); "
        "const third = host.slot.set('y'); var trace = first + second + third;"
    )
    replacement = "var trace = host.slot.set('x') + host.slot.set('x') + host.slot.set('y');"
    assert rows["local_add_saved_results"]["source"].replace(old, replacement) == (
        rows["local_identity_repeated_keys"]["source"]
    )
    rows["local_add_saved_results"].update(
        repair="local_identity_repeated_keys", removed_text=old, replacement_text=replacement
    )
    for name, row in rows.items():
        raw = prepared = row["expected_raw_calls"]
        if name in NUMERIC_ENTRY_HISTORY:
            raw, prepared, digest = NUMERIC_ENTRY_HISTORY[name]
            assert hashlib.sha256(row["source"].encode()).hexdigest() == digest, name
            assert row["expected_raw_calls"] == raw, name
        row.update(raw_calls=raw, prepared_calls=prepared)
    return rows


NUMERIC_ENTRY_UNOWNED = {
    "local_add_string_control",
    "local_add_object_control",
    "local_numeric_future_bool",
    "local_numeric_later_bool",
    "local_numeric_bool",
    "local_numeric_null",
    "local_numeric_undefined",
}
NUMERIC_ENTRY_SAVED_GLOBALS = {"local_add_saved_results", "local_numeric_saved_snapshot"}
NUMERIC_ENTRY_CARRIERS = set()
NUMERIC_ENTRY_EXISTING_POSITIVES = {
    "local_identity_saved",
    "leaf_object_number_field",
    "leaf_object_string_field",
}
NUMERIC_ENTRY_PROMOTED = {"local_identity_repeated_keys"}


def numeric_entry_sources():
    return {
        name: (row["source"], "host", row["expected_trace"])
        for name, row in numeric_entry_cases().items()
        if name
        not in NUMERIC_ENTRY_UNOWNED | NUMERIC_ENTRY_EXISTING_POSITIVES | NUMERIC_ENTRY_CARRIERS
    }


def numeric_entry_refusals():
    cases = numeric_entry_cases()
    for name, old, replacement, repair in (
        (
            "local_add_string_control",
            "(host.slot.set('x') + '2') === '12' ? 1 : 0",
            "host.slot.set('x') + 1",
            "local_add_number_literal",
        ),
        (
            "local_add_object_control",
            "(host.slot.set('x') + {}) === '1[object Object]' ? 1 : 0",
            "host.slot.set('x') + 1",
            "local_add_number_literal",
        ),
    ):
        row = cases[name]
        assert row["source"].count(old) == 1
        assert row["source"].replace(old, replacement) == cases[repair]["source"]
        row.update(repair=repair, removed_text=old, replacement_text=replacement)
    return {
        name: (
            row["source"],
            row["expected_trace"],
            row["removed_text"],
            row["replacement_text"],
            row["repair"],
            row["prepared_calls"],
        )
        for name, row in cases.items()
        if name in NUMERIC_ENTRY_UNOWNED
    }
