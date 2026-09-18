#!/usr/bin/env python3
"""Prove direct DOM receivers while retaining the class/DOM refusal boundary."""

import argparse
from pathlib import Path
import re
import shutil

from CTNative.Browser import native_dom as dom
from CTNative.Browser import native_dom_strings as strings
from CTNative.HostContract import contract as host
from CTNative.harness import find_compilers, run
from CTNative.Lowering.Objects import class_initialization as classes
from CTNative.Lowering.Objects.constructor_refusals import check_mutable_helper
from Target.Cpp.harness import FLAGS

CLASS = """class Shape {
    constructor(key) { this.key = key; }
    read() { return this.key; }
  }
  const shape = new Shape('x');
"""
READ = "  return element.getAttribute(shape.read()) === null;\n"
ELEMENT_CLASS = """class Button {
    constructor(element) { this.element = element; }
    static get NAME() { return 'test-token'; }
    press() {
      this.element.classList.toggle(Button.NAME, true);
      return this.element.getAttribute('x') === null;
    }
  }
  return new Button(element).press();
"""
CLASS_CASES = {
    "class_key": (CLASS + READ, "1000"),
    "class_order": (
        CLASS + """  const saved = element.getAttribute(shape.read());
  element.setAttribute(shape.read(), 'after');
  shape.key = 'marker';
  element.setAttribute(shape.read(), 'done');
  return saved === null;
""",
        "1000",
    ),
    "class_element": (ELEMENT_CLASS, "1000"),
}
CLASS_REFUSALS = {
    "ambient_entry": CLASS + "  ambient();\n" + READ,
    "ambient_method": CLASS.replace("read() {", "unused() { ambient(); }\n    read() {") + READ,
    "prototype_replaced": CLASS
    + "  Shape.prototype.read = function() { return 'missing'; };\n"
    + READ,
    "instance_replaced": CLASS + "  shape.read = function() { return 'missing'; };\n" + READ,
    "detached_receiver": CLASS
    + "  const read = shape.read; return element.getAttribute(read()) === null;\n",
    "receiver_escapes": CLASS.replace("return this.key;", "return this;")
    + "  const saved = shape.read(); return element.getAttribute(saved.key) === null;\n",
    "helper_replaced": "  __ctbrowser_class_defined = function(value) {};\n" + CLASS + READ,
    "unknown_dom": CLASS + "  element.unknown();\n" + READ,
    "unused_unknown_dom": ELEMENT_CLASS.replace(
        "    press() {", "    unused() { this.element.unknown(); }\n    press() {"
    ),
    "unused_unknown_receiver": ELEMENT_CLASS.replace(
        "    press() {",
        "    unused() { const other = {}; other.getAttribute('x'); }\n    press() {",
    ),
    "unused_ambient_getter": ELEMENT_CLASS.replace(
        "    press() {", "    static get UNUSED() { ambient(); return true; }\n    press() {"
    ),
    "dom_method_replaced": ELEMENT_CLASS.replace(
        "      this.element.classList",
        "      this.element.getAttribute = element;\n      this.element.classList",
    ),
    "entry_receiver": CLASS + "  return this.getAttribute(shape.read()) === null;\n",
    "retained_element": ELEMENT_CLASS.replace(
        "      this.element.classList",
        "      this.element.saved = this.element;\n      this.element.classList",
    ),
}
CASES = {
    "direct_read": (
        """function directRead(target, key) { return target.getAttribute(key); }
  return directRead(element, 'x') === null;
""",
        "1000",
    ),
    "direct_order": (
        """function directRead(target, key) {
    const saved = target.getAttribute(key);
    target.setAttribute(key, 'after');
    return saved;
  }
  const saved = directRead(element, 'x');
  const again = directRead(element, 'x');
  const second = directRead(other, 'other');
  return saved === null && again === 'after' && second === 'second';
""",
        "1000",
    ),
}
ORDER_CHECKS = """assert(doc.read().attribute_value(node, state) == "after");
            assert(doc.read().attribute_value(other_node, atoms.intern("other")) == "after");
            assert(doc.read().attribute_value(other_node, state) == "different");"""
FUNCTION = re.compile(r"^  ctjs.func (?:private )?@([^ (]+)\([^\n]*\n.*?^  }\n", re.M | re.S)


def source(name, body):
    parameters = "element, other" if name in CASES else "element"
    return f"function {name}({parameters}) {{\n  {body}}}\n"


