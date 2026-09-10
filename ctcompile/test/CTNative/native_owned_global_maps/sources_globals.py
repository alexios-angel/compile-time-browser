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
    'local_identity_saved': (6, 6,
        '8f7a762b9b19eadfc3a9cb08f1d9469dc1e7a84da8b25b2dfc735ee05ce9bee9'),
    'local_identity_repeated_keys': (8, 8,
        '379ccc667b2d463c5fbdc531c53a90ec01c7ba7d8ab578ef1493c3e62f4e281e'),
    'local_add_preserve_calls_repair': (8, 8,
        '8e4afab2d13c5d736de6865b3ca06cc5c2360fbf32d757ec1b40b9561a4ffcd8'),
    'local_add_saved_results': (8, 8,
        'd74ae2ee15979027a09d9ab8a786f3d2f2640617acf9979ef00ce32a226f2065'),
    'local_add_number_literal': (6, 6,
        '91f6c981306b32630d4d6de71009be928c346f79fbbb5b0b24202abcab8899a5'),
    'local_add_result_key': (8, 8,
        '431079d361e861ebc26669231751bf4199d5b2c520c729d47a1dcdf6acb9cc81'),
    'local_add_string_control': (6, 6,
        'b4be269c9a52dd9707d8a44164fb6fdfd93bb5e0ee20ad57091f018032a11995'),
    'local_add_object_control': (6, 6,
        'cf1616932bc7c61be88eb6bcd9b27bce08284fde7d30c18c7b70bd758203a000'),
    'local_clear_zero_literal_repair': (8, 8,
        '33aa4c4a24bba6adeec8cc2711e406190f25942f3829ee0c35efae2c9dba36a0'),
    'local_clear_zero_size_key': (8, 8,
        '496635583adf728c73d3a48a71a98d7e4733a2bd9a5d14d17d21b1660359b316'),
    'leaf_object_number_field': (7, 7,
        '80b192281418b92f773f12308bfa3b93ce5160880da6aa3ced1ae29bf1329c3c'),
    'leaf_object_string_field': (7, 7,
        '88d51f7dc833c6e17e1c8bf2e54096a504cc53417541f0ccdda4e66569b5f9e6'),
}


