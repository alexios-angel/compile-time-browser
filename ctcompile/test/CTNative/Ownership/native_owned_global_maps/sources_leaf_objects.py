from .sources_nullable_maps import (
    RESULT_SIGNATURES,
)
from .sources_scalar_maps import (
    hashlib,
    payload_result_refusals,
    re,
)

LEAF_OBJECT_SOURCE = """var host = {};
(function(factory) { host.slot = factory(); })(function() {
    const state = new Map();
    return {
        size() { return state.size; },
        set(key) { const item = {}; state.set(key, item); return state.size; }
    };
});
host.slot.set('x'); host.slot.set('x'); host.slot.set('y');
var trace = host.slot.size();
"""


def leaf_object_sources():
    number = LEAF_OBJECT_SOURCE.replace("const item = {};", "const item = {value: 1};")
    fields = LEAF_OBJECT_SOURCE.replace(
        "const item = {};",
        "const item = {value: 1, flag: true, empty: null, absent: void 0}; "
        "item.value = state.size; item.flag = false;",
    )
    lifetime = fields.replace(
        "size() { return state.size; },",
        "size() { return state.size; }, " "erase(key) { state.delete(key); return state.size; },",
    )
    lifetime = lifetime.replace("var trace =", "host.slot.erase('y'); var trace =")
    return {
        # These first two programs preserve the exact seven-call/five-function
        # continuation measured in e533a865, including repeated live keys.
        "leaf_object_plain": (LEAF_OBJECT_SOURCE, "host", 2),
        "leaf_object_number_field": (number, "host", 2),
        "leaf_object_scalar_writes": (fields, "host", 2),
        "leaf_object_alias": (
            fields.replace("state.set(key, item);", "const alias = item; state.set(key, alias);"),
            "host",
            2,
        ),
        # An independently called deleting sibling provides a numeric ABI for
        # the saved-callable observer. No object crosses a published boundary.
        "leaf_object_lifetime": (lifetime, "host", 1),
        "leaf_object_number_repair": (
            LEAF_OBJECT_SOURCE.replace("const item = {};", "const item = 1;"),
            "host",
            2,
        ),
        "leaf_object_string_repair": (
            LEAF_OBJECT_SOURCE.replace("const item = {};", "const item = 'instance';"),
            "host",
            2,
        ),
        "leaf_object_identity_repair": (
            number.replace(
                "host.slot.set('x'); host.slot.set('x'); host.slot.set('y');\nvar trace = host.slot.size();",
                "host.slot.size(); var trace = host.slot.set('x');",
            ),
            "host",
            1,
        ),
    }


LEAF_OBJECT_CALLS = {
    name: 9 if name == "leaf_object_lifetime" else 5 if name == "leaf_object_identity_repair" else 7
    for name in leaf_object_sources()
}
LEAF_OBJECT_FUNCTIONS = {
    name: 6 if name == "leaf_object_lifetime" else 5 for name in leaf_object_sources()
}
LEAF_OBJECT_FIELDS = {
    "leaf_object_number_field": "number",
    "leaf_object_identity_repair": "number",
    "leaf_object_scalar_writes": "scalar",
    "leaf_object_alias": "scalar",
    "leaf_object_lifetime": "scalar",
}


