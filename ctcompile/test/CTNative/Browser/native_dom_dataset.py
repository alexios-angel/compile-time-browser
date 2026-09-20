#!/usr/bin/env python3
"""Proved HTML/SVG dataset snapshots, filtering, iteration and present values."""

import argparse
import json
import os
from pathlib import Path
import re
import shutil

from CTNative.Browser import native_dom as dom
from CTNative.Browser.native_dom_json import BOOTSTRAP_M, quote
from CTNative.harness import find_compilers, run
from Target.Cpp.harness import FLAGS

BODIES = {
    "dataset_keys": "return Object.keys(element.dataset);",
    "dataset_alias": "const data = element.dataset; return Object.keys(data);",
    "dataset_snapshot": "const keys = Object.keys(element.dataset); "
    "element.setAttribute('data-later', 'x'); element.removeAttribute('data-bs-z'); return keys;",
    "dataset_after_write": "element.setAttribute('data-later', 'x'); return Object.keys(element.dataset);",
    "dataset_reread": "Object.keys(element.dataset); element.setAttribute('data-later', 'x'); "
    "element.removeAttribute('data-bs-z'); return Object.keys(element.dataset);",
}
BOOTSTRAP_FILTER = (
    'Object.keys(t.dataset).filter(t => t.startsWith("bs") && !t.startsWith("bsConfig"))'
)
PREDICATE = 't => t.startsWith("bs") && !t.startsWith("bsConfig")'
BOOTSTRAP_GUARD = "if (!t) return {};"
FILTER_BODIES = {
    "dataset_filter": f"const t = element; return {BOOTSTRAP_FILTER};",
    "dataset_filter_length": f"const t = element; return ({BOOTSTRAP_FILTER}).length;",
    "dataset_filter_alias": f"const keys = Object.keys(element.dataset); "
    f"const selected = keys.filter({PREDICATE}); return selected;",
    "dataset_filter_snapshot": f"const t = element; const selected = {BOOTSTRAP_FILTER}; "
    "element.setAttribute('data-later', 'x'); element.removeAttribute('data-bs-z'); return selected;",
    "dataset_filter_saved_keys": "const keys = Object.keys(element.dataset); "
    "element.setAttribute('data-later', 'x'); element.removeAttribute('data-bs-z'); "
    f"return keys.filter({PREDICATE});",
    "dataset_filter_prefix": "return Object.keys(element.dataset).filter(key => key.startsWith('bs'));",
    "dataset_filter_helper": f"function select(keys) {{ return keys.filter({PREDICATE}); }} "
    "return select(Object.keys(element.dataset));",
}
LOOP_COUNT = (
    f"let count = 0; for (const n of {BOOTSTRAP_FILTER}) {{ count = count + 1; }} return count;"
)
ITERATION_INTRINSICS = [
    "Object",
    "Array",
    "String",
    "__ctbrowser_for_of_open",
    "__ctbrowser_iter_next",
    "__ctbrowser_iter_close",
]
PREFIX_INTRINSICS = ITERATION_INTRINSICS + ["RegExp", "__ctbrowser_regexp"]
PREFIX = 'let i = n.replace(/^bs/, "");'
LOOP_SOURCES = {
    "dataset_loop": f"function dataset_loop(t) {{ {LOOP_COUNT} }}\n",
    "dataset_loop_order": f"function dataset_loop_order(t) {{ let joined = ''; "
    f"for (const n of {BOOTSTRAP_FILTER}) {{ joined = joined + n + '|'; }} return joined; }}\n",
    "dataset_loop_snapshot": f"function dataset_loop_snapshot(t) {{ const selected = {BOOTSTRAP_FILTER}; "
    "t.setAttribute('data-bs-later', 'x'); t.removeAttribute('data-bs-z'); let joined = ''; "
    "for (const n of selected) { joined = joined + n + '|'; } return joined; }\n",
}
PREFIX_SOURCES = {
    "dataset_loop_prefix": f"function dataset_loop_prefix(t) {{ let joined = ''; "
    f"for (const n of {BOOTSTRAP_FILTER}) {{ {PREFIX} joined = joined + i + '|'; }} return joined; }}\n",
    "dataset_loop_prefix_all": "function dataset_loop_prefix_all(t) { let joined = ''; "
    f"for (const n of Object.keys(t.dataset)) {{ {PREFIX} joined = joined + i + '|'; }} return joined; }}\n",
}
LOOP_SOURCES.update(PREFIX_SOURCES)
VALUE_SOURCES = {
    "dataset_values": "function dataset_values(t) { let joined = ''; "
    f"for (const n of {BOOTSTRAP_FILTER}) {{ joined = joined + t.dataset[n] + '|'; }} return joined; }}\n",
    "dataset_values_element_alias": "function dataset_values_element_alias(t) { const alias = t; let joined = ''; "
    f"for (const n of {BOOTSTRAP_FILTER}) {{ joined = joined + alias.dataset[n] + '|'; }} return joined; }}\n",
    "dataset_values_dataset_alias": "function dataset_values_dataset_alias(t) { const data = t.dataset; let joined = ''; "
    f"for (const n of Object.keys(data).filter({PREDICATE})) {{ joined = joined + data[n] + '|'; }} return joined; }}\n",
    "dataset_values_saved": "function dataset_values_saved(t) { "
    f"const selected = {BOOTSTRAP_FILTER}; let joined = ''; "
    "for (const n of selected) { joined = joined + t.dataset[n] + '|'; } return joined; }\n",
    "dataset_values_all": "function dataset_values_all(t) { let joined = ''; "
    "for (const n of Object.keys(t.dataset)) { joined = joined + t.dataset[n] + '|'; } return joined; }\n",
}
VALUE_SOURCES["dataset_guarded_values"] = (
    f"function dataset_guarded_values(t) {{ {BOOTSTRAP_GUARD} let joined = ''; "
    f"for (const n of {BOOTSTRAP_FILTER}) {{ joined = joined + t.dataset[n] + '|'; }} return joined; }}\n"
)
NORMALIZED_SOURCES = {
    name: f"function {name}(t) {{ {BOOTSTRAP_M} {BOOTSTRAP_GUARD} let joined = ''; "
    f"for (const n of {BOOTSTRAP_FILTER}) {{ joined = joined + typeof M({value}) + '|'; }} return joined; }}\n"
    for name, value in (
        ("dataset_normalized_values", "t.dataset[n]"),
        ("dataset_normalized_attribute", "t.getAttribute('data-bs-config')"),
    )
}
VALUE_SOURCES.update(NORMALIZED_SOURCES)
LOOP_SOURCES.update(VALUE_SOURCES)
LOOP_SOURCES.update(
    {
        "dataset_guarded_count": f"function dataset_guarded_count(t) {{ {BOOTSTRAP_GUARD} {LOOP_COUNT} }}\n",
        "dataset_truthy_count": f"function dataset_truthy_count(t) {{ if (t) {{ {LOOP_COUNT} }} else {{ return {{}}; }} }}\n",
        "dataset_guard_alias_count": f"function dataset_guard_alias_count(element) {{ const t = element; {BOOTSTRAP_GUARD} {LOOP_COUNT} }}\n",
    }
)
VALUE_TEXT = {
    "bsZ": "first",
    "bsEmpty": "",
    "bsUnicode": "été 😀",
    "bs": "bare",
    "__proto__": "own proto",
    "10": "ten",
    "2": "two",
}
NORMALIZED_TEXT = VALUE_TEXT | {
    "bsZ": "42",
    "bsTrue": "true",
    "bsFalse": "false",
    "bsNull": "null",
    "bsObject": "%7B%22saved%22%3A%5Btrue%2Cnull%5D%7D",
    "bsBadUri": "%",
    "bsBadJson": "not%20json",
    "bsConfig": "true",
}
# Web IDL's named-property order is attribute order, including numeric names.
# Chrome independently measures this order; an ordinary object is the wrong double.
KEYS = ["bsZ", "10", "2", "01", "", "__proto__", "Foo", "foo-Bar"]
ATTRS = [
    "data-bs-z",
    "data-10",
    "data-2",
    "data-01",
    "data-",
    "data-__proto__",
    "data--foo",
    "data-foo--bar",
]
# (attribute names, exposed keys, keys retained by the original predicate).
FILTER_CASES = [
    (
        [
            "data-bs-z",
            "data-10",
            "data-bs-config",
            "data-2",
            "data-bs-toggle",
            "data-bs-config-more",
            "data-bsconfig",
            "data-bs",
            "data-b",
            "data-__proto__",
            "data-bsé",
        ],
        [
            "bsZ",
            "10",
            "bsConfig",
            "2",
            "bsToggle",
            "bsConfigMore",
            "bsconfig",
            "bs",
            "b",
            "__proto__",
            "bsé",
        ],
        ["bsZ", "bsToggle", "bsconfig", "bs", "bsé"],
    ),
    ([], [], []),
    (["data-bs-z", "data-bs", "data-bs-a"], ["bsZ", "bs", "bsA"], ["bsZ", "bs", "bsA"]),
    (
        ["data-bs-config", "data-bs-config-more", "data-2", "data-b"],
        ["bsConfig", "bsConfigMore", "2", "b"],
        [],
    ),
]
SOURCES = {
    name: f"function {name}(element) {{ {body} }}\n"
    for name, body in (BODIES | FILTER_BODIES).items()
} | LOOP_SOURCES
CASES = {name: [(ATTRS, KEYS, KEYS)] if name in BODIES else FILTER_CASES for name in SOURCES}
for name in PREFIX_SOURCES:
    CASES[name] = FILTER_CASES + [
        (
            [
                "data-bsÉtage",
                "data-bsİtem",
                "data-bs𐐀name",
                "data-bs😀name",
                "data-",
                "data-b",
                "data-abs",
            ],
            ["bsÉtage", "bsİtem", "bs𐐀name", "bs😀name", "", "b", "abs"],
            ["bsÉtage", "bsİtem", "bs𐐀name", "bs😀name"],
        ),
    ]
