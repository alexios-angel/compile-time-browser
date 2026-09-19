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
FILTER_PREDICATE = 't => t.startsWith("bs") && !t.startsWith("bsConfig")'
FILTER_CLASS = (
    """function datasetKeys(t) {
    return Object.keys(t.dataset).filter("""
    + FILTER_PREDICATE
    + """).length;
  }
  class Shape {
    constructor(element) { this.element = element; }
    read() { return datasetKeys(this.element); }
  }
  const shape = new Shape(element);
  const count = shape.read();
  return 0 < count && count < 2 && element.getAttribute('x') === null;
"""
)
FILTER_CASES = {
    "class_filter_captured": (FILTER_CLASS, "1000"),
    "class_filter_multiple": (
        FILTER_CLASS.replace("const count =", "shape.read(); const count ="),
        "1000",
    ),
}
FILTER_REFUSALS = {
    "class_filter_unknown": FILTER_CLASS.replace('t.startsWith("bs")', "unknown(t)"),
    "class_filter_capture": FILTER_CLASS.replace('t.startsWith("bs")', "t === element"),
    "class_filter_index": FILTER_CLASS.replace(FILTER_PREDICATE, "(t, i) => i === 0"),
    "class_filter_this": FILTER_CLASS.replace(FILTER_PREDICATE, "function(t) { return this; }"),
    "class_filter_nested": FILTER_CLASS.replace(
        FILTER_PREDICATE, "t => { function hidden() { return t; } return hidden(); }"
    ),
    "class_filter_escape": FILTER_CLASS.replace(
        "return Object.keys(t.dataset).filter(" + FILTER_PREDICATE + ").length;",
        "const callback = " + FILTER_PREDICATE + '; t.setAttribute("leak", callback); '
        "return Object.keys(t.dataset).filter(callback).length;",
    ),
    "class_filter_unused_effect": FILTER_CLASS.replace(
        "    read()",
        "    unused() { datasetKeys(this.element); this.element.unknown(); }\n    read()",
    ),
    "class_filter_later_input": FILTER_CLASS.replace(
        "const count =", "datasetKeys({}); const count ="
    ),
}
# Complete vendor H stays a refusal until every original holder slot is proved.
BOOTSTRAP_H = """    const H = {
        setDataAttribute(t, e, i) {
            t.setAttribute(`data-bs-${F(e)}`, i)
        },
        removeDataAttribute(t, e) {
            t.removeAttribute(`data-bs-${F(e)}`)
        },
        getDataAttributes(t) {
            if (!t) return {};
            const e = {},
                i = Object.keys(t.dataset).filter(t => t.startsWith("bs") && !t.startsWith("bsConfig"));
            for (const n of i) {
                let i = n.replace(/^bs/, "");
                i = i.charAt(0).toLowerCase() + i.slice(1), e[i] = M(t.dataset[n])
            }
            return e
        },
        getDataAttribute: (t, e) => M(t.getAttribute(`data-bs-${F(e)}`))
    };
"""
FULL_H = BOOTSTRAP_M + strings.BOOTSTRAP_F + BOOTSTRAP_H + """class Shape {
    constructor(element) { this.element = element; }
    read() { return H.getDataAttribute(this.element, 'config'); }
  }
  return typeof new Shape(element).read() === 'object';
"""
FILTER_REFUSALS["class_filter_full_h"] = FULL_H
# Original H slot and M/F bodies, with a class result supplying the source observation.
H_CAPTURE_CLASS = ATTRIBUTE_HELPERS + CLASS + """  element.setAttribute('data-bs-config',
    element.getAttribute(shape.read()) === null ? '%7B%7D' : '%');
  return typeof H.getDataAttribute(element, 'config') === 'object';
"""
H_CAPTURE_CASES = {
    "class_h_captures": (H_CAPTURE_CLASS, "1000"),
    "class_h_captures_multiple": (
        H_CAPTURE_CLASS.replace(
            "  return typeof", "  H.getDataAttribute(element, 'config');\n  return typeof"
        ),
        "1000",
    ),
    "class_h_captures_fallback": (
        H_CAPTURE_CLASS.replace("'%7B%7D' : '%'", "'not%20json' : '%7B%7D'").replace(
            "=== 'object'", "=== 'string'"
        ),
        "1000",
    ),
}
H_CAPTURE_REFUSALS = {
    "class_h_capture_m_replaced": H_CAPTURE_CLASS.replace(
        "class Shape", "M = function(t) { return t; }; class Shape"
    ),
    "class_h_capture_f_replaced": H_CAPTURE_CLASS.replace(
        "class Shape", "F = function(t) { return t; }; class Shape"
    ),
    "class_h_capture_m_escape": H_CAPTURE_CLASS.replace(
        "(t, e) => M(", "(t, e) => (t.setAttribute('leak', M), M("
    ).replace("F(e)}`))", "F(e)}`)))"),
    "class_h_capture_caught_effect": H_CAPTURE_CLASS.replace("return t\n", "return unknown(t)\n"),
    "class_h_capture_callback_effect": H_CAPTURE_CLASS.replace("t.toLowerCase()", "unknown(t)"),
    "class_h_capture_later_key": H_CAPTURE_CLASS.replace(
        "  return typeof", "  H.getDataAttribute(element, 'Config');\n  return typeof"
    ),
    "class_h_capture_unused": H_CAPTURE_CLASS.replace(
        "const H = {", "const H = { unused(t) { return M(t); },"
    ),
    "class_h_capture_holder_escape": H_CAPTURE_CLASS.replace(
        "  return typeof", "  element.setAttribute('leak', H);\n  return typeof"
    ),
    "class_h_capture_object": H_CAPTURE_CLASS.replace(
        "read() { return this.key; }", "read() { return H.getDataAttribute(this.key, 'config'); }"
    ),
}
H_CAPTURE_CASES["class_h_capture_later_key"] = (
    H_CAPTURE_REFUSALS.pop("class_h_capture_later_key"),
    "1000",
)
# The class method is the original H slot's only caller.
H_OBJECT_CLASS = ATTRIBUTE_HELPERS + """class Shape {
    constructor(element) { this.element = element; }
    read(key) { return H.getDataAttribute(this.element, key); }
  }
  const shape = new Shape(element);
  element.setAttribute('data-bs-config',
    element.getAttribute('x') === null ? '%7B%7D' : '%');
  return typeof shape.read('config') === 'object';
"""
H_OBJECT_CASES = {
    "class_h_object": (H_OBJECT_CLASS, "1000"),
    "class_h_object_order": (
        H_OBJECT_CLASS.replace(
            "  return typeof shape.read('config') === 'object';",
            """  const first = shape.read('config');
  element.setAttribute('data-bs-config', 'not%20json');
  const second = shape.read('config');
  element.setAttribute('data-bs-toggle', '%7B%7D');
  const third = shape.read('toggle');
  element.setAttribute('data-bs-config', '%');
  const last = shape.read('config');
  return typeof first === 'object' && second === 'not%20json' &&
    typeof third === 'object' && last === '%';""",
        ),
        "1000",
    ),
    "class_h_object_branch": (
        H_OBJECT_CLASS.replace(
            "read(key) { return H.getDataAttribute(this.element, key); }",
            """read(key) {
      if (this.element.getAttribute('x') === null)
        return H.getDataAttribute(this.element, key);
      return H.getDataAttribute(this.element, 'toggle');
    }""",
        ).replace(
            "  return typeof", "  element.setAttribute('data-bs-toggle', '%');\n  return typeof"
        ),
        "1000",
    ),
}
H_OBJECT_REFUSALS = {
    "class_h_object_rebound": H_OBJECT_CLASS.replace("const H =", "let H =").replace(
        "  const shape", "  H = { getDataAttribute(t, e) { return null; } };\n  const shape"
    ),
    "class_h_object_alias": H_OBJECT_CLASS.replace(
        "class Shape", "const alias = H; class Shape"
    ).replace("return H.getDataAttribute", "return alias.getDataAttribute"),
    "class_h_object_escape": H_OBJECT_CLASS.replace(
        "read(key) {", "read(key) { this.element.setAttribute('leak', H);"
    ),
    "class_h_object_slot_replaced": H_OBJECT_CLASS.replace(
        "  const shape", "  H.getDataAttribute = function(t, e) { return null; };\n  const shape"
    ),
    "class_h_object_unused_replace": H_OBJECT_CLASS.replace(
        "    read(key)",
        "    unused() { H.getDataAttribute = function(t, e) { return null; }; }\n    read(key)",
    ),
    "class_h_object_later_key": H_OBJECT_CASES["class_h_object_order"][0].replace(
        "const last = shape.read('config')", "const last = shape.read('Config')"
    ),
    "class_h_object_later_target": H_OBJECT_CLASS.replace(
        "  return typeof", "  shape.read('config'); shape.element = {};\n  return typeof"
    ),
    "class_h_object_unused_slot": H_OBJECT_CLASS.replace(
        "const H = {", "const H = { unused() { return 'safe'; },"
    ),
    "class_h_object_caught_effect": H_OBJECT_CLASS.replace("return t\n", "return unknown(t)\n"),
}
H_OBJECT_REFUSALS["class_h_object_order_value_compare"] = H_OBJECT_CASES["class_h_object_order"][0]
H_OBJECT_CASES["class_h_object_order"] = (
    H_OBJECT_CASES["class_h_object_order"][0]
    .replace("second === 'not%20json'", "typeof second === 'string'")
    .replace("last === '%'", "typeof last === 'string'"),
    "1000",
)
H_OBJECT_CASES["class_h_object_alias"] = (H_OBJECT_REFUSALS.pop("class_h_object_alias"), "1000")
H_OBJECT_CASES["class_h_object_unused_slot"] = (
    H_OBJECT_REFUSALS.pop("class_h_object_unused_slot"),
    "1000",
)
H_CAPTURE_CASES.update(H_OBJECT_CASES)
H_CAPTURE_REFUSALS.update(H_OBJECT_REFUSALS)
for cases in (M_CASES, NUMBER_CASES, F_CASES, CLASS_CASES):
    cases.update(H_CAPTURE_CASES)