def leaf_object_refusals():
    number = leaf_object_sources()["leaf_object_number_field"][0]
    repaired = leaf_object_sources()["leaf_object_identity_repair"][0]
    body = "const item = {value: 1}; state.set(key, item); return state.size;"
    saved = (
        "const item = {value: 1}; state.set(key, item); const saved = state.get(key); "
        "state.set(key, {value: 1}); state.delete(key); return saved === item ? 1 : 0;"
    )
    rows = {}
    for name, old, replacement, repair, value, calls in (
        (
            "string_field",
            "const item = {value: 1};",
            "const item = {value: 'instance'};",
            "leaf_object_number_field",
            2,
            7,
        ),
        (
            "nested_field",
            "const item = {value: 1};",
            "const item = {value: {}};",
            "leaf_object_number_field",
            2,
            7,
        ),
        (
            "cycle",
            "const item = {value: 1};",
            "const item = {value: 1}; item.value = item;",
            "leaf_object_number_field",
            2,
            7,
        ),
        (
            "dynamic_field",
            "const item = {value: 1};",
            "const item = {value: 1}; item[key] = 1;",
            "leaf_object_number_field",
            2,
            7,
        ),
        (
            "prototype",
            "const item = {value: 1};",
            "const item = {value: 1}; item.__proto__ = {};",
            "leaf_object_number_field",
            2,
            7,
        ),
        (
            "object_key",
            "state.set(key, item);",
            "state.set(item, item);",
            "leaf_object_number_field",
            3,
            7,
        ),
        (
            "object_return",
            "state.set(key, item); return state.size;",
            "state.set(key, item); return item;",
            "leaf_object_number_field",
            2,
            7,
        ),
        (
            "later_actual",
            "var trace =",
            "host.slot.set({}); var trace =",
            "leaf_object_number_field",
            3,
            8,
        ),
        (
            "unknown_read",
            "size() { return state.size; }",
            "size() { const item = state.get('x'); return item ? 1 : 0; }",
            "leaf_object_number_field",
            1,
            8,
        ),
        (
            "unsafe_sibling",
            "size() { return state.size; }",
            "size() { const item = {}; item.self = item; state.set('cycle', item); return state.size; }",
            "leaf_object_number_field",
            3,
            8,
        ),
    ):
        rows["leaf_object_" + name] = (
            number.replace(old, replacement),
            value,
            replacement,
            old,
            repair,
            calls,
        )
    # The exact saved/distinct/deleted identity continuation remains separate
    # from write-only leaf ownership. Each repair restores the same complete
    # numeric-returning source, rather than erasing or trusting forged reads.
    for name, expression, value, calls in (
        ("saved_identity", "saved === item", 1, 8),
        ("distinct_identity", "saved === {value: 1}", 0, 8),
        ("deleted_identity", "state.get(key) === item", 0, 9),
    ):
        changed = saved.replace("saved === item", expression)
        rows["leaf_object_" + name] = (
            repaired.replace(body, changed),
            value,
            changed,
            body,
            "leaf_object_identity_repair",
            calls,
        )
    return rows


def leaf_readback_sources():
    # These preserve the sixteen measured next.json sources from 5d2d843a.
    # Keep the two post-delete reads as refusals. The comparison-only fresh
    # allocations now use an independent strict-comparison identity census.
    base = leaf_object_sources()["leaf_object_identity_repair"][0]
    old = "const item = {value: 1}; state.set(key, item); return state.size;"
    empty = "const item = {}; state.set(key, item); const saved = state.get(key); "
    field = "const item = {value: 1}; state.set(key, item); const saved = state.get(key); "
    rows = {
        "local_identity_saved": (empty + "return saved === item ? 1 : 0;", 1),
        "local_identity_distinct_fresh": (empty + "return saved === {} ? 1 : 0;", 0),
        "local_field_direct": (
            "const item = {value: 1}; state.set(key, item); return item.value;",
            1,
        ),
        "local_field_get": (field + "return saved.value;", 1),
        "local_field_get_guarded": (field + "return saved === item ? saved.value : 0;", 1),
        "local_identity_distinct_stored": (
            empty + "const replacement = {}; "
            "state.set(key, replacement); return saved === replacement ? 1 : 0;",
            0,
        ),
        "local_identity_saved_overwrite": (
            field + "state.set(key, {value: 1}); " "return saved === item ? 1 : 0;",
            1,
        ),
        "local_identity_saved_delete": (
            field + "state.delete(key); return saved === item ? 1 : 0;",
            1,
        ),
        "local_field_saved_overwrite": (
            field + "state.set(key, {value: 2}); " "return saved === item ? saved.value : 0;",
            1,
        ),
        "local_field_saved_delete": (
            field + "state.delete(key); " "return saved === item ? saved.value : 0;",
            1,
        ),
        "local_field_saved_alias_write": (
            field + "item.value = 2; " "return saved === item ? saved.value : 0;",
            2,
        ),
    }
    result = {
        name: (base.replace(old, body), "host", value) for name, (body, value) in rows.items()
    }
    same = result["local_identity_saved"][0]
    result["local_identity_repeated_keys"] = (
        same.replace(
            "host.slot.set('x');", "host.slot.set('x') + host.slot.set('x') + host.slot.set('y');"
        ),
        "host",
        3,
    )
    for name in ("saved_identity", "distinct_identity"):
        source, value, *_ = leaf_object_refusals()["leaf_object_" + name]
        result["historical_object_" + name] = source, "host", value
    result["local_identity_not_equal"] = (
        same.replace("saved === item", "saved !== item"),
        "host",
        0,
    )
    result["local_field_readback_test"] = (
        base.replace(old, field + "return saved.value === 1 ? 1 : 0;"),
        "host",
        1,
    )
    result["local_field_export_repair"] = (
        result["local_field_get_guarded"][0]
        .replace(
            "return saved === item ? saved.value : 0;",
            "return saved === item && saved.value === 1 ? 1 : 0;",
        )
        .replace(
            "var trace = host.slot.set('x');", "host.slot.set('x'); var trace = host.slot.size();"
        ),
        "host",
        1,
    )
    for name, literal in (("boolean", "false"), ("null", "null"), ("undefined", "void 0")):
        result["local_field_" + name] = (
            base.replace(
                old,
                "const item = {value: " + literal + "}; state.set(key, item); "
                "const saved = state.get(key); return saved.value === (" + literal + ") ? 1 : 0;",
            ),
            "host",
            1,
        )
    lifetime = base.replace("set(key)", "set(key, value)").replace(
        old,
        field + "state.set(key, {value: 3}); state.delete(key); item.value = value; "
        "return saved === item ? saved.value : 0;",
    )
    result["local_field_readback_lifetime"] = (
        lifetime.replace("host.slot.set('x')", "host.slot.set('x', 2)"),
        "host",
        2,
    )
    for name, (old_return, checked_return) in LEAF_READBACK_CHECKED_RETURNS.items():
        source, binding, value = result[name]
        result[name + "_checked"] = source.replace(old_return, checked_return), binding, value
    return result


