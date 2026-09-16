#!/usr/bin/env python3
"""Proved HTML/SVG dataset key snapshots, Bootstrap filtering and iteration."""

import argparse
import json
import os
from pathlib import Path
import re
import shutil

from CTNative.Browser import native_dom as dom
from CTNative.Browser.native_dom_json import quote
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
LOOP_SOURCES = {
    "dataset_loop": f"function dataset_loop(t) {{ {LOOP_COUNT} }}\n",
    "dataset_loop_order": f"function dataset_loop_order(t) {{ let joined = ''; "
    f"for (const n of {BOOTSTRAP_FILTER}) {{ joined = joined + n + '|'; }} return joined; }}\n",
    "dataset_loop_snapshot": f"function dataset_loop_snapshot(t) {{ const selected = {BOOTSTRAP_FILTER}; "
    "t.setAttribute('data-bs-later', 'x'); t.removeAttribute('data-bs-z'); let joined = ''; "
    "for (const n of selected) { joined = joined + n + '|'; } return joined; }\n",
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
}


def oracles(args):
    source = "".join(SOURCES.values())
    names, wanted = [], {}
    for name in SOURCES:
        wanted[name] = []
        for _, keys, selected in CASES[name]:
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
            number = name in ("dataset_filter_length", "dataset_loop")
            if number:
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
            source += f"""
var {label} = (function() {{
    const keys = {json.dumps(keys)};
    const target = {{}};
    for (const key of keys) Object.defineProperty(target, key, {{value: 'x', enumerable: true, configurable: true}});
    const dataset = new Proxy(target, {{ownKeys() {{ return keys.slice(); }} }});
    const element = {{dataset,
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
    return {observation};
}})();
"""
    node = args.work / "dataset-node.js"
    node.write_text(source + "".join(f"console.log({name});\n" for name in names))
    expected = run([args.node, node]).stdout.splitlines()
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
        "{" + ",".join(json.dumps(key) for key in attrs) + "}" for attrs, _, _ in CASES[name]
    )
    number = name in ("dataset_filter_length", "dataset_loop")
    saved_type = (
        "double"
        if number
        else "std::string" if name in LOOP_SOURCES else "std::vector<std::string>"
    )
    observe = (
        "std::cout << saved;"
        if number or name in LOOP_SOURCES
        else """
        for (std::size_t i = 0; i < saved.size(); ++i) {
            if (i) std::cout << '|';
            std::cout << saved[i];
        }
        """
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
    for (const auto & names : std::vector<std::vector<const char *>>{{{fixtures}}}) {{
    for (node_ns ns : {{node_ns::html, node_ns::svg}}) {{
        {saved_type} saved{{}};
        {{
            {setup}
            const auto node = doc.create_element(doc.atoms().intern("test"), ns);
            element_ref element{{&doc, node}};
            for (const char * name : names) {{
                assert(doc.set_attribute_ns(node, {{}}, doc.atoms().intern(name), "x"));
            }}
            assert(doc.set_attribute_ns(node, doc.atoms().intern("urn:ignored"), doc.atoms().intern("data-hidden"), "x"));
            assert(doc.set_attribute_ns(node, {{}}, doc.atoms().intern("data-Upper"), "x"));
            saved = {call};
            {mutations}
            assert(doc.set_attribute(node, doc.atoms().intern("data-after"), "x"));
            const auto other = doc.create_element(doc.atoms().intern("test"), node_ns::other);
            element = {{&doc, other}};
            const auto version = doc.version();
            bool rejected = false;
            try {{ (void){call}; }} catch (const std::invalid_argument &) {{ rejected = true; }}
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
                                ITERATION_INTRINSICS
                                if name in LOOP_SOURCES
                                else (
                                    ["Object", "Array", "String"]
                                    if name not in BODIES
                                    else ["Object"]
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
                    assert len(entries) == (1 if name in BODIES else 2) and not dom.FUNCTION.search(
                        native.read_text()
                    ), native.read_text()
                    entry = next(
                        symbol
                        for symbol in entries
                        if re.fullmatch(re.escape(name) + r"_\d+", symbol)
                    )
                    cpp = run([args.translate, "--mlir-to-cpp", native]).stdout
                    assert "ctnative::dataset_keys" in cpp and not dom.VM.search(cpp), cpp
                    assert "__ctbrowser_" not in cpp, cpp
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
    for name, body in REFUSALS.items():
        ir, contract = dom.prepare(
            args, name, f"function {name}(element) {{ {body} }}", 1, entry_name=name
        )
        for provider in ("ctbrowser-dom-v1", "ctbrowser-dom-session-v1"):
            for optimize in (False, True):
                dom.lower(
                    args,
                    ir,
                    dict(
                        contract,
                        provider=provider,
                        initial_intrinsics=ITERATION_INTRINSICS,
                        dataset_parameters=[0],
                    ),
                    f"refuse-{name}-{provider}-{optimize}",
                    optimize=optimize,
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
    print(
        f"native DOM dataset: {len(SOURCES)} sources, {sum(map(len, expected.values()))} Node/VM source-double observations, 8 GCC/Clang binaries, HTML/SVG and lifetime sanitizer, {refusals} refusals"
    )


if __name__ == "__main__":
    main()
