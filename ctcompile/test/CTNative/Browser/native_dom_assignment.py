#!/usr/bin/env python3
"""Fresh DOM result assignments preserve order and own original M's values."""

import argparse
import json
import os
from pathlib import Path
import re
import shutil

from CTNative.Browser import native_dom as dom
from CTNative.Browser import native_dom_json as values
from CTNative.Browser import native_dom_numbers as numbers
from CTNative.Browser.native_dom_dataset import BOOTSTRAP_FILTER, ITERATION_INTRINSICS
from CTNative.harness import find_compilers, run
from Target.Cpp.harness import FLAGS

GET = "M(t.getAttribute('data-bs-config'))"
LATER = "M(t.getAttribute('data-bs-later'))"
BODIES = {
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
    # Preserve the former refusal body and its original dataset observations.
    "loop_dynamic_key": "const e = {}; "
    f"for (const n of {BOOTSTRAP_FILTER}) {{ e[n] = M(t.dataset[n]); }} return e;",
    "loop_dynamic_all": "const e = {}; "
    "for (const n of Object.keys(t.dataset)) { e[n] = M(t.dataset[n]); } return e;",
}
SOURCES = {
    name: f"function {name}(t) {{ {values.BOOTSTRAP_M if 'M(' in body else ''} {body} }}\n"
    for name, body in BODIES.items()
}
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
FIXTURES = {
    name: [
        ([] if value is None else [("data-bs-config", value)])
        + [("data-bs-later", values.LATER_INPUT)]
        for value in (values.SPREAD_INPUTS if name == "assign_spread" else INPUTS)
    ]
    for name in SOURCES
}
FIXTURES["assign_order"] = FIXTURES["assign_order"][:2]
# Every normalized value is the last overwrite, with zero-trip controls first.
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
    "member_read": f"const e = {{}}; e.saved = {GET}; return e.saved;",
    "target_identity": f"const e = {{}}; e.saved = {GET}; return e === e;",
    "target_capture": f"const e = {{}}; e.saved = {GET}; return () => e;",
    "target_escape": f"const e = {{}}; e.saved = {GET}; t.saved = e; return e;",
    "source_alias_mutation": f"const saved = {GET}; const e = {{}}; e.saved = saved; saved.x = 1; return e;",
    "copy_then_mutate": f"const e = {{}}; e.saved = {GET}; const copied = {{...e}}; e.saved = 1; return copied;",
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
}


