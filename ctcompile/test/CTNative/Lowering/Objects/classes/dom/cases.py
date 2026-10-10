#!/usr/bin/env python3
"""Compose original class proofs with typed DOM entries and local fields."""

import argparse
import json
from pathlib import Path
import re
import shutil

from CTNative.Browser.dom import dom
from CTNative.Browser.dom.json import ATTRIBUTE_HELPERS, BOOTSTRAP_M
from CTNative.Browser.dom import strings
from CTNative.HostContract import contract as host
from CTNative.harness import build_and_run, find_compilers, run
from CTNative.Lowering.Objects.classes import initialization as classes
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
        CLASS
        + """  const saved = element.getAttribute(shape.read());
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
    # Instance calls do not supply argument authority to a static body. The
    # static is called: an unread one is removed before the proof (8b60703a6).
    "class_static_shared_dom_helper": """const get = t => t.getAttribute('x');
  class Button {
    constructor(element) { this.element = element; }
    read() { return get(this.element) === null; }
    static unused(t) { return get(t); }
  }
  Button.unused(element);
  return new Button(element).read();
""",
    # Helpers first seen by a static retain the strict source census.
    "class_static_shared_dom_transitive": """const get = t => t.getAttribute('x');
  const wrap = t => get(t);
  class Utility { static unused(t) { return wrap(t); } }
  const utility = new Utility();
  class Button {
    constructor(element) { this.element = element; }
    read() { return get(this.element) === null; }
  }
  Utility.unused(element);
  return new Button(element).read();
""",
    # A static declared later must not inherit the earlier instance's DOM proof.
    "class_static_shared_dom_later_static": """const get = t => t.getAttribute('x');
  const wrap = t => get(t);
  class Button {
    constructor(element) { this.element = element; }
    read() { return get(this.element) === null; }
  }
  class Utility { static unused(t) { return wrap(t); } }
  const utility = new Utility();
  Utility.unused(element);
  return new Button(element).read();
""",
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
# Bootstrap's element helpers, verbatim. On a declared element `a` returns its
# argument (element facts: `jquery` is outside the VM's element name set), so
# the constructor arm is decided, the call becomes its argument and both
# helpers lose their last call.
BOOTSTRAP_ELEMENT = """r = t => !(!t || "object" != typeof t) && (void 0 !== t.jquery && (t = t[0]), void 0 !== t.nodeType),
        a = t => r(t) ? t.jquery ? t[0] : t : "string" == typeof t && t.length > 0 ? document.querySelector(n(t)) : null"""
ELEMENT_ARM = (
    "const "
    + BOOTSTRAP_ELEMENT
    + """;
  class Button {
    constructor(t) { (t = a(t)) && (this.element = t); }
    read() { return this.element.getAttribute('x') === null; }
  }
"""
)
ELEMENT_ARM_CASES = {
    "class_element_arm": (ELEMENT_ARM + "  return new Button(element).read();\n", "1000")
}
CLASS_CASES.update(ELEMENT_ARM_CASES)
# Each must refuse where named. An element expando - before or after the
# construction, or in an unused method - fails the typed DOM proof's write
# check, a prototype write fails the census, and a non-element actual leaves
# the arm undecided, so `a` keeps its call and its census.
ELEMENT_ARM_REFUSALS = {
    "element_arm_expando": (
        ELEMENT_ARM + "  element.jquery = 1;\n  return new Button(element).read();\n",
        "DOM JSON assignment requires a fresh target",
    ),
    "element_arm_expando_after": (
        ELEMENT_ARM
        + "  const button = new Button(element);\n  element.jquery = 1;\n  return button.read();\n",
        "DOM JSON assignment requires a fresh target",
    ),
    "element_arm_expando_method": (
        ELEMENT_ARM.replace("    read() {", "    mark() { this.element.jquery = 1; }\n    read() {")
        + "  return new Button(element).read();\n",
        "DOM JSON assignment requires a fresh target",
    ),
    "element_arm_prototype": (
        ELEMENT_ARM + "  Object.prototype.jquery = 1;\n  return new Button(element).read();\n",
        "(op ctjs.get_property)",
    ),
    "element_arm_object": (
        ELEMENT_ARM + "  return new Button({}).read();\n",
        'unknown call, binding or reflective effect (global "document")',
    ),
}
CLASS_REFUSALS.update({name: body for name, (body, _) in ELEMENT_ARM_REFUSALS.items()})
SNAPSHOT_CLASS = """class Shape {
    constructor(element) { this.element = element; this.x = 'ready'; }
    read() {
      const keys = Object.getOwnPropertyNames(this);
      return keys.length === 2 && keys[0] === 'element'
          && this.element.getAttribute(keys[1]) === null;
    }
  }
  const shape = new Shape(element);
