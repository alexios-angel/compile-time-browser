"""class dom iteration cases: continued from class_dom_cases."""

from CTNative.Lowering.Objects.class_dom_cases import *

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
