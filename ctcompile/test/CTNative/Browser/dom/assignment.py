#!/usr/bin/env python3
"""Test assigning Bootstrap's data-attribute parser results to a fresh object, natively.

`M` is the unmodified vendored Bootstrap normalizeData function: it turns an
attribute string into a Boolean, Number, null or parsed JSON. Each source
writes M's results into a fresh object. The native code must match Node and
the VM (same property order, same values) and own those values after the DOM
changes; each REFUSALS body must not compile natively.
"""

import json
import re

from CTNative.Browser.dom import dom
from CTNative.Browser.dom import json_parse as values
from CTNative.Browser.dom import number_conversion
from CTNative.Browser.dom.dataset import BOOTSTRAP_FILTER, PREFIX_INTRINSICS
from CTNative.harness import build_and_run, run
from Target.Cpp.harness import FLAGS

# M applied to a data attribute, as Bootstrap's getDataAttribute does.
GET = "M(t.getAttribute('data-bs-config'))"
LATER = "M(t.getAttribute('data-bs-later'))"
BODIES = {
    # Control: reading the member back from a fresh local object forwards M's value.
    "member_read": f"const e = {{}}; e.saved = {GET}; return e.saved;",
    "assign_value": f"const e = {{}}; e.saved = {GET}; return e;",
    "assign_optional": "const e = {}; e.nil = null; e.flag = t.hasAttribute('data-bs-config'); "
    "e.text = t.getAttribute('data-bs-config'); e.number = 42; return e;",
    "assign_order": "const e = {}; e.z = 0; e['10'] = 10; e.a = 1; e['2'] = 2; "
    "e['01'] = 'leading'; e['4294967295'] = 'ordinary'; e['4294967294'] = 'index'; "
    "e['-0'] = 'negative'; e['0'] = 'zero'; e[''] = 'empty'; "
    "e['a\\u0000b'] = 'nul'; e.constructor = 'own'; "
    f"e.saved = {GET}; e.z = false; e.saved = {LATER}; return e;",
    "assign_branch": "const e = {}; e.before = true; if (t.hasAttribute('data-bs-config')) "
    f"{{ e.saved = {GET}; }} else {{ e.saved = {LATER}; }} e.after = null; return e;",
    "assign_loop": "const e = {}; e.tail = false; "
    f"for (const n of {BOOTSTRAP_FILTER}) {{ e.saved = M(t.dataset[n]); }} "
    "e['2'] = 2; return e;",
    "assign_spread": f"const i = {GET}; const e = {{...('object' == typeof i ? i : {{}})}}; "
    f"e.saved = {LATER}; e['2'] = 'last index'; return e;",
    # A spread of a literal whose keys are known is a field copy (lifted by
    # --ctjs-lift-to-scf): the copy keeps the value the source held, and the
    # source's later write does not reach it.
    "copy_then_mutate": f"const e = {{}}; e.saved = {GET}; const copied = {{...e}}; "
    "e.saved = 1; return copied;",
    # Loops that write one dynamic key per dataset entry.
    "loop_dynamic_key": "const e = {}; "
    f"for (const n of {BOOTSTRAP_FILTER}) {{ e[n] = M(t.dataset[n]); }} return e;",
    "loop_dynamic_all": "const e = {}; "
    "for (const n of Object.keys(t.dataset)) { e[n] = M(t.dataset[n]); } return e;",
    # Bootstrap's key normalization (strip `bs`, lower-case the first letter).
    "normalized_result": "const result = {}; "
    f"for (const n of {BOOTSTRAP_FILTER}) {{ let i = n.replace(/^bs/, ''); "
    "i = i.charAt(0).toLowerCase() + i.slice(1); result[i] = M(t.dataset[n]); } return result;",
    "prefix_result": "const result = {}; "
    f"for (const n of {BOOTSTRAP_FILTER}) {{ result[n.replace(/^bs/, '')] = M(t.dataset[n]); }} return result;",
}
SOURCES = {
    name: f"function {name}(t) {{ {values.BOOTSTRAP_M if 'M(' in body else ''} {body} }}\n"
    for name, body in BODIES.items()
}
# Attribute values for the data-bs-config fixtures; None means the attribute is absent.
INPUTS = (
    None,
    "",
    "true",
    "false",
    "null",
    "42",
    "-0",
    "1.0",
    "NaN",
    "Infinity",
    "%7B%22saved%22%3A%5Btrue%2Cnull%5D%7D",
    '["é",{"nested":[1,2]}]',
    '"a\\u0000b"',
    "%",
    "not%20json",
)
# Source name -> list of attribute sets, one native and oracle run each.
FIXTURES = {
    name: [
        ([] if value is None else [("data-bs-config", value)])
        + [("data-bs-later", values.LATER_INPUT)]
        for value in (values.SPREAD_INPUTS if name == "assign_spread" else INPUTS)
    ]
    for name in SOURCES
}
FIXTURES["assign_order"] = FIXTURES["assign_order"][:2]
# Every normalized value is the last overwrite; the zero-iteration controls come first.
FIXTURES["assign_loop"] = [
    [],
    [("data-bs-config", "ignored"), ("data-other", "ignored")],
] + [[("data-bs-z", "first"), ("data-bs-a", value)] for value in INPUTS if value is not None]
DYNAMIC_INPUTS = tuple(value for value in INPUTS if value is not None) + (
    "{}",
    "[]",
    '{"__proto__":{"safe":true},"collision":"inherited","constructor":"inherited",'
    '"toString":"inherited","2":"inherited"}',
    '[{"__proto__":{"safe":true}},null]',
)
FIXTURES["loop_dynamic_key"] = [
    [],
    [("data-bs-config", "ignored"), ("data-other", "ignored")],
] + [[("data-bs-z", "first"), ("data-bs-a", value)] for value in DYNAMIC_INPUTS]
FIXTURES["loop_dynamic_key"].append(
    [
        ("data-bs-z", "42"),
        ("data-bs-config", "ignored"),
        ("data-bs__proto__", DYNAMIC_INPUTS[-2]),
        ("data-bs", "null"),
        ("data-bsé", '["é",null]'),
        ("data-other", "ignored"),
    ]
)
FIXTURES["loop_dynamic_all"] = [[], [("other", "ignored")]] + [
    [
        ("data-before", '"before"'),
        ("data-__proto__", value),
        ("data-collision", "42"),
        ("data-constructor", "false"),
        ("data-to-string", "null"),
        ("data-0", "false"),
        ("data-2", "true"),
        ("data-length", "9"),
        ("data-saved", value),
    ]
    for value in DYNAMIC_INPUTS
]
# The same own properties must survive setting the prototype after those writes.
FIXTURES["loop_dynamic_all"] += [
    attrs[:1] + attrs[2:] + attrs[1:2] for attrs in FIXTURES["loop_dynamic_all"][2:]
]
FIXTURES["loop_dynamic_all"].append(
    [
        ("data-z", "0"),
        ("data-10", "10"),
        ("data-a", "1"),
        ("data-2", "2"),
        ("data-01", '"leading"'),
        ("data-4294967295", '"ordinary"'),
        ("data-4294967294", '"index"'),
        ("data--0", '"negative"'),
        ("data-0", '"zero"'),
        ("data-", '"empty"'),
        ("data-a\0b", '"nul"'),
        ("data-é", '"unicode"'),
        ("data-𐐀", "[true]"),
        ("data-😀", "null"),
    ]
)
FIXTURES["prefix_result"] = [
    [],
    [("data-__proto__", "null"), ("data-bs-config", "ignored")],
] + [
    [
        ("data-bs__proto__", value),
        ("data-bs", value),
        ("data-bscollision", "42"),
        ("data-bsconstructor", "false"),
        ("data-bsto-string", "null"),
        ("data-bs0", "false"),
        ("data-bs2", "true"),
        ("data-bslength", "9"),
        ("data-bssaved", value),
        ("data-__proto__", "null"),
        ("data-bs-config", "ignored"),
    ]
    for value in DYNAMIC_INPUTS
]
# Prototype writes must preserve the same own data in either source position.
FIXTURES["prefix_result"] += [attrs[1:] + attrs[:1] for attrs in FIXTURES["prefix_result"][2:]]
FIXTURES["prefix_result"].append(
    [("data-bs" + key[5:], value) for key, value in FIXTURES["loop_dynamic_all"][-1]]
    + [("data-bsbs__proto__", "42"), ("data-__proto__", "null")]
)