def numeric_entry_cases():
    rows = {}

    def add(name, source, value, boundary, historical=False, repair=None):
        calls = 2 + len(re.findall(r'\b(?:state|host\.slot)\.(?:set|get|size|has|delete|clear)\(', source))
        rows[name] = dict(source=source, expected_trace=value, expected_raw_calls=calls,
                          historical=historical, boundary=boundary)
        if repair:
            old, replacement, target = repair
            assert source.count(old) == 1 and source.replace(old, replacement) == rows[target]['source']
            rows[name].update(repair=target, removed_text=old, replacement_text=replacement)

    source, _, value = leaf_readback_sources()['local_identity_saved']
    add('local_identity_saved', source, value, 'original six-call exact one-call repair', True)
    base = source
    source, _, value = leaf_readback_sources()['local_identity_repeated_keys']
    add('local_identity_repeated_keys', source, value, 'original eight-call numeric entry addition', True,
        ("host.slot.set('x') + host.slot.set('x') + host.slot.set('y')", "host.slot.set('x')",
         'local_identity_saved'))
    expression = "var trace = host.slot.set('x') + host.slot.set('x') + host.slot.set('y');"
    assert source.count(expression) == 1
    add('local_add_preserve_calls_repair', source.replace(expression,
        "host.slot.set('x'); host.slot.set('x'); var trace = host.slot.set('y');"), 1,
        'all three original published calls survive; only result addition is removed')
    add('local_add_saved_results', source.replace(expression,
        "const first = host.slot.set('x'); const second = host.slot.set('x'); "
        "const third = host.slot.set('y'); var trace = first + second + third;"), 3,
        'separate saved results preserve the exact two-level numeric dependency')
    add('local_add_number_literal', base.replace("var trace = host.slot.set('x');",
        "var trace = host.slot.set('x') + 1;"), 2,
        'independently proved Number call result plus literal Number')
    add('local_add_result_key', base.replace("var trace = host.slot.set('x');",
        'var trace = host.slot.set(host.slot.set(1) + host.slot.set(2));'), 1,
        'the added Number result becomes a later method argument; host result provenance remains live')
    add('local_add_string_control', base.replace("var trace = host.slot.set('x');",
        "var trace = (host.slot.set('x') + '2') === '12' ? 1 : 0;"), 1,
        'Number/String + is concatenation and must not be classified as numeric addition')
    add('local_add_object_control', base.replace("var trace = host.slot.set('x');",
        "var trace = (host.slot.set('x') + {}) === '1[object Object]' ? 1 : 0;"), 1,
        'an object operand needs observable conversion and cannot supply a Number proof')

    source = leaf_clear_cases()['local_clear_reseed_disjoint']['source']
    old = ("state.clear(); state.set('other', item); "
           "return state.get('gone') === void 0 ? 1 : 0;")
    new = ('state.clear(); const zero = state.size; state.set(1, item); '
           'return state.get(zero) === void 0 ? 1 : 0;')
    assert source.count(old) == 1
    source = source.replace(old, new).replace("host.slot.set('x')", 'host.slot.set(7)')
    repair = source.replace('const zero = state.size;', 'state.size; const zero = 0;')
    add('local_clear_zero_literal_repair', repair, 1,
        'retain the evaluated size read and use independent literal-zero key disjointness')
    add('local_clear_zero_size_key', source, 1,
        'clear establishes exact zero size, but existing lower-bound evidence cannot prove zero differs from one',
        repair=('const zero = state.size;', 'state.size; const zero = 0;', 'local_clear_zero_literal_repair'))

    source, value, old, new, repair_name, calls = leaf_object_refusals()['leaf_object_string_field']
    repaired, _, repaired_value = leaf_object_sources()[repair_name]
    add(repair_name, repaired, repaired_value, 'original numeric owning leaf-field repair', True)
    add('leaf_object_string_field', source, value, 'original String owning leaf-field refusal', True,
        (old, new, repair_name))
    assert rows['leaf_object_string_field']['expected_raw_calls'] == calls

    # Each source still evaluates both calls. The size-returning setter makes
    # subtraction/division/power sensitive to reversing their execution order.
    growing = base.replace('return saved === item ? 1 : 0;', 'return state.size;')
    for kind, symbol, value in (("add", "+", 3), ("sub", "-", -1), ("mul", "*", 2),
                                ("div", "/", 0.5), ("mod", "%", 1), ("pow", "**", 1)):
        add('local_numeric_' + kind, growing.replace("var trace = host.slot.set('x');",
            "var trace = host.slot.set('x') " + symbol + " host.slot.set('y');"), value,
            'independently Number operands; both mutating calls retain source evaluation order')
    add('local_numeric_nested_key', growing.replace("var trace = host.slot.set('x');",
        'var trace = host.slot.set((host.slot.set(1) + host.slot.set(2)) * host.slot.set(3));'), 4,
        'completed arithmetic feeds a later same-method actual only after all input categories recheck')
    add('local_numeric_nan_key', growing.replace("var trace = host.slot.set('x');",
        'host.slot.set(host.slot.size() / host.slot.size()); '
        'var trace = host.slot.set((host.slot.size() - host.slot.size()) / 0);'), 1,
        'NaN keys retain SameValueZero equality; no observed Number is substituted for the real result')
    add('local_numeric_saved_snapshot', growing.replace("var trace = host.slot.set('x');",
        "const first = host.slot.set('x'); const second = host.slot.set('y'); "
        "host.slot.set('z'); var trace = first * 10 + second;"), 12,
        'saved Number results keep read-time values across later Map growth')
    field = base.replace('set(key) { const item = {};',
        'set(key, value) { const item = {value: value};').replace(
        'return saved === item ? 1 : 0;', 'state.clear(); return saved.value;')
    expression = "host.slot.set('x', 2) + host.slot.set('y', 3) * host.slot.set('z', 4)"
    field = field.replace("host.slot.set('x')", expression)
    add('local_numeric_saved_lifetime', field, 14,
        'saved Number fields and evaluated arithmetic survive clearing and final owner release')
    conditional = field.replace('set(key, value)', 'set(key, value, flag)').replace(
        'state.clear(); return saved.value;',
        'if (flag) { state.clear(); } else { state.delete(key); } return saved.value;')
    conditional = conditional.replace("'x', 2", "'x', 2, false").replace(
        "'y', 3", "'y', 3, false").replace("'z', 4", "'z', 4, false")
    add('local_numeric_branch_lifetime', conditional, 14,
        'both future Boolean arms preserve independently numeric saved fields')
    simple = field.replace(expression, "host.slot.set('x', 2) + 1")
    add('local_numeric_future_number_repair', simple.replace('return saved.value;',
        'return value ? saved.value : 0;'), 3,
        'both future Number truth arms independently return Number')
    add('local_numeric_future_bool', simple.replace('return saved.value;',
        'return value ? saved.value : false;'), 3,
        'a truthy startup Number cannot hide the future Boolean result', repair=(
        'return value ? saved.value : false;', 'return value ? saved.value : 0;',
        'local_numeric_future_number_repair'))
    add('local_numeric_later_number_repair', simple + "host.slot.set('later', 5);\n", 3,
        'a later independently numeric actual preserves the whole method family')
    add('local_numeric_later_bool', simple + "host.slot.set('later', false);\n", 3,
        'a later actual invalidates provisional numeric arithmetic facts', repair=(
        "host.slot.set('later', false);", "host.slot.set('later', 5);",
        'local_numeric_later_number_repair'))
    number_literal = rows['local_add_number_literal']['source']
    for tag, literal, value in (("bool", "true", 2), ("null", "null", 1),
                                ("undefined", "void 0", "NaN")):
        add('local_numeric_' + tag, number_literal.replace(" + 1;", ' + ' + literal + ';'), value,
            'the exact Number pair proof does not admit conversion from ' + tag,
            repair=(' + ' + literal + ';', ' + 1;', 'local_add_number_literal'))
    snapshot = rows['local_numeric_saved_snapshot']['source']
    old = ("const first = host.slot.set('x'); const second = host.slot.set('y'); "
           "host.slot.set('z'); var trace = first * 10 + second;")
    replacement = "var trace = host.slot.set('x') * 10 + host.slot.set('y'); host.slot.set('z');"
    assert snapshot.count(old) == 1
    add('local_numeric_inline_snapshot_repair', snapshot.replace(old, replacement), 12,
        'same arithmetic and x/y/z call order without extra scalar global reads')
    rows['local_numeric_saved_snapshot'].update(repair='local_numeric_inline_snapshot_repair',
        removed_text=old, replacement_text=replacement)
    old = ("const first = host.slot.set('x'); const second = host.slot.set('x'); "
           "const third = host.slot.set('y'); var trace = first + second + third;")
    replacement = "var trace = host.slot.set('x') + host.slot.set('x') + host.slot.set('y');"
    assert rows['local_add_saved_results']['source'].replace(old, replacement) == (
        rows['local_identity_repeated_keys']['source'])
    rows['local_add_saved_results'].update(repair='local_identity_repeated_keys',
        removed_text=old, replacement_text=replacement)
    for name, row in rows.items():
        raw = prepared = row['expected_raw_calls']
        if name in NUMERIC_ENTRY_HISTORY:
            raw, prepared, digest = NUMERIC_ENTRY_HISTORY[name]
            assert hashlib.sha256(row['source'].encode()).hexdigest() == digest, name
            assert row['expected_raw_calls'] == raw, name
        row.update(raw_calls=raw, prepared_calls=prepared)
    return rows