for refusals in (M_REFUSALS, NUMBER_REFUSALS, F_REFUSALS, CLASS_REFUSALS):
    refusals.update(H_CAPTURE_REFUSALS)
HOLDER_CLASS = (
    """const holder = {
    keys(t) { return Object.keys(t.dataset).filter("""
    + FILTER_PREDICATE
    + """).length; },
    read(t, key) { return t.getAttribute(key); }
  };
  """
    + CLASS
    + """  const count = holder.keys(element);
  const saved = holder.read(element, shape.read());
  return 0 < count && count < 2 && saved === null;
"""
)
FILTER_CASES.update(
    {
        "class_holder_filter": (HOLDER_CLASS, "1000"),
        "class_holder_filter_multiple": (
            HOLDER_CLASS.replace("const count =", "holder.keys(element); const count ="),
            "1000",
        ),
    }
)
FILTER_REFUSALS.update(
    {
        "class_holder_conditional_use": HOLDER_CLASS.replace(
            "  const saved = holder.read(element, shape.read());\n", ""
        ).replace("saved === null", "holder.read(element, shape.read()) === null"),
        "class_holder_unused": HOLDER_CLASS.replace(
            "    keys(t)", "    unused() { return 'safe'; },\n    keys(t)"
        ),
        "class_holder_unused_effect": HOLDER_CLASS.replace(
            "    keys(t)", "    unused(t) { return unknown(t); },\n    keys(t)"
        ),
        "class_holder_unused_coercion": HOLDER_CLASS.replace(
            "    keys(t)", "    unused() { return String({}); },\n    keys(t)"
        ),
        "class_holder_unknown": HOLDER_CLASS.replace('t.startsWith("bs")', "unknown(t)"),
        "class_holder_capture": HOLDER_CLASS.replace('t.startsWith("bs")', "t === element"),
        "class_holder_index": HOLDER_CLASS.replace(FILTER_PREDICATE, "(t, i) => i === 0"),
        "class_holder_this": HOLDER_CLASS.replace(FILTER_PREDICATE, "function(t) { return this; }"),
        "class_holder_escape": HOLDER_CLASS.replace(
            "const count =", "element.setAttribute('leak', holder); const count ="
        ),
        "class_holder_callback_escape": HOLDER_CLASS.replace(
            "return Object.keys(t.dataset).filter(" + FILTER_PREDICATE + ").length;",
            "const callback = " + FILTER_PREDICATE + '; t.setAttribute("leak", callback); '
            "return Object.keys(t.dataset).filter(callback).length;",
        ),
        "class_holder_replaced": HOLDER_CLASS.replace(
            "const count =", "holder.keys = function(t) { return 1; }; const count ="
        ),
        "class_holder_later_input": HOLDER_CLASS.replace(
            "const count =", "holder.keys({}); const count ="
        ),
        "class_holder_detached": HOLDER_CLASS.replace(
            "const count = holder.keys(element);",
            "const read = holder.keys; const count = read(element);",
        ),
    }
)
FILTER_CASES["class_holder_unused"] = (FILTER_REFUSALS.pop("class_holder_unused"), "1000")
# Bounded composition: original M and predicate share one slot's callee.
# Complete H's dynamic-key loop and unused slots remain separate proof obligations.
H_COMBINED_CLASS = (
    BOOTSTRAP_M
    + """const H = {
    read(t, key) {
      const count = Object.keys(t.dataset).filter("""
    + FILTER_PREDICATE
    + """).length;
      const saved = M(t.getAttribute(key));
      return 0 < count && count < 2 && typeof saved === 'object';
    }
  };
  """
    + CLASS
    + "  return H.read(element, shape.read());\n"
)
H_COMBINED_METHOD = H_COMBINED_CLASS.replace(
    CLASS,
    """class Shape { read(target, key) { return H.read(target, key); } }
  const shape = new Shape();
""",
).replace("H.read(element, shape.read())", "shape.read(element, 'x')")
H_COMBINED_CASES = {
    "class_h_combined": (H_COMBINED_CLASS, "1100"),
    "class_h_combined_method": (H_COMBINED_METHOD, "1100"),
    "class_h_combined_order": (
        H_COMBINED_METHOD.replace(
            "  return shape.read(element, 'x');",
            """  const saved = shape.read(element, 'x');
  element.setAttribute('x', '%7B%7D');
  const after = shape.read(element, 'x');
  return saved && after;""",
        ),
        "1100",
    ),
}
H_COMBINED_REFUSALS = {
    "class_h_combined_effect": H_COMBINED_CLASS.replace('t.startsWith("bs")', "unknown(t)"),
    "class_h_combined_capture": H_COMBINED_CLASS.replace('t.startsWith("bs")', "t === element"),
    "class_h_combined_this": H_COMBINED_CLASS.replace(
        FILTER_PREDICATE, "function(t) { return this; }"
    ),
    "class_h_combined_escape": H_COMBINED_CLASS.replace(
        "const count = Object.keys(t.dataset).filter(" + FILTER_PREDICATE + ").length;",
        "const callback = " + FILTER_PREDICATE + "; t.setAttribute('leak', callback); "
        "const count = Object.keys(t.dataset).filter(callback).length;",
    ),
    "class_h_combined_m_escape": H_COMBINED_CLASS.replace(
        "const saved = M(", "t.setAttribute('leak', M); const saved = M("
    ),
    "class_h_combined_m_replaced": H_COMBINED_CLASS.replace(
        "class Shape", "M = function(t) { return t; }; class Shape"
    ),
    "class_h_combined_later_input": H_COMBINED_METHOD.replace(
        "  return shape.read", "  shape.read({}, 'x');\n  return shape.read"
    ),
    "class_h_combined_unused": H_COMBINED_CLASS.replace(
        "const H = {", "const H = { unused(t) { return M(t); },"
    ),
}
for cases in (FILTER_CASES, M_CASES, NUMBER_CASES):
    cases.update(H_COMBINED_CASES)