for name in VALUE_SOURCES:
    CASES[name] = [
        ([], [], []),
        (["data-bs-config", "data-other"], ["bsConfig", "other"], []),
        (["data-bs-z", "data-bs-empty"], ["bsZ", "bsEmpty"], ["bsZ", "bsEmpty"]),
        (
            ["data-bs-unicode", "data-bs-empty"],
            ["bsUnicode", "bsEmpty"],
            ["bsUnicode", "bsEmpty"],
        ),
        (
            ["data-__proto__", "data-10", "data-2", "data-bs"],
            ["__proto__", "10", "2", "bs"],
            ["bs"],
        ),
    ]
for name in NORMALIZED_SOURCES:
    CASES[name] = CASES[name] + [
        (
            [
                "data-bs-z",
                "data-bs-true",
                "data-bs-false",
                "data-bs-null",
                "data-bs-object",
                "data-bs-bad-uri",
                "data-bs-bad-json",
                "data-bs-config",
            ],
            ["bsZ", "bsTrue", "bsFalse", "bsNull", "bsObject", "bsBadUri", "bsBadJson", "bsConfig"],
            ["bsZ", "bsTrue", "bsFalse", "bsNull", "bsObject", "bsBadUri", "bsBadJson"],
        ),
    ]
# An optional fourth field overrides attribute values for this observation.
CASES["dataset_normalized_attribute"] += [
    (
        ["data-bs-z", "data-bs-config"],
        ["bsZ", "bsConfig"],
        ["bsZ"],
        {"bsConfig": value},
    )
    for value in ("%7B%22saved%22%3Atrue%7D", "%", "not%20json")
]
REFUSALS = {
    "missing_read": "return element.dataset.missing;",
    "dataset_escape": "return element.dataset;",
    "dataset_write": "element.dataset.bsZ = 'x'; return Object.keys(element.dataset);",
    "keys_mutation": "const keys = Object.keys(element.dataset); keys[0] = 'x'; return keys;",
    "keys_identity": "const keys = Object.keys(element.dataset); return keys === keys;",
    "stale_alias": "const data = element.dataset; element.setAttribute('data-later', 'x'); return Object.keys(data);",
    "replaced_keys": "Object.keys = element; return Object.keys(element.dataset);",
    "wrong_receiver": "const keys = Object.keys; return keys(element.dataset);",
    "extra_argument": "return Object.keys(element.dataset, 1);",
    "ordinary_object": "return Object.keys({a: 1});",
    "replaced_filter": f"Array.prototype.filter = element; const t = element; return {BOOTSTRAP_FILTER};",
    "replaced_startswith": f"String.prototype.startsWith = element; const t = element; return {BOOTSTRAP_FILTER};",
    "replaced_constructor": f"Array.prototype.constructor = element; const t = element; return {BOOTSTRAP_FILTER};",
    "custom_species": "Object.defineProperty(Array, Symbol.species, {value: function() { return {}; }}); "
    f"const t = element; return {BOOTSTRAP_FILTER};",
    "own_constructor": "const keys = Object.keys(element.dataset); keys.constructor = element; "
    f"return keys.filter({PREDICATE});",
    "filter_wrong_receiver": "const keys = Object.keys(element.dataset); const filter = keys.filter; "
    f"return filter({PREDICATE});",
    "startswith_wrong_receiver": "return Object.keys(element.dataset).filter(key => "
    "{ const starts = key.startsWith; return starts('bs'); });",
    "filter_this_arg": f"return Object.keys(element.dataset).filter({PREDICATE}, element);",
    "startswith_position": "return Object.keys(element.dataset).filter(key => key.startsWith('bs', 1));",
    "startswith_nonstring": "return Object.keys(element.dataset).filter(key => key.startsWith(1));",
    "startswith_unicode_prefix": "return Object.keys(element.dataset).filter(key => key.startsWith('é'));",
    "filter_receiver_mutation": "const keys = Object.keys(element.dataset); const alias = keys; "
    f"alias[0] = 'bsChanged'; return keys.filter({PREDICATE});",
    "filter_result_mutation": f"const t = element; const selected = {BOOTSTRAP_FILTER}; "
    "selected[0] = 'changed'; return selected;",
    "filter_length_mutation": f"const t = element; const selected = {BOOTSTRAP_FILTER}; "
    "selected.length = 0; return selected.length;",
    "filter_receiver_escape": "const keys = Object.keys(element.dataset); element.saved = keys; "
    f"return keys.filter({PREDICATE});",
    "filter_callback_escape": f"const callback = {PREDICATE}; "
    "Object.keys(element.dataset).filter(callback); return callback;",
    "filter_callback_mutation": f"let callback = {PREDICATE}; callback = element; "
    "return Object.keys(element.dataset).filter(callback);",
    "filter_callback_write": "return Object.keys(element.dataset).filter(key => "
    "{ element.setAttribute('data-call', 'x'); return key.startsWith('bs'); });",
    "filter_callback_capture": "const prefix = element.getAttribute('prefix'); "
    "return Object.keys(element.dataset).filter(key => key.startsWith(prefix));",
    "filter_callback_array_write": "return Object.keys(element.dataset).filter((key, index, keys) => "
    "{ keys[0] = 'bsChanged'; return key.startsWith('bs'); });",
    "filter_callback_index": "return Object.keys(element.dataset).filter((key, index) => index === 0);",
    "filter_callback_this": "return Object.keys(element.dataset).filter(function(key) "
    "{ return this === element; });",
    **{
        f"loop_replaced_{helper}": f"__ctbrowser_{helper} = element; const t = element; {LOOP_COUNT}"
        for helper in ("for_of_open", "iter_next", "iter_close")
    },
    "loop_replaced_iterator": "Array.prototype[Symbol.iterator] = element; "
    f"const t = element; {LOOP_COUNT}",
    "loop_replaced_next": "Object.getPrototypeOf([][Symbol.iterator]()).next = element; "
    f"const t = element; {LOOP_COUNT}",
    "loop_replaced_return": "Object.getPrototypeOf([][Symbol.iterator]()).return = element; "
    f"const t = element; {LOOP_COUNT}",
    "loop_own_iterator": f"const t = element; const selected = {BOOTSTRAP_FILTER}; "
    "selected[Symbol.iterator] = element; let count = 0; "
    "for (const n of selected) { count = count + 1; } return count;",
    "loop_vector_mutation": f"const t = element; const selected = {BOOTSTRAP_FILTER}; "
    "let count = 0; for (const n of selected) { selected[0] = n; count = count + 1; } return count;",
    "loop_iterator_escape": f"const t = element; const selected = {BOOTSTRAP_FILTER}; "
    "return __ctbrowser_for_of_open(selected);",
    "loop_dom_mutation": f"const t = element; let count = 0; for (const n of {BOOTSTRAP_FILTER}) "
    "{ t.setAttribute('data-bs-later', 'x'); count = count + 1; } return count;",
    "loop_global_write": f"const t = element; let count = 0; for (const n of {BOOTSTRAP_FILTER}) "
    "{ saved = n; count = count + 1; } return count;",
    "loop_callback": f"const t = element; let count = 0; for (const n of {BOOTSTRAP_FILTER}) "
    "{ element(n); count = count + 1; } return count;",
    "guard_live_write": f"const t = element; {BOOTSTRAP_GUARD} saved = 1; {LOOP_COUNT}",
    "guard_unknown": f"const t = element; if (!unknown) return {{}}; {LOOP_COUNT}",
    "guard_nullable_element": f"const t = element.closest('.missing'); {BOOTSTRAP_GUARD} {LOOP_COUNT}",
    "guard_optional_string": f"const t = element.getAttribute('data-any'); {BOOTSTRAP_GUARD} {LOOP_COUNT}",
    "guard_ordinary_object": f"const t = {{}}; {BOOTSTRAP_GUARD} {LOOP_COUNT}",
}

