#!/usr/bin/env python3
"""Publish proved scalar record fields and snapshots; retain the class/session boundary."""

import argparse
from pathlib import Path
import re
import shutil

from CTNative.Browser import native_dom as dom
from CTNative.Browser import native_dom_data_session as session
from CTNative.Exports import boundary
from CTNative.harness import find_compilers, run
from CTNative.HostContract import contract as host
from CTNative.Lowering.Objects import class_initialization_inputs as classes
from Target.Cpp.harness import FLAGS


def source(constructor=False):
    inputs = Path(__file__).resolve().parents[1] / "Lowering/Objects/Inputs/class-initialization"
    text = (inputs / "50.mlir").read_text()
    kind = "constructor" if constructor else "holder"
    original = text.split(f"//--- class-map-record-nested-vendor-{kind}.js\n", 1)[1].split(
        "//---", 1
    )[0]
    root = Path(__file__).resolve().parents[4]
    vendor = (root / "ctbrowser/vendor/bootstrap/bootstrap.bundle.js").read_text()
    data = (
        vendor[
            vendor.index("    const t = new Map,") : vendor.index('        i = "transitionend",')
        ]
        .rstrip()
        .removesuffix(",")
        + ";\n"
    )
    if data not in original:
        raise RuntimeError("complete vendor Data declaration changed")
    if constructor:
        original = (
            original.replace("constructor(n)", "constructor(n, key)")
            .replace('e.set("element", "bs.item", this)', 'e.set(key, "bs.item", this)')
            .replace("new Item(2)", "new Item(2, element)")
            .replace("new Item(7)", "new Item(7, element)")
        )
    adapted = (
        original.replace("function probe() {", "function probe(element) {", 1)
        .replace('"element"', "element")
        .replace("return savedFirst.n", "a = savedFirst.n", 1)
        .replace("var a = probe();", "")
    )
    if data not in adapted:
        raise RuntimeError("DOM input adapter changed the vendor declaration")
    return adapted


def published_source(constructor=False):
    prefix = session.source.SOURCE.split("  traceEntered = 1;", 1)[0]
    prefix = prefix.replace("dataEntry(element, other)", "probe(element)").replace(
        "return state.has(key);", "return state.get(key);"
    )
    body = source(constructor).replace("function probe(element) {\n", prefix, 1)
    body = body.replace("a = savedFirst.n", "const result = savedFirst.n", 1)
    return body.rstrip().removesuffix("}") + """
  traceEntered = 1;
  traceBefore = host.slot.get(element);
  host.slot.remove(element);
  host.slot.set(element, result);
  traceOther = host.slot.get(element);
  traceAfter = host.slot.get(element);
}
"""


def record_source():
    return (
        session.source.SOURCE.replace("dataEntry(element, other)", "probe(element)")
        .replace("return state.has(key);", "return state.get(key);")
        .replace("  traceEntered = 1;", "  const payload = {n: 15927};\n  traceEntered = 1;")
        .replace(
            "host.slot.set(element, 42);",
            "host.slot.remove(element);\n  host.slot.set(element, payload.n);",
        )
        .replace("traceOther = host.slot.get(other);", "traceOther = host.slot.get(element);")
        .replace("  host.slot.remove(other);\n", "")
    )