for refusals in (FILTER_REFUSALS, M_REFUSALS, NUMBER_REFUSALS):
    refusals.update(H_COMBINED_REFUSALS)
DYNAMIC_BODY = "const keys = Object.keys(t.dataset).filter(" + FILTER_PREDICATE + """);
      let joined = '';
      for (let i = 0; i < keys.length; i = i + 1) {
        const n = keys[i];
        joined = joined + t.dataset[n] + '|';
      }
      return joined;
"""
DYNAMIC_INDEXED_BODY = DYNAMIC_BODY
DYNAMIC_BODY = DYNAMIC_BODY.replace(
    "for (let i = 0; i < keys.length; i = i + 1) {\n        const n = keys[i];",
    "for (const n of keys) {",
)
DYNAMIC_HELPER = (
    "function readDataset(t) { "
    + DYNAMIC_BODY
    + "}\n"
    + CLASS
    + "return readDataset(element) === 'value|' && element.getAttribute(shape.read()) === null;\n"
)
DYNAMIC_METHOD = (
    "class Shape { constructor(t) { this.element = t; } read() { const t = this.element; "
    + DYNAMIC_BODY
    + "} } return new Shape(element).read() === 'value|' && element.getAttribute('x') === null;\n"
)
DYNAMIC_HOLDER = (
    "const H = { read(t) { " + DYNAMIC_BODY + "} }; class Shape { read(t) { return H.read(t); } }\n"
    "const shape = new Shape();\n"
    "return shape.read(element) === 'value|' && element.getAttribute('x') === null;\n"
)
DYNAMIC_CAPTURE = DYNAMIC_HOLDER
DYNAMIC_HOLDER = (
    DYNAMIC_HOLDER.replace(
        "class Shape { read(t) { return H.read(t); } }",
        "class Shape { constructor() { this.key = 'x'; } }",
    )
    .replace("shape.read(element)", "H.read(element)")
    .replace("element.getAttribute('x')", "element.getAttribute(shape.key)")
)
DYNAMIC_OUTPUT = (
    DYNAMIC_HOLDER.replace("let joined = '';", "const result = {};")
    .replace("joined = joined + t.dataset[n] + '|';", "result[n] = t.dataset[n];")
    .replace("return joined;", "return result;")
    .replace("H.read(element) === 'value|'", "typeof H.read(element) === 'object'")
)
DYNAMIC_CASES = {
    "class_dynamic_holder": (DYNAMIC_HOLDER, "1000"),
    "class_dynamic_output": (DYNAMIC_OUTPUT, "1000"),
    "class_dynamic_prefix": (
        DYNAMIC_OUTPUT.replace("result[n]", "result[n.replace(/^bs/, '')]"),
        "1000",
    ),
}
DYNAMIC_REFUSALS = {
    "class_dynamic_repeated": DYNAMIC_HOLDER.replace(
        "return H.read(element)", "H.read(element); return H.read(element)"
    ),
    "class_dynamic_helper": DYNAMIC_HELPER,
    "class_dynamic_method": DYNAMIC_METHOD,
    "class_dynamic_capture": DYNAMIC_CAPTURE,
    "class_dynamic_indexed": DYNAMIC_METHOD.replace(DYNAMIC_BODY, DYNAMIC_INDEXED_BODY),
    "class_dynamic_stale": DYNAMIC_HOLDER.replace(
        "let joined", "t.setAttribute('data-bs-later', 'x'); let joined"
    ),
    "class_dynamic_changed_keys": DYNAMIC_HOLDER.replace(
        "let joined", "keys[0] = 'absent'; let joined"
    ),
    "class_dynamic_transformed": DYNAMIC_HOLDER.replace("t.dataset[n]", "t.dataset[n + '']"),
    "class_dynamic_object_key": DYNAMIC_HOLDER.replace("t.dataset[n]", "t.dataset[{}]"),
    "class_dynamic_prototype": DYNAMIC_OUTPUT.replace("result[n]", "result['__proto__']"),
    "class_dynamic_two_writers": DYNAMIC_OUTPUT.replace(
        "result[n] =", "result[n] = null; result[n] ="
    ),
    "class_dynamic_later_input": DYNAMIC_HOLDER.replace(
        "return H.read(element)", "H.read({}); return H.read(element)"
    ),
    "class_dynamic_unused": DYNAMIC_HOLDER.replace(
        "const H = {", "const H = { unused(t) { return t[t]; },"
    ),
    "class_dynamic_unused_method": DYNAMIC_METHOD.replace(
        "read() {", "unused() { return this.element[this.element]; } read() {"
    ),
}
DYNAMIC_CASES.update(
    {
        "class_dynamic_method": (DYNAMIC_REFUSALS.pop("class_dynamic_method"), "1000"),
        "class_dynamic_capture": (DYNAMIC_REFUSALS.pop("class_dynamic_capture"), "1000"),
        "class_dynamic_method_alias": (
            DYNAMIC_METHOD.replace(
                "const t = this.element;", "const self = this; const t = self.element;"
            ),
            "1000",
        ),
        "class_dynamic_parameter": (
            "class Shape { read(t) { " + DYNAMIC_BODY + "} } const shape = new Shape(); "
            "return shape.read(element) === 'value|' && element.getAttribute('x') === null;",
            "1000",
        ),
        "class_dynamic_method_branch": (
            DYNAMIC_METHOD.replace(
                "joined = joined + t.dataset[n] + '|';",
                "if (n.startsWith('bs')) { joined = joined + t.dataset[n] + '|'; }",
            ),
            "1000",
        ),
    }
)
DYNAMIC_REFUSALS.update(
    {
        "class_dynamic_method_changing": DYNAMIC_METHOD.replace("const t =", "let t =").replace(
            "joined = joined +", "t = {}; joined = joined +"
        ),
        "class_dynamic_method_later_write": DYNAMIC_METHOD.replace("const t =", "let t =").replace(
            "return joined;", "t = {}; return joined;"
        ),
        "class_dynamic_method_callback_this": DYNAMIC_METHOD.replace(
            FILTER_PREDICATE, "t => this.element"
        ),
        "class_dynamic_method_callback_identity": DYNAMIC_METHOD.replace(
            FILTER_PREDICATE, "function callback(t) { return callback; }"
        ),
        "class_dynamic_method_repeated": DYNAMIC_METHOD.replace(
            "return new Shape(element).read()",
            "const shape = new Shape(element); shape.read(); return shape.read()",
        ),
        "class_dynamic_method_detached": DYNAMIC_METHOD.replace(
            "return new Shape(element).read()",
            "const shape = new Shape(element); const read = shape.read; return read()",
        ),
        "class_dynamic_method_unknown": DYNAMIC_METHOD.replace("t.dataset[n]", "t.unknown(n)"),
        "class_dynamic_method_unused_loop": DYNAMIC_METHOD.replace(
            "read() {", "unused(t) { " + DYNAMIC_BODY + "} read() {"
        ),
        "class_dynamic_parameter_early": DYNAMIC_CASES["class_dynamic_parameter"][0].replace(
            "const keys =", "if (t.getAttribute('x') !== null) return ''; const keys ="
        ),
    }
)
# Direct entry helpers use the same original callback and receiver census as
# captured helpers; argument effects and returned snapshots keep source order.
DYNAMIC_HELPER_METHOD = (
    "function readDataset(t) { " + DYNAMIC_BODY + "}\n"
    "class Shape { constructor(t) { this.element = t; } "
    "read() { return readDataset(this.element); } }\n"
    "return new Shape(element).read() === 'value|' && element.getAttribute('x') === null;\n"
)
DYNAMIC_HELPER_ORDER = DYNAMIC_HELPER.replace(
    "const keys =", "t.setAttribute('marker', 'helper'); const keys ="
).replace(
    "return readDataset(element) === 'value|'",
    "return readDataset((element.setAttribute('marker', 'argument'), element)) === 'value|' "
    "&& element.getAttribute('marker') === 'helper'",
)
DYNAMIC_CASES.update(
    {
        "class_dynamic_helper": (DYNAMIC_REFUSALS.pop("class_dynamic_helper"), "1000"),
        "class_dynamic_helper_method": (DYNAMIC_HELPER_METHOD, "1000"),
        "class_dynamic_helper_order": (DYNAMIC_HELPER_ORDER, "1000"),
        "class_dynamic_helper_snapshot": (
            DYNAMIC_HELPER.replace(
                "return readDataset(element)",
                "const saved = readDataset(element); "
                "element.setAttribute('data-bs-toggle', 'changed'); return saved",
            ),
            "1000",
        ),
    }
)
DYNAMIC_REFUSALS.update(
    {
        "class_dynamic_helper_this": DYNAMIC_HELPER.replace(FILTER_PREDICATE, "t => this"),
        "class_dynamic_helper_callback_identity": DYNAMIC_HELPER.replace(
            FILTER_PREDICATE, "function callback(t) { return callback; }"
        ),
        "class_dynamic_helper_effect": DYNAMIC_HELPER.replace(FILTER_PREDICATE, "t => unknown(t)"),
        "class_dynamic_helper_escape": DYNAMIC_HELPER.replace(
            "return readDataset(element)",
            "element.setAttribute('leak', readDataset); return readDataset(element)",
        ),
        "class_dynamic_helper_replaced": DYNAMIC_HELPER.replace(
            "return readDataset(element)",
            "readDataset = function(t) { return ''; }; return readDataset(element)",
        ),
        "class_dynamic_helper_invalid": DYNAMIC_HELPER.replace(
            "readDataset(element)", "readDataset({})"
        ),
        "class_dynamic_helper_receiver": DYNAMIC_HELPER.replace(
            "let joined = '';", "let joined = this;"
        ),
        "class_dynamic_helper_new_target": DYNAMIC_HELPER.replace(
            "let joined = '';", "let joined = new.target;"
        ),
        "class_dynamic_helper_unused": DYNAMIC_HELPER.replace(
            "return readDataset(element) === 'value|' &&", "return"
        ),
        "class_dynamic_helper_repeated": DYNAMIC_HELPER.replace(
            "return readDataset(element)", "readDataset(element); return readDataset(element)"
        ),
        "class_dynamic_helper_early": DYNAMIC_HELPER.replace(
            "const keys =", "if (t.getAttribute('x') !== null) return ''; const keys ="
        ),
    }
)
# Each sequential iterator reuses the complete proof of its original prefix.
for name in (
    "class_dynamic_repeated",
    "class_dynamic_method_repeated",
    "class_dynamic_helper_repeated",
):
    DYNAMIC_CASES[name] = (DYNAMIC_REFUSALS.pop(name), "1000")