PREFIX_LOOP = f"const t = element; let joined = ''; for (const n of {BOOTSTRAP_FILTER}) {{ {PREFIX} joined = joined + i; }} return joined;"
REFUSALS.update(
    {
        "prefix_unanchored": PREFIX_LOOP.replace("/^bs/", "/bs/"),
        "prefix_flags": PREFIX_LOOP.replace("/^bs/", "/^bs/i"),
        "prefix_stateful": PREFIX_LOOP.replace("/^bs/", "/^bs/g"),
        "prefix_pattern": PREFIX_LOOP.replace("/^bs/", "/^b./"),
        "prefix_nonempty": PREFIX_LOOP.replace('replace(/^bs/, "")', 'replace(/^bs/, "x")'),
        "prefix_nonstring": PREFIX_LOOP.replace('replace(/^bs/, "")', "replace(/^bs/, 0)"),
        "prefix_dynamic_replacement": PREFIX_LOOP.replace(
            'replace(/^bs/, "")', "replace(/^bs/, n)"
        ),
        "prefix_dynamic_flags": PREFIX_LOOP.replace("/^bs/", '__ctbrowser_regexp("^bs", n)'),
        "prefix_wrong_receiver": PREFIX_LOOP.replace(
            PREFIX, 'const replace = n.replace; let i = replace(/^bs/, "");'
        ),
        "prefix_extra_argument": PREFIX_LOOP.replace('replace(/^bs/, "")', 'replace(/^bs/, "", 1)'),
        "prefix_escape": PREFIX_LOOP.replace(
            PREFIX, 'const regex = /^bs/; element.saved = regex; let i = n.replace(regex, "");'
        ),
        "prefix_reuse": PREFIX_LOOP.replace(
            PREFIX, 'const regex = /^bs/; n.replace(regex, ""); let i = n.replace(regex, "");'
        ),
        **{
            f"prefix_replaced_{index}": f"{target} = element; {PREFIX_LOOP}"
            for index, target in enumerate(
                (
                    "__ctbrowser_regexp",
                    "String.prototype.replace",
                    "RegExp.prototype.exec",
                    "RegExp.prototype[Symbol.replace]",
                )
            )
        },
    }
)

