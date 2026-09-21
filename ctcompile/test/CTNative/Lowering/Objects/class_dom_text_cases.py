"""class dom text cases: continued from class_dom_iteration_cases."""

from CTNative.Lowering.Objects.class_dom_iteration_cases import *

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
for name in ("class_utf16_char_index", "class_utf16_slice_index"):
    UTF16_CASES[name] = (UTF16_REFUSALS.pop(name), "0000")
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
FIELD_CHECKS["class_constructor_chain"] = FIELD_CHECKS["class_constructor_then_call"] = (
    'assert(doc.read().attribute_value(node, state) == "after");'
)
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
