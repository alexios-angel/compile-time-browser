#!/usr/bin/env python3
"""Compose original class proofs with typed DOM entries and local fields."""

from CTNative.Lowering.Objects.class_dom_text_cases import *


def source(name, body):
    parameters = "element, other" if name in CASES or name in TWO_ELEMENT_CLASSES else "element"
    return f"function {name}({parameters}) {{\n  {body}}}\n"


def check_oracles(args):
    cases = CLASS_CASES | CASES
    declarations = "".join(source(name, body) for name, (body, _) in cases.items())
    observations, expected, vm_expected = [], [], []
    for name, (_, bits) in cases.items():
        for value, bit, vm_bit in zip(
            ("null", "''", r"'a\0b'", r"'\u00e9'"), bits, UTF16_VM_BITS.get(name, bits)
        ):
            variable = f"observation{len(observations):03}"
            effects = {
                "class_order": "if (element.getAttribute('x') !== 'after' || element.getAttribute('marker') !== 'done') throw new Error('lost class writes');",
                "class_element": "if (toggles !== 1) throw new Error('lost class toggle');",
                "class_unused_element": "if (toggles !== 1) throw new Error('lost class toggle');",
                "class_unused_write": "if (toggles !== 1 || element.getAttribute('unused-probe') !== null) throw new Error('proof method executed');",
                "class_method_key": "if (element.getAttribute('marker') !== 'done') throw new Error('lost method argument');",
                "class_method_transitive": "if (element.getAttribute('marker') !== 'done') throw new Error('lost transitive argument');",
                "method_transitive_only": "if (element.getAttribute('marker') !== 'done') throw new Error('lost transitive argument');",
                "class_method_default_key": "if (element.getAttribute('marker') !== 'done') throw new Error('lost default argument');",
                "class_method_default_order": "if (element.getAttribute('order') !== 'body' || element.getAttribute('default') !== 'untouched' || element.getAttribute('unused-default') !== 'untouched') throw new Error('lost default order');",
                "class_method_transitive_order": "if (element.getAttribute('x') !== 'after' || other.getAttribute('other') !== 'after' || other.getAttribute('x') !== 'different') throw new Error('lost transitive writes');",
                "direct_order": "if (element.getAttribute('x') !== 'after' || other.getAttribute('other') !== 'after' || other.getAttribute('x') !== 'different') throw new Error('lost receiver writes');",
                "field_order": "if (element.getAttribute('x') !== 'after' || element.getAttribute('marker') !== 'done') throw new Error('lost field writes');",
                "field_element": "if (other.getAttribute('other') !== 'after' || other.getAttribute('x') !== 'different') throw new Error('lost field receiver');",
                "field_snapshot": "if (element.getAttribute('x') !== 'after') throw new Error('lost snapshot write');",
                "field_unused_effect": "if (element.getAttribute('marker') !== 'done') throw new Error('lost unused effect');",
            }.get(name, "")
            observations.append(f"""var {variable} = (() => {{
  const element = observationElement({value});
  {"element.dataset = {bsConfig: 'value', bsConfigExtra: 'value', bsToggle: 'value', other: 'value'};" if name in DYNAMIC_CASES else "element.dataset = {bsConfig: 'a', bsConfigExtra: 'b', bsToggle: 'c', other: 'd'};" if name in FILTER_CASES else ""}
  const other = observationElement('different');
  other.setAttribute('other', 'second');
  let toggles = 0;
  const toggle = element.classList.toggle;
  element.classList.toggle = function(name, force) {{ toggles++; return toggle(name, force); }};
  const result = {name}(element, other);
  {effects}
  return result;
}})();""")
            expected.append(f"{variable}={'true' if bit == '1' else 'false'}\n")
            vm_expected.append(f"{variable}={'true' if vm_bit == '1' else 'false'}\n")
    oracle = declarations + strings.BOOLEAN_DOUBLE + "\n".join(observations) + "\n"
    path = args.work / "oracle.js"
    path.write_text(oracle)
    reference = run([args.reference, str(path)]).stdout
    path.write_text(
        oracle
        + "\n".join(
            f"console.log('observation{i:03}=' + observation{i:03});"
            for i in range(len(observations))
        )
    )
    node = run([args.node, str(path)]).stdout
    if reference != "".join(vm_expected) or node != "".join(expected):
        raise RuntimeError(f"class DOM source observations differ: {reference!r}, {node!r}")
    return len(observations)


