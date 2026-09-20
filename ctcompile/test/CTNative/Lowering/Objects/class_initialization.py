#!/usr/bin/env python3
"""Keep class initialization, mutable helper effects and native admission distinct."""

from CTNative.Lowering.Objects.class_initialization_inputs import *

OWN_FIELDS = {
    "own-fields-length": (2, 2),
    "own-fields-order": (113, 113),
    "own-fields-clear": (11, 11),
    "own-fields-effects": (246, 246),
    "own-fields-boxed-key": (11, 11),
    "own-fields-loop": (11, 11),
    "own-fields-conditional": (1, 1),
    "own-fields-dynamic": (1, 1),
    "own-fields-new-field": (2, 2),
    "own-fields-numeric-key": (7, 7),
    "own-fields-receiver-escape": (1, 1),
    "own-fields-snapshot-escape": (7, 7),
    "own-fields-callback": (7, 7),
    "own-fields-object-replaced": (1, 1),
    "own-fields-helper-replaced": (1, 1),
    "inherited-own-fields-collision": (7, 7),
    "own-fields-unused-ambient": (7, 7),
    "own-fields-replacement-return": (7, 7),
}
OBSERVATIONS.update(OWN_FIELDS)
OBSERVATIONS["bootstrap-base-data"] = (7, 7)
POSITIVES.update(
    {
        "own-fields-length",
        "own-fields-order",
        "own-fields-clear",
        "own-fields-effects",
        "own-fields-boxed-key",
        "own-fields-loop",
        "inherited-own-fields-loop",
    }
)


def check_nested_helper_root(args, source, manifest, name="nested-helper-root"):
    # Ordinary helpers retain their frames across direct-call conversion.
    # Root only the captured wrapper, leaving every constructor frame untouched.
    functions = re.compile(r"^  ctjs.func\b[^\n]*\n.*?^  }\n", re.M | re.S)
    text = source.read_text()
    helpers = [
        body
        for body in functions.findall(text)
        if "ctjs.load_upvalue " in body and "ctjs.set_property " not in body
    ]
    if len(helpers) != 1:
        raise RuntimeError("nested root control lost its unique captured wrapper")
    helper = helpers[0]
    symbol = re.search(r"ctjs.func\s+@([^\s(]+)", helper)[1]
    rooted, count = re.subn(
        r"(^[ \t]*)ctjs.frame_exit (%\w+)",
        r"\1%helper_root = ctjs.constant #ctjs.undefined\n"
        r"\1ctjs.root %helper_root in \2\n\g<0>",
        helper,
        flags=re.M,
    )
    if count != 1:
        raise RuntimeError("nested root control lost its single helper frame exit")
    path = args.work / f"{name}.mlir"
    path.write_text(text.replace(helper, rooted, 1))
    prepared = prepare(
        args,
        name,
        path,
        dict(manifest, module_sha256=host.fingerprint(args.opt, path)),
        success=True,
    )
    after = prepared.read_text()
    retained = [body for body in functions.findall(after) if f"@{symbol}(" in body.splitlines()[0]]
    if len(retained) != 1 or f"ctjs.call_direct @{symbol}(" not in after:
        raise RuntimeError("nested root control lost its direct helper identity")
    if any(
        retained[0].count(operation) != rooted.count(operation)
        for operation in ("ctjs.frame_enter", "ctjs.frame_exit", "ctjs.root ")
    ):
        raise RuntimeError("nested helper preparation discarded its live frame or root")
    return prepared


def check_borrowed_helper_inputs(args, prepared):
    text = prepared.read_text()
    symbol = re.search(r"ctjs.func private @(fn\$\d+)\(", text)[1]
    call = re.search(rf"(?m)^(\s*)%\w+ = ctjs.call_direct @{re.escape(symbol)}\(([^\n]+)\)$", text)
    if not call:
        raise RuntimeError("borrowed helper lost its private direct call")
    operands = call[2].split(", ")
    operands[3] = operands[0]  # An additional caller passes undefined, not an object.
    extra = f"{call[1]}%borrow_bad = ctjs.call_direct @{symbol}({', '.join(operands)})"
    variants = {
        "module-reference": text.replace(
            "module attributes {", f"module attributes {{test.borrow_ref = @{symbol}, ", 1
        ),
        "public-helper": text.replace(f"ctjs.func private @{symbol}(", f"ctjs.func @{symbol}(", 1),
        "mixed-helper-call": text[: call.end()] + "\n" + extra + text[call.end() :],
    }
    for label, changed in variants.items():
        source = args.work / f"borrow-{label}.mlir"
        source.write_text(changed)
        output = args.work / f"borrow-{label}.native.mlir"
        run([args.opt, str(source), "--ctnative-lower-to-emitc=optimize=false", "-o", str(output)])
        check_refusal(label, output.read_text(), len(FUNCTION.findall(changed)))
    return len(variants)