RECORD_CLIENT = r"""
#include <array>
#include <cassert>
#include <iostream>
#include <type_traits>

int main() {
    using namespace ctbrowser;
    using session_type = @OWNER@;
    static_assert(!std::is_copy_constructible_v<session_type>);
    static_assert(!std::is_copy_assignable_v<session_type>);
    static_assert(!std::is_move_constructible_v<session_type>);
    static_assert(!std::is_move_assignable_v<session_type>);
    for (int lifetime = 0; lifetime < 16; ++lifetime) {
        session_type session, second;
        auto & doc = session.document();
        auto & other = second.document();
        auto button = doc.create_element(doc.atoms().intern("button"));
        auto child = doc.create_element(doc.atoms().intern("span"));
        auto foreign_button = other.create_element(other.atoms().intern("button"));
        assert(button == foreign_button);
        assert(doc.append_child(doc.root(), button));
        assert(doc.append_child(button, child));
        const element_ref element{&doc, button}, alias = element;
        const element_ref distinct{&doc, child}, foreign{&other, foreign_button};
        auto snapshot = [&] {
            return std::array{session.observe_traceAfter(), session.observe_traceBefore(),
                              session.observe_traceEntered(), session.observe_traceOther()};
        };
        auto check = [&] {
            assert(ctnative::global_number(session.observe_traceEntered()).value() == 1);
            assert(session.observe_traceBefore().tag == ctnative::nullable_scalar::kind::undefined);
            assert(ctnative::global_number(session.observe_traceOther()).value() == 15927);
            assert(ctnative::global_number(session.observe_traceAfter()).value() == 15927);
        };
        const auto text = doc.create_text("text");
        auto reject = [&] {
            auto before = snapshot();
            for (element_ref rejected : {foreign, element_ref{}, element_ref{&doc, {}},
                     element_ref{&doc, text},
                     element_ref{&doc, {button.slot, button.generation + 2}}}) {
                bool caught = false;
                try { (void)session.invoke(rejected); }
                catch (const std::exception &) { caught = true; }
                assert(caught);
            }
            auto after = snapshot();
            for (unsigned i = 0; i < before.size(); ++i) {
                assert(before[i].tag == after[i].tag && before[i].value == after[i].value);
            }
        };
        assert(session.observe_traceEntered().tag == ctnative::nullable_scalar::kind::undefined);
        reject(); // Validate the entire input domain before source effects.
        for (element_ref input : {element, alias, distinct, element}) {
            (void)session.invoke(input);
            check(); // Fresh source Map, including the retained key on repeated input.
        }
        assert(second.observe_traceEntered().tag == ctnative::nullable_scalar::kind::undefined);
        (void)second.invoke(foreign);
        assert(ctnative::global_number(second.observe_traceAfter()).value() == 15927);
        check();
        reject();
        assert(doc.remove_child(button));
        assert(!doc.read().parent(button));
        (void)session.invoke(alias);
        check();
    } // Private tables are destroyed before their document.
    std::cout << "DOM record Data passed\n";
}
"""


def published_prepare(args, name, text):
    ir, contract = dom.prepare(args, name, text, 1, entry_name="probe")
    contract.update(
        roots=[{"binding": "host", "properties": ["slot"]}],
        provider="ctbrowser-dom-data-session-v1",
        observations=session.source.OBSERVATIONS,
        absent_bindings=[],
        undefined_bindings=[],
        initial_intrinsics=["Map", "__ctbrowser_class_defined"],
    )
    return ir, contract


def published_observation(args, name, text, expected=15927, *, snapshot=False, class_result=False):
    observed = args.work / f"{name}.oracle.js"
    observed.write_text(
        text
        + "\nvar first = {}, second = {}; probe(first);\n"
        + "var traceFirst = traceBefore === undefined; probe(first);\n"
        + "var traceReset = traceBefore === undefined; probe(second);\n"
    )
    wanted = (
        f"traceAfter={expected}\ntraceBefore=undefined\ntraceEntered=1\n"
        f"traceFirst=true\ntraceOther={expected}\ntraceReset=true\n"
    )
    if snapshot:
        wanted = f"saved={expected}\n" + wanted
    if class_result:
        wanted = f"classResult={expected}\n" + wanted
    node = session.source.NODE.replace(
        "typeof value !== 'number'", "typeof value !== 'number' && typeof value !== 'undefined'"
    ).replace(
        "key.startsWith('trace')",
        "key.startsWith('trace') || key === 'saved' || key === 'classResult'",
    )
    for command in ([args.node, "-e", node, str(observed)], [args.reference, str(observed)]):
        if run(command).stdout != wanted:
            raise RuntimeError(f"{name}: scalar publication/reset source observation changed")


