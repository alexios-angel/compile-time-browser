from .sources_globals import (
    NUMERIC_ENTRY_HISTORY,
    StringValue,
    numeric_entry_cases,
)
from .sources_object_maps import (
    leaf_object_sources,
)
from .sources_nullable_maps import parameter_refusals
from .sources_scalar_maps import (
    hashlib,
    json,
)

STRING_FIELD_BYTES = StringValue('quoted "field"\n\t%\\=;雪\x00tail')
STRING_FIELD_LONG = StringValue("saved field contents-" * 64)
STRING_FIELD_PROMOTED = {"leaf_object_string_field"}


def string_field_cases():
    base = leaf_object_sources()["leaf_object_identity_repair"][0]
    old = "const item = {value: 1}; state.set(key, item); return state.size;"
    assert base.count(old) == 1
    rows = {}

    def add(name, source, value, calls, *, repair=None, owner=True, admitted=True):
        rows[name] = dict(
            source=source,
            expected_trace=value,
            raw_calls=calls,
            prepared_calls=calls,
            sha256=hashlib.sha256(source.encode()).hexdigest(),
            owner=owner,
            admitted=admitted,
        )
        if repair:
            removed, replacement, target = repair
            assert source.count(removed) == 1, name
            assert source.replace(removed, replacement) == rows[target]["source"], name
            rows[name].update(removed_text=removed, replacement_text=replacement, repair=target)

    historical = numeric_entry_cases()["leaf_object_string_field"]
    add("leaf_object_string_field", historical["source"], 2, 7)
    assert (
        rows["leaf_object_string_field"]["sha256"]
        == NUMERIC_ENTRY_HISTORY["leaf_object_string_field"][2]
    )
    number = leaf_object_sources()["leaf_object_number_field"][0]
    assert (
        hashlib.sha256(number.encode()).hexdigest()
        == NUMERIC_ENTRY_HISTORY["leaf_object_number_field"][2]
    )

    def body(name, replacement, value, calls=5):
        add(name, base.replace(old, replacement), value, calls)

    body(
        "field_string_read",
        "const item = {value: 'instance'}; state.set(key, item); return item.value;",
        StringValue("instance"),
    )
    body(
        "field_string_empty",
        "const item = {value: ''}; state.set(key, item); return item.value;",
        StringValue(""),
    )
    body(
        "field_string_bytes",
        "const item = {value: "
        + json.dumps(STRING_FIELD_BYTES)
        + "}; state.set(key, item); return item.value;",
        STRING_FIELD_BYTES,
    )
    body(
        "field_string_saved",
        "const item = {value: " + json.dumps(STRING_FIELD_LONG) + "}; "
        "state.set(key, item); const alias = state.get(key); const saved = alias.value; "
        "alias.value = 'changed'; state.delete(key); return saved;",
        STRING_FIELD_LONG,
        7,
    )
    body(
        "field_string_alias_write",
        "const item = {value: 'first'}; state.set(key, item); const alias = item; "
        "alias.value = 'changed'; return item.value;",
        StringValue("changed"),
    )
    body(
        "field_string_get",
        "const item = {value: 'instance'}; state.set(key, item); "
        "const alias = state.get(key); return alias.value;",
        StringValue("instance"),
        6,
    )
    body(
        "field_string_separate_members",
        "const item = {value: " + json.dumps(STRING_FIELD_BYTES) + ", count: 7, flag: false}; "
        "state.set(key, item); item.count = 9; item.flag = true; return item.value;",
        STRING_FIELD_BYTES,
    )
    for tag, literal in (("null", "null"), ("undefined", "void 0")):
        body(
            "field_string_" + tag + "_store",
            "const item = {value: " + literal + "}; item.value = 'instance'; "
            "state.set(key, item); return item.value === 'instance' ? 1 : 0;",
            1,
        )
    body(
        "field_string_saved_overwrite",
        "const item = {value: " + json.dumps(STRING_FIELD_LONG) + "}; "
        "state.set(key, item); const alias = state.get(key); const saved = alias.value; "
        "state.set(key, {value: 'replacement'}); item.value = 'changed'; "
        "state.delete(key); return saved;",
        STRING_FIELD_LONG,
        8,
    )
    lifetime = base.replace("set(key)", "set(key, value, flag)").replace(
        old,
        "const item = {value: value}; state.set(key, item); const alias = state.get(key); "
        "const saved = alias.value; if (flag) { alias.value = 'left'; } "
        "else { item.value = 'right'; } state.delete(key); return saved;",
    )
    lifetime = lifetime.replace(
        "host.slot.set('x')", "host.slot.set('x', " + json.dumps(STRING_FIELD_LONG) + ", false)"
    )
    add("field_string_lifetime", lifetime, STRING_FIELD_LONG, 7)

    read = rows["field_string_read"]["source"]
    for name, removed, replacement, value, owner in (
        (
            "field_mixed_write_control",
            "return item.value;",
            "item.value = 1; return item.value;",
            1,
            True,
        ),
        (
            "field_dynamic_write_control",
            "return item.value;",
            "item[key] = 'changed'; return item.value;",
            StringValue("instance"),
            False,
        ),
        (
            "field_prototype_control",
            "return item.value;",
            "item.__proto__ = {}; return item.value;",
            StringValue("instance"),
            False,
        ),
        ("field_string_missing", "value: 'instance'", "other: 'instance'", "undefined", False),
        ("field_string_dynamic_read", "item.value", "item[key]", "undefined", False),
    ):
        add(
            name,
            read.replace(removed, replacement),
            value,
            5,
            repair=(replacement, removed, "field_string_read"),
            owner=owner,
            admitted=False,
        )
    saved = rows["field_string_saved_overwrite"]["source"]
    add(
        "field_string_other_allocation_number",
        saved.replace("{value: 'replacement'}", "{value: 7}"),
        STRING_FIELD_LONG,
        8,
        repair=("{value: 7}", "{value: 'replacement'}", "field_string_saved_overwrite"),
        admitted=False,
    )
    return rows