def check_record_map_inputs(args, prepared):
    text = prepared.read_text()
    keys = dict(re.findall(r'(%\w+) = ctjs.constant #ctjs.string<"([^"]*)">', text))
    methods = {
        result: (owner, keys.get(key))
        for result, owner, key in re.findall(r"(%\w+) = ctjs.get_property (%\w+)\[(%\w+)\]", text)
    }
    calls = list(re.finditer(r"(?m)^\s*(%\w+) = ctjs.call (%\w+)\(([^\n]+)\)$", text))
    get = next(call for call in calls if methods.get(call[2], (None, None))[1] == "get")
    put = next(call for call in calls if methods.get(call[2], (None, None))[1] == "set")
    payload = put[3].split(", ")[-1]
    key = put[3].split(", ")[1]
    variants = {
        "binding-replaced": text[: get.end()]
        + f'\n    ctjs.store_global "Map", {key}'
        + text[get.end() :],
        "alias-published": text[: get.end()]
        + f'\n    ctjs.store_global "saved", {get[1]}'
        + text[get.end() :],
        "missing-read": text[: get.start()]
        + '\n    %missing_key = ctjs.constant #ctjs.string<"missing">'
        + get[0].replace(get[3].split(", ")[-1], "%missing_key")
        + text[get.end() :],
        "mixed-payload": text[: put.start()]
        + put[0].replace(", " + payload + ")", ", " + put[3].split(", ")[1] + ")")
        + text[put.end() :],
    }
    forged = (
        text[: get.end()]
        + "\n    %sink_undefined = ctjs.constant #ctjs.undefined"
        + f"\n    %sink_result = ctjs.call_direct @record_alias_sink({get[1]}, "
        "%sink_undefined, %sink_undefined) {ctnative.receiver}" + text[get.end() :]
    )
    end = forged.rfind("}")
    variants["forged-receiver-publication"] = (
        forged[:end] + "  ctjs.func private @record_alias_sink(%self: !ctjs.value, "
        "%new_target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value "
        "attributes {ctnative.receiver, upvalue_count = 0 : i32} {\n"
        '    ctjs.store_global "saved", %self\n'
        "    %undefined = ctjs.constant #ctjs.undefined\n"
        "    ctjs.return %undefined\n  }\n" + forged[end:]
    )
    for label, changed in variants.items():
        # Rejected live contents cannot inherit a previous record Map proof.
        changed = re.sub(
            r"(?m)^(\s*%\w+ = ctjs.construct %\w+\(%\w+\))$",
            r"\1 {ctnative.map_records, ctnative.map_site}",
            changed,
            count=1,
        )
        source = args.work / f"record-map-{label}.mlir"
        source.write_text(changed)
        for optimize in (False, True):
            output = args.work / f"record-map-{label}.{optimize}.native.mlir"
            run(
                [
                    args.opt,
                    str(source),
                    f"--ctnative-lower-to-emitc=optimize={str(optimize).lower()}",
                    "-o",
                    str(output),
                ]
            )
            check_refusal(label, output.read_text(), len(FUNCTION.findall(changed)))
            if label == "binding-replaced" and (
                "standard Map binding is assigned in this program" not in output.read_text()
            ):
                raise RuntimeError(
                    "retained records bypassed the final standard Map identity proof"
                )


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
    # Add the original shared Map and complete Data holder without changing the
    # existing ambient-e control. Only the declaration separator becomes a ';'.
    data = (
        bootstrap[
            bootstrap.index("    const t = new Map,") : bootstrap.index(
                '        i = "transitionend",'
            )
        ]
        .rstrip()
        .removesuffix(",")
        + ";\n"
    )
    (args.fixtures / "bootstrap-base-data.js").write_text(
        (args.fixtures / "bootstrap-base.js")
        .read_text()
        .replace("function bootstrapBase() {\n", "function bootstrapBase() {\n" + data, 1)
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
                    if name.startswith(("inherited-", "override-", "bootstrap-base"))
                    or name.startswith("class-map-inherited")
                    or name
                    in (
                        "class-map-record-alias-method-inherited",
                        "class-map-record-alias-snapshot-inherited",
                    )
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
        if name.startswith("class-map-") or name == "bootstrap-base-data":
            manifest["initial_intrinsics"].append("Map")
        if name == "own-fields-boxed-key":
            lifted = structured.read_text()
            calls = set(re.findall(r"(%\w+) = ctjs.call ", lifted))
            scalar_reads = [
                result
                for result, owner in re.findall(
                    r"(%\w+) = ctjs.get_property (%\w+)\[%\w+\]", lifted
                )
                if owner in calls
            ]
            if not any(
                re.search(r"ctjs.cell_set %\w+, " + re.escape(value) + r"\b", lifted)
                for value in scalar_reads
            ):
                raise RuntimeError("own-field scalar snapshot lost its boxed local producer")
        if (
            name in OWN_FIELDS
            or name.startswith("class-map-record-alias-snapshot-")
            or name == "class-map-inherited-early-snapshot"
            or name.startswith("own-fields-branch-")
            or name.startswith("inherited-own-fields-")
            or name.startswith("bootstrap-base")
        ):
            manifest["initial_intrinsics"].append("Object")
        if name in (
            "own-fields-loop",
            "inherited-own-fields-loop",
            "class-map-record-alias-snapshot-dispose",
        ) or name.startswith("inherited-own-fields-iterate-"):
            manifest["initial_intrinsics"] += [
                "Array",
                "__ctbrowser_for_of_open",
                "__ctbrowser_iter_next",
                "__ctbrowser_iter_close",
            ]
        if name.startswith(
            ("inherited", "override-", "bootstrap-base", "class-map-inherited")
        ) or name in (
            "class-map-record-alias-method-inherited",
            "class-map-record-alias-snapshot-inherited",
        ):
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
            "bootstrap-base-data",
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
            "class-map-record-missing": "class retained Map get requires a present record",
            "class-map-record-read-after-delete": "class retained Map get requires a present record",
            "class-map-record-mixed-scalar": "class retained Map requires one completed constructor family",
            "class-map-record-mixed-constructors": "class retained Map requires one completed constructor family",
            "class-map-record-computed-key": "class retained Map requires literal string keys",
            "class-map-record-alias-constructor": "class retained Map alias requires data field reads",
            "class-map-record-alias-shadow": "class method is observed or shadowed",
            "class-map-record-alias-constructor-write": "class retained Map alias requires data field reads",
            "class-map-record-alias-snapshot-write": "class own-key snapshot field set changes",
            "class-map-record-alias-snapshot-added": "class own-key snapshot field set changes",
            "class-map-record-alias-snapshot-method-added": "class own-key snapshot field set changes",
            "class-map-record-alias-snapshot-deleted": "class receiver escapes or observes a prototype/descriptor",
            "class-map-record-alias-snapshot-constructor-publication": "class own-key snapshot constructor observes its receiver",
            "inherited-own-fields-iterate-forward-missing": "class construction helper requires an existing own field",
            "inherited-own-fields-iterate-forward-recursive": "class construction helper proof exceeds its depth bound",
            "inherited-own-fields-iterate-forward-unused-effects": "unknown call, binding or reflective effect",
            "inherited-own-fields-iterate-borrow-missing": "class construction helper requires an existing own field",
            "inherited-own-fields-iterate-borrow-incompatible": "class construction helper requires an existing own field",
            "inherited-own-fields-iterate-method-missing": "class construction method requires an existing own field",
            "inherited-own-fields-iterate-method-add-field": "class construction method requires an existing own field",
            "inherited-own-fields-iterate-method-override-missing": "class construction method requires an existing own field",
            "inherited-own-fields-iterate-method-snapshot": "class construction method observes an own-key snapshot",
            "inherited-own-fields-iterate-method-inherited-snapshot": "class construction method observes an own-key snapshot",
            "inherited-own-fields-iterate-method-super-snapshot": "class construction method observes an own-key snapshot",
            "inherited-own-fields-iterate-method-inherited-early-snapshot": "class construction method observes an own-key snapshot",
            "inherited-own-fields-iterate-method-super-early-snapshot": "class construction method observes an own-key snapshot",
            "inherited-own-fields-iterate-method-recursive": "class construction method proof exceeds its depth bound",
            "inherited-own-fields-iterate-method-alias-escape-distinct": "class construction method receiver escapes its fixed fields",
            "inherited-own-fields-iterate-method-alias-escape": "class construction method receiver escapes its fixed fields",
            "inherited-own-fields-iterate-method-dead-ambient": "unknown call, binding or reflective effect",
            "inherited-own-fields-iterate-method-receiver-argument": "class receiver escapes or observes a prototype/descriptor",
            "inherited-post-super-holder-branches": "unknown call, binding or reflective effect",
            "own-fields-branch-order": "class own-key snapshot branches change ordered fields",
            "inherited-own-fields-branch-missing": "class own-key snapshot branches change ordered fields",
            "inherited-own-fields-branch-early": "super completion index is not proved",
            "own-fields-branch-observed": "class construction method observes an own-key snapshot",
            "own-fields-branch-loop": "class own-key snapshot requires fixed constructor fields",
            "inherited-own-fields-branch-before-super": "super condition is not a proved Boolean",
            "inherited-own-fields-branch-unused-ambient": "unknown call, binding or reflective effect",
            "inherited-own-fields-conditional": "class own-key snapshot branches change ordered fields",
            "inherited-own-fields-added": "inherited own-key snapshot requires the same ordered fields",
            "inherited-own-fields-empty-added": "inherited own-key snapshot requires the same ordered fields",
            "inherited-own-fields-grandchild-added": "inherited own-key snapshot requires the same ordered fields",
            "inherited-own-fields-sibling-added": "inherited own-key snapshot requires the same ordered fields",
            "inherited-own-fields-leaf-collision": "class own-key snapshot requires fixed constructor fields",
            "inherited-own-fields-before-store": "class construction method observes an own-key snapshot",
            "inherited-own-fields-ancestor-write": "class own-key snapshot field set changes",
            "inherited-own-fields-implicit": "derived class requires receiver-preserving super normalization",
            "own-fields-conditional": "class own-key snapshot branches change ordered fields",
            "own-fields-dynamic": "class own-key snapshot requires fixed constructor fields",
            "own-fields-new-field": "class own-key snapshot field set changes",
            "own-fields-numeric-key": "class own-key snapshot requires fixed constructor fields",
            "own-fields-snapshot-escape": "class own-key snapshot requires fixed length or index reads",
            "own-fields-unused-ambient": "unknown call, binding or reflective effect",
            "own-fields-replacement-return": "class own-key snapshot requires a primitive constructor return",
            "nested-helper-changing": "class local cell has changing writes",
            "nested-helper-identity": "captured helper escapes its ordinary local call",
            "nested-helper-excess": "captured helper escapes its ordinary local call",
            "nested-helper-primitive": "class method capture is not its constructor or an inert sibling helper",
            "nested-helper-receiver": "captured helper observes its implicit receiver or new.target",
            "nested-helper-effect": "unknown call, binding or reflective effect",
            "nested-helper-newtarget": "captured helper observes its implicit receiver or new.target",
            "nested-helper-recursive": "class local cell is observed before initialization",
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
            "inherited-method-leaf-shadow": "class method is observed or shadowed",
            "inherited-method-base-shadow": "class method is observed or shadowed",
            "inherited-method-ambient": "unknown call, binding or reflective effect",
            "inherited-method-shadow": "class method is observed or shadowed",
            "bootstrap-base": "class own-key snapshot constructor observes its receiver",
            "bootstrap-base-data": "class own-key snapshot constructor observes its receiver",
            "class-map-inherited-early-snapshot": "class construction method observes an own-key snapshot",
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
        if name == "class-map-record-direct":
            check_record_map_inputs(args, prepared)
        if name.startswith("class-map-record-") and name in POSITIVES:
            # Super normalization removes the imported ReferenceError guards;
            # the inherited source still constructs its Map and leaf record.
            constructions = (
                2
                if name
                in (
                    "class-map-record-alias-method-inherited",
                    "class-map-record-alias-snapshot-inherited",
                )
                else text.count("ctjs.construct")
            )
            if constructions != prepared.read_text().count("ctjs.construct"):
                raise RuntimeError(f"{name}: preparation discarded a record or Map construction")
        if name.startswith("class-map-") and name in POSITIVES | PREPARED_ONLY:
            prepare(
                args,
                name + "-no-map-identity",
                structured,
                dict(
                    manifest,
                    initial_intrinsics=[
                        identity for identity in manifest["initial_intrinsics"] if identity != "Map"
                    ],
                ),
                success=False,
            )
            preparation_refusals += 1
        if name.startswith("class-map-record-alias-snapshot-") and name in POSITIVES:
            prepare(
                args,
                name + "-no-object-identity",
                structured,
                dict(
                    manifest,
                    initial_intrinsics=[
                        item for item in manifest["initial_intrinsics"] if item != "Object"
                    ],
                ),
                success=False,
                diagnostic="class own-key snapshot needs declared Object identity",
            )
            preparation_refusals += 1
        if name == "own-fields-loop":
            for identity in (
                "Array",
                "__ctbrowser_for_of_open",
                "__ctbrowser_iter_next",
                "__ctbrowser_iter_close",
            ):
                prepare(
                    args,
                    "loop-without-" + identity,
                    structured,
                    dict(
                        manifest,
                        initial_intrinsics=[
                            item for item in manifest["initial_intrinsics"] if item != identity
                        ],
                    ),
                    success=False,
                    diagnostic="class own-key loop requires original Array iterator identities",
                )
                preparation_refusals += 1
            original = structured.read_text()
            opened = re.search(
                r'^(\s*)(%\w+) = ctjs.load_global "__ctbrowser_for_of_open"$', original, re.M
            )
            frame = re.findall(r"(%\w+) = ctjs.frame_enter", original[: opened.start()])[-1]
            rooted = args.work / "own-loop-root.mlir"
            rooted.write_text(
                original[: opened.end()]
                + f"\n{opened[1]}ctjs.root {opened[2]} in {frame}"
                + original[opened.end() :]
            )
            prepare(
                args,
                "own-loop-root",
                rooted,
                dict(manifest, module_sha256=host.fingerprint(args.opt, rooted)),
                success=False,
                diagnostic="class own-key loop contains an unproved effect",
            )
            preparation_refusals += 1
        if name == "own-fields-order":
            prepare(
                args,
                "own-fields-no-object",
                structured,
                dict(manifest, initial_intrinsics=["__ctbrowser_class_defined"]),
                success=False,
                diagnostic="class own-key snapshot needs declared Object identity",
            )
            preparation_refusals += 1
        if name == "static-call-capture":
            cutoffs[name] = check_proof_inputs(args, structured, manifest, prepared, name)
            preparation_refusals += 4
            prepared = check_nested_helper_root(args, structured, manifest, "static-method-root")
        if name == "nested-helper-constructor":
            prepared = check_nested_helper_root(args, structured, manifest)
        if name == "class-map-helper-constructor":
            symbol = re.search(r"ctjs.func private @(fn\$\d+)\(", prepared.read_text())[1]
            source = args.work / "map-helper-symbol.mlir"
            source.write_text(
                structured.read_text().replace(
                    "module attributes {", f"module attributes {{test.helper_ref = @{symbol}, ", 1
                )
            )
            prepare(
                args,
                "map-helper-symbol",
                source,
                dict(manifest, module_sha256=host.fingerprint(args.opt, source)),
                success=False,
                diagnostic="class Map helper has an unproved symbol reference",
            )
            preparation_refusals += 1
            check_nested_helper_root(args, structured, manifest, "map-helper-root")
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
            if name.startswith("inherited-own-fields-iterate-borrow-") or name in (
                "override-different-leaves",
                "inherited-helper-order",
                "captured-holder-unused",
            ):
                operations = ()
            elif name in ("class-map-nested-holder", "class-map-helper-numeric-key"):
                operations = ("ctjs.construct",)
            elif name == "class-map-inherited":
                # Super guard Error constructions disappear during normalization.
                operations = ("ctjs.load_upvalue",)
            elif name.startswith("class-map-record-"):
                operations = ("ctjs.construct",)
                if before.count("ctjs.call ") != after.count("ctjs.call ") + 1:
                    raise RuntimeError("record Map preparation changed a source Map call")
            elif name.startswith("class-map-"):
                operations = ("ctjs.construct", "ctjs.load_upvalue")
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
        if name in (
            "inherited-own-fields-iterate-borrow-captured",
            "inherited-own-fields-iterate-forward-captured",
        ):
            prepared_refusals += check_borrowed_helper_inputs(args, prepared)
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
            "own-fields-order",
            "own-fields-loop",
            "nested-helper-chain",
            "captured-helper-constructor",
            "captured-holder-sibling",
            "captured-holder-unused-capture",
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
            "class-map-record-overwrite",
            "class-map-record-alias-snapshot-overwrite-delete",
            "local-helper-branches",
            "local-holder-arrow",
            "global-holder-chain",
        ):
            cutoffs[name] = check_proof_inputs(args, structured, manifest, prepared, name)
            preparation_refusals += 4
        if name in (
            "class-map-helper-constructor",
            "class-map-helper-holder",
            "class-map-helper-chain",
            "class-map-helper-shared-callers",
            "class-map-direct",
            "class-map-mixed-method",
            "class-map-method-loop",
            "inherited-own-fields-iterate-forward-direct",
            "inherited-own-fields-iterate-forward-captured",
            "inherited-own-fields-iterate-borrow-direct",
            "inherited-own-fields-iterate-borrow-captured",
            "inherited-own-fields-iterate-borrow-holder",
            "inherited-own-fields-iterate-getter-direct",
            "inherited-own-fields-iterate-getter-chain",
            "inherited-own-fields-iterate-getter-fresh-empty",
            "inherited-own-fields-iterate-helper-captured",
            "inherited-own-fields-iterate-helper-nested",
            "inherited-own-fields-iterate-method-read",
            "inherited-own-fields-iterate-method-nested",
        ):
            cutoffs[name] = check_proof_budget(args, structured, manifest, prepared, name)
            preparation_refusals += 1
        if name in (
            "class-map-inherited-distinct",
            "class-map-inherited-chain",
            "class-map-inherited-mixed-captures",
            "inherited-own-fields-iterate-forward-inherited",
            "inherited-own-fields-iterate-borrow-inherited",
            "inherited-own-fields-iterate-method-nearest",
            "inherited-own-fields-iterate-helper-distinct",
            "inherited-post-super-holder-chain",
            "inherited-post-super-holder-order",
            "inherited-captured-holder-shared",
            "inherited-captured-holder-order",
            "inherited-own-fields-shared",
            "inherited-own-fields-branch-arguments",
            "inherited-branch-helper",
            "inherited-super-order",
            "inherited-helper-distinct",
            "inherited-nested-helper-distinct",
        ):
            check_super_roots(
                args,
                structured,
                manifest,
                (
                    "super constructor declarations, roots and global writes remain unsupported"
                    if name != "inherited-super-order"
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
                    key_type = (
                        "!ctnative.boxed"
                        if name == "class-map-optional-key-object"
                        else "!ctnative.opt"
                    )
                    if (
                        name.startswith("class-map-")
                        and not name.startswith("class-map-record-")
                        and f"!ctnative.map<{key_type}" not in native_text
                    ):
                        raise RuntimeError("class Map lost its key representation refusal")
                    if name == "inherited-helper-order" and (
                        "an object literal passed to a direct call as an argument"
                        not in native_text
                    ):
                        raise RuntimeError("helper mutation lost its argument ownership refusal")
                    if name == "captured-holder-unused" and (
                        "a method field: nothing calls it" not in native_text
                    ):
                        raise RuntimeError("unused holder method lost its invocation refusal")
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