DYNAMIC_CASES.update(
    {
        "class_dynamic_three": (
            DYNAMIC_HELPER.replace(
                "return readDataset(element)",
                "readDataset(element); readDataset(element); return readDataset(element)",
            ),
            "1000",
        ),
        "class_dynamic_sequential_saved": (
            DYNAMIC_HELPER.replace(
                "return readDataset(element) === 'value|'",
                "const saved = readDataset(element); "
                "const next = readDataset(element); return saved + next === 'value|value|'",
            ),
            "1000",
        ),
        "class_dynamic_sequential_write": (
            DYNAMIC_HELPER.replace(
                "return readDataset(element) === 'value|'",
                "const saved = readDataset(element); element.setAttribute('marker', 'between'); "
                "const next = readDataset(element); return saved + next === 'value|value|' "
                "&& element.getAttribute('marker') === 'between'",
            ),
            "1000",
        ),
    }
)
DYNAMIC_REFUSALS.update(
    {
        "class_dynamic_sequential_invalid": DYNAMIC_HELPER.replace(
            "return readDataset(element)", "readDataset(element); return readDataset({})"
        ),
        "class_dynamic_sequential_effect": DYNAMIC_HELPER.replace(
            "return readDataset(element)",
            "readDataset(element); element.unknown(); return readDataset(element)",
        ),
        "class_dynamic_sequential_callback": DYNAMIC_HELPER.replace(
            "return readDataset(element)",
            "readDataset(element); Object.keys(element.dataset).filter(t => unknown(t)); "
            "return readDataset(element)",
        ),
        "class_dynamic_nested": DYNAMIC_HELPER.replace(
            "joined = joined + t.dataset[n] + '|';",
            "for (const other of keys) { joined = joined + t.dataset[other] + '|'; }",
        ),
        "class_dynamic_sequential_stale": DYNAMIC_HELPER.replace(
            "return joined;",
            "t.setAttribute('data-bs-new', 'later'); "
            "for (const other of keys) { joined = joined + t.dataset[other] + '|'; } "
            "return joined;",
        ),
        "class_dynamic_holder_early": DYNAMIC_HOLDER.replace(
            "const keys =", "if (t.getAttribute('x') !== null) return ''; const keys ="
        ),
        "class_dynamic_helper_early_inverse": DYNAMIC_HELPER.replace(
            "const keys =", "if (t.getAttribute('x') === null) return ''; const keys ="
        ).replace(
            "element.getAttribute(shape.read()) === null",
            "element.getAttribute(shape.read()) !== null",
        ),
        "class_dynamic_conditional_sequential": DYNAMIC_HELPER.replace(
            "return readDataset(element) === 'value|'",
            "const saved = readDataset(element); "
            "if (element.getAttribute('x') !== null) return false; "
            "return saved + readDataset(element) === 'value|value|'",
        ),
        "class_dynamic_conditional_nested_if": DYNAMIC_HELPER.replace(
            DYNAMIC_BODY,
            "if (t.getAttribute('x') === null) { if (!t.hasAttribute('skip')) { "
            + DYNAMIC_BODY
            + "} } return '';",
        ),
        "class_dynamic_conditional_untaken_effect": DYNAMIC_HELPER.replace(
            "const keys =",
            "if (t.getAttribute('x') !== null) { unknown(); return ''; } const keys =",
        ),
        "class_dynamic_conditional_changed_keys": DYNAMIC_HELPER.replace(
            "let joined = '';",
            "if (t.hasAttribute('x')) keys[0] = 'absent'; let joined = '';",
        ).replace("const keys =", "if (t.hasAttribute('skip')) return ''; const keys ="),
        "class_dynamic_conditional_stale": DYNAMIC_HELPER.replace(
            "let joined = '';",
            "if (t.hasAttribute('x')) t.setAttribute('data-bs-later', 'later'); let joined = '';",
        ).replace("const keys =", "if (t.hasAttribute('skip')) return ''; const keys ="),
        "class_dynamic_conditional_nested_loop": DYNAMIC_HELPER.replace(
            "joined = joined + t.dataset[n] + '|';",
            "for (const other of keys) { joined = joined + t.dataset[other] + '|'; }",
        ).replace("const keys =", "if (t.getAttribute('x') !== null) return ''; const keys ="),
        "class_dynamic_conditional_scalar": DYNAMIC_HELPER.replace(
            "let joined = '';",
            "let joined = ''; if (t.getAttribute('x') === null) joined = 0;",
        ).replace("const keys =", "if (t.hasAttribute('skip')) return ''; const keys ="),
    }
)
# Conditional prefixes retain their original guards; the complete entry still
# proves both arms and their joined state before publishing native code.
for name, bits in (
    ("class_dynamic_parameter_early", "1000"),
    ("class_dynamic_helper_early", "1000"),
    ("class_dynamic_holder_early", "1000"),
    ("class_dynamic_helper_early_inverse", "0111"),
    ("class_dynamic_conditional_nested_if", "1000"),
):
    DYNAMIC_CASES[name] = (DYNAMIC_REFUSALS.pop(name), bits)
