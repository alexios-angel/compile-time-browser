#!/usr/bin/env python3
"""Test static factory methods (`static create(el) { return new this(el); }`).

The native lowering expands a call such as `Button.create(element)` into the
construction it performs before proving the class code native. Each CASES
source must match Node and the reference VM and run natively; each BAD source
must be refused.
"""

from CTNative.Browser.dom import dom
from CTNative.harness import run

# The class-support intrinsics a compiled class needs; the probe declares all.
INTRINSICS = [
    "__ctbrowser_class_defined",
    "__ctbrowser_class_heritage",
    "__ctbrowser_bind_this",
    "__ctbrowser_init_fields",
]

CASES = {
    "own": """class Button { constructor(el) { this._element = el; }
    static create(el) { return new this(el); }
    mark() { this._element.setAttribute('x', 'done'); } }
  Button.create(element).mark();
  return element.getAttribute('x') === 'done';""",
    "inherited": """class Base { constructor(el) { this._element = el; }
    static create(el) { return new this(el); } }
  class Button extends Base { mark() { this._element.setAttribute('x', 'done'); } }
  Button.create(element).mark();
  return element.getAttribute('x') === 'done';""",
    "chained": """class Button { constructor(el) { this._element = el; }
    static create(el) { return this.make(el); }
    static make(el) { return new this(el); }
    mark(value) { this._element.setAttribute('x', value); } }
  Button.create(element).mark('first');
  Button.make(element).mark('done');
  return element.getAttribute('x') === 'done';""",
    "padded": """class Button { constructor(el, label) { this._element = el; this._label = label; }
    static create(el, label) { return new this(el, label); }
    mark() { this._element.setAttribute('x', this._label === 'label' ? 'label' : 'done'); } }
  Button.create(element).mark();
  return element.getAttribute('x') === 'done';""",
}

CHECKS = r"""
        (void)classes;
        (void)pressed;
        assert(@ENTRY@(element));
        assert(@ENTRY@(alias));
        assert(@ENTRY@(foreign));
        assert(doc.read().attribute_value(button, atoms.intern("x")) == "done");
        (void)doc.take_writes();
"""

# Each makes the static read or factory body unsafe to expand: a conditional
# result, a reassigned or detached method, a computed name, new.target, the
# wrong constructor argument, or an accessor that shadows the factory.
BAD = {
    "conditional": CASES["own"].replace(
        "return new this(el);",
        "return this.find(el) || new this(el); }\n    static find(el) { return null;",
    ),
    "reassigned": CASES["own"].replace(
        "  Button.create(element)",
        "  Button.create = function (el) { return null; };\n  Button.create(element)",
    ),
    "escaped": CASES["own"].replace(
        "  Button.create(element).mark();",
        "  const create = Button.create;\n  create.call(Button, element).mark();",
    ),
    "computed": CASES["own"].replace("Button.create(element)", "Button['cre' + 'ate'](element)"),
    "new-target": CASES["own"].replace(
        "return new this(el);", "return new.target ? null : new this(el);"
    ),
    "receiver-argument": CASES["own"].replace("return new this(el);", "return new this(this);"),
    "shadowed-accessor": CASES["inherited"].replace(
        "class Button extends Base {",
        "class Button extends Base { static get create() { return null; }",
    ),
}


def source(body):
    """Wrap `body` as the probe(element) entry function."""
    return "function probe(element) {\n" + body + "\n}\n"


# A JavaScript stand-in element for the Node and VM oracles.
ELEMENT = (
    "{attributes: {}, setAttribute(k, v) { this.attributes[k] = String(v); },"
    " getAttribute(k) { return k in this.attributes ? this.attributes[k] : null; }}"
)


def main():
    args = dom.parse_args(__doc__, oracles=True)
    includes, libraries = dom.link_options(args)
    executions = refusals = 0
    for name, body in CASES.items():
        text = source(body)
        oracle = args.work / f"{name}.oracle.js"
        oracle.write_text(text + f"var result = probe({ELEMENT});\n")
        assert run([args.reference, str(oracle)]).stdout == "result=true\n", name
        oracle.write_text(oracle.read_text() + "console.log('result=' + result);\n")
        assert run([args.node, str(oracle)]).stdout == "result=true\n", name
        ir, contract = dom.prepare(args, name, text, 1, entry_name="probe")
        declared = dict(contract, initial_intrinsics=INTRINSICS)
        for owned, optimize, manifest in dom.variants(declared):
            label = f"{name}-{owned}-{optimize}"
            native = dom.lower(args, ir, manifest, label, optimize=optimize, max_steps=1000000)
            dom.standalone(args, native, label, CHECKS, args.compilers, includes, libraries)
            executions += 4  # Explicit and deduced C++, each built by both compilers.
        dom.refuse(
            args, ir, dict(contract, initial_intrinsics=INTRINSICS[1:]), name + "-undeclared"
        )
        refusals += 2  # Without __ctbrowser_class_defined: refused under both policies.
        dom.lower(args, ir, declared, name + "-budget", success=False, max_steps=0)
        refusals += 1
    for name, body in BAD.items():
        ir, contract = dom.prepare(args, "bad-" + name, source(body), 1, entry_name="probe")
        for owned, optimize, manifest in dom.variants(
            dict(contract, initial_intrinsics=INTRINSICS)
        ):
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
        f"DOM class static factories: {len(CASES)} Node/VM sources, "
        f"{executions} native executions, {refusals} refusals"
    )


if __name__ == "__main__":
    main()
