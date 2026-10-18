#!/usr/bin/env python3
"""Test local callbacks that a class method creates and calls synchronously.

The native lowering may inline such a callback but must keep its DOM writes in
order. Each CASES source runs under Node and the reference VM, then lowers to a
native DOM entry that is compiled and run against a real ctbrowser document.
Each BAD source lets the callback escape, misuses its call or performs an
unknown effect, and must be refused. The constants here are shared by the
drivers in callback/.
"""

import re

from CTNative.Browser.dom import dom
from CTNative.harness import run

# The callback: writes `inside` and reads it back.
LEAF = """node => {
      node.setAttribute('inside', 'done');
      return node.getAttribute('inside') === 'done';
    }"""
# A method that writes `before`, calls the callback, then writes `after`.
METHOD = f"""class Item {{
    run(target) {{
      const local = {LEAF};
      target.setAttribute('before', 'done');
      const result = local(target);
      target.setAttribute('after', 'done');
      return result;
    }}
  }}"""
# The probe body after the class: run the method on both probe arguments.
CALLS = """
  const item = new Item();
  const first = item.run(element);
  const second = item.run(other);
  return first && second;
"""
CASES = {
    "method": METHOD + CALLS,
    "captured-helper": f"""const invoke = target => {{
    const local = {LEAF};
    target.setAttribute('before', 'done');
    const result = local(target);
    target.setAttribute('after', 'done');
    return result;
  }};
  class Item {{ run(target) {{ return invoke(target); }} }}"""
    + CALLS,
    "discarded": METHOD.replace("const result = local(target);", "local(target);").replace(
        "return result;", "return true;"
    )
    + CALLS,
    "repeated": METHOD.replace(
        "const result = local(target);", "local(target); const result = local(target);"
    )
    + CALLS,
    "branch-local": METHOD.replace(
        "const local =", "if (target.hasAttribute('ready')) { const local ="
    ).replace("return result;", "return result; } return true;")
    + CALLS,
}

BAD = {
    "returned": METHOD.replace("const result = local(target);", "const result = local;") + CALLS,
    "stored": METHOD.replace(
        "const result = local(target);", "target.saved = local; const result = true;"
    )
    + CALLS,
    "later-escape": METHOD.replace(
        "const result = local(target);", "const result = local(target); target.saved = local;"
    )
    + CALLS,
    "timer": METHOD.replace(
        "const result = local(target);", "setTimeout(local, 0); const result = true;"
    )
    + CALLS,
    "event": METHOD.replace(
        "const result = local(target);",
        "target.addEventListener('click', local); const result = true;",
    )
    + CALLS,
    "mutable-capture": METHOD.replace("const local =", "let label = 'inside'; const local =")
    .replace("node.setAttribute('inside', 'done')", "node.setAttribute('inside', label)")
    .replace("const result = local(target);", "label = 'changed'; const result = local(target);")
    + CALLS,
    "receiver": METHOD.replace("node.getAttribute('inside') === 'done'", "this === node") + CALLS,
    "new-target": METHOD.replace("node =>", "function(node)").replace(
        "node.getAttribute('inside') === 'done'", "new.target === undefined"
    )
    + CALLS,
    "extra-argument": METHOD.replace("local(target)", "local(target, target)") + CALLS,
    "missing-argument": METHOD.replace("local(target)", "local()") + CALLS,
    "unknown-effect": METHOD.replace("node.setAttribute('inside', 'done')", "ambient(node)")
    + CALLS,
    "later-input": METHOD + CALLS.replace("item.run(other)", "item.run(3)"),
    "unused-effect": METHOD.replace(
        "class Item {", "class Item { unused() { return (() => ambient())(); }"
    )
    + CALLS,
    "branch-effect": CASES["branch-local"].replace(
        "node.setAttribute('inside', 'done')", "ambient(node)"
    ),
    "branch-escape": CASES["branch-local"].replace(
        "const result = local(target);", "const result = local(target); target.saved = local;"
    ),
}