LEAF_READBACK_CHECKED_RETURNS = {
    "local_field_direct": ("return item.value;", "return item.value === 1 ? 1 : 0;"),
    "local_field_get_guarded": (
        "return saved === item ? saved.value : 0;",
        "return saved === item && saved.value === 1 ? 1 : 0;",
    ),
    "local_field_saved_overwrite": (
        "return saved === item ? saved.value : 0;",
        "return saved === item && saved.value === 1 ? 1 : 0;",
    ),
    "local_field_saved_delete": (
        "return saved === item ? saved.value : 0;",
        "return saved === item && saved.value === 1 ? 1 : 0;",
    ),
    "local_field_saved_alias_write": (
        "return saved === item ? saved.value : 0;",
        "return saved === item && saved.value === 2 ? 2 : 0;",
    ),
    "local_field_readback_lifetime": (
        "return saved === item ? saved.value : 0;",
        "return saved === item && saved.value === value ? value : 0;",
    ),
}


LEAF_READBACK_CALLS = {
    "local_identity_saved": 6,
    "local_identity_distinct_fresh": 6,
    "local_field_direct": 5,
    "local_field_get": 6,
    "local_field_get_guarded": 6,
    "local_identity_distinct_stored": 7,
    "local_identity_saved_overwrite": 7,
    "local_identity_saved_delete": 7,
    "local_field_saved_overwrite": 7,
    "local_field_saved_delete": 7,
    "local_field_saved_alias_write": 6,
    "local_identity_repeated_keys": 8,
    "historical_object_saved_identity": 8,
    "historical_object_distinct_identity": 8,
    "local_identity_not_equal": 6,
    "local_field_boolean": 6,
    "local_field_null": 6,
    "local_field_undefined": 6,
    "local_field_readback_lifetime": 8,
    "local_field_readback_test": 6,
    "local_field_export_repair": 7,
}
LEAF_READBACK_CALLS.update(
    {name + "_checked": LEAF_READBACK_CALLS[name] for name in LEAF_READBACK_CHECKED_RETURNS}
)

# Keep the exact formerly nullable own-field sources; their initialized reads
# now have an independent per-read presence proof. The schema still joins all
# stored value types, including explicitly written Undefined.
LEAF_FIELD_RESULTS = {"local_field_get", *LEAF_READBACK_CHECKED_RETURNS}
LEAF_COMPARISON_REPAIRS = {
    "local_identity_distinct_fresh": "local_identity_saved",
    "historical_object_distinct_identity": "historical_object_saved_identity",
}
LEAF_COMPARISON_CASES = (*LEAF_COMPARISON_REPAIRS, *LEAF_COMPARISON_REPAIRS.values())
LEAF_READBACK_UNOWNED = set()


