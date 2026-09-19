"""sources map key mutations: continued from sources_map_key_sizes."""

from .sources_map_key_sizes import *

JOIN_SIZE_HISTORY = {
    "joined_delete_disjoint_false": (
        15,
        "2d1763ebbce40e07c852e21e89b45106e767f8a83432af6be650b59f7e3c20fc",
    ),
    "joined_delete_disjoint_false_literal_repair": (
        15,
        "b0d72c4ef0d1b5017a8d041903d1bf14dde9d93cdf1e998ec5a31f28eb08b249",
    ),
    "joined_set_disjoint_false": (
        13,
        "1ae41caf1b62a270c2ce75e18acdfaaa8ea2c539c9ddf127cef02607f11a62bd",
    ),
    "joined_set_disjoint_false_literal_repair": (
        13,
        "f09bc7aa7f61f0af53bcb7ab8b96a1a94a0cbd899d3ff642ad3d2e7621208490",
    ),
    "joined_unequal_false": (
        15,
        "88585cc58f7f089c488aa6830eeb0cee526ac3ab1405077196729fe0ec0a75ec",
    ),
    "joined_saved_in_arms_false": (
        15,
        "a0dc17164da6f46dd8777af5c4e457d7236f0559f76b248759c04d972244e721",
    ),
    "joined_delete_disjoint_true": (
        15,
        "6b49533862c057079caccb946c3d5f13f240d004e6c702844bf61e3ccd8d12ee",
    ),
    "joined_delete_disjoint_true_literal_repair": (
        15,
        "de28b4538d2411f4d94fb904e7db763a4acf531e8d57283828171904e4a77d92",
    ),
    "joined_set_disjoint_true": (
        13,
        "082976f04967785688271e1d1d527297f56a3b9e54b4baf74fd7c700b4e0b701",
    ),
    "joined_set_disjoint_true_literal_repair": (
        13,
        "efa12dbf6ee1d9d884c8ca176ebab5e126247a0a15e06773318bb295dcee2724",
    ),
    "joined_unequal_true": (15, "2e5576ed9ca3ddf67201ce65a31358e99b4cefb8af4a791961095e2426a1ab41"),
    "joined_saved_in_arms_true": (
        15,
        "95836a416bdf0cd1b40f6dd77f286bdc4be6856a04b703f8d1597590136e0876",
    ),
}


def join_size_cases():
    rows = {}

    def add(name, source, value, calls, admitted=True, repair=None):
        digest = hashlib.sha256(source.encode()).hexdigest()
        if name in JOIN_SIZE_HISTORY:
            assert (calls, digest) == JOIN_SIZE_HISTORY[name], name
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

    def source(body, flag):
        return """var host = {};
(function(factory) { host.slot = factory(); })(function() {
    const state = new Map();
    return {
        size() { return state.size; },
        set(key, flag) { BODY }
    };
});
host.slot.size(); var trace = host.slot.set(7, FLAG);
""".replace("BODY", body).replace("FLAG", flag)

    prefix = "const item = {value: 1}; state.set(key, item); state.clear(); "
    start = prefix + "state.set(1, item); state.set(2, item); "
    branches = "if (flag) { state.delete(1); } " "else { state.delete(2); state.has(key); } "
    snapshot = "const saved = state.size; "
    field = (
        "state.clear(); state.set(1, item); state.set(3, item); "
        "return state.get(saved).value === 1 ? 1 : 0;"
    )
    control = (
        "state.clear(); state.set(1, item); state.set(3, item); "
        "return state.get(saved) === void 0 ? 2 : 1;"
    )
    for flag in ("false", "true"):
        body = start + branches + snapshot + field
        add("joined_delete_disjoint_" + flag, source(body, flag), 1, 15)
        add(
            "joined_delete_disjoint_" + flag + "_literal_repair",
            source(body.replace(snapshot, "state.size; const saved = 1; "), flag),
            1,
            15,
        )
        setters = (
            "if (flag) { state.set(7, item); } " "else { state.set(9, item); state.has(key); } "
        )
        body = prefix + setters + snapshot + field
        add("joined_set_disjoint_" + flag, source(body, flag), 1, 13)
        add(
            "joined_set_disjoint_" + flag + "_literal_repair",
            source(body.replace(snapshot, "state.size; const saved = 1; "), flag),
            1,
            13,
        )
        name = "joined_unequal_" + flag
        equal = source(start + branches + snapshot + control, flag)
        add(name + "_repair", equal, 1, 15)
        unequal = equal.replace("if (flag) { state.delete(1); }", "if (flag) { state.has(1); }")
        add(
            name,
            unequal,
            2 if flag == "true" else 1,
            15,
            False,
            ("if (flag) { state.has(1); }", "if (flag) { state.delete(1); }", name + "_repair"),
        )
        saved_arms = (
            "let saved; if (flag) { state.delete(1); saved = state.size; } "
            "else { state.delete(2); saved = state.size; state.has(key); } "
        )
        add("joined_saved_in_arms_" + flag, source(start + saved_arms + field, flag), 1, 15)

        # Equal sizes do not prove either surviving key is present on both arms.
        name = "joined_missing_common_key_" + flag
        uncertain = source(
            start + branches + snapshot + "return state.get(saved) === void 0 ? 2 : 1;", flag
        )
        repaired = uncertain.replace(snapshot, snapshot + "state.set(1, item); ")
        add(name + "_repair", repaired, 1, 13)
        add(
            name,
            uncertain,
            2 if flag == "true" else 1,
            12,
            False,
            (snapshot, snapshot + "state.set(1, item); ", name + "_repair"),
        )

        # A write after the join inserts on one arm and overwrites on the other.
        # It must invalidate the mutable exact-size fact before the next read.
        name = "joined_mutated_size_" + flag
        changed = source(start + branches + "state.set(1, item); " + snapshot + control, flag)
        repaired = changed.replace(
            "state.set(1, item); " + snapshot, "state.clear(); state.set(1, item); " + snapshot
        )
        add(name + "_repair", repaired, 1, 17)
        add(
            name,
            changed,
            2 if flag == "true" else 1,
            16,
            False,
            (
                "state.set(1, item); " + snapshot,
                "state.clear(); state.set(1, item); " + snapshot,
                name + "_repair",
            ),
        )

    base = rows["joined_delete_disjoint_false"]["source"]
    add(
        "joined_captured_alias",
        base.replace("const saved = state.size;", "const alias = state; const saved = alias.size;"),
        1,
        15,
    )
    # The saved one remains immutable while current size grows to two, the
    # selected owning leaf is deleted, and the Map is finally reseeded.
    lifetime = base.replace(
        field,
        "state.clear(); state.set(1, item); state.set(3, item); "
        "const leaf = state.get(saved); state.delete(1); state.clear(); state.set(2, item); "
        "return leaf === item ? (flag ? 2 : 1) : 0;",
    )
    add("joined_size_saved_lifetime", lifetime, 1, 18)
    for name, (calls, digest) in JOIN_SIZE_HISTORY.items():
        assert (rows[name]["raw_calls"], rows[name]["sha256"]) == (calls, digest), name
    return rows