"""
SNAPSHOT_CASES = {
    "class_own_keys": (SNAPSHOT_CLASS + "  return shape.read();\n", "1000"),
    "class_own_keys_entry": (
        CLASS
        + """  const keys = Object.getOwnPropertyNames(shape);
  shape.key = 'marker';
  return keys.length === 1 && keys[0] === 'key' && shape.read() === 'marker'
      && element.getAttribute('x') === null;
""",
        "1000",
    ),
    "class_own_keys_clear": (
        SNAPSHOT_CLASS.replace(
            "    read() {",
            "    dispose() { for (const key of Object.getOwnPropertyNames(this)) this[key] = null; }\n"
            "    read() {",
        )
        + """  const saved = shape.read();
  shape.dispose();
  return saved && shape.element === null && shape.x === null;
""",
        "1000",
    ),
    "class_element_arm_snapshot": (
        ELEMENT_ARM.replace(
            "    read() { return",
            "    read() { const keys = Object.getOwnPropertyNames(this);\n"
            "      return keys.length === 1 && keys[0] === 'element' &&",
        )
        + "  return new Button(element).read();\n",
        "1000",
    ),
}
ELEMENT_ARM_CASES["class_element_arm_snapshot"] = SNAPSHOT_CASES["class_element_arm_snapshot"]
SNAPSHOT_REFUSALS = {
    "own_keys_added": SNAPSHOT_CLASS + "  shape.extra = 1; return shape.read();\n",
    "own_keys_alias_added": SNAPSHOT_CLASS
    + "  const alias = shape; alias.extra = 1; return shape.read();\n",
    "own_keys_unused_write": SNAPSHOT_CLASS.replace(
        "    read() {", "    change() { this.extra = 1; }\n    read() {"
    )
    + "  return shape.read();\n",
    "own_keys_deleted": SNAPSHOT_CLASS + "  delete shape.x; return shape.read();\n",
    "own_keys_replaced_intrinsic": SNAPSHOT_CLASS
    + "  Object.getOwnPropertyNames = () => []; return shape.read();\n",
    "own_keys_escaped_snapshot": SNAPSHOT_CLASS.replace(
        "      return keys.length", "      foreign(keys);\n      return keys.length"
    )
    + "  return shape.read();\n",
    "own_keys_partial_constructor": SNAPSHOT_CLASS.replace(
        "this.x = 'ready';", "this.read(); this.x = 'ready';"
    )
    + "  return shape.read();\n",
    "own_keys_clear_iterator_replaced": SNAPSHOT_CASES["class_own_keys_clear"][0].replace(
        "  shape.dispose();", "  __ctbrowser_for_of_open = x => x; shape.dispose();"
    ),
    "own_keys_clear_array_replaced": SNAPSHOT_CASES["class_own_keys_clear"][0].replace(
        "  shape.dispose();", "  Array.prototype[Symbol.iterator] = () => null; shape.dispose();"
    ),
}
CLASS_CASES.update(SNAPSHOT_CASES)
CLASS_REFUSALS.update(SNAPSHOT_REFUSALS)
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
M_CLASS = (
    BOOTSTRAP_M
    + """
  class Shape {
    constructor(element) { this.element = element; }
    read() { return this.element.getAttribute('x'); }
  }
  const shape = new Shape(element);