VALUE_KEYS = f"const selected = Object.keys(element.dataset).filter({PREDICATE}); "
VALUE_LOOP = (
    "let joined = ''; for (const n of selected) { "
    "joined = joined + element.dataset[n] + '|'; } return joined;"
)
REFUSALS.update(
    {
        "value_deleted": VALUE_KEYS + "element.removeAttribute('data-bs-z'); " + VALUE_LOOP,
        "value_changed": VALUE_KEYS + "element.setAttribute('data-bs-z', 'new'); " + VALUE_LOOP,
        "value_unrelated_write": VALUE_KEYS
        + "element.setAttribute('data-other', 'x'); "
        + VALUE_LOOP,
        "value_stale_dataset": "const data = element.dataset; element.setAttribute('data-bs-z', 'new'); "
        + VALUE_KEYS
        + VALUE_LOOP.replace("element.dataset[n]", "data[n]"),
        "value_stale_keys_fresh_dataset": VALUE_KEYS
        + "element.setAttribute('data-bs-z', 'new'); const fresh = element.dataset; "
        + VALUE_LOOP.replace("element.dataset[n]", "fresh[n]"),
        "value_stale_keys_filtered_later": "const keys = Object.keys(element.dataset); "
        "element.removeAttribute('data-bs-z'); "
        f"const selected = keys.filter({PREDICATE}); " + VALUE_LOOP,
        "value_transformed_key": VALUE_KEYS
        + VALUE_LOOP.replace("dataset[n]", 'dataset[n.replace(/^bs/, "")]'),
        "value_equal_text_key": VALUE_KEYS + VALUE_LOOP.replace("dataset[n]", "dataset[n + '']"),
        "value_joined_key": VALUE_KEYS
        + VALUE_LOOP.replace(
            "joined = joined + element.dataset[n]",
            "const key = n.startsWith('bs') ? n : 'bsZ'; joined = joined + element.dataset[key]",
        ),
        "value_literal_key": VALUE_KEYS + VALUE_LOOP.replace("dataset[n]", "dataset['bsZ']"),
        "value_mutated_keys": VALUE_KEYS + "selected[0] = 'bsZ'; " + VALUE_LOOP,
        "value_loop_write": VALUE_KEYS
        + VALUE_LOOP.replace(
            "joined = joined +", "element.removeAttribute('data-bs-z'); joined = joined +"
        ),
    }
)