def direct_receiver(args, name, ir, contract):
    """Move the source helper's explicit target to its equivalent receiver ABI."""
    original = ir.read_text()
    functions = {match[1]: match[0] for match in FUNCTION.finditer(original)}
    helpers = [symbol for symbol in functions if symbol.rsplit("$", 1)[0] == "directRead"]
    if len(helpers) != 1:
        raise RuntimeError(f"{name}: expected one complete directRead helper")
    symbol = helpers[0]
    entry, helper = functions[contract["entry"]], functions[symbol]
    closure = re.search(
        r"^    (%\w+) = ctjs.create_closure %arg2\["
        + symbol.rsplit("$", 1)[1]
        + r"\] this (%\w+)\n",
        entry,
        re.M,
    )
    if not closure or f"{closure[2]} = ctjs.constant #ctjs.undefined" not in entry:
        raise RuntimeError(f"{name}: helper lost its uncaptured source closure")
    callee, undefined = closure[1], closure[2]
    calls = []

    def rewrite(match):
        arguments = match[1].split(", ")
        if (
            len(arguments) != 5
            or arguments[2] != callee
            or any(
                f"{operand} = ctjs.constant #ctjs.undefined" not in entry
                for operand in arguments[:2]
            )
        ):
            raise RuntimeError(f"{name}: source helper invocation changed")
        direct = (
            f"ctjs.call_direct @{symbol}"
            f"({arguments[3]}, {undefined}, {undefined}, {arguments[4]})"
        )
        calls.append(direct)
        return direct

    rewritten = re.sub(r"ctjs.call_direct @" + re.escape(symbol) + r"\(([^)]*)\)", rewrite, entry)
    if len(calls) != (3 if name == "direct_order" else 1):
        raise RuntimeError(f"{name}: source helper call count changed")
    rewritten = rewritten.replace(closure[0], "")
    rewritten = re.sub(
        r"^    ctjs.root " + re.escape(callee) + r" in %\w+\n",
        "",
        rewritten,
        flags=re.M,
    )
    if re.search(re.escape(callee) + r"\b", rewritten):
        raise RuntimeError(f"{name}: direct helper retains a live closure use")
    if ", %arg3: !ctjs.value" not in helper or "ctjs.get_property %arg3[" not in helper:
        raise RuntimeError(f"{name}: explicit target ABI changed")
    lowered_helper = helper.replace(
        f"ctjs.func @{symbol}", f"ctjs.func private @{symbol}", 1
    ).replace(", %arg3: !ctjs.value", "", 1)
    lowered_helper = re.sub(r"%arg3\b", "%arg0", lowered_helper)
    normalized = original.replace(entry, rewritten).replace(helper, lowered_helper)
    output = args.work / f"{name}.receiver.mlir"
    output.write_text(normalized)
    return (
        output,
        dict(contract, module_sha256=host.fingerprint(args.opt, output)),
        {
            "symbol": symbol,
            "helper": lowered_helper,
            "closure": closure[0],
            "entry": rewritten,
            "call": calls[0],
            "undefined": undefined,
        },
    )


