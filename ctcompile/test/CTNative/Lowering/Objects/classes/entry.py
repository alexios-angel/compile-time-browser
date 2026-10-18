#!/usr/bin/env python3
"""Check class-initialization preparation when the entry is a named function.

The entry function declares a class and returns a value. Its no-argument and
unused-argument forms must prepare without changing the script's wrapper
function, and every negative variant (stale manifest, wrong entry, DOM
parameters, a parameter the callee reads, replaced or detached methods, ambient
calls) must be refused.
"""

import re

from CTNative.Browser.dom import dom
from CTNative.HostContract import contract as host
from CTNative.harness import native_args, run
from CTNative.Lowering.Objects.classes import initialization as classes
from CTNative.Lowering.Objects.constructor_refusals import check_mutable_helper

CLASS = """class Shape {
    constructor(key) { this.key = key; }
    read() { return this.key; }
  }
  const shape = new Shape('x');
"""
BODY = CLASS + "  return shape.read() === 'x';\n"
DOM_BODY = CLASS + "  return element.getAttribute(shape.read()) === null;\n"
# The top-level script function (`_script_$0`), which preparation must leave
# byte-for-byte unchanged.
WRAPPER = re.compile(r"^  ctjs.func @_script_\$0\(.*?^  }\n", re.M | re.S)


def main(argv=None):
    args = native_args(__doc__, argv=argv)
    refusals, observations = 0, 0

    def imported(name, body, parameters="element", suffix=""):
        source = f"function {name}({parameters}) {{\n  {body}}}\n{suffix}"
        ir, request = dom.prepare(args, name, source, 1, entry_name=name)
        request = dict(
            host.manifest(args.opt, ir),
            entry=request["entry"],
            initial_intrinsics=["__ctbrowser_class_defined"],
            roots=[{"binding": name, "properties": ["slot"]}],
        )
        check_mutable_helper(ir.read_text())
        return source, ir, request

    for name, parameters in (("no_arguments", ""), ("unused_argument", "element")):
        source, ir, request = imported(name, BODY, parameters)
        oracle = args.work / f"{name}.oracle.js"
        oracle.write_text(source + f"var observation = {name}();\n")
        reference = run([args.reference, str(oracle)]).stdout
        oracle.write_text(oracle.read_text() + "console.log('observation=' + observation);\n")
        if (
            run([args.node, str(oracle)]).stdout != "observation=true\n"
            or reference != "observation=true\n"
        ):
            raise RuntimeError(f"{name}: original source observation changed")
        observations += 2
        prepared = classes.prepare(args, name, ir, request, success=True)
        text = prepared.read_text()
        before, after = WRAPPER.search(ir.read_text()), WRAPPER.search(text)
        if not before or not after or before.group() != after.group():
            raise RuntimeError(f"{name}: preparation changed the declaration wrapper")
        if 'ctjs.load_global "__ctbrowser_class_defined"' in text or "ctjs.construct" not in text:
            raise RuntimeError(f"{name}: lost the class preparation/constructor boundary")
        for control, changed, options in (
            ("missing", dict(request, initial_intrinsics=[]), ""),
            ("stale", dict(request, module_sha256="0" * 64), ""),
            ("budget", request, "max-steps=0"),
            ("wrong-entry", dict(request, entry="missing$999"), ""),
            ("dom", dict(request, provider="ctbrowser-dom-v1", element_parameters=[0]), ""),
        ):
            classes.prepare(args, f"{name}-{control}", ir, changed, success=False, options=options)
            refusals += 1
        missing = args.work / f"{name}-missing-wrapper.mlir"
        missing.write_text(WRAPPER.sub("", ir.read_text(), count=1))
        classes.prepare(
            args,
            f"{name}-missing-wrapper",
            missing,
            dict(request, module_sha256=host.fingerprint(args.opt, missing)),
            success=False,
        )
        refusals += 1
        entry = re.compile(
            rf"^  ctjs.func (?:private )?@{re.escape(request['entry'])}\(.*?^  }}\n",
            re.M | re.S,
        )
        observed, count = re.subn(
            r"(ctjs.get_property )%\w+(\[)",
            r"\1%arg2\2",
            entry.search(ir.read_text()).group(),
            count=1,
        )
        if count != 1:
            raise RuntimeError("callee observation control lost its property read")
        for control, changed, selected in (
            ("callee-observed", entry.sub(lambda _: observed, ir.read_text()), request["entry"]),
            (
                "script-parameter",
                ir.read_text().replace(
                    before.group(),
                    before.group().replace(") ->", ", %external: !ctjs.value) ->", 1),
                    1,
                ),
                "_script_$0",
            ),
        ):
            mutated = args.work / f"{name}-{control}.mlir"
            mutated.write_text(changed)
            classes.prepare(
                args,
                f"{name}-{control}",
                mutated,
                dict(request, entry=selected, module_sha256=host.fingerprint(args.opt, mutated)),
                success=False,
            )
            refusals += 1

    for name, body, suffix in (
        ("used_argument", CLASS + "  return element;\n", ""),
        ("receiver", CLASS + "  return this.key;\n", ""),
        ("ambient_entry", CLASS + "  ambient(); return true;\n", ""),
        ("unused_ambient", BODY.replace("read() {", "unused() { ambient(); } read() {"), ""),
        ("helper_override", "  __ctbrowser_class_defined = function(value) {};\n" + BODY, ""),
        (
            "prototype_replaced",
            CLASS + "  Shape.prototype.read = function() {}; return true;\n",
            "",
        ),
        ("method_replaced", CLASS + "  shape.read = function() {}; return true;\n", ""),
        ("detached", CLASS + "  const read = shape.read; return read();\n", ""),
        ("noninert_wrapper", BODY, "var observed = noninert_wrapper();\n"),
        ("dom_class", DOM_BODY, ""),
    ):
        _, ir, request = imported(name, body, suffix=suffix)
        classes.prepare(args, name, ir, request, success=False)
        refusals += 1
    print(
        f"class entry: 2 preparations, {observations} Node/interpreter observations, {refusals} refusals"
    )


def test_entry(lit):
    main(
        lit.args(
            "--translate ctjs-translate --opt ctjs-opt --node %node --reference %native_reference --work %t"
        )
    )


if __name__ == "__main__":
    main()