def join_size_sources():
    return {
        name: (row["source"], "host", row["expected_trace"])
        for name, row in join_size_cases().items()
        if row["admitted"]
    }


# The twelve measured after-join mutation sources retain their exact bytes.
MUTATION_SIZE_HISTORY = {
    "joined_disjoint_set_false": (
        16,
        "56679e2d1edd49ce7e69a84633446fe408c31320c2613ed495484080a435ffa2",
    ),
    "joined_disjoint_set_false_literal_repair": (
        16,
        "a11c4179e2b11578f284a41461ea87c867c9a96b5bfdd8d4b8d1154bb52bba9d",
    ),
    "joined_absent_delete_false": (
        16,
        "887e65fcb8f4fbba94f28d0e9a4964410a6e2760eabb6ecfe80c9588976a0c7b",
    ),
    "joined_absent_delete_false_literal_repair": (
        16,
        "8e3fa6f1ff6c10e623a47e2434fc0fab68a7ef6af8c19f13de21d30c622c9766",
    ),
    "joined_saved_before_set_false": (
        16,
        "c28a63747431a341068e2d81c2199cf34b1b1493d2f633201a5548c6cd7d7c45",
    ),
    "joined_clear_recovers_two_false": (
        18,
        "25328a26cc6940e5f392ae84a51e9de9852e16f7eb840b7cccd1ddf85bd278cb",
    ),
    "joined_disjoint_set_true": (
        16,
        "f5a3976cc0bc5b6d2174aeb04e51af71a832e02ef55b7f919578f59aee8560d5",
    ),
    "joined_disjoint_set_true_literal_repair": (
        16,
        "d427d189362470f047c8584cf8496c1e0cb41f04f7ea61b2a384577a309536d9",
    ),
    "joined_absent_delete_true": (
        16,
        "aa8a314e12f297afe52d1e704334b2aaa63e7b5e565f5d93ea92d4c4eda3198c",
    ),
    "joined_absent_delete_true_literal_repair": (
        16,
        "e19738c73cd27c7249a28630143241ddc27beecf1c884a1ccd50e992a2c52b9c",
    ),
    "joined_saved_before_set_true": (
        16,
        "ab2ff3d0b512ed737e5ec19dba85e6172a31ddf5382cf8df702805708c08fcd2",
    ),
    "joined_clear_recovers_two_true": (
        18,
        "7559345a4caf261b6fef00703ff01e938ac410414b29812730885b224b8960f7",
    ),
}


