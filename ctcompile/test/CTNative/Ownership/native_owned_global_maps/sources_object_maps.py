"""sources object maps: continued from sources_leaf_objects."""

from .sources_leaf_objects import *


def primitive_absence_sources():
    # Keep the historical refusal's ten calls and empty/undefined key
    # observations. Definite absence supplies its missing host result fact;
    # the existing nullable String carrier independently admits native output.
    source, value = payload_result_refusals()["result_seeded_empty_deleted"]
    return {"result_seeded_empty_deleted": (source, "host", value)}


RESULT_SIGNATURES["result_seeded_empty_deleted"] = (
    "ctnative::nullable_string",
    "ctnative::nullable_string",
    6,
)


# Exact Map.clear continuation measured in b1e8ba6b; includes final newlines.
LEAF_CLEAR_HISTORY = {
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
    "local_clear_return_undefined": (
        6,
        6,
        "f588042993857d57196f537ee31132ac692a2921059c233094ecaef5d75877de",
    ),
    "local_clear_unseeded_size": (
        5,
        5,
        "7861a90e905b27e26e1c3f06dea4441bcd8d9353727d3377b1948dbb37ef9ec7",
    ),
    "local_clear_object_size": (
        6,
        6,
        "bd2ec865d2d1f8f8bea877bcb9db912d837cf76ad2fa536c4b42f59f9592e2e4",
    ),
    "local_clear_saved_field": (
        7,
        7,
        "6488e9a1462acb75211e5de3632a95a8e3be8e36928ea1f957dd3f981e451434",
    ),
    "local_clear_repeated": (
        8,
        8,
        "1f442f2a5462785528c5b6b18bb38e2afbd5ecb29edd67a6351f28bcac0fc9f9",
    ),
    "local_clear_unseen_key": (
        6,
        6,
        "76c2d862b23af519069d7a7fe4cee1174903cde73244c92e2d0fe239b12a84c2",
    ),
    "local_clear_other_key": (
        7,
        7,
        "e54168ebedbf327c5d149f92115667444a70c70fcbdaea9c99606e8e72d94e23",
    ),
    "local_clear_reseed_present": (
        8,
        8,
        "e768e989d5b9952bdf5d54efd0a4f2406629dbe5f7e0621ce8ae0716bcf70c31",
    ),
    "local_clear_reseed_disjoint": (
        8,
        8,
        "95a6412d07a604d8d736c87ed4dbf53b3a6ae74d855046edb81e5adc868b2984",
    ),
    "local_clear_saved_undefined": (
        8,
        8,
        "b537f32205f9e2e79eb8d1f656e20a5f5ca52ec8df3b4639dcc53e81a260ad95",
    ),
    "local_clear_fluent_alias": (
        7,
        7,
        "dde8e34a2b2dd7e67d9d6eb72dcd8ad0007e90545188da3f64fa4812d623b861",
    ),
    "local_clear_loaded_alias": (
        7,
        7,
        "5cdd2ec70f49022dd16ab386ffd804ed63c80fda6cb3b52caf1b1bf6321f7742",
    ),
    "local_clear_maybe_reseed_same": (
        8,
        8,
        "dedac8028b1687ba267930ddfea8cbf762f4e90937dccc55f8dc1982e24b43ea",
    ),
    "local_clear_maybe_reseed_distinct": (
        8,
        8,
        "dba320a20a9bae178bc448a5588cdba4910050b0045f6e6ddad97c3ce13564c5",
    ),
    "local_clear_both_branches_false": (
        9,
        9,
        "f17db714155f41d9696f0b831d7c4c4847d644872a76988e7c64171c20f9585f",
    ),
    "local_clear_one_branch_false": (
        8,
        8,
        "cd32c46cd32280b44dccf549e09f51634cfddd5add24e3c04e2c4d1a9ae67288",
    ),
    "local_clear_saved_across_branch_false": (
        8,
        8,
        "cc901dc08dc91c467d2042be690d004ab1dbfb71118fc35747ec7509317f1810",
    ),
    "local_clear_both_branches_true": (
        9,
        9,
        "5c6b52b1a45125577dcd8095a110e582dfbd19b91075de96eaf1a189713ca592",
    ),
    "local_clear_one_branch_true": (
        8,
        8,
        "e23b7b4b19a9f75ab8f570377ef8caef81919f4972563ce360e166e69d18cea4",
    ),
    "local_clear_saved_across_branch_true": (
        8,
        8,
        "d4d89b0601d41e48d36cc85a903e7a77525e7890a0ecf3fa0d60d0d7b2b99e7d",
    ),
}


