#!/usr/bin/env python3
"""Proved HTML/SVG dataset key snapshots, with no script dependency in C++."""

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
SOURCES = {name: f"function {name}(element) {{ {body} }}\n" for name, body in BODIES.items()}
REFUSALS = {
    "missing_read": "return element.dataset.missing;",
    "dataset_escape": "return element.dataset;",
    "dataset_write": "element.dataset.bsZ = 'x'; return Object.keys(element.dataset);",
    "keys_mutation": "const keys = Object.keys(element.dataset); keys[0] = 'x'; return keys;",
    "keys_identity": "const keys = Object.keys(element.dataset); return keys === keys;",
    "keys_filter": "return Object.keys(element.dataset).filter(key => key.startsWith('bs'));",
    "stale_alias": "const data = element.dataset; element.setAttribute('data-later', 'x'); return Object.keys(data);",
    "replaced_keys": "Object.keys = element; return Object.keys(element.dataset);",
    "wrong_receiver": "const keys = Object.keys; return keys(element.dataset);",
    "extra_argument": "return Object.keys(element.dataset, 1);",
    "ordinary_object": "return Object.keys({a: 1});",
}


def oracles(args):
    source = "".join(SOURCES.values())
    names = []
    for name in SOURCES:
        label = f"datasetObservation{len(names)}"
        names.append(label)
        source += f"""
var {label} = (function() {{
    const keys = {json.dumps(KEYS)};
    const target = {{}};
    for (const key of keys) Object.defineProperty(target, key, {{value: 'x', enumerable: true, configurable: true}});
    const dataset = new Proxy(target, {{ownKeys() {{ return keys.slice(); }} }});
    const element = {{dataset,
        setAttribute(key, value) {{
            if (key !== 'data-later' || value !== 'x') throw new Error('unexpected write');
            keys.push('later'); Object.defineProperty(target, 'later', {{value, enumerable: true, configurable: true}});
        }},
        removeAttribute(key) {{
            if (key !== 'data-bs-z') throw new Error('unexpected removal');
            keys.splice(keys.indexOf('bsZ'), 1); delete target.bsZ;
        }}
    }};
    return {name}(element).join('|');
}})();
"""
    node = args.work / "dataset-node.js"
    node.write_text(source + "".join(f"console.log({name});\n" for name in names))
    expected = run([args.node, node]).stdout.splitlines()
    assert expected == ["|".join(KEYS)] * 3 + [
        "|".join(KEYS + ["later"]),
        "|".join(KEYS[1:] + ["later"]),
    ]
    vm = args.work / "dataset-vm.js"
    vm.write_text(source)
    actual = run([args.reference, vm]).stdout
    assert actual == "".join(
        f'{name}="{quote(value)}"\n' for name, value in zip(names, expected)
    ), actual
    return expected


def client(symbol, owned):
    setup = (
        f"{symbol}_session session; auto & doc = session.document();"
        if owned
        else "atom_table atoms; document doc{atoms};"
    )
    call = "session.invoke(element)" if owned else f"{symbol}(element)"
    attributes = ",".join(json.dumps(key) for key in ATTRS)
    return f"""
    for (node_ns ns : {{node_ns::html, node_ns::svg}}) {{
        std::vector<std::string> saved;
        {{
            {setup}
            const auto node = doc.create_element(doc.atoms().intern("test"), ns);
            element_ref element{{&doc, node}};
            for (const char * name : {{{attributes}}}) {{
                assert(doc.set_attribute_ns(node, {{}}, doc.atoms().intern(name), "x"));
            }}
            assert(doc.set_attribute_ns(node, doc.atoms().intern("urn:ignored"), doc.atoms().intern("data-hidden"), "x"));
            assert(doc.set_attribute_ns(node, {{}}, doc.atoms().intern("data-Upper"), "x"));
            saved = {call};
            assert(doc.set_attribute(node, doc.atoms().intern("data-after"), "x"));
            const auto other = doc.create_element(doc.atoms().intern("test"), node_ns::other);
            element = {{&doc, other}};
            const auto version = doc.version();
            bool rejected = false;
            try {{ (void){call}; }} catch (const std::invalid_argument &) {{ rejected = true; }}
            assert(rejected && doc.version() == version);
        }}
        for (std::size_t i = 0; i < saved.size(); ++i) {{
            if (i) std::cout << '|';
            std::cout << saved[i];
        }}
        std::cout << '\\n';
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
    assert "Object.keys(t.dataset)" in vendor.read_text(), "Bootstrap dataset source pin changed"
    expected = oracles(args)
    compilers = find_compilers()
    compilers[1] = args.clang
    includes, libraries = dom.link_options(args)
    prepared = [(name, *dom.prepare(args, name, source, 1)) for name, source in SOURCES.items()]
    for optimize in (False, True):
        for layout in ("explicit", "deduced"):
            headers, bodies, clients, wanted = set(), [], [], []
            for provider in ("ctbrowser-dom-v1", "ctbrowser-dom-session-v1"):
                owned = "session" in provider
                for index, (name, ir, contract) in enumerate(prepared):
                    label = f"{name}-{owned}-{optimize}-{layout}"
                    native = dom.lower(
                        args,
                        ir,
                        dict(
                            contract,
                            provider=provider,
                            initial_intrinsics=["Object"],
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
                    assert len(entries) == 1 and not dom.FUNCTION.search(
                        native.read_text()
                    ), native.read_text()
                    cpp = run([args.translate, "--mlir-to-cpp", native]).stdout
                    assert "ctnative::dataset_keys" in cpp and not dom.VM.search(cpp), cpp
                    assert not re.search(
                        r"shared_ptr|weak_ptr|nullable_scalar|std::variant", cpp
                    ), cpp
                    headers.update(re.findall(r"^#(?:include|define CTNATIVE_)[^\n]*", cpp, re.M))
                    body = re.sub(r"^#(?:include|define CTNATIVE_)[^\n]*\n?", "", cpp, flags=re.M)
                    namespace = name + ("_session" if owned else "_free")
                    bodies.append(f"namespace {namespace} {{\n{body}\n}}\n")
                    clients.append(client(namespace + "::" + entries[0], owned))
                    wanted += [expected[index]] * 2
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
                        initial_intrinsics=["Object"],
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
    print(
        f"native DOM dataset: {len(SOURCES)} sources, {len(expected)} Node/VM source-double observations, 8 GCC/Clang binaries, HTML/SVG and lifetime sanitizer, {refusals} refusals"
    )


if __name__ == "__main__":
    main()