NUMERIC_ENTRY_UNOWNED = {
    'local_add_string_control', 'local_add_object_control',
    'local_numeric_future_bool', 'local_numeric_later_bool',
    'local_numeric_bool', 'local_numeric_null', 'local_numeric_undefined',
}
NUMERIC_ENTRY_SAVED_GLOBALS = {'local_add_saved_results', 'local_numeric_saved_snapshot'}
NUMERIC_ENTRY_CARRIERS = set()
NUMERIC_ENTRY_EXISTING_POSITIVES = {
    'local_identity_saved', 'leaf_object_number_field', 'leaf_object_string_field',
}
NUMERIC_ENTRY_PROMOTED = {'local_identity_repeated_keys'}


def numeric_entry_sources():
    return {name: (row['source'], 'host', row['expected_trace'])
            for name, row in numeric_entry_cases().items()
            if name not in NUMERIC_ENTRY_UNOWNED | NUMERIC_ENTRY_EXISTING_POSITIVES | NUMERIC_ENTRY_CARRIERS}


def numeric_entry_refusals():
    cases = numeric_entry_cases()
    for name, old, replacement, repair in (
        ('local_add_string_control', "(host.slot.set('x') + '2') === '12' ? 1 : 0",
         "host.slot.set('x') + 1", 'local_add_number_literal'),
        ('local_add_object_control', "(host.slot.set('x') + {}) === '1[object Object]' ? 1 : 0",
         "host.slot.set('x') + 1", 'local_add_number_literal'),
    ):
        row = cases[name]
        assert row['source'].count(old) == 1
        assert row['source'].replace(old, replacement) == cases[repair]['source']
        row.update(repair=repair, removed_text=old, replacement_text=replacement)
    return {name: (row['source'], row['expected_trace'], row['removed_text'],
                   row['replacement_text'], row['repair'], row['prepared_calls'])
            for name, row in cases.items() if name in NUMERIC_ENTRY_UNOWNED}


SCALAR_GLOBAL_UNOWNED = {
    "scalar_read_before_write", "scalar_later_bool", "scalar_future_bool", "scalar_duplicate_number",
}
SCALAR_GLOBAL_CARRIERS = {
    "scalar_duplicate_write",
}
SCALAR_GLOBAL_INITIALIZED = {"scalar_alias", "scalar_single_write_repair"}