def oracle_source(accessors):
    source = "".join(SOURCES.values()) + values.OBSERVE
    labels = []
    descriptor = (
        "get() { trace.push('dataset:' + key); return pair[1]; }" if accessors else "value: pair[1]"
    )
    for name, fixtures in FIXTURES.items():
        for attrs in fixtures:
            label = f"assignmentObservation{len(labels):03}"
            labels.append(label)
            if name in ("assign_loop", "loop_dynamic_key", "loop_dynamic_all"):
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
    if (trace.join('|') !== {json.dumps('|'.join(trace))}) throw new Error('assignment source order: ' + trace.join('|'));
    return observe(result);
}})();
"""
    return source, labels


def oracles(args):
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
    # The VM's Object.keys currently invokes enumerable accessors. Keep the
    # full getter trace in Node above; compare unchanged entry sources and
    # values with plain data properties in both oracles. Dataset getter counts
    # and source getAttribute/hasAttribute ordering remain checked in both.
    vm = args.work / "assignment-vm.js"
    vm.write_text(source)
    actual = run([args.reference, vm]).stdout
    wanted = "".join(f'{label}="{values.quote(value)}"\n' for label, value in zip(labels, expected))
    assert actual == wanted, (actual, wanted)
    return expected


def client(name, symbol, owned):
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
            + numbers.cpp_string(key)
            + f", {len(key.encode())}"
            + "},std::string{"
            + numbers.cpp_string(value)
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
    actual = output.splitlines()
    assert len(actual) == len(expected), (len(actual), len(expected))
    for index, (value, wanted) in enumerate(zip(actual, expected)):
        assert value.startswith("object:") and wanted.startswith("object:"), (value, wanted)
        # Keep the constructed target's raw key order (and duplicate keys)
        # observable before adapting the public parsed-tree representation.
        keys = [key for key, _ in json.loads(value[7:], object_pairs_hook=list)]
        wanted_keys = [key for key, _ in json.loads(wanted[7:], object_pairs_hook=list)]
        assert keys == wanted_keys, (index, keys, wanted_keys)
    # Core JSON deliberately retains parse order, including nested numeric
    # names. Observe it with JS enumeration, preserving ordinary key order
    # and every value/type; source-object insertion order was checked above.
    observed = run(
        [
            args.node,
            "-e",
            "for (const line of require('fs').readFileSync(0, 'utf8').trimEnd().split('\\n')) "
            "console.log('object:' + JSON.stringify(JSON.parse(line.slice(7))));",
        ],
        input_text=output,
    ).stdout.splitlines()
    assert observed == expected, [
        (i, a, e) for i, (a, e) in enumerate(zip(observed, expected)) if a != e
    ]


def main():
    parser = argparse.ArgumentParser()
    for name in ("translate", "opt", "clang", "node", "reference", "build", "include", "work"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--nm", default=shutil.which("nm"))
    args = parser.parse_args()
    args.work = args.work.resolve()
    args.work.mkdir(parents=True, exist_ok=True)
    vendor = Path(__file__).resolve().parents[4] / "ctbrowser/vendor/bootstrap/bootstrap.bundle.js"
    assert values.BOOTSTRAP_M in vendor.read_text(), "Bootstrap M source pin changed"
    assert BOOTSTRAP_FILTER in vendor.read_text(), "Bootstrap filter source pin changed"
    expected = oracles(args)
    compilers = find_compilers()
    compilers[1] = args.clang
    includes, libraries = dom.link_options(args)
    intrinsics = ITERATION_INTRINSICS + ["Number", "JSON", "decodeURIComponent"]
    prepared = [
        (name, *dom.prepare(args, name, source, 1, entry_name=name))
        for name, source in SOURCES.items()
    ]
    for optimize in (False, True):
        modules = []
        for provider in ("ctbrowser-dom-v1", "ctbrowser-dom-session-v1"):
            owned = "session" in provider
            for name, ir, contract in prepared:
                label = f"{name}-{owned}-{optimize}"
                native = dom.lower(
                    args,
                    ir,
                    dict(
                        contract,
                        provider=provider,
                        initial_intrinsics=intrinsics,
                        dataset_parameters=[0],
                    ),
                    label,
                    optimize=optimize,
                )
                deduced = args.work / f"{label}.deduced.mlir"
                run([args.opt, native, "--ctnative-print-deduced", "-o", deduced])
                modules.append((name, owned, {"explicit": native, "deduced": deduced}))
        for layout in ("explicit", "deduced"):
            headers, bodies, clients = set(), [], []
            for name, owned, layouts in modules:
                module = layouts[layout]
                entries = dom.NATIVE.findall(module.read_text())
                assert len(entries) == (
                    2 if name in ("assign_loop", "loop_dynamic_key") else 1
                ) and not dom.FUNCTION.search(module.read_text()), module.read_text()
                symbol = next(
                    entry for entry in entries if re.fullmatch(re.escape(name) + r"_\d+", entry)
                )
                cpp = run([args.translate, "--mlir-to-cpp", module]).stdout
                assert "ctbrowser::json_value" in cpp and not dom.VM.search(cpp), cpp
                assert not re.search(
                    r"shared_ptr|weak_ptr|std::variant|nullable_scalar|std::function|__ctbrowser_",
                    cpp,
                ), cpp
                if name != "assign_optional":
                    assert (
                        "ctbrowser::parse_json" in cpp and "ctbrowser::decode_uri_component" in cpp
                    ), cpp
                if name in ("loop_dynamic_key", "loop_dynamic_all"):
                    assert "ctnative::dataset_value(" in cpp, cpp
                    assert "ctnative::assign_json_snapshot_property(" in cpp, cpp
                    assert "__proto__" not in cpp, cpp
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
                binary = path.with_suffix(f".{index}")
                run([compiler, *FLAGS, *includes, path, *libraries, "-o", binary])
                assert not dom.VM.search(run([args.nm, "-C", binary]).stdout)
                check_output(args, run([binary]).stdout, expected * 2)
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
                assert not result.stderr, result.stderr
                check_output(args, result.stdout, expected * 2)
    refusals = 0
    for name, body in REFUSALS.items():
        ir, contract = dom.prepare(
            args,
            name,
            f"function {name}(t) {{ {values.BOOTSTRAP_M if 'M(' in body else ''} {body} }}",
            1,
            entry_name=name,
        )
        for provider in ("ctbrowser-dom-v1", "ctbrowser-dom-session-v1"):
            for optimize in (False, True):
                dom.lower(
                    args,
                    ir,
                    dict(
                        contract,
                        provider=provider,
                        initial_intrinsics=intrinsics,
                        dataset_parameters=[0],
                    ),
                    f"refuse-{name}-{provider}-{optimize}",
                    optimize=optimize,
                    success=False,
                )
                refusals += 1
    for name in ("assign_loop", "loop_dynamic_key"):
        _, ir, contract = next(row for row in prepared if row[0] == name)
        for provider in ("ctbrowser-dom-v1", "ctbrowser-dom-session-v1"):
            for optimize in (False, True):
                for budget in (0, 64, 256):
                    dom.lower(
                        args,
                        ir,
                        dict(
                            contract,
                            provider=provider,
                            initial_intrinsics=intrinsics,
                            dataset_parameters=[0],
                        ),
                        f"budget-{refusals}",
                        optimize=optimize,
                        max_steps=budget,
                        success=False,
                    )
                    refusals += 1
    print(
        f"native DOM assignments: {len(SOURCES)} sources, {len(expected)} Node/VM source-double observations plus Node accessor traces, 8 GCC/Clang binaries, lifetime sanitizer, both providers/policies/layouts, {refusals} refusals"
    )


if __name__ == "__main__":
    main()
