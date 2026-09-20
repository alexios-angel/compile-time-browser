"""Numeric-prefix cases for the DOM String gate, using its compiler/native harness."""

import json
from urllib.parse import quote_from_bytes

from CTNative.Browser import native_dom as dom
from CTNative.harness import run

# These common cases agree with Node and the existing Core/VM implementation.
# The inherited repeated-sign, huge-radix and overflow/trailing-text defects are
# separate oracle work; this table does not assert arbitrary-string compliance.
VALUES = (
    (None, "0"),
    ("", "0"),
    ("0", "0"),
    ("-0", "0"),
    ("01", "1"),
    ("+1", "1"),
    ("0x10", "16"),
    ("NaN", "NaN"),
    ("Infinity", "Infinity"),
    ("-Infinity", "-Infinity"),
    ("1e21", "1e+21"),
    ("1e+21", "1e+21"),
    ("1e-6", "0.000001"),
    ("0.000001", "0.000001"),
    (" \t\r\n", "0"),
    (" \t1\r\n", "1"),
    ("42.5", "42.5"),
    ("-42.5", "-42.5"),
    ("0b10", "2"),
    ("0o10", "8"),
    ("\u00a0+42\ufeff", "42"),
    ("garbage", "NaN"),
)
GET = "element.getAttribute('data-bs-config')"
CHANGE = "element.setAttribute('data-bs-config', '37'); element.removeAttribute('data-bs-config');"
CASES = {
    "numeric_text": f"return Number({GET}).toString();",
    "numeric_compare": f"const saved = {GET}; return saved === Number(saved).toString();",
    "numeric_saved_number": f"const saved = Number({GET}); {CHANGE} return saved.toString();",
    "numeric_saved_text": f"const saved = {GET}; {CHANGE} return Number(saved).toString();",
    "numeric_saved_compare": f"const saved = {GET}; {CHANGE} return saved === Number(saved).toString();",
    "numeric_repeated": f"""const saved = {GET};
      element.setAttribute('numeric-first', Number(saved).toString());
      {CHANGE}
      return Number(saved).toString();""",
}
BOOLEANS = {"numeric_compare", "numeric_saved_compare"}
MUTATIONS = {
    "numeric_saved_number",
    "numeric_saved_text",
    "numeric_saved_compare",
    "numeric_repeated",
}
SOURCES = tuple(
    (name, f"function {name}(element) {{ {body} }}\n", 1) for name, body in CASES.items()
)
RESULTS = [
    original == text if name in BOOLEANS else text for name in CASES for original, text in VALUES
]


def encoded(value):
    if isinstance(value, bool):
        return "true" if value else "false"
    data = value.encode()
    return f"{len(data)}:{data.hex()}"


EXPECTED = "".join(encoded(value) + "\n" for value in RESULTS)

ORACLE_HELPER = """
function numericObserve(entry, original, expected, trace, first) {
  const calls = [];
  const attributes = {};
  if (original !== null) attributes['data-bs-config'] = original;
  const element = {
    getAttribute(name) {
      calls.push('get:' + name);
      return attributes[name] === undefined ? null : attributes[name];
    },
    setAttribute(name, value) {
      calls.push('set:' + name + ':' + value);
      attributes[name] = value;
    },
    removeAttribute(name) {
      calls.push('remove:' + name);
      delete attributes[name];
    }
  };
  const result = entry(element);
  if (result !== expected || calls.join('|') !== trace)
    throw new Error('numeric result or source effect order');
  if (first !== null && attributes['numeric-first'] !== first)
    throw new Error('first Number call was not preserved');
  attributes['data-bs-config'] = 'later';
  if (result !== expected) throw new Error('numeric String borrowed the attribute');
  return result;
}
"""


