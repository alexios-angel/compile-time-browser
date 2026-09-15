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
    "capture_callable": "function invalid(element) { function read(target) { return target.getAttribute('x'); } function invoke() { return read(element); } return invoke(); }\n",
    "capture_holder": "function invalid(element) { const helpers = {read(target) { return target.getAttribute('x'); }}; function invoke() { return helpers.read(element); } return invoke(); }\n",
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
BOOLEAN_CASES.update(HELPER_CASES)
BOOLEAN_SOURCES = tuple(
    (name, f"function {name}(element) {{ {body} }}\n", 1)
    for name, (body, _) in BOOLEAN_CASES.items()
)
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
    "helper_capture_saved": r'assert(doc.read().attribute_value(node, state) == std::string_view("a\0b", 3));',
    "helper_capture_repeated": 'assert(doc.read().attribute_value(node, state) == "after");',
    "helper_capture_callable_order": r'assert(doc.read().attribute_value(node, state) == std::string_view("a\0b", 3));',
    "helper_capture_holder_order": r'assert(doc.read().attribute_value(node, state) == std::string_view("a\0b", 3));',
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
for (const entry of [readAttribute, savedAttribute, capture_callable, capture_holder]) {
  const key = entry === capture_callable || entry === capture_holder ? 'x' : 'DATA-State';
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
    "capture_forwarded": "function invoke() { function read() { return element.getAttribute('x'); } return read(); } return invoke();",
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
    "helper_regex": "const F = t => t.replace(/[A-Z]/g, t => `-${t.toLowerCase()}`); return element.getAttribute('data-bs-' + F('config'));",
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
REFUSALS.update(HELPER_REFUSALS)


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
            "DOM helper requires complete source functions and an uncaptured entry",
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
    for label, body in (("capture-graph-depth", graph), ("capture-mixed-depth", mixed)):
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
        "negative-slot": (original.replace(load, load.replace("[0]", "[-1]")), cell),
        "missing-slot": (original.replace(load, load.replace("[0]", "[1]")), cell),
        "capture-count": (original.replace("upvalue_count = 1", "upvalue_count = 2"), cell),
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
            "DOM helper requires an immutable local leaf capture",
        ),
        "duplicate-closure": (
            original.replace(closure, closure + "\n    %duplicate = " + closure),
            cell,
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
    getAttribute(name) { return name === 'DATA-State' || name === 'x' ? value : null; },
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
                    "invalid" if name in CAPTURE_RETURNS else name if name in HELPER_CASES else None
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
                    client = client.replace(
                        "check_values(invoke, doc, element, @SAVED@)",
                        'check_values(invoke, doc, element, false, "x")',
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
    print(
        f"native DOM Strings: {9 + 4 * len(CAPTURE_RETURNS) + len(boolean_values)} Node/VM observations, 8 GCC/Clang binaries, "
        f"both providers/policies/layouts; {len(REFUSALS) * 4} source refusal checks, "
        f"{provenance_checks} provenance/depth refusal checks, {method_checks} method provenance checks, "
        f"{capture_checks} capture provenance/budget checks"
    )


if __name__ == "__main__":
    main()