"""
)
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
F_CLASS = (
    strings.BOOTSTRAP_F
    + """class Shape {
    read(key) { return F(key); }
  }
  const shape = new Shape();
  return shape.read('config') === 'config' && element.getAttribute('x') === null;
"""
)
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

INHERITED_CLASS = """class Base {
    constructor(element) { this.element = element; }
    read() { return this.element.getAttribute('x') === null; }
  }
  class Button extends Base { constructor(element) { super(element); } }
  return new Button(element).read();
"""
INHERITED_CASES = {
    "class_inherited_dom": (INHERITED_CLASS, "1000"),
    "class_inherited_dom_parameter": (
        INHERITED_CLASS.replace("read()", "read(key)", 1)
        .replace("getAttribute('x')", "getAttribute(key)")
        .replace(".read();", ".read('x');"),
        "1000",
    ),
    "class_inherited_dom_unused": (
        INHERITED_CLASS.replace(
            "    read()",
            "    unused() { this.element.setAttribute('unused-probe', 'bad'); }\n    read()",
        ).replace(
            "  return new Button(element).read();",
            "  const saved = new Button(element).read();\n"
            "  return saved && element.getAttribute('unused-probe') === null;",
        ),
        "1000",
    ),
    "class_inherited_dom_base_instance": (
        INHERITED_CLASS.replace(
            "  return new Button(element).read();",
            "  const base = new Base(element);\n"
            "  const button = new Button(element);\n"
            "  return base.read() && button.read();",
        ),
        "1000",
    ),
    "class_inherited_dom_siblings": (
        INHERITED_CLASS.replace(
            "  return new Button(element).read();",
            "  class Other extends Base { constructor(element) { super(element); } }\n"
            "  const first = new Button(element);\n"
            "  const second = new Other(other);\n"
            "  return first.read() && !second.read();",
        ),
        "1000",
    ),
    "class_inherited_dom_chain": (
        INHERITED_CLASS.replace(
            "  return new Button(element).read();",
            "  class Leaf extends Button { constructor(element) { super(element); } }\n"
            "  return new Leaf(element).read();",
        ),
        "1000",
    ),
    "class_inherited_dom_field_order": (
        INHERITED_CLASS.replace(
            "  return new Button(element).read();",
            "  const button = new Button(element);\n"
            "  const saved = button.read();\n"
            "  button.element = other;\n"
            "  return saved && !button.read();",
        ),
        "1000",
    ),
    "class_inherited_dom_snapshot": (
        INHERITED_CLASS.replace(
            "    read()",
            "    keys() { return Object.getOwnPropertyNames(this)[0]; }\n    read()",
        ).replace(
            "  return new Button(element).read();",
            "  const button = new Button(element);\n"
            "  return button.read() && button.keys() === 'element';",
        ),
        "1000",
    ),
    "class_inherited_dom_getter": (
        INHERITED_CLASS.replace("    read()", "    static get KEY() { return 'x'; }\n    read()")
        .replace("getAttribute('x')", "getAttribute(this.constructor.KEY)")
        .replace(
            "super(element); } }",
            "super(element); } static get KEY() { return 'marker'; } }",
        )
        .replace(
            "  return new Button(element).read();",
            "  element.setAttribute('marker', 'done');\n"
            "  const base = new Base(element);\n"
            "  const button = new Button(element);\n"
            "  return base.read() && !button.read();",
        ),
        "1000",
    ),
}
INHERITED_REFUSALS = {
    "inherited_unused_unknown_dom": INHERITED_CLASS.replace(
        "    read()", "    unused() { this.element.unknown(); }\n    read()"
    ),
    "inherited_unused_parameter": INHERITED_CLASS.replace(
        "    read()", "    unused(key) { this.element.getAttribute(key); }\n    read()"
    ),
    "inherited_unused_ambient": INHERITED_CLASS.replace(
        "    read()", "    unused() { ambient(); }\n    read()"
    ),
    "inherited_unused_bad_dispatch": INHERITED_CLASS.replace(
        "    read()", "    unused() { this.element = {}; this.read(); }\n    read()"
    ),
    "inherited_sibling_missing_call": INHERITED_CASES["class_inherited_dom_parameter"][0].replace(
        "  return new Button(element).read('x');",
        "  class Other extends Base { constructor(element) { super(element); } }\n"
        "  const other = new Other(element);\n"
        "  return new Button(element).read('x');",
    ),
    "inherited_shadowed_unknown_dom": INHERITED_CLASS.replace(
        "super(element); } }", "super(element); } read() { return true; } }"
    ).replace("getAttribute('x')", "unknown()"),
    "inherited_replaced_element": INHERITED_CLASS.replace(
        "  return new Button(element).read();",
        "  const button = new Button(element); button.read();\n"
        "  button.element = {}; return button.read();",
    ),
    "inherited_bad_sibling": INHERITED_CASES["class_inherited_dom_siblings"][0].replace(
        "new Other(other)", "new Other({})"
    ),
    "inherited_detached": INHERITED_CLASS.replace(
        "  return new Button(element).read();",
        "  const button = new Button(element); const read = button.read; return read();",
    ),
    "inherited_receiver_escape": INHERITED_CLASS.replace(
        "return this.element.getAttribute('x') === null;", "return this;"
    ),
    "inherited_method_replaced": INHERITED_CLASS.replace(
        "  return new Button(element).read();",
        "  const button = new Button(element); button.read = () => true; return button.read();",
    ),
    "inherited_heritage_replaced": "__ctbrowser_class_heritage = () => {};\n" + INHERITED_CLASS,
    "inherited_unconstructed_leaf": """class Base {
    bad() { this.element.unknown(); }
  }
  class Leaf extends Base {}
  class Dummy {}
  const dummy = new Dummy();
  return dummy instanceof Leaf;