def leaf_field_result_refusals():
    positive = leaf_readback_sources()
    rows = {}
    for name, repaired, old, replacement in (
        ("schema_bool", "local_field_saved_overwrite", "{value: 2}", "{value: false}"),
        (
            "explicit_undefined",
            "local_field_get",
            "const item = {value: 1};",
            "const item = {value: void 0}; item.value = 1;",
        ),
    ):
        source, _, value = positive[repaired]
        assert source.count(old) == 1
        rows["local_field_" + name] = (
            source.replace(old, replacement),
            value,
            replacement,
            old,
            repaired,
            LEAF_READBACK_CALLS[repaired],
        )
    return rows


def leaf_readback_refusals():
    positive = leaf_readback_sources()
    field = positive["local_field_get_guarded"][0]
    tested = positive["local_field_readback_test"][0]
    same = positive["local_identity_saved"][0]
    rows = {}
    for name, source, old, replacement, repair, value, calls in (
        (
            "unknown_incoming",
            same,
            "state.set(key, item);",
            "state.has(key);",
            "local_identity_saved",
            0,
            6,
        ),
        ("other_key", same, "state.get(key)", "state.get('other')", "local_identity_saved", 0, 6),
        (
            "object_export",
            positive["local_field_export_repair"][0],
            "return saved === item && saved.value === 1 ? 1 : 0;",
            "return saved;",
            "local_field_export_repair",
            1,
            7,
        ),
        ("uninitialized_field", tested, "value: 1", "other: 1", "local_field_readback_test", 0, 6),
        (
            "dynamic_field_read",
            tested,
            "saved.value",
            "saved[key]",
            "local_field_readback_test",
            0,
            6,
        ),
        (
            "prototype_field_read",
            tested,
            "saved.value",
            "saved.__proto__",
            "local_field_readback_test",
            0,
            6,
        ),
        (
            "unknown_alias_field",
            tested,
            "value: 1",
            "value: key",
            "local_field_readback_test",
            0,
            6,
        ),
        (
            "deleted_before_read",
            same,
            "const saved = state.get(key);",
            "state.delete(key); const saved = state.get(key);",
            "local_identity_saved",
            0,
            7,
        ),
    ):
        rows["leaf_readback_" + name] = (
            source.replace(old, replacement),
            value,
            replacement,
            old,
            repair,
            calls,
        )
    deleted, value, old, replacement, _, calls = leaf_object_refusals()[
        "leaf_object_deleted_identity"
    ]
    rows["historical_object_deleted_identity"] = (
        deleted,
        value,
        old,
        replacement,
        "leaf_object_identity_repair",
        calls,
    )
    direct = field.replace("const saved = state.get(key); ", "")
    direct = direct.replace(
        "return saved === item ? saved.value : 0;",
        "state.delete(key); return state.get(key) === item ? 1 : 0;",
    )
    repaired = positive["local_identity_saved_delete"][0]
    rows["local_identity_after_delete"] = (
        direct,
        0,
        "state.delete(key); return state.get(key) === item ? 1 : 0;",
        "const saved = state.get(key); state.delete(key); return saved === item ? 1 : 0;",
        "local_identity_saved_delete",
        7,
    )
    assert (
        direct.replace(
            rows["local_identity_after_delete"][2], rows["local_identity_after_delete"][3]
        )
        == repaired
    )
    rows["local_identity_repeated_keys"] = (
        positive["local_identity_repeated_keys"][0],
        3,
        "host.slot.set('x') + host.slot.set('x') + host.slot.set('y')",
        "host.slot.set('x')",
        "local_identity_saved",
        8,
    )
    return rows


# Exact thirty-one-source continuation measured in 60b744e1. Hashes include
# the final newline; historical sources and repairs are never rewritten.
LEAF_ABSENCE_SIX_HASHES = {
    "local_identity_distinct_fresh": "be194b3ff4db536532aea5c24b8cffe7827bb16f1fb5869bacb45f1198159b46",
    "historical_object_distinct_identity": "16df1ce7541f6bd86916457f0b6e30944789cd24b27f753a6046f648d3fbb70f",
    "local_identity_saved": "8f7a762b9b19eadfc3a9cb08f1d9469dc1e7a84da8b25b2dfc735ee05ce9bee9",
    "historical_object_saved_identity": "ea27f3b5417898cd676c2aa353afa2e4a794fc078a46748e7bf9b1e45f2ab86f",
    "local_identity_after_delete": "ac773f554cc849271c8167f9d7f93faf703ae1c2941042782b4a878cd1ff9603",
    "historical_object_deleted_identity": "f7dd606a2ca43f70e1a2ea26c4c51518682e27c7cbcb2d80155aa8e679994980",
}


