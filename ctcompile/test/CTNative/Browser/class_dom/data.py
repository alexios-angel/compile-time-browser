#!/usr/bin/env python3
"""Test Bootstrap's Map-based Data helper, used from a class or an object record, natively.

The probe keeps Item instances in Bootstrap's element -> key -> instance Map and
publishes a scalar through `host.slot`, a Map-backed provider owned by the
native DOM Data session. Each source runs under Node and the reference VM
first. The published_* families are then prepared (class initialization
specialized by --ctnative-specialize-class-initialization), proved by the host
contract analysis, lowered to C++ and run against a real ctbrowser document.
Each mutated source or IR must be refused. Sources that never publish through
`host.slot` (the cases in main) prepare but are still refused natively.
"""

from pathlib import Path
import re

from CTNative.Browser.dom import dom
from CTNative.Browser.dom import data_session as session
from CTNative.Exports import boundary
from CTNative.harness import build_and_run, run
from CTNative.HostContract import contract as host
from CTNative.Lowering.Objects.classes import initialization_inputs as classes
from Target.Cpp.harness import FLAGS


def source(constructor=False):
    """Return the JS probe source for the Bootstrap Data test.

    It comes from the class-map-record-nested-vendor-* case in
    Inputs/initialization/50.mlir and is checked to still contain Bootstrap's
    real `Data` declaration. `probe` takes the DOM element as a parameter. With
    `constructor=True` the element is passed through the Item constructor
    instead of a holder.
    """
    inputs = Path(__file__).resolve().parents[2] / "Lowering/Objects/classes/Inputs/initialization"
    text = (inputs / "50.mlir").read_text()
    kind = "constructor" if constructor else "holder"
    original = text.split(f"//--- class-map-record-nested-vendor-{kind}.js\n", 1)[1].split(
        "//---", 1
    )[0]
    # Drop the comment lines that introduce the next `//---` section.
    original = re.sub(r"(?:^//.*\n)+\Z", "", original, flags=re.M)
    vendor = dom.BOOTSTRAP.read_text()
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
    """Return source() inside the DOM Data session source: the probe's result is
    published through `host.slot` and the trace* globals record each step."""
    prefix = session.source.SOURCE.split("  traceEntered = 1;", 1)[0]
    prefix = prefix.replace("dataEntry(element, other)", "probe(element)").replace(
        "return state.has(key);", "return state.get(key);"
    )
    body = source(constructor).replace("function probe(element) {\n", prefix, 1)
    body = body.replace("a = savedFirst.n", "const result = savedFirst.n", 1)
    return (
        body.rstrip().removesuffix("}")
        + """
  traceEntered = 1;
  traceBefore = host.slot.get(element);
  host.slot.remove(element);
  host.slot.set(element, result);
  traceOther = host.slot.get(element);
  traceAfter = host.slot.get(element);
}
"""
    )


def record_source():
    """Return the DOM Data session source publishing the field of a plain object
    record ({n: 15927}) instead of a class instance."""
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