def published_families(args):
    prepared_count = refusals = observed = 0
    original = (
        published_source()
        .replace("host.slot.set(element, result)", "host.slot.set(element, 15927)")
        .replace("  traceEntered = 1;", "  classResult = result;\n  traceEntered = 1;")
    )
    constructor = (
        published_source(True)
        .replace("host.slot.set(element, result)", "host.slot.set(element, 59112)")
        .replace("  traceEntered = 1;", "  classResult = result;\n  traceEntered = 1;")
    )
    for label, text, expected in (
        ("holder", original, 15927),
        ("constructor", constructor, 59112),
        (
            "alias",
            original.replace("  traceEntered = 1;", "  const alias = element; traceEntered = 1;")
            .replace("host.slot.get(element)", "host.slot.get(alias)")
            .replace("host.slot.remove(element)", "host.slot.remove(alias)"),
            15927,
        ),
    ):
        name = "class-family-" + label
        published_observation(args, name, text, expected, class_result=True)
        observed += 1
        ir, contract = published_prepare(args, name, text)
        contract["observations"] = ["classResult", *contract["observations"]]
        prepared = classes.prepare(args, name, ir, contract, success=True)
        prepared_count += 1
        body = prepared.read_text()
        if body.count("ctjs.construct") != 5 or len(dom.FUNCTION.findall(body)) != 8:
            raise RuntimeError(f"{name}: preparation lost class, child Map or public family owners")
        if 'name = "classResult"' not in body:
            raise RuntimeError(f"{name}: original class computation lost its observation")
        checked = dict(contract, module_sha256=host.fingerprint(args.opt, prepared))
        for optimize in (False, True):
            dom.lower(
                args, prepared, checked, f"{name}-{optimize}", optimize=optimize, success=False
            )
            refusals += 1
        if label == "holder":
            for suffix, request, options in (
                ("stale", dict(contract, module_sha256="0" * 64), ""),
                ("no-root", dict(contract, roots=[]), ""),
                ("no-input", dict(contract, element_parameters=[]), ""),
                ("no-map", dict(contract, initial_intrinsics=["__ctbrowser_class_defined"]), ""),
                ("budget", contract, "max-steps=0"),
                ("small-budget", contract, "max-steps=100"),
            ):
                classes.prepare(
                    args, name + "-" + suffix, ir, request, success=False, options=options
                )
                refusals += 1
    controls = {
        "property": original.replace("  traceEntered = 1;", "  element.n; traceEntered = 1;"),
        "coercion": original.replace("  traceEntered = 1;", "  element + ''; traceEntered = 1;"),
        "capture": original.replace(
            "  traceEntered = 1;", "  const unused = () => element; traceEntered = 1;"
        ),
        "wrapper-effect": original.replace(
            "host.slot = factory();", "unknown(); host.slot = factory();"
        ),
        "factory-effect": original.replace(
            "const state = new Map();", "unknown(); const state = new Map();"
        ),
        "uncalled-effect": original.replace(
            "set(key, value) {", "bad() { unknown(); }, set(key, value) {"
        ),
        "method-effect": original.replace(
            "return state.get(key);", "unknown(); return state.get(key);"
        ),
        "extracted": original.replace(
            "  traceEntered = 1;", "  const extracted = host.slot.get; traceEntered = 1;"
        ),
        "root-reassigned": original.replace(
            "  traceEntered = 1;", "  host = {}; traceEntered = 1;"
        ),
        "map-reassigned": original.replace(
            "  traceEntered = 1;", "  Map = function() {}; traceEntered = 1;"
        ),
        "early-root": original.replace("  host = {};", "  if (false) { host.n; }\n  host = {};"),
    }
    for label, text in controls.items():
        if text == original:
            raise RuntimeError(f"{label}: family refusal did not change its source")
        name = "class-family-refused-" + label
        ir, contract = published_prepare(args, name, text)
        contract["observations"] = ["classResult", *contract["observations"]]
        classes.prepare(args, name, ir, contract, success=False, diagnostic="class ")
        refusals += 1
    # The actual class-result payload still needs constructor scalar evidence.
    for constructor in (False, True):
        name = "class-family-result-" + str(constructor)
        text = published_source(constructor)
        published_observation(args, name, text, 59112 if constructor else 15927)
        observed += 1
        ir, contract = published_prepare(args, name, text)
        classes.prepare(args, name, ir, contract, success=False, diagnostic="class ")
        refusals += 1
        for optimize in (False, True):
            dom.lower(args, ir, contract, f"{name}-{optimize}", optimize=optimize, success=False)
            refusals += 1
    print(
        f"class public family: {observed} Node/VM observations, {prepared_count} preparations, "
        f"{refusals} refusals; native class ownership remains refused"
    )