LEAF_ABSENCE_HISTORY = {
    "local_identity_distinct_fresh": (
        6,
        6,
        "be194b3ff4db536532aea5c24b8cffe7827bb16f1fb5869bacb45f1198159b46",
    ),
    "historical_object_distinct_identity": (
        8,
        8,
        "16df1ce7541f6bd86916457f0b6e30944789cd24b27f753a6046f648d3fbb70f",
    ),
    "local_identity_saved": (
        6,
        6,
        "8f7a762b9b19eadfc3a9cb08f1d9469dc1e7a84da8b25b2dfc735ee05ce9bee9",
    ),
    "historical_object_saved_identity": (
        8,
        8,
        "ea27f3b5417898cd676c2aa353afa2e4a794fc078a46748e7bf9b1e45f2ab86f",
    ),
    "local_identity_after_delete": (
        7,
        7,
        "ac773f554cc849271c8167f9d7f93faf703ae1c2941042782b4a878cd1ff9603",
    ),
    "historical_object_deleted_identity": (
        9,
        9,
        "f7dd606a2ca43f70e1a2ea26c4c51518682e27c7cbcb2d80155aa8e679994980",
    ),
    "local_absence_delete_undefined": (
        7,
        7,
        "f3350b8928408e7ca35dfd7a66da79a26d0c917e3d15ff70b4fb4c8901ea4fb5",
    ),
    "local_absence_delete_unseeded": (
        6,
        6,
        "7cb035992513acaeb8d54c21ccee5c6e6fb9becbfd655a32ef2c6ccd93b6f158",
    ),
    "local_absence_delete_number_payload": (
        7,
        7,
        "8509513b19a62bf74917ab03ccaba9a9663206d4d03d56a663ad3429cf46e292",
    ),
    "local_absence_delete_repeated": (
        8,
        8,
        "da3263b29e5d4bb2956bce24c48d6d68ff1f07d19ff923a24f559ff394787f2d",
    ),
    "local_absence_saved_undefined": (
        8,
        8,
        "4ee213ce73918df8e3747933748c0f6d64a557fcbb422caa79cf794949a4462d",
    ),
    "local_absence_reseed_present": (
        8,
        8,
        "f2e5c0858a191a1305b65cb699d120987dbb957db98a6316c9360b04866f334a",
    ),
    "local_absence_clear_undefined": (
        7,
        7,
        "a041e8248d43dac780775c97916939a7e9d88034ce153a24d4576ebbc2f25a16",
    ),
    "local_absence_clear_saved_identity": (
        7,
        7,
        "5aefbb04e557a199248b20305a10953764ee1c14b977eff5b7ce2c4a55fdb024",
    ),
    "local_absence_clear_delete_repair": (
        7,
        7,
        "1e016aa351caea2f3b2ddbb8bfd1207e8f8666d6f27e0f1b00ed80e846c97374",
    ),
    "local_absence_maybe_delete_same": (
        7,
        7,
        "cdfbcbcc16e4516d3fd9a08edde81ffc63d1b267778eb342649e27136119b84b",
    ),
    "local_absence_maybe_delete_same_repair": (
        7,
        7,
        "38fc7f528f5b5f37c90fdc752c333fde481f6862dc032f651509adae40243a24",
    ),
    "local_absence_maybe_overwrite_same": (
        8,
        8,
        "4945c4bd5f341e1302164385b7fadaa3173c322eded2454a06abc1333f4d3736",
    ),
    "local_absence_maybe_delete_distinct": (
        7,
        7,
        "dbf8e092c3d04f51f49e1668f7adf7b800c926abeec9b93598852c9bc446ea30",
    ),
    "local_absence_maybe_delete_distinct_repair": (
        7,
        7,
        "e1dbc8789991996de401443f8fc3beb6dd73b43060e0640d78039b1f17c15e12",
    ),
    "local_absence_maybe_overwrite_distinct": (
        8,
        8,
        "324a473538c0ddd03fee467e6cfefaaf52feddded6f691e16fef5eaa803b9a4a",
    ),
    "local_absence_disjoint_overwrite": (
        8,
        8,
        "2cab6817f281dc926c9073503f6387cca62737319166a120c2617db3f1b72097",
    ),
    "local_absence_same_overwrite": (
        8,
        8,
        "69247a061ae63469937258ebb8d363e39026eb529fc02d2342aed0bf8d0d859b",
    ),
    "local_absence_both_branches_false": (
        8,
        7,
        "74f9761c679b886a698a0d76cd2dfa7e45e463376fce2b40acee7c1f1343a677",
    ),
    "local_absence_one_branch_false": (
        8,
        8,
        "f50b6577ab3fa54fcdac040cbb994f64b824d20920ccdbab3580fb173ba6a4c2",
    ),
    "local_absence_branch_reseed_false": (
        9,
        9,
        "f11bdced6d7d21bc1305ad1d394107f29ba02f767b5d99e518c4958568f0724f",
    ),
    "local_absence_both_branches_false_repair": (
        8,
        7,
        "e212680a19ef874e6d552b01d590066c309054376a9fd8b47046a0c077d3d3d0",
    ),
    "local_absence_both_branches_true": (
        8,
        7,
        "6d2c08e0a121a4f1d09373816fb7e76afc94d87760dd1535acebe4546f62b778",
    ),
    "local_absence_one_branch_true": (
        8,
        8,
        "1b05e80b52332395d22ba181e15c7e4e63063333ea4e89fc21c7f926dd2e36ef",
    ),
    "local_absence_branch_reseed_true": (
        9,
        9,
        "1edf2e628415e9f37edc95125aaed2d199eb23bb5ceb333958f5c45cde1b8501",
    ),
    "local_absence_both_branches_true_repair": (
        8,
        7,
        "49eecbd44a9538437455213372db826a0405c76baaf719a029606a48bc5312cf",
    ),
}


