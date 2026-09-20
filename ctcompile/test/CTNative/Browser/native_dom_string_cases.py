#!/usr/bin/env python3
"""Gate copied optional String DOM reads against source and the public DOM/Core API."""

import argparse
import json
from pathlib import Path
import re
import shutil
from urllib.parse import quote_from_bytes

from CTNative.Browser import native_dom as dom
from CTNative.Browser import native_dom_numbers as numbers
from CTNative.Browser import native_dom_nullable_uri as nullable_uri
from CTNative.Browser import native_dom_uri as uri
from CTNative.harness import find_compilers, run
from Target.Cpp.harness import FLAGS

READ = """function readAttribute(element) {
  return element.getAttribute('DATA-State');
}
"""
SAVED = """function savedAttribute(element, other) {
  const saved = element.getAttribute('DATA-State');
  element.getAttribute('data-missing');
  element.setAttribute('DATA-State', 'after');
  const current = other.getAttribute('DATA-State');
  element.removeAttribute('DATA-State');
  return saved;
}
"""
NAMES = r"""function readNames(element) {
  element.getAttribute('bad name');
  element.getAttribute('');
  return element.getAttribute('a\0b');
}
"""
WIDE = r"""function readWideName(element) {
  return element.getAttribute('\ud800');
}
"""
# Annex B gives the block function a local binding; its self-copy is not a second export.
HOST_LOCAL_FUNCTION_COPY = "const key = 'x'; function host_refusal(element) { return element.getAttribute(key); } host_refusal = host_refusal;"