def check_oracles(args):
    observations = []
    for name in CASES:
        for original, text in VALUES:
            trace = ["get:data-bs-config"]
            first = text if name == "numeric_repeated" else None
            if first is not None:
                trace.append("set:numeric-first:" + first)
            if name in MUTATIONS:
                trace += ["set:data-bs-config:37", "remove:data-bs-config"]
            values = (original, RESULTS[len(observations)], "|".join(trace), first)
            arguments = ", ".join(json.dumps(value) for value in values)
            observations.append(
                f"var numericObservation{len(observations):03} = numericObserve({name}, {arguments});"
            )
    source = "".join(source for _, source, _ in SOURCES) + ORACLE_HELPER + "\n".join(observations)
    node = args.work / "numeric-node.js"
    printed = []
    for i, value in enumerate(RESULTS):
        expression = f"numericObservation{i:03}"
        if not isinstance(value, bool):
            expression = (
                f'Buffer.from({expression}).length + ":" + '
                f'Buffer.from({expression}).toString("hex")'
            )
        printed.append(f"console.log({expression});")
    node.write_text(source + "\n" + "\n".join(printed))
    if run([args.node, str(node)]).stdout != EXPECTED:
        raise RuntimeError("numeric DOM observations disagree with Node")
    vm = args.work / "numeric-vm.js"
    vm.write_text(source)
    expected = "".join(
        f"numericObservation{i:03}="
        + (
            encoded(value)
            if isinstance(value, bool)
            else '"' + quote_from_bytes(value.encode()) + '"'
        )
        + "\n"
        for i, value in enumerate(RESULTS)
    )
    actual = run([args.reference, str(vm)]).stdout
    if actual != expected:
        raise RuntimeError(f"numeric DOM VM observations disagree with Node: {actual}")


def prepare(args):
    return [
        (name, ir, dict(contract, initial_intrinsics=["Number"]))
        for name, source, count in SOURCES
        for ir, contract in [dom.prepare(args, name, source, count)]
    ]


def cpp_string(text):
    # Fixed-width octal escapes preserve UTF-8 bytes without C++ hex continuation.
    return '"' + "".join(f"\\{byte:03o}" for byte in text.encode()) + '"'


CLIENT = """
#include <array>
#include <optional>
#include <string>
#include <string_view>
struct numeric_sample {
    std::optional<std::string> input;
    std::string_view text;
};
static const std::array<numeric_sample, @COUNT@> numeric_samples{{
@SAMPLES@
}};
""".replace("@COUNT@", str(len(VALUES))).replace(
    "@SAMPLES@",
    ",\n".join(
        "    {"
        + ("std::nullopt" if original is None else "std::string{" + cpp_string(original) + "}")
        + ", "
        + cpp_string(text)
        + "}"
        for original, text in VALUES
    ),
)

RUN = r"""
    {
        @SURVIVOR@
        {
            @SETUP@
            const auto node = doc.create_element(doc.atoms().intern("button"));
            const element_ref element{&doc, node};
            const auto state = doc.atoms().intern("data-bs-config");
            auto invoke = [&](element_ref value) { return @CALL@; };
            doc.log_writes(true);
            for (const auto & sample : numeric_samples) {
                if (sample.input) { assert(doc.set_attribute(node, state, *sample.input)); }
                else { assert(doc.remove_attribute(node, state)); }
                (void)doc.take_writes();
                const auto version = doc.version();
                const auto result = invoke(element);
                static_assert(std::is_same_v<std::remove_cv_t<decltype(result)>, @TYPE@>);
                assert(result == @EXPECTED@);
                const auto writes = doc.take_writes();
                @WRITES@
                assert(doc.set_attribute(node, state, "later"));
                assert(result == @EXPECTED@);
                @OBSERVE@
            }
            (void)doc.take_writes();
            bool rejected = false;
            try { (void)invoke(element_ref{}); }
            catch (const std::exception &) { rejected = true; }
            assert(rejected && doc.take_writes().empty());
        }
        @LIFETIME@
    }
"""


def client(name, entry, owned):
    boolean = name in BOOLEANS
    writes = "assert(writes.empty() && doc.version() == version);"
    if name in MUTATIONS:
        offset = int(name == "numeric_repeated")
        writes = f"""assert(writes.size() == {offset + 1});
                assert(writes[{offset}].node == node && writes[{offset}].name == state);
                assert(doc.version() == version + {offset + 2});
                assert(!doc.read().has_attribute(node, state));"""
        if offset:
            writes += """
                const auto first = doc.atoms().intern("numeric-first");
                assert(writes[0].node == node && writes[0].name == first);
                assert(doc.read().attribute_value(node, first) == sample.text);"""
    replacements = {
        "SURVIVOR": "" if boolean else "std::string survivor;",
        "SETUP": (
            f"{entry}_session session; auto & doc = session.document();"
            if owned
            else "atom_table atoms; document doc{atoms};"
        ),
        "CALL": "session.invoke(value)" if owned else entry + "(value)",
        "TYPE": "ctnative::js_boolean_t" if boolean else "ctnative::js_string",
        "EXPECTED": (
            "ctnative::js_boolean_t{sample.input && *sample.input == sample.text}"
            if boolean
            else "ctnative::js_string{sample.text}"
        ),
        "WRITES": writes,
        "OBSERVE": (
            'std::cout << (result ? "true\\n" : "false\\n");'
            if boolean
            else "observe(result.value()); survivor = result.value();"
        ),
        "LIFETIME": "" if boolean else 'assert(survivor == "NaN");',
    }
    result = RUN
    for key, value in replacements.items():
        result = result.replace("@" + key + "@", value)
    return result