# C++ client for a native record session: checks the trace* observations,
# rejects foreign, null, text and stale element inputs before any effect, and
# reruns after the element is detached. 15927 is replaced by the expected value.
RECORD_CLIENT = (
    r"""
#include <array>
#include <cassert>
#include <iostream>
#include <type_traits>
"""
    + dom.SESSION_PREAMBLE
    + r"""
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
)


# 15927 is what the holder probe computes (savedFirst.n * 1000 +
# e.get(...).n * 100 + the flags and sizes it adds up), and 59112 is what the
# constructor variant computes. Other values below (12927, 21927, 29112) are
# the same sums with one field changed; the record sources reuse 15927.
def check_record_executable(
    args, name, native, expected=15927, class_value=None, *, client=RECORD_CLIENT
):
    """Translate `native` (explicit and deduced types) to C++, build it with both
    compilers and run RECORD_CLIENT against it; return the number of runs."""
    client = client.replace("15927", str(expected))
    if expected in ("true", "false"):
        client = re.sub(
            r"ctnative::global_number\(((?:session|second)\.observe_trace(?:After|Other)\(\))\)"
            r"\.value\(\) == " + expected,
            rf"ctnative::global_boolean(\1) == ctnative::js_boolean_t{{{expected}}}",
            client,
        )
    if class_value is not None:
        client = client.replace(
            "return std::array{", "return std::array{session.observe_classResult(), "
        ).replace(
            "auto check = [&] {",
            "auto check = [&] {\n"
            f"            assert(ctnative::global_number(session.observe_classResult()).value() == {class_value});",
        )
    compilers = args.compilers
    includes, libraries = dom.link_options(args)
    executions = 0
    deduced = args.work / f"{name}.deduced.mlir"
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
        path = args.work / f"{name}.{mode}.cpp"
        path.write_text(cpp + client.replace("@OWNER@", owner[0]))
        flags = [*FLAGS, *includes]
        for index, compiler in enumerate(compilers):
            binary = path.with_suffix(f".{index}")
            result = build_and_run(
                compiler,
                path,
                binary,
                flags=flags,
                libraries=libraries,
                nm=args.nm,
                vm=dom.VM,
                clean=True,
            )
            if result.stdout != "DOM record Data passed\n":
                raise RuntimeError(f"{name}: real document observations incomplete")
            executions += 1
    return executions


def published_prepare(args, name, text, *, parameters=1):
    """Prepare `text` with a DOM Data session contract rooted at `host.slot`."""
    ir, contract = dom.prepare(args, name, text, parameters, entry_name="probe")
    contract.update(
        roots=[{"binding": "host", "properties": ["slot"]}],
        provider="ctbrowser-dom-data-session-v1",
        observations=session.source.OBSERVATIONS,
        absent_bindings=[],
        undefined_bindings=[],
        initial_intrinsics=["Map", "__ctbrowser_class_defined"],
    )
    return ir, contract


def published_observation(
    args, name, text, expected=15927, *, snapshot=False, class_result=False, class_value=None
):
    """Run `text` under Node and the reference VM and check the published traces."""
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
        wanted = f"classResult={expected if class_value is None else class_value}\n" + wanted
    node = session.source.NODE.replace(
        "typeof value !== 'number'", "typeof value !== 'number' && typeof value !== 'undefined'"
    ).replace(
        "key.startsWith('trace')",
        "key.startsWith('trace') || key === 'saved' || key === 'classResult'",
    )
    for command in ([args.node, "-e", node, str(observed)], [args.reference, str(observed)]):
        if run(command).stdout != wanted:
            raise RuntimeError(f"{name}: scalar publication/reset source observation changed")


def published_provider(args, name, prepared, contract, *, adversarial=False):
    """Prove the prepared provider and check the report; with `adversarial`,
    also check that forged or mutated IR is refused."""
    report, annotated, _ = host.analyze(
        args.opt, prepared, contract, args.work / (name + "-provider")
    )
    if (
        not report["proved"]
        or report["outer_key_inputs"] != 1
        or report["outer_key_objects"] != 0
        or report["observation_stores"] != len(contract["observations"])
        or len(report["slots"]) != 1
        # Five ctjs.construct sites (the host slot's `state` Map, Bootstrap's
        # outer and per-element Maps, and the two Items) must all be proved.
        or report["slots"][0]["proved_edges"] != 5
    ):
        raise RuntimeError(f"{name}: incomplete prepared class provider proof: {report}")
    text = annotated.read_text()
    if (
        host.fingerprint(args.opt, annotated) != contract["module_sha256"]
        or dom.FUNCTION.findall(text) != dom.FUNCTION.findall(prepared.read_text())
        or text.count("ctjs.construct") != 5
    ):
        raise RuntimeError(f"{name}: provider analysis changed the retained source graph")
    if not adversarial:
        return report, annotated

    # Adversarial checks: edit the already-prepared IR but keep its old success
    # report. Each edit gets a new fingerprint, so the analysis must re-run the
    # proof on the edited IR and refuse; it must never trust the stale report.
    constructor = re.search(r"(?ms)^  ctjs.func @Item\$(\d+)\([^\n]+\n.*?^  }", text)
    if not constructor:
        raise RuntimeError("provider controls lost the retained constructor")
    creation = re.search(
        rf"(?m)^    (%\w+) = ctjs.create_closure %arg2\[{constructor[1]}\] this %\w+$", text
    )
    if not creation:
        raise RuntimeError("provider controls lost the constructor closure")
    instance = re.search(
        rf"(?m)^    (%\w+) = ctjs.construct {creation[1]}\({creation[1]}, (%\w+)\)$", text
    )
    local_map = re.search(
        r'(?m)^    (%\w+) = ctjs.load_global "Map"\n' r"    (%\w+) = ctjs.construct \1\(\1\)$",
        text,
    )
    if not instance or not local_map:
        raise RuntimeError("provider controls lost original instance or local Map allocation")
    method = re.search(rf"(?m)^    (%\w+) = ctjs.get_property {local_map[2]}\[(%\w+)\]$", text)
    getter = re.search(
        r'(?m)^    (%\w+) = ctjs.constant #ctjs.string<"get">\n'
        rf"    (%\w+) = ctjs.get_property {local_map[2]}\[\1\]$",
        text,
    )
    if not method or not getter:
        raise RuntimeError("provider controls lost local Map method selections")
    get = re.search(rf"(?m)^    (%\w+) = ctjs.call {getter[2]}\({local_map[2]}, (%\w+)\)$", text)
    stored = re.search(
        rf"(?m)^    (%\w+) = ctjs.call %\w+\({local_map[2]}, %\w+, {instance[1]}\)$", text
    )
    if not get or not stored:
        raise RuntimeError("provider controls lost local Map get/set calls")
    key = f'    {get[2]} = ctjs.constant #ctjs.string<"bs.item">'
    method_key = f'    {method[2]} = ctjs.constant #ctjs.string<"has">'
    if text.count(key) != 1 or text.count(method_key) != 1:
        raise RuntimeError("provider controls lost exact local Map keys")
    effect = constructor[0].replace(
        "    ctjs.frame_exit",
        '    %provider_effect = ctjs.load_global "provider_unknown"\n    ctjs.frame_exit',
        1,
    )
    replacement = re.sub(
        r"ctjs.return %\w+",
        "ctjs.return %provider_replacement",
        constructor[0].replace(
            "    ctjs.frame_exit",
            "    %provider_replacement = ctjs.create_object\n    ctjs.frame_exit",
            1,
        ),
    )
    variants = {
        "constructor-skipped": text.replace(
            constructor[0], constructor[0].replace("attributes {", "attributes {ctjs.skipped,", 1)
        ),
        "map-read-skipped": text.replace(method[0], method[0] + " {ctjs.skipped}", 1),
        "constructor-effect": text.replace(constructor[0], effect),
        "constructor-replacement": text.replace(constructor[0], replacement),
        "constructor-argument": text.replace(
            instance[0], instance[0].replace(", " + instance[2] + ")", ", %arg3)")
        ),
        "constructor-callee": text.replace(
            instance[0],
            instance[0].replace("construct " + creation[1], "construct " + instance[2]),
        ),
        "constructor-new-target": text.replace(
            instance[0], instance[0].replace("(" + creation[1] + ",", "(" + instance[2] + ",")
        ),
        "constructor-program": text.replace(
            creation[0], creation[0].replace("create_closure %arg2", "create_closure %arg3")
        ),
        "missing-get": text.replace(key, key.replace('"bs.item"', '"provider-missing"')),
        "unknown-map-method": text.replace(method_key, method_key.replace('"has"', '"clear"')),
        "map-receiver": text.replace(
            get[0], get[0].replace("(" + local_map[2] + ",", "(" + instance[1] + ",")
        ),
    }
    for label, anchor, value in (
        ("constructor-escape", creation[0], creation[1]),
        ("map-escape", local_map[0], local_map[2]),
        ("map-method-escape", method[0], method[1]),
        ("map-result-escape", stored[0], stored[1]),
        ("instance-escape", instance[0], instance[1]),
        ("instance-alias-escape", get[0], get[1]),
    ):
        variants[label] = text.replace(
            anchor, anchor + f'\n    ctjs.store_global "provider_escape", {value}', 1
        )
    for label, changed in variants.items():
        if changed == text:
            raise RuntimeError(f"{label}: provider control did not change prepared IR")
        path = args.work / f"{name}-provider-{label}.mlir"
        path.write_text(changed)
        fresh = dict(contract, module_sha256=host.fingerprint(args.opt, path))
        rejected, _, _ = host.analyze(
            args.opt, path, fresh, args.work / f"{name}-provider-{label}-check"
        )
        if (
            rejected["proved"]
            or not rejected["reason"]
            or any(slot["proved_edges"] for slot in rejected["slots"])
        ):
            raise RuntimeError(
                f"{label}: forged success survived live provider reproof: {rejected}"
            )
        if label in ("constructor-callee", "constructor-new-target", "constructor-program"):
            dom.refuse(args, path, fresh, f"{name}-provider-{label}-native")
    for label, request, options, reason in (
        ("stale", dict(contract, module_sha256="0" * 64), "", "fingerprint mismatch"),
        ("budget", contract, "max-steps=0", "budget"),
    ):
        rejected, _, _ = host.analyze(
            args.opt, annotated, request, args.work / f"{name}-provider-{label}", options=options
        )
        if (
            rejected["proved"]
            or reason not in rejected["reason"]
            or any(slot["proved_edges"] for slot in rejected["slots"])
        ):
            raise RuntimeError(f"{label}: provider failed to withhold all proof: {rejected}")
    forged = args.work / f"{name}-provider-forged-report.mlir"
    changed, count = re.subn(
        r"ctnative.host_outer_key_inputs = 1 : i64",
        "ctnative.host_outer_key_inputs = 99 : i64",
        text,
    )
    if count != 1:
        raise RuntimeError("provider report control lost its input attribute")
    forged.write_text(changed)
    rechecked, _, _ = host.analyze(
        args.opt,
        forged,
        dict(contract, module_sha256=host.fingerprint(args.opt, forged)),
        args.work / f"{name}-provider-forged-report-check",
    )
    if rechecked != report:
        raise RuntimeError("forged report changed the recomputed class provider proof")

    # Rewrite each `new Item(...)` by hand into create_object plus a direct call
    # of the constructor, with no proof annotations. The complete rewrite must
    # prove; each damaged one below must be refused.
    sites = re.findall(
        rf"(?m)^    (%\w+) = ctjs.construct {creation[1]}\({creation[1]}, (%\w+)\)$", text
    )
    if len(sites) != 2:
        raise RuntimeError("lifted provider control lost its two constructor sites")
    lifted = text
    for index, (value, argument) in enumerate(sites):
        lifted = lifted.replace(
            f"    {value} = ctjs.construct {creation[1]}({creation[1]}, {argument})",
            f"    {value} = ctjs.create_object\n"
            f"    %initializer_undefined{index} = ctjs.constant #ctjs.undefined\n"
            f"    %initializer_result{index} = ctjs.call_direct @Item${constructor[1]}"
            f"({value}, %initializer_undefined{index}, {creation[1]}, {argument})",
        )
    first, argument = sites[0]
    call = (
        f"    %initializer_result0 = ctjs.call_direct @Item${constructor[1]}"
        f"({first}, %initializer_undefined0, {creation[1]}, {argument})"
    )
    allocation = f"    {first} = ctjs.create_object"
    early_key = '    %initializer_key = ctjs.constant #ctjs.string<"n">\n'
    variants = {
        "complete": lifted,
        "callee": lifted.replace(
            call, call.replace(", " + creation[1] + ",", ", %initializer_undefined0,")
        ),
        "receiver": lifted.replace(call, call.replace("(" + first + ",", "(%arg3,")),
        "repeated": lifted.replace(
            call, call + "\n" + call.replace("%initializer_result0", "%initializer_repeat")
        ),
        "read-before": lifted.replace(
            allocation,
            allocation
            + "\n"
            + early_key
            + f"    %initializer_early = ctjs.get_property {first}[%initializer_key]",
        ),
        "write-before": lifted.replace(
            allocation,
            allocation
            + "\n"
            + early_key
            + f"    ctjs.set_property {first}[%initializer_key], {argument}",
        ),
        "map-before": lifted.replace(call + "\n", "").replace(stored[0], stored[0] + "\n" + call),
        "result-escape": lifted.replace(
            call, call + '\n    ctjs.store_global "provider_escape", %initializer_result0'
        ),
    }
    for label, changed in variants.items():
        path = args.work / f"{name}-initializer-{label}.mlir"
        path.write_text(changed)
        fresh = dict(contract, module_sha256=host.fingerprint(args.opt, path))
        checked, _, _ = host.analyze(args.opt, path, fresh, path.with_suffix(".check"))
        if label == "complete":
            if not checked["proved"] or checked["slots"][0]["proved_edges"] != 5:
                raise RuntimeError(f"lifted initializer lost its live provider proof: {checked}")
            for suffix, request, options in (
                ("stale", contract, ""),
                ("budget", fresh, "max-steps=0"),
            ):
                refused, _, _ = host.analyze(
                    args.opt,
                    path,
                    request,
                    args.work / f"{name}-initializer-{suffix}",
                    options=options,
                )
                if refused["proved"] or any(slot["proved_edges"] for slot in refused["slots"]):
                    raise RuntimeError(f"{suffix}: lifted initializer retained stale proof")
        elif (
            changed == lifted
            or checked["proved"]
            or not checked["reason"]
            or any(slot["proved_edges"] for slot in checked["slots"])
        ):
            raise RuntimeError(f"{label}: changed initializer retained provider proof: {checked}")
    print(
        "class provider: 19 prepared-IR refusals, six native provenance refusals, "
        "one lifted initializer proof, nine initializer refusals, forged report reproof passed"
    )


def published_families(args):
    """Check the holder, constructor and alias sources that also publish the
    class result, then source edits that must be refused during preparation."""
    prepared_count = refusals = observed = executions = 0
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
        published_provider(args, name, prepared, checked)
        for optimize in (False, True):
            native = dom.lower(args, prepared, checked, f"{name}-{optimize}", optimize=optimize)
            executions += check_record_executable(
                args, f"{name}-{optimize}", native, expected, expected
            )
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
    print(
        f"class public family: {observed} Node/VM observations, {prepared_count} preparations, "
        f"{prepared_count} provider proofs, {executions} native executions, {refusals} refusals"
    )


def published_composite_results(args):
    """Check sources that publish the whole computed result, plus source and IR
    edits to its arithmetic, Map reads and branches that must be refused."""
    executions = refusals = 0
    for constructor, observations in ((False, False), (True, False), (False, True)):
        name = "published-composite-" + ("constructor" if constructor else "holder")
        text = published_source(constructor)
        expected = 59112 if constructor else 15927
        if observations:
            name += "-observations"
            text = text.replace(
                "+ count;",
                '+ count + savedChild.has("late") * 100000'
                '+ savedChild.delete("late") * 1000000'
                " + (savedChild.clear() === void 0) * 10000000;",
            )
            expected += 11100000
        published_observation(args, name, text, expected)
        ir, contract = published_prepare(args, name, text)
        prepared = classes.prepare(args, name, ir, contract, success=True)
        body = prepared.read_text()
        if body.count("ctjs.construct") != 5 or len(dom.FUNCTION.findall(body)) != 8:
            raise RuntimeError(
                f"{name}: preparation lost original constructor, Map or function owners"
            )
        checked = dict(contract, module_sha256=host.fingerprint(args.opt, prepared))
        _, annotated = published_provider(args, name, prepared, checked)
        for optimize in (False, True):
            # Without preparation, native lowering must refuse the raw source.
            dom.lower(
                args, ir, contract, f"{name}-raw-{optimize}", optimize=optimize, success=False
            )
            refusals += 1
            native = dom.lower(args, prepared, checked, f"{name}-{optimize}", optimize=optimize)
            executions += check_record_executable(args, f"{name}-{optimize}", native, expected)
        if constructor or observations:
            continue
        for suffix, request, options in (
            ("stale", dict(contract, module_sha256="0" * 64), ""),
            ("no-root", dict(contract, roots=[]), ""),
            ("no-input", dict(contract, element_parameters=[]), ""),
            ("no-map", dict(contract, initial_intrinsics=["__ctbrowser_class_defined"]), ""),
            ("budget", contract, "max-steps=0"),
            ("small-budget", contract, "max-steps=100"),
        ):
            classes.prepare(args, name + "-" + suffix, ir, request, success=False, options=options)
            refusals += 1
        controls = {
            "map-operand": text.replace("+ savedChild.size", "+ savedChild"),
            "string-operand": text.replace("+ (absent === null)", '+ "1"'),
            "branch-object": text.replace("? 31 : 5 + second.n", "? {} : 5 + second.n"),
            "branch-field-write": text.replace(
                "? 31 : 5 + second.n", "? (second.n = 31) : 5 + second.n"
            ),
            "branch-dom-operand": text.replace("? 31 : 5 + second.n", "? 31 : 5 + element"),
            "map-size-receiver": text.replace("+ savedChild.size", "+ element.size"),
            "unknown-map-member": text.replace('savedChild.has("missing")', "savedChild.valueOf()"),
            "map-key-operand": text.replace('savedChild.has("missing")', "savedChild.has(element)"),
            "child-escape": text.replace(
                "  traceEntered = 1;", "  escaped = savedChild; traceEntered = 1;"
            ),
            "record-escape": text.replace(
                "  traceEntered = 1;", "  escaped = savedFirst; traceEntered = 1;"
            ),
        }
        for label, changed in controls.items():
            if changed == text:
                raise RuntimeError(f"{label}: composite source control changed no source")
            variant = name + "-" + label
            bad_ir, bad_contract = published_prepare(args, variant, changed)
            classes.prepare(args, variant, bad_ir, bad_contract, success=False, diagnostic="class ")
            refusals += 1

        # Edit the proved IR but keep its success report. The new fingerprint
        # makes the analysis re-prove the edited IR, which it must refuse.
        proved = annotated.read_text()
        entry = re.search(
            rf"(?ms)^  ctjs.func (?:private )?@{re.escape(checked['entry'])}\([^\n]+\n.*?^  }}",
            proved,
        )
        if not entry:
            raise RuntimeError("composite provider controls lost the complete entry")
        multiply = re.search(r"(?m)^    (%\w+) = ctjs.binary mul (%\w+), (%\w+)$", entry[0])
        size = re.search(
            r'(?m)^    (%\w+) = ctjs.constant #ctjs.string<"size">\n'
            r"    (%\w+) = ctjs.get_property (%\w+)\[\1\]$",
            entry[0],
        )
        branch_read = re.search(
            r"(?m)^      (%\w+) = ctjs.get_property (%\w+)\[(%\w+)\]$", entry[0]
        )
        if not multiply or not size or not branch_read:
            raise RuntimeError(
                "composite provider controls lost arithmetic, Map size or branch read"
            )
        size_read = size[0].split("\n")[1]
        variants = {
            "object-operand": proved.replace(
                multiply[0],
                "    %composite_object = ctjs.create_object\n"
                + multiply[0].replace(", " + multiply[3], ", %composite_object"),
                1,
            ),
            "dom-operand": proved.replace(
                multiply[0], multiply[0].replace(", " + multiply[3], ", %arg3"), 1
            ),
            "size-receiver": proved.replace(
                size_read, size_read.replace("get_property " + size[3], "get_property %arg3"), 1
            ),
            "size-skipped": proved.replace(size_read, size_read + " {ctjs.skipped}", 1),
            "arithmetic-skipped": proved.replace(multiply[0], multiply[0] + " {ctjs.skipped}", 1),
            "branch-write": proved.replace(
                branch_read[0],
                branch_read[0]
                + f"\n      ctjs.set_property {branch_read[2]}[{branch_read[3]}], {branch_read[1]}",
                1,
            ),
        }
        for label, changed in variants.items():
            if changed == proved:
                raise RuntimeError(f"{label}: composite provider control changed no IR")
            variant = name + "-provider-" + label
            path = args.work / f"{variant}.mlir"
            path.write_text(changed)
            fresh = dict(checked, module_sha256=host.fingerprint(args.opt, path))
            rejected, _, _ = host.analyze(args.opt, path, fresh, args.work / (variant + "-check"))
            if (
                rejected["proved"]
                or not rejected["reason"]
                or any(slot["proved_edges"] for slot in rejected["slots"])
            ):
                raise RuntimeError(f"{label}: composite retained forged provider proof: {rejected}")
            refusals += 1
            dom.refuse(args, path, fresh, variant)
            refusals += 2
    print(
        "original composite DOM Data: three Node/VM observations, three preparations, "
        f"three provider proofs, {executions} native executions, {refusals} refusals"
    )


def published_class_fields(args):
    """Check sources that publish an Item field written after construction."""
    original = (
        published_source()
        .replace("  traceEntered = 1;", "  classResult = result;\n  traceEntered = 1;")
        .replace("host.slot.set(element, result)", "host.slot.set(element, savedFirst.n)")
    )
    constructor = (
        published_source(True)
        .replace("  traceEntered = 1;", "  classResult = result;\n  traceEntered = 1;")
        .replace("host.slot.set(element, result)", "host.slot.set(element, savedFirst.n)")
    )
    sources = {
        "saved-record": (original, 5, 15927),
        "constructor": (constructor, 5, 59112),
        "distinct-record": (
            original.replace(
                "host.slot.set(element, savedFirst.n)", "host.slot.set(element, second.n)"
            ),
            9,
            15927,
        ),
        "read-snapshot": (
            original.replace(
                "  traceEntered = 1;",
                "  const scalar = savedFirst.n; savedFirst.n = {};\n  traceEntered = 1;",
            ).replace("host.slot.set(element, savedFirst.n)", "host.slot.set(element, scalar)"),
            5,
            15927,
        ),
        "arithmetic": (
            original.replace(
                "host.slot.set(element, savedFirst.n)",
                "host.slot.set(element, savedFirst.n + second.n)",
            ),
            14,
            15927,
        ),
    }
    refusals = executions = 0
    for label, (text, expected, class_value) in sources.items():
        name = "class-field-" + label
        published_observation(
            args, name, text, expected, class_result=True, class_value=class_value
        )
        ir, contract = published_prepare(args, name, text)
        contract["observations"] = ["classResult", *contract["observations"]]
        prepared = classes.prepare(args, name, ir, contract, success=True)
        body = prepared.read_text()
        if body.count("ctjs.construct") != 5 or len(dom.FUNCTION.findall(body)) != 8:
            raise RuntimeError(f"{name}: preparation lost constructor, Map or function owners")
        if 'name = "classResult"' not in body:
            raise RuntimeError(f"{name}: original class computation lost its observation")
        checked = dict(contract, module_sha256=host.fingerprint(args.opt, prepared))
        published_provider(args, name, prepared, checked)
        for optimize in (False, True):
            if label != "read-snapshot":
                native = dom.lower(args, prepared, checked, f"{name}-{optimize}", optimize=optimize)
                executions += check_record_executable(
                    args, f"{name}-{optimize}", native, expected, class_value
                )
                continue
            dom.lower(
                args, prepared, checked, f"{name}-{optimize}", optimize=optimize, success=False
            )
            refusals += 1
        if label == "saved-record":
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
        "object-field": original.replace("savedFirst.n = 5;", "savedFirst.n = {};"),
        "unproved-field-source": original.replace(
            "savedFirst.n = 5;", "const outside = {n: 5}; savedFirst.n = outside.n;"
        ),
        "late-object-field": original.replace(
            "  traceEntered = 1;", "  savedFirst.n = {}; traceEntered = 1;"
        ),
        "escaped-alias": original.replace(
            "  traceEntered = 1;", "  unknown(savedFirst); traceEntered = 1;"
        ),
        "captured-alias": original.replace(
            "  traceEntered = 1;", "  const unused = () => savedFirst; traceEntered = 1;"
        ),
        "getter": original.replace(
            "  traceEntered = 1;",
            '  Object.defineProperty(savedFirst, "n", {get() { return 5; }}); traceEntered = 1;',
        ),
        "replacement": original.replace("this.n = n;", "this.n = n; return {};"),
        "constructor-effect": original.replace("this.n = n;", "this.n = n; unknown();"),
        "missing-key": original.replace(
            "host.slot.set(element, savedFirst.n)", "host.slot.set(element, savedFirst.missing)"
        ),
        "missing-record": original.replace(
            'const savedChild = t.get(element), savedFirst = e.get(element, "bs.item");',
            'const savedChild = t.get(element), savedFirst = e.get(element, "missing");',
        ),
    }
    for label, text in controls.items():
        if text == original:
            raise RuntimeError(f"{label}: class-field refusal did not change the source")
        name = "class-field-refused-" + label
        ir, contract = published_prepare(args, name, text)
        contract["observations"] = ["classResult", *contract["observations"]]
        classes.prepare(args, name, ir, contract, success=False, diagnostic="class ")
        refusals += 1
    print(
        f"class scalar fields: {len(sources)} Node/VM observations, {len(sources)} preparations, {len(sources)} provider proofs, {executions} native executions, {refusals} refusals"
    )


def published_constructor_fields(args):
    """Check sources that publish an Item field set only by the constructor."""
    original = (
        published_source()
        .replace("  traceEntered = 1;", "  classResult = result;\n  traceEntered = 1;")
        .replace("host.slot.set(element, result)", "host.slot.set(element, savedFirst.n)")
    )
    initialized = original.replace("savedFirst.n = 5;", "")
    categories = (
        original.replace("constructor(n)", "constructor(n, marker)")
        .replace("this.n = n;", "this.n = n; this.marker = marker;")
        .replace("new Item(2)", "new Item(2, true)")
        .replace("new Item(7)", "new Item(7, 19)")
    )
    sources = {
        # Each source's published field keeps the value the constructor gave it.
        # All must prove; all but read-snapshot must also run natively.
        "constructor-only": (initialized, 2, 12927),
        "wrong-record": (original.replace("savedFirst.n = 5;", "second.n = 5;"), 2, 12927),
        "early-read": (
            original.replace(
                "savedFirst.n = 5;", "const early = savedFirst.n; savedFirst.n = 5;"
            ).replace("host.slot.set(element, savedFirst.n)", "host.slot.set(element, early)"),
            2,
            15927,
        ),
        "literal": (initialized.replace("this.n = n;", "this.n = 11;"), 11, 21927),
        "registered": (
            published_source(True)
            .replace("savedFirst.n = 5;", "")
            .replace("  traceEntered = 1;", "  classResult = result;\n  traceEntered = 1;")
            .replace("host.slot.set(element, result)", "host.slot.set(element, savedFirst.n)"),
            2,
            29112,
        ),
        "boolean-instance": (
            categories.replace(
                "host.slot.set(element, savedFirst.n)", "host.slot.set(element, savedFirst.marker)"
            ),
            "true",
            15927,
        ),
        "number-instance": (
            categories.replace(
                "host.slot.set(element, savedFirst.n)", "host.slot.set(element, second.marker + 1)"
            ),
            20,
            15927,
        ),
        "last-store": (initialized.replace("this.n = n;", "this.n = true; this.n = n;"), 2, 12927),
        "read-snapshot": (
            initialized.replace(
                "  traceEntered = 1;",
                "  const scalar = savedFirst.n; savedFirst.n = {};\n  traceEntered = 1;",
            ).replace("host.slot.set(element, savedFirst.n)", "host.slot.set(element, scalar)"),
            2,
            12927,
        ),
        "boolean-arithmetic": (
            categories.replace(
                "host.slot.set(element, savedFirst.n)",
                "host.slot.set(element, savedFirst.marker + 1)",
            ),
            2,
            15927,
        ),
        "last-store-kind": (
            categories.replace(
                "this.marker = marker;", "this.marker = marker; this.marker = true;"
            ).replace(
                "host.slot.set(element, savedFirst.n)", "host.slot.set(element, second.marker + 1)"
            ),
            2,
            15927,
        ),
        "read-time-kind": (
            categories.replace(
                "  traceEntered = 1;",
                "  const early = savedFirst.marker; savedFirst.marker = 7; traceEntered = 1;",
            ).replace("host.slot.set(element, savedFirst.n)", "host.slot.set(element, early + 1)"),
            2,
            15927,
        ),
    }
    refusals = executions = 0
    for label, (text, expected, class_value) in sources.items():
        name = "class-constructor-field-" + label
        published_observation(
            args, name, text, expected, class_result=True, class_value=class_value
        )
        ir, contract = published_prepare(args, name, text)
        contract["observations"] = ["classResult", *contract["observations"]]
        prepared = classes.prepare(args, name, ir, contract, success=True)
        body = prepared.read_text()
        if body.count("ctjs.construct") != 5 or len(dom.FUNCTION.findall(body)) != 8:
            raise RuntimeError(f"{name}: preparation lost constructor, Map or function owners")
        if 'name = "classResult"' not in body:
            raise RuntimeError(f"{name}: original class computation lost its observation")
        checked = dict(contract, module_sha256=host.fingerprint(args.opt, prepared))
        published_provider(args, name, prepared, checked, adversarial=label == "constructor-only")
        for optimize in (False, True):
            if label != "read-snapshot":
                native = dom.lower(args, prepared, checked, f"{name}-{optimize}", optimize=optimize)
                executions += check_record_executable(
                    args, f"{name}-{optimize}", native, expected, class_value
                )
                continue
            dom.lower(
                args, prepared, checked, f"{name}-{optimize}", optimize=optimize, success=False
            )
            refusals += 1
        if label == "constructor-only":
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
        "object-argument": initialized.replace("new Item(2)", "new Item({n: 2})"),
        "unknown-argument": initialized.replace("new Item(2)", "new Item(unknown())"),
        "missing-argument": initialized.replace("new Item(2)", "new Item()"),
        "constructor-expression": initialized.replace("this.n = n;", "this.n = n + 1;"),
        "replacement": initialized.replace("this.n = n;", "this.n = n; return {};"),
        "effect": initialized.replace("this.n = n;", "this.n = n; unknown();"),
    }
    for label, text in controls.items():
        if text == initialized:
            raise RuntimeError(f"{label}: constructor-field refusal did not change the source")
        name = "class-constructor-field-refused-" + label
        ir, contract = published_prepare(args, name, text)
        contract["observations"] = ["classResult", *contract["observations"]]
        classes.prepare(args, name, ir, contract, success=False, diagnostic="class ")
        refusals += 1
    print(
        f"class constructor fields: {len(sources)} Node/VM observations, {len(sources)} preparations, {len(sources)} provider proofs, {executions} native executions, {refusals} refusals"
    )


def element_field_source():
    """Return published_source() with the element stored in an Item field and
    `host.slot` keyed through that field."""
    return (
        published_source()
        .replace("constructor(n)", "constructor(n, key)")
        .replace("this.n = n;", "this.n = n; this.element = key;")
        .replace("new Item(2)", "new Item(2, element)")
        .replace("new Item(7)", "new Item(7, element)")
        .replace("host.slot.get(element)", "host.slot.get(savedFirst.element)")
        .replace("host.slot.remove(element)", "host.slot.remove(savedFirst.element)")
        .replace("host.slot.set(element, result)", "host.slot.set(savedFirst.element, result)")
    )


def published_element_fields(args):
    """Check sources whose `host.slot` key is read back from an Item field."""
    original = element_field_source()
    sources = {
        "retained": original,
        "direct": original.replace("savedFirst.element", "first.element"),
        "saved-key": original.replace(
            "  traceEntered = 1;", "  const key = savedFirst.element; traceEntered = 1;"
        ).replace("host.slot.get(savedFirst.element)", "host.slot.get(key)"),
    }
    executions = refusals = 0
    for label, text in sources.items():
        name = "class-element-field-" + label
        published_observation(args, name, text)
        ir, contract = published_prepare(args, name, text)
        prepared = classes.prepare(args, name, ir, contract, success=True)
        checked = dict(contract, module_sha256=host.fingerprint(args.opt, prepared))
        published_provider(args, name, prepared, checked)
        for optimize in (False, True):
            native = dom.lower(args, prepared, checked, f"{name}-{optimize}", optimize=optimize)
            executions += check_record_executable(args, f"{name}-{optimize}", native)
        if label == "retained":
            for suffix, request, options in (
                ("stale", dict(contract, module_sha256="0" * 64), ""),
                ("no-input", dict(contract, element_parameters=[]), ""),
                ("no-root", dict(contract, roots=[]), ""),
                ("budget", contract, "max-steps=0"),
                ("small-budget", contract, "max-steps=100"),
            ):
                classes.prepare(args, name + suffix, ir, request, success=False, options=options)
                refusals += 1
    controls = {
        "retained-write": original.replace("savedFirst.n = 5;", "savedFirst.element = {};"),
        "direct-write": original.replace("savedFirst.n = 5;", "first.element = {};"),
        "nested-write": original.replace("5 + second.n", "(second.element = {}, 14)"),
        "saved-key-write": sources["saved-key"]
        .replace("const key =", "let key =")
        .replace("traceEntered = 1;", "key = {}; traceEntered = 1;"),
        "repeated-field": original.replace(
            "this.element = key;", "this.element = key; this.element = {};"
        ),
        "mixed-actual": original.replace("new Item(7, element)", "new Item(7, {})"),
        "missing-actual": original.replace("new Item(7, element)", "new Item(7)"),
        "replacement": original.replace("this.element = key;", "this.element = key; return {};"),
        "method": original.replace(
            "this.element = key; } }", "this.element = key; } key() { return this.element; } }"
        ),
        "helper-receiver": original.replace(
            "this.element = key;", "this.element = key; unknown(this);"
        ),
        "property-observer": original.replace(
            "host.slot.set(savedFirst.element, result)",
            "host.slot.set(savedFirst.element, savedFirst.element.nodeType)",
        ),
        "payload": original.replace(
            "host.slot.set(savedFirst.element, result)",
            "host.slot.set(savedFirst.element, savedFirst.element)",
        ),
        "coercion": original.replace(
            "host.slot.set(savedFirst.element, result)",
            "host.slot.set(savedFirst.element, +savedFirst.element)",
        ),
        "escape": original.replace(
            "  traceEntered = 1;", "  escaped = savedFirst; traceEntered = 1;"
        ),
    }
    for label, text in controls.items():
        if text == original:
            raise RuntimeError(f"{label}: element-field control did not change its source")
        name = "class-element-field-refused-" + label
        ir, contract = published_prepare(args, name, text)
        classes.prepare(args, name, ir, contract, success=False)
        refusals += 1
    print(
        f"class element fields: {len(sources)} Node/VM observations, {len(sources)} provider proofs, {executions} native executions, {refusals} refusals"
    )


def published_field_registration(args):
    """Check a constructor that registers the instance under its own element
    field (`e.set(this.element, ...)`)."""
    original = (
        published_source(True)
        .replace("this.n = n;", "this.n = n; this.element = key;")
        .replace('e.set(key, "bs.item", this)', 'e.set(this.element, "bs.item", this)')
    )
    retained = (
        original.replace("host.slot.get(element)", "host.slot.get(savedFirst.element)")
        .replace("host.slot.remove(element)", "host.slot.remove(savedFirst.element)")
        .replace("host.slot.set(element, result)", "host.slot.set(savedFirst.element, result)")
    )
    executions = refusals = 0
    for label, text in (("constructor", original), ("retained", retained)):
        name = "class-field-registration-" + label
        published_observation(args, name, text, 59112)
        ir, contract = published_prepare(args, name, text)
        prepared = classes.prepare(args, name, ir, contract, success=True)
        checked = dict(contract, module_sha256=host.fingerprint(args.opt, prepared))
        published_provider(args, name, prepared, checked)
        for optimize in (False, True):
            native = dom.lower(args, prepared, checked, f"{name}-{optimize}", optimize=optimize)
            executions += check_record_executable(args, f"{name}-{optimize}", native, 59112)
        if label == "constructor":
            for suffix, request, options in (
                ("no-input", dict(contract, element_parameters=[]), ""),
                ("no-map", dict(contract, initial_intrinsics=["__ctbrowser_class_defined"]), ""),
                ("budget", contract, "max-steps=0"),
                ("small-budget", contract, "max-steps=100"),
            ):
                classes.prepare(args, name + suffix, ir, request, success=False, options=options)
                refusals += 1
    registration = 'e.set(this.element, "bs.item", this)'
    controls = {
        "read-before-store": original.replace("this.element = key;", "").replace(
            registration, 'e.set(this.element, (this.element = key, "bs.item"), this)'
        ),
        "repeated-field": original.replace(
            "this.element = key;", "this.element = key; this.element = {};"
        ),
        "receiver-alias": original.replace(
            registration, '((alias) => e.set(alias.element, "bs.item", alias))(this)'
        ),
        "receiver-escape": original.replace(registration, "unknown(this); " + registration),
        "read-observer": original.replace(registration, "this.element.nodeType; " + registration),
        "parameter-observer": original.replace(registration, "key.nodeType; " + registration),
        "second-field": original.replace(
            "this.element = key;", "this.element = key; this.other = key;"
        ),
        "replacement": original.replace(registration + ";", registration + "; return {};"),
        "mixed-actual": original.replace("new Item(7, element)", "new Item(7, {})"),
        "accessor": original.replace(
            "    const first =",
            "    Object.defineProperty(Item.prototype, 'element', {set(value) {}});\n    const first =",
        ),
        "retained-write": retained.replace("savedFirst.n = 5;", "savedFirst.element = {};"),
    }
    for label, text in controls.items():
        if text == original or text == retained:
            raise RuntimeError(f"{label}: field-registration control did not change its source")
        name = "class-field-registration-refused-" + label
        ir, contract = published_prepare(args, name, text)
        classes.prepare(args, name, ir, contract, success=False)
        refusals += 1
    print(
        f"class field registration: two Node/VM sources, two provider proofs, {executions} native executions, {refusals} refusals"
    )


def published_records(args):
    """Check sources that publish a plain object record's field, without
    preparation, plus forged reports and edits that must be refused."""
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
            executions += check_record_executable(args, f"{name}-{optimize}", native)
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
        dom.refuse(args, ir, contract, name)
        refusals += 2
    print(
        f"published DOM Data: {len(sources)} Node/VM observations, {executions} native record executions, {refusals} refusals"
    )


def object_constructors(args, constructor):
    """Run the constructor source with plain-object keys instead of a DOM
    element natively; return the number of executions."""
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


def main(argv=None):
    args = dom.parse_args(__doc__, oracles=True, switches=("class-families-only",), argv=argv)
    published_families(args)
    published_composite_results(args)
    published_class_fields(args)
    published_constructor_fields(args)
    published_element_fields(args)
    published_field_registration(args)
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
        # Both Item constructions and both Bootstrap Map constructions (the outer
        # Map and the per-element child Map) must survive preparation.
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
        dom.refuse(args, prepared, checked, f"{name}-owner", "native DOM Data")
        native_refusals += 2
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
        f"{executions} native object-key executions; unpublished class DOM sessions remain refused"
    )
    published_records(args)


def test_data(lit):
    main(
        lit.args(
            "--translate ctjs-translate --opt ctjs-opt --node %node --reference %native_reference --clang %dom_clang --build %build --include %{monorepo}/ctbrowser/include --nm %nm --work %t"
        )
    )


if __name__ == "__main__":
    main()