# Repeated helpers keep their identity and saved results across branch transport.
CONDITIONAL_SEQUENTIAL = DYNAMIC_REFUSALS.pop("class_dynamic_conditional_sequential")
DYNAMIC_CASES.update(
    {
        "class_dynamic_conditional_sequential": (CONDITIONAL_SEQUENTIAL, "1000"),
        "class_dynamic_conditional_discarded": (
            CONDITIONAL_SEQUENTIAL.replace(
                "const saved = readDataset(element);", "readDataset(element);"
            )
            .replace("saved + readDataset(element)", "readDataset(element)")
            .replace("'value|value|'", "'value|'"),
            "1000",
        ),
        "class_dynamic_conditional_saved": (
            CONDITIONAL_SEQUENTIAL.replace(
                "return saved + readDataset(element)",
                "element.setAttribute('marker', 'between'); "
                "const next = readDataset(element); "
                "return element.getAttribute('marker') === 'between' && saved + next",
            ),
            "1000",
        ),
        "class_dynamic_conditional_three": (
            CONDITIONAL_SEQUENTIAL.replace(
                "return saved + readDataset(element) === 'value|value|'",
                "const next = readDataset(element); "
                "return saved + next + readDataset(element) === 'value|value|value|'",
            ),
            "1000",
        ),
        "class_dynamic_conditional_capture": (
            DYNAMIC_HELPER_METHOD.replace(
                "return new Shape(element).read() === 'value|'",
                "const shape = new Shape(element); const saved = shape.read(); "
                "if (element.getAttribute('x') !== null) return false; "
                "return saved + shape.read() === 'value|value|'",
            ),
            "1000",
        ),
    }
)
DYNAMIC_REFUSALS.update(
    {
        "class_dynamic_conditional_nested_calls": CONDITIONAL_SEQUENTIAL.replace(
            "if (element.getAttribute('x') !== null) return false;",
            "if (element.hasAttribute('x')) { "
            "if (element.getAttribute('x') !== null) return false; }",
        ),
        "class_dynamic_conditional_identity": CONDITIONAL_SEQUENTIAL.replace(
            "return saved + readDataset(element)",
            "return readDataset === readDataset && saved + readDataset(element)",
        ),
        "class_dynamic_conditional_escape": CONDITIONAL_SEQUENTIAL.replace(
            "return saved + readDataset(element)",
            "element.setAttribute('leak', readDataset); return saved + readDataset(element)",
        ),
        "class_dynamic_conditional_receiver": CONDITIONAL_SEQUENTIAL.replace(
            "let joined = '';", "let joined = this;"
        ),
        "class_dynamic_conditional_new_target": CONDITIONAL_SEQUENTIAL.replace(
            "let joined = '';", "let joined = new.target;"
        ),
        "class_dynamic_conditional_later_effect": CONDITIONAL_SEQUENTIAL.replace(
            "return saved + readDataset(element)",
            "element.unknown(); return saved + readDataset(element)",
        ),
        "class_dynamic_conditional_later_invalid": CONDITIONAL_SEQUENTIAL.replace(
            "return saved + readDataset(element)", "return saved + readDataset({})"
        ),
        "class_dynamic_conditional_excess": CONDITIONAL_SEQUENTIAL.replace(
            "return saved + readDataset(element)", "return saved + readDataset(element, 1)"
        ),
        "class_dynamic_conditional_excess_effect": CONDITIONAL_SEQUENTIAL.replace(
            "return saved + readDataset(element)",
            "return saved + readDataset(element, element.unknown())",
        ),
    }
)
# Dataset keys are Strings, but their contents are unknown during compilation.
# Keep the entire original F, including the callback, in every specimen.
F_DATASET = strings.BOOTSTRAP_F + DYNAMIC_HOLDER.replace(
    "joined = joined + t.dataset[n] + '|';", "joined = joined + F(n) + '|';"
).replace("=== 'value|'", "=== 'bs-toggle|'")
F_DATASET_CASES = {
    "class_f_dataset_key": (F_DATASET, "1000"),
    "class_f_dataset_value": (
        F_DATASET.replace("F(n)", "F(t.dataset[n])").replace("=== 'bs-toggle|'", "=== 'value|'"),
        "1000",
    ),
    "class_f_dataset_matches": (
        F_DATASET.replace("F(n)", "F(n + 'AAZÉ𐐀')").replace(
            "=== 'bs-toggle|'", "=== 'bs-toggle-a-a-zÉ𐐀|'"
        ),
        "1000",
    ),
    "class_f_dataset_saved": (
        F_DATASET.replace(
            "joined = joined + F(n) + '|';",
            "const saved = F(n); t.setAttribute('marker', saved); "
            "joined = joined + saved + '|';",
        ).replace(
            "return H.read(element) === 'bs-toggle|'",
            "return H.read(element) === 'bs-toggle|' && "
            "element.getAttribute('marker') === 'bs-toggle'",
        ),
        "1000",
    ),
}
F_DATASET_REFUSALS = {
    "class_f_dataset_later_nullable": F_DATASET.replace(
        "return H.read(element)",
        "const saved = H.read(element); F(element.getAttribute('x')); return saved",
    ),
    "class_f_dataset_later_object": F_DATASET.replace(
        "return H.read(element)", "const saved = H.read(element); F({}); return saved"
    ),
    "class_f_dataset_callback_changed": F_DATASET.replace("t.toLowerCase()", "t.toUpperCase()"),
    "class_f_dataset_callback_discarded_effect": F_DATASET.replace(
        "t => `-${t.toLowerCase()}`", "t => { unknown(t); return `-${t.toLowerCase()}`; }"
    ),
    "class_f_dataset_callback_offset": F_DATASET.replace(
        "t => `-${t.toLowerCase()}`", "(t, index) => `-${t.toLowerCase()}${index}`"
    ),
    "class_f_dataset_lowercase_replaced": F_DATASET.replace(
        "const shape =", "String.prototype.toLowerCase = () => 'changed'; const shape ="
    ),
    "class_f_dataset_pattern_changed": F_DATASET.replace("/[A-Z]/g", "/[a-z]/g"),
    "class_f_dataset_flags_changed": F_DATASET.replace("/[A-Z]/g", "/[A-Z]/gi"),
}
# Mutation inside a traversal still needs the existing backedge alias proof.
F_DATASET_REFUSALS["class_f_dataset_saved"] = F_DATASET_CASES.pop("class_f_dataset_saved")[0]
F_DATASET_CASES["class_f_saved_after_loop"] = (
    F_DATASET.replace(
        "return H.read(element) === 'bs-toggle|'",
        "const saved = H.read(element); element.setAttribute('marker', saved); "
        "return saved === 'bs-toggle|' && element.getAttribute('marker') === saved",
    ),
    "1000",
)
F_CASES.update(F_DATASET_CASES)
F_REFUSALS.update(F_DATASET_REFUSALS)
DYNAMIC_CASES.update(F_DATASET_CASES)
DYNAMIC_REFUSALS.update(F_DATASET_REFUSALS)
FILTER_CASES.update(DYNAMIC_CASES)
FILTER_REFUSALS.update(DYNAMIC_REFUSALS)
# Preserve the entire original method, including M, for-of and Unicode key
# normalization; unused sibling slots retain their separate refusal.
ORIGINAL_ATTRIBUTES = (
    BOOTSTRAP_M
    + "const H = {\n"
    + BOOTSTRAP_H[
        BOOTSTRAP_H.index("        getDataAttributes(t)") : BOOTSTRAP_H.index(
            "        getDataAttribute:"
        )
    ]
    + "}; class Shape { constructor() { this.key = 'x'; } }\n"
    "const shape = new Shape();\n"
    "return typeof H.getDataAttributes(element) === 'object' && element.getAttribute(shape.key) === null;\n"
)
for cases in (CLASS_CASES, FILTER_CASES, M_CASES, NUMBER_CASES, DYNAMIC_CASES):
    cases["class_dynamic_original"] = (ORIGINAL_ATTRIBUTES, "1000")