REFUSALS = {
    "number_shadowed": f"const Number = element; return Number({GET}).toString();",
    "number_replaced": f"Number = element; return Number({GET}).toString();",
    "number_replaced_later": f"const saved = Number({GET}); Number = element; return saved.toString();",
    "number_prototype": f"Number.prototype = element; return Number({GET}).toString();",
    "number_to_string": f"Number.prototype.toString = element; return Number({GET}).toString();",
    "number_to_string_later": f"const saved = Number({GET}); Number.prototype.toString = element; return saved.toString();",
    "number_prototype_alias": f"const prototype = Number.prototype; prototype.toString = element; return Number({GET}).toString();",
    "number_to_string_accessor": f"Object.defineProperty(Number.prototype, 'toString', {{get() {{ return element; }}}}); return Number({GET}).toString();",
    "number_escape": f"element.saved = Number; return Number({GET}).toString();",
    "number_return_builtin": f"Number({GET}); return Number;",
    "number_escape_method": f"element.saved = Number({GET}).toString; return true;",
    "number_detached_method": f"const method = Number({GET}).toString; return method();",
    "number_object": "return Number(element).toString();",
    "number_coercible_object": "return Number({valueOf() { element.setAttribute('x', 'called'); return 1; }}).toString();",
    "number_missing": "element.getAttribute('x'); return Number().toString();",
    "number_extra": f"return Number({GET}, 'ignored').toString();",
    "number_receiver": f"return Number.call(element, {GET}).toString();",
    "number_construct": f"return new Number({GET}).toString();",
    "number_to_string_receiver": f"return Number.prototype.toString.call({GET});",
    "number_to_string_radix": f"return Number({GET}).toString(10);",
    "number_to_string_other_radix": f"return Number({GET}).toString(16);",
    "number_to_string_undefined": f"return Number({GET}).toString(undefined);",
    "number_to_string_extra": f"return Number({GET}).toString(10, 'ignored');",
    "number_reentry_before": f"sideEffect(); return Number({GET}).toString();",
    "number_reentry_between": f"const saved = Number({GET}); sideEffect(); return saved.toString();",
    "number_reentry_after": f"const saved = Number({GET}).toString(); sideEffect(); return saved;",
    "number_argument_reentry": f"return Number((sideEffect(), {GET})).toString();",
    "number_numeric_property_write": f"const saved = Number({GET}); saved.toString = element; return saved.toString();",
    "number_mixed_result": f"const saved = {GET}; if (saved === Number(saved).toString()) return Number(saved); return saved;",
}


def refusal_checks(args, prepared):
    cases = []
    for name, body in REFUSALS.items():
        ir, contract = dom.prepare(
            args, name, f"function invalid(element) {{ {body} }}\n", 1, entry_name="invalid"
        )
        cases.append((name, ir, dict(contract, initial_intrinsics=["Number"])))
    _, ir, contract = next(row for row in prepared if row[0] == "numeric_text")
    absent = dict(contract)
    del absent["initial_intrinsics"]
    cases += [
        ("number_no_opt_in", ir, absent),
        ("number_empty_opt_in", ir, dict(absent, initial_intrinsics=[])),
    ]
    # Declaring the entry itself as Number replaces the initial global before entry.
    ir, contract = dom.prepare(
        args,
        "number_entry_replaces_global",
        f"function Number(element) {{ return Number({GET}).toString(); }}\n",
        1,
    )
    cases.append(
        ("number_entry_replaces_global", ir, dict(contract, initial_intrinsics=["Number"]))
    )
    for name, ir, contract in cases:
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
                    raise RuntimeError(
                        f"{name}: missing intended numeric DOM refusal\n{diagnostic}"
                    )
    return 4 * len(cases)