def published_records(args):
    original = record_source()
    snapshot = original.replace(
        "traceEntered = 1;", "saved = payload.n; traceEntered = 1;"
    ).replace("host.slot.set(element, payload.n)", "host.slot.set(element, saved)")
    sources = {
        "field": original,
        "alias": original.replace(
            "const payload = {n: 15927};", "const payload = {n: 15927}; const alias = payload;"
        ).replace("host.slot.set(element, payload.n)", "host.slot.set(element, alias.n)"),
        "saved-alias": original.replace(
            "const payload = {n: 15927};",
            "const payload = {n: 15927}; const alias = payload;\n"
            "  const saved = alias.n; alias.n = 7;",
        ).replace("host.slot.set(element, payload.n)", "host.slot.set(element, saved)"),
        "arithmetic": original.replace("{n: 15927}", "{n: 15920}").replace(
            "host.slot.set(element, payload.n)", "host.slot.set(element, payload.n + 7)"
        ),
        "global-snapshot": snapshot,
        "global-snapshot-alias": snapshot.replace(
            "const payload = {n: 15927};", "const payload = {n: 15927}; const alias = payload;"
        ).replace("saved = payload.n;", "saved = alias.n; alias.n = 7;"),
        "global-snapshot-arithmetic": snapshot.replace("{n: 15927}", "{n: 15920}").replace(
            "saved = payload.n;", "saved = payload.n + 7;"
        ),
    }
    compilers = find_compilers()
    compilers[1] = args.clang
    includes, libraries = dom.link_options(args)
    executions = refusals = 0
    for label, text in sources.items():
        name = "published-record-" + label
        published_observation(args, name, text, snapshot=label.startswith("global-snapshot"))
        ir, contract = published_prepare(args, name, text)
        report, annotated, _ = host.analyze(args.opt, ir, contract, args.work / (name + "-proof"))
        if (
            not report["proved"]
            or report["outer_key_inputs"] != 1
            or report["outer_key_objects"] != 0
            or report["observation_stores"] != len(session.source.OBSERVATIONS)
            or len(report["slots"]) != 1
            or report["slots"][0]["proved_edges"] != 5
        ):
            raise RuntimeError(f"{name}: missing complete input/factory proof: {report}")
        if host.fingerprint(args.opt, annotated) != host.fingerprint(args.opt, ir):
            raise RuntimeError(f"{name}: scalar publication proof changed the source")
        for optimize in (False, True):
            native = dom.lower(args, ir, contract, f"{name}-{optimize}", optimize=optimize)
            if (
                dom.FUNCTION.search(native.read_text())
                or len(dom.NATIVE.findall(native.read_text())) != 6
            ):
                raise RuntimeError(f"{name}: incomplete source function family")
            deduced = args.work / f"{name}-{optimize}.deduced.mlir"
            run([args.opt, str(native), "--ctnative-print-deduced", "-o", str(deduced)])
            for mode, module in (("explicit", native), ("deduced", deduced)):
                cpp = run([args.translate, "--mlir-to-cpp", str(module)]).stdout
                owner = re.findall(r"class (\w+_session) \{", cpp)
                if (
                    len(owner) != 1
                    or dom.VM.search(cpp)
                    or re.search(
                        r"shared_ptr<ctnative::method_|shared_ptr<ctn_|invoke_callable|invoke_session|\bmain\s*\(",
                        cpp,
                    )
                ):
                    raise RuntimeError(f"{name}: escaping runtime/table/callable")
                if cpp.index("atom_table atoms_") > cpp.index("document document_"):
                    raise RuntimeError(f"{name}: document outlives its atoms")
                path = args.work / f"{name}-{optimize}.{mode}.cpp"
                path.write_text(cpp + RECORD_CLIENT.replace("@OWNER@", owner[0]))
                for index, compiler in enumerate(compilers):
                    binary = path.with_suffix(f".{index}")
                    result = run(
                        [compiler, *FLAGS, *includes, str(path), *libraries, "-o", str(binary)]
                    )
                    if (
                        result.stdout
                        or result.stderr
                        or dom.VM.search(run([args.nm, "-C", str(binary)]).stdout)
                    ):
                        raise RuntimeError(f"{name}: native compilation/symbol gate failed")
                    if run([str(binary)]).stdout != "DOM record Data passed\n":
                        raise RuntimeError(f"{name}: real document observations incomplete")
                    executions += 1
        if label in ("field", "global-snapshot"):
            for suffix, changed, budget in (
                ("stale", dict(contract, module_sha256="0" * 64), None),
                ("missing-root", dict(contract, roots=[]), None),
                ("missing-input", dict(contract, element_parameters=[]), None),
                ("no-map", dict(contract, initial_intrinsics=[]), None),
                ("budget", contract, 0),
                ("small-budget", contract, 100),
            ):
                for optimize in (False, True):
                    dom.lower(
                        args,
                        ir,
                        changed,
                        f"{name}-{suffix}-{optimize}",
                        optimize=optimize,
                        success=False,
                        max_steps=budget,
                    )
                    refusals += 1
            forged = args.work / f"{name}-forged.mlir"
            forged_text, count = re.subn(
                r"ctnative.host_outer_key_inputs = 1 : i64",
                "ctnative.host_outer_key_inputs = 99 : i64",
                annotated.read_text(),
            )
            if count != 1:
                raise RuntimeError("record forged input control lost its report attribute")
            forged.write_text(forged_text)
            fresh = dict(contract, module_sha256=host.fingerprint(args.opt, forged))
            rechecked, _, _ = host.analyze(
                args.opt, forged, fresh, args.work / f"{name}-forged-proof"
            )
            if rechecked != report:
                raise RuntimeError("forged report changed the live record proof")
            for optimize in (False, True):
                dom.lower(args, forged, fresh, f"{name}-forged-{optimize}", optimize=optimize)
    controls = {
        "getter": original.replace("{n: 15927}", "{get n() { return 15927; }}"),
        "dynamic-key": original.replace("payload.n", "payload[element]"),
        "uninitialized": original.replace("{n: 15927}", "{}"),
        "escape": original.replace("traceEntered = 1;", "escaped = payload; traceEntered = 1;"),
        "capture": original.replace(
            "traceEntered = 1;", "const unused = () => payload; traceEntered = 1;"
        ),
        "unknown-call": original.replace(
            "traceEntered = 1;", "unknown(payload); traceEntered = 1;"
        ),
        "global-reassigned": snapshot.replace(
            "saved = payload.n;", "saved = payload.n; saved = 7;"
        ),
        "global-object-escape": snapshot.replace(
            "saved = payload.n;", "escaped = payload; saved = payload.n;"
        ),
        "global-getter": snapshot.replace("{n: 15927}", "{get n() { return 15927; }}"),
        "global-capture": snapshot.replace(
            "saved = payload.n;", "const unused = () => payload; saved = payload.n;"
        ),
        "global-uninitialized": snapshot.replace("{n: 15927}", "{}"),
        "global-unowned-field": snapshot.replace(
            "host.slot.set(element, saved)", "host.slot.set(element, 15927)"
        ),
    }
    for label, text in controls.items():
        name = "record-refused-" + label
        ir, contract = published_prepare(args, name, text)
        for optimize in (False, True):
            dom.lower(args, ir, contract, f"{name}-{optimize}", optimize=optimize, success=False)
            refusals += 1
    for constructor in (False, True):
        name = "published-class-" + ("constructor" if constructor else "holder")
        text = published_source(constructor)
        published_observation(args, name, text, 59112 if constructor else 15927)
        ir, contract = published_prepare(args, name, text)
        classes.prepare(args, name, ir, contract, success=False, diagnostic="class ")
        refusals += 1
        for optimize in (False, True):
            dom.lower(args, ir, contract, f"{name}-{optimize}", optimize=optimize, success=False)
            refusals += 1
    print(
        f"published DOM Data: {len(sources) + 2} Node/VM observations, {executions} native record executions, {refusals} refusals; complete vendor class publication remains refused"
    )