def leaf_absence_cases():
    positives, refusals = leaf_readback_sources(), leaf_readback_refusals()
    rows = {}
    for name, digest in LEAF_ABSENCE_SIX_HASHES.items():
        row = positives.get(name) or refusals[name]
        source = row[0]
        assert hashlib.sha256(source.encode()).hexdigest() == digest, name
        value = row[2] if name in positives else row[1]
        calls = LEAF_READBACK_CALLS[name] if name in positives else row[5]
        rows[name] = dict(
            source=source,
            expected_trace=value,
            functions=5,
            syntactic_source_calls=calls,
            historical_expected_prepared_calls=calls,
            boundary="historical exact six-case continuation",
        )
    base = positives["local_identity_saved_delete"][0]
    body = (
        "const item = {value: 1}; state.set(key, item); const saved = state.get(key); "
        "state.delete(key); return saved === item ? 1 : 0;"
    )
    assert base.count(body) == 1

    def add(name, changed, expected, boundary, *, params="key", actuals="'x'"):
        source = base.replace(body, changed).replace("set(key)", "set(" + params + ")")
        # The signature replacement must not change a Map.set or Map.get.
        source = source.replace("host.slot.set('x')", "host.slot.set(" + actuals + ")")
        rows["local_absence_" + name] = dict(
            source=source,
            expected_trace=expected,
            functions=5,
            syntactic_source_calls=4
            + len(re.findall(r"\bstate\.(?:set|get|has|delete|clear)\(", changed)),
            boundary=boundary,
        )

    prefix = "const item = {value: 1}; state.set(key, item); "
    absent = "return state.get(key) === void 0 ? 1 : 0;"
    present = "return state.get(key) === item ? 1 : 0;"
    add(
        "delete_undefined",
        prefix + "state.delete(key); " + absent,
        1,
        "exact-key delete proves absence; fresh read must remain in source",
    )
    add(
        "delete_unseeded",
        "state.delete(key); " + absent,
        1,
        "exact delete proves absence without assuming entry-start contents",
    )
    add(
        "delete_number_payload",
        "state.set(key, 1); state.delete(key); " + absent,
        1,
        "primitive payload isolates absence from object identity representation",
    )
    add(
        "delete_repeated",
        prefix + "state.delete(key); state.delete(key); " + absent,
        1,
        "a second exact delete preserves known absence",
    )
    add(
        "saved_undefined",
        prefix + "state.delete(key); const saved = state.get(key); "
        "state.set(key, item); return saved === void 0 ? 1 : 0;",
        1,
        "saved absent result must not retarget after a later write",
    )
    add(
        "reseed_present",
        prefix + "state.delete(key); state.set(key, item); " + present,
        1,
        "exact reseed restores the live object; it must invalidate absence",
    )
    add(
        "clear_undefined",
        prefix + "state.clear(); " + absent,
        1,
        "clear is a separate currently unsupported captured-host method",
    )
    add(
        "clear_saved_identity",
        prefix + "const saved = state.get(key); " "state.clear(); return saved === item ? 1 : 0;",
        1,
        "saved identity isolates unsupported clear from fresh absent read",
    )
    add(
        "clear_delete_repair",
        prefix + "const saved = state.get(key); "
        "state.delete(key); return saved === item ? 1 : 0;",
        1,
        "one exact clear-to-delete edit restores historical saved identity",
    )
    assert rows["local_absence_clear_delete_repair"]["source"] == base

    for alias, other, missing in (("same", "'x'", 1), ("distinct", "'other'", 0)):
        actuals = "'x', " + other
        add(
            "maybe_delete_" + alias,
            prefix + "state.delete(other); " + absent,
            missing,
            "two String formals may alias; startup equality is not a future-call proof",
            params="key, other",
            actuals=actuals,
        )
        add(
            "maybe_delete_" + alias + "_repair",
            prefix + "state.has(other); " + present,
            1,
            "same call count; has preserves a definitely present independently seeded leaf",
            params="key, other",
            actuals=actuals,
        )
        add(
            "maybe_overwrite_" + alias,
            prefix + "state.delete(key); " "state.set(other, {value: 2}); " + absent,
            1 - missing,
            "a possibly equal later write destroys definite absence",
            params="key, other",
            actuals=actuals,
        )
    add(
        "disjoint_overwrite",
        prefix + "state.delete('gone'); state.set('other', item); "
        "return state.get('gone') === void 0 ? 1 : 0;",
        1,
        "known distinct literal write preserves exact-key absence",
    )
    add(
        "same_overwrite",
        prefix + "state.delete('gone'); state.set('gone', item); "
        "return state.get('gone') === item ? 1 : 0;",
        1,
        "same-key overwrite replaces absence with an independently known live object",
    )

    for flag in (False, True):
        word = str(flag).lower()
        add(
            "both_branches_" + word,
            prefix + "if (flag) { state.delete(key); } else { state.delete(key); } " + absent,
            1,
            "both live arms prove exact absence; branch join must intersect knowledge",
            params="key, flag",
            actuals="'x', " + word,
        )
        add(
            "one_branch_" + word,
            prefix + "if (flag) { state.delete(key); } else { state.has(key); } " + absent,
            int(flag),
            "one deleting arm is maybe absent; do not infer Undefined from present=false",
            params="key, flag",
            actuals="'x', " + word,
        )
        add(
            "branch_reseed_" + word,
            prefix
            + "state.delete(key); if (flag) { state.set(key, item); } else { state.delete(key); } "
            + absent,
            int(not flag),
            "one same-key reseed invalidates joined absence",
            params="key, flag",
            actuals="'x', " + word,
        )
        add(
            "both_branches_" + word + "_repair",
            prefix + "if (flag) { state.has(key); } else { state.has(key); } " + present,
            1,
            "same source calls/branches; both nondestructive arms preserve the seeded leaf",
            params="key, flag",
            actuals="'x', " + word,
        )

    repairs = {
        "local_absence_clear_saved_identity": (
            "local_absence_clear_delete_repair",
            "state.clear();",
            "state.delete(key);",
        ),
        "local_absence_delete_undefined": (
            "local_identity_saved_delete",
            "state.delete(key); return state.get(key) === void 0 ? 1 : 0;",
            "const saved = state.get(key); state.delete(key); return saved === item ? 1 : 0;",
        ),
    }
    for flag in ("false", "true"):
        repairs["local_absence_both_branches_" + flag] = (
            "local_absence_both_branches_" + flag + "_repair",
            "if (flag) { state.delete(key); } else { state.delete(key); } " + absent,
            "if (flag) { state.has(key); } else { state.has(key); } " + present,
        )
    for name, (repair, old, replacement) in repairs.items():
        repaired = rows[repair]["source"] if repair in rows else positives[repair][0]
        assert rows[name]["source"].count(old) == 1, name
        assert rows[name]["source"].replace(old, replacement) == repaired, name
        rows[name].update(exact_repair=repair, removed_text=old, replacement_text=replacement)
    for flag in (False, True):
        word = str(flag).lower()
        add(
            "distinct_branches_" + word,
            prefix
            + "if (flag) { state.delete(key); } else { state.delete(key); state.has(key); } "
            + absent,
            1,
            "nonidentical safe arms must survive LiftToSCF and intersect exact absence",
            params="key, flag",
            actuals="'x', " + word,
        )
    for name, row in rows.items():
        if name in LEAF_ABSENCE_HISTORY:
            raw, prepared, digest = LEAF_ABSENCE_HISTORY[name]
            assert hashlib.sha256(row["source"].encode()).hexdigest() == digest, name
            assert row["syntactic_source_calls"] == raw, name
        else:
            raw = prepared = row["syntactic_source_calls"]
        row.update(raw_calls=raw, prepared_calls=prepared)
    return rows