def scalar_global_cases():
    historical = numeric_entry_cases()
    rows = {}

    def add(name, source, value, saved, boundary, repair=None):
        calls = 2 + len(re.findall(r"\b(?:state|host\.slot)\.(?:set|get|size|has|delete|clear)\(", source))
        rows[name] = dict(source=source, expected_trace=value, saved=saved, raw_calls=calls,
                          prepared_calls=calls, expected_raw_calls=calls, boundary=boundary)
        if repair:
            old, replacement, target = repair
            assert source.count(old) == 1 and source.replace(old, replacement) == rows[target]["source"]
            rows[name].update(repair=target, removed_text=old, replacement_text=replacement)

    snapshot = historical["local_numeric_saved_snapshot"]["source"]
    add("scalar_alias", snapshot.replace("var trace = first * 10 + second;",
        "const alias = first; var trace = alias * 10 + second;"), 12,
        {"first": 1, "second": 2, "alias": 1},
        "the alias saves an independently completed published Number result")
    add("scalar_arithmetic_result", snapshot.replace("var trace = first * 10 + second;",
        "const total = first * 10 + second; var trace = total + 1;"), 13,
        {"first": 1, "second": 2, "total": 12},
        "the stored arithmetic result retains both completed call dependencies")
    add("scalar_builtin_spelling", snapshot.replace("first", "Reflect").replace("second", "prototype"),
        12, {"Reflect": 1, "prototype": 2},
        "independent live Number origins supply authority even for builtin-like spellings")
    prefix = snapshot.rsplit("host.slot.size();", 1)[0] + "host.slot.size(); "
    add("scalar_result_key", prefix +
        "const first = host.slot.set(1); const second = host.slot.set(2); "
        "const key = first + second; var trace = host.slot.set(key);\n", 3,
        {"first": 1, "second": 2, "key": 3},
        "saved arithmetic becomes a later same-method actual after the complete future-input census")
    single = prefix + "var first = host.slot.set('x'); host.slot.set('y'); var trace = first;\n"
    add("scalar_single_write_repair", single, 1, {"first": 1},
        "the later mutating call remains evaluated without rewriting the saved global")
    add("scalar_duplicate_write", single.replace("host.slot.set('y');", "first = host.slot.set('y');"),
        2, {"first": 2}, "a later second write invalidates the single-store proof", repair=(
        "first = host.slot.set('y');", "host.slot.set('y');", "scalar_single_write_repair"))
    ordered = "var first = host.slot.set('x'); var trace = first + host.slot.set('y');"
    prior = "var trace = first + host.slot.set('y'); var first = host.slot.set('x');"
    add("scalar_prior_store_repair", prefix + ordered + "\n", 3, {"first": 1},
        "the producing call and store precede the saved Number read")
    add("scalar_read_before_write", prefix + prior + "\n", "NaN", {"first": 2},
        "a later declaration and result store cannot authorize an earlier Undefined read", repair=(
        prior, ordered, "scalar_prior_store_repair"))
    field = historical["local_numeric_branch_lifetime"]["source"]
    expression = ("var trace = host.slot.set('x', 2, false) + "
                  "host.slot.set('y', 3, false) * host.slot.set('z', 4, false);")
    saved = ("const first = host.slot.set('x', 2, false); const second = host.slot.set('y', 3, false); "
             "const third = host.slot.set('z', 4, false); var trace = first + second * third;")
    assert field.count(expression) == 1
    field = field.replace(expression, saved)
    values = {"first": 2, "second": 3, "third": 4}
    add("scalar_saved_branch_lifetime", field, 14, values,
        "saved global Number fields survive both future branch arms and owner release")
    add("scalar_later_number_repair", field + "host.slot.set('later', 5, false);\n", 14, values,
        "a later Number actual preserves every earlier saved result category")
    add("scalar_later_bool", field + "host.slot.set('later', false, false);\n", 14, values,
        "later mixed actuals invalidate provisional Number result dependencies", repair=(
        "host.slot.set('later', false, false);", "host.slot.set('later', 5, false);",
        "scalar_later_number_repair"))
    add("scalar_future_number_repair", field.replace("return saved.value;",
        "return value ? saved.value : 0;"), 14, values,
        "both unseen Number truth arms return independently proved Numbers")
    add("scalar_future_bool", field.replace("return saved.value;",
        "return value ? saved.value : false;"), 14, values,
        "truthy startup inputs cannot hide a future Boolean public result", repair=(
        "return value ? saved.value : false;", "return value ? saved.value : 0;",
        "scalar_future_number_repair"))
    literal = snapshot + "const fixed = 7; const copy = 7;\n"
    add("scalar_constant_literal_repair", literal, 12,
        {"first": 1, "second": 2, "fixed": 7, "copy": 7},
        "literal stores stay evaluated without a global read lacking a published result dependency")
    add("scalar_constant_only", literal.replace("const copy = 7;", "const copy = fixed;"), 12,
        {"first": 1, "second": 2, "fixed": 7, "copy": 7},
        "a constant-only Number global has no completed published-result dependency", repair=(
        "const copy = fixed;", "const copy = 7;", "scalar_constant_literal_repair"))
    # Keep the measured alias-only sources and their arithmetic repairs intact.
    # Exact initialization evidence now removes only the implicit Undefined seed;
    # the actual stored SSA value still supplies the independently inferred type.
    alias = rows["scalar_alias"]["source"]
    add("scalar_alias_number_repair", alias.replace("const alias = first;", "const alias = first + 0;"),
        12, rows["scalar_alias"]["saved"],
        "retain the scalar alias store/read and use definite Number arithmetic at its initializer")
    rows["scalar_alias"].update(repair="scalar_alias_number_repair",
        removed_text="const alias = first;", replacement_text="const alias = first + 0;")
    add("scalar_single_number_repair", single.replace("var trace = first;", "var trace = first + 0;"),
        1, {"first": 1}, "retain the saved global read and both calls with a definite Number observation")
    rows["scalar_single_write_repair"].update(repair="scalar_single_number_repair",
        removed_text="var trace = first;", replacement_text="var trace = first + 0;")
    rows["scalar_duplicate_write"].update(repair="scalar_single_number_repair",
        removed_text="first = host.slot.set('y'); var trace = first;",
        replacement_text="host.slot.set('y'); var trace = first + 0;")
    duplicate = rows["scalar_duplicate_write"]["source"].replace("var trace = first;",
                                                                         "var trace = first + 0;")
    add("scalar_duplicate_number", duplicate, 2, {"first": 2},
        "definite Number output isolates the global all-writes census from the alias observation carrier",
        repair=("first = host.slot.set('y');", "host.slot.set('y');", "scalar_single_number_repair"))
    add("scalar_alias_chain", alias.replace("const alias = first;",
        "const saved = first; const alias = saved;"), 12,
        {**rows["scalar_alias"]["saved"], "saved": 1},
        "each alias edge subscribes to its own stored value after independent initialization proof")
    arithmetic = rows["scalar_arithmetic_result"]["source"]
    add("scalar_alias_arithmetic", arithmetic.replace("var trace = total + 1;",
        "const alias = total; var trace = alias + 1;"), 13,
        {**rows["scalar_arithmetic_result"]["saved"], "alias": 12},
        "an alias of a stored arithmetic result preserves both actual Number dependencies")
    branch = rows["scalar_saved_branch_lifetime"]["source"]
    add("scalar_alias_branch_lifetime", branch.replace("var trace = first + second * third;",
        "const left = first; const middle = second; const right = third; "
        "var trace = left + middle * right;"), 14,
        {**rows["scalar_saved_branch_lifetime"]["saved"], "left": 2, "middle": 3, "right": 4},
        "saved aliases survive future branch effects, owner release and independent reentry")
    for name, calls, digest in (
        ("scalar_alias", 8, "8003b4bc3a35bc936752067dc66c97ab02b36294db685d776d10a53f1a42b388"),
        ("scalar_single_write_repair", 7,
         "06efa534b2cbb1c79ce8c714677e6b2e99b43b553da63d025bcb86f0beb59f3f"),
    ):
        assert rows[name]["raw_calls"] == calls, name
        assert hashlib.sha256(rows[name]["source"].encode()).hexdigest() == digest, name
    for name, row in rows.items():
        if "repair" in row:
            assert row["source"].count(row["removed_text"]) == 1, name
            assert row["source"].replace(row["removed_text"], row["replacement_text"]) == rows[row["repair"]]["source"], name
    return rows


