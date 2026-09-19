#!/usr/bin/env python3
"""Compose original class proofs with typed DOM entries and local fields."""

import argparse
import json
from pathlib import Path
import re
import shutil

from CTNative.Browser import native_dom as dom
from CTNative.Browser.native_dom_json import ATTRIBUTE_HELPERS, BOOTSTRAP_M
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
ERROR_CLASS = """class Shape {
    static get NAME() { throw new Error('unused NAME'); }
    constructor(element) { this.element = element; }
    read() { return this.element.getAttribute('x') === null; }
  }
  return new Shape(element).read();
"""
CLASS_CASES = {
    "class_error_unused": (ERROR_CLASS, "1000"),
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
    "class_error_read": ERROR_CLASS.replace("  return new Shape", "  Shape.NAME; return new Shape"),
    "class_error_replaced": ERROR_CLASS.replace(
        "  return new Shape", "  Error = 9; return new Shape"
    ),
    "class_error_returned": ERROR_CLASS.replace("throw new Error", "return new Error"),
    "class_error_message": ERROR_CLASS.replace("'unused NAME'", "ambient()"),
    "class_error_unknown_effect": ERROR_CLASS.replace(
        "  return new Shape", "  element.unknown(); return new Shape"
    ),
    "class_error_unused_read": ERROR_CLASS.replace(
        "    read() {", "    unused() { return this.constructor.NAME; }\n    read() {"
    ),
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
CONSTRUCTOR_CALL = """class Reader {
    constructor(target, key) { this.value = this.read(target, key); }
    read(target, key) { return target.getAttribute(key); }
  }
  const reader = new Reader(element, 'x');
  return reader.value === null;
"""
CONSTRUCTOR_CHAIN = """class Reader {
    constructor(target, key) { this.element = target; this.value = this.forward(key); }
    read(key) {
      const saved = this.element.getAttribute(key);
      this.element.setAttribute(key, 'after');
      return saved;
    }
    forward(key) { return this.read(key); }
  }
  const first = new Reader(element, 'x');
  const second = new Reader(element, 'x');
  return first.value === null && second.value === 'after';
"""
CLASS_CASES.update(
    {
        "class_constructor_call": (CONSTRUCTOR_CALL, "1000"),
        "class_constructor_chain": (CONSTRUCTOR_CHAIN, "1000"),
        "class_constructor_then_call": (
            CONSTRUCTOR_CALL.replace(
                "  return reader.value === null;",
                "  element.setAttribute('x', 'after');\n"
                "  return reader.value === null && reader.read(element, 'x') === 'after';",
            ),
            "1000",
        ),
    }
)
CLASS_REFUSALS.update(
    {
        "constructor_call_bad_key": CONSTRUCTOR_CALL.replace(
            "this.read(target, key)", "this.read(target, {})"
        ),
        "constructor_call_bad_target": CONSTRUCTOR_CALL.replace(
            "new Reader(element, 'x')", "new Reader({}, 'x')"
        ),
        "constructor_call_unused_parameter": CONSTRUCTOR_CALL.replace(
            "    read(target, key)",
            "    unused(target) { return target.getAttribute('x'); }\n    read(target, key)",
        ),
        "constructor_call_dead_bad_key": CONSTRUCTOR_CALL.replace(
            "read(target, key) { return",
            "read(target, key) { if (false) target.getAttribute({}); return",
        ),
        "constructor_chain_bad_second_instance": CONSTRUCTOR_CHAIN.replace(
            "const second = new Reader(element, 'x')", "const second = new Reader({}, 'x')"
        ),
        "constructor_chain_read_before_field": CONSTRUCTOR_CHAIN.replace(
            "this.element = target; this.value = this.forward(key)",
            "this.value = this.forward(key); this.element = target",
        ),
    }
)
NUMBER_CLASS = """class Shape {
    constructor(element) { this.element = element; }
    read() { return Number(this.element.getAttribute('x')).toString() === '0'; }
  }
  return new Shape(element).read();
"""
NUMBER_CASES = {
    "class_number_unused_identity": (
        NUMBER_CLASS.replace("    read()", "    unused() { return Number; }\n    read()"),
        "1100",
    ),
    "class_number_method": (NUMBER_CLASS, "1100"),
    "class_number_entry": (
        CLASS + "  return Number(element.getAttribute(shape.read())).toString() === '0';\n",
        "1100",
    ),
    "class_number_unused": (
        NUMBER_CLASS.replace(
            "    read()",
            "    unused() { Number(this.element.getAttribute('x')); }\n    read()",
        ),
        "1100",
    ),
    "class_error_number": (
        NUMBER_CLASS.replace(
            "    read()",
            "    static get NAME() { throw new Error('unused NAME'); }\n    read()",
        ),
        "1100",
    ),
}
NUMBER_REFUSALS = {
    "class_number_unused_escape": NUMBER_CLASS.replace(
        "    read()", "    unused() { this.element.setAttribute('leak', Number); }\n    read()"
    ),
    "class_number_unused_replace_without_calls": CLASS.replace(
        "    read()", "    unused() { Number = 9; }\n    read()"
    )
    + READ,
    "class_number_object_to_string": NUMBER_CLASS.replace(
        "Number(this.element.getAttribute('x'))", "{}"
    ),
    "class_number_unused_replace": NUMBER_CLASS.replace(
        "    read()", "    unused() { Number = 9; }\n    read()"
    ),
    "class_number_unused_object": NUMBER_CLASS.replace(
        "    read()", "    unused() { Number({}); }\n    read()"
    ),
    "class_number_replaced_after_read": NUMBER_CLASS.replace(
        "return Number(", "const saved = Number("
    ).replace("=== '0';", "=== '0'; Number = 9; return saved;"),
    "class_number_dead_unknown": NUMBER_CLASS.replace(
        "    read() {", "    read() { if (false) this.element.unknown();"
    ),
    "class_number_dead_replaced": NUMBER_CLASS.replace(
        "    read() {", "    read() { if (false) Number = 9;"
    ),
    "class_number_effectful_getter": NUMBER_CLASS.replace(
        "    read()", "    static get NAME() { return Number('0'); }\n    read()"
    ),
}
# Keep the complete original helper, including both exception-producing calls.
M_CLASS = BOOTSTRAP_M + """
  class Shape {
    constructor(element) { this.element = element; }
    read() { return this.element.getAttribute('x'); }
  }
  const shape = new Shape(element);
"""
M_CASES = {
    "class_m": (M_CLASS + "  return typeof M(shape.read()) === 'object';\n", "1100"),
    "class_m_json": (
        M_CLASS + "  const text = shape.read() === null ? '%7B%22key%22%3A1%7D' : '%';\n"
        "  return typeof M(text) === 'object';\n",
        "1000",
    ),
    "class_m_json_fallback": (
        M_CLASS + "  const text = shape.read() === null ? 'not%20json' : '%7B%7D';\n"
        "  return typeof M(text) === 'string';\n",
        "1000",
    ),
    "class_m_numeric": (
        M_CLASS + "  const text = shape.read() === null ? '42' : 'true';\n"
        "  return typeof M(text) === 'number';\n",
        "1000",
    ),
}
M_REFUSALS = {
    "class_m_unknown": M_CASES["class_m"][0].replace("return Number(t);", "return unknown(t);"),
    "class_m_caught_effect": M_CASES["class_m"][0].replace("return t\n", "return unknown(t)\n"),
    "class_m_replaced": M_CASES["class_m"][0].replace(
        "  return typeof", "  JSON = 9; return typeof"
    ),
    "class_m_missing_call": M_CASES["class_m"][0].replace("M(shape.read())", "shape.read()"),
    "class_m_bad_input": M_CASES["class_m"][0].replace("M(shape.read())", "M({})"),
    "class_m_unused_holder": M_CASES["class_m"][0].replace(
        "  const shape", "  const holder = { bad() { Number({}); } };\n  const shape"
    ),
    "class_m_unused_method": M_CASES["class_m"][0].replace(
        "    read()", "    unused() { Number({}); }\n    read()"
    ),
    "class_m_dead_effect": M_CASES["class_m"][0].replace(
        "  return typeof", "  if (false) element.unknown(); return typeof"
    ),
}
# The method is M's only caller: no entry call primes the helper proof.
M_CAPTURE_CLASS = M_CLASS.replace(
    "return this.element.getAttribute('x');", "return M(this.element.getAttribute('x'));"
)
M_CASES.update(
    {
        "class_m_captured": (
            M_CAPTURE_CLASS + "  return typeof shape.read() === 'object';\n",
            "1100",
        ),
        "class_m_captured_json": (
            M_CAPTURE_CLASS.replace(
                "M(this.element.getAttribute('x'))",
                "M(this.element.getAttribute('x') === null ? '%7B%22key%22%3A1%7D' : '%')",
            )
            + "  return typeof shape.read() === 'object';\n",
            "1000",
        ),
        "class_m_captured_fallback": (
            M_CAPTURE_CLASS.replace(
                "M(this.element.getAttribute('x'))",
                "M(this.element.getAttribute('x') === null ? 'not%20json' : '%7B%7D')",
            )
            + "  return typeof shape.read() === 'string';\n",
            "1000",
        ),
        "class_m_captured_numeric": (
            M_CAPTURE_CLASS.replace(
                "M(this.element.getAttribute('x'))",
                "M(this.element.getAttribute('x') === null ? '42' : 'true')",
            )
            + "  return typeof shape.read() === 'number';\n",
            "1000",
        ),
    }
)
M_REFUSALS.update(
    {
        "class_m_capture_replaced": M_CASES["class_m_captured"][0].replace(
            "  const shape", "  M = function(value) { return value; };\n  const shape"
        ),
        "class_m_capture_unused_replace": M_CASES["class_m_captured"][0].replace(
            "    read()", "    unused() { M = 9; }\n    read()"
        ),
        "class_m_capture_escaped": M_CASES["class_m_captured"][0].replace(
            "    read() {", "    read() { this.element.setAttribute('leak', M);"
        ),
        "class_m_capture_unused_escape": M_CASES["class_m_captured"][0].replace(
            "    read()", "    unused() { this.element.setAttribute('leak', M); }\n    read()"
        ),
        "class_m_capture_unused_effect": M_CASES["class_m_captured"][0].replace(
            "    read()",
            "    unused() { M(this.element.getAttribute('x')); this.element.unknown(); }\n"
            "    read()",
        ),
        "class_m_capture_caught_effect": M_CASES["class_m_captured"][0].replace(
            "return t\n", "return unknown(t)\n"
        ),
    }
)
NUMBER_CASES["class_number_helper"] = (
    "function numberText(text) { return Number(text).toString(); }\n"
    + CLASS
    + "  return numberText(element.getAttribute(shape.read())) === '0';\n",
    "1100",
)
NUMBER_CASES["class_number_captured_helper"] = (
    "function numberText(text) { return Number(text).toString(); }\n"
    + NUMBER_CLASS.replace(
        "Number(this.element.getAttribute('x')).toString()",
        "numberText(this.element.getAttribute('x'))",
    ),
    "1100",
)
# A nested closure still observes its enclosing callee during preparation.
# Deleting children in a different order cannot authorize erasing that identity.
NUMBER_REFUSALS["class_number_nested_helper"] = NUMBER_CASES["class_number_helper"][0].replace(
    "return Number(text)",
    "function identity(value) { return value; } return Number(identity(text))",
)
NUMBER_CASES.update(M_CASES)
NUMBER_REFUSALS.update(M_REFUSALS)
CLASS_CASES.update(NUMBER_CASES)
CLASS_REFUSALS.update(NUMBER_REFUSALS)
F_CLASS = strings.BOOTSTRAP_F + """class Shape {
    read(key) { return F(key); }
  }
  const shape = new Shape();
  return shape.read('config') === 'config' && element.getAttribute('x') === null;
"""
F_CASES = {
    "class_f_captured": (
        F_CLASS.replace("read(key) { return F(key); }", "read() { return F('config'); }").replace(
            "shape.read('config')", "shape.read()"
        ),
        "1000",
    ),
    "class_f_argument": (F_CLASS, "1000"),
    "class_f_multiple": (
        F_CLASS.replace(
            "return shape.read('config')",
            "return shape.read('toggle') === 'toggle' && shape.read('config') === 'config' "
            "&& shape.read('config')",
        ),
        "1000",
    ),
}
F_REFUSALS = {
    "class_f_matching": F_CLASS.replace("shape.read('config')", "shape.read('Config')"),
    "class_f_dynamic": F_CLASS.replace(
        "shape.read('config')", "shape.read(element.getAttribute('x'))"
    ),
    "class_f_matching_later": F_CLASS.replace(
        "return shape.read('config')",
        "shape.read('config'); return shape.read('Config')",
    ),
    "class_f_matching_first": F_CLASS.replace(
        "return shape.read('config')",
        "shape.read('Config'); return shape.read('config')",
    ),
    "class_f_unknown_later": F_CLASS.replace(
        "return shape.read('config')",
        "shape.read('config'); return shape.read(element.getAttribute('x'))",
    ),
    "class_f_unused_matching": F_CLASS.replace(
        "    read(key)", "    unused() { return F('Config'); }\n    read(key)"
    ),
    "class_f_callback_global": F_CLASS.replace("t.toLowerCase()", "unknown(t)"),
    "class_f_callback_nested": F_CLASS.replace(
        "t => `-${t.toLowerCase()}`",
        "t => { function hidden() { return t; } return hidden(); }",
    ),
    "class_f_replaced": F_CLASS.replace(
        "  const shape", "  F = function(t) { return t; };\n  const shape"
    ),
    "class_f_escape": F_CLASS.replace("read(key) { return F(key); }", "read(key) { return F; }"),
}
# Keep original observations when their complete matching-input census admits.
for name, bits in (
    ("class_f_matching", "0000"),
    ("class_f_matching_later", "0000"),
    ("class_f_matching_first", "1000"),
    ("class_f_unused_matching", "1000"),
):
    F_CASES[name] = (F_REFUSALS.pop(name), bits)
F_CASES.update(
    {
        "class_f_ascii_matches": (
            F_CLASS.replace(
                "return shape.read('config') === 'config'",
                "return shape.read('Config') === '-config' && "
                "shape.read('AAZ') === '-a-a-z' && "
                "shape.read('bsConfigExtra') === 'bs-config-extra' && "
                "shape.read('Config') === '-config'",
            ),
            "1000",
        ),
        "class_f_unicode_matches": (
            F_CLASS.replace(
                "shape.read('config') === 'config'", "shape.read('ÉA𐐀Zé') === 'É-a𐐀-zé'"
            ),
            "1000",
        ),
        "class_f_unicode_nonmatches": (
            F_CLASS.replace("shape.read('config') === 'config'", "shape.read('Éİ𐐀é') === 'Éİ𐐀é'"),
            "1000",
        ),
    }
)
F_REFUSALS.update(
    {
        "class_f_matching_callback_effect": F_CASES["class_f_matching"][0].replace(
            "t.toLowerCase()", "unknown(t)"
        ),
        "class_f_matching_callback_changed": F_CASES["class_f_matching"][0].replace(
            "t.toLowerCase()", "t.toUpperCase()"
        ),
        "class_f_matching_lowercase_replaced": F_CASES["class_f_matching"][0].replace(
            "  const shape", "  String.prototype.toLowerCase = () => 'changed';\n  const shape"
        ),
        "class_f_matching_unknown_later": F_REFUSALS["class_f_unknown_later"].replace(
            "shape.read('config');", "shape.read('Config');"
        ),
    }
)
CLASS_CASES.update(F_CASES)
CLASS_REFUSALS.update(F_REFUSALS)