# Every original H slot has actual arguments. The earlier FULL_H specimen keeps
# its uncalled, parameterized slots and remains a separate proof boundary.
FULL_H_CALLS = (
    BOOTSTRAP_M
    + strings.BOOTSTRAP_F
    + BOOTSTRAP_H
    + """class Shape { constructor() { this.key = 'x'; } }
  const shape = new Shape();
  const dataset = H.getDataAttributes(element);
  H.setDataAttribute(element, 'config', '%7B%7D');
  const saved = H.getDataAttribute(element, 'config');
  H.removeDataAttribute(element, 'config');
  const removed = H.getDataAttribute(element, 'config');
  return typeof dataset === 'object' && typeof saved === 'object' &&
    typeof removed === 'object' && element.getAttribute('data-bs-config') === null &&
    element.getAttribute(shape.key) === null;
"""
)
FULL_H_CASES = {
    "class_h_full_calls": (FULL_H_CALLS, "1000"),
    "class_h_full_distinct_keys": (
        FULL_H_CALLS.replace(
            "H.setDataAttribute(element, 'config'", "H.setDataAttribute(element, 'toggle'"
        ).replace(
            "const saved = H.getDataAttribute(element, 'config'",
            "const saved = H.getDataAttribute(element, 'toggle'",
        ),
        "1000",
    ),
    "class_h_full_method": (
        FULL_H_CALLS.replace(
            "class Shape { constructor() { this.key = 'x'; } }\n  const shape = new Shape();",
            "class Shape { read(element) {",
        ).replace(
            "element.getAttribute(shape.key) === null;",
            "element.getAttribute('x') === null; } }\n  return new Shape().read(element);",
        ),
        "1000",
    ),
}
FULL_H_REFUSALS = {
    "class_h_full_duplicate_slot": FULL_H_CALLS.replace(
        "const H = {", "const H = { getDataAttribute(t, e) { return null; },"
    ),
    "class_h_full_accessor_slot": FULL_H_CALLS.replace(
        "const H = {", "const H = { get unused() { return 'safe'; },"
    ),
    "class_h_full_proto_slot": FULL_H_CALLS.replace(
        "const H = {", "const H = { __proto__() { return 'safe'; },"
    ),
    "class_h_full_scalar_slot": FULL_H_CALLS.replace("const H = {", "const H = { unused: 1,"),
    "class_h_full_unused_slot": FULL_H_CALLS.replace(
        "const H = {", "const H = { unused() { return 'safe'; },"
    ),
    "class_h_full_unused_effect": FULL_H_CALLS.replace(
        "const H = {", "const H = { unused() { return unknown(); },"
    ),
    "class_h_full_slot_replaced": FULL_H_CALLS.replace(
        "  const dataset =", "  H.getDataAttribute = (t, e) => null;\n  const dataset ="
    ),
    "class_h_full_slot_deleted": FULL_H_CALLS.replace(
        "  const dataset =", "  delete H.getDataAttribute;\n  const dataset ="
    ),
    "class_h_full_slot_detached": FULL_H_CALLS.replace(
        "  const saved = H.getDataAttribute(element, 'config');",
        "  const read = H.getDataAttribute;\n  const saved = read(element, 'config');",
    ),
    "class_h_full_holder_escape": FULL_H_CALLS.replace(
        "  const dataset =", "  element.setAttribute('leak', H);\n  const dataset ="
    ),
    "class_h_full_later_key": FULL_H_CALLS.replace(
        "H.removeDataAttribute(element, 'config')", "H.removeDataAttribute(element, 'Config')"
    ),
    "class_h_full_later_target": FULL_H_CALLS.replace(
        "H.removeDataAttribute(element, 'config')", "H.removeDataAttribute({}, 'config')"
    ),
}
# Fresh H results are distinct from the class prototype and each instance.
# Keep the complete original bodies while checking aliases and real writes.
FULL_H_METHOD = FULL_H_CASES["class_h_full_method"][0]
FULL_H_CASES.update(
    {
        "class_h_full_method_result_alias": (
            FULL_H_METHOD.replace("const e = {},", "const result = {}; const e = result,"),
            "1000",
        ),
        "class_h_full_method_field": (
            FULL_H_METHOD.replace(
                "class Shape { read(element) {",
                "class Shape { constructor(element) { this.element = element; } "
                "read() { const element = this.element;",
            ).replace("new Shape().read(element)", "new Shape(element).read()"),
            "1000",
        ),
    }
)
FULL_H_REFUSALS.update(
    {
        "class_h_full_method_prototype_write": FULL_H_METHOD.replace(
            "return new Shape().read(element);",
            "const prototype = Shape.prototype; "
            "prototype[element.getAttribute('slot')] = element => true; "
            "return new Shape().read(element);",
        ),
        "class_h_full_method_instance_write": FULL_H_METHOD.replace(
            "return new Shape().read(element);",
            "const shape = new Shape(); const alias = shape; "
            "alias[element.getAttribute('slot')] = element => true; "
            "return shape.read(element);",
        ),
        "class_h_full_method_parameter_write": FULL_H_METHOD.replace(
            "return new Shape().read(element);",
            "function change(target, key) { target[key] = element => true; } "
            "const shape = new Shape(); change(shape, element.getAttribute('slot')); "
            "return shape.read(element);",
        ),
        "class_h_full_method_joined_instance_write": FULL_H_METHOD.replace(
            "return new Shape().read(element);",
            "const shape = new Shape(); let alias = {}; "
            "if (element.getAttribute('slot') === null) alias = shape; "
            "alias.read = element => false; return shape.read(element);",
        ),
        "class_h_full_method_prototype_replaced": FULL_H_METHOD.replace(
            "return new Shape().read(element);",
            "Shape.prototype.read = element => true; return new Shape().read(element);",
        ),
        "class_h_full_method_instance_replaced": FULL_H_METHOD.replace(
            "return new Shape().read(element);",
            "const shape = new Shape(); shape.read = element => true; "
            "return shape.read(element);",
        ),
        "class_h_full_method_prototype_deleted": FULL_H_METHOD.replace(
            "return new Shape().read(element);",
            "delete Shape.prototype.read; return new Shape().read(element);",
        ),
        "class_h_full_method_instance_deleted": FULL_H_METHOD.replace(
            "return new Shape().read(element);",
            "const shape = new Shape(); delete shape.read; return shape.read(element);",
        ),
        "class_h_full_method_borrowed": FULL_H_METHOD.replace(
            "return new Shape().read(element);",
            "const shape = new Shape(); const borrowed = {read: shape.read}; "
            "return borrowed.read(element);",
        ),
        "class_h_full_method_prototype_escape": FULL_H_METHOD.replace(
            "return new Shape().read(element);",
            "element.saved = Shape.prototype; return new Shape().read(element);",
        ),
    }
)
# An uncalled leaf needs no invented argument values: every operation must be
# inert for all values. Keep unknown calls, conversions and captures refused.
for name, method in {
    "ignored": "unused(t) { return 'safe'; }",
    "identity": "unused(t) { return t; }",
    "typeof": "unused(t) { return typeof t; }",
    "not": "unused(t) { return !t; }",
    "void": "unused(t) { return void t; }",
    "strict": "unused(t, e) { return t === e; }",
}.items():
    FULL_H_CASES["class_h_unused_" + name] = (
        FULL_H_CALLS.replace("const H = {", "const H = { " + method + ","),
        "1000",
    )
