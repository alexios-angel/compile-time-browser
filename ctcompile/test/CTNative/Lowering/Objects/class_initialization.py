#!/usr/bin/env python3
"""Keep class initialization, mutable helper effects and native admission distinct."""

from CTNative.Lowering.Objects.class_initialization_inputs import *


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("translate", "opt", "node", "reference"):
        parser.add_argument("--" + name, required=True)
    for name in ("fixtures", "work"):
        parser.add_argument("--" + name, type=Path, required=True)
    args = parser.parse_args()
    args.work.mkdir(parents=True, exist_ok=True)
    vendor = Path(__file__).resolve().parents[5] / "ctbrowser/vendor/bootstrap/bootstrap.bundle.js"
    bootstrap = vendor.read_text()
    defaults = (args.fixtures / "static-defaults.js").read_text()
    for name in ("Default", "DefaultType"):
        body = f"        static get {name}() {{\n            return {{}}\n        }}"
        if body not in bootstrap or body not in defaults:
            raise RuntimeError(f"Bootstrap Config {name} getter source pin changed")
    # Keep every original Config method, even though this entry only reads Default.
    # Its iterator/throw exits remain a separate proof boundary from local dispatch.
    start = bootstrap.index("    class W {")
    end = bootstrap.index("    class B extends W {", start)
    (args.fixtures / "bootstrap-config-defaults.js").write_text(
        "function configDefaults() {\n"
        + bootstrap[start:end]
        + "\n    var instance = new W();\n    var result = W.Default;\n"
        "    result.n = 7;\n    return result.n;\n}\nvar a = configDefaults();\n"
    )
    # Include the original helper separately; retain the W-only refusal above.
    helper = bootstrap.split("        r = ", 1)[1].split(",\n        a = ", 1)[0]
    declaration = "const r = " + helper + ";\n"
    # Keep the complete original W/B classes and helpers. A missing element
    # exercises B's real early exit after super, without invented H calls.
    element = bootstrap.split("        a = ", 1)[1].split(",\n        l = ", 1)[0]
    base_end = bootstrap.index("    const z = t => {", end)
    (args.fixtures / "bootstrap-base.js").write_text(
        "function bootstrapBase() {\n"
        + declaration
        + "const a = "
        + element
        + ";\n"
        + bootstrap[start:base_end]
        + "\n    var instance = new B(null, null);\n"
        "    return instance._element === undefined ? 7 : 9;\n}\nvar a = bootstrapBase();\n"
    )
    (args.fixtures / "bootstrap-config-r-defaults.js").write_text(
        declaration + (args.fixtures / "bootstrap-config-defaults.js").read_text()
    )
    # Defining H and its original normalization helpers does not grant DOM or
    # callable-holder authority to the class proof. Keep all four H methods.
    helpers = bootstrap[bootstrap.index("    function M(t) {") : start]
    combined = (args.fixtures / "bootstrap-config-r-defaults.js").read_text()
    (args.fixtures / "bootstrap-config-r-h-defaults.js").write_text(
        combined.replace("var a = configDefaults();", helpers + "\nvar a = configDefaults();")
    )
    (args.fixtures / "bootstrap-r.js").write_text(
        declaration + "function probe() { class Shape { read(t) { return r(t); } } "
        "var instance = new Shape(); return instance.read(null) ? 9 : 7; } var a = probe();\n"
    )
    helper_checked, helper_refused = check_helper_guards(args, declaration)
    key_checked = key_refused = 0
    refusals = 0
    preparation_refusals = 0
    checked = prepared_refusals = 0
    cutoffs = {}
    for name, (node_expected, reference_expected) in OBSERVATIONS.items():
        source = args.fixtures / f"{name}.js"
        node = run([args.node, "-e", NODE, str(source)], success=node_expected is not None)
        reference = run([args.reference, str(source)], success=reference_expected is not None)
        if node_expected is not None and (node.stdout != f"a={node_expected}\n" or node.stderr):
            raise RuntimeError(f"{name}: Node observation changed\n{node.stdout}{node.stderr}")
        reference_output = f"a={reference_expected}\n"
        if name == "static-global-effect":
            reference_output += "count=3\n"
        if name == "static-error-replaced":
            reference_output = "Error=9\n" + reference_output
        if reference_expected is not None and reference.stdout != reference_output:
            raise RuntimeError(
                f"{name}: interpreter observation changed\n{reference.stdout}{reference.stderr}"
            )
        raw = args.work / f"{name}.raw.mlir"
        imported = run([args.translate, "--ctbrowser-js-to-ctjs", str(source), "-o", str(raw)])
        text = raw.read_text()
        if "ctjs.skipped" in text or "is not compiled:" in imported.stderr:
            raise RuntimeError(f"{name}: importer skipped source functions")
        check_mutable_helper(text)
        functions = len(FUNCTION.findall(text))
        structured = args.work / f"{name}.structured.mlir"
        run(
            [
                args.opt,
                str(raw),
                "--ctjs-resolve-globals",
                "--ctjs-lift-to-scf",
                # Explicit super guards can put grouped SCF results in a
                # cf.switch default edge; its custom parser rejects %n#i there.
                # Generic assembly preserves every operation and operand.
                *(
                    ["--mlir-print-op-generic"]
                    if name.startswith(("inherited-", "override-")) or name == "bootstrap-base"
                    else []
                ),
                "-o",
                str(structured),
            ]
        )
        manifest = dict(
            host.manifest(args.opt, structured),
            initial_intrinsics=["__ctbrowser_class_defined"],
        )
        if name.startswith(("inherited", "override-")) or name == "bootstrap-base":
            # Declare the mutable implementation hooks emitted by the source.
            # Their identities do not establish ancestry or super semantics.
            manifest["initial_intrinsics"] += [
                "__ctbrowser_class_heritage",
                "__ctbrowser_bind_this",
                "__ctbrowser_init_fields",
                "__ctbrowser_super_get",
            ]
        if name.startswith(("static-throw-", "static-error-")) or name in (
            "bootstrap-base",
            "bootstrap-config-defaults",
            "bootstrap-config-r-defaults",
            "bootstrap-config-r-h-defaults",
        ):
            manifest["initial_intrinsics"].append("Error")
        if name in (
            "method-increment-dispatch",
            "method-decrement-dispatch",
            "method-counter-ambient",
        ):
            if (
                "ctjs.binary_static add" not in text
                or "ctjs.binary_static add" not in structured.read_text()
            ):
                raise RuntimeError(f"{name}: import lost the static counter operation")
        if name in (
            "method-dispatch",
            "method-increment-dispatch",
            "method-decrement-dispatch",
            "receiver-default-dispatch",
            "global-holder-dispatch",
        ):
            for operation in (
                "scf.index_switch",
                "arith.index_castui",
                "arith.trunci",
                "ub.poison",
            ):
                if operation not in structured.read_text():
                    raise RuntimeError(f"method dispatch no longer exercises {operation}")
        diagnostic = {
            "inherited-helper-changing": "class local cell has changing writes",
            "inherited-helper-identity": "captured helper escapes its ordinary local call",
            "inherited-helper-effect": "unknown call, binding or reflective effect",
            "inherited-helper-receiver": "captured helper observes its implicit receiver or new.target",
            "inherited-helper-newtarget": "captured helper observes its implicit receiver or new.target",
            "captured-helper-replaced": "class local cell has changing writes",
            "inherited-helper-ambient": "unknown call, binding or reflective effect",
            "captured-helper-identity": "captured helper escapes its ordinary local call",
            "captured-helper-receiver": "captured helper observes its implicit receiver or new.target",
            "captured-helper-excess": "captured helper escapes its ordinary local call",
            "captured-helper-nested": "class method capture is not its constructor or an inert sibling helper",
            "inherited-super-helper": "super method requires a proved capture-free linear base target",
            "inherited": "derived class requires receiver-preserving super normalization",
            "inherited-explicit": "derived class requires receiver-preserving super normalization",
            "override-ambient": "unknown call, binding or reflective effect",
            "override-shadowed-receiver": "class method is observed or shadowed",
            "override-hidden-ancestor": "class method is observed or shadowed",
            "override-unused-constructor": "inherited receiver getters require per-leaf target proof",
            "inherited-method-unused-constructor": "inherited receiver getters require per-leaf target proof",
            "inherited-method-leaf-shadow": "class method is observed or shadowed",
            "inherited-method-base-shadow": "class method is observed or shadowed",
            "inherited-method-ambient": "unknown call, binding or reflective effect",
            "inherited-method-getter": "inherited receiver getters require per-leaf target proof",
            "inherited-method-shadow": "class method is observed or shadowed",
            "bootstrap-base": "class method capture is not its constructor or an inert sibling helper",
            "method-counter-ambient": "unknown call, binding or reflective effect",
            "method-dispatch-ambient": "unknown call, binding or reflective effect",
            "method-dispatch-shadow": "class method is observed or shadowed",
            "method-captured-class-ambient": "unknown call, binding or reflective effect",
            "method-throw-ambient": "unknown call, binding or reflective effect",
            "method-throw-object": "unknown call, binding or reflective effect",
            "method-throw-parameter": "unknown call, binding or reflective effect",
            "bootstrap-config-defaults": 'unknown call, binding or reflective effect (global "r")',
            "bootstrap-config-r-defaults": 'unknown call, binding or reflective effect (global "H")',
            "bootstrap-config-r-h-defaults": "unknown call, binding or reflective effect",
            "static-throw-ambient": "static getter body is not a closed expression",
            "static-throw-object": "static getter throw needs a literal or declared Error payload",
            "static-error-return": "declared Error payload escapes its throw",
            "static-error-replaced": "declared intrinsic binding is replaced by source",
            "static-error-coercion": "static getter throw needs a literal or declared Error payload",
            "static-error-method": "unknown call, binding or reflective effect",
            "instance-default-replacement": "primitive constructor return",
        }.get(name, "")
        prepared = prepare(
            args,
            name,
            structured,
            manifest,
            success=name in POSITIVES | PREPARED_ONLY,
            diagnostic=diagnostic,
        )
        preparation_refusals += name not in POSITIVES | PREPARED_ONLY
        if name == "inherited-explicit":
            preparation_refusals += check_ancestry_inputs(args, structured, manifest)
            preparation_refusals += check_super_inputs(args, structured, manifest)
            check_super_roots(
                args,
                structured,
                manifest,
                "super constructor declarations, roots and global writes remain unsupported",
            )
            preparation_refusals += 1
        if name == "inherited-method":
            # A fresh leaf prototype may precede the base's method producers.
            # Preparation must insert inherited slots after those definitions.
            text = structured.read_text()
            objects = re.findall(r'^ +%\w+ = "ctjs.create_object"\(\)[^\n]+\n', text, re.M)
            if len(objects) != 2:
                raise RuntimeError("inherited method control lost its two prototypes")
            moved = args.work / "inherited-method-hoisted.mlir"
            moved.write_text(
                text.replace(objects[1], "").replace(objects[0], objects[1] + objects[0])
            )
            prepared = prepare(
                args,
                "inherited-method-hoisted",
                moved,
                dict(manifest, module_sha256=host.fingerprint(args.opt, moved)),
                success=True,
            )
        if name == "bootstrap-r":
            key_checked, key_refused = check_prototype_keys(args, prepared)
        if name == "global-holder-chain":
            for label, request in (
                ("root", {"roots": [{"binding": "H", "properties": ["read"]}]}),
                ("observation", {"observations": ["a", "H"]}),
            ):
                prepare(
                    args,
                    f"holder-host-{label}",
                    structured,
                    dict(manifest, **request),
                    success=False,
                    diagnostic="global callable holder is requested by the host",
                )
                preparation_refusals += 1
        if name in PREPARED_ONLY | GLOBAL_HOLDERS:
            before, after = structured.read_text(), prepared.read_text()
            if name in ("override-different-leaves", "inherited-helper-order"):
                operations = ()
            elif name in GLOBAL_HOLDERS:
                operations = ()
                for operation in ('ctjs.load_global "H"', 'ctjs.store_global "H"'):
                    if operation not in before or operation in after:
                        raise RuntimeError(f"{name}: preparation did not remove the proved holder")
                converted = before.count("ctjs.call ") - after.count("ctjs.call ") - 1
                if (
                    converted <= 0
                    or after.count("ctjs.call_direct")
                    != before.count("ctjs.call_direct") + converted
                ):
                    raise RuntimeError(f"{name}: holder calls did not become direct helpers")
            else:
                operations = (
                    ("ctjs.call_direct",)
                    if name in ("local-helper-order", "bootstrap-r")
                    else (
                        ("cf.switch", "ctjs.throw")
                        if name.startswith("method-")
                        else ("ctjs.throw",)
                    )
                )
            for operation in operations:
                if not before.count(operation) or before.count(operation) != after.count(operation):
                    raise RuntimeError(
                        f"{name}: preparation changed the original {operation} exits"
                    )
            # Preparation removes the two reads; unused key literals may remain.
            if name == "method-throw-default" and (
                before.count("ctjs.get_property") - after.count("ctjs.get_property") != 2
            ):
                raise RuntimeError("throwing method retained a constructor getter read")
            if name.startswith("static-throw-"):
                getters = set(re.findall(r"ctjs.func @(fn\$\d+)\(", before))
                calls = set(re.findall(r"ctjs.call_direct @(fn\$\d+)\(", after))
                if not getters or calls != getters or "ctjs.define_accessor" in after:
                    raise RuntimeError(
                        f"{name}: throwing getter lost its direct call or retained its accessor"
                    )
        if name == "static-throw-error":
            prepare(
                args,
                "undeclared-error",
                structured,
                dict(manifest, initial_intrinsics=["__ctbrowser_class_defined"]),
                success=False,
                diagnostic="static getter throw needs a literal or declared Error payload",
            )
            preparation_refusals += 1
        if name == "static-chain":
            preparation_refusals += check_getter_parent(args, structured, manifest, prepared)
        if name == "method-captured-class-name":
            preparation_refusals += check_class_capture_inputs(args, structured, manifest)
        if name == "empty":
            check_overflow_input(args, structured)
            for label, control, options in (
                ("no-authority", dict(manifest, initial_intrinsics=[]), ""),
                ("stale", dict(manifest, module_sha256="0" * 64), ""),
                ("budget", manifest, "max-steps=0"),
            ):
                prepare(args, label, structured, control, success=False, options=options)
                preparation_refusals += 1
        if name in (
            "captured-helper-constructor",
            "empty",
            "method",
            "method-chain-order",
            "method-loop",
            "method-dispatch",
            "method-increment-dispatch",
            "method-dispatch-throw",
            "receiver-default-dispatch",
            "method-constructor-order",
            "method-captured-class-name",
            "static-chain",
            "static-repeated",
            "static-forward-chain",
            "static-defaults-chain",
            "static-throw-chain",
            "local-helper-branches",
            "local-holder-arrow",
            "global-holder-chain",
        ):
            cutoffs[name] = check_proof_inputs(args, structured, manifest, prepared, name)
            preparation_refusals += 4
        if name in ("inherited-super-order", "inherited-helper-distinct"):
            check_super_roots(
                args,
                structured,
                manifest,
                (
                    "super constructor declarations, roots and global writes remain unsupported"
                    if name == "inherited-helper-distinct"
                    else "super method target has unsupported control flow, roots or declarations"
                ),
            )
            cutoffs[name] = check_proof_budget(args, structured, manifest, prepared, name)
            preparation_refusals += 2
        for optimize in (False, True):
            native = args.work / f"{name}.{optimize}.untrusted.mlir"
            run(
                [
                    args.opt,
                    str(raw),
                    "--ctjs-resolve-globals",
                    "--ctjs-lift-to-scf",
                    f"--ctnative-lower-to-emitc=optimize={str(optimize).lower()}",
                    "-o",
                    str(native),
                ]
            )
            check_refusal(f"{name}/{optimize}/untrusted", native.read_text(), functions)
            if name == "helper-global-alias":
                check_mutable_helper(native.read_text())
            refusals += 1
            if name in POSITIVES | PREPARED_ONLY:
                native = args.work / f"{name}.{optimize}.trusted.mlir"
                run(
                    [
                        args.opt,
                        str(prepared),
                        f"--ctnative-lower-to-emitc=optimize={str(optimize).lower()}",
                        "-o",
                        str(native),
                    ]
                )
                if name in PREPARED_ONLY and not (name == "bootstrap-r" and optimize):
                    native_text = native.read_text()
                    check_refusal(name, native_text, len(FUNCTION.findall(prepared.read_text())))
                    if name == "inherited-helper-order" and (
                        "an object literal passed to a direct call as an argument"
                        not in native_text
                    ):
                        raise RuntimeError("helper mutation lost its argument ownership refusal")
                    if len(re.findall(r"^\s*ctjs.throw ", native_text, re.M)) != len(
                        re.findall(r"^\s*ctjs.throw ", prepared.read_text(), re.M)
                    ):
                        raise RuntimeError(f"{name}: native refusal changed the source throw exits")
                    prepared_refusals += 1
                else:
                    checked += check_executable(args, f"{name}.{optimize}", native, node_expected)
    plain_checked, plain_refused = check_constructed_methods(args)
    print(
        f"class initialization controls: {len(OBSERVATIONS)} source observations, "
        f"{checked} native executions, {refusals} unprepared refusals, "
        f"{preparation_refusals} preparation refusals, first complete budgets {cutoffs}"
    )
    print(
        f"constructed method controls: {plain_checked} native executions, {plain_refused} refusals"
    )
    print(f"original r guards: {helper_checked} native executions, {helper_refused} refusals")
    print(f"prototype keys: {key_checked} native executions, {key_refused} refusals")
    print(
        f"prepared source controls: {prepared_refusals} native refusals with original calls/exits"
    )


if __name__ == "__main__":
    main()