def object_constructors(args, constructor):
    original = (
        constructor.replace("probe(element)", "probe()")
        .replace("    const t", "    const element = {};\n    const t", 1)
        .replace("a = savedFirst.n", "return savedFirst.n", 1)
        + "\nvar a = probe();\n"
    )
    checked = 0
    for label, text, expected in (
        ("object", original, 59112),
        (
            "alias",
            original.replace("    const t", "    const alias = element;\n    const t", 1).replace(
                "new Item(7, element)", "new Item(7, alias)"
            ),
            59112,
        ),
        (
            "distinct",
            original.replace("    const t", "    const other = {};\n    const t", 1)
            .replace("new Item(7, element)", "new Item(7, other)")
            .replace('e.get(element, "bs.item").n * 1000', 'e.get(other, "bs.item").n * 1000')
            .replace(
                "return savedFirst.n",
                'return (e.get(element, "bs.item") === null) * 100000 + savedFirst.n',
            ),
            159112,
        ),
    ):
        name = "class-map-record-constructor-key-" + label
        js, ir, _ = boundary.prepare(args, name, text)
        for command in ([args.node, "-e", classes.NODE, str(js)], [args.reference, str(js)]):
            if run(command).stdout != f"a={expected}\n":
                raise RuntimeError(f"{name}: source observation changed")
        requested = host.manifest(args.opt, ir)
        requested.update(initial_intrinsics=["Map", "__ctbrowser_class_defined"])
        prepared = classes.prepare(args, name, ir, requested, success=True)
        for optimize in (False, True):
            native = args.work / f"{name}-{optimize}.native.mlir"
            run(
                [
                    args.opt,
                    str(prepared),
                    f"--ctnative-lower-to-emitc=optimize={str(optimize).lower()}",
                    "-o",
                    str(native),
                ]
            )
            checked += classes.check_executable(args, f"{name}-{optimize}", native, expected)
    return checked


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("translate", "opt", "node", "reference", "clang"):
        parser.add_argument("--" + name, required=True)
    for name in ("build", "include"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--nm", default=shutil.which("nm"))
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--class-families-only", action="store_true")
    args = parser.parse_args()
    args.work = args.work.resolve()
    args.work.mkdir(parents=True, exist_ok=True)
    published_families(args)
    if args.class_families_only:
        return
    original = source()
    constructor = source(constructor=True)
    cases = {
        "input": (original, 1, ""),
        "alias": (
            original.replace("    class Item", "    const alias = element;\n    class Item", 1)
            .replace("e.get(element,", "e.get(alias,")
            .replace("e.remove(element,", "e.remove(alias,"),
            1,
            "",
        ),
        "two-inputs": (
            original.replace("probe(element)", "probe(element, other)").replace(
                'e.set(element, "bs.item", second)', 'e.set(other, "bs.item", second)'
            ),
            2,
            "class nested Map DOM inputs require an alias proof",
        ),
        "property-observer": (
            original.replace("    class Item", "    element.n;\n    class Item", 1),
            1,
            "class DOM input has an observer outside its proved Map keys",
        ),
        "coercion-observer": (
            original.replace("    class Item", "    element + '';\n    class Item", 1),
            1,
            "class DOM input has an observer outside its proved Map keys",
        ),
        "captured-observer": (
            original.replace(
                "    class Item", "    const unused = () => element;\n    class Item", 1
            ),
            1,
            "unknown call, binding or reflective effect (op ctjs.create_closure)",
        ),
        "constructor": (constructor, 1, ""),
        "constructor-alias": (
            constructor.replace(
                "    class Item", "    const alias = element;\n    class Item", 1
            ).replace("new Item(7, element)", "new Item(7, alias)"),
            1,
            "",
        ),
        "constructor-two-inputs": (
            constructor.replace("probe(element)", "probe(element, other)").replace(
                "new Item(7, element)", "new Item(7, other)"
            ),
            2,
            "class nested Map DOM inputs require an alias proof",
        ),
    }
    for label, statement in (
        ("field", "this.element = key;"),
        ("property", "key.n;"),
        ("coercion", "key + '';"),
        ("capture", "const unused = () => key;"),
        ("suffix", "a = 17;"),
    ):
        cases["constructor-" + label] = (
            constructor.replace(
                'e.set(key, "bs.item", this);', f'e.set(key, "bs.item", this); {statement}'
            ),
            1,
            "class ",
        )
    observed = prepared_count = refusals = native_refusals = 0
    for name, (text, parameters, diagnostic) in cases.items():
        if not diagnostic:
            observed_source = args.work / f"{name}.oracle.js"
            observed_source.write_text(text + "\nvar a; probe({});\n")
            for command in (
                [args.node, "-e", classes.NODE, str(observed_source)],
                [args.reference, str(observed_source)],
            ):
                expected = 59112 if name.startswith("constructor") else 15927
                if run(command).stdout != f"a={expected}\n":
                    raise RuntimeError(f"{name}: source observation changed")
            observed += 1
        ir, contract = dom.prepare(args, name, text, parameters, entry_name="probe")
        contract.update(
            provider="ctbrowser-dom-data-session-v1",
            roots=[{"binding": "host", "properties": ["slot"]}],
            observations=["a"],
            absent_bindings=[],
            undefined_bindings=[],
            initial_intrinsics=["Map", "__ctbrowser_class_defined"],
        )
        prepared = classes.prepare(
            args, name, ir, contract, success=not diagnostic, diagnostic=diagnostic
        )
        if diagnostic:
            refusals += 1
            continue
        prepared_count += 1
        body = prepared.read_text()
        # Two record constructions and two independently owned child Maps survive.
        if body.count("ctjs.construct") != 4 or "ctjs.set_property" not in body:
            raise RuntimeError(f"{name}: preparation erased a record or child owner")
        entry = re.search(
            r"ctjs.func(?: private)? @probe\$\d+\((.*?)\)(.*?)(?=\n  ctjs.func |\n})", body, re.S
        )
        if not entry:
            raise RuntimeError(f"{name}: preparation lost its source entry")
        arguments = re.findall(r"(%[\w.$-]+): !ctjs.value", entry[1])
        if len(arguments) != 4 or any(
            arguments[-1] in line and "ctjs.root " not in line for line in entry[2].splitlines()
        ):
            raise RuntimeError(f"{name}: a DOM input escaped its discharged key uses")
        checked = dict(contract, module_sha256=host.fingerprint(args.opt, prepared))
        for optimize in (False, True):
            result = dom.lower(
                args,
                prepared,
                checked,
                f"{name}-owner-{optimize}",
                optimize=optimize,
                success=False,
            )
            if "native DOM Data" not in result:
                raise RuntimeError("local class Data acquired an unproved session owner")
            native_refusals += 1
        for label, requested, options in (
            ("stale", dict(contract, module_sha256="0" * 64), ""),
            ("no-map", dict(contract, initial_intrinsics=["__ctbrowser_class_defined"]), ""),
            ("budget", contract, "max-steps=0"),
            ("small-budget", contract, "max-steps=100"),
            ("missing-input", dict(contract, element_parameters=[]), ""),
        ):
            classes.prepare(args, f"{name}-{label}", ir, requested, success=False, options=options)
            refusals += 1
    executions = object_constructors(args, constructor)
    print(
        f"class DOM Data: {observed} Node/VM observations, {prepared_count} prepared entries, "
        f"{refusals} preparation refusals, {native_refusals} incomplete-session refusals; "
        f"{executions} native object-key executions; no native class DOM admission"
    )
    published_records(args)


if __name__ == "__main__":
    main()