# C++ checks for the native entry. @ORDER@ lists the attribute writes one call
# makes, @STEPS@ is their count, and @PRECHECKS@ is spliced in before the
# first unconditional call.
CHECKS = r"""
        (void)classes;
        (void)pressed;
        static_assert(std::is_same_v<decltype(@ENTRY@(element, foreign)), ctnative::js_boolean_t>);
        foreign_doc.log_writes(true);
        const auto check = [&](document & owner, atom_table & table, node_id id, size_t calls) {
            const auto writes = owner.take_writes();
            assert(writes.size() == calls * @STEPS@);
            size_t index = 0;
            for (size_t i = 0; i < calls; ++i) {
                for (const auto * name : {@ORDER@}) {
                    assert(writes[index].node == id && writes[index].name == table.intern(name));
                    ++index;
                }
            }
            assert(owner.read().attribute_value(id, table.intern("inside")) == "done");
        };
        @PRECHECKS@
        assert(@ENTRY@(element, foreign));
        check(doc, atoms, button, 1);
        check(foreign_doc, foreign_atoms, other_button, 1);
        assert(@ENTRY@(alias, element));
        check(doc, atoms, button, 2);
        assert(foreign_doc.take_writes().empty());
"""

# @PRECHECKS@ for a callback inside `if (target.hasAttribute('ready'))`: no writes
# without `ready`, then writes on only the element that has it.
BRANCH_CHECKS = r"""
        assert(@ENTRY@(element, foreign));
        assert(doc.take_writes().empty() && foreign_doc.take_writes().empty());
        assert(doc.set_attribute(button, atoms.intern("ready"), ""));
        (void)doc.take_writes();
        assert(@ENTRY@(element, foreign));
        check(doc, atoms, button, 1);
        assert(foreign_doc.take_writes().empty());
        assert(doc.remove_attribute(button, atoms.intern("ready")));
        assert(foreign_doc.set_attribute(other_button, foreign_atoms.intern("ready"), ""));
        (void)doc.take_writes();
        (void)foreign_doc.take_writes();
        assert(@ENTRY@(element, foreign));
        assert(doc.take_writes().empty());
        check(foreign_doc, foreign_atoms, other_button, 1);
        assert(doc.set_attribute(button, atoms.intern("ready"), ""));
        (void)doc.take_writes();
"""

# A JavaScript stand-in element for the Node and VM oracles; `trace` records
# the name of every attribute written.
ELEMENT = """{attributes: {}, trace: '',
 setAttribute(key, value) { this.attributes[key] = String(value); this.trace += key + ','; },
 hasAttribute(key) { return key in this.attributes; },
 getAttribute(key) { return key in this.attributes ? this.attributes[key] : null; }}"""


def source(body):
    """Wrap `body` as the probe(element, other) entry function."""
    return "function probe(element, other) {\n" + body + "\n}\n"


def call_variants(args, ir, contract):
    """Yield (label, IR path, manifest) for rewrites of the one `ctjs.call_direct`.

    "ordinary" turns it into an equivalent `ctjs.call` and must still lower;
    the others call the wrong function, swap the receiver or new.target, or
    change the argument count, and must be refused.
    """
    text = run([args.opt, str(ir), "--mlir-print-op-generic"]).stdout
    calls = list(
        re.finditer(
            r'(%\w+) = "ctjs.call_direct"\(([^)]*)\) <\{callee = @([^}]+)\}> '
            r": \([^)]*\) -> !ctjs.value",
            text,
        )
    )
    assert len(calls) == 1, text
    call = calls[0]
    operands = call[2].split(", ")
    assert len(operands) == 4, call[0]
    method = re.search(r'sym_name = "(run\$\d+)"', text)[1]

    def spelling(operation, values, properties=""):
        return (
            f'{call[1]} = "{operation}"({", ".join(values)}){properties} : '
            f"({', '.join(['!ctjs.value'] * len(values))}) -> !ctjs.value"
        )

    ordinary = [operands[2], operands[0], operands[3]]
    mutations = {
        "ordinary": spelling("ctjs.call", ordinary),
        "wrong-target": call[0].replace("@" + call[3], "@" + method),
        "receiver": spelling(
            "ctjs.call_direct", [operands[3], *operands[1:]], f" <{{callee = @{call[3]}}}>"
        ),
        "new-target": spelling(
            "ctjs.call_direct",
            [operands[0], operands[3], *operands[2:]],
            f" <{{callee = @{call[3]}}}>",
        ),
        "extra-argument": spelling("ctjs.call", [*ordinary, operands[3]]),
        "missing-argument": spelling("ctjs.call", ordinary[:-1]),
    }
    for label, replacement in mutations.items():
        path = args.work / f"call-{label}.mlir"
        path.write_text(text[: call.start()] + replacement + text[call.end() :])
        manifest = dict(contract, module_sha256=dom.fingerprint(args.opt, path))
        yield label, path, manifest


