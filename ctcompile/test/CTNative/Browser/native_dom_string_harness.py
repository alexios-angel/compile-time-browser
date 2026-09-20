"""native dom string harness: continued from native_dom_string_cases."""

from CTNative.Browser.native_dom_string_cases import *

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
        ("branch_loop", "helper_branch_loop"),
        "assert(!doc.read().has_attribute(node, state));",
    ),
    "set_value": 'assert(doc.read().attribute_value(node, atoms.intern("y")) == (value ? std::string_view(*value) : std::string_view("null")));',
    "helper_completion_effects": r"""assert(doc.read().attribute_value(node, atoms.intern("marker")) ==
            (!value ? "missing" : value->empty() ? "empty" :
             *value == std::string_view("a\0b", 3) ? "nul" : "wide"));""",
    "helper_completion_capture_snapshot": """assert(doc.read().attribute_value(node, state) == "after");
        assert(doc.read().attribute_value(node, atoms.intern("marker")) ==
            (!value ? "missing" : value->empty() ? "empty" : "present"));""",
    "helper_branch_effects": 'assert(doc.read().has_attribute(node, atoms.intern("marker")) == static_cast<bool>(result));',
    "helper_branch_saved": 'assert(doc.read().attribute_value(node, state) == "after");',
    "helper_branch_capture": 'assert(doc.read().attribute_value(node, state) == "after");',
    "helper_branch_capture_arms": 'assert(doc.read().attribute_value(node, state) == "after");',
    "helper_branch_early_effects": 'assert(doc.read().attribute_value(node, atoms.intern("marker")) == (result ? "missing" : "present"));',
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
    "comparison_force": 'assert(doc.read().has_attribute(node, atoms.intern("data-force")) == static_cast<bool>(result));',
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
for (const entry of [readAttribute, savedAttribute, helper_branch_call_in_arm, host_local_function_copy, branch_optional_return, helper_optional_return, capture_callable, capture_holder, capture_forwarded, helper_regex, nested_control]) {
  const key = entry === readAttribute || entry === savedAttribute ? 'DATA-State' : entry === helper_regex ? 'data-bs-config' : 'x';
  for (const expected of [null, '', 'a\0b', '\u00e9']) {
    let value = expected;
    const calls = [];
    const element = {
      hasAttribute(name) { calls.push('has:' + name); return name === key && value !== null; },
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
    const answer = entry === nested_control && expected === null ? '' : expected;
    assert.equal(result, answer);
    assert.deepEqual(calls, entry === helper_branch_call_in_arm || entry === nested_control
      ? (expected === null ? ['has:x'] : ['has:x', 'get:x'])
      : entry !== savedAttribute ? ['get:' + key] : [
      'get:DATA-State', 'get:data-missing', 'set:DATA-State:after',
      'get:DATA-State', 'remove:DATA-State'
    ]);
    assert.equal(value, entry !== savedAttribute ? expected : null);
    value = 'later';
    assert.equal(result, answer);
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
                         std::string_view name = "data-state", bool missing_empty = false) {
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
        const std::optional<std::string> answer = !expected && missing_empty
            ? std::optional<std::string>{std::string{}} : expected;
        assert(result == answer);
        const auto writes = doc.take_writes();
        if (saved) {
            assert(!doc.read().has_attribute(element.id, state));
            assert(doc.version() == version + 2);
            // The existing write log records sets, not removals.
            assert(writes.size() == 1);
            assert(writes[0].node == element.id && writes[0].name == state);
        } else { assert(writes.empty() && doc.version() == version); }
        assert(doc.set_attribute(element.id, state, "later"));
        assert(result == answer); // The returned String owns its bytes.
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
            static_assert(std::is_same_v<std::remove_cv_t<decltype(result)>, ctnative::js_boolean_t>);
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
    "branch_loop_unknown_call": "while (element.hasAttribute('x')) { element.unknown(); element.removeAttribute('x'); } return true;",
    "boolean-call": "return Boolean(element.getAttribute('x'));",
    "stringify": "return '' + element.getAttribute('x');",
    "loose-equality": "return element.getAttribute('x') == null;",
    "element-equality": "return element.getAttribute('x') === element;",
    "closest-equality": "return element.getAttribute('x') === element.closest('button');",
    "object-name": "return element.getAttribute('x' + {});",
    "object-value": "element.setAttribute('x', 'y' + {}); return true;",
    "return-null": "element.getAttribute('x'); return null;",
    "dynamic-name": "return element.getAttribute(element.getAttribute('x'));",
    "string-property": "return element.getAttribute('x').length;",
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
    "helper_completion_unsafe_middle": "function read(target) { if (target.getAttribute('x') === null) return false; if (target.getAttribute('x') === '') { target.unknown(); return true; } return false; } return read(element);",
    "helper_completion_unsafe_tail": "function read(target) { if (target.getAttribute('x') === null) return false; if (target.getAttribute('x') === '') return true; target.unknown(); return false; } return read(element);",
    "helper_completion_mixed_return": "function read(target) { if (target.getAttribute('x') === null) return false; if (target.getAttribute('x') === '') return 'text'; return false; } return read(element);",
    "helper_completion_optional_undefined": "function read(target) { if (target.getAttribute('x') === null) return null; if (target.getAttribute('x') === '') return undefined; return target.getAttribute('x'); } return read(element);",
    "helper_completion_exception": "function read(target) { try { if (target.getAttribute('x') === null) return false; if (target.getAttribute('x') === '') return true; return false; } catch (error) { return true; } } return read(element);",
    "helper_branch_unsafe_then": "function read(target) { if (target.hasAttribute('x')) { target.unknown(); } return target.getAttribute('x'); } return read(element);",
    "helper_branch_unsafe_else": "function read(target) { if (target.hasAttribute('x')) { return target.getAttribute('x'); } else { target.unknown(); return null; } } return read(element);",
    "helper_branch_loop_unknown_call": "function read(target) { while (target.hasAttribute('x')) { target.unknown(); target.removeAttribute('x'); } return target.getAttribute('x'); } return read(element);",
    "helper_branch_mixed_join": "function read(target) { if (target.hasAttribute('x')) return true; return 'text'; } return read(element);",
    "helper_branch_optional_undefined": "function read(target) { if (target.hasAttribute('x')) return target.getAttribute('x'); return undefined; } return read(element);",
    "helper_branch_borrowed_join": "function read(target) { let value; if (target.hasAttribute('x')) { value = target; } else { value = target.closest('button'); } return value.getAttribute('x'); } return read(element);",
    "helper_branch_callable_join": "function read(target) { function first(node) { return node.getAttribute('x'); } function second(node) { return node.getAttribute('y'); } let chosen; if (target.hasAttribute('x')) { chosen = first; } else { chosen = second; } return chosen(target); } return read(element);",
    "helper_branch_unused": "function read(target) { if (target.hasAttribute('x')) return true; return false; } return element.getAttribute('x');",
    "helper_branch_unknown_after_return": "function read(target) { if (target.hasAttribute('x')) return true; sideEffect(); return false; } return read(element);",
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
# Preserve every original matching source, including its return type and effects.
REGEXP_MATCHING = {
    name: (
        REGEXP_REFUSALS.pop(name),
        (
            "null"
            if name in {"regex_matching", "regex_mixed_matching", "regex_mixed_matching_first"}
            else "true"
        ),
    )
    for name in tuple(REGEXP_REFUSALS)
    if "matching" in name
}
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
    "host_duplicate_export": "const key = 'x'; var host_refusal = function host_refusal(element) { return element.getAttribute(key); }; host_refusal = host_refusal;",
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