# Colliding output names overwrite in source order; dataset reads keep n.
FIXTURES["normalized_result"] = [
    [],
    [("data-bs", "42"), ("data-bs-foo", "1"), ("data-bsfoo", "2")],
    [("data-bsfoo", "2"), ("data-bs-foo", "1")],
    [("data-bsÉx", "1"), ("data-bséx", "2")],
    [("data-bséx", "2"), ("data-bsÉx", "1")],
    [("data-bsİx", "1"), ("data-bsi\u0307x", "2")],
    [("data-bsi\u0307x", "2"), ("data-bsİx", "1")],
    [("data-bs𐐀x", "42"), ("data-bsΣ", "true"), ("data-bsK", "false")],
] + [
    attrs
    for value in ("null", '{"collision":7,"__proto__":{"safe":true}}', "[true]")
    for attrs in (
        [
            ("data-bs__proto__", value),
            ("data-bs-foo", "1"),
            ("data-bs-foo2", "2"),
            ("data-bsfoo", "3"),
            ("data-bsbs__proto__", "4"),
        ],
        [("data-bsfoo", "3"), ("data-bs-foo", "1"), ("data-bs__proto__", value)],
    )
]
# Bodies the native compiler must refuse: dynamic or __proto__ keys and
# prototype mutation can redirect a write away from a plain own property, and
# an escaping, aliased or captured target can be observed after the DOM changes.
REFUSALS = {
    "dynamic_key": f"const e = {{}}; e[t.getAttribute('key')] = {GET}; return e;",
    "prototype_key": f"const e = {{}}; e['__proto__'] = {GET}; return e;",
    "prototype_scalar": "const e = {}; e.__proto__ = 1; return e;",
    "prototype_after_spread": f"const i = {GET}; const e = {{...('object' == typeof i ? i : {{}})}}; "
    f"e.__proto__ = {LATER}; return e;",
    "prototype_mutation": f"Object.prototype.saved = t; const e = {{}}; e.saved = {GET}; return e;",
    "target_prototype": f"const e = {{}}; Object.setPrototypeOf(e, t); e.saved = {GET}; return e;",
    "unknown_value": "const e = {}; e.saved = t; return e;",
    "undefined_value": "const e = {}; e.saved = undefined; return e;",
    "cycle": "const e = {}; e.saved = e; return e;",
    "target_identity": f"const e = {{}}; e.saved = {GET}; return e === e;",
    "target_capture": f"const e = {{}}; e.saved = {GET}; return () => e;",
    "target_escape": f"const e = {{}}; e.saved = {GET}; t.saved = e; return e;",
    "source_alias_mutation": f"const saved = {GET}; const e = {{}}; e.saved = saved; saved.x = 1; return e;",
    "loop_alias": "const e = {}, result = {}; let first = true; "
    f"for (const n of {BOOTSTRAP_FILTER}) {{ e.saved = M(t.dataset[n]); "
    "if (first) { result.saved = e; first = false; } } return result;",
    "loop_snapshot": "const e = {}; let observed = ''; "
    f"for (const n of {BOOTSTRAP_FILTER}) {{ e.saved = M(t.dataset[n]); "
    "const copied = {...e}; observed = typeof copied; } return observed;",
    "identity_before_write": f"const e = {{}}; const observed = e === e; e.saved = {GET}; return e;",
    "dynamic_after_spread": f"const i = {GET}; const e = {{...('object' == typeof i ? i : {{}})}}; "
    "for (const n of Object.keys(t.dataset)) { e[n] = M(t.dataset[n]); } return e;",
    "dynamic_prior_write": "const e = {}; e.__proto__ = null; "
    "for (const n of Object.keys(t.dataset)) { e[n] = M(t.dataset[n]); } return e;",
    "dynamic_later_write": "const e = {}; "
    "for (const n of Object.keys(t.dataset)) { e[n] = M(t.dataset[n]); } e.saved = 1; return e;",
    "dynamic_second_writer": "const e = {}; "
    "for (const n of Object.keys(t.dataset)) { e[n] = M(t.dataset[n]); e[n] = null; } return e;",
    "dynamic_repeated_loop": "const e = {}; const keys = Object.keys(t.dataset); "
    "for (const n of keys) { e[n] = M(t.dataset[n]); } "
    "for (const n of keys) { e[n] = M(t.dataset[n]); } return e;",
    "dynamic_nested_loop": "const e = {}; for (const n of Object.keys(t.dataset)) { "
    "for (let i = 0; i < 2; i = i + 1) { e[n] = M(t.dataset[n]); } } return e;",
    "dynamic_repeated_index": "const e = {}; const keys = Object.keys(t.dataset); "
    "for (let i = 0; i < keys.length; i = i + 1) { const n = keys[0]; "
    "e[n] = M(t.dataset[n]); } return e;",
    "dynamic_transformed_key": "const e = {}; "
    "for (const n of Object.keys(t.dataset)) { e[n + ''] = M(t.dataset[n]); } return e;",
    "dynamic_colliding_keys": "const e = {}; "
    "for (const n of Object.keys(t.dataset)) { e[n.slice(2)] = M(t.dataset[n]); } return e;",
    "dynamic_mutated_keys": "const e = {}; const keys = Object.keys(t.dataset); "
    "keys[0] = '__proto__'; for (const n of keys) { e[n] = M(t.dataset[n]); } return e;",
    "dynamic_stale_keys": "const e = {}; const keys = Object.keys(t.dataset); "
    "t.setAttribute('data-later', 'new'); "
    "for (const n of keys) { e[n] = M(t.dataset[n]); } return e;",
    "dynamic_loop_snapshot": "const e = {}; let observed = ''; "
    "for (const n of Object.keys(t.dataset)) { e[n] = M(t.dataset[n]); "
    "const copied = {...e}; observed = typeof copied; } return observed;",
    "dynamic_member_read": "const e = {}; let observed = null; "
    "for (const n of Object.keys(t.dataset)) { e[n] = M(t.dataset[n]); observed = e[n]; } "
    "return observed;",
    "dynamic_prototype_read": "const e = {}; "
    "for (const n of Object.keys(t.dataset)) { e[n] = M(t.dataset[n]); } "
    "return Object.getPrototypeOf(e);",
    "prefix_unfiltered": BODIES["prefix_result"].replace(
        BOOTSTRAP_FILTER, "Object.keys(t.dataset)"
    ),
    "prefix_either_preimage": BODIES["prefix_result"].replace(
        't.startsWith("bs") && !t.startsWith("bsConfig")',
        't.startsWith("bs") || t.startsWith("__proto__")',
    ),
    "prefix_exclusion_only": BODIES["prefix_result"].replace(
        't.startsWith("bs") && !t.startsWith("bsConfig")', '!t.startsWith("bsConfig")'
    ),
    "prefix_dataset_lookup": BODIES["prefix_result"].replace(
        "t.dataset[n]", "t.dataset[n.replace(/^bs/, '')]"
    ),
    "prefix_twice": BODIES["prefix_result"].replace(
        "result[n.replace(/^bs/, '')]", "result[n.replace(/^bs/, '').replace(/^bs/, '')]"
    ),
    "prefix_changed_pattern": BODIES["prefix_result"].replace("/^bs/", "/^b/"),
    "prefix_changed_flags": BODIES["prefix_result"].replace("/^bs/", "/^bs/i"),
    "prefix_second_writer": BODIES["prefix_result"].replace(
        "M(t.dataset[n]);", "M(t.dataset[n]); result[n.replace(/^bs/, '')] = null;"
    ),
}