def main(argv=None):
    args = dom.parse_args(__doc__, oracles=True, argv=argv)
    includes, libraries = dom.link_options(args)
    executions = refusals = 0
    for name, body in CASES.items():
        text = source(body)
        order = ["before", "inside"] + (["inside"] if name == "repeated" else []) + ["after"]
        checks = CHECKS.replace("@STEPS@", str(len(order))).replace(
            "@ORDER@", ", ".join('"' + key + '"' for key in order)
        )
        checks = checks.replace("@PRECHECKS@", BRANCH_CHECKS if name == "branch-local" else "")
        oracle = args.work / f"{name}.oracle.js"
        observation = (
            text + f"var first = {ELEMENT}, second = {ELEMENT};\n"
            f"var result = probe(first, second) && first.trace === '{','.join(order)},'"
            " && second.trace === first.trace;\n"
        )
        if name == "branch-local":
            observation = (
                text + f"var first = {ELEMENT}, second = {ELEMENT};\n"
                "var result = probe(first, second) && first.trace === '' && second.trace === '';\n"
                "first.attributes.ready = '';\n"
                "result = result && probe(first, second) && first.trace === 'before,inside,after,'"
                " && second.trace === '';\n"
                "first.trace = ''; delete first.attributes.ready; second.attributes.ready = '';\n"
                "result = result && probe(first, second) && first.trace === ''"
                " && second.trace === 'before,inside,after,';\n"
                "first.attributes.ready = ''; second.trace = '';\n"
                "result = result && probe(first, second) && first.trace === 'before,inside,after,'"
                " && second.trace === first.trace;\n"
            )
        oracle.write_text(observation)
        assert run([args.reference, str(oracle)]).stdout == "result=true\n", name
        oracle.write_text(oracle.read_text() + "console.log('result=' + result);\n")
        assert run([args.node, str(oracle)]).stdout == "result=true\n", name
        ir, contract = dom.prepare(args, name, text, 2, entry_name="probe")
        declared = dict(contract, initial_intrinsics=["__ctbrowser_class_defined"])
        for owned, optimize, manifest in dom.variants(declared):
            label = f"{name}-{owned}-{optimize}"
            native = dom.lower(args, ir, manifest, label, optimize=optimize, max_steps=1000000)
            dom.standalone(args, native, label, checks, args.compilers, includes, libraries)
            executions += 4  # Explicit and deduced C++, each built by both compilers.
        dom.refuse(args, ir, contract, name + "-undeclared")
        dom.lower(args, ir, declared, name + "-budget", success=False, max_steps=0)
        refusals += 3  # Two undeclared-intrinsic refusals and an exhausted budget of 0.
        if name == "method":
            for label, path, manifest in call_variants(args, ir, declared):
                for owned, optimize, variant in dom.variants(manifest):
                    admitted = label == "ordinary"
                    case = f"call-{label}-{owned}-{optimize}"
                    native = dom.lower(
                        args,
                        path,
                        variant,
                        case,
                        optimize=optimize,
                        success=admitted,
                        max_steps=1000000,
                    )
                    if admitted:
                        dom.standalone(
                            args, native, case, checks, args.compilers, includes, libraries
                        )
                        executions += 4
                    else:
                        refusals += 1
    for name, body in BAD.items():
        ir, contract = dom.prepare(args, name, source(body), 2, entry_name="probe")
        declared = dict(contract, initial_intrinsics=["__ctbrowser_class_defined"])
        for owned, optimize, manifest in dom.variants(declared):
            dom.lower(
                args,
                ir,
                manifest,
                f"bad-{name}-{owned}-{optimize}",
                optimize=optimize,
                success=False,
                max_steps=1000000,
            )
            refusals += 1
    print(
        f"DOM class callbacks: {len(CASES)} Node/VM sources, {executions} native executions, {refusals} refusals"
    )


def test_callbacks(lit):
    main(lit.args("%dom_driver --node %node --reference %native_reference"))


if __name__ == "__main__":
    main()