for name, method in {
    "property": "unused(t) { return t.value; }",
    "coercion": "unused(t) { return +t; }",
    "loose": "unused(t, e) { return t == e; }",
    "call": "unused(t) { return t(); }",
    "capture": "unused() { return element; }",
    "receiver": "unused() { return this; }",
    "nested": "unused() { function hidden() { return 'safe'; } return hidden(); }",
}.items():
    FULL_H_REFUSALS["class_h_unused_" + name] = FULL_H_CALLS.replace(
        "const H = {", "const H = { " + method + ","
    )
# Conditional leaves still need proof of every original arm, without calls
# supplying parameter types or dead-branch pruning hiding an unknown effect.
for name, expression in {
    "conditional": "t ? e : t",
    "nested_conditional": "t ? (e ? typeof t : !e) : void t",
    "typed_conditional": "typeof t === 'string' ? t : e",
}.items():
    FULL_H_CASES["class_h_unused_" + name] = (
        FULL_H_CALLS.replace(
            "const H = {", "const H = { unused(t, e) { return " + expression + "; },"
        ),
        "1000",
    )
for name, expression in {
    "then_effect": "t ? (e(), t) : e",
    "else_effect": "t ? e : (e(), t)",
    "branch_property": "t ? e.value : t",
    "branch_coercion": "t ? e : +t",
    "branch_capture": "t ? e : element",
    "dead_effect": "false ? (e(), t) : t",
}.items():
    FULL_H_REFUSALS["class_h_unused_" + name] = FULL_H_CALLS.replace(
        "const H = {", "const H = { unused(t, e) { return " + expression + "; },"
    )
for name, body in {
    "early": "if (t) return e; return t;",
    "nested_early": "if (t) { if (e) return typeof t; return !e; } return void t;",
    "strict_early": "if (t === e) return true; return false;",
}.items():
    FULL_H_CASES["class_h_unused_" + name] = (
        FULL_H_CALLS.replace("const H = {", "const H = { unused(t, e) { " + body + " },"),
        "1000",
    )
for name, body in {
    "early_then_effect": "if (t) { e(); return t; } return e;",
    "early_else_effect": "if (t) return e; e(); return t;",
    "early_property": "if (t) return e.value; return t;",
    "early_dead_effect": "if (false) { e(); return t; } return e;",
    "early_loop": "while (t) { if (e) return t; t = !t; } return e;",
}.items():
    FULL_H_REFUSALS["class_h_unused_" + name] = FULL_H_CALLS.replace(
        "const H = {", "const H = { unused(t, e) { " + body + " },"
    )
FULL_H_CASES["class_h_full_unused_slot"] = (FULL_H_REFUSALS.pop("class_h_full_unused_slot"), "1000")
FULL_H_CASES["class_h_full_later_key"] = (FULL_H_REFUSALS.pop("class_h_full_later_key"), "0000")
for cases in (CLASS_CASES, FILTER_CASES, M_CASES, NUMBER_CASES, F_CASES, DYNAMIC_CASES):
    cases.update(FULL_H_CASES)
for refusals in (CLASS_REFUSALS, FILTER_REFUSALS, M_REFUSALS, NUMBER_REFUSALS, F_REFUSALS):
    refusals.update(FULL_H_REFUSALS)
# The VM still indexes bytes. Pin its known divergence separately from the
# Node/native UTF-16 contract; all pre-existing differential expectations remain.
UTF16_CASES, UTF16_VM_BITS = {}, {}
for name, text, first, rest, agrees in (
    ("empty", "", "", "", True),
    ("ascii", "ab", "a", "b", True),
    ("nul", "\0x", "\0", "x", True),
    ("bmp", "Éx", "É", "x", False),
    ("dotted", "İx", "İ", "x", False),
    ("pair", "𐐀x", "\ud801", "\udc00x", False),
    ("high", "\ud800x", "\ud800", "x", False),
    ("low", "\udc00x", "\udc00", "x", False),
    ("ascii_pair", "A𐐀", "A", "𐐀", True),
):
    name = "class_utf16_" + name
    body = (
        """class Shape { constructor() { this.key = 'x'; } }
  const shape = new Shape();
  element.setAttribute(shape.key, """
        + json.dumps(text)
        + """);
  const text = element.getAttribute(shape.key);
  if (text === null) return false;
  const first = text.charAt(0);
  element.setAttribute(shape.key, 'later');
  const rest = text.slice(1);
  return first === """
        + json.dumps(first)
        + " && rest === "
        + json.dumps(rest)
        + """
    && first + rest === text && element.getAttribute(shape.key) === 'later';
"""
    )
    UTF16_CASES[name + "_early"] = (body, "1111")
    UTF16_VM_BITS[name + "_early"] = "1111" if agrees else "0000"
    body = (
        body.replace(
            "if (text === null) return false;", "let answer = false; if (text !== null) {"
        ).replace("  return first ===", "  answer = first ===")
        + "  } return answer;\n"
    )
    UTF16_CASES[name] = (body, "1111")
    UTF16_VM_BITS[name] = "1111" if agrees else "0000"