def string_field_sources():
    return {
        name: (row["source"], "host", row["expected_trace"])
        for name, row in string_field_cases().items()
        if row["admitted"]
    }


# Exact sources of the thirteen-row post-String-field continuation probe.
ZERO_SIZE_HISTORY = {
    "local_clear_zero_size_key": (
        8,
        "496635583adf728c73d3a48a71a98d7e4733a2bd9a5d14d17d21b1660359b316",
    ),
    "local_clear_zero_literal_repair": (
        8,
        "33aa4c4a24bba6adeec8cc2711e406190f25942f3829ee0c35efae2c9dba36a0",
    ),
    "zero_size_captured_alias": (
        8,
        "cceb9728650e2925ba748fd9a27fc57f4d30553e16730fa0ee4de0b42d4d9ef4",
    ),
    "zero_size_string_leaf": (
        8,
        "1e44bcc417f20088f63c93a7a4c2fb91aa17781d30c7e9606c8a0d6eec06ddfc",
    ),
    "zero_size_repeated_clear": (
        9,
        "191a9cb3efcae5015bafda9da3c20c6756b8f8a5e439c07204f34b75d365e492",
    ),
    "zero_size_saved_growth": (
        9,
        "4071dc58f3ec2f5d482627ddad087468a206f16587a653299a3fcde5ba2b23ec",
    ),
    "zero_size_equal_key": (8, "cc2c90639e721c7a96232f1498b12982c3c18123bbb6c0641d4d1ce9bc3ab71e"),
    "zero_size_read_after_write": (
        8,
        "544f425bc1c5766d6176bbc358947a58ebdd423f4d6a517546c93fa22f67627e",
    ),
    "zero_size_read_before_clear": (
        8,
        "0dd2cee611397cd0d548a6492c40fb094fac983aeed235b4277664792b7b4e16",
    ),
    "zero_size_both_clear_false": (
        10,
        "300e1269bf254ab5d3412b4bdc6e93d7ba597c8c830bf78bea428c24a2414cf2",
    ),
    "zero_size_both_clear_true": (
        10,
        "657c263d050eff8303d8fae6a9b510c8be40b07dd142c98c05fbb65e3633df2b",
    ),
    "zero_size_one_clear_false": (
        9,
        "b98bd0d0bc499ddbb41fda01a808acaacfe7ad0170f12728eb3a1603e447bbe6",
    ),
    "zero_size_one_clear_true": (
        9,
        "9cc3dd808711f667c9241c2b882905a7c23357e059eac7889e172b6a6f2203df",
    ),
}