# Keep the original refusal sources byte-for-byte when admitting their graphs.
CAPTURE_RETURNS = {
    "helper_branch_call_in_arm": "function invalid(element) { function read(target) { function attribute(node) { return node.getAttribute('x'); } if (target.hasAttribute('x')) return attribute(target); return null; } return read(element); }\n",
    "host_local_function_copy": "{\n" + HOST_LOCAL_FUNCTION_COPY + "\n}\n",
    "branch_optional_return": "function invalid(element) { const saved = element.getAttribute('x'); let value; if (saved === null) { value = null; } else { value = saved; } return value; }\n",
    "helper_optional_return": "function invalid(element) { function read(target) { const saved = target.getAttribute('x'); if (saved === null) return null; return saved; } return read(element); }\n",
    "capture_callable": "function invalid(element) { function read(target) { return target.getAttribute('x'); } function invoke() { return read(element); } return invoke(); }\n",
    "capture_holder": "function invalid(element) { const helpers = {read(target) { return target.getAttribute('x'); }}; function invoke() { return helpers.read(element); } return invoke(); }\n",
    "capture_forwarded": "function invalid(element) { function invoke() { function read() { return element.getAttribute('x'); } return read(); } return invoke(); }\n",
    "helper_regex": "function invalid(element) { const F = t => t.replace(/[A-Z]/g, t => `-${t.toLowerCase()}`); return element.getAttribute('data-bs-' + F('config')); }\n",
    "nested_control": "function invalid(element) { if (element.hasAttribute('x')) return element.getAttribute('x'); return ''; }\n",
}
CAPTURE_SOURCE = "".join(
    (
        source + "const host_local_function_copy = host_refusal;\n"
        if name == "host_local_function_copy"
        else f"const {name} = (() => {{\n{source}return invalid;\n}})();\n"
    )
    for name, source in CAPTURE_RETURNS.items()
)
SOURCES = (
    ("read", READ, 1),
    ("saved", SAVED, 2),
    ("names", NAMES, 1),
    ("wide", WIDE, 1),
    *((name, source, 1) for name, source in CAPTURE_RETURNS.items()),
)
BOOLEAN_CASES = {
    "set_value": ("element.setAttribute('y', element.getAttribute('x')); return true;", "1111"),
    "boolean": ("return !element.getAttribute('x');", "1100"),
    "truthy": ("return !!element.getAttribute('x');", "0011"),
    "nullable_equality": ("return element.getAttribute('x') === null;", "1000"),
    "nullable_inequality": ("return element.getAttribute('x') !== null;", "0111"),
    "reverse_null_equality": ("return null === element.getAttribute('x');", "1000"),
    "reverse_null_inequality": ("return null !== element.getAttribute('x');", "0111"),
    "empty_equality": ("return '' === element.getAttribute('x');", "0100"),
    "empty_inequality": ("return element.getAttribute('x') !== '';", "1011"),
    "nul_equality": (r"return element.getAttribute('x') === 'a\0b';", "0010"),
    "nul_inequality": (r"return 'a\0b' !== element.getAttribute('x');", "1101"),
    "optional_equality": (
        "return element.getAttribute('x') === element.getAttribute('missing');",
        "1000",
    ),
    "optional_inequality": (
        "return element.getAttribute('missing') !== element.getAttribute('x');",
        "0111",
    ),
    "saved_equality": (
        r"""const saved = element.getAttribute('x');
  element.setAttribute('x', 'a\0b');
  return saved === element.getAttribute('x');""",
        "0010",
    ),
    "comparison_force": (
        """const wanted = element.getAttribute('x') !== null;
  return element.toggleAttribute('data-force', wanted);""",
        "0111",
    ),
    "null_constants": ("element.getAttribute('x'); return null === null;", "1111"),
    "string_null": ("element.getAttribute('x'); return 'null' === null;", "0000"),
    "unused_boolean": ("!element.getAttribute('x'); return true;", "1111"),
    "computed": (
        r"""const name = 'data-' + 'copy';
  element.setAttribute(name, 'wh' + 'ole');
  const saved = element.getAttribute(name);
  const key = 'a' + '\0b';
  const whole = element.getAttribute(key);
  element.classList.toggle('test-' + 'token', saved === whole);
  element.removeAttribute(name);
  return !element.hasAttribute(name);""",
        "1111",
    ),
}
BOOLEAN_CASES.update(
    {
        "branch_loop": (
            "while (element.hasAttribute('x')) { element.removeAttribute('x'); } "
            "return element.getAttribute('x') === null;",
            "1111",
        ),
        "branch_effects": (
            """if (element.getAttribute('x')) {
      element.setAttribute('marker', 'yes');
    } else { element.removeAttribute('marker'); }
    return element.hasAttribute('marker');""",
            "0011",
        ),
        "branch_boolean": (
            """let answer; if (element.getAttribute('x') !== null) {
      answer = true;
    } else { answer = false; } return answer;""",
            "0111",
        ),
        "branch_string": (
            """let answer; if (element.hasAttribute('x')) {
      answer = 'yes';
    } else { answer = 'no'; }
    element.setAttribute('marker', answer);
    return element.getAttribute('marker') === 'yes';""",
            "0111",
        ),
        "branch_optional_string": (
            """let answer; if (element.hasAttribute('x')) {
      answer = element.getAttribute('x');
    } else { answer = 'fallback'; } return answer === 'fallback';""",
            "1000",
        ),
        "branch_string_optional": (
            """let answer; if (element.hasAttribute('x')) {
      answer = 'present';
    } else { answer = element.getAttribute('x'); } return answer === null;""",
            "1000",
        ),
        "branch_saved": (
            """let answer; if (element.hasAttribute('x')) {
      answer = element.getAttribute('x');
    } else { answer = null; }
    element.setAttribute('x', 'after'); return !answer;""",
            "1100",
        ),
        "branch_null_string": (
            """let answer; if (element.hasAttribute('x')) {
      answer = 'present';
    } else { answer = null; } return !!answer;""",
            "0111",
        ),
        "branch_nested": (
            """let answer; if (element.hasAttribute('x')) {
      if (element.getAttribute('x')) { answer = 'nonempty'; }
      else { answer = 'empty'; }
    } else { answer = 'absent'; }
    element.setAttribute('marker', answer);
    return element.getAttribute('marker') === 'empty';""",
            "0100",
        ),
    }
)
# Preserve the original three-return source while proving LLVM completion dispatch.
HELPER_COMPLETION_SOURCE = """function observe(target) {
    function read(name) {
      if (target.getAttribute(name) === null) return false;
      if (target.getAttribute(name) === '') return true;
      return false;
    }
    return read('x');
  }
  return observe(element);"""