""",
    "inherited_unconstructed_own_method": """class Base {}
  class Leaf extends Base { bad() { this.element.unknown(); } }
  class Dummy {}
  const dummy = new Dummy();
  return dummy instanceof Leaf;
""",
}
SHADOWED_CLASS = INHERITED_CLASS.replace(
    "super(element); } }", "super(element); } read() { return true; } }"
)
SHADOWED_HELPER = "const get = target => target.getAttribute('x');\n" + SHADOWED_CLASS.replace(
    "this.element.getAttribute('x')", "get(this.element)"
)
SHADOWED_CASES = {
    # The former refusal source, unchanged.
    "class_shadowed_dom": (SHADOWED_CLASS, "1111"),
    "class_shadowed_key_collision": (
        SHADOWED_CLASS.replace(
            "this.element = element;",
            "this.element = element; this.__ctnative_shadow_6 = 'first'; "
            "this.__ctnative_shadow_6_ = 'second';",
        ).replace(
            "  return new Button(element).read();",
            "  const button = new Button(element);\n"
            "  return button.read() && button.__ctnative_shadow_6 === 'first' "
            "&& button.__ctnative_shadow_6_ === 'second';",
        ),
        "1111",
    ),
    "class_shadowed_write": (
        SHADOWED_CLASS.replace(
            "return this.element.getAttribute('x') === null;",
            "this.element.setAttribute('shadow-probe', 'bad'); return false;",
        ).replace(
            "  return new Button(element).read();",
            "  const button = new Button(element);\n"
            "  return button.read() && element.getAttribute('shadow-probe') === null;",
        ),
        "1111",
    ),
    "class_shadowed_field_effect": (
        SHADOWED_CLASS.replace(
            "  return new Button(element).read();",
            "  const button = new Button(element);\n"
            "  button.unread = element.setAttribute('effect', 'kept');\n"
            "  return button.read() && element.getAttribute('effect') === 'kept';",
        ),
        "1111",
    ),
    "class_shadowed_dispatch": (
        SHADOWED_CLASS.replace(
            "return this.element.getAttribute('x') === null;", "return this.read();"
        ).replace(
            "read() { return true; }", "read() { return this.element.getAttribute('x') === null; }"
        ),
        "1000",
    ),
    "class_shadowed_chain": (
        SHADOWED_CLASS.replace(
            "  return new Button(element).read();",
            "  class Leaf extends Button {\n"
            "    constructor(element) { super(element); }\n"
            "    read() { return this.element.getAttribute('x') === null; }\n"
            "  }\n"
            "  const middle = new Button(element);\n"
            "  const leaf = new Leaf(element);\n"
            "  return middle.read() && leaf.read();",
        ),
        "1000",
    ),
    "class_shadowed_base_instance": (
        SHADOWED_CLASS.replace(
            "  return new Button(element).read();",
            "  const base = new Base(element);\n"
            "  const button = new Button(element);\n"
            "  return base.read() && button.read();",
        ),
        "1000",
    ),
    "class_shadowed_instances": (
        SHADOWED_CLASS.replace(
            "  return new Button(element).read();",
            "  const first = new Button(element);\n"
            "  const second = new Button(other);\n"
            "  return first.read() && second.read();",
        ),
        "1111",
    ),
    "class_shadowed_snapshot": (
        SHADOWED_CLASS.replace(
            "return this.element.getAttribute('x') === null;",
            "const keys = Object.getOwnPropertyNames(this);\n"
            "      return keys.length === 1 && keys[0] === 'element';",
        ),
        "1111",
    ),
    "class_shadowed_getter": (
        SHADOWED_CLASS.replace("    read()", "    static get KEY() { return 'x'; }\n    read()")
        .replace("getAttribute('x')", "getAttribute(this.constructor.KEY)")
        .replace(
            "read() { return true; }",
            "read() { return true; } static get KEY() { return 'marker'; }",
        ),
        "1111",
    ),
}
SHADOWED_REFUSALS = {
    # The unconstructed base's original constructor retains its separate
    # call proof even when a copied leaf body could consume the field.
    "shadowed_constructor_effect": SHADOWED_CLASS.replace(
        "this.element = element;",
        "this.element = element; this.unread = element.setAttribute('effect', 'kept');",
    ).replace(
        "  return new Button(element).read();",
        "  const button = new Button(element);\n"
        "  return button.read() && element.getAttribute('effect') === 'kept';",
    ),
    "shadowed_field_effect": SHADOWED_CASES["class_shadowed_field_effect"][0].replace(
        "element.setAttribute('effect', 'kept')", "element.unknown()"
    ),
    "shadowed_ambient": SHADOWED_CLASS.replace("getAttribute('x')", "getAttribute(ambient())"),
    "shadowed_bad_argument": SHADOWED_CLASS.replace("getAttribute('x')", "getAttribute({})"),
    "shadowed_bad_dispatch": SHADOWED_CASES["class_shadowed_dispatch"][0].replace(
        "return this.read();", "this.element = {}; return this.read();"
    ),
    "shadowed_default_parameter": SHADOWED_CLASS.replace("    read()", "    read(key = 'x')"),
    "shadowed_bad_instance": SHADOWED_CASES["class_shadowed_instances"][0].replace(
        "new Button(other)", "new Button({})"
    ),
    "shadowed_bad_chain": SHADOWED_CASES["class_shadowed_chain"][0].replace(
        "getAttribute('x')", "unknown()", 1
    ),
    "shadowed_receiver_escape": SHADOWED_CLASS.replace(
        "return this.element.getAttribute('x') === null;", "return this;"
    ),
    "shadowed_detached": SHADOWED_CLASS.replace(
        "return this.element.getAttribute('x') === null;",
        "const read = this.element.getAttribute; return read('x');",
    ),
    "shadowed_helper_effect": SHADOWED_HELPER.replace(
        "target.getAttribute('x')", "target.unknown()"
    ),
}
# These dependencies lose their last call only when their independently
# probed method is omitted; unrelated unused helper bodies still require proof.
SHADOW_HELPER_CASES = {
    "class_shadowed_helper": (SHADOWED_HELPER, "1111"),
    "class_shadowed_helper_chain": (
        SHADOWED_HELPER.replace(
            "const get = target => target.getAttribute('x');",
            "const inner = target => target.getAttribute('x');\n"
            "const get = target => inner(target);",
        ),
        "1111",
    ),
    "class_shadowed_helper_shared": (
        SHADOWED_HELPER.replace(
            "read() { return true; }", "read() { return get(this.element) === null; }"
        ),
        "1000",
    ),
    "class_shadowed_helper_write": (
        SHADOWED_HELPER.replace(
            "target => target.getAttribute('x')",
            "target => { target.setAttribute('helper-probe', 'bad'); return null; }",
        ).replace(
            "  return new Button(element).read();",
            "  const button = new Button(element);\n"
            "  return button.read() && element.getAttribute('helper-probe') === null;",
        ),
        "1111",
    ),
    "class_shadowed_helper_instances": (
        SHADOWED_HELPER.replace(
            "  return new Button(element).read();",
            "  const first = new Button(element);\n"
            "  const second = new Button(other);\n"
            "  return first.read() && second.read();",
        ),
        "1111",
    ),
    "class_shadowed_helper_entry": (
        SHADOWED_HELPER.replace(
            "  return new Button(element).read();",
            "  const button = new Button(element);\n"
            "  return button.read() && get(element) === null;",
        ),
        "1000",
    ),
}
SHADOW_HELPER_REFUSALS = {
    "shadowed_helper_bad_argument": SHADOWED_HELPER.replace("get(this.element)", "get({})"),
    "shadowed_helper_unused_unrelated": "const unrelated = target => target.unknown();\n"
    + SHADOWED_HELPER,
    "shadowed_helper_bad_instance": SHADOW_HELPER_CASES["class_shadowed_helper_instances"][
        0
    ].replace("new Button(other)", "new Button({})"),
    "shadowed_helper_chain_effect": SHADOW_HELPER_CASES["class_shadowed_helper_chain"][0].replace(
        "target.getAttribute('x')", "target.unknown()"
    ),
    "shadowed_helper_shared_bad_receiver": SHADOW_HELPER_CASES["class_shadowed_helper_shared"][
        0
    ].replace(
        "read() { return get(this.element) === null; }", "read() { return get({}) === null; }"
    ),
    "shadowed_helper_entry_bad_receiver": SHADOW_HELPER_CASES["class_shadowed_helper_entry"][
        0
    ].replace("get(element) === null", "get({}) === null"),
    "shadowed_helper_mutable": SHADOWED_HELPER.replace("const get =", "let get =").replace(
        "  return new Button(element).read();",
        "  get = target => target.unknown();\n  return new Button(element).read();",
    ),
    "shadowed_helper_escape": SHADOWED_HELPER.replace(
        "return get(this.element) === null;", "return get;"
    ),
}
UNUSED_FORMAL_CASES = {
    # Exact former refusal; the key has no source observer.
    "class_shadowed_parameter": (SHADOWED_CLASS.replace("    read()", "    read(key)"), "1111"),
    "class_shadowed_parameters": (
        SHADOWED_CLASS.replace("    read()", "    read(first, second)"),
        "1111",
    ),
    "class_shadowed_parameter_write": (
        SHADOWED_CASES["class_shadowed_write"][0].replace("    read()", "    read(unused)"),
        "1111",
    ),
    "class_shadowed_parameter_helper": (
        SHADOWED_HELPER.replace("    read()", "    read(unused)"),
        "1111",
    ),
    "class_unused_parameter": (
        ELEMENT_CLASS.replace(
            "    press()",
            "    unused(ignored) { this.element.getAttribute('x'); }\n    press()",
        ),
        "1000",
    ),
    "class_inherited_unused_parameter": (
        INHERITED_CLASS.replace(
            "    read()",
            "    unused(ignored) { this.element.getAttribute('x'); }\n    read()",
        ),
        "1000",
    ),
    "class_called_unused_parameter_effect": (
        ELEMENT_CLASS.replace("    press()", "    press(ignored)").replace(
            "  return new Button(element).press();",
            "  const button = new Button(element);\n"
            "  const saved = button.press(element.setAttribute('argument', 'kept'));\n"
            "  return saved && element.getAttribute('argument') === 'kept';",
        ),
        "1000",
    ),
}
UNUSED_FORMAL_REFUSALS = {
    "shadowed_parameter_observed": SHADOWED_CLASS.replace("    read()", "    read(key)").replace(
        "getAttribute('x')", "getAttribute(key)"
    ),
    "shadowed_second_parameter_observed": SHADOWED_CLASS.replace(
        "    read()", "    read(ignored, key)"
    ).replace("getAttribute('x')", "getAttribute(key)"),
    "shadowed_parameter_branch": SHADOWED_CLASS.replace("    read()", "    read(key)").replace(
        "return this.element.getAttribute('x') === null;",
        "if (key) { this.element.setAttribute('x', 'changed'); } return true;",
    ),
    "shadowed_parameter_default_effect": SHADOWED_CLASS.replace(
        "    read()", "    read(ignored = ambient())"
    ),
    "shadowed_parameter_arguments": SHADOWED_CLASS.replace(
        "    read()", "    read(ignored)"
    ).replace("getAttribute('x')", "getAttribute(arguments[0])"),
    "shadowed_parameter_rest": SHADOWED_CLASS.replace(
        "    read()", "    read(ignored, ...rest)"
    ).replace("getAttribute('x')", "getAttribute(rest[0])"),
    "shadowed_parameter_unknown_dom": UNUSED_FORMAL_CASES["class_shadowed_parameter"][0].replace(
        "getAttribute('x')", "unknown()"
    ),
    "shadowed_parameter_bad_instance": UNUSED_FORMAL_CASES["class_shadowed_parameter"][0].replace(
        "new Button(element)", "new Button({})"
    ),
    "called_unused_parameter_bad_effect": UNUSED_FORMAL_CASES[
        "class_called_unused_parameter_effect"
    ][0].replace("element.setAttribute('argument', 'kept')", "element.unknown()"),
}
SHADOWED_CASES.update(
    {
        name: case
        for name, case in UNUSED_FORMAL_CASES.items()
        if name not in ("class_unused_parameter", "class_called_unused_parameter_effect")
    }
)
CLASS_CASES.update(UNUSED_FORMAL_CASES)
SHADOWED_REFUSALS.update(
    {
        name: body
        for name, body in UNUSED_FORMAL_REFUSALS.items()
        if name != "called_unused_parameter_bad_effect"
    }
)
CLASS_REFUSALS.update(UNUSED_FORMAL_REFUSALS)
SHADOWED_CASES.update(SHADOW_HELPER_CASES)
SHADOWED_REFUSALS.update(SHADOW_HELPER_REFUSALS)
INHERITED_CASES.update(SHADOWED_CASES)
INHERITED_REFUSALS.update(SHADOWED_REFUSALS)
TWO_ELEMENT_CLASSES.update(
    {
        "class_inherited_dom_siblings",
        "class_inherited_dom_field_order",
        "inherited_bad_sibling",
        "class_shadowed_instances",
        "class_shadowed_helper_instances",
        "shadowed_helper_bad_instance",
        "shadowed_bad_instance",
    }
)
CLASS_CASES.update(INHERITED_CASES)
CLASS_REFUSALS.update(INHERITED_REFUSALS)