def zero_size_cases():
    rows = {}
    old = numeric_entry_cases()
    base = old["local_clear_zero_size_key"]["source"]

    def add(name, source, value, calls, admitted=True, repair=None):
        digest = hashlib.sha256(source.encode()).hexdigest()
        if name in ZERO_SIZE_HISTORY:
            assert (calls, digest) == ZERO_SIZE_HISTORY[name], name
        row = dict(
            source=source,
            expected_trace=value,
            raw_calls=calls,
            prepared_calls=calls,
            sha256=digest,
            admitted=admitted,
        )
        if repair:
            before, after, target = repair
            assert (
                source.count(before) == 1
                and source.replace(before, after) == rows[target]["source"]
            ), name
            row.update(removed_text=before, replacement_text=after, repair=target)
        rows[name] = row

    add("local_clear_zero_size_key", base, 1, 8)
    add("local_clear_zero_literal_repair", old["local_clear_zero_literal_repair"]["source"], 1, 8)
    add(
        "zero_size_captured_alias",
        base.replace(
            "state.clear(); const zero = state.size;",
            "const alias = state; alias.clear(); const zero = alias.size;",
        ),
        1,
        8,
    )
    add("zero_size_string_leaf", base.replace("{value: 1}", "{value: 'instance'}"), 1, 8)
    add(
        "zero_size_repeated_clear",
        base.replace("state.clear();", "state.clear(); state.clear();"),
        1,
        9,
    )
    add(
        "zero_size_saved_growth",
        base.replace("state.set(1, item);", "state.set(1, item); state.set(2, item);"),
        1,
        9,
    )
    add("zero_size_equal_key", base.replace("state.set(1, item);", "state.set(0, item);"), 0, 8)
    # Unary Neg still lacks the host key-value proof. Keep this source intact;
    # raw Number negative-zero bits exercise SameValueZero independently.
    add(
        "zero_size_negative_zero",
        base.replace("state.set(1, item);", "state.set(-0, item);"),
        0,
        8,
        False,
        ("state.set(-0, item);", "state.set(0, item);", "zero_size_equal_key"),
    )
    add(
        "zero_size_read_after_write",
        base.replace(
            "const zero = state.size; state.set(1, item);",
            "state.set(1, item); const zero = state.size;",
        ),
        0,
        8,
        True,
        (
            "state.set(1, item); const zero = state.size;",
            "const zero = state.size; state.set(1, item);",
            "local_clear_zero_size_key",
        ),
    )
    add(
        "zero_size_read_before_clear",
        base.replace(
            "state.clear(); const zero = state.size;", "const zero = state.size; state.clear();"
        ),
        0,
        8,
        False,
        (
            "const zero = state.size; state.clear();",
            "state.clear(); const zero = state.size;",
            "local_clear_zero_size_key",
        ),
    )
    branch = base.replace("set(key)", "set(key, flag)")
    for flag in ("false", "true"):
        source = branch.replace("host.slot.set(7)", "host.slot.set(7, " + flag + ")")
        both = source.replace(
            "state.clear();", "if (flag) { state.clear(); } else { state.clear(); state.has(key); }"
        )
        both_name = "zero_size_both_clear_" + flag
        add(both_name, both, 1, 10)
        add(
            "zero_size_one_clear_" + flag,
            both.replace("else { state.clear(); state.has(key); }", "else { state.has(key); }"),
            int(flag == "true"),
            9,
            False,
            ("else { state.has(key); }", "else { state.clear(); state.has(key); }", both_name),
        )
        # Intersecting the known-key lists produces no common entries although
        # every runtime path is nonempty. Equal cardinality now proves saved
        # one independently; a later real clear instead establishes zero.
        intersect = source.replace(
            "state.clear();",
            "state.clear(); if (flag) { state.set(1, item); } else { state.set(2, item); }",
        )
        repaired = intersect.replace(
            "} const zero = state.size;", "} state.clear(); const zero = state.size;"
        )
        repair_name = "zero_size_cleared_join_" + flag
        add(repair_name, repaired, 1, 11)
        add(
            "zero_size_empty_intersection_" + flag,
            intersect,
            0,
            10,
            True,
            (
                "} const zero = state.size;",
                "} state.clear(); const zero = state.size;",
                repair_name,
            ),
        )
        joined = source.replace("const zero = state.size;", "const zero = flag ? state.size : 0;")
        joined_name = "zero_size_joined_zero_" + flag
        add(joined_name, joined, 1, 8)
        add(
            "zero_size_joined_nonzero_" + flag,
            joined.replace("flag ? state.size : 0;", "flag ? state.size : 1;"),
            int(flag == "true"),
            8,
            False,
            ("flag ? state.size : 1;", "flag ? state.size : 0;", joined_name),
        )
    lifetime = rows["zero_size_both_clear_false"]["source"]
    lifetime = lifetime.replace(
        "state.set(1, item); return state.get(zero) === void 0 ? 1 : 0;",
        "state.set(0, item); const saved = state.get(zero); state.clear(); state.set(1, item); "
        "return saved === item ? (flag ? 2 : 1) : 0;",
    )
    add("zero_size_saved_lifetime", lifetime, 1, 12)
    return rows