HELPER_CASES = {
    "helper_branch_completion_dispatch": (HELPER_COMPLETION_SOURCE, "0100"),
    "helper_completion_effects": (
        r"""function change(target) {
    const saved = target.getAttribute('x');
    if (saved === null) { target.setAttribute('marker', 'missing'); return true; }
    if (saved === '') { target.setAttribute('marker', 'empty'); return false; }
    if (saved === 'a\0b') { target.setAttribute('marker', 'nul'); return true; }
    target.setAttribute('marker', 'wide'); return false;
  }
  return change(element);""",
        "1010",
    ),
    "helper_completion_capture_snapshot": (
        """const saved = element.getAttribute('x');
  function read(target) {
    if (saved === null) { target.setAttribute('marker', 'missing'); return null; }
    if (saved === '') { target.setAttribute('marker', 'empty'); return ''; }
    target.setAttribute('marker', 'present'); return saved;
  }
  element.setAttribute('x', 'after');
  return !read(element);""",
        "1100",
    ),
    "helper_branch": (
        "function read(target) { if (target.hasAttribute('x')) return true; return false; } return read(element);",
        "0111",
    ),
    "helper_branch_loop": (
        "function read(target) { while (target.hasAttribute('x')) { target.removeAttribute('x'); } "
        "return target.getAttribute('x'); } return read(element) === null;",
        "1111",
    ),
    "helper_branch_effects": (
        """function change(target) {
    if (target.getAttribute('x')) { target.setAttribute('marker', 'yes'); }
    else { target.removeAttribute('marker'); }
    return target.hasAttribute('marker');
  }
  return change(element);""",
        "0011",
    ),
    "helper_branch_string_null": (
        """function read(target) {
    if (target.getAttribute('x') === null) return null;
    return 'present';
  }
  return read(element) === null;""",
        "1000",
    ),
    "helper_branch_saved": (
        """function read(target) {
    const saved = target.getAttribute('x');
    if (saved === null) return null;
    return saved;
  }
  const before = read(element); element.setAttribute('x', 'after');
  return !before;""",
        "1100",
    ),
    "helper_branch_early_effects": (
        """function change(target) {
    if (target.getAttribute('x') === null) {
      target.setAttribute('marker', 'missing'); return true;
    }
    target.setAttribute('marker', 'present'); return false;
  }
  return change(element);""",
        "1000",
    ),
    "helper_branch_nested": (
        """function observe(target) {
    function read(name) {
      let answer;
      if (target.hasAttribute(name)) {
        if (target.getAttribute(name)) { answer = 'nonempty'; }
        else { answer = 'empty'; }
      } else { answer = 'absent'; }
      return answer === 'empty';
    }
    return read('x');
  }
  return observe(element);""",
        "0100",
    ),
    "helper_branch_capture": (
        """const saved = element.getAttribute('x');
  function read() { if (saved === null) return true; else return false; }
  element.setAttribute('x', 'after'); return read();""",
        "1000",
    ),
    "helper_branch_capture_arms": (
        """const saved = element.getAttribute('x');
  const missing = element.getAttribute('missing');
  function read(target) {
    if (target.hasAttribute('x')) return saved;
    return missing;
  }
  const before = read(element); element.setAttribute('x', 'after');
  return !before;""",
        "1100",
    ),
    "helper_branch_holder": (
        """const helpers = {read(target) {
    let answer; if (target.getAttribute('x') === null) { answer = 'missing'; }
    else { answer = 'present'; } return answer;
  }};
  function invoke() { return helpers.read(element) === 'missing'; }
  return invoke();""",
        "1000",
    ),
    "helper_branch_repeated": (
        """function read(target, name) {
    if (target.getAttribute(name) === null) return null;
    return target.getAttribute(name);
  }
  return read(element, 'x') === read(element, 'missing');""",
        "1000",
    ),
    "helper_capture": (
        "function read() { return element.getAttribute('x'); } return read() === null;",
        "1000",
    ),
    "helper_capture_key": (
        "const key = 'x'; function read(target) { return target.getAttribute(key); } return read(element) === null;",
        "1000",
    ),
    "helper_capture_saved": (
        r"""const saved = element.getAttribute('x');
  function compare(target) { return saved === target.getAttribute('x'); }
  element.setAttribute('x', 'a\0b'); return compare(element);""",
        "0010",
    ),
    "helper_capture_repeated": (
        """const key = 'x';
  function read() { return element.getAttribute(key); }
  const before = read(); element.setAttribute('x', 'after');
  return before === read();""",
        "0000",
    ),
    "helper_capture_method": (
        """const key = 'x';
  const helpers = {read() { return element.getAttribute(key); }};
  return helpers.read() === null;""",
        "1000",
    ),
    "helper_capture_arrow": (
        "const key = 'x'; const read = () => element.getAttribute(key); return read() === null;",
        "1000",
    ),
    "helper_capture_local_nested": (
        """function observe(target, name) {
    function read() { return target.getAttribute(name); }
    return read();
  }
  return observe(element, 'x') === null;""",
        "1000",
    ),
    "helper_capture_callable": (
        """function read(target) { return target.getAttribute('x'); }
  function invoke() { return read(element); }
  return invoke() === null;""",
        "1000",
    ),
    "helper_capture_callable_alias": (
        """const read = target => target.getAttribute('x');
  const alias = read;
  function invoke() { return alias(element); }
  return invoke() === null;""",
        "1000",
    ),
    "helper_capture_callable_callers": (
        """function read(target, key) { return target.getAttribute(key); }
  function first(target) { return read(target, 'x'); }
  function second(target) { return read(target, 'missing'); }
  return first(element) === second(element);""",
        "1000",
    ),
    "helper_capture_callable_order": (
        r"""const saved = element.getAttribute('x');
  function compare(target) { return saved === target.getAttribute('x'); }
  function invoke() { return compare(element); }
  element.setAttribute('x', 'a\0b'); return invoke();""",
        "0010",
    ),
    "helper_capture_holder": (
        """const helpers = {read(target) { return target.getAttribute('x'); }};
  function invoke() { return helpers.read(element); }
  return invoke() === null;""",
        "1000",
    ),
    "helper_capture_holder_alias": (
        """const helpers = {read(target) { return target.getAttribute('x'); }};
  const alias = helpers;
  function invoke() { return alias.read(element); }
  return invoke() === null;""",
        "1000",
    ),
    "helper_capture_holder_extracted": (
        """const helpers = {read(target) { return target.getAttribute('x'); }};
  function invoke() { const read = helpers.read; return read(element); }
  return invoke() === null;""",
        "1000",
    ),
    "helper_capture_holder_order": (
        r"""const helpers = {change(target, saved) {
    target.setAttribute('x', 'a\0b');
    return saved === target.getAttribute('x');
  }};
  function invoke(saved) { return helpers.change(element, saved); }
  return invoke(element.getAttribute('x'));""",
        "0010",
    ),
    "helper_capture_nonleaf": (
        """const key = 'x';
  function read(target) {
    function identity(name) { return name; }
    return target.getAttribute(identity(key));
  }
  return read(element) === null;""",
        "1000",
    ),
    "helper_capture_forwarded_mixed": (
        """const key = 'x';
  function invoke(target) {
    function read() { return target.getAttribute(key); }
    return read();
  }
  return invoke(element) === null;""",
        "1000",
    ),
    "helper_capture_forwarded_chain": (
        """const key = 'x';
  function outer() {
    function invoke() {
      function read() { return element.getAttribute(key); }
      return read();
    }
    return invoke();
  }
  return outer() === null;""",
        "1000",
    ),
    "helper_capture_forwarded_alias": (
        """const key = 'x';
  function invoke(target) {
    const name = key;
    function read() { return target.getAttribute(name); }
    return read();
  }
  return invoke(element) === null;""",
        "1000",
    ),
    "helper_capture_forwarded_callers": (
        """const prefix = '';
  function read(target, key) {
    function name() { return prefix + key; }
    return target.getAttribute(name());
  }
  return read(element, 'x') === read(element, 'missing');""",
        "1000",
    ),
    "helper_capture_forwarded_callable": (
        """function read(target) { return target.getAttribute('x'); }
  function invoke() {
    function nested() { return read(element); }
    return nested();
  }
  return invoke() === null;""",
        "1000",
    ),
    "helper_capture_forwarded_holder": (
        """const helpers = {read(target) { return target.getAttribute('x'); }};
  function invoke() {
    function nested() { return helpers.read(element); }
    return nested();
  }
  return invoke() === null;""",
        "1000",
    ),
    "helper_capture_forwarded_order": (
        r"""const saved = element.getAttribute('x');
  function invoke() {
    function compare() { return saved === element.getAttribute('x'); }
    return compare();
  }
  element.setAttribute('x', 'a\0b'); return invoke();""",
        "0010",
    ),
    "helper_read": (
        """function read(target, key) { return target.getAttribute(key); }
  return read(element, 'x') === null;""",
        "1000",
    ),
    "helper_nested": (
        """function read(target, key) {
    function attributeName(name) { return 'data-' + name; }
    return target.getAttribute(attributeName(key));
  }
  element.setAttribute('data-config', 'value');
  return read(element, 'config') === 'value';""",
        "1111",
    ),
    "helper_order": (
        r"""function change(target, saved) {
    target.setAttribute('x', 'a\0b');
    return saved === target.getAttribute('x');
  }
  return change(element, element.getAttribute('x'));""",
        "0010",
    ),
    "helper_repeated": (
        """function read(target, key) { return target.getAttribute(key); }
  return read(element, 'x') === read(element, 'missing');""",
        "1000",
    ),
    "helper_forward": (
        """function identity(target) { return target; }
  return identity(element).getAttribute('x') === null;""",
        "1000",
    ),
    "helper_unused_result": (
        """function read(target) { return target.getAttribute('x'); }
  read(element); return true;""",
        "1111",
    ),
    "helper_object_method": (
        """const helpers = {read(target, key) { return target.getAttribute(key); }};
  return helpers.read(element, 'x') === null;""",
        "1000",
    ),
    "helper_object_function": (
        """const helpers = {read: function(target, key) { return target.getAttribute(key); }};
  return helpers.read(element, 'x') === null;""",
        "1000",
    ),
    "helper_object_shorthand": (
        """function read(target, key) { return target.getAttribute(key); }
  const helpers = {read};
  return helpers.read(element, 'x') === null;""",
        "1000",
    ),
    "helper_object_extracted": (
        """const helpers = {read(target, key) { return target.getAttribute(key); }};
  const read = helpers.read;
  return read(element, 'x') === null;""",
        "1000",
    ),
    "helper_object_literal_alias": (
        """const helpers = {'read-name': function(target, key) { return target.getAttribute(key); }};
  const alias = helpers;
  return alias['read-name'](element, 'x') === null;""",
        "1000",
    ),
    "helper_object_repeated": (
        """const helpers = {read(target, key) { return target.getAttribute(key); }};
  return helpers.read(element, 'x') === helpers.read(element, 'missing');""",
        "1000",
    ),
    "helper_object_multiple": (
        """const helpers = {
    read(target, key) { return target.getAttribute(key); },
    name(value) { return 'data-' + value; }
  };
  element.setAttribute('data-config', 'value');
  return helpers.read(element, helpers.name('config')) === 'value';""",
        "1111",
    ),
    "helper_object_nested": (
        """function read(target, key) {
    const helpers = {name(value) { return 'data-' + value; }};
    return target.getAttribute(helpers.name(key));
  }
  element.setAttribute('data-config', 'value');
  return read(element, 'config') === 'value';""",
        "1111",
    ),
    "helper_object_order": (
        r"""const helpers = {change(target, saved) {
    target.setAttribute('x', 'a\0b');
    return saved === target.getAttribute('x');
  }};
  return helpers.change(element, element.getAttribute('x'));""",
        "0010",
    ),
    "helper_object_own_prototype_name": (
        """const helpers = {toString(target) { return target.getAttribute('x'); }};
  return helpers.toString(element) === null;""",
        "1000",
    ),
}
# Preserve Bootstrap's complete helper, including its replacement callback.
BOOTSTRAP_F = """    function F(t) {
        return t.replace(/[A-Z]/g, t => `-${t.toLowerCase()}`)
    }
"""
# These original H methods form the String-only slice before live M normalization.
BOOTSTRAP_H_WRITES = """        setDataAttribute(t, e, i) {
            t.setAttribute(`data-bs-${F(e)}`, i)
        },
        removeDataAttribute(t, e) {
            t.removeAttribute(`data-bs-${F(e)}`)
        },
"""
BOOTSTRAP_H_ACTION = """  H.setDataAttribute(element, 'config', 'first');
  H.setDataAttribute(element, 'toggle', 'second');
  const saved = element.getAttribute('data-bs-config');
  H.removeDataAttribute(element, 'config');
  return saved !== element.getAttribute('data-bs-toggle');"""