for name, old, new in (
    ("normalized_dataset", "t.dataset[n]", "t.dataset[i]"),
    ("normalized_tail", "i.slice(1)", "n.slice(1)"),
    ("normalized_first", "i.charAt(0)", "n.charAt(0)"),
    ("normalized_whole", "i.charAt(0).toLowerCase()", "i.toLowerCase()"),
    ("normalized_arguments", ".toLowerCase()", ".toLowerCase(1)"),
    ("normalized_unfiltered", BOOTSTRAP_FILTER, "Object.keys(t.dataset)"),
    ("normalized_second_writer", "return result;", "result.other = 1; return result;"),
    ("normalized_effect", "let i =", "t.setAttribute('data-bs-new', '1'); let i ="),
):
    REFUSALS[name] = BODIES["normalized_result"].replace(old, new)


def oracle_source(accessors):
    """Return (JS source, labels): every FIXTURES run against a stand-in element
    that traces dataset and attribute reads. With `accessors`, dataset entries
    are getters that record their own reads."""
    source = "".join(SOURCES.values()) + values.OBSERVE
    labels = []
    descriptor = (
        "get() { trace.push('dataset:' + key); return pair[1]; }" if accessors else "value: pair[1]"
    )
    for name, fixtures in FIXTURES.items():
        for attrs in fixtures:
            label = f"assignmentObservation{len(labels):03}"
            labels.append(label)
            if name in (
                "assign_loop",
                "loop_dynamic_key",
                "loop_dynamic_all",
                "prefix_result",
                "normalized_result",
            ):
                keys = [
                    re.sub(r"-([a-z])", lambda m: m[1].upper(), attr[5:])
                    for attr, _ in attrs
                    if attr.startswith("data-")
                ]
                trace = ["dataset"] + [
                    item
                    for key in keys
                    if name == "loop_dynamic_all"
                    or (key.startswith("bs") and not key.startswith("bsConfig"))
                    for item in (("dataset", "dataset:" + key) if accessors else ("dataset",))
                ]
            else:
                trace = ["get:data-bs-config"]
                if name in ("assign_order", "assign_spread"):
                    trace.append("get:data-bs-later")
                elif name == "assign_optional":
                    trace.insert(0, "has:data-bs-config")
                elif name == "assign_branch":
                    trace = [
                        "has:data-bs-config",
                        "get:"
                        + (
                            "data-bs-config"
                            if dict(attrs).get("data-bs-config") is not None
                            else "data-bs-later"
                        ),
                    ]
            source += f"""
var {label} = (function() {{
    const attributes = {json.dumps(attrs)};
    const trace = [];
    const target = {{}};
    const keys = [];
    for (const pair of attributes) {{
        if (pair[0].startsWith('data-')) {{
            const key = pair[0].slice(5).replace(/-([a-z])/g, (_, letter) => letter.toUpperCase());
            keys.push(key);
            Object.defineProperty(target, key, {{enumerable: true, {descriptor}}});
        }}
    }}
    // DOM named properties preserve attribute order, including numeric names.
    const data = new Proxy(target, {{ownKeys() {{ return keys.slice(); }} }});
    const result = {name}({{
        get dataset() {{ trace.push('dataset'); return data; }},
        hasAttribute(key) {{ trace.push('has:' + key); return attributes.some(pair => pair[0] === key); }},
        getAttribute(key) {{ trace.push('get:' + key); const pair = attributes.find(pair => pair[0] === key); return pair ? pair[1] : null; }}
    }});
    if (trace.join('|') !== {json.dumps("|".join(trace))}) throw new Error('assignment source order: ' + trace.join('|'));
    return observe(result);
}})();
"""
    return source, labels