def fixture_values(name, keys, overrides):
    text = NORMALIZED_TEXT if name in NORMALIZED_SOURCES else VALUE_TEXT
    if overrides:
        text = text | overrides[0]
    return [text.get(key, "x") if name in VALUE_SOURCES else "x" for key in keys]


def oracles(args):
    source = "".join(SOURCES.values())
    names, wanted = [], {}
    for name in SOURCES:
        wanted[name] = []
        for _, keys, selected, *overrides in CASES[name]:
            label = f"datasetObservation{len(names)}"
            names.append(label)
            if name == "dataset_filter_prefix":
                selected = [key for key in keys if key.startswith("bs")]
            elif name in BODIES:
                selected = keys
                if name == "dataset_after_write":
                    selected = keys + ["later"]
                elif name == "dataset_reread":
                    selected = keys[1:] + ["later"]
            if name in PREFIX_SOURCES:
                if name == "dataset_loop_prefix_all":
                    selected = keys
                selected = [key.removeprefix("bs") for key in selected]
            values = fixture_values(name, keys, overrides)
            number = name in (
                "dataset_filter_length",
                "dataset_loop",
                "dataset_guarded_count",
                "dataset_truthy_count",
                "dataset_guard_alias_count",
            )
            if name in NORMALIZED_SOURCES:
                wanted[name].append(None)
            elif name in VALUE_SOURCES:
                if name == "dataset_values_all":
                    selected = keys
                first = "".join(VALUE_TEXT.get(key, "x") + "|" for key in selected)
                second = "".join(
                    ("second" if key == "bsZ" else VALUE_TEXT.get(key, "x")) + "|"
                    for key in selected
                )
                wanted[name].append(first + "~" + second)
            elif number:
                wanted[name].append(str(len(selected)))
            elif name in LOOP_SOURCES:
                wanted[name].append("".join(key + "|" for key in selected))
            else:
                wanted[name].append("|".join(selected))
            observation = (
                f"String({name}(element))"
                if number or name in LOOP_SOURCES
                else f"{name}(element).join('|')"
            )
            observe = f"return {observation};"
            if name in VALUE_SOURCES:
                observe = f"""
    const saved = {name}(element);
    if (keys.indexOf('bsZ') >= 0) Object.defineProperty(target, 'bsZ', {{value: 'second'}});
    const updated = {name}(element);
    return saved + '~' + updated;
"""
            source += f"""
var {label} = (function() {{
    const keys = {json.dumps(keys)};
    const values = {json.dumps(values)};
    const target = {{}};
    for (let index = 0; index < keys.length; ++index) Object.defineProperty(target, keys[index], {{value: values[index], enumerable: true, configurable: true}});
    const dataset = new Proxy(target, {{ownKeys() {{ return keys.slice(); }} }});
    const element = {{dataset,
        getAttribute(key) {{
            if (key !== 'data-bs-config') throw new Error('unexpected attribute read');
            return keys.indexOf('bsConfig') >= 0 ? target.bsConfig : null;
        }},
        setAttribute(key, value) {{
            if ((key !== 'data-later' && key !== 'data-bs-later') || value !== 'x') throw new Error('unexpected write');
            const name = key === 'data-later' ? 'later' : 'bsLater';
            keys.push(name); Object.defineProperty(target, name, {{value, enumerable: true, configurable: true}});
        }},
        removeAttribute(key) {{
            if (key !== 'data-bs-z') throw new Error('unexpected removal');
            const index = keys.indexOf('bsZ');
            if (index >= 0) keys.splice(index, 1);
            delete target.bsZ;
        }}
    }};
    {observe}
}})();
"""
    node = args.work / "dataset-node.js"
    node.write_text(source + "".join(f"console.log({name});\n" for name in names))
    expected = run([args.node, node]).stdout.splitlines()
    # Original M supplies the new expectations; keep the older hand-checked rows.
    offset = 0
    for name, values in wanted.items():
        if name in NORMALIZED_SOURCES:
            wanted[name] = expected[offset : offset + len(values)]
        offset += len(values)
    assert expected == [value for values in wanted.values() for value in values], expected
    vm = args.work / "dataset-vm.js"
    vm.write_text(source)
    actual = run([args.reference, vm]).stdout
    assert actual == "".join(
        f'{name}="{quote(value)}"\n' for name, value in sorted(zip(names, expected))
    ), actual
    return wanted