# Keep the newly admitted refusal source unchanged, both directly and in the oracle wrapper.
REGEXP_MIXED_SOURCE = (
    "function invalid(element) { "
    + BOOTSTRAP_F
    + " element.setAttribute('data-bs-' + F('config'), 'value'); return element.getAttribute('data-bs-' + F('toggle')); }\n"
)
REGEXP_CAPTURED_SOURCE = (
    "function invalid(element) { "
    + BOOTSTRAP_F
    + " function read(target, name) { return target.getAttribute('data-bs-' + F(name)); } return read(element, 'config'); }\n"
)
REGEXP_CASES = {
    "helper_regex_direct": (
        """const key = 'data-bs-' + 'config'.replace(/[A-Z]/g, t => `-${t.toLowerCase()}`);
  element.setAttribute(key, 'value');
  return element.getAttribute(key) === 'value';""",
        "1111",
    ),
    "helper_regex_original": (
        BOOTSTRAP_F + """
  element.setAttribute('data-bs-' + F('config'), 'value');
  return element.getAttribute('data-bs-' + F('config')) === 'value';""",
        "1111",
    ),
    "regex_mixed_constants": (
        REGEXP_MIXED_SOURCE + "return invalid(element) === null;",
        "1111",
    ),
    "helper_regex_distinct_names": (
        BOOTSTRAP_F
        + """
  element.setAttribute('data-bs-' + F('config'), 'first');
  element.setAttribute('data-bs-' + F('toggle'), 'second');
  return element.getAttribute('data-bs-' + F('config')) !== element.getAttribute('data-bs-' + F('toggle'));""",
        "1111",
    ),
    "helper_regex_forwarded_names": (
        "function read(target, name) {\n" + BOOTSTRAP_F + """
    return target.getAttribute('data-bs-' + F(name));
  }
  element.setAttribute('data-bs-config', 'first');
  element.setAttribute('data-bs-toggle', 'second');
  return read(element, 'config') !== read(element, 'toggle');""",
        "1111",
    ),
    "regex_captured_callable": (
        REGEXP_CAPTURED_SOURCE + "return invalid(element) === null;",
        "1111",
    ),
    "helper_regex_captured_names": (
        BOOTSTRAP_F + """
  function read(target, name) { return target.getAttribute('data-bs-' + F(name)); }
  element.setAttribute('data-bs-config', 'first');
  element.setAttribute('data-bs-toggle', 'second');
  return read(element, 'config') !== read(element, 'toggle');""",
        "1111",
    ),
    "helper_regex_captured_consumers": (
        BOOTSTRAP_F + """
  function first(target) { return target.getAttribute('data-bs-' + F('config')); }
  function second(target) { return target.getAttribute('data-bs-' + F('toggle')); }
  element.setAttribute('data-bs-' + F('direct'), 'third');
  element.setAttribute('data-bs-config', 'first');
  element.setAttribute('data-bs-toggle', 'second');
  return first(element) !== second(element);""",
        "1111",
    ),
    "helper_regex_captured_chain": (
        BOOTSTRAP_F + """
  function key(name) { return F(name); }
  function read(target, name) { return target.getAttribute('data-bs-' + key(name)); }
  element.setAttribute('data-bs-config', 'first');
  element.setAttribute('data-bs-toggle', 'second');
  return read(element, 'config') !== read(element, 'toggle');""",
        "1111",
    ),
    "helper_regex_original_h": (
        BOOTSTRAP_F + "const H = {\n" + BOOTSTRAP_H_WRITES + "};\n" + BOOTSTRAP_H_ACTION,
        "1111",
    ),
    "helper_regex_range": (
        """const key = 'data-bs-' + 'config'.replace(/[0-9]/g, t => t);
  element.setAttribute(key, 'value');
  return element.getAttribute(key) === 'value';""",
        "1111",
    ),
    "helper_regex_no_flags": (
        """const key = 'data-bs-' + 'config'.replace(/[A-Z]/, t => t);
  element.setAttribute(key, 'value');
  return element.getAttribute(key) === 'value';""",
        "1111",
    ),
    "helper_regex_bytes": (
        BOOTSTRAP_F + r"""
  element.setAttribute('data-bs-config', F('a\0\u00e9'));
  return element.getAttribute('data-bs-config') === F('a\0\u00e9');""",
        "1111",
    ),
}
HELPER_CASES.update(REGEXP_CASES)
BOOLEAN_CASES.update(HELPER_CASES)