def check_oracles(args):
    cases = CLASS_CASES | CASES
    declarations = "".join(source(name, body) for name, (body, _) in cases.items())
    observations, expected = [], []
    for name, (_, bits) in cases.items():
        for value, bit in zip(("null", "''", r"'a\0b'", r"'\u00e9'"), bits):
            variable = f"observation{len(observations):02}"
            effects = {
                "class_order": "if (element.getAttribute('x') !== 'after' || element.getAttribute('marker') !== 'done') throw new Error('lost class writes');",
                "class_element": "if (toggles !== 1) throw new Error('lost class toggle');",
                "direct_order": "if (element.getAttribute('x') !== 'after' || other.getAttribute('other') !== 'after' || other.getAttribute('x') !== 'different') throw new Error('lost receiver writes');",
            }.get(name, "")
            observations.append(f"""var {variable} = (() => {{
  const element = observationElement({value});
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
    oracle = declarations + strings.BOOLEAN_DOUBLE + "\n".join(observations) + "\n"
    path = args.work / "oracle.js"
    path.write_text(oracle)
    reference = run([args.reference, str(path)]).stdout
    path.write_text(
        oracle
        + "\n".join(
            f"console.log('observation{i:02}=' + observation{i:02});"
            for i in range(len(observations))
        )
    )
    node = run([args.node, str(path)]).stdout
    if reference != "".join(expected) or node != reference:
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
    for layout in ("explicit", "deduced"):
        headers, bodies, checks, expected = set(), [], [], []
        for name, owned, native in modules:
            label = f"{name}_{owned}_{optimize}_{layout}"
            if layout == "deduced":
                deduced = args.work / f"{label}.mlir"
                run([args.opt, str(native), "--ctnative-print-deduced", "-o", str(deduced)])
                native = deduced
            cpp, symbol = strings.emitted(args, native, label)
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
            call = "session.invoke(element, other)" if owned else entry + "(element, other)"
            checks.append(
                strings.BOOLEAN_RUN.replace("@SETUP@", setup)
                .replace(
                    "        const auto state =",
                    """        const auto other_node = doc.create_element(atoms.intern("button"));
        const element_ref other{&doc, other_node};
        assert(doc.set_attribute(other_node, atoms.intern("x"), "different"));
        const auto state =""",
                )
                .replace(
                    "            const auto result =",
                    '            assert(doc.set_attribute(other_node, atoms.intern("other"), "second"));\n'
                    "            const auto result =",
                )
                .replace("@CALL@", call)
                .replace("@CHECKS@", ORDER_CHECKS if name == "direct_order" else "")
            )
            expected.extend("true\n" if bit == "1" else "false\n" for bit in CASES[name][1])
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
    observations = check_oracles(args)
    compilers = find_compilers()
    compilers[1] = args.clang
    includes, libraries = dom.link_options(args)
    prepared, refusals = [], 0
    for name, (body, _) in CASES.items():
        ir, contract = dom.prepare(args, name, source(name, body), 2, entry_name=name)
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
    for optimize in (False, True):
        modules = [
            (
                name,
                owned,
                dom.lower(args, ir, contract, f"{name}-{owned}-{optimize}", optimize=optimize),
            )
            for name, owned, ir, contract in prepared
        ]
        check_native(args, modules, optimize, compilers, includes, libraries)
    class_sources = {name: body for name, (body, _) in CLASS_CASES.items()} | CLASS_REFUSALS
    for name, body in class_sources.items():
        ir, contract = dom.prepare(args, name, source(name, body), 1, entry_name=name)
        check_mutable_helper(ir.read_text())
        if "ctjs.construct" not in ir.read_text() or '"prototype"' not in ir.read_text():
            raise RuntimeError(f"{name}: source lost ordinary class construction")
        for owned in (False, True):
            request = dict(
                contract,
                provider="ctbrowser-dom-session-v1" if owned else "ctbrowser-dom-v1",
                initial_intrinsics=["__ctbrowser_class_defined"],
            )
            classes.prepare(args, f"{name}-{owned}", ir, request, success=False)
            refusals += 1
    print(
        f"DOM receivers: {observations} Node/interpreter observations, "
        f"8 combined native executions, {refusals} refusals; class/DOM preparation remains refused"
    )


if __name__ == "__main__":
    main()