def check_direct_refusals(args, ir, contract, facts):
    original = ir.read_text()
    symbol, helper, call = (facts[key] for key in ("symbol", "helper", "call"))
    operands = call[call.index("(") + 1 : -1].split(", ")
    variants = {
        "public-target": original.replace(f"ctjs.func private @{symbol}", f"ctjs.func @{symbol}"),
        "captured-target": original.replace(
            helper, helper.replace("upvalue_count = 0", "upvalue_count = 1", 1)
        ),
        "observed-callee": original.replace(
            helper, helper.replace("ctjs.get_property %arg0[", "ctjs.get_property %arg2[", 1)
        ),
        "observed-new-target": original.replace(
            helper, helper.replace("ctjs.get_property %arg0[", "ctjs.get_property %arg1[", 1)
        ),
        "unknown-dom": original.replace(
            helper, helper.replace('#ctjs.string<"getAttribute">', '#ctjs.string<"unknown">', 1)
        ),
        "live-closure": original.replace(
            facts["entry"],
            facts["entry"].replace(
                f"{facts['undefined']} = ctjs.constant #ctjs.undefined\n",
                f"{facts['undefined']} = ctjs.constant #ctjs.undefined\n" + facts["closure"],
                1,
            ),
        ),
        "recursive-target": original.replace(
            helper,
            helper.replace(
                "    ctjs.frame_exit",
                "    %recursive_undefined = ctjs.constant #ctjs.undefined\n"
                f"    %recursive = ctjs.call_direct @{symbol}"
                "(%arg0, %recursive_undefined, %recursive_undefined, %arg4)\n"
                "    ctjs.frame_exit",
                1,
            ),
        ),
    }
    for name, changed in (
        ("callee-value", [operands[0], operands[1], operands[0], operands[3]]),
        ("new-target", [operands[0], operands[0], operands[2], operands[3]]),
        ("non-dom-receiver", [operands[1], *operands[1:]]),
    ):
        variants[name] = original.replace(
            call, f"ctjs.call_direct @{symbol}({', '.join(changed)})", 1
        )
    refusals = 0
    for name, text in variants.items():
        if text == original:
            raise RuntimeError(f"{name}: receiver refusal did not change the source")
        mutated = args.work / f"receiver-{name}.mlir"
        mutated.write_text(text)
        request = dict(contract, module_sha256=host.fingerprint(args.opt, mutated))
        for owned in (False, True):
            request["provider"] = "ctbrowser-dom-session-v1" if owned else "ctbrowser-dom-v1"
            dom.lower(args, mutated, request, f"receiver-{name}-{owned}", success=False)
            refusals += 1
    return refusals


