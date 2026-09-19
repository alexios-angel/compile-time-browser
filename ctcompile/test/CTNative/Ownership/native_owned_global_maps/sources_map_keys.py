"""sources map keys: continued from sources_map_key_mutations."""

from .sources_map_key_mutations import *


def object_argument_cases():
    # Keep the exact 20d4806e continuation, including the entry allocation and
    # Bootstrap's t.has(e). A discarded allocation is a separate owner boundary.
    base = """var host = {};
(function(factory) { host.slot = factory(); })(function() {
    const t = new Map;
    return { get(e) { return t.has(e) ? 1 : 0; } };
});
var trace = host.slot.get({});
"""
    rows = {}

    def add(name, source, calls=4, value=0, admitted=False, functions=4):
        rows["object_argument_" + name] = dict(
            source=source,
            expected_trace=value,
            raw_calls=calls,
            prepared_calls=calls,
            functions=functions,
            admitted=admitted,
            owner=admitted or name == "seeded",
            sha256=hashlib.sha256(source.encode()).hexdigest(),
        )

    add("exact", base, admitted=True)
    add("seeded", base.replace("return t.has(e)", "t.set(1, 1); return t.has(e)"), 5, admitted=True)
    add(
        "alias",
        base.replace("return t.has(e)", "const alias = e; return t.has(alias)"),
        admitted=True,
    )
    add(
        "repeated",
        base.replace(
            "var trace = host.slot.get({});", "host.slot.get({}); var trace = host.slot.get({});"
        ),
        5,
        admitted=True,
    )
    add(
        "two_formals",
        base.replace("get(e)", "get(e, other)")
        .replace("return t.has(e) ? 1 : 0;", "return t.has(e) ? 1 : t.has(other) ? 2 : 0;")
        .replace("host.slot.get({})", "host.slot.get({}, {})"),
        5,
        admitted=True,
    )
    add("evaluated_number", base.replace("host.slot.get({})", "host.slot.get(({}, 1))"))
    for name, actual in (
        ("root", "host"),
        ("table", "host.slot"),
        ("field", "{value: 1}"),
        ("array", "[]"),
    ):
        add(
            name,
            base.replace("host.slot.get({})", "host.slot.get(" + actual + ")"),
            admitted=name == "field",
        )
    add(
        "global",
        base.replace(
            "var trace = host.slot.get({});", "var key = {}; var trace = host.slot.get(key);"
        ),
        admitted=True,
    )
    named = rows["object_argument_global"]["source"]
    add(
        "global_repeated",
        named.replace("var trace =", "host.slot.get(key); var trace ="),
        5,
        admitted=True,
    )
    add(
        "global_retained",
        rows["object_argument_global_repeated"]["source"].replace(
            "return t.has(e) ? 1 : 0;", "t.set(e, 1); return t.has(e) ? t.size : 0;"
        ),
        6,
        1,
        True,
    )
    for name in ("global", "global_repeated", "global_retained"):
        rows["object_argument_" + name]["global_key"] = True
    add(
        "global_early",
        named.replace(
            "var key = {}; var trace = host.slot.get(key);",
            "var trace = host.slot.get(key); var key = {};",
        ),
    )
    add("global_second_store", named.replace("var trace =", "key = {}; var trace ="))
    add("global_late_store", named + "key = {};\n")
    add(
        "global_alias",
        named.replace(
            "var trace = host.slot.get(key);", "var alias = key; var trace = host.slot.get(alias);"
        ),
        admitted=True,
    )
    aliased = rows["object_argument_global_alias"]["source"]
    add(
        "global_alias_early_root",
        aliased.replace("var key = {}; var alias = key;", "var alias = key; var key = {};"),
    )
    add(
        "global_alias_early_read",
        aliased.replace(
            "var alias = key; var trace = host.slot.get(alias);",
            "var trace = host.slot.get(alias); var alias = key;",
        ),
    )
    add("global_alias_late_root", aliased + "key = {};\n")
    add("global_alias_late_alias", aliased + "alias = {};\n")
    add(
        "global_alias_chain",
        aliased.replace(
            "var trace = host.slot.get(alias);",
            "var copy = alias; var trace = host.slot.get(copy);",
        ),
        admitted=True,
    )
    chain = rows["object_argument_global_alias_chain"]["source"]
    rows["object_argument_global_alias_chain"].update(global_alias=True, global_chain=("copy",))
    add("global_chain_late_intermediate", chain + "alias = {};\n")
    add(
        "global_chain_early_descendant",
        chain.replace("var alias = key; var copy = alias;", "var copy = alias; var alias = key;"),
    )
    rows["object_argument_global_chain_early_descendant"]["undefined_globals"] = ("copy",)
    add(
        "global_chain_foreign_intermediate",
        "function consume(value) { return 0; }\n"
        + chain.replace("var trace =", "consume(alias); var trace ="),
        5,
        functions=5,
    )
    add("global_chain_unused_descendant", chain + "var spare = copy;\n")
    add("global_alias_cycle", aliased.replace("var key = {};", "var key = alias;"))
    rows["object_argument_global_alias_early_root"]["undefined_globals"] = ("alias",)
    rows["object_argument_global_alias_cycle"]["undefined_globals"] = ("alias", "key")
    add(
        "global_alias_nonentry",
        "function initialize() { alias = key; }\n"
        + aliased.replace("var alias = key;", "var alias; initialize();"),
        5,
        functions=5,
    )
    add(
        "global_alias_foreign_consumer",
        "function consume(value) { return 0; }\n"
        + aliased.replace("var trace =", "consume(alias); var trace ="),
        5,
        functions=5,
    )
    add("global_alias_unused", aliased.replace("host.slot.get(alias)", "host.slot.get(key)"))
    add(
        "global_field_write",
        named.replace("var trace =", "key.value = 1; var trace ="),
        admitted=True,
    )
    add(
        "global_unknown_consumer",
        "function consume(value) { return 0; }\n"
        + named.replace("return t.has(e)", "consume(e); return t.has(e)"),
        5,
        functions=5,
    )
    add(
        "global_object_payload",
        named.replace("return t.has(e)", "t.set(e, e); return t.has(e)"),
        5,
        1,
        True,
    )
    rows["object_argument_global_object_payload"].update(global_key=True, object_payload=True)
    add(
        "global_object_return",
        named.replace("return t.has(e) ? 1 : 0;", "return e;").replace(
            "var trace = host.slot.get(key);", "var trace = host.slot.get(key) === key ? 1 : 0;"
        ),
        3,
        1,
    )
    add(
        "global_later_number",
        named.replace(
            "var trace = host.slot.get(key);", "host.slot.get(key); var trace = host.slot.get(1);"
        ),
        5,
    )
    add("global_later_object", named.replace("var trace =", "host.slot.get(1); var trace ="), 5)
    add("later_object", base.replace("var trace =", "host.slot.get(1); var trace ="), 5)
    add(
        "later_number",
        base.replace(
            "var trace = host.slot.get({});", "host.slot.get({}); var trace = host.slot.get(1);"
        ),
        5,
    )
    add("field_write", base.replace("return t.has(e)", "e.value = 1; return t.has(e)"))
    add("key_write", base.replace("return t.has(e)", "t.set(e, 1); return t.has(e)"), 5, 1, True)
    add(
        "object_payload",
        base.replace("return t.has(e)", "t.set(e, e); return t.has(e)"),
        5,
        1,
        True,
    )
    rows["object_argument_object_payload"]["object_payload"] = True
    # A scalar key cannot retain the caller's object if its payload owner is lost.
    payload = named.replace(
        "return { get(e) { return t.has(e) ? 1 : 0; } };",
        """return {
        get(e) { t.set(1, e); return t.has(1) ? 1 : 0; },
        erase() { return t.delete(1) ? 1 : 0; },
        clear() { t.clear(); return 0; }
    };""",
    ).replace(
        "var trace = host.slot.get(key);",
        "host.slot.get(key); host.slot.erase(); host.slot.clear(); var trace = host.slot.get(key);",
    )
    add("scalar_key_payload", payload, 10, 1, True, functions=6)
    rows["object_argument_scalar_key_payload"].update(
        global_key=True, object_payload=True, payload_only=True
    )
    retained = rows["object_argument_global_object_payload"]["source"]
    add("payload_field", retained.replace("var key = {};", "var key = {value: 1};"), 5, 1, True)
    rows["object_argument_payload_field"].update(global_key=True, object_payload=True)
    add("payload_field_write", retained.replace("t.set(e, e);", "e.value = 1; t.set(e, e);"), 5, 1)
    add("payload_cycle", retained.replace("t.set(e, e);", "e.self = e; t.set(e, e);"), 5, 1)
    add(
        "payload_return",
        retained.replace("return t.has(e) ? 1 : 0;", "t.has(e); return e;").replace(
            "var trace = host.slot.get(key);", "var trace = host.slot.get(key) === key ? 1 : 0;"
        ),
        5,
        1,
    )
    add(
        "payload_unknown_consumer",
        "function consume(value) { return 0; }\n"
        + retained.replace("t.set(e, e);", "consume(e); t.set(e, e);"),
        6,
        1,
        functions=5,
    )
    add(
        "payload_later_number",
        retained.replace(
            "var trace = host.slot.get(key);", "host.slot.get(key); var trace = host.slot.get(1);"
        ),
        6,
        1,
    )
    add(
        "payload_later_object",
        retained.replace("var trace =", "host.slot.get(1); var trace ="),
        6,
        1,
    )
    for name in (
        "global_later_number",
        "global_later_object",
        "later_object",
        "later_number",
        "payload_later_number",
        "payload_later_object",
    ):
        rows["object_argument_" + name]["owner"] = True
    siblings = base.replace(
        "return { get(e) { return t.has(e) ? 1 : 0; } };",
        """return {
        get(e) { return t.has(e) ? t.get(e) : 0; },
        set(e, value) { t.set(e, value); return t.get(e); },
        erase(e) { return t.delete(e) ? 1 : 0; },
        clear() { t.clear(); return 0; }
    };""",
    ).replace(
        "var trace = host.slot.get({});",
        """{
    const key = {}, alias = key, other = {};
    host.slot.set(key, 7);
    host.slot.set(alias, 9);
    host.slot.get(other);
    host.slot.erase(other);
    host.slot.clear();
    host.slot.set(key, 11);
    var trace = host.slot.get(alias);
}""",
    )
    # Keep both the original block-const source and its true-global companion.
    add("siblings_named", siblings, 15, 11, True, functions=7)
    add(
        "siblings_global",
        siblings.replace("    const key =", "    var key ="),
        15,
        11,
        True,
        functions=7,
    )
    for name in ("global_alias", "siblings_global"):
        rows["object_argument_" + name]["global_alias"] = True
    branched = (
        rows["object_argument_siblings_global"]["source"]
        .replace(
            "alias = key, other = {};",
            "alias = key, copy = alias, tail = copy, branch = alias, other = {};",
        )
        .replace("host.slot.set(key,", "host.slot.set(tail,")
        .replace("host.slot.set(alias,", "host.slot.set(branch,")
        .replace("host.slot.get(alias)", "host.slot.get(branch)")
    )
    add("siblings_global_chain", branched, 15, 11, True, functions=7)
    rows["object_argument_siblings_global_chain"].update(
        global_alias=True, global_chain=("copy", "tail", "branch")
    )
    siblings = siblings.replace("    const key = {}, alias = key, other = {};\n", "")
    for actual in ("key", "alias", "other"):
        siblings = siblings.replace("(" + actual + ",", "({},").replace("(" + actual + ")", "({})")
    add("siblings", siblings, 15, 0, True, functions=7)
    add(
        "sibling_mutation",
        siblings.replace("get(e) { return", "get(e) { e.value = 1; return"),
        15,
        0,
        functions=7,
    )
    # Keep the original five-function setter/getter refusal source and name.
    historical = parameter_refusals()["parameter_object"]
    rows["parameter_object"] = dict(
        source=historical,
        expected_trace=1,
        raw_calls=5,
        prepared_calls=5,
        functions=5,
        admitted=True,
        owner=True,
        sha256=hashlib.sha256(historical.encode()).hexdigest(),
    )
    assert rows["parameter_object"]["sha256"] == (
        "7b592b8354bb285a71e0b9fc72ab6f811c567156b1f6294f06bab9fcd0fa135c"
    )
    assert rows["object_argument_key_write"]["sha256"].startswith("7381e2fb")
    assert rows["object_argument_seeded"]["sha256"].startswith("5eba229d")
    assert rows["object_argument_siblings_named"]["sha256"] == (
        "b6d341ad2c2ad02ca5ca78483291c5c66f0636720c022dfdf33b39ae52eef8d1"
    )
    assert rows["object_argument_siblings_global"]["sha256"] == (
        "600b8fb69ef191ed02c4f4fb9db9a9d204a011aeb516a072025d81c2edd15882"
    )
    assert rows["object_argument_global"]["sha256"] == (
        "7573e89b9f576f9d433b7003b033e0aa8525b2fff97c6991ea5325d669f4ab81"
    )
    assert rows["object_argument_global_alias"]["sha256"] == (
        "abbf4b9c87939c11c4fa1ea57a27dbe9c1d8140bef4b8a39d931956a3be3745c"
    )
    assert rows["object_argument_global_alias_chain"]["sha256"] == (
        "511cca3159bab80ed32ef4957d495fd4f114144c127408907b9fb769663b5e9c"
    )
    assert rows["object_argument_global_object_payload"]["sha256"] == (
        "7f6601d735fd317744668aa37d75290cbe2f23eb1207175052811ade91a00644"
    )
    assert rows["object_argument_object_payload"]["sha256"] == (
        "9e803ae57583cbfdfb53754cef34b0f899eb2d9f2e290c3035431323ba33d8de"
    )
    assert rows["object_argument_exact"]["sha256"] == (
        "20d4806e4f39a2defadcfa9e66380d8d4b680d62ae08a371ce6d870382cbc7a9"
    )
    assert rows["object_argument_evaluated_number"]["sha256"] == (
        "2507446b08d7e897441904b6d09aea9ea97c1512c8913e1976092455e56d1514"
    )
    return rows


def object_argument_sources():
    return {
        name: (row["source"], "host", row["expected_trace"])
        for name, row in object_argument_cases().items()
        if row["admitted"]
    }