def client(symbol, owned, name):
    setup = (
        f"{symbol}_session session; auto & doc = session.document();"
        if owned
        else "atom_table atoms; document doc{atoms};"
    )
    call = "session.invoke(element)" if owned else f"{symbol}(element)"
    fixtures = ",".join(
        "{"
        + ",".join(
            "{"
            + json.dumps(attr, ensure_ascii=False)
            + ","
            + json.dumps(value, ensure_ascii=False)
            + "}"
            for attr, value in zip(attrs, fixture_values(name, keys, overrides))
        )
        + "}"
        for attrs, keys, _, *overrides in CASES[name]
    )
    number = name in (
        "dataset_filter_length",
        "dataset_loop",
        "dataset_guarded_count",
        "dataset_truthy_count",
        "dataset_guard_alias_count",
    )
    observed_call = f"{call}.value()" if number or name in LOOP_SOURCES else call
    saved_type = (
        "double"
        if number
        else "std::string" if name in LOOP_SOURCES else "std::vector<std::string>"
    )
    observe = (
        "std::cout << saved << '~' << updated;"
        if name in VALUE_SOURCES
        else (
            "std::cout << saved;"
            if number or name in LOOP_SOURCES
            else """
        for (std::size_t i = 0; i < saved.size(); ++i) {
            if (i) std::cout << '|';
            std::cout << saved[i];
        }
        """
        )
    )
    repeat = (
        f"""
            if (doc.read().has_attribute(node, doc.atoms().intern("data-bs-z"))) {{
                assert(doc.set_attribute_ns(node, {{}}, doc.atoms().intern("data-bs-z"), "second"));
            }}
            updated = {observed_call};
        """
        if name in VALUE_SOURCES
        else ""
    )
    mutations = (
        """
            assert(doc.read().attribute_value(node, doc.atoms().intern("data-bs-later")) == "x");
            assert(!doc.read().has_attribute(node, doc.atoms().intern("data-bs-z")));
        """
        if name == "dataset_loop_snapshot"
        else ""
    )
    return f"""
    for (const auto & attributes : std::vector<std::vector<std::pair<const char *, const char *>>>{{{fixtures}}}) {{
    for (node_ns ns : {{node_ns::html, node_ns::svg}}) {{
        {saved_type} saved{{}};
        {'std::string updated;' if name in VALUE_SOURCES else ''}
        {{
            {setup}
            const auto node = doc.create_element(doc.atoms().intern("test"), ns);
            element_ref element{{&doc, node}};
            for (const auto & [name, value] : attributes) {{
                assert(doc.set_attribute_ns(node, {{}}, doc.atoms().intern(name), value));
            }}
            assert(doc.set_attribute_ns(node, doc.atoms().intern("urn:ignored"), doc.atoms().intern("data-hidden"), "x"));
            assert(doc.set_attribute_ns(node, {{}}, doc.atoms().intern("data-Upper"), "x"));
            saved = {observed_call};
            {repeat}
            {mutations}
            assert(doc.set_attribute(node, doc.atoms().intern("data-after"), "x"));
            const auto other = doc.create_element(doc.atoms().intern("test"), node_ns::other);
            element = {{&doc, other}};
            const auto version = doc.version();
            bool rejected = false;
            try {{ (void){call}; }} catch (const std::invalid_argument &) {{ rejected = true; }}
            assert(rejected && doc.version() == version);
            element = {{}};
            rejected = false;
            try {{ (void){call}; }} catch (const std::exception &) {{ rejected = true; }}
            assert(rejected && doc.version() == version);
        }}
        {observe}
        std::cout << '\\n';
    }}
    }}
"""


