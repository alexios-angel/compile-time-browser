#!/usr/bin/env python3
"""Compose original class proofs with typed DOM entries and local fields."""

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
    "class_unused_element": (
        ELEMENT_CLASS.replace(
            "    press() {", "    unused() { this.element.getAttribute('x'); }\n    press() {"
        ),
        "1000",
    ),
    "class_unused_write": (
        ELEMENT_CLASS.replace(
            "    press() {",
            "    unused() { this.element.getAttribute('x'); }\n"
            "    writeUnused() { this.element.setAttribute('unused-probe', 'bad'); }\n"
            "    press() {",
        ),
        "1000",
    ),
    "class_captured_key": (
        """class Button {
    static get NAME() { return 'x'; }
    read() { return Button.NAME; }
  }
  const button = new Button();
  return element.getAttribute(button.read()) === null;
""",
        "1000",
    ),
    "class_method_key": (
        """class Button {
    constructor(element) { this.element = element; }
    read(key) { return this.element.getAttribute(key); }
  }
  const button = new Button(element);
  const saved = button.read('x');
  element.setAttribute('marker', 'done');
  const marker = button.read('marker');
  return saved === null && marker === 'done';
""",
        "1000",
    ),
    "class_method_target": (
        """class Reader {
    read(target, key) { return target.getAttribute(key); }
  }
  const reader = new Reader();
  const saved = reader.read(element, 'x');
  const second = reader.read(other, 'other');
  return saved === null && second === 'second';
""",
        "1000",
    ),
    "class_method_field_order": (
        """class Button {
    constructor(element) { this.element = element; }
    read(key) { return this.element.getAttribute(key); }
  }
  const button = new Button(element);
  const saved = button.read('x');
  button.element = other;
  const second = button.read('other');
  return saved === null && second === 'second';
""",
        "1000",
    ),
    "class_method_instances": (
        """class Button {
    constructor(element) { this.element = element; }
    read(key) { return this.element.getAttribute(key); }
  }
  const first = new Button(element);
  const second = new Button(other);
  const saved = first.read('x');
  const again = second.read('other');
  return saved === null && again === 'second';
""",
        "1000",
    ),
    "class_method_transitive": (
        """class Button {
    constructor(element) { this.element = element; }
    read(target, key) { return target.getAttribute(key); }
    press(key) { return this.read(this.element, key); }
  }
  const button = new Button(element);
  const saved = button.read(element, 'x');
  const again = button.press('x');
  element.setAttribute('marker', 'done');
  const marker = button.press('marker');
  return saved === null && again === null && marker === 'done';
""",
        "1000",
    ),
}
CLASS_CASES.update(
    {
        "method_transitive_only": (
            CLASS_CASES["class_method_transitive"][0].replace(
                "button.read(element, 'x')", "button.press('x')"
            ),
            "1000",
        ),
        "class_method_transitive_order": (
            """class Button {
    constructor(element) { this.element = element; }
    read(target, key) {
      const saved = target.getAttribute(key);
      target.setAttribute(key, 'after');
      return saved;
    }
    forward(key) { return this.read(this.element, key); }
    press(key, target) { this.element = target; return this.forward(key); }
  }
  const button = new Button(element);
  const saved = button.press('x', element);
  const again = button.press('x', element);
  const second = button.press('other', other);
  return saved === null && again === 'after' && second === 'second';
""",
            "1000",
        ),
        "class_method_transitive_instances": (
            CLASS_CASES["class_method_instances"][0]
            .replace("    read(key)", "    press(key) { return this.read(key); }\n    read(key)")
            .replace("first.read('x')", "first.press('x')")
            .replace("second.read('other')", "second.press('other')"),
            "1000",
        ),
        "class_method_omitted": (
            """class Button {
    constructor(element) { this.element = element; }
    read(key, missing) {
      return this.element.getAttribute(key) === null && missing === void 0;
    }
  }
  return new Button(element).read('x');
""",
            "1000",
        ),
        "class_method_default_key": (
            """class Button {
    constructor(element) { this.element = element; }
    read(key = 'x') { return this.element.getAttribute(key); }
  }
  const button = new Button(element);
  const omitted = button.read();
  const explicit = button.read(void 0);
  element.setAttribute('marker', 'done');
  const supplied = button.read('marker');
  return omitted === null && explicit === null && supplied === 'done';
""",
            "1000",
        ),
        "class_method_default_constructor": (
            """class Button {
    constructor(element) { this.element = element; }
    static get KEY() { return 'x'; }
    read(key = this.constructor.KEY) { return this.element.getAttribute(key); }
    press(key) { return this.read(key); }
  }
  const button = new Button(element);
  const omitted = button.press();
  const explicit = button.press(void 0);
  return omitted === null && explicit === null;
""",
            "1000",
        ),
        "class_method_default_config": (
            """class Button {
    constructor(element) { this.element = element; }
    static get DefaultType() { return {}; }
    read(key, types = this.constructor.DefaultType) {
      return this.element.getAttribute(key);
    }
    press(key) { return this.read(key); }
  }
  return new Button(element).press('x') === null;
""",
            "1000",
        ),
        "class_method_default_transitive": (
            """class Button {
    constructor(element) { this.element = element; }
    read(key = 'x') { return this.element.getAttribute(key); }
    forward() { return this.read(); }
    press() { return this.forward(); }
  }
  return new Button(element).press() === null;
""",
            "1000",
        ),
        "class_method_default_null": (
            """class Reader {
    read(value = 'x') { return value; }
  }
  const reader = new Reader();
  const omitted = reader.read();
  const explicit = reader.read(void 0);
  const supplied = reader.read(null);
  return element.getAttribute(omitted) === null && explicit === 'x' && supplied === null;
""",
            "1000",
        ),
        "class_method_default_order": (
            """class Button {
    constructor(element) { this.element = element; }
    defaultKey() {
      const order = this.element.getAttribute('order');
      this.element.setAttribute('default', order);
      return 'x';
    }
    read(key = this.defaultKey(), ignored = this.element.setAttribute('unused-default', this.element.getAttribute('default'))) {
      const saved = this.element.getAttribute(key);
      this.element.setAttribute('order', 'body');
      return saved;
    }
  }
  const button = new Button(element);
  element.setAttribute('order', 'before');
  const saved = button.read(void 0, element.setAttribute('order', 'argument'));
  const order = element.getAttribute('default');
  const unused = element.getAttribute('unused-default');
  element.setAttribute('default', 'untouched');
  element.setAttribute('unused-default', 'untouched');
  button.read('x', true);
  return saved === null && order === 'argument' && unused === 'argument';
""",
            "1000",
        ),
    }
)
TWO_ELEMENT_CLASSES = {
    "class_method_target",
    "class_method_field_order",
    "class_method_instances",
    "class_method_transitive_order",
    "class_method_transitive_instances",
    "method_bad_target_after_good",
    "method_missing_target",
    "method_detached_target",
    "method_replaced_element_after_good",
    "method_bad_key_after_field_change",
    "method_transitive_bad_field_after_good",
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
    "unused_dom_then_ambient": ELEMENT_CLASS.replace(
        "    press() {",
        "    unused() { this.element.getAttribute('x'); ambient(); }\n    press() {",
    ),
    "unused_replaced_element": ELEMENT_CLASS.replace(
        "    press() {",
        "    unused() { this.element = {}; return this.element.getAttribute('x'); }\n    press() {",
    ),
    "unused_transitive_replaced_element": ELEMENT_CLASS.replace(
        "    press() {",
        "    unused() { this.element = {}; return this.press(); }\n    press() {",
    ),
    "unused_dead_dom": ELEMENT_CLASS.replace(
        "    press() {",
        "    unused() { if (false) this.element.unknown(); }\n    press() {",
    ),
    "second_fake_instance": ELEMENT_CLASS.replace(
        "  return new Button(element).press();",
        "  const first = new Button(element); const other = new Button({}); return first.press();",
    ),
    "nested_class_instance": ELEMENT_CLASS.replace(
        "      this.element.classList",
        "      class Nested { constructor(value) { this.element = value; } "
        "read() { return this.element.getAttribute('x'); } }\n"
        "      switch (1) { case 1: new Nested(this.element).read(); break; }\n"
        "      this.element.classList",
    ),
    "unused_bad_dom_argument": ELEMENT_CLASS.replace(
        "    press() {", "    unused() { this.element.getAttribute({}); }\n    press() {"
    ),
    "unused_detached_element_method": ELEMENT_CLASS.replace(
        "    press() {",
        "    unused() { const read = this.element.getAttribute; return read('x'); }\n    press() {",
    ),
    "fake_stored_element": ELEMENT_CLASS.replace("this.element = element", "this.element = {}"),
    "late_replaced_element": ELEMENT_CLASS.replace(
        "      this.element.classList", "      this.element = {};\n      this.element.classList"
    ),
    "detached_element_method": ELEMENT_CLASS.replace(
        "return this.element.getAttribute('x') === null;",
        "const read = this.element.getAttribute; return read('x') === null;",
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
    "unused_key_getter": CLASS.replace(
        "    read() {", "    static get UNUSED() { ambient(); return true; }\n    read() {"
    )
    + READ,
    "unused_key_dom_method": CLASS.replace(
        "    read() {", "    unused(target) { target.getAttribute('x'); }\n    read() {"
    )
    + READ,
    "unused_ambient_helper": CLASS + "  function unused() { ambient(); }\n" + READ,
    "unused_dom_helper": CLASS + "  function unused(target) { target.getAttribute('x'); }\n" + READ,
    "method_bad_key_after_good": CLASS_CASES["class_method_key"][0].replace(
        "button.read('marker')", "button.read({})"
    ),
    "method_dead_bad_key": CLASS_CASES["class_method_key"][0].replace(
        "  return saved", "  if (false) button.read({});\n  return saved"
    ),
    "method_dead_unknown_dom": CLASS_CASES["class_method_key"][0].replace(
        "  return saved", "  if (false) element.unknown();\n  return saved"
    ),
    "method_bad_target_after_good": CLASS_CASES["class_method_target"][0].replace(
        "reader.read(other, 'other')", "reader.read({}, 'other')"
    ),
    "method_missing_target": CLASS_CASES["class_method_target"][0].replace(
        "reader.read(other, 'other')", "reader.read()"
    ),
    "method_detached_target": CLASS_CASES["class_method_target"][0].replace(
        "const second = reader.read(other, 'other');",
        "const read = reader.read; const second = read(element, 'other');",
    ),
    "method_unused_parameter": CLASS_CASES["class_method_key"][0].replace(
        "    read(key)", "    unused(target) { target.getAttribute('x'); }\n    read(key)"
    ),
    "method_unused_unknown_parameter": CLASS_CASES["class_method_key"][0].replace(
        "    read(key)", "    unused(target) { target.unknown(); }\n    read(key)"
    ),
    "method_second_instance_uncalled": CLASS_CASES["class_method_key"][0].replace(
        "  const saved =", "  const unused = new Button(element);\n  const saved ="
    ),
    "method_replaced_element_after_good": CLASS_CASES["class_method_field_order"][0].replace(
        "button.element = other", "button.element = {}"
    ),
    "method_bad_key_after_field_change": CLASS_CASES["class_method_field_order"][0].replace(
        "button.read('other')", "button.read({})"
    ),
    "method_transitive_bad_target": CLASS_CASES["class_method_transitive"][0].replace(
        "this.read(this.element, key)", "this.read({}, key)"
    ),
    "method_transitive_only_bad_key": CLASS_CASES["method_transitive_only"][0].replace(
        "this.read(this.element, key)", "this.read(this.element, {})"
    ),
    "method_transitive_only_bad_target": CLASS_CASES["method_transitive_only"][0].replace(
        "this.read(this.element, key)", "this.read({}, key)"
    ),
    "method_transitive_only_unused_parameter": CLASS_CASES["method_transitive_only"][0].replace(
        "    read(target, key)",
        "    unused(target) { target.getAttribute('x'); }\n    read(target, key)",
    ),
    "method_transitive_only_uncalled_instance": CLASS_CASES["method_transitive_only"][0].replace(
        "  const saved =", "  const unused = new Button(element);\n  const saved ="
    ),
    "method_transitive_only_dead_bad_key": CLASS_CASES["method_transitive_only"][0].replace(
        "press(key) {", "press(key) { if (false) this.read(this.element, {});"
    ),
    "method_transitive_recursive": CLASS_CASES["method_transitive_only"][0].replace(
        "return target.getAttribute(key);", "target.getAttribute(key); return this.press(key);"
    ),
    "method_transitive_bad_field_after_good": CLASS_CASES["class_method_transitive_order"][
        0
    ].replace("button.press('other', other)", "button.press('other', {})"),
    "method_default_bad_key": CLASS_CASES["class_method_default_key"][0].replace(
        "read(key = 'x')", "read(key = {})"
    ),
    "method_default_bad_receiver": """class Reader {
    read(target = {}) { return target.getAttribute('x'); }
  }
  return new Reader().read() === null;
""",
    "method_default_skipped_ambient": CLASS_CASES["class_method_default_key"][0]
    .replace("read(key = 'x')", "read(key = ambient())")
    .replace("button.read()", "button.read('x')")
    .replace("button.read(void 0)", "button.read('x')"),
    "method_default_dead_unknown": CLASS_CASES["class_method_default_key"][0].replace(
        "read(key = 'x')", "read(key = false ? this.element.unknown() : 'x')"
    ),
    "method_default_unused_parameter": CLASS_CASES["class_method_default_key"][0].replace(
        "    read(key", "    unused(key = 'x') { this.element.getAttribute(key); }\n    read(key"
    ),
    "method_default_after_good": CLASS_CASES["class_method_default_key"][0]
    .replace("read(key = 'x')", "read(key = {})")
    .replace("  const omitted =", "  button.read('x');\n  const omitted ="),
    "method_default_transitive_bad_key": CLASS_CASES["class_method_default_constructor"][0].replace(
        "return 'x';", "return {};"
    ),
    "method_default_null_dom": CLASS_CASES["class_method_default_key"][0].replace(
        "button.read(void 0)", "button.read(null)"
    ),
    "method_default_unused_effect": """class Button {
    constructor(element) { this.element = element; }
    read(ignored = this.element.unknown()) {
      return this.element.getAttribute('x') === null;
    }
  }
  return new Button(element).read(true);
""",
    "method_default_replaces_receiver": CLASS_CASES["class_method_default_key"][0].replace(
        "read(key = 'x')", "read(key = (this.element = {}, 'x'))"
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
FIELD_CASES = {
    "field_key": (
        "const shape = {key: 'x'}; return element.getAttribute(shape.key) === null;",
        "1000",
    ),
    "field_order": (
        """const shape = {key: 'x'};
  const saved = element.getAttribute(shape.key);
  shape.key = 'marker'; element.setAttribute(shape.key, 'done');
  shape.key = 'x'; element.setAttribute(shape.key, 'after');
  return saved === null;""",
        "1000",
    ),
    "field_element": (
        """const holder = {element};
  const saved = holder.element.getAttribute('x');
  holder.element = other; holder.element.setAttribute('other', 'after');
  return saved === null;""",
        "1000",
    ),
    "field_snapshot": (
        """const holder = {saved: element.getAttribute('x')};
  const saved = holder.saved; holder.saved = 'after';
  element.setAttribute('x', holder.saved); return saved === null;""",
        "1000",
    ),
    "field_empty_key": (
        "const holder = {'': 'x'}; return element.getAttribute(holder['']) === null;",
        "1000",
    ),
    "field_unused_effect": (
        """const holder = {key: 'x', unused: element.setAttribute('marker', 'done')};
  return element.getAttribute(holder.key) === null;""",
        "1000",
    ),
}
CASES.update(FIELD_CASES)
FIELD_CHECKS = {
    "field_order": 'assert(doc.read().attribute_value(node, atoms.intern("marker")) == "done");\n'
    'assert(doc.read().attribute_value(node, state) == "after");',
    "field_element": 'assert(doc.read().attribute_value(other_node, atoms.intern("other")) == "after");\n'
    'assert(doc.read().attribute_value(other_node, state) == "different");',
    "field_snapshot": 'assert(doc.read().attribute_value(node, state) == "after");',
    "field_unused_effect": 'assert(doc.read().attribute_value(node, atoms.intern("marker")) == "done");',
}
FIELD_CHECKS["class_order"] = FIELD_CHECKS["field_order"]
FIELD_CHECKS["class_method_key"] = FIELD_CHECKS["class_method_transitive"] = FIELD_CHECKS[
    "method_transitive_only"
] = 'assert(doc.read().attribute_value(node, atoms.intern("marker")) == "done");'
FIELD_CHECKS["class_method_default_key"] = FIELD_CHECKS["class_method_key"]
FIELD_CHECKS["class_method_default_order"] = (
    'assert(doc.read().attribute_value(node, atoms.intern("order")) == "body");'
    'assert(doc.read().attribute_value(node, atoms.intern("default")) == "untouched");'
    'assert(doc.read().attribute_value(node, atoms.intern("unused-default")) == "untouched");'
)
FIELD_CHECKS["class_unused_write"] = (
    'assert(doc.read().attribute_value(node, atoms.intern("class")) == "test-token");'
    'assert(doc.read().attribute_value(node, atoms.intern("unused-probe")).empty());'
)
FIELD_CHECKS["class_element"] = FIELD_CHECKS["class_unused_element"] = (
    'assert(doc.read().attribute_value(node, atoms.intern("class")) == "test-token");'
)
FIELD_REFUSALS = {
    "missing_field": "const holder = {}; return element.getAttribute(holder.key) === null;",
    "read_before_write": "const holder = {}; const key = holder.key; holder.key = 'x'; return element.getAttribute(key) === null;",
    "prototype_key": "const holder = {}; holder.__proto__ = element; return holder.__proto__.getAttribute('x') === null;",
    "reserved_key": "const holder = {valueOf: 'x'}; return element.getAttribute(holder.valueOf) === null;",
    "dynamic_key": "const holder = {x: 'x'}; return element.getAttribute(holder[element.getAttribute('key')]) === null;",
    "escaped_holder": "const holder = {key: 'x'}; element.saved = holder; return element.getAttribute(holder.key) === null;",
    "returned_holder": "const holder = {key: 'x'}; element.getAttribute(holder.key); return holder;",
    "nested_write": "const holder = {key: 'x'}; if (element.hasAttribute('x')) holder.key = 'marker'; return element.getAttribute(holder.key) === null;",
    "deleted_field": "const holder = {key: 'x'}; delete holder.key; return element.getAttribute(holder.key) === null;",
    "unknown_stored_effect": "const holder = {key: 'x', unused: element.unknown()}; return element.getAttribute(holder.key) === null;",
    "ambient_stored_effect": "const holder = {key: 'x', unused: ambient()}; return element.getAttribute(holder.key) === null;",
    "overwritten_effect": "const holder = {key: 'x', unused: ambient()}; holder.unused = 0; return element.getAttribute(holder.key) === null;",
    "self_store": "const holder = {key: 'x'}; holder.self = holder; return element.getAttribute(holder.key) === null;",
    "fake_dom_receiver": "const holder = {element: {}}; return holder.element.getAttribute('x') === null;",
    "unused_callable": "const holder = {key: 'x', unused() { ambient(); }}; return element.getAttribute(holder.key) === null;",
}
ORDER_CHECKS = """assert(doc.read().attribute_value(node, state) == "after");
            assert(doc.read().attribute_value(other_node, atoms.intern("other")) == "after");
            assert(doc.read().attribute_value(other_node, state) == "different");"""
FIELD_CHECKS["class_method_transitive_order"] = ORDER_CHECKS
FUNCTION = re.compile(r"^  ctjs.func (?:private )?@([^ (]+)\([^\n]*\n.*?^  }\n", re.M | re.S)


def source(name, body):
    parameters = "element, other" if name in CASES or name in TWO_ELEMENT_CLASSES else "element"
    return f"function {name}({parameters}) {{\n  {body}}}\n"


def check_oracles(args):
    cases = CLASS_CASES | CASES
    declarations = "".join(source(name, body) for name, (body, _) in cases.items())
    observations, expected = [], []
    for name, (_, bits) in cases.items():
        for value, bit in zip(("null", "''", r"'a\0b'", r"'\u00e9'"), bits):
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
            f"console.log('observation{i:03}=' + observation{i:03});"
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
    cases = CLASS_CASES | CASES
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
        for owned in (False, True):
            request = dict(
                contract,
                provider="ctbrowser-dom-session-v1" if owned else "ctbrowser-dom-v1",
                initial_intrinsics=["__ctbrowser_class_defined"],
            )
            classes.prepare(args, f"{name}-{owned}", ir, request, success=False)
            refusals += 1
            if name not in CLASS_CASES:
                dom.lower(args, ir, request, f"{name}-{owned}", success=False)
                refusals += 1
                continue
            prepared.append((name, owned, ir, request))
            for control, changed in (
                ("missing-entry", dict(request, entry="missing$999")),
                ("missing-element", dict(request, element_parameters=[])),
                ("stale", dict(request, module_sha256="0" * 64)),
                ("no-authority", dict(request, initial_intrinsics=[])),
                (
                    "mixed-dom-authority",
                    dict(request, initial_intrinsics=["__ctbrowser_class_defined", "Object"]),
                ),
                (
                    "extra-authority",
                    dict(request, initial_intrinsics=["__ctbrowser_class_defined", "Error"]),
                ),
            ):
                dom.lower(args, ir, changed, f"{name}-{owned}-{control}", success=False)
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
                dom.lower(args, ir, contract, f"{name}-{owned}-{optimize}", optimize=optimize),
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
