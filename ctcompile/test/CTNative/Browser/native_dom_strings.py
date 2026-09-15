#!/usr/bin/env python3
"""Gate copied optional String DOM reads against source and the public DOM/Core API."""

import argparse
from pathlib import Path
import re
import shutil
from urllib.parse import quote_from_bytes

from CTNative.Browser import native_dom as dom
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
# Keep the original refusal sources byte-for-byte when admitting their graphs.
CAPTURE_RETURNS = {
    "branch_optional_return": "function invalid(element) { const saved = element.getAttribute('x'); let value; if (saved === null) { value = null; } else { value = saved; } return value; }\n",
    "capture_callable": "function invalid(element) { function read(target) { return target.getAttribute('x'); } function invoke() { return read(element); } return invoke(); }\n",
    "capture_holder": "function invalid(element) { const helpers = {read(target) { return target.getAttribute('x'); }}; function invoke() { return helpers.read(element); } return invoke(); }\n",
    "capture_forwarded": "function invalid(element) { function invoke() { function read() { return element.getAttribute('x'); } return read(); } return invoke(); }\n",
    "helper_regex": "function invalid(element) { const F = t => t.replace(/[A-Z]/g, t => `-${t.toLowerCase()}`); return element.getAttribute('data-bs-' + F('config')); }\n",
}
CAPTURE_SOURCE = "".join(
    f"const {name} = (() => {{\n{source}return invalid;\n}})();\n"
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
HELPER_CASES = {
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
# Preserve Bootstrap's complete helper, including the never-invoked callback.
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
BOOLEAN_SOURCES = tuple(
    (name, f"function {name}(element) {{ {body} }}\n", 1)
    for name, (body, _) in BOOLEAN_CASES.items()
)
HOST_CASES = {
    "host_factory_original_h": (
        "var host_factory_original_h = (function() {\n"
        + BOOTSTRAP_F
        + "const H = {\n"
        + BOOTSTRAP_H_WRITES
        + "};\nfunction host_factory_original_h(element) {\n"
        + BOOTSTRAP_H_ACTION
        + "\n}\nreturn {entry: host_factory_original_h};\n})().entry;\n",
        "1111",
    ),
    "host_capture_late_initialization": (
        """let hostLateKey;
function host_capture_late_initialization(element) {
  return element.getAttribute(hostLateKey) === null;
}
hostLateKey = 'x';
""",
        "1000",
    ),
    "host_capture_key": (
        """const hostKey = 'x';
function host_capture_key(element) { return element.getAttribute(hostKey) === null; }
""",
        "1000",
    ),
    "host_capture_name": (
        """const hostPrefix = 'data-'; const hostName = 'config';
function host_capture_name(element) {
  element.setAttribute(hostPrefix + hostName, 'value');
  return element.getAttribute(hostPrefix + hostName) === 'value';
}
""",
        "1111",
    ),
    "host_capture_callable": (
        """const hostRead = target => target.getAttribute('x');
function host_capture_callable(element) { return hostRead(element) === null; }
""",
        "1000",
    ),
    "host_capture_holder": (
        """const hostHolderKey = 'x';
const hostHelpers = {read(target) { return target.getAttribute(hostHolderKey); }};
function host_capture_holder(element) { return hostHelpers.read(element) === null; }
""",
        "1000",
    ),
    "host_capture_forwarded": (
        """const hostForwardedKey = 'x';
function host_capture_forwarded(element) {
  function invoke() {
    function read() { return element.getAttribute(hostForwardedKey); }
    return read();
  }
  return invoke() === null;
}
""",
        "1000",
    ),
    "host_capture_nested_callable": (
        """const hostNestedKey = 'x';
const hostNestedRead = target => {
  function read() { return target.getAttribute(hostNestedKey); }
  return read();
};
function host_capture_nested_callable(element) { return hostNestedRead(element) === null; }
""",
        "1000",
    ),
    "host_capture_callers": (
        """const hostCallerPrefix = '';
const hostCallerRead = (target, key) => {
  function name() { return hostCallerPrefix + key; }
  return target.getAttribute(name());
};
function host_capture_callers(element) {
  return hostCallerRead(element, 'x') === hostCallerRead(element, 'missing');
}
""",
        "1000",
    ),
    "host_capture_order": (
        r"""const hostOrderKey = 'x';
const hostChange = (target, saved) => {
  target.setAttribute(hostOrderKey, 'a\0b');
  return saved === target.getAttribute(hostOrderKey);
};
function host_capture_order(element) {
  return hostChange(element, element.getAttribute(hostOrderKey));
}
""",
        "0010",
    ),
    "host_factory_entry": (
        """var host_factory_entry = (function() {
  function host_factory_entry(element) { return element.getAttribute('x') === null; }
  return host_factory_entry;
})();
""",
        "1000",
    ),
    "host_factory_key": (
        """var host_factory_key = (function(key) {
  function host_factory_key(element) { return element.getAttribute(key) === null; }
  return host_factory_key;
})('x');
""",
        "1000",
    ),
    "host_factory_late_initialization": (
        """var host_factory_late_initialization = (() => {
  let key;
  function host_factory_late_initialization(element) {
    return element.getAttribute(key) === null;
  }
  key = 'x';
  return host_factory_late_initialization;
})();
""",
        "1000",
    ),
    "host_factory_callable": (
        """var host_factory_callable = (function() {
  const read = target => target.getAttribute('x');
  function host_factory_callable(element) { return read(element) === null; }
  return host_factory_callable;
})();
""",
        "1000",
    ),
    "host_factory_holder": (
        """var host_factory_holder = (function() {
  const key = 'x';
  const helpers = {read(target) { return target.getAttribute(key); }};
  function host_factory_holder(element) { return helpers.read(element) === null; }
  return host_factory_holder;
})();
""",
        "1000",
    ),
    "host_factory_order": (
        r"""var host_factory_order = (function() {
  const key = 'x';
  const change = (target, saved) => {
    target.setAttribute(key, 'a\0b');
    return saved === target.getAttribute(key);
  };
  function host_factory_order(element) {
    return change(element, element.getAttribute('x'));
  }
  return host_factory_order;
})();
""",
        "0010",
    ),
    "host_factory_table_entry": (
        """var host_factory_table_entry = (function() {
  function host_factory_table_entry(element) { return element.getAttribute('x') === null; }
  return {entry: host_factory_table_entry};
})().entry;
""",
        "1000",
    ),
    "host_factory_table_key": (
        """var host_factory_table_key = (function(key) {
  function host_factory_table_key(element) { return element.getAttribute(key) === null; }
  return {'read-attribute': host_factory_table_key};
})('x')['read-attribute'];
""",
        "1000",
    ),
    "host_factory_table_late_initialization": (
        """var host_factory_table_late_initialization = (() => {
  let key;
  function host_factory_table_late_initialization(element) {
    return element.getAttribute(key) === null;
  }
  const table = {entry: host_factory_table_late_initialization};
  key = 'x';
  return table;
})().entry;
""",
        "1000",
    ),
    "host_factory_table_callable": (
        """var host_factory_table_callable = (function() {
  const key = 'x';
  const read = target => target.getAttribute(key);
  function host_factory_table_callable(element) { return read(element) === null; }
  return {entry: host_factory_table_callable};
})().entry;
""",
        "1000",
    ),
    "host_factory_table_holder": (
        """var host_factory_table_holder = (function() {
  const key = 'x';
  const helpers = {read(target) { return target.getAttribute(key); }};
  function host_factory_table_holder(element) { return helpers.read(element) === null; }
  return {entry: host_factory_table_holder};
})().entry;
""",
        "1000",
    ),
    "host_factory_table_order": (
        r"""var host_factory_table_order = (function() {
  const key = 'x';
  const change = (target, saved) => {
    target.setAttribute(key, 'a\0b');
    return saved === target.getAttribute(key);
  };
  function host_factory_table_order(element) {
    return change(element, element.getAttribute(key));
  }
  return {entry: host_factory_table_order};
})().entry;
""",
        "0010",
    ),
    "host_factory_table_multiple": (
        """var host_factory_table_multiple = (function() {
  function host_factory_table_multiple(element) { return element.getAttribute('x') === null; }
  return {entry: host_factory_table_multiple, alias: host_factory_table_multiple};
})().alias;
""",
        "1000",
    ),
    "host_factory_table_own_prototype_name": (
        """var host_factory_table_own_prototype_name = (function() {
  function host_factory_table_own_prototype_name(element) {
    return element.getAttribute('x') === null;
  }
  return {toString: host_factory_table_own_prototype_name};
})().toString;
""",
        "1000",
    ),
}
HOST_CASES = {name: ("{\n" + source + "}\n", bits) for name, (source, bits) in HOST_CASES.items()}
BOOLEAN_CASES.update(HOST_CASES)
BOOLEAN_SOURCES += tuple((name, source, 1) for name, (source, _) in HOST_CASES.items())
BOOLEAN_DOUBLE = r"""
function observationElement(value) {
  const attributes = {'a\0b': 'whole', a: 'truncated'};
  if (value !== null) attributes.x = value;
  return {
    getAttribute(name) { return attributes[name] === undefined ? null : attributes[name]; },
    setAttribute(name, text) { attributes[name] = text; },
    removeAttribute(name) { delete attributes[name]; },
    hasAttribute(name) { return attributes[name] !== undefined; },
    toggleAttribute(name, force) {
      if (force) attributes[name] = '';
      else delete attributes[name];
      return force;
    },
    classList: {
      toggle(name, force) {
        if (name !== 'test-token' || force !== true) throw new Error('incorrect class toggle');
        return force;
      }
    }
  };
}
"""
BOOLEAN_OBSERVATIONS = "\n".join(
    f"var booleanObservation{i * 4 + j:03} = {name}(observationElement({value}));"
    for i, name in enumerate(BOOLEAN_CASES)
    for j, value in enumerate(("null", "''", r"'a\0b'", r"'\u00e9'"))
)
BOOLEAN_CHECKS = {
    **dict.fromkeys(
        ("helper_regex_original_h", "host_factory_original_h"),
        """assert(!doc.read().has_attribute(node, atoms.intern("data-bs-config")));
        assert(doc.read().attribute_value(node, atoms.intern("data-bs-toggle")) == "second");""",
    ),
    "helper_regex_direct": 'assert(doc.read().attribute_value(node, atoms.intern("data-bs-config")) == "value");',
    "helper_regex_original": 'assert(doc.read().attribute_value(node, atoms.intern("data-bs-config")) == "value");',
    "regex_mixed_constants": """assert(doc.read().attribute_value(node, atoms.intern("data-bs-config")) == "value");
        assert(!doc.read().has_attribute(node, atoms.intern("data-bs-toggle")));""",
    "helper_regex_distinct_names": """assert(doc.read().attribute_value(node, atoms.intern("data-bs-config")) == "first");
        assert(doc.read().attribute_value(node, atoms.intern("data-bs-toggle")) == "second");""",
    "helper_regex_forwarded_names": """assert(doc.read().attribute_value(node, atoms.intern("data-bs-config")) == "first");
        assert(doc.read().attribute_value(node, atoms.intern("data-bs-toggle")) == "second");""",
    "helper_regex_captured_names": """assert(doc.read().attribute_value(node, atoms.intern("data-bs-config")) == "first");
        assert(doc.read().attribute_value(node, atoms.intern("data-bs-toggle")) == "second");""",
    "helper_regex_captured_consumers": """assert(doc.read().attribute_value(node, atoms.intern("data-bs-config")) == "first");
        assert(doc.read().attribute_value(node, atoms.intern("data-bs-toggle")) == "second");
        assert(doc.read().attribute_value(node, atoms.intern("data-bs-direct")) == "third");""",
    "helper_regex_captured_chain": """assert(doc.read().attribute_value(node, atoms.intern("data-bs-config")) == "first");
        assert(doc.read().attribute_value(node, atoms.intern("data-bs-toggle")) == "second");""",
    "helper_regex_range": 'assert(doc.read().attribute_value(node, atoms.intern("data-bs-config")) == "value");',
    "helper_regex_no_flags": 'assert(doc.read().attribute_value(node, atoms.intern("data-bs-config")) == "value");',
    "helper_regex_bytes": r'assert(doc.read().attribute_value(node, atoms.intern("data-bs-config")) == std::string_view("a\0\xc3\xa9", 4));',
    "host_capture_name": 'assert(doc.read().attribute_value(node, atoms.intern("data-config")) == "value");',
    "host_capture_order": r'assert(doc.read().attribute_value(node, state) == std::string_view("a\0b", 3));',
    "host_factory_order": r'assert(doc.read().attribute_value(node, state) == std::string_view("a\0b", 3));',
    "host_factory_table_order": r'assert(doc.read().attribute_value(node, state) == std::string_view("a\0b", 3));',
    "helper_capture_saved": r'assert(doc.read().attribute_value(node, state) == std::string_view("a\0b", 3));',
    "helper_capture_repeated": 'assert(doc.read().attribute_value(node, state) == "after");',
    "helper_capture_callable_order": r'assert(doc.read().attribute_value(node, state) == std::string_view("a\0b", 3));',
    "helper_capture_holder_order": r'assert(doc.read().attribute_value(node, state) == std::string_view("a\0b", 3));',
    "helper_capture_forwarded_order": r'assert(doc.read().attribute_value(node, state) == std::string_view("a\0b", 3));',
    "helper_order": r'assert(doc.read().attribute_value(node, state) == std::string_view("a\0b", 3));',
    "helper_nested": 'assert(doc.read().attribute_value(node, atoms.intern("data-config")) == "value");',
    "helper_object_order": r'assert(doc.read().attribute_value(node, state) == std::string_view("a\0b", 3));',
    "helper_object_multiple": 'assert(doc.read().attribute_value(node, atoms.intern("data-config")) == "value");',
    "helper_object_nested": 'assert(doc.read().attribute_value(node, atoms.intern("data-config")) == "value");',
    "saved_equality": r'assert(doc.read().attribute_value(node, state) == std::string_view("a\0b", 3));',
    "comparison_force": 'assert(doc.read().has_attribute(node, atoms.intern("data-force")) == result);',
    "computed": """assert(!doc.read().has_attribute(node, atoms.intern("data-copy")));
        assert(doc.read().attribute_value(node, atoms.intern("class")) == "test-token");""",
}

# This double supplies only the source's platform methods. Node runs the same
# entry bodies imported below; real DOM casing/namespaces are checked separately.
ORACLE = r"""
const assert = require('node:assert/strict');
const names = [];
assert.equal(readNames({getAttribute(name) { names.push(name); return name; }}), 'a\0b');
assert.deepEqual(names, ['bad name', '', 'a\0b']);
assert.equal(readWideName({getAttribute(name) { return name; }}), '\ud800');
for (const entry of [readAttribute, savedAttribute, branch_optional_return, capture_callable, capture_holder, capture_forwarded, helper_regex]) {
  const key = entry === readAttribute || entry === savedAttribute ? 'DATA-State' : entry === helper_regex ? 'data-bs-config' : 'x';
  for (const expected of [null, '', 'a\0b', '\u00e9']) {
    let value = expected;
    const calls = [];
    const element = {
      getAttribute(name) {
        calls.push('get:' + name);
        return name === key ? value : null;
      },
      setAttribute(name, text) {
        calls.push('set:' + name + ':' + text);
        value = text;
      },
      removeAttribute(name) {
        calls.push('remove:' + name);
        value = null;
      }
    };
    const result = entry(element, element);
    assert.equal(result, expected);
    assert.deepEqual(calls, entry !== savedAttribute ? ['get:' + key] : [
      'get:DATA-State', 'get:data-missing', 'set:DATA-State:after',
      'get:DATA-State', 'remove:DATA-State'
    ]);
    assert.equal(value, entry !== savedAttribute ? expected : null);
    value = 'later';
    assert.equal(result, expected);
    const bytes = result === null ? null : Buffer.from(result, 'utf8');
    console.log(bytes === null ? 'null' : bytes.length + ':' + bytes.toString('hex'));
  }
}
"""

CLIENT = r"""
#include <array>
#include <cassert>
#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <type_traits>

using namespace ctbrowser;

static void observe(const std::optional<std::string> & value) {
    if (!value) { std::cout << "null\n"; return; }
    constexpr char hex[] = "0123456789abcdef";
    std::cout << value->size() << ':';
    for (char character : *value) {
        const auto byte = static_cast<unsigned char>(character);
        std::cout << hex[byte >> 4] << hex[byte & 15];
    }
    std::cout << '\n';
}

static void check_values(auto invoke, document & doc, element_ref element, bool saved,
                         std::string_view name = "data-state") {
    const auto state = doc.atoms().intern(name);
    const std::array<std::optional<std::string>, 4> values{
        std::nullopt, std::string{}, std::string{"a\0b", 3}, std::string{"\xc3\xa9"}};
    doc.log_writes(true);
    for (const auto & expected : values) {
        if (expected) { assert(doc.set_attribute(element.id, state, *expected)); }
        else { assert(doc.remove_attribute(element.id, state)); }
        (void)doc.take_writes();
        const auto version = doc.version();
        const auto result = invoke(element, element);
        static_assert(std::is_same_v<std::remove_cv_t<decltype(result)>,
                                     std::optional<std::string>>);
        assert(result == expected);
        const auto writes = doc.take_writes();
        if (saved) {
            assert(!doc.read().has_attribute(element.id, state));
            assert(doc.version() == version + 2);
            // The existing write log records sets, not removals.
            assert(writes.size() == 1);
            assert(writes[0].node == element.id && writes[0].name == state);
        } else { assert(writes.empty() && doc.version() == version); }
        assert(doc.set_attribute(element.id, state, "later"));
        assert(result == expected); // The returned String owns its bytes.
        observe(result);
    }
}

static void check_reads(auto read, document & doc, element_ref element) {
    auto & atoms = doc.atoms();
    const auto upper = atoms.intern("DATA-State"), lower = atoms.intern("data-state");
    assert(doc.set_attribute(element.id, upper, "upper"));
    assert(doc.set_attribute(element.id, lower, "lower"));
    assert(read(element) == "lower");
    const auto svg = doc.create_element(atoms.intern("svg"), node_ns::svg);
    assert(doc.set_attribute(svg, lower, "lower"));
    assert(!read(element_ref{&doc, svg}));
    assert(doc.set_attribute(svg, upper, "svg"));
    assert(read(element_ref{&doc, svg}) == "svg");
    doc.set_xml(true);
    assert(read(element) == "upper"); // HTML namespace in an XML document.
    doc.set_xml(false);

    const auto namespaced = doc.create_element(atoms.intern("button"));
    assert(doc.set_attribute_ns(namespaced, atoms.intern("urn:first"), lower, "first"));
    assert(doc.set_attribute_ns(namespaced, atoms.intern("urn:second"), lower, "second"));
    assert(read(element_ref{&doc, namespaced}) == "first");
    assert(doc.remove_attribute_ns(namespaced, "urn:first", "data-state"));
    assert(read(element_ref{&doc, namespaced}) == "second");

    assert(doc.remove_child(element.id));
    assert(!doc.read().parent(element.id));
    assert(read(element) == "lower"); // Detached nodes remain alive in the document.
}

static void check_invalid(auto invoke, document & doc, element_ref element) {
    const auto text = doc.create_text("not an element");
    (void)doc.take_writes();
    for (const element_ref invalid : {
             element_ref{}, element_ref{&doc, {}}, element_ref{&doc, text},
             element_ref{&doc, {element.id.slot, element.id.generation + 2}}}) {
        for (bool first : {false, true}) {
            bool caught = false;
            try { (void)invoke(first ? invalid : element, first ? element : invalid); }
            catch (const std::exception &) { caught = true; }
            assert(caught && doc.take_writes().empty());
        }
    }
}

int main() {
    @RUNS@
}
"""

FREE_RUN = r"""
    {
        atom_table atoms, foreign_atoms;
        document doc{atoms}, foreign_doc{foreign_atoms};
        const auto node = doc.create_element(atoms.intern("button"));
        const auto foreign_node = foreign_doc.create_element(foreign_atoms.intern("button"));
        assert(node == foreign_node); // IDs with identical bits have separate owners.
        assert(doc.append_child(doc.root(), node));
        const element_ref element{&doc, node}, foreign{&foreign_doc, foreign_node};
        auto invoke = [](element_ref first, element_ref second) {
            @INVOKE@
        };
        check_values(invoke, doc, element, @SAVED@);
        @CHECKS@
    }
"""
FREE_READ_CHECKS = r"""
        auto read = [](element_ref value) { return @ENTRY@(value); };
        check_reads(read, doc, element);
        assert(foreign_doc.set_attribute(foreign_node, foreign_atoms.intern("data-state"), "foreign"));
        assert(read(foreign) == "foreign");
        assert(read(element) == "lower");
        std::optional<std::string> copied;
        {
            atom_table temporary_atoms;
            document temporary{temporary_atoms};
            auto temporary_node = temporary.create_element(temporary_atoms.intern("button"));
            assert(temporary.set_attribute(temporary_node, temporary_atoms.intern("data-state"), "owned"));
            copied = read(element_ref{&temporary, temporary_node});
        }
        assert(copied == "owned"); // No borrow survives the source document's destruction.
"""
FREE_SAVED_CHECKS = r"""
        check_invalid(invoke, doc, element);
        assert(foreign_doc.set_attribute(foreign_node, foreign_atoms.intern("data-state"), "foreign"));
        assert(invoke(foreign, element) == "foreign");
        assert(!foreign_doc.read().has_attribute(foreign_node, foreign_atoms.intern("data-state")));
        assert(doc.read().attribute_value(node, atoms.intern("data-state")) == "later");
"""
SESSION_RUN = r"""
    {
        @OWNER@ session, foreign_session;
        auto & doc = session.document();
        auto & foreign_doc = foreign_session.document();
        auto node = doc.create_element(doc.atoms().intern("button"));
        auto foreign_node = foreign_doc.create_element(foreign_doc.atoms().intern("button"));
        assert(node == foreign_node);
        assert(doc.append_child(doc.root(), node));
        const element_ref element{&doc, node}, foreign{&foreign_doc, foreign_node};
        auto invoke = [&](element_ref first, element_ref second) {
            @INVOKE@
        };
        check_values(invoke, doc, element, @SAVED@);
        @CHECKS@
        (void)doc.take_writes();
        bool rejected = false;
        try { (void)invoke(foreign, foreign); }
        catch (const std::invalid_argument &) { rejected = true; }
        assert(rejected && doc.take_writes().empty());
    }
"""
SESSION_READ_CHECKS = """
        auto read = [&](element_ref value) { return session.invoke(value); };
        check_reads(read, doc, element);
"""
SESSION_SAVED_CHECKS = r"""
        check_invalid(invoke, doc, element);
        for (bool first : {false, true}) {
            bool caught = false;
            try { (void)invoke(first ? foreign : element, first ? element : foreign); }
            catch (const std::invalid_argument &) { caught = true; }
            assert(caught && doc.take_writes().empty());
        }
"""

NAMES_RUN = r"""
    {
        @SETUP@
        auto & atoms = doc.atoms();
        const auto node = doc.create_element(atoms.intern("button"));
        const element_ref element{&doc, node};
        assert(!@CALL@); // Invalid names are legal reads, absent means null.
        assert(doc.set_attribute(node, atoms.intern("a"), "truncated"));
        assert(doc.set_attribute(node, atoms.intern(@NAME@), "whole"));
        assert(@CALL@ == "whole"); // Preserve every source name byte.
    }
"""

BOOLEAN_RUN = r"""
    {
        @SETUP@
        auto & atoms = doc.atoms();
        const auto node = doc.create_element(atoms.intern("button"));
        const element_ref element{&doc, node};
        const auto state = atoms.intern("x");
        assert(doc.set_attribute(node, atoms.intern("a"), "truncated"));
        assert(doc.set_attribute(node, atoms.intern(std::string_view{"a\0b", 3}), "whole"));
        for (const auto & value : std::array<std::optional<std::string>, 4>{
                 std::nullopt, std::string{}, std::string{"a\0b", 3}, std::string{"\xc3\xa9"}}) {
            if (value) { assert(doc.set_attribute(node, state, *value)); }
            else { assert(doc.remove_attribute(node, state)); }
            const auto result = @CALL@;
            static_assert(std::is_same_v<std::remove_cv_t<decltype(result)>, bool>);
            @CHECKS@
            std::cout << (result ? "true\n" : "false\n");
        }
    }
"""

REFUSALS = {
    "branch_unsafe_then": "if (element.hasAttribute('x')) { element.unknown(); } return true;",
    "branch_unsafe_else": "if (element.hasAttribute('x')) { element.setAttribute('a', 'b'); } else { element.unknown(); } return true;",
    "branch_mixed_join": "let value; if (element.hasAttribute('x')) { value=true; } else { value='text'; } return value;",
    "branch_optional_undefined": "let value; if (element.hasAttribute('x')) { value=element.getAttribute('x'); } else { value=undefined; } return value;",
    "branch_borrowed_join": "let value; if (element.hasAttribute('x')) { value=element; } else { value=element.closest('x'); } return value === element;",
    "branch_loop": "while (element.hasAttribute('x')) { element.removeAttribute('x'); } return true;",
    "boolean-call": "return Boolean(element.getAttribute('x'));",
    "stringify": "return '' + element.getAttribute('x');",
    "loose-equality": "return element.getAttribute('x') == null;",
    "element-equality": "return element.getAttribute('x') === element;",
    "closest-equality": "return element.getAttribute('x') === element.closest('button');",
    "object-name": "return element.getAttribute('x' + {});",
    "object-value": "element.setAttribute('x', 'y' + {}); return true;",
    "return-null": "element.getAttribute('x'); return null;",
    "set-value": "element.setAttribute('y', element.getAttribute('x')); return true;",
    "dynamic-name": "return element.getAttribute(element.getAttribute('x'));",
    "string-property": "return element.getAttribute('x').length;",
    "nested-control": "if (element.hasAttribute('x')) return element.getAttribute('x'); return '';",
    "retained-result": "element.saved = element.getAttribute('x'); return true;",
    "dataset-escape": "element.dataset.saved = element.getAttribute('x'); return true;",
    "receiver-escape": "element.getAttribute('x'); return element;",
    "method-escape": "return element.getAttribute;",
    "method-write": "element.getAttribute = element; return element.getAttribute('x');",
    "missing-name": "return element.getAttribute();",
    "extra-name": "return element.getAttribute('x', 'y');",
    "non-string-name": "return element.getAttribute(null);",
}
HELPER_REFUSALS = {
    "capture_reassigned": "let key = 'x'; function read() { return element.getAttribute(key); } key = 'y'; return read();",
    "capture_reassigned_later": "let key = 'x'; function read() { return element.getAttribute(key); } const result = read(); key = 'y'; return result;",
    "capture_child_write": "let key = 'x'; function read() { key = 'y'; return element.getAttribute(key); } return read();",
    "capture_early_call": "function read() { return element.getAttribute(key); } const result = read(); var key = 'x'; return result;",
    "capture_early_read": "function read() { return element.getAttribute(key); } const before = key; var key = 'x'; element.getAttribute(before); return read();",
    "capture_unknown_effect": "const key = 'x'; function read() { return element.getAttribute(key); } sideEffect(); return read();",
    "capture_closure_escape": "function read() { return element.getAttribute('x'); } read(); return read;",
    "capture_borrowed_return": "function read() { element.getAttribute('x'); return element; } return read();",
    "capture_forwarded_reassigned": "let key = 'x'; function invoke() { function read() { return element.getAttribute(key); } return read(); } key = 'y'; return invoke();",
    "capture_forwarded_reassigned_later": "let key = 'x'; function invoke() { function read() { return element.getAttribute(key); } return read(); } const saved = invoke(); key = 'y'; return saved;",
    "capture_forwarded_parent_write": "let key = 'x'; function invoke() { key = 'y'; function read() { return element.getAttribute(key); } return read(); } return invoke();",
    "capture_forwarded_child_write": "let key = 'x'; function invoke() { function read() { key = 'y'; return element.getAttribute(key); } return read(); } return invoke();",
    "capture_forwarded_early_call": "function invoke() { function read() { return element.getAttribute(key); } return read(); } const saved = invoke(); var key = 'x'; return saved;",
    "capture_forwarded_escape": "function invoke() { function read() { return element.getAttribute('x'); } read(); return read; } return invoke();",
    "capture_forwarded_unused": "function invoke() { function read() { return element.getAttribute('x'); } return element.getAttribute('x'); } return invoke();",
    "capture_forwarded_receiver": "function invoke() { const read = () => { this.saved = element; return element.getAttribute('x'); }; return read(); } return invoke();",
    "capture_forwarded_unknown_effect": "function invoke() { function read() { sideEffect(); return element.getAttribute('x'); } return read(); } return invoke();",
    "capture_callable_reassigned": "let read = target => target.getAttribute('x'); function invoke() { return read(element); } read = target => target.getAttribute('y'); return invoke();",
    "capture_callable_reassigned_later": "let read = target => target.getAttribute('x'); function invoke() { return read(element); } const saved = invoke(); read = target => target.getAttribute('y'); return saved;",
    "capture_callable_escape": "function read(target) { return target.getAttribute('x'); } function invoke() { read(element); return read; } return invoke();",
    "capture_callable_metadata": "function read(target) { return target.getAttribute('x'); } function invoke() { read(element); return read.name; } return invoke();",
    "capture_callable_recursive": "function read(target) { return invoke(target); } function invoke(target) { target.getAttribute('x'); return read(target); } return invoke(element);",
    "capture_callable_early_call": "function invoke() { return read(element); } const saved = invoke(); var read = target => target.getAttribute('x'); return saved;",
    "capture_holder_reassigned": "let helpers = {read(target) { return target.getAttribute('x'); }}; function invoke() { return helpers.read(element); } helpers = {read(target) { return target.getAttribute('y'); }}; return invoke();",
    "capture_holder_late_overwrite": "const helpers = {read(target) { return target.getAttribute('x'); }}; function invoke() { return helpers.read(element); } const saved = invoke(); helpers.read = target => target.getAttribute('y'); return saved;",
    "capture_holder_escape": "const helpers = {read(target) { return target.getAttribute('x'); }}; function invoke() { helpers.read(element); return helpers; } return invoke();",
    "capture_holder_this": "const helpers = {read(target) { target.getAttribute('x'); return this; }}; function invoke() { return helpers.read(element); } return invoke();",
    "capture_holder_early_call": "function invoke() { return helpers.read(element); } const saved = invoke(); var helpers = {read(target) { return target.getAttribute('x'); }}; return saved;",
    "capture_holder_dynamic_key": "const helpers = {read(target) { return target.getAttribute('x'); }}; function invoke() { return helpers[element.getAttribute('key')](element); } return invoke();",
    "helper_this": "function read() { return this.getAttribute('x'); } return read();",
    "helper_new_target": "function read(target) { target.getAttribute('x'); return new.target; } return read(element);",
    "helper_recursive": "function read(target) { return read(target); } return read(element);",
    "helper_short": "function read(target, key) { return target.getAttribute(key); } return read(element);",
    "helper_extra": "function read(target) { return target.getAttribute('x'); } return read(element, 'x');",
    "helper_identity": "function read(target) { return target.getAttribute('x'); } read(element); return read;",
    "helper_metadata": "function read(target) { return target.getAttribute('x'); } read(element); return read.name;",
    "helper_unused": "function read(target) { return target.getAttribute('x'); } return true;",
    "helper_unknown": "function read(target) { sideEffect(); return target.getAttribute('x'); } return read(element);",
    "helper_branch": "function read(target) { if (target.hasAttribute('x')) return true; return false; } return read(element);",
    "helper_mutation": "function read(target) { return target.getAttribute('x'); } read.name = 'other'; return read(element);",
    "helper_optional_name": "function read(target, key) { return target.getAttribute(key); } return read(element, element.getAttribute('x'));",
    "helper_object_overwrite": "const helpers = {read(target) { return target.getAttribute('x'); }}; helpers.read = function(target) { return target.getAttribute('y'); }; return helpers.read(element);",
    "helper_object_duplicate": "const helpers = {read(target) { return target.getAttribute('x'); }, read(target) { return target.getAttribute('y'); }}; return helpers.read(element);",
    "helper_object_read_before_write": "const helpers = {}; const read = helpers.read; helpers.read = function(target) { return target.getAttribute('x'); }; return read(element);",
    "helper_object_missing": "const helpers = {read(target) { return target.getAttribute('x'); }}; helpers.read(element); return helpers.missing(element);",
    "helper_object_prototype": "const helpers = {read(target) { return target.getAttribute('x'); }}; helpers.read(element); return helpers.toString();",
    "helper_object_proto_key": "const helpers = {__proto__(target) { return target.getAttribute('x'); }}; return helpers.__proto__(element);",
    "helper_object_escape": "const helpers = {read(target) { return target.getAttribute('x'); }}; escaped = helpers; return helpers.read(element);",
    "helper_object_return": "const helpers = {read(target) { return target.getAttribute('x'); }}; helpers.read(element); return helpers;",
    "helper_object_argument": "function invoke(target, holder) { return holder.read(target); } const helpers = {read(target) { return target.getAttribute('x'); }}; return invoke(element, helpers);",
    "helper_object_this": "const helpers = {read(target) { this.saved = target; return target.getAttribute('x'); }}; return helpers.read(element);",
    "helper_object_this_read": "const helpers = {read(target) { target.getAttribute('x'); return this === target; }}; return helpers.read(element);",
    "helper_object_getter": "const helpers = {get read() { return function(target) { return target.getAttribute('x'); }; }}; return helpers.read(element);",
    "helper_object_dynamic_key": "const helpers = {read(target) { return target.getAttribute('x'); }}; return helpers[element.getAttribute('key')](element);",
    "helper_object_short": "const helpers = {read(target, key) { return target.getAttribute(key); }}; return helpers.read(element);",
    "helper_object_extra": "const helpers = {read(target) { return target.getAttribute('x'); }}; return helpers.read(element, 'x');",
    "helper_object_recursive": "const helpers = {read(target) { target.getAttribute('x'); return helpers.read(target); }}; return helpers.read(element);",
    "helper_object_call_receiver": "const helpers = {read(target) { return target.getAttribute('x'); }}; return helpers.read.call(element, element);",
    "helper_object_delete": "const helpers = {read(target) { return target.getAttribute('x'); }}; delete helpers.read; return helpers.read(element);",
    "helper_object_unrelated_write": "const helpers = {read(target) { return target.getAttribute('x'); }}; helpers.saved = element; return helpers.read(element);",
    "helper_object_prototype_write": "Object.prototype.read = element; const helpers = {read(target) { return target.getAttribute('x'); }}; return helpers.read(element);",
    "helper_object_unknown_effect": "const helpers = {read(target) { return target.getAttribute('x'); }}; sideEffect(); return helpers.read(element);",
    "helper_object_nested_receiver": "const helpers = {read(target) { const observe = () => this; observe(); return target.getAttribute('x'); }}; return helpers.read(element);",
}
REGEXP_REFUSALS = {
    "template_optional": "return element.getAttribute(`data-${element.getAttribute('x')}`);",
    "template_element": "return element.getAttribute(`data-${element}`);",
    "template_boolean": "return element.getAttribute(`data-${true}`);",
    "template_number": "return element.getAttribute(`data-${1}`);",
    "regex_original_h_matching": REGEXP_CASES["helper_regex_original_h"][0].replace(
        "H.setDataAttribute(element, 'config'", "H.setDataAttribute(element, 'Config'", 1
    ),
    "regex_original_h_live": REGEXP_CASES["helper_regex_original_h"][0].replace(
        "H.setDataAttribute(element, 'toggle'",
        "H.setDataAttribute(element, element.getAttribute('x')",
        1,
    ),
    "regex_factory_override": "__ctbrowser_regexp = function() { return null; }; "
    + BOOTSTRAP_F
    + " return element.getAttribute('data-bs-' + F('config'));",
    "regex_matching": BOOTSTRAP_F + " return element.getAttribute('data-bs-' + F('Config'));",
    "regex_dynamic": BOOTSTRAP_F
    + " return element.getAttribute('data-bs-' + F(element.getAttribute('x')));",
    "regex_mixed_matching": BOOTSTRAP_F
    + " element.setAttribute('data-bs-' + F('config'), 'value'); return element.getAttribute('data-bs-' + F('Toggle'));",
    "regex_mixed_matching_first": BOOTSTRAP_F
    + " element.setAttribute('data-bs-' + F('Config'), 'value'); return element.getAttribute('data-bs-' + F('toggle'));",
    "regex_mixed_live": BOOTSTRAP_F
    + " element.setAttribute('data-bs-' + F('config'), 'value'); return element.getAttribute('data-bs-' + F(element.getAttribute('x')));",
    "regex_mixed_live_first": BOOTSTRAP_F
    + " element.setAttribute('data-bs-' + F(element.getAttribute('x')), 'value'); return element.getAttribute('data-bs-' + F('toggle'));",
    "regex_captured_reassigned": BOOTSTRAP_F
    + " function read(target, name) { return target.getAttribute('data-bs-' + F(name)); } F = name => name; return read(element, 'config');",
    "regex_captured_reassigned_later": BOOTSTRAP_F
    + " function read(target, name) { return target.getAttribute('data-bs-' + F(name)); } const saved = read(element, 'config'); F = name => name; return saved;",
    "regex_captured_early_call": "function read(target, name) { return target.getAttribute('data-bs-' + F(name)); } const saved = read(element, 'config'); var F = t => t.replace(/[A-Z]/g, t => `-${t.toLowerCase()}`); return saved;",
    "regex_captured_identity": BOOTSTRAP_F
    + " function read(target, name) { target.getAttribute('data-bs-' + F(name)); return F; } return read(element, 'config');",
    "regex_captured_unused": BOOTSTRAP_F
    + " function read(target, name) { return target.getAttribute('data-bs-' + F(name)); } return element.getAttribute('data-bs-config');",
    "regex_forwarded_live": REGEXP_CASES["helper_regex_forwarded_names"][0].replace(
        "read(element, 'toggle')", "read(element, element.getAttribute('x'))"
    ),
    "regex_forwarded_matching": REGEXP_CASES["helper_regex_forwarded_names"][0].replace(
        "read(element, 'toggle')", "read(element, 'Toggle')"
    ),
    "regex_live_callback": "const key = 'Config'.replace(/[A-Z]/g, t => { element.setAttribute('x', t); return 'c'; }); return element.getAttribute('data-bs-' + key);",
    "regex_replace_override": "String.prototype.replace = function() { return 'other'; }; "
    + BOOTSTRAP_F
    + " return element.getAttribute('data-bs-' + F('config'));",
    "regex_replace_late_override": BOOTSTRAP_F
    + " const key = F('config'); String.prototype.replace = function() { return 'other'; }; return element.getAttribute('data-bs-' + key);",
    "regex_exec_override": "RegExp.prototype.exec = function() { return null; }; "
    + BOOTSTRAP_F
    + " return element.getAttribute('data-bs-' + F('config'));",
    "regex_symbol_override": "RegExp.prototype[Symbol.replace] = function() { return 'other'; }; "
    + BOOTSTRAP_F
    + " return element.getAttribute('data-bs-' + F('config'));",
    "regex_global_getter": "Object.defineProperty(RegExp.prototype, 'global', {get() { element.setAttribute('x', 'observed'); return true; }}); "
    + BOOTSTRAP_F
    + " return element.getAttribute('data-bs-' + F('config'));",
    "regex_custom_exec": "const pattern = /[A-Z]/g; pattern.exec = function() { return null; }; return element.getAttribute('config'.replace(pattern, t => t));",
    "regex_custom_replace": "const pattern = /[A-Z]/g; pattern[Symbol.replace] = function() { return 'other'; }; return element.getAttribute('config'.replace(pattern, t => t));",
    "regex_last_index_write": "const pattern = /[A-Z]/g; pattern.lastIndex = 1; return element.getAttribute('config'.replace(pattern, t => t));",
    "regex_last_index_read": "const pattern = /[A-Z]/g; element.getAttribute('config'.replace(pattern, t => t)); return pattern.lastIndex;",
    "regex_callback_escape": "const callback = t => `-${t.toLowerCase()}`; element.saved = callback; return element.getAttribute('config'.replace(/[A-Z]/g, callback));",
    "regex_callback_identity": "const callback = t => `-${t.toLowerCase()}`; element.getAttribute('config'.replace(/[A-Z]/g, callback)); return callback;",
    "regex_string_substitute": "const text = {replace() { return 'other'; }}; return element.getAttribute(text.replace(/[A-Z]/g, t => t));",
    "regex_ignore_case": "return element.getAttribute('config'.replace(/[A-Z]/gi, t => t));",
    "regex_unicode_flag": "return element.getAttribute('config'.replace(/[A-Z]/gu, t => t));",
    "regex_escaped_range": r"return element.getAttribute('config'.replace(/[\x41-\x5a]/g, t => t));",
    "regex_nested_pattern": "return element.getAttribute('config'.replace(/([A-Z])/g, t => t));",
}
for case in ("names", "consumers", "chain"):
    for order, name in (("first", "config"), ("last", "toggle")):
        before = f"F('{name}')" if case == "consumers" else f"read(element, '{name}')"
        for kind, value in (
            ("matching", repr(name.title())),
            ("live", "element.getAttribute('x')"),
        ):
            after = (
                f"F({value.replace('element.', 'target.')})"
                if case == "consumers"
                else f"read(element, {value})"
            )
            REGEXP_REFUSALS[f"regex_captured_{case}_{kind}_{order}"] = REGEXP_CASES[
                f"helper_regex_captured_{case}"
            ][0].replace(before, after, 1)
HELPER_REFUSALS.update(REGEXP_REFUSALS)
REFUSALS.update(HELPER_REFUSALS)
HOST_REFUSALS = {
    "host_wrapper_receiver": "const key = this; function host_refusal(element) { return element.getAttribute(key); }",
    "host_reassigned": "let key = 'x'; function host_refusal(element) { return element.getAttribute(key); } key = 'y'; key = 'z';",
    "host_entry_write": "let key = 'x'; function host_refusal(element) { key = 'y'; return element.getAttribute(key); }",
    "host_forwarded_write": "let key = 'x'; function host_refusal(element) { function read() { key = 'y'; return element.getAttribute(key); } return read(); }",
    "host_callable_reassigned": "let read = target => target.getAttribute('x'); function host_refusal(element) { return read(element); } read = target => target.getAttribute('y');",
    "host_holder_overwrite": "const helpers = {read(target) { return target.getAttribute('x'); }}; function host_refusal(element) { return helpers.read(element); } helpers.read = target => target.getAttribute('y');",
    "host_holder_escape": "const helpers = {read(target) { return target.getAttribute('x'); }}; function host_refusal(element) { return helpers.read(element); } escaped = helpers;",
    "host_callable_escape": "const read = target => target.getAttribute('x'); function host_refusal(element) { read(element); return read; }",
    "host_early_read": "const saved = key; var key = 'x'; function host_refusal(element) { return element.getAttribute(saved); }",
    "host_wrapper_effect": "const key = 'x'; sideEffect(); function host_refusal(element) { return element.getAttribute(key); }",
    "host_wrapper_read": "const key = externalKey; function host_refusal(element) { return element.getAttribute(key); }",
    "host_wrapper_call": "const key = 'x'; function host_refusal(element) { return element.getAttribute(key); } host_refusal({});",
    "host_duplicate_export": "const key = 'x'; function host_refusal(element) { return element.getAttribute(key); } host_refusal = host_refusal;",
    "host_other_export": "const key = 'x'; function host_refusal(element) { return element.getAttribute(key); } function other(element) { return element.getAttribute(key); }",
    "host_factory_repeated": "const make = function() { function host_refusal(element) { return element.getAttribute('x'); } return host_refusal; }; var host_refusal = make(); const second = make();",
    "host_factory_reassigned": "var host_refusal = (function() { let key = 'x'; function host_refusal(element) { return element.getAttribute(key); } key = 'y'; key = 'z'; return host_refusal; })();",
    "host_factory_entry_write": "var host_refusal = (function() { let key = 'x'; function host_refusal(element) { key = 'y'; return element.getAttribute(key); } return host_refusal; })();",
    "host_factory_holder_overwrite": "var host_refusal = (function() { const helpers = {read(target) { return target.getAttribute('x'); }}; function host_refusal(element) { return helpers.read(element); } helpers.read = target => target.getAttribute('y'); return host_refusal; })();",
    "host_factory_effect": "var host_refusal = (function() { sideEffect(); function host_refusal(element) { return element.getAttribute('x'); } return host_refusal; })();",
    "host_factory_global_read": "var host_refusal = (function() { const key = externalKey; function host_refusal(element) { return element.getAttribute(key); } return host_refusal; })();",
    "host_factory_capture": "const key = 'x'; var host_refusal = (function() { function host_refusal(element) { return element.getAttribute(key); } return host_refusal; })();",
    "host_factory_receiver": "var host_refusal = (function() { const key = this; function host_refusal(element) { return element.getAttribute(key); } return host_refusal; })();",
    "host_factory_new_target": "var host_refusal = (function() { const key = new.target; function host_refusal(element) { return element.getAttribute(key); } return host_refusal; })();",
    "host_factory_nested": "var host_refusal = (function() { return (function() { function host_refusal(element) { return element.getAttribute('x'); } return host_refusal; })(); })();",
    "host_factory_extra_argument": "var host_refusal = (function() { function host_refusal(element) { return element.getAttribute('x'); } return host_refusal; })('x');",
    "host_factory_missing_argument": "var host_refusal = (function(key) { function host_refusal(element) { return element.getAttribute(key); } return host_refusal; })();",
    "host_factory_identity": "const make = function() { function host_refusal(element) { return element.getAttribute('x'); } return host_refusal; }; const name = make.name; var host_refusal = make();",
    "host_factory_early_read": "var host_refusal = (function() { const saved = key; var key = 'x'; function host_refusal(element) { return element.getAttribute(saved); } return host_refusal; })();",
    "host_factory_table_missing": "var host_refusal = (function() { function host_refusal(element) { return element.getAttribute('x'); } return {entry: host_refusal}; })().missing;",
    "host_factory_table_inherited": "var host_refusal = (function() { function host_refusal(element) { return element.getAttribute('x'); } return {entry: host_refusal}; })().toString;",
    "host_factory_table_duplicate": "var host_refusal = (function() { function host_refusal(element) { return element.getAttribute('x'); } return {entry: host_refusal, entry: host_refusal}; })().entry;",
    "host_factory_table_mutation": "var host_refusal = (function() { function host_refusal(element) { return element.getAttribute('x'); } const table = {entry: host_refusal}; table.entry = host_refusal; return table; })().entry;",
    "host_factory_table_getter": "var host_refusal = (function() { function host_refusal(element) { return element.getAttribute('x'); } return {get entry() { return host_refusal; }}; })().entry;",
    "host_factory_table_identity": "var host_refusal = (function() { function host_refusal(element) { return element.getAttribute('x'); } const table = {entry: host_refusal}; const same = table === table; return table; })().entry;",
    "host_factory_table_repeated": "const make = function() { function host_refusal(element) { return element.getAttribute('x'); } return {entry: host_refusal}; }; var host_refusal = make().entry; const second = make().entry;",
    "host_factory_table_capture": "const key = 'x'; var host_refusal = (function() { function host_refusal(element) { return element.getAttribute(key); } return {entry: host_refusal}; })().entry;",
    "host_factory_table_effect": "var host_refusal = (function() { sideEffect(); function host_refusal(element) { return element.getAttribute('x'); } return {entry: host_refusal}; })().entry;",
    "host_factory_table_escape": "const table = (function() { function host_refusal(element) { return element.getAttribute('x'); } return {entry: host_refusal}; })(); escaped = table; var host_refusal = table.entry;",
    "host_factory_table_dynamic_key": "var host_refusal = (function() { function host_refusal(element) { return element.getAttribute('x'); } return {entry: host_refusal}; })()[externalKey];",
    "host_factory_table_receiver": "var host_refusal = (function() { function host_refusal(element) { element.getAttribute('x'); return this; } return {entry: host_refusal}; })().entry;",
    "host_factory_table_unused_method": "var host_refusal = (function() { function host_refusal(element) { return element.getAttribute('x'); } return {entry: host_refusal, unused(target) { return target.getAttribute('y'); }}; })().entry;",
    "host_factory_table_wrong_slot": "var host_refusal = (function() { function host_refusal(element) { return element.getAttribute('x'); } return {intended: host_refusal, entry(target) { return target.getAttribute('y'); }}; })().entry;",
    "host_factory_table_data_slot": "var host_refusal = (function() { function host_refusal(element) { return element.getAttribute('x'); } return {entry: host_refusal, count: 1}; })().entry;",
}


def emitted(args, module, name):
    text = module.read_text()
    entries = dom.NATIVE.findall(text)
    if len(entries) != 1 or dom.FUNCTION.search(text) or "ctnative.not_native" in text:
        raise RuntimeError(f"{name}: expected one complete typed DOM entry\n{text}")
    cpp = run([args.translate, "--mlir-to-cpp", str(module)]).stdout
    if (
        dom.VM.search(cpp)
        or re.search(
            r"nullable_scalar|nullable_string|std::variant|shared_ptr|weak_ptr|invoke_callable|\bmain\s*\(",
            cpp,
        )
        or "ctbrowser::get_element_attribute" not in cpp
        or "std::optional<std::string>" not in cpp
    ):
        raise RuntimeError(
            f"{name}: expected an ordinary optional String using the shared DOM API\n{cpp}"
        )
    return cpp, entries[0]


def helper_provenance_refusals(args, ir, contract):
    original = ir.read_text()

    def once(text, old, new):
        if text.count(old) != 1:
            raise RuntimeError(f"helper provenance mutation is not unique: {old}")
        return text.replace(old, new)

    functions = re.findall(r"^  ctjs\.func [^\n]+\n.*?^  }\n", original, re.M | re.S)
    if len(functions) != 3:
        raise RuntimeError("helper provenance controls require the complete read source")
    entry = next(body for body in functions if f"@{contract['entry']}(" in body.splitlines()[0])
    helper = next(body for body in functions if "@read$2(" in body.splitlines()[0])
    closure = "ctjs.create_closure %arg2[2] this %1"
    direct = "ctjs.call_direct @read$2(%4, %5, %2, %arg3, %3)"
    identity = "DOM helper lacks an exact local closure identity"
    call_shape = "DOM helper callable escapes or its call shape is unsupported"
    duplicate = once(helper, "@read$2(", "@duplicate$2(")
    alternative = once(helper, "@read$2(", "@alternative$3(")
    variants = {
        "duplicate-index": (
            once(original, helper, helper + duplicate),
            "DOM helper source function identity is ambiguous",
        ),
        "negative-index": (
            once(original, closure, closure.replace("[2]", "[-1]")),
            identity,
        ),
        "missing-index": (
            once(original, closure, closure.replace("[2]", "[2147483647]")),
            "DOM helper closure target is missing",
        ),
        "foreign-enclosing-closure": (
            once(original, closure, closure.replace("%arg2[", "%arg3[")),
            identity,
        ),
        "foreign-enclosing-this": (
            once(original, closure, closure.replace("this %1", "this %arg3")),
            identity,
        ),
        "forged-proof": (
            once(
                original,
                closure,
                closure.replace("%arg2[", "%arg3[") + " {ctnative.host_proved = true}",
            ),
            identity,
        ),
        "different-direct-target": (
            once(
                once(original, helper, helper + alternative),
                direct,
                direct.replace("@read$2(", "@alternative$3("),
            ),
            call_shape,
        ),
        "direct-receiver": (
            once(original, direct, direct.replace("(%4,", "(%arg3,")),
            call_shape,
        ),
        "direct-new-target": (
            once(original, direct, direct.replace(", %5,", ", %arg3,")),
            call_shape,
        ),
        "skipped-helper": (
            once(
                original,
                helper,
                once(helper, "attributes {", "attributes {ctjs.skipped = true, "),
            ),
            "DOM helper requires complete source functions and an uncaptured wrapper",
        ),
    }
    if closure not in entry or direct not in entry:
        raise RuntimeError("helper provenance controls no longer target the entry call")
    for name, (text, reason) in variants.items():
        mutated = args.work / f"helper-provenance-{name}.mlir"
        mutated.write_text(text)
        # Parsing and fingerprinting must succeed before checking the native
        # refusal: malformed IR is not evidence for a provenance guard.
        checked = dict(contract, module_sha256=dom.fingerprint(args.opt, mutated))
        for owned in (False, True):
            provider = "ctbrowser-dom-session-v1" if owned else "ctbrowser-dom-v1"
            for optimize in (False, True):
                diagnostic = dom.lower(
                    args,
                    mutated,
                    dict(checked, provider=provider),
                    f"helper-provenance-{name}-{owned}-{optimize}",
                    optimize=optimize,
                    success=False,
                )
                if f"error: native DOM source: {reason}" not in diagnostic:
                    raise RuntimeError(f"helper provenance {name}: wrong refusal\n{diagnostic}")
    # Each local helper calls only its own capture-free child. The 64th
    # expansion must hit the depth bound, independently of the work budget.
    body = "return target.getAttribute('x') === null;"
    for depth in reversed(range(64)):
        body = f"function deep{depth}(target) {{ {body} }} return deep{depth}(target);"
    body = once(body, "return deep0(target);", "return deep0(element);")
    deep_ir, deep_contract = dom.prepare(
        args,
        "helper-depth",
        f"function helper_depth(element) {{ {body} }}\n",
        1,
        entry_name="helper_depth",
    )
    diagnostic = dom.lower(args, deep_ir, deep_contract, "helper-depth", success=False)
    if "error: native DOM source: DOM helper call tree is recursive or too deep" not in diagnostic:
        raise RuntimeError(f"helper depth: wrong refusal\n{diagnostic}")
    # Flat sibling helpers capture the previous callable. Their iterative
    # expansion must retain the same depth limit as nested helper bodies.
    graph = "function graph0(target) { return target.getAttribute('x') === null; }\n"
    graph += "".join(
        f"function graph{depth}(target) {{ return graph{depth - 1}(target); }}\n"
        for depth in range(1, 64)
    )
    graph += "return graph63(element);"
    nested = "function inner0(target) { return target.getAttribute('x') === null; }\n"
    nested += "".join(
        f"function inner{depth}(target) {{ return inner{depth - 1}(target); }}\n"
        for depth in range(1, 32)
    )
    mixed = f"function graph0(target) {{ {nested} return inner31(target); }}\n"
    mixed += "".join(
        f"function graph{depth}(target) {{ return graph{depth - 1}(target); }}\n"
        for depth in range(1, 32)
    )
    mixed += "return graph31(element);"
    forwarded = "return element.getAttribute('x') === null;"
    for depth in reversed(range(64)):
        forwarded = f"function forward{depth}() {{ {forwarded} }} return forward{depth}();"
    for label, body in (
        ("capture-graph-depth", graph),
        ("capture-mixed-depth", mixed),
        ("capture-forwarded-depth", forwarded),
    ):
        graph_ir, graph_contract = dom.prepare(
            args,
            label,
            f"function capture_graph_depth(element) {{ {body} }}\n",
            1,
            entry_name="capture_graph_depth",
        )
        diagnostic = dom.lower(
            args, graph_ir, graph_contract, label, max_steps=10000000, success=False
        )
        if (
            "error: native DOM source: DOM helper call tree is recursive or too deep"
            not in diagnostic
        ):
            raise RuntimeError(f"{label}: wrong refusal\n{diagnostic}")
    return len(variants) * 4 + 4


def factory_provenance_checks(args, ir, contract, *, table=False):
    prefix = "factory-table" if table else "factory"
    original = ir.read_text()
    creator = "ctjs.create_closure %arg2[1] this %1"
    entry = "ctjs.create_closure %arg2[2] this %2 captures %1"
    call = "ctjs.call_direct @fn$1(%4, %5, %2, %3)"
    returned = "ctjs.return %4" if table else "ctjs.return %3"
    if any(original.count(anchor) != 1 for anchor in (creator, entry, call, returned)):
        raise RuntimeError("factory provenance anchors changed")
    variants = {
        "foreign-creator": original.replace(creator, creator.replace("%arg2", "%arg0")),
        "forged-creator": original.replace(
            creator, creator.replace("%arg2", "%arg0") + " {ctnative.host_proved = true}"
        ),
        "duplicate-creator": original.replace(creator, creator + "\n    %duplicate = " + creator),
        "wrong-callee": original.replace(call, call.replace("@fn$1", "@" + contract["entry"])),
        "receiver": original.replace(call, call.replace("(%4,", "(%3,")),
        "new-target": original.replace(call, call.replace("%4, %5,", "%4, %2,")),
        "repeated-call": original.replace(call, call + "\n    %again = " + call),
        "observed-result": original.replace(
            call, call + "\n    %same = ctjs.compare strict_eq %6, %6"
        ),
        "wrong-return": original.replace(returned, "ctjs.return %arg3"),
        "foreign-entry-creator": original.replace(entry, entry.replace("%arg2", "%arg0")),
        "implicit-capture": original.replace("ctjs.create_cell %arg3", "ctjs.create_cell %arg0"),
    }
    if table:
        selected = "ctjs.get_property %6[%7]"
        slot = "ctjs.set_property %4[%5], %3"
        name = '%7 = ctjs.constant #ctjs.string<"read-attribute">'
        if any(original.count(anchor) != 1 for anchor in (selected, slot, name)):
            raise RuntimeError("factory table provenance anchors changed")
        variants.update(
            {
                "missing-slot": original.replace(name, name.replace("read-attribute", "missing")),
                "prototype-setter": original.replace("read-attribute", "__proto__"),
                "duplicate-slot": original.replace(slot, slot + "\n    " + slot),
                "noncallable-slot": original.replace(slot, slot.replace(", %3", ", %arg3")),
                "early-slot-read": original.replace(
                    slot, "%early = ctjs.get_property %4[%5]\n    " + slot
                ),
                "dynamic-selection": original.replace(
                    selected, selected.replace("[%7]", "[%arg0]")
                ),
                "observed-selection": original.replace(
                    selected, selected + "\n    %same = ctjs.compare strict_eq %8, %8"
                ),
                "repeated-selection": original.replace(
                    selected, selected + "\n    %again = " + selected
                ),
            }
        )
    for name, text in variants.items():
        mutated = args.work / f"{prefix}-provenance-{name}.mlir"
        mutated.write_text(text)
        checked = dict(contract, module_sha256=dom.fingerprint(args.opt, mutated))
        for provider in ("ctbrowser-dom-v1", "ctbrowser-dom-session-v1"):
            for optimize in (False, True):
                diagnostic = dom.lower(
                    args,
                    mutated,
                    dict(checked, provider=provider),
                    f"{prefix}-provenance-{name}-{provider}-{optimize}",
                    optimize=optimize,
                    success=False,
                )
                if "error: native DOM source:" not in diagnostic:
                    raise RuntimeError(f"{prefix} {name}: wrong refusal\n{diagnostic}")
    indirect = args.work / f"{prefix}-indirect.mlir"
    indirect.write_text(original.replace(call, "ctjs.call %2(%4, %3)"))
    checked = dict(contract, module_sha256=dom.fingerprint(args.opt, indirect))
    dom.lower(args, indirect, checked, f"{prefix}-indirect")
    for budget in (0, 1, 128):
        diagnostic = dom.lower(
            args, ir, contract, f"{prefix}-budget-{budget}", max_steps=budget, success=False
        )
        if "budget exhausted" not in diagnostic:
            raise RuntimeError(f"factory initialization: missing budget refusal\n{diagnostic}")
    return len(variants) * 4 + 4


def host_provenance_checks(args, ir, contract):
    original = ir.read_text()
    closure = "ctjs.create_closure %arg2[1] this %3 captures %2"
    export = 'ctjs.store_global "host_capture_key", %4'
    cell = "ctjs.create_cell %1"
    if any(original.count(anchor) != 1 for anchor in (closure, export, cell)):
        raise RuntimeError("host initialization provenance anchors changed")
    variants = {
        "duplicate-creator": original.replace(closure, closure + "\n    %duplicate = " + closure),
        "wrong-export": original.replace(export, export.replace('"host_capture_key"', '"other"')),
        "foreign-creator": original.replace(closure, closure.replace("%arg2[", "%arg0[")),
        "forged-creator": original.replace(
            closure,
            closure.replace("%arg2[", "%arg0[") + " {ctnative.host_proved = true}",
        ),
        "wrapper-receiver": original.replace(cell, "ctjs.create_cell %arg0"),
        "entry-slot": original.replace("ctjs.load_upvalue %arg2[0]", "ctjs.load_upvalue %arg2[1]"),
        "capture-count": original.replace("upvalue_count = 1", "upvalue_count = 2"),
        "wrapper-return": original.replace("ctjs.return %5", "ctjs.return %1"),
        "wrapper-arity": original.replace(
            "@_script_$0(%arg0:", "@_script_$0(%extra: !ctjs.value, %arg0:"
        ),
    }
    for name, text in variants.items():
        mutated = args.work / f"host-provenance-{name}.mlir"
        mutated.write_text(text)
        checked = dict(contract, module_sha256=dom.fingerprint(args.opt, mutated))
        for provider in ("ctbrowser-dom-v1", "ctbrowser-dom-session-v1"):
            for optimize in (False, True):
                diagnostic = dom.lower(
                    args,
                    mutated,
                    dict(checked, provider=provider),
                    f"host-provenance-{name}-{provider}-{optimize}",
                    optimize=optimize,
                    success=False,
                )
                if "error: native DOM source:" not in diagnostic:
                    raise RuntimeError(f"host initialization {name}: wrong refusal\n{diagnostic}")
    for budget in (0, 1, 128):
        diagnostic = dom.lower(
            args, ir, contract, f"host-budget-{budget}", max_steps=budget, success=False
        )
        if "budget exhausted" not in diagnostic:
            raise RuntimeError(f"host initialization: missing budget refusal\n{diagnostic}")
    return len(variants) * 4 + 3


def capture_provenance_checks(args, ir, contract, graph_ir, graph_contract):
    original = ir.read_text()
    closure = "ctjs.create_closure %arg2[2] this %3 captures %2"
    load = "ctjs.load_upvalue %arg2[0]"
    store = "    ctjs.cell_set %2, %5\n"
    if any(original.count(anchor) != 1 for anchor in (closure, load, store)):
        raise RuntimeError("captured helper provenance anchors changed")
    cell = "DOM helper capture cell is mutable or escapes"
    slot = "DOM helper upvalue lacks an exact capture slot"
    variants = {
        "foreign-load": (original.replace(load, load.replace("%arg2", "%arg3")), slot),
        "forged-load-proof": (
            original.replace(
                load, load.replace("%arg2", "%arg3") + " {ctnative.host_proved = true}"
            ),
            slot,
        ),
        "negative-slot": (original.replace(load, load.replace("[0]", "[-1]")), slot),
        "missing-slot": (original.replace(load, load.replace("[0]", "[1]")), slot),
        "capture-count": (
            original.replace("upvalue_count = 1", "upvalue_count = 2"),
            "DOM helper capture target has not been completely expanded",
        ),
        "non-cell": (
            original.replace(closure, closure.replace("captures %2", "captures %arg3")),
            "DOM helper capture lacks a proved local cell",
        ),
        "forwarded-slot": (
            original.replace(
                closure,
                closure.replace("captures %2", "captures %3")
                + " {enclosing_indices = array<i32: 0>}",
            ),
            "DOM helper forwarded capture lacks an exact enclosing slot",
        ),
        "duplicate-closure": (
            original.replace(closure, closure + "\n    %duplicate = " + closure),
            "DOM helper requires an immutable local leaf capture",
        ),
        "second-write": (original.replace(store, store + store), cell),
        "late-write": (
            original.replace(store, "").replace(
                "    %9 = ctjs.constant", store + "    %9 = ctjs.constant"
            ),
            "DOM helper capture assignment does not precede its call",
        ),
        "early-read": (
            original.replace(store, "    %early = ctjs.cell_get %2\n" + store),
            "DOM helper capture assignment does not precede its read",
        ),
    }
    for name, (text, reason) in variants.items():
        mutated = args.work / f"capture-provenance-{name}.mlir"
        mutated.write_text(text)
        checked = dict(contract, module_sha256=dom.fingerprint(args.opt, mutated))
        for provider in ("ctbrowser-dom-v1", "ctbrowser-dom-session-v1"):
            for optimize in (False, True):
                diagnostic = dom.lower(
                    args,
                    mutated,
                    dict(checked, provider=provider),
                    f"capture-provenance-{name}-{provider}-{optimize}",
                    optimize=optimize,
                    success=False,
                )
                if f"error: native DOM source: {reason}" not in diagnostic:
                    raise RuntimeError(f"capture provenance {name}: wrong refusal\n{diagnostic}")
    diagnostic = dom.lower(args, ir, contract, "capture-budget", max_steps=128, success=False)
    if "budget exhausted" not in diagnostic:
        raise RuntimeError(f"capture query: missing work-budget refusal\n{diagnostic}")
    graph = graph_ir.read_text()
    cell = "    %3 = ctjs.create_cell %2\n"
    store = "    ctjs.cell_set %3, %5\n"
    after_call = "    %11 = ctjs.constant #ctjs.null\n"
    if any(graph.count(anchor) != 1 for anchor in (cell, store, after_call)):
        raise RuntimeError("captured callable storage anchors changed")
    initial = graph.replace(cell, "").replace(store, "    %3 = ctjs.create_cell %5\n")
    late = graph.replace(store, "").replace(after_call, store + after_call)
    for name, text, reason in (
        ("initial-cell", initial, None),
        ("uninitialized-call", late, "DOM helper capture assignment does not precede its call"),
    ):
        mutated = args.work / f"capture-storage-{name}.mlir"
        mutated.write_text(text)
        checked = dict(graph_contract, module_sha256=dom.fingerprint(args.opt, mutated))
        for provider in ("ctbrowser-dom-v1", "ctbrowser-dom-session-v1"):
            for optimize in (False, True):
                label = f"capture-storage-{name}-{provider}-{optimize}"
                result = dom.lower(
                    args,
                    mutated,
                    dict(checked, provider=provider),
                    label,
                    optimize=optimize,
                    success=reason is None,
                )
                if reason is None:
                    emitted(args, result, label)
                elif f"error: native DOM source: {reason}" not in result:
                    raise RuntimeError(f"capture storage {name}: wrong refusal\n{result}")
    return len(variants) * 4 + 9


def forwarded_provenance_checks(args, ir, contract):
    original = ir.read_text()
    closure = "ctjs.create_closure %arg2[3] this %2 captures %1"
    indices = "enclosing_indices = array<i32: 0>"
    if any(original.count(anchor) != 1 for anchor in (closure, indices)):
        raise RuntimeError("forwarded capture provenance anchors changed")
    slot = "DOM helper forwarded capture lacks an exact enclosing slot"
    variants = {
        "missing-parent-slot": (original.replace(indices, indices.replace("0>", "1>")), slot),
        "forged-parent-slot": (
            original.replace(
                indices, indices.replace("0>", "1>") + ", ctnative.host_proved = true"
            ),
            slot,
        ),
        "local-placeholder": (
            original.replace(indices, indices.replace("0>", "-1>")),
            "DOM helper capture lacks a proved local cell",
        ),
        "foreign-parent": (
            original.replace(closure, closure.replace("%arg2[", "%arg0[")),
            "DOM helper lacks an exact local closure identity",
        ),
        "duplicate-creator": (
            original.replace(
                closure + " {" + indices + "}",
                closure + " {" + indices + "}\n    %duplicate = " + closure + " {" + indices + "}",
            ),
            "DOM helper forwarded capture identity is ambiguous",
        ),
    }
    for name, (text, reason) in variants.items():
        mutated = args.work / f"forwarded-provenance-{name}.mlir"
        mutated.write_text(text)
        checked = dict(contract, module_sha256=dom.fingerprint(args.opt, mutated))
        for provider in ("ctbrowser-dom-v1", "ctbrowser-dom-session-v1"):
            for optimize in (False, True):
                diagnostic = dom.lower(
                    args,
                    mutated,
                    dict(checked, provider=provider),
                    f"forwarded-provenance-{name}-{provider}-{optimize}",
                    optimize=optimize,
                    success=False,
                )
                if f"error: native DOM source: {reason}" not in diagnostic:
                    raise RuntimeError(f"forwarded provenance {name}: wrong refusal\n{diagnostic}")
    diagnostic = dom.lower(args, ir, contract, "forwarded-budget", max_steps=128, success=False)
    if "budget exhausted" not in diagnostic:
        raise RuntimeError(f"forwarded capture: missing work-budget refusal\n{diagnostic}")
    return len(variants) * 4 + 1


def method_provenance_checks(args, ir, contract):
    original = ir.read_text()
    call = "ctjs.call %6(%1, %arg3, %7)"
    direct = "ctjs.call_direct @fn$2(%1, %2, %6, %arg3, %7)"
    helper = re.search(r"^  ctjs\.func @fn\$2[^\n]+\n.*?^  }\n", original, re.M | re.S)
    if original.count(call) != 1 or not helper:
        raise RuntimeError("object helper provenance anchors changed")
    exact = original.replace(call, direct)
    call_shape = "DOM helper callable escapes or its call shape is unsupported"
    alternative = helper[0].replace("@fn$2(", "@alternative$3(")
    variants = {
        "direct": (exact, None),
        "direct-target": (
            exact.replace(helper[0], helper[0] + alternative).replace(
                direct, direct.replace("@fn$2(", "@alternative$3(")
            ),
            call_shape,
        ),
        "direct-new-target": (
            exact.replace(direct, direct.replace(", %2,", ", %arg3,")),
            call_shape,
        ),
        "foreign-receiver": (original.replace(call, call.replace("(%1,", "(%arg3,")), call_shape),
        "foreign-callee": (
            original.replace(call, call.replace("%6(", "%arg3(")),
            "DOM helper object escapes or observes its identity",
        ),
        "observed-receiver": (
            original.replace(
                helper[0], helper[0].replace("ctjs.get_property %arg3[", "ctjs.get_property %arg0[")
            ),
            "DOM helper observes an implicit argument",
        ),
    }
    for name, (text, reason) in variants.items():
        mutated = args.work / f"method-provenance-{name}.mlir"
        mutated.write_text(text)
        checked = dict(contract, module_sha256=dom.fingerprint(args.opt, mutated))
        for provider in ("ctbrowser-dom-v1", "ctbrowser-dom-session-v1"):
            for optimize in (False, True):
                label = f"method-provenance-{name}-{provider}-{optimize}"
                result = dom.lower(
                    args,
                    mutated,
                    dict(checked, provider=provider),
                    label,
                    optimize=optimize,
                    success=reason is None,
                )
                if reason is None:
                    cpp, _ = emitted(args, result, label)
                    if "ctjs.call" in cpp or "script::" in cpp:
                        raise RuntimeError(f"{label}: direct method retained dynamic dispatch")
                elif f"error: native DOM source: {reason}" not in result:
                    raise RuntimeError(f"{label}: wrong refusal\n{result}")
    return len(variants) * 4


def regexp_provenance_checks(args, ir, contract, *, prefix="replacement"):
    original = ir.read_text()
    variants = {
        "factory": ('ctjs.load_global "__ctbrowser_regexp"', 'ctjs.load_global "RegExp"'),
        "matching": ('#ctjs.string<"[A-Z]">', '#ctjs.string<"[a-z]">'),
        "flags": ('#ctjs.string<"g">', '#ctjs.string<"gi">'),
        "creator": ("ctjs.create_closure %arg2[3]", "ctjs.create_closure %arg0[3]"),
        "callback-target": ("ctjs.create_closure %arg2[3]", "ctjs.create_closure %arg2[2]"),
        "direct-target": ("ctjs.call_direct @F$2(", "ctjs.call_direct @fn$3("),
        "mixed-arguments": ('#ctjs.string<"config">', '#ctjs.string<"Config">'),
    }
    if prefix == "replacement-mixed":
        variants["second-matching"] = ('#ctjs.string<"toggle">', '#ctjs.string<"Toggle">')
    if prefix == "replacement-captured":
        # F is reached through the proved capture, so the imported call is indirect.
        del variants["direct-target"]
    for name, (before, after) in variants.items():
        if before not in original:
            raise RuntimeError(f"replacement provenance anchor changed: {name}")
        mutated = args.work / f"{prefix}-provenance-{name}.mlir"
        mutated.write_text(original.replace(before, after, 1))
        checked = dict(contract, module_sha256=dom.fingerprint(args.opt, mutated))
        for provider in ("ctbrowser-dom-v1", "ctbrowser-dom-session-v1"):
            for optimize in (False, True):
                diagnostic = dom.lower(
                    args,
                    mutated,
                    dict(checked, provider=provider),
                    f"{prefix}-provenance-{name}-{provider}-{optimize}",
                    optimize=optimize,
                    success=False,
                )
                if "error: native DOM source:" not in diagnostic:
                    raise RuntimeError(f"replacement {name}: wrong refusal\n{diagnostic}")
    for budget in (0, 32, 64):
        diagnostic = dom.lower(
            args, ir, contract, f"{prefix}-budget-{budget}", max_steps=budget, success=False
        )
        if "budget exhausted" not in diagnostic:
            raise RuntimeError(f"replacement budget {budget}: wrong refusal\n{diagnostic}")
    return 4 * len(variants) + 3


def main():
    vendor = Path(__file__).resolve().parents[4] / "ctbrowser/vendor/bootstrap/bootstrap.bundle.js"
    if any(source not in vendor.read_text() for source in (BOOTSTRAP_F, BOOTSTRAP_H_WRITES)):
        raise RuntimeError("original Bootstrap F/H source changed")
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("translate", "opt", "clang", "node", "reference"):
        parser.add_argument("--" + name, required=True)
    for name in ("build", "include", "work"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--nm", default=shutil.which("nm"))
    args = parser.parse_args()
    args.work = args.work.resolve()
    args.work.mkdir(parents=True, exist_ok=True)
    if not args.nm:
        raise RuntimeError("native DOM String gate requires nm")
    boolean_values = [
        "true" if bit == "1" else "false" for _, bits in BOOLEAN_CASES.values() for bit in bits
    ]
    boolean_source = "".join(source for _, source, _ in BOOLEAN_SOURCES)
    boolean_oracle = boolean_source + BOOLEAN_DOUBLE + BOOLEAN_OBSERVATIONS
    oracle = args.work / "oracle.js"
    oracle.write_text(
        READ
        + SAVED
        + NAMES
        + WIDE
        + CAPTURE_SOURCE
        + ORACLE
        + boolean_oracle
        + "\n"
        + "\n".join(
            f"assert.equal(booleanObservation{i:03}, {value}); console.log(booleanObservation{i:03});"
            for i, value in enumerate(boolean_values)
        )
    )
    expected = run([args.node, str(oracle)]).stdout
    if expected != "null\n0:\n3:610062\n2:c3a9\n" * (2 + len(CAPTURE_RETURNS)) + "".join(
        value + "\n" for value in boolean_values
    ):
        raise RuntimeError("source optional String observations were not completed")
    observed = args.work / "vm-oracle.js"
    observed.write_text(
        READ
        + SAVED
        + WIDE
        + CAPTURE_SOURCE
        + """
function makeElement(value) {
  return {
    getAttribute(name) { return name === 'DATA-State' || name === 'x' || name === 'data-bs-config' ? value : null; },
    setAttribute(name, text) { value = text; },
    removeAttribute(name) { value = null; }
  };
}
"""
        + "\n".join(
            f"var observation{i * 4 + j} = {entry}(makeElement({value}), makeElement('other'));"
            for i, entry in enumerate(("readAttribute", "savedAttribute", *CAPTURE_RETURNS))
            for j, value in enumerate(("null", "''", r"'a\0b'", r"'\u00e9'"))
        )
    )
    with observed.open("a") as stream:
        stream.write("\nvar nameBytes = readWideName({getAttribute(name) { return name; }});\n")
        stream.write(boolean_oracle)
    reference = run([args.reference, str(observed)]).stdout
    encoded = [f"booleanObservation{i:03}={value}\n" for i, value in enumerate(boolean_values)]
    encoded.append('nameBytes="%ED%A0%80"\n')
    for index, line in enumerate(expected.splitlines()[: 4 * (2 + len(CAPTURE_RETURNS))]):
        value = (
            "null"
            if line == "null"
            else '"' + quote_from_bytes(bytes.fromhex(line.split(":")[1])) + '"'
        )
        encoded.append(f"observation{index}={value}\n")
    if reference != "".join(sorted(encoded, key=lambda line: line.partition("=")[0])):
        raise RuntimeError(f"VM optional String observations disagree with Node: {reference}")
    compilers = find_compilers()
    compilers[1] = args.clang
    includes, libraries = dom.link_options(args)
    prepared = [
        (
            name,
            *dom.prepare(
                args,
                name,
                source,
                count,
                entry_name=(
                    "invalid"
                    if name in CAPTURE_RETURNS
                    else name if name in HELPER_CASES or name in HOST_CASES else None
                ),
            ),
        )
        for name, source, count in SOURCES + BOOLEAN_SOURCES
    ]
    for optimize in (False, True):
        modules = {}
        for provider in ("ctbrowser-dom-v1", "ctbrowser-dom-session-v1"):
            owned = "session" in provider
            for name, ir, contract in prepared:
                label = f"{name}-{owned}-{optimize}"
                native = dom.lower(
                    args, ir, dict(contract, provider=provider), label, optimize=optimize
                )
                deduced = args.work / f"{label}.deduced.mlir"
                run([args.opt, str(native), "--ctnative-print-deduced", "-o", str(deduced)])
                modules[name, owned] = {"explicit": native, "deduced": deduced}
        for layout in ("explicit", "deduced"):
            headers, bodies, runs = set(), [], []
            for (name, owned), layouts in modules.items():
                namespace = f"{name}_{'session' if owned else 'free'}"
                cpp, symbol = emitted(args, layouts[layout], namespace)
                # Hoist includes before isolating each complete translation unit;
                # generated local helper names need not be globally unique.
                headers.update(re.findall(r"^#include[^\n]*", cpp, re.M))
                body = re.sub(r"^#include[^\n]*\n?", "", cpp, flags=re.M)
                bodies.append(f"namespace {namespace} {{\n{body}\n}}\n")
                entry = namespace + "::" + symbol
                if name in ("names", "wide") or name in BOOLEAN_CASES:
                    setup = (
                        f"{entry}_session session; auto & doc = session.document();"
                        if owned
                        else "atom_table atoms_owner; document doc{atoms_owner};"
                    )
                    call = "session.invoke(element)" if owned else entry + "(element)"
                    if name in BOOLEAN_CASES:
                        runs.append(
                            BOOLEAN_RUN.replace("@SETUP@", setup)
                            .replace("@CALL@", call)
                            .replace("@CHECKS@", BOOLEAN_CHECKS.get(name, ""))
                        )
                        continue
                    key = (
                        r'std::string_view{"a\0b", 3}'
                        if name == "names"
                        else r'std::string_view{"\xed\xa0\x80", 3}'
                    )
                    runs.append(
                        NAMES_RUN.replace("@SETUP@", setup)
                        .replace("@CALL@", call)
                        .replace("@NAME@", key)
                    )
                    continue
                saved = name == "saved"
                if owned:
                    checks = SESSION_SAVED_CHECKS if saved else SESSION_READ_CHECKS
                    target = "session.invoke"
                    client = SESSION_RUN.replace("@OWNER@", entry + "_session")
                else:
                    checks = FREE_SAVED_CHECKS if saved else FREE_READ_CHECKS
                    target = entry
                    client = FREE_RUN
                if name in CAPTURE_RETURNS:
                    checks = "" if owned else "assert(!invoke(foreign, element));"
                    key = "data-bs-config" if name == "helper_regex" else "x"
                    client = client.replace(
                        "check_values(invoke, doc, element, @SAVED@)",
                        f'check_values(invoke, doc, element, false, "{key}")',
                    )
                call = (
                    f"return {target}(first, second);"
                    if saved
                    else f"(void)second; return {target}(first);"
                )
                runs.append(
                    client.replace("@CHECKS@", checks)
                    .replace("@ENTRY@", entry)
                    .replace("@INVOKE@", call)
                    .replace("@SAVED@", "true" if saved else "false")
                )
            path = args.work / f"combined-{optimize}-{layout}.cpp"
            path.write_text(
                "\n".join(sorted(headers))
                + "\n"
                + "\n".join(bodies)
                + CLIENT.replace("@RUNS@", "\n".join(runs))
            )
            for index, compiler in enumerate(compilers):
                binary = path.with_suffix(f".{index}")
                run([compiler, *FLAGS, *includes, str(path), *libraries, "-o", str(binary)])
                if dom.VM.search(run([args.nm, "-C", str(binary)]).stdout):
                    raise RuntimeError("native DOM optional Strings link Script/AOT")
                if run([str(binary)]).stdout != expected * 2:
                    raise RuntimeError("native optional String observations disagree with source")
    for name, body in REFUSALS.items():
        ir, contract = dom.prepare(
            args,
            name,
            f"function invalid(element) {{ {body} }}\n",
            1,
            entry_name="invalid" if name in HELPER_REFUSALS else None,
        )
        for owned in (False, True):
            provider = "ctbrowser-dom-session-v1" if owned else "ctbrowser-dom-v1"
            for optimize in (False, True):
                diagnostic = dom.lower(
                    args,
                    ir,
                    dict(contract, provider=provider),
                    f"{name}-{owned}-{optimize}",
                    optimize=optimize,
                    success=False,
                )
                if "DOM" not in diagnostic:
                    raise RuntimeError(f"{name}: missing intended DOM proof refusal\n{diagnostic}")
    for name, source in HOST_REFUSALS.items():
        ir, contract = dom.prepare(
            args, name, "{\n" + source + "\n}\n", 1, entry_name="host_refusal"
        )
        for provider in ("ctbrowser-dom-v1", "ctbrowser-dom-session-v1"):
            for optimize in (False, True):
                diagnostic = dom.lower(
                    args,
                    ir,
                    dict(contract, provider=provider),
                    f"{name}-{provider}-{optimize}",
                    optimize=optimize,
                    success=False,
                )
                if "DOM" not in diagnostic:
                    raise RuntimeError(f"{name}: missing intended DOM proof refusal\n{diagnostic}")
    _, branch_ir, branch_contract = next(row for row in prepared if row[0] == "branch_nested")
    for budget in (0, 1, 32):
        diagnostic = dom.lower(
            args,
            branch_ir,
            branch_contract,
            f"branch-budget-{budget}",
            max_steps=budget,
            success=False,
        )
        if "budget exhausted" not in diagnostic:
            raise RuntimeError(f"branch budget {budget}: wrong refusal\n{diagnostic}")
    for depth in (63, 64):
        source = (
            "function branchDepth(element) {"
            + "if (element.hasAttribute('x')) {" * depth
            + "element.setAttribute('marker', 'yes');"
            + "}" * depth
            + "return true;}"
        )
        ir, contract = dom.prepare(
            args, f"branch-depth-{depth}", source, 1, entry_name="branchDepth"
        )
        for provider in ("ctbrowser-dom-v1", "ctbrowser-dom-session-v1"):
            for optimize in (False, True):
                result = dom.lower(
                    args,
                    ir,
                    dict(contract, provider=provider),
                    f"branch-depth-{depth}-{provider}-{optimize}",
                    optimize=optimize,
                    success=depth == 63,
                )
                if depth == 64 and "DOM entry branch depth" not in result:
                    raise RuntimeError(f"branch depth: wrong refusal\n{result}")
    _, helper_ir, helper_contract = next(row for row in prepared if row[0] == "helper_nested")
    for budget in (0, 1, 32):
        diagnostic = dom.lower(
            args,
            helper_ir,
            helper_contract,
            f"helper-budget-{budget}",
            success=False,
            max_steps=budget,
        )
        if "budget exhausted" not in diagnostic:
            raise RuntimeError(f"helper budget {budget}: missing bounded refusal\n{diagnostic}")
    stale = dict(helper_contract, module_sha256="0" * 64)
    if "fingerprint mismatch" not in dom.lower(
        args, helper_ir, stale, "helper-stale", success=False
    ):
        raise RuntimeError("DOM helper preparation accepted a stale source fingerprint")
    _, helper_ir, helper_contract = next(row for row in prepared if row[0] == "helper_read")
    provenance_checks = helper_provenance_refusals(args, helper_ir, helper_contract)
    _, method_ir, method_contract = next(
        row for row in prepared if row[0] == "helper_object_method"
    )
    method_checks = method_provenance_checks(args, method_ir, method_contract)
    _, capture_ir, capture_contract = next(
        row for row in prepared if row[0] == "helper_capture_key"
    )
    _, graph_ir, graph_contract = next(
        row for row in prepared if row[0] == "helper_capture_callable"
    )
    capture_checks = capture_provenance_checks(
        args, capture_ir, capture_contract, graph_ir, graph_contract
    )
    _, forwarded_ir, forwarded_contract = next(
        row for row in prepared if row[0] == "capture_forwarded"
    )
    capture_checks += forwarded_provenance_checks(args, forwarded_ir, forwarded_contract)
    _, host_ir, host_contract = next(row for row in prepared if row[0] == "host_capture_key")
    capture_checks += host_provenance_checks(args, host_ir, host_contract)
    _, factory_ir, factory_contract = next(row for row in prepared if row[0] == "host_factory_key")
    capture_checks += factory_provenance_checks(args, factory_ir, factory_contract)
    _, table_ir, table_contract = next(
        row for row in prepared if row[0] == "host_factory_table_key"
    )
    capture_checks += factory_provenance_checks(args, table_ir, table_contract, table=True)
    _, regexp_ir, regexp_contract = next(
        row for row in prepared if row[0] == "helper_regex_original"
    )
    replacement_checks = regexp_provenance_checks(args, regexp_ir, regexp_contract)
    for prefix, source in (
        ("replacement-mixed", REGEXP_MIXED_SOURCE),
        ("replacement-captured", REGEXP_CAPTURED_SOURCE),
    ):
        ir, contract = dom.prepare(args, prefix, source, 1, entry_name="invalid")
        for provider in ("ctbrowser-dom-v1", "ctbrowser-dom-session-v1"):
            for optimize in (False, True):
                label = f"{prefix}-{provider}-{optimize}"
                native = dom.lower(
                    args,
                    ir,
                    dict(contract, provider=provider),
                    label,
                    optimize=optimize,
                )
                emitted(args, native, label)
        replacement_checks += 4
        replacement_checks += regexp_provenance_checks(args, ir, contract, prefix=prefix)
    print(
        f"native DOM Strings: {9 + 4 * len(CAPTURE_RETURNS) + len(boolean_values)} Node/VM observations, 8 GCC/Clang binaries, "
        f"both providers/policies/layouts; {(len(REFUSALS) + len(HOST_REFUSALS)) * 4} source refusal checks, "
        f"{provenance_checks} provenance/depth refusal checks, {method_checks} method provenance checks, "
        f"{capture_checks} capture provenance/budget checks; "
        f"{replacement_checks} replacement provenance/budget checks; 11 branch depth/budget checks"
    )


if __name__ == "__main__":
    main()