UTF16_CASES["class_utf16_reverse_guard"] = (
    UTF16_CASES["class_utf16_ascii"][0].replace("text !== null", "null !== text"),
    "1111",
)
UTF16_CASES["class_utf16_else_guard"] = (
    UTF16_CASES["class_utf16_ascii"][0].replace(
        "if (text !== null) {", "if (text === null) { answer = false; } else {"
    ),
    "1111",
)
UTF16_REFUSALS = {
    "class_utf16_" + name: UTF16_CASES["class_utf16_ascii"][0].replace(before, after)
    for name, before, after in (
        ("char_index", "charAt(0)", "charAt(1)"),
        ("char_coercion", "charAt(0)", "charAt('0')"),
        ("char_missing", "charAt(0)", "charAt()"),
        ("char_extra", "charAt(0)", "charAt(0, 1)"),
        ("slice_index", "slice(1)", "slice(0)"),
        ("slice_extra", "slice(1)", "slice(1, 2)"),
        ("lowercase", "charAt(0)", "charAt(0).toLowerCase()"),
        ("nullable", "if (text !== null)", "if (true)"),
        ("null_arm", "if (text !== null)", "if (text === null)"),
        ("detached", "text.charAt(0)", "(0, text.charAt)(0)"),
        ("replaced", "const first =", "String.prototype.charAt = () => 'a'; const first ="),
        ("effect", "charAt(0)", "charAt(unknown())"),
    )
}
UTF16_CASES["class_utf16_lowercase"] = (UTF16_REFUSALS.pop("class_utf16_lowercase"), "1111")
CLASS_CASES.update(UTF16_CASES)
CLASS_REFUSALS.update(UTF16_REFUSALS)
# The arrow saves lexical this, but its original Bootstrap predicate never reads it.
DIRECT_FILTER_EQUALITY_CLASS = (
    "class Shape { constructor(t) { this.element = t; } read() { return "
    "Object.keys(this.element.dataset).filter(" + FILTER_PREDICATE + ").length; } }\n"
    "const shape = new Shape(element);\n"
    "return shape.read() === 1 && element.getAttribute('x') === null;\n"
)
DIRECT_FILTER_CLASS = DIRECT_FILTER_EQUALITY_CLASS.replace(
    "return shape.read() === 1", "const count = shape.read(); return 0 < count && count < 2"
)
DIRECT_FILTER_CASES = {
    "class_filter_direct_receiver": (DIRECT_FILTER_CLASS, "1000"),
    "class_filter_direct_alias": (
        DIRECT_FILTER_CLASS.replace("read() {", "read() { const alias = this;").replace(
            "this.element.dataset", "alias.element.dataset"
        ),
        "1000",
    ),
    "class_filter_direct_branch": (
        DIRECT_FILTER_CLASS.replace(
            "read() {", "read() { if (this.element.hasAttribute('x')) return 2;"
        ),
        "1000",
    ),
    "class_filter_direct_parameter": (
        DIRECT_FILTER_CLASS.replace("read() {", "read(t) {")
        .replace("this.element.dataset", "t.dataset")
        .replace("shape.read()", "shape.read(element)"),
        "1000",
    ),
    "class_filter_direct_repeated": (
        DIRECT_FILTER_CLASS.replace("const count =", "shape.read(); const count ="),
        "1000",
    ),
}
DIRECT_FILTER_PARAMETER_FIELD = DIRECT_FILTER_CASES["class_filter_direct_parameter"][0]
DIRECT_FILTER_CASES["class_filter_direct_parameter"] = (
    DIRECT_FILTER_PARAMETER_FIELD.replace(
        "constructor(t) { this.element = t; }", "constructor() {}"
    ).replace("new Shape(element)", "new Shape()"),
    "1000",
)
DIRECT_FILTER_CASES["class_filter_parameter_branch"] = (
    DIRECT_FILTER_CASES["class_filter_direct_parameter"][0].replace(
        "read(t) {", "read(t) { if (t.hasAttribute('x')) return 2;"
    ),
    "1000",
)
FILTER_CASES.update(DIRECT_FILTER_CASES)
FILTER_REFUSALS.update(
    {
        "class_filter_parameter_unused_field": DIRECT_FILTER_PARAMETER_FIELD,
        "class_filter_parameter_branch_unused_field": DIRECT_FILTER_PARAMETER_FIELD.replace(
            "read(t) {", "read(t) { if (t.hasAttribute('x')) return 2;"
        ),
        "class_filter_direct_numeric_equality": DIRECT_FILTER_EQUALITY_CLASS,
        "class_filter_parameter_numeric_equality": DIRECT_FILTER_EQUALITY_CLASS.replace(
            "read() {", "read(t) {"
        )
        .replace("this.element.dataset", "t.dataset")
        .replace("shape.read()", "shape.read(element)"),
        "class_filter_repeated_numeric_equality": DIRECT_FILTER_EQUALITY_CLASS.replace(
            "return shape.read()", "shape.read(); return shape.read()"
        ),
        "class_filter_direct_this": DIRECT_FILTER_CLASS.replace(
            FILTER_PREDICATE, "t => this.element"
        ),
        "class_filter_direct_effect": DIRECT_FILTER_CLASS.replace(
            FILTER_PREDICATE, "t => unknown(t)"
        ),
        "class_filter_direct_escape": DIRECT_FILTER_CLASS.replace(
            "return Object.keys", "this.element.setAttribute('leak', this); return Object.keys"
        ),
        "class_filter_direct_changed_cell": DIRECT_FILTER_PARAMETER_FIELD.replace(
            "read(t) {", "read(t) { t.hasAttribute('x'); t = this.element;"
        ),
        "class_filter_direct_later_input": DIRECT_FILTER_CASES["class_filter_direct_parameter"][
            0
        ].replace("const count =", "shape.read({}); const count ="),
        "class_filter_direct_unused": DIRECT_FILTER_CLASS.replace(
            "read() {", "unused() { return unknown(this); } read() {"
        ),
    }
)
FILTER_IDENTITIES = ["Object", "Array", "String"]
DYNAMIC_ITERATION = ["__ctbrowser_for_of_open", "__ctbrowser_iter_next", "__ctbrowser_iter_close"]
CLASS_CASES.update(FILTER_CASES)
CLASS_REFUSALS.update(FILTER_REFUSALS)
# Shadowed child names make the frontend box these otherwise local values.
# Neither the outer helper nor its child is called to manufacture parameter facts.
UNUSED_CELL_CASES = {
    "parameter": "function hidden(t) { return t; } const saved = t; t = e; return saved === t;",
    "branch": "function hidden(t) { return t; } const saved = t; "
    "if (t) { t = e; t = !t; } else { t = typeof e; } return saved === t;",
    "local": "function hidden(local) { return local; } let local = t; "
    "local = e; return typeof local;",
}
UNUSED_CELL_REFUSALS = {
    "invoked_child": "function hidden(t) { return t; } t = e; return hidden(t);",
    "capture": "function hidden() { return t; } t = e; return t;",
    "child_effect": "function hidden(t) { return t(); } t = e; return t;",
    "child_property": "function hidden(t) { return t.value; } t = e; return t;",
    "outer_effect": "function hidden(t) { return t; } if (false) t = e(); return t;",
    "child_escape": "function hidden(t) { return t; } t = e; return t ? hidden : t;",
}
for name, body in UNUSED_CELL_CASES.items():
    # Standalone declarations still need class initialization's helper census.
    CLASS_REFUSALS["class_unused_cell_" + name] = (
        "function unusedCell(t, e) { " + body + " }\n" + CLASS + READ
    )
for name, body in UNUSED_CELL_REFUSALS.items():
    CLASS_REFUSALS["class_unused_cell_" + name] = (
        "function unusedCell(t, e) { " + body + " }\n" + CLASS + READ
    )
for name, body in (UNUSED_CELL_CASES | UNUSED_CELL_REFUSALS).items():
    source_body = (
        "const H = { unused: function unusedCell(t, e) { "
        + body
        + " }, read(t) { return t.getAttribute('x'); } };\n"
        + CLASS
        + "return H.read(element) === null && element.getAttribute(shape.read()) === null;"
    )
    if name in UNUSED_CELL_CASES:
        CLASS_CASES["class_unused_cell_holder_" + name] = (source_body, "1000")
    else:
        CLASS_REFUSALS["class_unused_cell_holder_" + name] = source_body
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
FIELD_CASES["field_unused_helper"] = (
    "function unused(t) { return t; } return element.getAttribute('x') === null;",
    "1000",
)
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
    "unused_helper_effect": "function unused(t) { return t(); } return element.getAttribute('x') === null;",
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
                dom.lower(args, ir, request, f"{name}-{owned}", success=False, max_steps=steps)
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