def scalar_global_sources():
    return {name: (row["source"], "host", row["expected_trace"])
            for name, row in scalar_global_cases().items()
            if name not in SCALAR_GLOBAL_UNOWNED | SCALAR_GLOBAL_CARRIERS}


def scalar_global_refusals():
    return {name: (row["source"], row["expected_trace"], row["removed_text"],
                   row["replacement_text"], row["repair"], row["prepared_calls"])
            for name, row in scalar_global_cases().items() if name in SCALAR_GLOBAL_UNOWNED}


def scalar_global_values(name):
    saved = {"local_add_saved_results": {"first": 1, "second": 1, "third": 1},
             "local_numeric_saved_snapshot": {"first": 1, "second": 2}}.get(name)
    if saved is None:
        row = scalar_global_cases().get(name)
        if row is None:
            row = constant_global_cases().get(name, {})
        saved = row.get("saved", {})
    return saved


class StringValue(str):
    """An actual String, distinct from the numeric/absence fixture sentinels."""


STRING_GLOBAL_BYTES = StringValue("quote'\" newline\n tab\t percent% slash\\ equals=semi; café 雪 😀\x00tail !()*")
STRING_GLOBAL_LONG = StringValue("owned-string-payload-" * 48 + "\x00tail")


def scalar_global_output(name, value):
    saved = scalar_global_values(name)
    globals_ = {**saved, "trace": value}

    def printed(result):
        if isinstance(result, StringValue) or result == "owned scalar":
            return '"' + quote(str(result), safe="-._~") + '"'
        if isinstance(result, bool):
            return str(result).lower()
        if result == "NaN":
            return "nan"
        if result == "-Infinity":
            return "-inf"
        return str(result)

    return "".join(f"{binding}={printed(result)}\n" for binding, result in sorted(globals_.items()))