def oracles(args):
    """Run oracle_source under Node and the VM; return the expected observations."""
    expected = None
    for accessors in (True, False):
        source, labels = oracle_source(accessors)
        node = args.work / f"assignment-node-{'accessors' if accessors else 'data'}.js"
        node.write_text(source + "".join(f"console.log({label});\n" for label in labels))
        observed = run([args.node, node]).stdout.splitlines()
        assert len(observed) == len(labels), observed
        if expected is None:
            expected = observed
        else:
            assert observed == expected, (observed, expected)
    # The VM's Object.keys invokes enumerable accessors, so only Node checks the
    # getter trace (above). Both oracles compare the results with plain data
    # properties, and both still check the dataset getter counts and the order
    # of getAttribute/hasAttribute calls.
    vm = args.work / "assignment-vm.js"
    vm.write_text(source)
    actual = run([args.reference, vm]).stdout
    wanted = "".join(f'{label}="{values.quote(value)}"\n' for label, value in zip(labels, expected))
    assert actual == wanted, (actual, wanted)
    return expected


def client(name, symbol, owned):
    """Return C++ that runs every FIXTURES[name] entry through `symbol`, checks
    it makes no DOM writes, then changes the attribute and destroys the
    document before printing the results, so they must own their values."""
    setup = (
        f"{symbol}_session session; auto & doc = session.document();"
        if owned
        else "atom_table atoms; document doc{atoms};"
    )
    call = "session.invoke(element)" if owned else f"{symbol}(element)"
    fixtures = ",".join(
        "{"
        + ",".join(
            "{std::string{"
            + number_conversion.cpp_string(key)
            + f", {len(key.encode())}"
            + "},std::string{"
            + number_conversion.cpp_string(value)
            + f", {len(value.encode())}"
            + "}}"
            for key, value in attrs
        )
        + "}"
        for attrs in FIXTURES[name]
    )
    return f"""
    {{
        std::vector<json_value> survivors;
        {{
            {setup}
            doc.log_writes(true);
            for (const auto & attributes : std::vector<std::vector<std::pair<std::string, std::string>>>{{{fixtures}}}) {{
                const auto node = doc.create_element(doc.atoms().intern("button"));
                element_ref element{{&doc, node}};
                for (const auto & [key, value] : attributes) {{ assert(doc.set_attribute(node, doc.atoms().intern(key), value)); }}
                (void)doc.take_writes();
                const auto version = doc.version();
                auto result = {call};
                static_assert(std::is_same_v<decltype(result), json_value>);
                assert(doc.version() == version && doc.take_writes().empty());
                assert(doc.set_attribute(node, doc.atoms().intern("data-bs-config"), "changed"));
                survivors.push_back(std::move(result));
            }}
        }}
        for (const auto & result : survivors) {{ observe(result); }}
    }}
"""