def zero_size_sources():
    return {
        name: (row["source"], "host", row["expected_trace"])
        for name, row in zero_size_cases().items()
        if row["admitted"]
    }


# Preserve the eight exact sources of the saved-one continuation probe.
ONE_SIZE_HISTORY = {
    "zero_size_read_after_write": (
        8,
        8,
        "544f425bc1c5766d6176bbc358947a58ebdd423f4d6a517546c93fa22f67627e",
    ),
    "size_one_literal_repair": (
        8,
        8,
        "d4120093ea2a496cffeacaf09611ce80a23ffbf547700e74ef23d56618c72192",
    ),
    "size_one_saved_growth": (
        9,
        9,
        "e9260d3055dd5ca7f2343c498608d6c1a8dfb0cbc1e75af2ef9927f74a9c1a60",
    ),
    "size_one_both_branches": (
        9,
        8,
        "f0571cb51770699ea09977d36da75b0f988d1a945928a797de1741973a10dbc2",
    ),
    "startup_empty_before_write": (
        6,
        6,
        "cf4a883f47920d4748645d4fceefed9c2d6d1b2eaa827f1f4567cde2b70935c1",
    ),
    "startup_empty_literal_repair": (
        6,
        6,
        "3db19ed0a9d5895fb0dbb3815c3c4a193cc8768995d4aeca8e0468b9ef8617f0",
    ),
    "size_deleted_last": (
        10,
        10,
        "6aa0868a86f765f22c30b09cc9e8494d9847c8e5d4e8f1bbaf6f3df1bd96276e",
    ),
    "size_deleted_last_literal_repair": (
        10,
        10,
        "adeaad915f06d7f5a186f589413f1556e671e6a5306b6e0afdf3cd4b184aa456",
    ),
}