def check_native(args, modules, optimize, compilers, includes, libraries):
    cases = CLASS_CASES | CASES
    for layout in ("explicit", "deduced"):
        headers, bodies, checks, expected = set(), [], [], []
        for name, owned, native in modules:
            label = f"{name}_{owned}_{optimize}_{layout}"
            if layout == "deduced":
                deduced = args.work / f"{label}.mlir"
                run([args.opt, str(native), "--ctnative-print-deduced", "-o", str(deduced)])
                native = deduced
            cpp, symbol = strings.emitted(
                args,
                native,
                label,
                callbacks=int(name in FILTER_CASES) + int(name in F_DATASET_CASES),
            )
            if name in F_DATASET_CASES and "ctnative::replace_uppercase<" not in cpp:
                raise RuntimeError(f"{label}: native output lost its original replacement callback")
            if name in UTF16_CASES and any(
                helper not in cpp
                for helper in ("ctbrowser::wtf8_to_utf16", "ctbrowser::utf16_to_wtf8")
            ):
                raise RuntimeError(f"{label}: native output lost its shared UTF-16 conversion")
            if name in FILTER_CASES and "ctnative::filter_strings<" not in cpp:
                raise RuntimeError(f"{label}: native output lost its original filter callback")
            if (
                name in ("class_dynamic_output", "class_dynamic_prefix")
                and "ctnative::assign_json_snapshot_property" not in cpp
            ):
                raise RuntimeError(f"{label}: native output lost its snapshot assignment")
            if re.search(r"__ctbrowser_class_defined|__proto__|__home__|invoke_callable", cpp):
                raise RuntimeError(f"{label}: native entry retained class metadata or dispatch")
            headers.update(re.findall(r"^#(?:include|define CTNATIVE_)[^\n]*", cpp, re.M))
            body = re.sub(r"^#(?:include|define CTNATIVE_)[^\n]*\n?", "", cpp, flags=re.M)
            bodies.append(f"namespace {label} {{\n{body}\n}}\n")
            entry = label + "::" + symbol
            setup = (
                f"{entry}_session session; auto & doc = session.document();"
                if owned
                else "atom_table atoms_owner; document doc{atoms_owner};"
            )
            two_elements = name not in CLASS_CASES or name in TWO_ELEMENT_CLASSES
            parameters = "element, other" if two_elements else "element"
            call = f"session.invoke({parameters})" if owned else f"{entry}({parameters})"
            check = (
                strings.BOOLEAN_RUN.replace("@SETUP@", setup)
                .replace("@CALL@", call)
                .replace(
                    "@CHECKS@",
                    ORDER_CHECKS if name == "direct_order" else FIELD_CHECKS.get(name, ""),
                )
            )
            if name in ("class_element", "class_unused_element", "class_unused_write"):
                check = check.replace(
                    "            const auto result =",
                    '            assert(doc.remove_attribute(node, atoms.intern("class")));\n'
                    "            const auto result =",
                )
            if two_elements:
                check = check.replace(
                    "        const auto state =",
                    """        const auto other_node = doc.create_element(atoms.intern("button"));
        const element_ref other{&doc, other_node};
        assert(doc.set_attribute(other_node, atoms.intern("x"), "different"));
        const auto state =""",
                ).replace(
                    "            const auto result =",
                    '            assert(doc.set_attribute(other_node, atoms.intern("other"), "second"));\n'
                    "            const auto result =",
                )
            if name in FILTER_CASES:
                check = check.replace(
                    "        const auto state =",
                    "\n".join(
                        f'        assert(doc.set_attribute(node, atoms.intern("data-{key}"), "value"));'
                        for key in ("bs-config", "bs-config-extra", "bs-toggle", "other")
                    )
                    + "\n        const auto state =",
                )
            checks.append(check)
            expected.extend("true\n" if bit == "1" else "false\n" for bit in cases[name][1])
        path = args.work / f"combined-{optimize}-{layout}.cpp"
        path.write_text(
            "\n".join(sorted(headers))
            + "\n#include <array>\n#include <cassert>\n#include <iostream>\n"
            "#include <optional>\n#include <string>\n#include <type_traits>\n"
            + "\n".join(bodies)
            + "\nusing namespace ctbrowser;\nint main() {\n"
            + "\n".join(checks)
            + "\n}\n"
        )
        for index, compiler in enumerate(compilers):
            binary = path.with_suffix(f".{index}")
            run([compiler, *FLAGS, *includes, str(path), *libraries, "-o", str(binary)])
            if dom.VM.search(run([args.nm, "-C", str(binary)]).stdout):
                raise RuntimeError("class DOM native program links Script/AOT")
            if run([str(binary)]).stdout != "".join(expected):
                raise RuntimeError("class DOM native observations disagree with source")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("translate", "opt", "clang", "node", "reference"):
        parser.add_argument("--" + name, required=True)
    for name in ("build", "include", "work"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--nm", default=shutil.which("nm"))
    args = parser.parse_args()
    args.work = args.work.resolve()
    args.work.mkdir(parents=True, exist_ok=True)
    vendor = args.include.parent / "vendor/bootstrap/bootstrap.bundle.js"
    assert BOOTSTRAP_M in vendor.read_text(), "Bootstrap M source pin changed"
    assert strings.BOOTSTRAP_F in vendor.read_text(), "Bootstrap F source pin changed"
    assert BOOTSTRAP_H in vendor.read_text(), "Bootstrap H source pin changed"
    assert FILTER_PREDICATE in BOOTSTRAP_H, "Bootstrap dataset predicate changed"
    observations = check_oracles(args)
    compilers = find_compilers()
    compilers[1] = args.clang
    includes, libraries = dom.link_options(args)
    prepared, refusals = [], 0
    for name, (body, _) in CASES.items():
        ir, contract = dom.prepare(args, name, source(name, body), 2, entry_name=name)
        if name in FIELD_CASES:
            normalized, refreshed, facts = ir, contract, {}
        else:
            normalized, refreshed, facts = direct_receiver(args, name, ir, contract)
        for owned in (False, True):
            request = dict(
                refreshed,
                provider="ctbrowser-dom-session-v1" if owned else "ctbrowser-dom-v1",
            )
            label = f"{name}-{owned}"
            prepared.append((name, owned, normalized, request))
            for control, changed in (
                ("missing-entry", dict(request, entry="missing$999")),
                ("stale", dict(request, module_sha256="0" * 64)),
            ):
                dom.lower(args, normalized, changed, f"{label}-{control}", success=False)
                refusals += 1
            dom.lower(args, normalized, request, f"{label}-budget", success=False, max_steps=0)
            refusals += 1
            if name == "direct_order":
                dom.lower(
                    args,
                    normalized,
                    dict(request, element_parameters=[0]),
                    f"{label}-missing-element",
                    success=False,
                )
                refusals += 1
        if name == "direct_read":
            refusals += check_direct_refusals(args, normalized, refreshed, facts)
    for name, body in FIELD_REFUSALS.items():
        ir, contract = dom.prepare(args, name, source(name, body), 1, entry_name=name)
        for owned in (False, True):
            request = dict(
                contract, provider="ctbrowser-dom-session-v1" if owned else "ctbrowser-dom-v1"
            )
            dom.lower(args, ir, request, f"{name}-{owned}", success=False)
            refusals += 1
    class_sources = {name: body for name, (body, _) in CLASS_CASES.items()} | CLASS_REFUSALS
    for name, body in class_sources.items():
        ir, contract = dom.prepare(
            args, name, source(name, body), 2 if name in TWO_ELEMENT_CLASSES else 1, entry_name=name
        )
        check_mutable_helper(ir.read_text())
        if "ctjs.construct" not in ir.read_text() or '"prototype"' not in ir.read_text():
            raise RuntimeError(f"{name}: source lost ordinary class construction")
        if name.startswith("class_unused_cell_"):
            helpers = [
                match[0]
                for match in FUNCTION.finditer(ir.read_text())
                if match[1].rsplit("$", 1)[0] == "unusedCell"
            ]
            if len(helpers) != 1 or any(
                operation not in helpers[0]
                for operation in ("ctjs.create_cell", "ctjs.cell_get", "ctjs.cell_set")
            ):
                raise RuntimeError(f"{name}: unused helper lost its original local cell operations")
        for owned in (False, True):
            request = dict(
                contract,
                provider="ctbrowser-dom-session-v1" if owned else "ctbrowser-dom-v1",
                initial_intrinsics=["__ctbrowser_class_defined"]
                + (["Error"] if name.startswith("class_error_") else [])
                + (["Number"] if name in NUMBER_CASES or name in NUMBER_REFUSALS else [])
                + (["JSON", "decodeURIComponent"] if name in M_CASES or name in M_REFUSALS else [])
                + (["__ctbrowser_regexp"] if name in F_CASES or name in F_REFUSALS else []),
            )
            if (
                name in UTF16_CASES
                or name in UTF16_REFUSALS
                or name == "class_f_matching_lowercase_replaced"
            ):
                request["initial_intrinsics"] += ["String"]
            if name in F_DATASET_CASES or name in F_DATASET_REFUSALS:
                request["initial_intrinsics"] += ["RegExp"]
            if name in FILTER_CASES or name in FILTER_REFUSALS:
                request["initial_intrinsics"] += FILTER_IDENTITIES
                request["dataset_parameters"] = [0]
                if name in DYNAMIC_CASES or name in DYNAMIC_REFUSALS:
                    request["initial_intrinsics"] += DYNAMIC_ITERATION
                if name == "class_dynamic_prefix":
                    request["initial_intrinsics"] += ["RegExp", "__ctbrowser_regexp"]
                if (
                    name in ("class_filter_full_h", "class_dynamic_original")
                    or name in FULL_H_CASES
                    or name in FULL_H_REFUSALS
                ):
                    request["initial_intrinsics"] += [
                        "Number",
                        "JSON",
                        "decodeURIComponent",
                        "__ctbrowser_regexp",
                        "RegExp",
                        "__ctbrowser_for_of_open",
                        "__ctbrowser_iter_next",
                        "__ctbrowser_iter_close",
                    ]
                    request["initial_intrinsics"] = list(
                        dict.fromkeys(request["initial_intrinsics"])
                    )
            steps = (
                1000000
                if name in M_CASES
                or name in M_REFUSALS
                or name in DYNAMIC_CASES
                or name in DYNAMIC_REFUSALS
                else 100000
            )
            classes.prepare(args, f"{name}-{owned}", ir, request, success=False)
            refusals += 1
            if name not in CLASS_CASES:
                diagnostic = dom.lower(
                    args, ir, request, f"{name}-{owned}", success=False, max_steps=steps
                )
                if name.startswith("class_static_shared_dom_"):
                    reason = (
                        "class initialization source contains an unknown call, binding or "
                        "reflective effect (op ctjs.call)"
                        if name == "class_static_shared_dom_transitive"
                        else "static method reaches a helper requiring DOM body proof"
                    )
                    if reason not in diagnostic:
                        raise RuntimeError(
                            f"{name}: static method lost its shared DOM helper refusal"
                        )
                refusals += 1
                continue
            prepared.append((name, owned, ir, request))
            # Preserve the original optional-Error requests as successful lowerings.
            dom.lower(
                args,
                ir,
                dict(
                    request,
                    initial_intrinsics=list(
                        dict.fromkeys(request["initial_intrinsics"] + ["Error"])
                    ),
                ),
                f"{name}-{owned}-extra-authority",
                max_steps=steps,
            )
            if name == "class_error_unused":
                dom.lower(
                    args,
                    ir,
                    dict(request, initial_intrinsics=["__ctbrowser_class_defined"]),
                    f"{name}-{owned}-undeclared-error",
                    success=False,
                )
                refusals += 1
            # Keep the old mixed request verbatim: only sources requiring Error
            # or Number still lack an identity. Also prove the full mixed request.
            mixed = ["__ctbrowser_class_defined", "Object"]
            missing_identity = (
                name.startswith("class_error_")
                or name in NUMBER_CASES
                or name in F_CASES
                or name in FILTER_CASES
                or name in UTF16_CASES
            )
            dom.lower(
                args,
                ir,
                dict(request, initial_intrinsics=mixed),
                f"{name}-{owned}-mixed-dom-authority",
                max_steps=steps,
                success=not missing_identity,
            )
            refusals += int(missing_identity)
            dom.lower(
                args,
                ir,
                dict(
                    request,
                    initial_intrinsics=list(
                        dict.fromkeys(request["initial_intrinsics"] + ["Object"])
                    ),
                ),
                f"{name}-{owned}-complete-mixed-authority",
                max_steps=steps,
            )
            if name in FILTER_CASES:
                for identity in FILTER_IDENTITIES + (
                    DYNAMIC_ITERATION if name in DYNAMIC_CASES else []
                ):
                    dom.lower(
                        args,
                        ir,
                        dict(
                            request,
                            initial_intrinsics=[
                                i for i in request["initial_intrinsics"] if i != identity
                            ],
                        ),
                        f"{name}-{owned}-missing-{identity}",
                        success=False,
                        max_steps=steps,
                    )
                    refusals += 1
                dom.lower(
                    args,
                    ir,
                    dict(request, dataset_parameters=[]),
                    f"{name}-{owned}-missing-dataset",
                    success=False,
                    max_steps=steps,
                )
                refusals += 1
            if name in NUMBER_CASES:
                for control, identities in (
                    (
                        "undeclared-number",
                        [i for i in request["initial_intrinsics"] if i != "Number"],
                    ),
                    ("duplicate-number", request["initial_intrinsics"] + ["Number"]),
                ):
                    dom.lower(
                        args,
                        ir,
                        dict(request, initial_intrinsics=identities),
                        f"{name}-{owned}-{control}",
                        success=False,
                        max_steps=steps,
                    )
                    refusals += 1
            if name in M_CASES:
                for identity in ("JSON", "decodeURIComponent"):
                    for control, identities in (
                        ("missing", [i for i in request["initial_intrinsics"] if i != identity]),
                        ("duplicate", request["initial_intrinsics"] + [identity]),
                    ):
                        dom.lower(
                            args,
                            ir,
                            dict(request, initial_intrinsics=identities),
                            f"{name}-{owned}-{control}-{identity}",
                            success=False,
                            max_steps=steps,
                        )
                        refusals += 1
            for control, changed in (
                ("missing-entry", dict(request, entry="missing$999")),
                ("missing-element", dict(request, element_parameters=[])),
                ("stale", dict(request, module_sha256="0" * 64)),
                ("no-authority", dict(request, initial_intrinsics=[])),
                (
                    "duplicate-error",
                    dict(
                        request, initial_intrinsics=["__ctbrowser_class_defined", "Error", "Error"]
                    ),
                ),
            ):
                dom.lower(
                    args, ir, changed, f"{name}-{owned}-{control}", success=False, max_steps=steps
                )
                refusals += 1
            dom.lower(args, ir, request, f"{name}-{owned}-budget", success=False, max_steps=0)
            refusals += 1
            if name in TWO_ELEMENT_CLASSES:
                dom.lower(
                    args,
                    ir,
                    dict(request, element_parameters=[0]),
                    f"{name}-{owned}-missing-second-element",
                    success=False,
                )
                refusals += 1
    for optimize in (False, True):
        modules = [
            (
                name,
                owned,
                dom.lower(
                    args,
                    ir,
                    contract,
                    f"{name}-{owned}-{optimize}",
                    optimize=optimize,
                    max_steps=1000000 if name in M_CASES or name in DYNAMIC_CASES else 100000,
                ),
            )
            for name, owned, ir, contract in prepared
        ]
        check_native(args, modules, optimize, compilers, includes, libraries)
    print(
        f"DOM classes, receivers and fields: {observations} Node/interpreter observations, "
        f"8 combined native executions, {refusals} refusals"
    )


if __name__ == "__main__":
    main()