LEAF_CLEAR_PROMOTED = {"local_absence_clear_undefined", "local_absence_clear_saved_identity"}
LEAF_CLEAR_UNOWNED = {
    "local_clear_maybe_reseed_same",
    "local_clear_maybe_reseed_distinct",
    "local_clear_one_branch_false",
    "local_clear_one_branch_true",
    "local_clear_evaluated_argument",
}


def leaf_clear_cases():
    absence = leaf_absence_cases()
    out = {
        name: dict(
            source=absence[name]["source"],
            expected_trace=absence[name]["expected_trace"],
            expected_raw_calls=absence[name]["raw_calls"],
            boundary="original captured clear continuation",
        )
        for name in LEAF_CLEAR_PROMOTED
    }
    base = absence["local_absence_clear_saved_identity"]["source"]
    body = (
        "const item = {value: 1}; state.set(key, item); const saved = state.get(key); "
        "state.clear(); return saved === item ? 1 : 0;"
    )
    assert base.count(body) == 1
    prefix = "const item = {value: 1}; state.set(key, item); "
    absent = "return state.get(key) === void 0 ? 1 : 0;"
    present = "return state.get(key) === item ? 1 : 0;"

    def add(name, changed, value, boundary, *, params="key", actuals="'x'", repair=None):
        source = base.replace(body, changed).replace("set(key) {", "set(" + params + ") {")
        source = source.replace("host.slot.set('x')", "host.slot.set(" + actuals + ")")
        raw = 4 + len(re.findall(r"\b(?:state|alias)\.(?:set|get|has|delete|clear)\(", changed))
        row = dict(
            source=source,
            expected_trace=value,
            expected_raw_calls=raw,
            historical=False,
            boundary=boundary,
        )
        if repair:
            old, replacement, repair_name = repair
            assert source.count(old) == 1
            assert source.replace(old, replacement) == out[repair_name]["source"]
            row.update(repair=repair_name, removed_text=old, replacement_text=replacement)
        out["local_clear_" + name] = row

    add(
        "return_undefined",
        prefix + "return state.clear() === void 0 ? 1 : 0;",
        1,
        "clear itself returns Undefined and executes its Map mutation",
    )
    add(
        "unseeded_size",
        "state.clear(); return state.size;",
        0,
        "clear must work for unknown incoming contents, without a seed",
    )
    add(
        "object_size",
        prefix + "state.clear(); return state.size;",
        0,
        "ordinary clear mutation isolated from object readback",
    )
    add(
        "saved_field",
        prefix + "const saved = state.get(key); state.clear(); return saved.value;",
        1,
        "saved owning leaf and initialized field survive full Map clearing",
    )
    add(
        "repeated",
        prefix + "state.clear(); state.clear(); " + absent,
        1,
        "repeated clearing preserves absence",
    )
    add(
        "unseen_key",
        "state.clear(); return state.get(key) === void 0 ? 1 : 0;",
        1,
        "whole Map clear establishes absence for any later queried key",
    )
    add(
        "other_key",
        prefix + "state.clear(); return state.get('other') === void 0 ? 1 : 0;",
        1,
        "clear removes all entries, not just keys enumerated by prior local writes",
    )
    add(
        "reseed_present",
        prefix + "state.clear(); state.set(key, item); " + present,
        1,
        "same-key reseed invalidates old absence and restores the exact local leaf",
    )
    add(
        "reseed_disjoint",
        prefix + "state.clear(); state.set('other', item); "
        "return state.get('gone') === void 0 ? 1 : 0;",
        1,
        "known distinct write preserves whole-Map absence for an unseen literal key",
    )
    add(
        "saved_undefined",
        prefix + "state.clear(); const saved = state.get(key); "
        "state.set(key, item); return saved === void 0 ? 1 : 0;",
        1,
        "saved absent result stays Undefined after later reseed",
    )
    add(
        "fluent_alias",
        "const item = {value: 1}; const alias = state.set(key, item); " "alias.clear(); " + absent,
        1,
        "clear on fluent set return mutates the same captured Map",
    )
    add(
        "loaded_alias",
        "const item = {value: 1}; state.set(key, item); const alias = state; "
        "alias.clear(); " + absent,
        1,
        "immutable captured alias denotes the same Map",
    )
    for alias, other, expected in (("same", "'x'", 0), ("distinct", "'other'", 1)):
        add(
            "maybe_reseed_" + alias,
            prefix + "state.clear(); state.set(other, {value: 2}); " + absent,
            expected,
            "possible formal-key equality invalidates a definitely absent result",
            params="key, other",
            actuals="'x', " + other,
        )
    for flag in (False, True):
        word = str(flag).lower()
        add(
            "both_branches_" + word,
            prefix
            + "if (flag) { state.clear(); } else { state.clear(); state.has(key); } "
            + absent,
            1,
            "nonidentical surviving arms both clear all entries",
            params="key, flag",
            actuals="'x', " + word,
        )
        add(
            "one_branch_" + word,
            prefix + "if (flag) { state.clear(); } else { state.has(key); } " + absent,
            int(flag),
            "one clearing arm does not prove post-join absence for future Boolean calls",
            params="key, flag",
            actuals="'x', " + word,
        )
        add(
            "saved_across_branch_" + word,
            prefix + "const saved = state.get(key); "
            "if (flag) { state.clear(); } else { state.has(key); } return saved === item ? 1 : 0;",
            1,
            "saved object remains exact despite conditional clearing of its former entry",
            params="key, flag",
            actuals="'x', " + word,
        )
    for alias in ("same", "distinct"):
        source = out["local_clear_maybe_reseed_" + alias]["source"]
        old = "state.set(other, {value: 2}); "
        assert source.count(old) == 1
        out["local_clear_maybe_reseed_" + alias + "_repair"] = dict(
            source=source.replace(old, old + "state.delete(key); "),
            expected_trace=1,
            expected_raw_calls=9,
            boundary="exact delete restores absence after a possible-alias write",
        )
    add(
        "evaluated_argument",
        prefix + "state.clear(state.has(key)); " + absent,
        1,
        "standard clear arity remains exact; its ignored argument is still evaluated",
    )
    add(
        "evaluated_argument_repair",
        prefix + "state.has(key); state.clear(); " + absent,
        1,
        "separating the evaluated ignored argument restores exact zero-argument clear",
    )
    for name, row in out.items():
        if name in LEAF_CLEAR_HISTORY:
            raw, prepared, digest = LEAF_CLEAR_HISTORY[name]
            assert hashlib.sha256(row["source"].encode()).hexdigest() == digest, name
            assert row["expected_raw_calls"] == raw, name
        else:
            raw = prepared = row["expected_raw_calls"]
        row.update(raw_calls=raw, prepared_calls=prepared)
    return out