def one_size_cases():
    rows = {}
    old = zero_size_cases()
    base = old["zero_size_read_after_write"]["source"]

    def add(name, source, value, calls, admitted=True, repair=None, prepared=None):
        digest = hashlib.sha256(source.encode()).hexdigest()
        prepared = calls if prepared is None else prepared
        if name in ONE_SIZE_HISTORY:
            assert (calls, prepared, digest) == ONE_SIZE_HISTORY[name], name
        row = dict(
            source=source,
            expected_trace=value,
            raw_calls=calls,
            prepared_calls=prepared,
            sha256=digest,
            admitted=admitted,
        )
        if repair:
            before, after, target = repair
            assert (
                source.count(before) == 1
                and source.replace(before, after) == rows[target]["source"]
            ), name
            row.update(removed_text=before, replacement_text=after, repair=target)
        rows[name] = row

    add("zero_size_read_after_write", base, 0, 8)
    # A raw own-field read requires the exact-one key to select the object.
    # A mistaken absence proof cannot pass through a nullable comparison.
    field = base.replace(
        "return state.get(zero) === void 0 ? 1 : 0;", "return state.get(zero).value;"
    )
    add(
        "size_one_present_field_entry_repair",
        field.replace("var trace = host.slot.set(7);", "var trace = host.slot.set(7) + 0;"),
        1,
        8,
    )
    add(
        "size_one_present_field_checked_repair",
        field.replace(
            "return state.get(zero).value;", "return state.get(zero).value === 1 ? 1 : 0;"
        ),
        1,
        8,
    )
    add(
        "size_one_present_field",
        field,
        1,
        8,
        False,
        (
            "var trace = host.slot.set(7);",
            "var trace = host.slot.set(7) + 0;",
            "size_one_present_field_entry_repair",
        ),
    )
    rows["size_one_present_field"]["owner"] = True
    add(
        "size_one_literal_repair",
        base.replace("const zero = state.size;", "state.size; const zero = 1;"),
        0,
        8,
    )
    add(
        "size_one_saved_growth",
        base.replace("const zero = state.size;", "const zero = state.size; state.set(2, item);"),
        0,
        9,
    )
    branch = base.replace("set(key)", "set(key, flag)").replace(
        "host.slot.set(7)", "host.slot.set(7, false)"
    )
    both = branch.replace(
        "state.clear(); state.set(1, item);",
        "state.clear(); if (flag) { state.set(1, item); } else { state.set(1, item); }",
    )
    add("size_one_both_branches", both, 0, 9, prepared=8)
    add(
        "size_one_captured_alias",
        base.replace(
            "state.clear(); state.set(1, item); const zero = state.size;",
            "const alias = state; alias.clear(); alias.set(1, item); const zero = alias.size;",
        ),
        0,
        8,
    )
    add(
        "size_one_fluent_alias",
        base.replace(
            "state.set(1, item); const zero = state.size;",
            "const alias = state.set(1, item); const zero = alias.size;",
        ),
        0,
        8,
    )
    add(
        "size_one_repeated_key",
        base.replace(
            "state.clear(); state.set(1, item);",
            "state.clear(); state.set(1, item); state.set(1, item);",
        ),
        0,
        9,
    )
    add(
        "size_one_saved_growth_gap",
        base.replace("const zero = state.size;", "const zero = state.size; state.set(3, item);"),
        0,
        9,
    )
    two = base.replace(
        "state.clear(); state.set(1, item);",
        "state.clear(); state.set(1, item); state.set(2, item);",
    )
    add("size_one_complete_two", two, 0, 9)
    add(
        "size_one_saved_two_growth_gap",
        two.replace("const zero = state.size;", "const zero = state.size; state.set(4, item);"),
        0,
        10,
    )
    add(
        "size_one_possible_formal_key",
        rows["size_one_repeated_key"]["source"].replace(
            "state.clear(); state.set(1, item);", "state.clear(); state.set(key, item);"
        ),
        1,
        9,
        False,
        (
            "state.clear(); state.set(key, item);",
            "state.clear(); state.set(1, item);",
            "size_one_repeated_key",
        ),
    )

    for flag in ("false", "true"):
        distinct = both.replace(
            "host.slot.set(7, false)", "host.slot.set(7, " + flag + ")"
        ).replace("else { state.set(1, item); }", "else { state.set(1, item); state.has(key); }")
        both_name = "size_one_distinct_branches_" + flag
        add(both_name, distinct, 0, 10)
        add(
            "size_one_one_writing_arm_" + flag,
            distinct.replace(
                "else { state.set(1, item); state.has(key); }", "else { state.has(key); }"
            ),
            int(flag == "false"),
            9,
            False,
            ("else { state.has(key); }", "else { state.set(1, item); state.has(key); }", both_name),
        )
        source = branch.replace("host.slot.set(7, false)", "host.slot.set(7, " + flag + ")")
        joined = source.replace("const zero = state.size;", "const zero = flag ? state.size : 1;")
        joined_name = "size_one_joined_equal_" + flag
        add(joined_name, joined, 0, 8)
        add(
            "size_one_joined_unequal_" + flag,
            joined.replace("flag ? state.size : 1;", "flag ? state.size : 2;"),
            int(flag == "false"),
            8,
            False,
            ("flag ? state.size : 2;", "flag ? state.size : 1;", joined_name),
        )
        duplicate = source.replace(
            "const zero = state.size;",
            "if (flag) { state.set(1, item); } else { state.has(key); } const zero = state.size;",
        )
        duplicate_name = "size_one_optional_duplicate_" + flag
        add(duplicate_name, duplicate, 0, 10)
        add(
            "size_one_optional_distinct_" + flag,
            duplicate.replace(
                "if (flag) { state.set(1, item); }", "if (flag) { state.set(2, item); }"
            ),
            0,
            10,
            False,
            (
                "if (flag) { state.set(2, item); }",
                "if (flag) { state.set(1, item); }",
                duplicate_name,
            ),
        )

    # Startup-only emptiness still needs a separately evaluated clear.
    # Exact deletion now also proves the two unchanged historical formal-key sources.
    fresh = old["local_clear_zero_size_key"]["source"].replace(
        "state.set(key, item); state.clear(); ", ""
    )
    deleted = old["local_clear_zero_size_key"]["source"].replace(
        "state.clear(); const zero",
        "state.clear(); state.set(key, item); state.delete(key); const zero",
    )
    for name, source, calls in (
        ("startup_empty_before_write", fresh, 6),
        (
            "startup_empty_literal_repair",
            fresh.replace("const zero = state.size;", "state.size; const zero = 0;"),
            6,
        ),
        ("size_deleted_last", deleted, 10),
        (
            "size_deleted_last_literal_repair",
            deleted.replace("const zero = state.size;", "state.size; const zero = 0;"),
            10,
        ),
    ):
        read = (
            "state.size; const zero = 0;"
            if "literal_repair" in name
            else "const zero = state.size;"
        )
        repaired_name = "size_one_cleared_" + name
        add(repaired_name, source.replace(read, "state.clear(); " + read), 1, calls + 1)
        add(
            name,
            source,
            1,
            calls,
            name.startswith("size_deleted_"),
            (read, "state.clear(); " + read, repaired_name),
        )

    lifetime = rows["size_one_distinct_branches_false"]["source"].replace(
        "return state.get(zero) === void 0 ? 1 : 0;",
        "state.set(3, item); const saved = state.get(zero); state.clear(); state.set(2, item); "
        "return saved === item ? (flag ? 2 : 1) : 0;",
    )
    add("size_one_saved_lifetime", lifetime, 1, 13)
    return rows