# The twelve original probes and twelve candidate edits were measured before
# constant-only Number admission. Keep their exact bytes; Boolean originals and
# candidates now need both live scalar proof and independently typed observations.
CONSTANT_GLOBAL_HISTORY = {
    'constant_exact_historical': ('3c1dfd95d22834d6ce64c352ab515f628fddd4e03168acf709642d62243c1491',
        '41a33e4066ff78c262289225670eb38736d06e7cbd2c3f55be2176668b084f71'),
    'constant_feeds_trace': ('032a7cf898ad6677b54369d302038c28ec94083906264e97955f5e66c7b413d1',
        '11bc2b77e812824764f7dc793f1f7c19f2a6bf6d2d1e63a596f6b209f21dcec4'),
    'constant_negative_zero': ('fd18ed89f0d6ab85b761908e77708383fbcb51bc79bd942a7ad897217a798f64',
        'aa78e8ee9040963d8781a3d8f06352d7f568f326bcee4a76e45264f1737db605'),
    'constant_nan': ('16e8e9f5790f88eef330cfa7992101f54b3a7ad32cfdf0fb2fb24c58566627e1',
        '2c2258e09a32408cee5773586f0e676579aa04a6b3c8bfb9817269e5b7d93edb'),
    'constant_boolean': ('681c88958033cc6856eb955d592166118c74d44cc9c5cb4de4bb90aacde2e63d',
        'd3a90c0165fe16ae6f3333d4d084c86e67c70b4ace304d888c4d43440cb46116'),
    'constant_string': ('3a99e34c6ceb6f9a13e85666c3480b7e7c74d52c783ad1420d726d103a655d8a',
        'f0a03c19d5620834869e559ef0aaafede25f6b017c24bae67021d9a460d76d84'),
    'constant_undefined': ('004faccaffe587297d5df60e77bb78c19a7b21dffe674cd38e8ad2244e050d6a',
        'def9220fcc5c8516122b8e1792454d97a505912b94713e6d709b25fb70bc94c6'),
    'constant_builtin_spelling': ('804006fadbd519e84980ccdfac5372d268a70bb2f96e76bd3727ad48458b6439',
        'c733a5cda331467b78b20b06c9d9748143905c39ef1b73f1c714a0c8a08ac222'),
    'constant_duplicate_write': ('41bd3d6a3f2635099f23de52e6d65c1299be65962b70e8684c19aaccc525da2f',
        '1e93521a3cfda469ebc5b0635a1f5f703db851afbe4ee6cd2244bf3acdaf0fd7'),
    'constant_read_before_write': ('37cfd8d9934dd7ba9ebde1fd4eabeb095746bb10299da0551efe7671f3dd698c',
        '69179658e296b40c839a5381005a3e96a3e15a7bbf85216926d2803456780409'),
    'constant_dynamic_global': ('37af242bbce92d2ed2e2bee75608b1c703bc93c1660f5e9a0070760f43b60214',
        '1e93521a3cfda469ebc5b0635a1f5f703db851afbe4ee6cd2244bf3acdaf0fd7'),
    'constant_future_method_write': ('c456bc57bd87cd1a850afae05dc4bc46d35e234dc6e68d4094800eeaee482375',
        '48bb0903c76f096f67e070d79a50af0f29ecb03063aff0a337681fcfae583e6d'),
}
CONSTANT_GLOBAL_UNOWNED = {
    "constant_read_before_write", "constant_dynamic_global", "constant_future_method_write",
    "constant_boolean_read_before_write", "constant_boolean_dynamic_global",
    "constant_boolean_future_method_write", "constant_boolean_optional", "constant_boolean_mixed",
    "constant_string_read_before_write", "constant_string_future_method_write",
    "constant_string_optional", "constant_string_mixed",
}
CONSTANT_GLOBAL_CARRIERS = {
    "constant_undefined", "constant_duplicate_write", "constant_undefined_candidate",
    "constant_boolean_duplicate_write", "constant_boolean_mixed_write",
    "constant_string_duplicate_write", "constant_string_mixed_write",
}
CONSTANT_GLOBAL_EXISTING = {
    "constant_exact_historical": "scalar_constant_only",
    "constant_exact_historical_candidate": "scalar_constant_literal_repair",
}