def leaf_clear_sources():
    return {
        name: (row["source"], "host", row["expected_trace"])
        for name, row in leaf_clear_cases().items()
        if name not in LEAF_CLEAR_UNOWNED
    }


def leaf_clear_refusals():
    cases = leaf_clear_cases()
    edits = {
        "local_clear_evaluated_argument": (
            "state.clear(state.has(key));",
            "state.has(key); state.clear();",
            "local_clear_evaluated_argument_repair",
        ),
    }
    for alias in ("same", "distinct"):
        edits["local_clear_maybe_reseed_" + alias] = (
            "state.set(other, {value: 2});",
            "state.set(other, {value: 2}); state.delete(key);",
            "local_clear_maybe_reseed_" + alias + "_repair",
        )
    for flag in ("false", "true"):
        edits["local_clear_one_branch_" + flag] = (
            "else { state.has(key); }",
            "else { state.clear(); state.has(key); }",
            "local_clear_both_branches_" + flag,
        )
    assert edits.keys() == LEAF_CLEAR_UNOWNED
    result = {}
    for name, (old, replacement, repair) in edits.items():
        row = cases[name]
        assert row["source"].count(old) == 1, name
        assert row["source"].replace(old, replacement) == cases[repair]["source"], name
        result[name] = (
            row["source"],
            row["expected_trace"],
            old,
            replacement,
            repair,
            row["prepared_calls"],
        )
    return result


# Preserve all twelve exact post-clear continuation sources and both call censuses.