def one_size_sources():
    return {
        name: (row["source"], "host", row["expected_trace"])
        for name, row in one_size_cases().items()
        if row["admitted"]
    }


# Every byte of the ten measured delete-size continuation sources is retained.
DELETE_SIZE_HISTORY = {
    "size_deleted_last": (10, "6aa0868a86f765f22c30b09cc9e8494d9847c8e5d4e8f1bbaf6f3df1bd96276e"),
    "size_deleted_last_literal_repair": (
        10,
        "adeaad915f06d7f5a186f589413f1556e671e6a5306b6e0afdf3cd4b184aa456",
    ),
    "size_deleted_last_cleared_repair": (
        11,
        "79587f9c4ad4c2bac6938a215324095d799c4878c2b99e90a9ab79b02be6822e",
    ),
    "size_deleted_literal_last": (
        10,
        "d5a66fc6f63b8aeb3976e570a16f9eab2e3aa6302c520b1f9c9b45a9953d3f5f",
    ),
    "size_deleted_literal_last_zero_repair": (
        10,
        "315c5f00fd4cee28236efb7779f91865f4549ba6d8398616f5dd366c8f2aa224",
    ),
    "size_deleted_one_of_two": (
        10,
        "aeaca3a032e3b77676839ab3318afc295fd73fe65ed8d010cf4e922e6b812c59",
    ),
    "size_deleted_one_of_two_literal_repair": (
        10,
        "b1c69bca8625eaa824e2f44b2c1b870eef91d0e01671130783f13946f4d06ab8",
    ),
    "size_saved_two_before_delete": (
        10,
        "00e098c19fdcdc3da8389f4901a06232ba7f433058fa08ca294fd11f659f6e38",
    ),
    "size_deleted_both_branches_false": (
        12,
        "f662d55b1ea0e8784b62f7ff006b1279b13260391d3279a7880372c93f3fb530",
    ),
    "size_deleted_both_branches_true": (
        12,
        "827b4258cf4cf8d807a4bc5460668c1025125487145865e7a22559448066b5df",
    ),
}