def main():
    parser = argparse.ArgumentParser()
    for name in ("translate", "opt", "clang", "node", "reference", "build", "include", "work"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--nm", default=shutil.which("nm"))
    args = parser.parse_args()
    args.work = args.work.resolve()
    args.work.mkdir(parents=True, exist_ok=True)
    vendor = Path(__file__).resolve().parents[4] / "ctbrowser/vendor/bootstrap/bootstrap.bundle.js"
    assert BOOTSTRAP_FILTER in vendor.read_text(), "Bootstrap dataset source pin changed"
    assert PREFIX in vendor.read_text(), "Bootstrap prefix source pin changed"
    assert "t.dataset[n]" in vendor.read_text(), "Bootstrap live-value source pin changed"
    assert BOOTSTRAP_GUARD in vendor.read_text(), "Bootstrap element-guard source pin changed"
    assert BOOTSTRAP_M in vendor.read_text(), "Bootstrap M source pin changed"
    expected = oracles(args)
    compilers = find_compilers()
    compilers[1] = args.clang
    includes, libraries = dom.link_options(args)
    prepared = [
        (name, *dom.prepare(args, name, source, 1, entry_name=name))
        for name, source in SOURCES.items()
    ]
    for optimize in (False, True):
        for layout in ("explicit", "deduced"):
            headers, bodies, clients, wanted = set(), [], [], []
            for provider in ("ctbrowser-dom-v1", "ctbrowser-dom-session-v1"):
                owned = "session" in provider
                for name, ir, contract in prepared:
                    label = f"{name}-{owned}-{optimize}-{layout}"
                    native = dom.lower(
                        args,
                        ir,
                        dict(
                            contract,
                            provider=provider,
                            initial_intrinsics=(
                                ITERATION_INTRINSICS + ["Number", "JSON", "decodeURIComponent"]
                                if name in NORMALIZED_SOURCES
                                else (
                                    PREFIX_INTRINSICS
                                    if name in PREFIX_SOURCES
                                    else (
                                        ITERATION_INTRINSICS
                                        if name in LOOP_SOURCES
                                        else (
                                            ["Object", "Array", "String"]
                                            if name not in BODIES
                                            else ["Object"]
                                        )
                                    )
                                )
                            ),
                            dataset_parameters=[0],
                        ),
                        label,
                        optimize=optimize,
                    )
                    if layout == "deduced":
                        deduced = args.work / f"{label}.deduced.mlir"
                        run([args.opt, native, "--ctnative-print-deduced", "-o", deduced])
                        native = deduced
                    entries = dom.NATIVE.findall(native.read_text())
                    assert len(entries) == (
                        1
                        if name in BODIES
                        or name in ("dataset_loop_prefix_all", "dataset_values_all")
                        else 2
                    ) and not dom.FUNCTION.search(native.read_text()), native.read_text()
                    entry = next(
                        symbol
                        for symbol in entries
                        if re.fullmatch(re.escape(name) + r"_\d+", symbol)
                    )
                    cpp = run([args.translate, "--mlir-to-cpp", native]).stdout
                    assert "ctnative::dataset_keys" in cpp and not dom.VM.search(cpp), cpp
                    assert "__ctbrowser_" not in cpp, cpp
                    if name in PREFIX_SOURCES:
                        assert ".startsWith(" in cpp and ".substr(" in cpp, cpp
                        assert not re.search(r"regex|RegExp", cpp), cpp
                    if name in VALUE_SOURCES and name != "dataset_normalized_attribute":
                        assert "ctnative::dataset_value(" in cpp and ".at(" in cpp, cpp
                    if name in NORMALIZED_SOURCES:
                        assert all(
                            token in cpp
                            for token in (
                                "ctbrowser::parse_json",
                                "ctbrowser::decode_uri_component",
                                "ctbrowser::json_value",
                            )
                        ), cpp
                    if name == "dataset_normalized_attribute":
                        assert "ctnative::get_attribute(" in cpp, cpp
                    assert not re.search(
                        r"shared_ptr|weak_ptr|nullable_scalar|std::variant|std::function", cpp
                    ), cpp
                    headers.update(re.findall(r"^#(?:include|define CTNATIVE_)[^\n]*", cpp, re.M))
                    body = re.sub(r"^#(?:include|define CTNATIVE_)[^\n]*\n?", "", cpp, flags=re.M)
                    namespace = name + ("_session" if owned else "_free")
                    bodies.append(f"namespace {namespace} {{\n{body}\n}}\n")
                    clients.append(client(namespace + "::" + entry, owned, name))
                    wanted += [value for value in expected[name] for _ in range(2)]
            path = args.work / f"combined-{optimize}-{layout}.cpp"
            path.write_text(
                "\n".join(sorted(headers))
                + "\n#include <cassert>\n#include <iostream>\n"
                + "".join(bodies)
                + "\nusing namespace ctbrowser;\nint main() {\n"
                + "".join(clients)
                + "}\n"
            )
            for index, compiler in enumerate(compilers):
                binary = path.with_suffix(f".{index}")
                run([compiler, *FLAGS, *includes, path, *libraries, "-o", binary])
                assert not dom.VM.search(run([args.nm, "-C", binary]).stdout)
                assert run([binary]).stdout.splitlines() == wanted
            if optimize and layout == "explicit":
                binary = path.with_suffix(".sanitized")
                run(
                    [
                        compilers[1],
                        *FLAGS,
                        "-O1",
                        "-g",
                        "-fsanitize=address,undefined",
                        "-fsanitize-address-use-after-scope",
                        "-fno-omit-frame-pointer",
                        *includes,
                        path,
                        *libraries,
                        "-o",
                        binary,
                    ]
                )
                result = run(
                    [binary],
                    environment=dict(
                        os.environ,
                        ASAN_OPTIONS="detect_stack_use_after_return=1:detect_leaks=1",
                        UBSAN_OPTIONS="halt_on_error=1",
                    ),
                )
                assert result.stdout.splitlines() == wanted and not result.stderr, result.stderr
    refusals = 0
    refused_sources = [
        (name, f"function {name}(element) {{ {body} }}", 1) for name, body in REFUSALS.items()
    ]
    refused_sources.append(
        (
            "value_other_element",
            "function value_other_element(element, other) { "
            + VALUE_KEYS
            + VALUE_LOOP.replace("element.dataset[n]", "other.dataset[n]")
            + " }",
            2,
        )
    )
    for name, source, parameters in refused_sources:
        ir, contract = dom.prepare(args, name, source, parameters, entry_name=name)
        for provider in ("ctbrowser-dom-v1", "ctbrowser-dom-session-v1"):
            for optimize in (False, True):
                dom.lower(
                    args,
                    ir,
                    dict(
                        contract,
                        provider=provider,
                        initial_intrinsics=PREFIX_INTRINSICS,
                        dataset_parameters=list(range(parameters)),
                    ),
                    f"refuse-{name}-{provider}-{optimize}",
                    optimize=optimize,
                    success=False,
                )
                refusals += 1
    _, ir, contract = next(item for item in prepared if item[0] == "dataset_guarded_values")
    for provider in ("ctbrowser-dom-v1", "ctbrowser-dom-session-v1"):
        for optimize in (False, True):
            for changes in ({"element_parameters": []}, {"dataset_parameters": []}):
                dom.lower(
                    args,
                    ir,
                    dict(
                        contract,
                        provider=provider,
                        initial_intrinsics=ITERATION_INTRINSICS,
                        dataset_parameters=[0],
                    )
                    | changes,
                    f"guard-premise-{refusals}",
                    optimize=optimize,
                    success=False,
                )
                refusals += 1
            for budget in (0, 64, 256):
                dom.lower(
                    args,
                    ir,
                    dict(
                        contract,
                        provider=provider,
                        initial_intrinsics=ITERATION_INTRINSICS,
                        dataset_parameters=[0],
                    ),
                    f"guard-budget-{refusals}",
                    optimize=optimize,
                    max_steps=budget,
                    success=False,
                )
                refusals += 1
    _, ir, contract = next(item for item in prepared if item[0] == "dataset_values")
    for provider in ("ctbrowser-dom-v1", "ctbrowser-dom-session-v1"):
        for optimize in (False, True):
            for missing in ITERATION_INTRINSICS:
                dom.lower(
                    args,
                    ir,
                    dict(
                        contract,
                        provider=provider,
                        dataset_parameters=[0],
                        initial_intrinsics=[
                            name for name in ITERATION_INTRINSICS if name != missing
                        ],
                    ),
                    f"value-premise-{refusals}",
                    optimize=optimize,
                    success=False,
                )
                refusals += 1
            for budget in (0, 64, 256):
                dom.lower(
                    args,
                    ir,
                    dict(
                        contract,
                        provider=provider,
                        dataset_parameters=[0],
                        initial_intrinsics=ITERATION_INTRINSICS,
                    ),
                    f"value-budget-{refusals}",
                    optimize=optimize,
                    max_steps=budget,
                    success=False,
                )
                refusals += 1
    _, ir, contract = prepared[0]
    for changes in (
        {},
        {"initial_intrinsics": ["Object"]},
        {"dataset_parameters": [0]},
        {"initial_intrinsics": ["Object", "Object"], "dataset_parameters": [0]},
        {"initial_intrinsics": ["Object"], "dataset_parameters": [1]},
        {"initial_intrinsics": ["Object"], "dataset_parameters": [0, 0]},
    ):
        dom.lower(args, ir, dict(contract, **changes), f"premise-{refusals}", success=False)
        refusals += 1
    _, ir, contract = next(item for item in prepared if item[0] == "dataset_filter")
    for provider in ("ctbrowser-dom-v1", "ctbrowser-dom-session-v1"):
        for optimize in (False, True):
            for intrinsics in (
                ["Object"],
                ["Object", "Array"],
                ["Object", "String"],
                ["Array", "String"],
            ):
                dom.lower(
                    args,
                    ir,
                    dict(
                        contract,
                        provider=provider,
                        initial_intrinsics=intrinsics,
                        dataset_parameters=[0],
                    ),
                    f"filter-premise-{refusals}",
                    optimize=optimize,
                    success=False,
                )
                refusals += 1
    _, ir, contract = next(item for item in prepared if item[0] == "dataset_loop")
    for budget in (0, 64, 256):
        for optimize in (False, True):
            dom.lower(
                args,
                ir,
                dict(contract, initial_intrinsics=ITERATION_INTRINSICS, dataset_parameters=[0]),
                f"loop-budget-{refusals}",
                optimize=optimize,
                max_steps=budget,
                success=False,
            )
            refusals += 1
    for missing in ITERATION_INTRINSICS:
        for provider in ("ctbrowser-dom-v1", "ctbrowser-dom-session-v1"):
            for optimize in (False, True):
                dom.lower(
                    args,
                    ir,
                    dict(
                        contract,
                        provider=provider,
                        initial_intrinsics=[
                            name for name in ITERATION_INTRINSICS if name != missing
                        ],
                        dataset_parameters=[0],
                    ),
                    f"loop-premise-{refusals}",
                    optimize=optimize,
                    success=False,
                )
                refusals += 1
    _, ir, contract = next(item for item in prepared if item[0] == "dataset_loop_prefix")
    for missing in ("String", "RegExp", "__ctbrowser_regexp"):
        for provider in ("ctbrowser-dom-v1", "ctbrowser-dom-session-v1"):
            for optimize in (False, True):
                dom.lower(
                    args,
                    ir,
                    dict(
                        contract,
                        provider=provider,
                        dataset_parameters=[0],
                        initial_intrinsics=[name for name in PREFIX_INTRINSICS if name != missing],
                    ),
                    f"prefix-premise-{refusals}",
                    optimize=optimize,
                    success=False,
                )
                refusals += 1
    print(
        f"native DOM dataset: {len(SOURCES)} sources, {sum(map(len, expected.values()))} Node/VM source-double observations, 8 GCC/Clang binaries, HTML/SVG and lifetime sanitizer, {refusals} refusals"
    )


if __name__ == "__main__":
    main()