LEAF_ABSENCE_UNOWNED = {
    "local_absence_clear_undefined",
    "local_absence_clear_saved_identity",
    "local_absence_maybe_delete_same",
    "local_absence_maybe_delete_distinct",
    "local_absence_maybe_overwrite_same",
    "local_absence_maybe_overwrite_distinct",
    "local_absence_one_branch_false",
    "local_absence_one_branch_true",
    "local_absence_branch_reseed_false",
    "local_absence_branch_reseed_true",
}
LEAF_ABSENCE_PROMOTED_REFUSALS = {
    "leaf_object_deleted_identity",
    "historical_object_deleted_identity",
    "local_identity_after_delete",
    "leaf_readback_deleted_before_read",
}


def leaf_absence_sources():
    existing = leaf_readback_sources()
    result = {
        name: (row["source"], "host", row["expected_trace"])
        for name, row in leaf_absence_cases().items()
        if name not in LEAF_ABSENCE_UNOWNED and name not in existing
    }
    # Preserve both original refusal aliases as independently compiled sources.
    for name, row in {**leaf_object_refusals(), **leaf_readback_refusals()}.items():
        if name in LEAF_ABSENCE_PROMOTED_REFUSALS:
            result[name] = row[0], "host", row[1]
    return result


def leaf_absence_refusals():
    cases = leaf_absence_cases()
    rows = {}
    edits = {
        "local_absence_clear_undefined": (
            "state.clear();",
            "state.delete(key);",
            "local_absence_delete_undefined",
        ),
        "local_absence_clear_saved_identity": (
            "state.clear();",
            "state.delete(key);",
            "local_absence_clear_delete_repair",
        ),
    }
    for alias in ("same", "distinct"):
        repair = "local_absence_maybe_delete_" + alias + "_repair"
        edits["local_absence_maybe_delete_" + alias] = (
            "state.delete(other); return state.get(key) === void 0 ? 1 : 0;",
            "state.has(other); return state.get(key) === item ? 1 : 0;",
            repair,
        )
        edits["local_absence_maybe_overwrite_" + alias] = (
            "state.delete(key); state.set(other, {value: 2}); return state.get(key) === void 0 ? 1 : 0;",
            "state.has(other); return state.get(key) === item ? 1 : 0;",
            repair,
        )
    for flag in ("false", "true"):
        repair = "local_absence_both_branches_" + flag
        edits["local_absence_one_branch_" + flag] = (
            "else { state.has(key); }",
            "else { state.delete(key); }",
            repair,
        )
        edits["local_absence_branch_reseed_" + flag] = (
            "state.delete(key); if (flag) { state.set(key, item); }",
            "if (flag) { state.delete(key); }",
            repair,
        )
    assert set(edits) == LEAF_ABSENCE_UNOWNED
    for name, (old, replacement, repair) in edits.items():
        row = cases[name]
        assert row["source"].count(old) == 1, name
        assert row["source"].replace(old, replacement) == cases[repair]["source"], name
        rows[name] = (
            row["source"],
            row["expected_trace"],
            old,
            replacement,
            repair,
            row["prepared_calls"],
        )
    return rows