def constant_global_cases():
    historical = scalar_global_cases()
    source = historical["scalar_constant_only"]["source"]
    snapshot = numeric_entry_cases()["local_numeric_saved_snapshot"]["source"]
    values = {"first": 1, "second": 2, "fixed": 7, "copy": 7}
    rows = {}

    def add(name, text, trace, saved, old=None, replacement=None, candidate_saved=None):
        calls = 2 + len(re.findall(r"\b(?:state|host\.slot)\.(?:set|get|size|has|delete|clear)\(", text))
        rows[name] = dict(source=text, expected_trace=trace, saved=saved, raw_calls=calls,
                          prepared_calls=calls, expected_raw_calls=calls)
        if old is None:
            return
        assert text.count(old) == 1, name
        candidate = name + "_candidate"
        rows[name].update(candidate=candidate, removed_text=old, replacement_text=replacement)
        rows[candidate] = dict(source=text.replace(old, replacement), expected_trace=trace,
            saved=saved if candidate_saved is None else candidate_saved,
            raw_calls=calls, prepared_calls=calls, expected_raw_calls=calls)

    add("constant_exact_historical", source, 12, values,
        "const copy = fixed;", "const copy = 7;")
    prefix = snapshot.replace("var trace = first * 10 + second;\n", "")
    add("constant_feeds_trace", prefix + "const fixed = 7; const copy = fixed; var trace = copy + first;\n",
        8, values, "const copy = fixed;", "const copy = 7;")
    add("constant_negative_zero", prefix +
        "const fixed = 0 / (0 - 1); const copy = fixed; var trace = 1 / copy;\n",
        "-Infinity", {**values, "fixed": "-0", "copy": "-0"},
        "const copy = fixed;", "const copy = 0 / (0 - 1);")
    add("constant_nan", prefix + "const fixed = 0 / 0; const copy = fixed; var trace = copy;\n",
        "NaN", {**values, "fixed": "NaN", "copy": "NaN"},
        "const copy = fixed;", "const copy = 0 / 0;")
    for tag, literal, value in (("boolean", "false", False),
                                ("string", "'owned scalar'", "owned scalar"),
                                ("undefined", "void 0", "undefined")):
        add("constant_" + tag, source.replace("const fixed = 7;", "const fixed = " + literal + ";"),
            12, {**values, "fixed": value, "copy": value},
            "const copy = fixed;", "const copy = " + literal + ";")
    add("constant_builtin_spelling", source.replace("fixed", "Reflect").replace("copy", "prototype"),
        12, {"first": 1, "second": 2, "Reflect": 7, "prototype": 7},
        "const prototype = Reflect;", "const prototype = 7;")
    add("constant_duplicate_write", snapshot + "var fixed = 7; const copy = fixed; fixed = 9;\n",
        12, {**values, "fixed": 9}, "fixed = 9;", "const later = 9;", {**values, "later": 9})
    add("constant_read_before_write", snapshot + "var copy = fixed; var fixed = 7;\n",
        12, {**values, "copy": "undefined"}, "var copy = fixed; var fixed = 7;",
        "var fixed = 7; var copy = fixed;", values)
    add("constant_dynamic_global", snapshot + "var fixed = 7; const copy = fixed; globalThis.fixed = 9;\n",
        12, {**values, "fixed": 9}, "globalThis.fixed = 9;", "const later = 9;", {**values, "later": 9})
    future = "var fixed = 7;\n" + snapshot.replace("set(key) {", "set(key) { fixed = 9;")
    add("constant_future_method_write", future + "const copy = fixed;\n",
        12, {**values, "fixed": 9, "copy": 9}, "fixed = 9;", "const future = 9;", values)
    add("constant_alias_chain", source.replace("const copy = fixed;",
        "const offset = fixed; const copy = offset;"), 12, {**values, "offset": 7})
    add("constant_alias_arithmetic", prefix +
        "const fixed = 2 * 3 + 1; const offset = fixed / 2; const copy = offset; "
        "var trace = copy + first;\n", 4.5, {**values, "offset": 3.5, "copy": 3.5})
    branch = historical["scalar_alias_branch_lifetime"]
    add("constant_branch_lifetime", branch["source"].replace(
        "var trace = left + middle * right;", "const fixed = 7; const offset = fixed; "
        "const copy = offset; var trace = left + middle * right + copy - fixed;"),
        14, {**branch["saved"], "fixed": 7, "offset": 7, "copy": 7})
    boolean = rows["constant_boolean"]["source"]
    flags = {**values, "fixed": False, "copy": False}
    add("constant_boolean_true", boolean.replace("const fixed = false;", "const fixed = true;"),
        12, {**flags, "fixed": True, "copy": True}, "const copy = fixed;", "const copy = true;")
    add("constant_boolean_alias_chain", boolean.replace("const copy = fixed;",
        "const enabled = true; const offset = fixed; const copy = offset;"),
        12, {**flags, "enabled": True, "offset": False})
    add("constant_boolean_builtin_spelling", boolean.replace("fixed", "Reflect").replace("copy", "prototype"),
        12, {"first": 1, "second": 2, "Reflect": False, "prototype": False})
    for flag, value in (("false", False), ("true", True)):
        add("constant_boolean_trace_" + flag, prefix +
            "const fixed = " + flag + "; const copy = fixed; var trace = copy;\n",
            value, {**flags, "fixed": value, "copy": value})
    result = boolean.replace("return state.size; }\n", "return saved === item; }\n").replace(
        "var trace = first * 10 + second;", "var trace = 12;").replace(
        "const copy = fixed;", "const copy = first;")
    assert result != boolean and "return saved === item;" in result
    add("constant_boolean_saved_result", result, 12,
        {"first": True, "second": True, "fixed": False, "copy": True})
    add("constant_boolean_duplicate_write", snapshot +
        "var fixed = false; const copy = fixed; fixed = true;\n", 12,
        {**flags, "fixed": True}, "fixed = true;", "const later = true;", {**flags, "later": True})
    add("constant_boolean_mixed_write", snapshot +
        "var fixed = false; const copy = fixed; fixed = 1;\n", 12,
        {**flags, "fixed": 1}, "fixed = 1;", "const later = 1;", {**flags, "later": 1})
    add("constant_boolean_read_before_write", snapshot + "var copy = fixed; var fixed = false;\n",
        12, {**flags, "copy": "undefined"}, "var copy = fixed; var fixed = false;",
        "var fixed = false; var copy = fixed;", flags)
    add("constant_boolean_dynamic_global", snapshot +
        "var fixed = false; const copy = fixed; globalThis.fixed = true;\n", 12,
        {**flags, "fixed": True}, "globalThis.fixed = true;", "const later = true;",
        {**flags, "later": True})
    future = "var fixed = false;\n" + snapshot.replace("set(key) {", "set(key) { fixed = true;")
    add("constant_boolean_future_method_write", future + "const copy = fixed;\n", 12,
        {**flags, "fixed": True, "copy": True}, "fixed = true;", "const future = true;", flags)
    for kind, other in (("optional", "void 0"), ("mixed", "0")):
        initializer = "const fixed = first ? false : " + other + ";"
        add("constant_boolean_" + kind, boolean.replace("const fixed = false;", initializer),
            12, flags, initializer, "const fixed = false;")
    lifetime = rows["constant_branch_lifetime"]
    add("constant_boolean_branch_lifetime", lifetime["source"] +
        "const fixed_flag = false; const enabled = true; "
        "const copy_flag = fixed_flag; const active = enabled;\n", 14,
        {**lifetime["saved"], "fixed_flag": False, "enabled": True, "copy_flag": False, "active": True})
    string = rows["constant_string"]["source"]
    strings = {**values, "fixed": StringValue("owned scalar"), "copy": StringValue("owned scalar")}
    for kind, value in (("empty", StringValue("")), ("bytes", STRING_GLOBAL_BYTES),
                        ("long", STRING_GLOBAL_LONG)):
        literal = json.dumps(value, ensure_ascii=False)
        add("constant_string_" + kind, string.replace("'owned scalar'", literal), 12,
            {**strings, "fixed": value, "copy": value}, "const copy = fixed;", "const copy = " + literal + ";")
    add("constant_string_alias_chain", string.replace("const copy = fixed;",
        "const offset = fixed; const copy = offset;"), 12,
        {**strings, "offset": StringValue("owned scalar")})
    for kind, value in (("empty", StringValue("")), ("bytes", STRING_GLOBAL_BYTES)):
        add("constant_string_trace_" + kind, prefix + "const fixed = " +
            json.dumps(value, ensure_ascii=False) + "; const copy = fixed; var trace = copy;\n",
            value, {**strings, "fixed": value, "copy": value})
    add("constant_string_duplicate_write", snapshot +
        "var fixed = 'owned scalar'; const copy = fixed; fixed = 'later';\n", 12,
        {**strings, "fixed": StringValue("later")}, "fixed = 'later';", "const later = 'later';",
        {**strings, "later": StringValue("later")})
    add("constant_string_mixed_write", snapshot +
        "var fixed = 'owned scalar'; const copy = fixed; fixed = false;\n", 12,
        {**strings, "fixed": False}, "fixed = false;", "const later = false;", {**strings, "later": False})
    add("constant_string_read_before_write", snapshot + "var copy = fixed; var fixed = 'owned scalar';\n",
        12, {**strings, "copy": "undefined"}, "var copy = fixed; var fixed = 'owned scalar';",
        "var fixed = 'owned scalar'; var copy = fixed;", strings)
    future = "var fixed = 'owned scalar';\n" + snapshot.replace("set(key) {", "set(key) { fixed = 'later';")
    add("constant_string_future_method_write", future + "const copy = fixed;\n", 12,
        {**strings, "fixed": StringValue("later"), "copy": StringValue("later")},
        "fixed = 'later';", "const future = 'later';", strings)
    for kind, other in (("optional", "void 0"), ("mixed", "false")):
        initializer = "const fixed = first ? 'owned scalar' : " + other + ";"
        add("constant_string_" + kind, string.replace("const fixed = 'owned scalar';", initializer),
            12, strings, initializer, "const fixed = 'owned scalar';")
    add("constant_string_branch_lifetime", lifetime["source"] +
        "const owned_text = " + json.dumps(STRING_GLOBAL_LONG) +
        "; const text_alias = owned_text; const saved_text = text_alias; const empty_text = '';\n",
        14, {**lifetime["saved"], "owned_text": STRING_GLOBAL_LONG, "text_alias": STRING_GLOBAL_LONG,
             "saved_text": STRING_GLOBAL_LONG, "empty_text": StringValue("")})
    for name, (original, candidate) in CONSTANT_GLOBAL_HISTORY.items():
        assert rows[name]["raw_calls"] == 8, name
        for key, digest in ((name, original), (name + "_candidate", candidate)):
            assert hashlib.sha256(rows[key]["source"].encode()).hexdigest() == digest, key
    for name, existing in CONSTANT_GLOBAL_EXISTING.items():
        assert rows[name]["source"] == historical[existing]["source"], name
    return rows


def constant_global_sources():
    return {name: (row["source"], "host", row["expected_trace"])
            for name, row in constant_global_cases().items()
            if name not in CONSTANT_GLOBAL_UNOWNED | CONSTANT_GLOBAL_CARRIERS | CONSTANT_GLOBAL_EXISTING.keys()}


def normalized_scalar_output(output):
    # Both reference and native printf may spell Number NaN as -nan. Do not
    # normalize any other value or turn a string, Undefined or infinity into NaN.
    return re.sub(r"(?m)^(\w+)=\-nan$", r"\1=nan", output)


# The original String-field source and its Number repair keep their exact
# bytes, function count and seven calls. Other names beginning field_string
# preserve the post-417cd0ac continuation probe, including its long saved read.