def mutation_size_cases():
    rows = {}

    def add(name, source, value, calls, admitted=True, repair=None):
        digest = hashlib.sha256(source.encode()).hexdigest()
        if name in MUTATION_SIZE_HISTORY:
            assert (calls, digest) == MUTATION_SIZE_HISTORY[name], name
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

    snapshot = "const saved = state.size; "
    branches = "if (flag) { state.delete(1); } " "else { state.delete(2); state.has(key); } "
    field = (
        "state.clear(); state.set(1, item); state.set(3, item); "
        "return state.get(saved).value === 1 ? 1 : 0;"
    )
    field_two = field.replace(
        "state.set(1, item); state.set(3, item);", "state.set(2, item); state.set(4, item);"
    )
    checked = "return state.get(saved) === void 0 ? 2 : 1;"
    for flag in ("false", "true"):
        base = join_size_cases()["joined_delete_disjoint_" + flag]["source"]
        inserted = base.replace(snapshot, "state.set(3, item); " + snapshot).replace(
            field, field_two
        )
        deleted = base.replace(snapshot, "state.delete(3); " + snapshot)
        for stem, source, cardinality in (
            ("joined_disjoint_set_", inserted, 2),
            ("joined_absent_delete_", deleted, 1),
        ):
            add(stem + flag, source, 1, 16)
            add(
                stem + flag + "_literal_repair",
                source.replace(snapshot, f"state.size; const saved = {cardinality}; "),
                1,
                16,
            )
        add(
            "joined_saved_before_set_" + flag,
            base.replace(snapshot, snapshot + "state.set(3, item); "),
            1,
            16,
        )
        add(
            "joined_clear_recovers_two_" + flag,
            base.replace(
                snapshot, "state.clear(); state.set(1, item); state.set(3, item); " + snapshot
            ).replace(field, field_two),
            1,
            18,
        )

        # A third key present in both arms is independent membership evidence.
        common = base.replace(branches, "state.set(3, item); " + branches)
        add(
            "joined_present_overwrite_" + flag,
            common.replace(snapshot, "state.set(3, item); " + snapshot).replace(field, field_two),
            1,
            17,
        )
        add(
            "joined_present_delete_" + flag,
            common.replace(snapshot, "state.delete(3); " + snapshot),
            1,
            17,
        )

        # Keep the reads executable even when a future key changes the size.
        # Complete cardinality alone cannot say which branch key survived.
        absent_repair = "joined_possible_delete_" + flag + "_repair"
        absent_checked = deleted.replace("return state.get(saved).value === 1 ? 1 : 0;", checked)
        add(absent_repair, absent_checked, 1, 16)
        add(
            "joined_possible_delete_" + flag,
            absent_checked.replace("state.delete(3); " + snapshot, "state.delete(1); " + snapshot),
            1 if flag == "true" else 2,
            16,
            False,
            ("state.delete(1); " + snapshot, "state.delete(3); " + snapshot, absent_repair),
        )
        insertion_repair = "joined_formal_insert_" + flag + "_repair"
        inserted_checked = inserted.replace("return state.get(saved).value === 1 ? 1 : 0;", checked)
        add(insertion_repair, inserted_checked, 1, 16)
        add(
            "joined_formal_insert_" + flag,
            inserted_checked.replace(
                "state.set(3, item); " + snapshot, "state.set(key, item); " + snapshot
            ),
            1,
            16,
            False,
            (
                "state.set(key, item); " + snapshot,
                "state.set(3, item); " + snapshot,
                insertion_repair,
            ),
        )
        add(
            "joined_formal_delete_" + flag,
            absent_checked.replace(
                "state.delete(3); " + snapshot, "state.delete(key); " + snapshot
            ),
            1,
            16,
            False,
            ("state.delete(key); " + snapshot, "state.delete(3); " + snapshot, absent_repair),
        )

    base = rows["joined_disjoint_set_false"]["source"]
    add(
        "joined_mutation_alias",
        base.replace(
            "state.set(3, item); " + snapshot,
            "const alias = state; alias.set(3, item); const saved = alias.size; ",
        ),
        1,
        16,
    )
    add(
        "joined_known_mutation_chain",
        base.replace(snapshot, "state.delete(3); state.delete(3); state.set(5, item); " + snapshot),
        1,
        19,
    )
    # Saved two selects a leaf while the current size is one; later deletion
    # and clear must not change that owning saved value or its scalar snapshot.
    add(
        "joined_mutation_saved_lifetime",
        base.replace(
            field_two,
            "state.clear(); state.set(2, item); const leaf = state.get(saved); "
            "state.delete(2); state.clear(); state.set(4, item); "
            "return leaf === item ? (flag ? 2 : 1) : 0;",
        ),
        1,
        18,
    )
    for name, (calls, digest) in MUTATION_SIZE_HISTORY.items():
        assert (rows[name]["raw_calls"], rows[name]["sha256"]) == (calls, digest), name
    return rows


def mutation_size_sources():
    return {
        name: (row["source"], "host", row["expected_trace"])
        for name, row in mutation_size_cases().items()
        if row["admitted"]
    }