def check_output(args, output, expected):
    """Compare the native program's observations with the oracle's, including
    each object's raw key order."""
    actual = output.splitlines()
    assert len(actual) == len(expected), (len(actual), len(expected))
    for index, (value, wanted) in enumerate(zip(actual, expected)):
        assert value.split(":", 1)[0] == wanted.split(":", 1)[0], (value, wanted)
        if not wanted.startswith("object:{"):
            continue
        # Compare the native object's raw key order, duplicates included, before
        # the normalizing pass below re-serializes it.
        keys = [key for key, _ in json.loads(value[7:], object_pairs_hook=list)]
        wanted_keys = [key for key, _ in json.loads(wanted[7:], object_pairs_hook=list)]
        assert keys == wanted_keys, (index, keys, wanted_keys)
    # The core JSON type keeps parse order, nested numeric names included.
    # Re-serialize each object through JS so it enumerates in JS key order,
    # then compare values and types; insertion order was checked above.
    observed = run(
        [
            args.node,
            "-e",
            "for (const line of require('fs').readFileSync(0, 'utf8').trimEnd().split('\\n')) "
            "console.log(line.startsWith('object:') ? 'object:' + "
            "JSON.stringify(JSON.parse(line.slice(7))) : line);",
        ],
        input_text=output,
    ).stdout.splitlines()
    assert observed == expected, [
        (i, a, e) for i, (a, e) in enumerate(zip(observed, expected)) if a != e
    ]