def delete_size_cases():
    rows = {}

    def add(name, source, value, calls, admitted=True, repair=None):
        digest = hashlib.sha256(source.encode()).hexdigest()
        if name in DELETE_SIZE_HISTORY:
            assert (calls, digest) == DELETE_SIZE_HISTORY[name], name
        row = dict(
            source=source,
            expected_trace=value,
            raw_calls=calls,
            prepared_calls=calls,
            sha256=digest,
            admitted=admitted,
        )
        if repair:
            before, after, target = repair
            assert (
                source.count(before) == 1
                and source.replace(before, after) == rows[target]["source"]
            ), name
            row.update(removed_text=before, replacement_text=after, repair=target)
        rows[name] = row

    old = one_size_cases()
    formal = old["size_deleted_last"]["source"]
    add(
        "size_deleted_last_cleared_repair",
        old["size_one_cleared_size_deleted_last"]["source"],
        1,
        11,
    )
    for name in ("size_deleted_last", "size_deleted_last_literal_repair"):
        rows[name] = dict(old[name])
        if name == "size_deleted_last":
            rows[name]["repair"] = "size_deleted_last_cleared_repair"
        else:
            repair = old[name]["repair"]
            rows[repair] = dict(old[repair])
    base = formal.replace(
        "state.clear(); state.set(key, item); state.delete(key);",
        "state.clear(); state.set(1, item); state.delete(1);",
    )
    add("size_deleted_literal_last", base, 1, 10)
    add(
        "size_deleted_literal_last_zero_repair",
        base.replace("const zero = state.size;", "state.size; const zero = 0;"),
        1,
        10,
    )
    two = base.replace(
        "state.set(1, item); state.delete(1);",
        "state.set(1, item); state.set(2, item); state.delete(1);",
    ).replace("const zero = state.size; state.set(1, item);", "const zero = state.size;")
    add("size_deleted_one_of_two", two, 1, 10)
    add(
        "size_deleted_one_of_two_literal_repair",
        two.replace("const zero = state.size;", "state.size; const zero = 1;"),
        1,
        10,
    )
    add(
        "size_saved_two_before_delete",
        two.replace(
            "state.delete(1); const zero = state.size;", "const zero = state.size; state.delete(2);"
        ),
        1,
        10,
    )
    add("size_deleted_disjoint", base.replace("state.delete(1);", "state.delete(9);"), 0, 10)
    add(
        "size_deleted_repeated",
        base.replace("state.delete(1);", "state.delete(1); state.delete(1);"),
        1,
        11,
    )
    add(
        "size_deleted_captured_alias",
        base.replace(
            "state.delete(1); const zero = state.size;",
            "const alias = state; alias.delete(1); const zero = alias.size;",
        ),
        1,
        10,
    )
    add(
        "size_deleted_fluent_alias",
        base.replace(
            "state.set(1, item); state.delete(1);",
            "const alias = state.set(1, item); alias.delete(1);",
        ),
        1,
        10,
    )
    add(
        "size_deleted_saved_key",
        base.replace("state.delete(1);", "const one = state.size; state.delete(one);"),
        1,
        10,
    )
    add(
        "size_deleted_possible_alias",
        base.replace("state.delete(1);", "state.delete(key);"),
        0,
        10,
        False,
        ("state.delete(key);", "state.delete(1);", "size_deleted_literal_last"),
    )
    add(
        "size_deleted_possible_remaining_key",
        base.replace(
            "state.clear(); state.set(1, item);",
            "state.clear(); state.set(key, item); state.set(1, item);",
        ),
        0,
        11,
        False,
        (
            "state.clear(); state.set(key, item); state.set(1, item);",
            "state.clear(); state.set(1, item);",
            "size_deleted_literal_last",
        ),
    )

    # Both witnesses force the independently proved current key to select a
    # real object and read its field. A wrong cardinality cannot fake absence.
    zero_field = base.replace(
        "const zero = state.size; state.set(1, item);",
        "const zero = state.size; state.set(0, item);",
    ).replace(
        "return state.get(zero) === void 0 ? 1 : 0;", "return state.get(zero).value === 1 ? 1 : 0;"
    )
    add("size_deleted_zero_present_field", zero_field, 1, 10)
    one_field = two.replace("state.delete(1);", "state.delete(2);").replace(
        "return state.get(zero) === void 0 ? 1 : 0;", "return state.get(zero).value === 1 ? 1 : 0;"
    )
    add("size_deleted_one_present_field", one_field, 1, 10)

    for flag in ("false", "true"):
        branch = base.replace("set(key)", "set(key, flag)").replace(
            "host.slot.set(7)", "host.slot.set(7, " + flag + ")"
        )
        both = branch.replace(
            "state.delete(1);",
            "if (flag) { state.delete(1); } else { state.delete(1); state.has(key); }",
        )
        name = "size_deleted_both_branches_" + flag
        add(name, both, 1, 12)
        add(
            "size_deleted_one_branch_" + flag,
            both.replace("else { state.delete(1); state.has(key); }", "else { state.has(key); }"),
            int(flag == "true"),
            11,
            False,
            ("else { state.has(key); }", "else { state.delete(1); state.has(key); }", name),
        )
        joined = branch.replace("const zero = state.size;", "const zero = flag ? state.size : 0;")
        name = "size_deleted_joined_equal_" + flag
        add(name, joined, 1, 10)
        add(
            "size_deleted_joined_unequal_" + flag,
            joined.replace("flag ? state.size : 0;", "flag ? state.size : 1;"),
            int(flag == "true"),
            10,
            False,
            ("flag ? state.size : 1;", "flag ? state.size : 0;", name),
        )

    lifetime = rows["size_deleted_both_branches_false"]["source"].replace(
        "state.set(1, item); return state.get(zero) === void 0 ? 1 : 0;",
        "state.set(0, item); state.set(3, item); const saved = state.get(zero); "
        "state.delete(0); state.clear(); state.set(2, item); "
        "return saved === item ? (flag ? 2 : 1) : 0;",
    )
    add("size_deleted_saved_lifetime", lifetime, 1, 16)
    for name, (calls, digest) in DELETE_SIZE_HISTORY.items():
        assert (rows[name]["raw_calls"], rows[name]["sha256"]) == (calls, digest), name
    return rows


def delete_size_sources():
    return {
        name: (row["source"], "host", row["expected_trace"])
        for name, row in delete_size_cases().items()
        if row["admitted"]
    }


# The twelve measured branch-cardinality continuation sources are immutable.