def main():
    args = dom.parse_args(__doc__, oracles=True)
    vendor = dom.BOOTSTRAP
    assert values.BOOTSTRAP_M in vendor.read_text(), "Bootstrap M source pin changed"
    assert BOOTSTRAP_FILTER in vendor.read_text(), "Bootstrap filter source pin changed"
    expected = oracles(args)
    compilers = args.compilers
    includes, libraries = dom.link_options(args)
    intrinsics = PREFIX_INTRINSICS + ["Number", "JSON", "decodeURIComponent"]

    def manifest(contract, provider):
        return dict(
            contract,
            provider=provider,
            initial_intrinsics=intrinsics,
            dataset_parameters=[0],
        )

    # Every source, variant, build and refusal writes only files named after
    # itself, so they run side by side (dom.parallel).
    prepared = dom.parallel(
        lambda row: (row[0], *dom.prepare(args, row[0], row[1], 1, entry_name=row[0])),
        SOURCES.items(),
    )

    def lower(task):
        label, name, ir, contract, provider, owned, optimize = task
        native = dom.lower(args, ir, manifest(contract, provider), label, optimize=optimize)
        deduced = args.work / f"{label}.deduced.mlir"
        run([args.opt, native, "--ctnative-print-deduced", "-o", deduced])
        return name, owned, {"explicit": native, "deduced": deduced}

    def emit(row):
        """The checked C++ of one lowered module and its entry symbol."""
        _, name, module = row
        entries = dom.NATIVE.findall(module.read_text())
        assert len(entries) == (
            2
            if name in ("assign_loop", "loop_dynamic_key", "prefix_result", "normalized_result")
            else 1
        ) and not dom.FUNCTION.search(module.read_text()), module.read_text()
        symbol = next(entry for entry in entries if re.fullmatch(re.escape(name) + r"_\d+", entry))
        cpp = run([args.translate, "--mlir-to-cpp", module]).stdout
        assert "ctbrowser::json_value" in cpp and not dom.VM.search(cpp), cpp
        assert not re.search(
            r"shared_ptr|weak_ptr|std::variant|nullable_scalar|std::function|__ctbrowser_",
            cpp,
        ), cpp
        if name != "assign_optional":
            assert "ctbrowser::parse_json" in cpp and "ctbrowser::decode_uri_component" in cpp, cpp
        if name in (
            "loop_dynamic_key",
            "loop_dynamic_all",
            "prefix_result",
            "normalized_result",
        ):
            assert "ctnative::dataset_value(" in cpp, cpp
            assert "ctnative::assign_json_snapshot_property(" in cpp, cpp
            assert "__proto__" not in cpp, cpp
        return cpp, symbol

    flags = [*FLAGS, *includes]
    builds = []
    for optimize in (False, True):
        modules = dom.parallel(
            lower,
            [
                (f"{name}-{owned}-{optimize}", name, ir, contract, provider, owned, optimize)
                for provider in dom.PROVIDERS
                for owned in ["session" in provider]
                for name, ir, contract in prepared
            ],
        )
        for layout in ("explicit", "deduced"):
            headers, bodies, clients = set(), [], []
            units = dom.parallel(
                emit,
                [
                    (f"{name}-{owned}-{optimize}-{layout}", name, layouts[layout])
                    for name, owned, layouts in modules
                ],
            )
            for (name, owned, _), (cpp, symbol) in zip(modules, units):
                headers.update(re.findall(r"^#(?:include|define CTNATIVE_)[^\n]*", cpp, re.M))
                body = re.sub(r"^#(?:include|define CTNATIVE_)[^\n]*\n?", "", cpp, flags=re.M)
                namespace = name + ("_session" if owned else "_free")
                bodies.append(f"namespace {namespace} {{\n{body}\n}}\n")
                clients.append(client(name, namespace + "::" + symbol, owned))
            path = args.work / f"combined-{optimize}-{layout}.cpp"
            path.write_text(
                "\n".join(sorted(headers))
                + "\n"
                + "".join(bodies)
                + values.CLIENT.replace("@RUNS@", "\n".join(clients))
            )
            for index, compiler in enumerate(compilers):
                builds.append((f"{path.stem}.{index}", path, compiler, False))
            if optimize and layout == "explicit":
                builds.append((f"{path.stem}.sanitized", path, compilers[1], True))

    def build(case):
        """Build and run one combined program (with GCC, Clang, or Clang's
        sanitizers) and check its observations."""
        label, path, compiler, sanitize = case
        if sanitize:
            result = build_and_run(
                compiler,
                path,
                path.with_suffix(".sanitized"),
                # Keep the fixtures out of line in the combined sanitizer build.
                flags=[*flags, "-fno-inline-functions"],
                libraries=libraries,
                sanitize=True,
            )
            assert not result.stderr, result.stderr
        else:
            result = build_and_run(
                compiler,
                path,
                args.work / label,
                flags=flags,
                libraries=libraries,
                nm=args.nm,
                vm=dom.VM,
            )
        check_output(args, result.stdout, expected * 2)

    dom.parallel(build, builds)

    def refuse(row):
        name, body = row
        ir, contract = dom.prepare(
            args,
            name,
            f"function {name}(t) {{ {values.BOOTSTRAP_M if 'M(' in body else ''} {body} }}",
            1,
            entry_name=name,
        )
        for provider in dom.PROVIDERS:
            dom.refuse(args, ir, manifest(contract, provider), f"refuse-{name}-{provider}")
        return 2 * len(dom.PROVIDERS)

    refusals = sum(dom.parallel(refuse, REFUSALS.items()))

    def exhaust(task):
        label, ir, contract, provider, optimize, budget = task
        dom.lower(
            args,
            ir,
            manifest(contract, provider),
            label,
            optimize=optimize,
            max_steps=budget,
            success=False,
        )

    budgets = [
        (f"budget-{name}-{provider}-{optimize}-{budget}", ir, contract, provider, optimize, budget)
        for name in ("assign_loop", "loop_dynamic_key", "prefix_result", "normalized_result")
        for _, ir, contract in [next(row for row in prepared if row[0] == name)]
        for provider in dom.PROVIDERS
        for optimize in (False, True)
        for budget in (0, 64, 256)
    ]
    dom.parallel(exhaust, budgets)
    refusals += len(budgets)
    print(
        f"native DOM assignments: {len(SOURCES)} sources, {len(expected)} Node/VM source-double observations plus Node accessor traces, 8 GCC/Clang binaries, lifetime sanitizer, both providers/policies/layouts, {refusals} refusals"
    )


if __name__ == "__main__":
    main()
