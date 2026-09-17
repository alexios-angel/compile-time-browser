#!/usr/bin/env python3
"""Original M's normalization and Config's JSON typeof/spread as native C++.

The decode and parse calls become nested invokes on one success path; either
failure returns the original String. Normalization owns ctbrowser::json_value;
typeof and guarded spreads are compared against Node and the VM.
"""

import argparse
import json
import os
from pathlib import Path
import re
import shutil
from urllib.parse import quote_from_bytes

from CTNative.Browser import native_dom as dom
from CTNative.Browser import native_dom_numbers as numbers
from CTNative.Browser import native_dom_strings as strings
from CTNative.harness import find_compilers, run
from Target.Cpp.harness import FLAGS

BODY = "try { return JSON.parse(decodeURIComponent(text)); } catch (ignored) { return text; }"
# name: (good text, bad text). Node and the VM supply the expected observations.
CASES = {
    "json_object": ("%7B%22a%22%3A1%2C%22b%22%3A%5Btrue%2Cnull%5D%7D", "%"),
    "json_invalid": ("%5B1%2C2%5D", "not%20json"),
    "json_scalars": ("%22x%22", "null"),
    "json_number_bool": ("42", "true"),
    "json_nested_failure": ("%5B%22%C3%A9%22%5D", "%5B1%2C"),
    "json_escaped": ("%22a%5Cu0000%5C%22%5C%5C%C3%A9%22", "%00"),
    "json_helper": ("%7B%22saved%22%3A%5B1%2C2%5D%7D", "not%20json"),
}
SOURCES = {
    name: f"function {name}(element) {{ const text = element.hasAttribute('good') ? "
    f"{json.dumps(good)} : {json.dumps(bad)}; {BODY} }}\n"
    for name, (good, bad) in CASES.items()
}
SOURCES["json_helper"] = SOURCES["json_helper"].replace(
    BODY, "function parse(text) { " + BODY + " } return parse(text);"
)
# Preserve the former refusal body, including its original earlier DOM observation.
SOURCES["json_not_element"] = (
    "function json_not_element(element) { const text = element.hasAttribute('good') ? '%7B%7D' : '%'; "
    "return !element; }\n"
)
BOOTSTRAP_M = """    function M(t) {
        if ("true" === t) return !0;
        if ("false" === t) return !1;
        if (t === Number(t).toString()) return Number(t);
        if ("" === t || "null" === t) return null;
        if ("string" != typeof t) return t;
        try {
            return JSON.parse(decodeURIComponent(t))
        } catch (e) {
            return t
        }
    }"""
BOOTSTRAP_F = """    function F(t) {
        return t.replace(/[A-Z]/g, t => `-${t.toLowerCase()}`)
    }"""
BOOTSTRAP_GET = "        getDataAttribute: (t, e) => M(t.getAttribute(`data-bs-${F(e)}`))"
BOOTSTRAP_SPREAD = '..."object" == typeof i ? i : {}'
BOOTSTRAP_SPREAD_LAST = '..."object" == typeof t ? t : {}'
ATTRIBUTE_HELPERS = BOOTSTRAP_M + "\n" + BOOTSTRAP_F + "\nconst H = {\n" + BOOTSTRAP_GET + "\n};\n"
PARSED_CONFIG = ATTRIBUTE_HELPERS + 'const i = H.getDataAttribute(element, "config"); '
CONFIG_SPREAD = "{" + BOOTSTRAP_SPREAD + "}"
SPREAD_CASES = {
    "json_config_spread": PARSED_CONFIG + f"return {CONFIG_SPREAD};",
    "json_spread_typeof": PARSED_CONFIG + f"return typeof {CONFIG_SPREAD};",
    "json_config_spread_chain": PARSED_CONFIG
    + 'const t = H.getDataAttribute(element, "later"); '
    + f"return {{{BOOTSTRAP_SPREAD}, {BOOTSTRAP_SPREAD_LAST}}};",
    "json_spread_target_write": "const text = element.hasAttribute('good') ? '%7B%7D' : '%'; "
    + PARSED_CONFIG
    + f"const result = {CONFIG_SPREAD}; result.saved = 1; return result;",
    "json_spread_target_alias_write": "const text = element.hasAttribute('good') ? '%7B%7D' : '%'; "
    + PARSED_CONFIG
    + f"const result = {CONFIG_SPREAD}; const alias = result; alias.saved = 1; return result;",
}
GET = "element.getAttribute('data-bs-config')"
NULLABLE_JOIN = (
    "const saved = element.getAttribute('x'); if ('string' != typeof saved) return saved; "
    + BODY.replace("text", "saved")
)
ATTRIBUTE_CASES = {
    "json_number_not": f"return !Number({GET});",
    "json_boolean_prefix": f"const text = {GET}; "
    + "if ('true' === text) return !0; if ('false' === text) return !1; "
    + "if ('string' != typeof text) return text; "
    + BODY,
    "json_bootstrap_m": BOOTSTRAP_M + f"\nreturn M({GET});",
    "json_bootstrap_attribute": ATTRIBUTE_HELPERS + 'return H.getDataAttribute(element, "config");',
    "json_attribute_typeof": ATTRIBUTE_HELPERS
    + 'return typeof H.getDataAttribute(element, "config");',
    "json_config_typeof_direct": ATTRIBUTE_HELPERS
    + 'return "object" == typeof H.getDataAttribute(element, "config");',
    # Preserve the former refusal body, including its earlier DOM observation.
    "json_config_typeof": "const text = element.hasAttribute('good') ? '%7B%7D' : '%'; "
    + ATTRIBUTE_HELPERS
    + 'const parsed = H.getDataAttribute(element, "config"); return "object" == typeof parsed;',
    # Preserve the former refusal body, including its earlier DOM observation.
    "json_nullable_join": "const text = element.hasAttribute('good') ? '%7B%7D' : '%'; "
    + NULLABLE_JOIN,
    **SPREAD_CASES,
}
SOURCES.update(
    (name, f"function {name}(element) {{ {body} }}\n") for name, body in ATTRIBUTE_CASES.items()
)
INPUTS = tuple(value for value, _ in numbers.VALUES) + (
    "true",
    "false",
    "null",
    "1",
    "1.0",
    "-0.0",
    "0e0",
    "00",
    "-00",
    "1e0",
    "1e+0",
    " -Infinity",
    "%7B%22saved%22%3A%5B1%2Ctrue%2Cnull%5D%7D",
    "%5B%22%C3%A9%22%5D",
    "%22a%5Cu0000%5C%22%5C%5C%C3%A9%22",
    "%",
    "not%20json",
    "%5B1%2C",
    "a\0b",
)
SPREAD_INPUTS = INPUTS + (
    "{}",
    "[]",
    '[10,{"saved":["é",null]},false]',
    '{"z":0,"2":"two","a":1,"10":"ten","2":"last","z":9}',
    '{"4294967295":"not-index","01":"leading","4294967294":"max",'
    '"0":"zero","-0":"negative","10":"ten","2":"two","__proto__":{"safe":true}}',
    '{"__proto__":{"nested":[{"saved":"original"},false]},'
    '"saved":{"deep":[1,2,3]},"__proto__":{"last":"owned"}}',
    '{"a\\u0000b":{"saved":"a\\u0000b"}}',
)
LATER_INPUT = (
    '{"z":"last","0":"zero","2":"second","tail":{"kept":[true,null]},'
    '"__proto__":{"safe":"later"}}'
)
OBSERVE = """
function observe(result) {
    if (typeof result === 'number') {
        if (result !== result) return 'number:NaN';
        if (result === Infinity) return 'number:Infinity';
        if (result === -Infinity) return 'number:-Infinity';
        if (result === 0 && 1 / result < 0) return 'number:-0';
    }
    return typeof result + ':' + JSON.stringify(result);
}
"""


def inputs_for(name):
    if name in SPREAD_CASES:
        return SPREAD_INPUTS
    return INPUTS if name in ATTRIBUTE_CASES else (None, "")


def check_oracles(args):
    source = "".join(SOURCES.values()) + OBSERVE
    names, observations = [], []
    for name in SOURCES:
        inputs = inputs_for(name)
        trace = "get:data-bs-config" if name in ATTRIBUTE_CASES else "has:good"
        if name == "json_config_spread_chain":
            trace += "|get:data-bs-later"
        if name in (
            "json_nullable_join",
            "json_config_typeof",
            "json_spread_target_write",
            "json_spread_target_alias_write",
        ):
            trace = "has:good|" + ("get:x" if name == "json_nullable_join" else trace)
        for value in inputs:
            label = f"jsonObservation{len(names):03}"
            names.append(label)
            observations.append(
                f"var {label} = (function() {{ "
                f"const queries = []; const result = {name}({{hasAttribute(key) {{ "
                f"queries.push('has:' + key); return {str(value is not None).lower()}; "
                "}, getAttribute(key) { queries.push('get:' + key); "
                f"return key === 'data-bs-later' ? {json.dumps(LATER_INPUT)} : {json.dumps(value)}; }} }}); "
                f"if (queries.join('|') !== {json.dumps(trace)}) throw new Error('JSON prefix order'); "
                "return observe(result); })();\n"
            )
    observations = "".join(observations)
    node = args.work / "json-node.js"
    node.write_text(source + observations + "".join(f"console.log({n});\n" for n in names))
    expected = run([args.node, str(node)]).stdout.splitlines()
    if len(expected) != len(names):
        raise RuntimeError("JSON observations were not completed by Node")
    vm = args.work / "json-vm.js"
    vm.write_text(source + observations)
    reference = run([args.reference, str(vm)]).stdout
    wanted = "".join(f'{n}="{quote(v)}"\n' for n, v in zip(names, expected))
    if reference != wanted:
        raise RuntimeError(f"JSON VM observations disagree with Node:\n{reference}\n{wanted}")
    return expected


def quote(value):
    return quote_from_bytes(value.encode(), safe="")


CLIENT = r"""
#include <cassert>
#include <cmath>
#include <iostream>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>
#include <ctbrowser/core/json.hpp>
#include <ctbrowser/core/number_format.hpp>

using namespace ctbrowser;

static std::string quote_json(std::string_view text) {
    std::string out = "\"";
    for (const char ch : text) {
        switch (ch) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (const auto byte = static_cast<unsigned char>(ch); byte < 32) {
                constexpr char hex[] = "0123456789abcdef";
                out += "\\u00";
                out += hex[byte >> 4];
                out += hex[byte & 15];
            } else { out += ch; }
        }
    }
    return out + '"';
}

static std::string stringify(const json_value & value) {
    struct visitor {
        std::string operator()(std::nullptr_t) const { return "null"; }
        std::string operator()(bool flag) const { return flag ? "true" : "false"; }
        std::string operator()(double number) const {
            return std::isfinite(number) ? number_to_string(number) : "null";
        }
        std::string operator()(const std::string & text) const { return quote_json(text); }
        std::string operator()(const json_value::array & items) const {
            std::string out = "[";
            for (const auto & item : items) { out += (out.size() > 1 ? "," : "") + stringify(item); }
            return out + "]";
        }
        std::string operator()(const json_value::object & members) const {
            std::string out = "{";
            for (const auto & member : members) {
                out += (out.size() > 1 ? "," : "") + quote_json(member.key) + ":" + stringify(member.value);
            }
            return out + "}";
        }
    };
    return std::visit(visitor{}, value.data);
}

static void observe(const json_value & value) {
    if (const auto * number = std::get_if<double>(&value.data)) {
        if (std::isnan(*number)) { std::cout << "number:NaN\n"; return; }
        if (std::isinf(*number)) {
            std::cout << (*number < 0 ? "number:-Infinity\n" : "number:Infinity\n"); return;
        }
        if (*number == 0 && std::signbit(*number)) { std::cout << "number:-0\n"; return; }
    }
    const char * type = std::holds_alternative<bool>(value.data)     ? "boolean"
                        : std::holds_alternative<double>(value.data) ? "number"
                        : std::holds_alternative<std::string>(value.data) ? "string"
                                                                          : "object";
    std::cout << type << ':' << stringify(value) << '\n';
}

int main() {
@RUNS@
    return 0;
}
"""


def client(name, entry, owned):
    setup = (
        f"{entry}_session session; auto & doc = session.document();"
        if owned
        else "atom_table atoms; document doc{atoms};"
    )
    call = "session.invoke(element)" if owned else entry + "(element)"
    inputs = inputs_for(name)
    samples = ", ".join(
        (
            "std::nullopt"
            if value is None
            else f"std::string{{{numbers.cpp_string(value)}, {len(value.encode())}}}"
        )
        for value in inputs
    )
    attribute = "data-bs-config" if name in ATTRIBUTE_CASES else "good"
    earlier_read = ""
    if name == "json_nullable_join":
        attribute = "x"
    if name in (
        "json_nullable_join",
        "json_config_typeof",
        "json_spread_target_write",
        "json_spread_target_alias_write",
    ):
        earlier_read = """
                const auto good = doc.atoms().intern("good");
                if (input) { assert(doc.set_attribute(node, good, "")); }
                else { assert(doc.remove_attribute(node, good)); }
"""
    later_setup = later_change = ""
    if name == "json_config_spread_chain":
        later_setup = (
            'const auto later = doc.atoms().intern("data-bs-later"); '
            f"assert(doc.set_attribute(node, later, {numbers.cpp_string(LATER_INPUT)}));"
        )
        later_change = 'assert(doc.set_attribute(node, later, "later"));'
    result_type = {
        "json_number_not": "bool",
        "json_not_element": "bool",
        "json_attribute_typeof": "std::string",
        "json_config_typeof_direct": "bool",
        "json_config_typeof": "bool",
        "json_spread_typeof": "std::string",
    }.get(name, "json_value")
    return f"""
    {{
        std::vector<json_value> survivors;
        {{
            {setup}
            const auto node = doc.create_element(doc.atoms().intern("button"));
            const element_ref element{{&doc, node}};
            const auto state = doc.atoms().intern("{attribute}");
            doc.log_writes(true);
            for (const auto & input : std::vector<std::optional<std::string>>{{{samples}}}) {{
                if (input) {{ assert(doc.set_attribute(node, state, *input)); }}
                else {{ assert(doc.remove_attribute(node, state)); }}
                {earlier_read}
                {later_setup}
                (void)doc.take_writes();
                const auto version = doc.version();
                auto result = {call};
                static_assert(std::is_same_v<decltype(result), {result_type}>);
                assert(doc.version() == version && doc.take_writes().empty());
                assert(doc.set_attribute(node, state, "later"));
                {later_change}
                survivors.emplace_back(std::move(result));
            }}
        }}
        for (const auto & result : survivors) {{ observe(result); }}
    }}
"""


REFUSALS = {
    "json_spread_branch_cell_write": PARSED_CONFIG
    + "let t; if (element.hasAttribute('good')) t = H.getDataAttribute(element, 'later'); "
    + f"return {{{BOOTSTRAP_SPREAD}, {BOOTSTRAP_SPREAD_LAST}}};",
    "json_spread_late_cell_write": PARSED_CONFIG
    + f"let t; const result = {{{BOOTSTRAP_SPREAD}, {BOOTSTRAP_SPREAD_LAST}}}; "
    + "t = H.getDataAttribute(element, 'later'); return result;",
    "json_spread_members": PARSED_CONFIG + f"const result = {CONFIG_SPREAD}; return result.saved;",
    "json_spread_source_write": PARSED_CONFIG
    + f"const result = {CONFIG_SPREAD}; i.saved = 1; return result;",
    "json_spread_source_alias_write": PARSED_CONFIG
    + f"const alias = i.saved; const result = {CONFIG_SPREAD}; alias.changed = 1; return result;",
    "json_spread_branch_target_write": PARSED_CONFIG
    + f"const result = {CONFIG_SPREAD}; "
    + "const target = element.hasAttribute('good') ? result : {}; target.saved = 1; return result;",
    "json_spread_source_identity": PARSED_CONFIG
    + f"const result = {CONFIG_SPREAD}; return result === i;",
    "json_spread_nested_identity": PARSED_CONFIG
    + f"const result = {CONFIG_SPREAD}; return result.saved === i.saved;",
    "json_spread_source_capture": PARSED_CONFIG
    + f"const result = {CONFIG_SPREAD}; return () => i;",
    "json_spread_target_capture": PARSED_CONFIG
    + f"const result = {CONFIG_SPREAD}; return () => result;",
    "json_spread_unknown_source": "return {...element};",
    "json_spread_unguarded": PARSED_CONFIG + "return {...i};",
    "json_spread_string": PARSED_CONFIG + 'return {..."string" == typeof i ? i : {}};',
    "json_spread_unknown_target": PARSED_CONFIG + "return Object.assign(element, i);",
    "json_typeof_members": ATTRIBUTE_HELPERS
    + 'const parsed = H.getDataAttribute(element, "config"); '
    + 'if ("object" == typeof parsed) return parsed.saved; return null;',
    "json_typeof_replaced": ATTRIBUTE_HELPERS
    + 'JSON.parse = element; return typeof H.getDataAttribute(element, "config");',
    "json_matching_key": ATTRIBUTE_HELPERS + 'return H.getDataAttribute(element, "Config");',
    "json_live_key": ATTRIBUTE_HELPERS
    + 'return H.getDataAttribute(element, element.getAttribute("key"));',
    "json_nullable_sibling": NULLABLE_JOIN.replace(
        "typeof saved", "typeof element.getAttribute('other')"
    ),
    "json_number_shadowed": "const Number = element; return !Number(text);",
    "json_number_replaced": "Number = element; return !Number(text);",
    "json_number_string_replaced": "Number.prototype.toString = element; return Number(text).toString();",
    "json_payload": BODY.replace("return text;", "return ignored;"),
    "json_payload_read": BODY.replace("return text;", "return ignored.message;"),
    "json_reversed": "try { return decodeURIComponent(JSON.parse(text)); } catch (ignored) { return text; }",
    "json_stringify": "try { return JSON.stringify(decodeURIComponent(text)); } catch (ignored) { return text; }",
    "json_replaced": "JSON.parse = element; " + BODY,
    "json_replaced_object": "JSON = element; " + BODY,
    "json_shadowed": "const JSON = element; " + BODY,
    "json_dead_replacement": "if (false) JSON.parse = element; " + BODY,
    "json_reviver": BODY.replace("decodeURIComponent(text))", "decodeURIComponent(text), element)"),
    "json_receiver": BODY.replace("JSON.parse(", "JSON.parse.call(element, "),
    "json_third_call": BODY.replace(
        "decodeURIComponent(text)", "decodeURIComponent(decodeURIComponent(text))"
    ),
    "json_unprotected_lookup": "const parse = JSON.parse; try { return parse(decodeURIComponent(text)); } catch (ignored) { return text; }",
}


def main():
    parser = argparse.ArgumentParser()
    for name in ("translate", "opt", "clang", "node", "reference", "build", "include", "work"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--nm", default=shutil.which("nm"))
    args = parser.parse_args()
    args.work = args.work.resolve()
    args.work.mkdir(parents=True, exist_ok=True)
    if not args.nm:
        raise RuntimeError("native DOM JSON gate requires nm")
    vendor = Path(__file__).resolve().parents[4] / "ctbrowser/vendor/bootstrap/bootstrap.bundle.js"
    if any(
        source not in vendor.read_text()
        for source in (
            BOOTSTRAP_M,
            BOOTSTRAP_F,
            BOOTSTRAP_GET,
            BOOTSTRAP_SPREAD,
            BOOTSTRAP_SPREAD_LAST,
        )
    ):
        raise RuntimeError("Bootstrap M/F/getDataAttribute/Config spread source pin changed")
    expected = check_oracles(args)
    compilers = find_compilers()
    compilers[1] = args.clang
    includes, libraries = dom.link_options(args)
    intrinsics = ["decodeURIComponent", "JSON", "Number"]
    source_intrinsics = {
        "json_number_not": ["Number"],
        "json_not_element": intrinsics,
        "json_bootstrap_m": intrinsics,
        "json_bootstrap_attribute": intrinsics,
        "json_attribute_typeof": intrinsics,
        "json_config_typeof_direct": intrinsics,
        "json_config_typeof": intrinsics,
        **dict.fromkeys(SPREAD_CASES, intrinsics),
    }
    prepared = [
        (
            name,
            ir,
            dict(
                contract,
                initial_intrinsics=source_intrinsics.get(name, intrinsics[:2]),
            ),
        )
        for name, source in SOURCES.items()
        for ir, contract in [dom.prepare(args, name, source, 1, entry_name=name)]
    ]
    for optimize in (False, True):
        modules = []
        for provider in ("ctbrowser-dom-v1", "ctbrowser-dom-session-v1"):
            owned = "session" in provider
            for name, ir, contract in prepared:
                label = f"{name}-{owned}-{optimize}"
                native = dom.lower(
                    args,
                    ir,
                    dict(
                        contract,
                        provider=provider,
                        initial_intrinsics=(
                            list(reversed(contract["initial_intrinsics"]))
                            if owned
                            else contract["initial_intrinsics"]
                        ),
                    ),
                    label,
                    optimize=optimize,
                )
                deduced = args.work / f"{label}.deduced.mlir"
                run([args.opt, str(native), "--ctnative-print-deduced", "-o", str(deduced)])
                modules.append((name, owned, {"explicit": native, "deduced": deduced}))
        for layout in ("explicit", "deduced"):
            headers, bodies, runs = set(), [], []
            for name, owned, layouts in modules:
                namespace = f"{name}_{'session' if owned else 'free'}"
                cpp, symbol = strings.emitted(
                    args,
                    layouts[layout],
                    namespace,
                    optional_read=name in ATTRIBUTE_CASES,
                    uri_call=name not in ("json_number_not", "json_not_element"),
                )
                if name not in ("json_number_not", "json_not_element") and (
                    any(
                        token not in cpp
                        for token in ("ctbrowser::parse_json", "ctbrowser::json_value", "std::move")
                    )
                    or "catch" in cpp
                ):
                    raise RuntimeError(
                        f"{namespace}/{layout}: expected shared Core JSON parsing\n{cpp}"
                    )
                headers.update(re.findall(r"^#(?:include|define CTNATIVE_)[^\n]*", cpp, re.M))
                body = re.sub(r"^#(?:include|define CTNATIVE_)[^\n]*\n?", "", cpp, flags=re.M)
                bodies.append(f"namespace {namespace} {{\n{body}\n}}\n")
                runs.append(client(name, namespace + "::" + symbol, owned))
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
                    raise RuntimeError("native JSON entries link Script/AOT")
                if run([str(binary)]).stdout.splitlines() != expected * 2:
                    raise RuntimeError(
                        f"{path.name}/{index}: native JSON observations disagree with Node"
                    )
            if optimize and layout == "explicit":
                sanitized = path.with_suffix(".sanitized")
                run(
                    [
                        compilers[1],
                        *FLAGS,
                        "-O1",
                        "-g",
                        "-fno-omit-frame-pointer",
                        "-fsanitize=address,undefined",
                        "-fsanitize-address-use-after-scope",
                        *includes,
                        str(path),
                        *libraries,
                        "-o",
                        str(sanitized),
                    ]
                )
                result = run(
                    [str(sanitized)],
                    environment=dict(
                        os.environ,
                        ASAN_OPTIONS="detect_stack_use_after_return=1:detect_leaks=1",
                        UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1",
                    ),
                )
                if result.stdout.splitlines() != expected * 2 or result.stderr:
                    raise RuntimeError("native JSON lifetime sanitizer check failed")
    refusals = 0
    for name, body in REFUSALS.items():
        ir, contract = dom.prepare(
            args,
            name,
            "function invalid(element) { const text = element.hasAttribute('good') ? '%7B%7D' : '%'; "
            + body
            + " }\n",
            1,
            entry_name="invalid",
        )
        for provider in ("ctbrowser-dom-v1", "ctbrowser-dom-session-v1"):
            for optimize in (False, True):
                diagnostic = dom.lower(
                    args,
                    ir,
                    dict(contract, provider=provider, initial_intrinsics=intrinsics),
                    f"{name}-{provider}-{optimize}",
                    optimize=optimize,
                    success=False,
                )
                if "DOM" not in diagnostic:
                    raise RuntimeError(f"{name}: missing JSON source refusal\n{diagnostic}")
                refusals += 1
    premises = {
        "json_object": (
            None,
            [],
            ["decodeURIComponent"],
            ["JSON"],
            ["JSON", "JSON", "decodeURIComponent"],
            ["JSON", "decodeURIComponent", "decodeURIComponent"],
        ),
        "json_number_not": (None, [], ["decodeURIComponent", "JSON"], ["Number", "Number"]),
        "json_bootstrap_m": (
            None,
            [],
            ["decodeURIComponent", "JSON"],
            ["Number", "JSON"],
            ["Number", "decodeURIComponent"],
            ["Number", "Number", "decodeURIComponent", "JSON"],
        ),
    }
    premises["json_config_typeof_direct"] = premises["json_bootstrap_m"]
    premises["json_config_spread"] = premises["json_bootstrap_m"]
    for name, variants in premises.items():
        _, ir, contract = next(row for row in prepared if row[0] == name)
        for index, names in enumerate(variants):
            premise = dict(contract)
            if names is None:
                del premise["initial_intrinsics"]
            else:
                premise["initial_intrinsics"] = names
            for provider in ("ctbrowser-dom-v1", "ctbrowser-dom-session-v1"):
                for optimize in (False, True):
                    diagnostic = dom.lower(
                        args,
                        ir,
                        dict(premise, provider=provider),
                        f"{name}-premise-{index}-{provider}-{optimize}",
                        optimize=optimize,
                        success=False,
                    )
                    if "DOM" not in diagnostic and "initial_intrinsics" not in diagnostic:
                        raise RuntimeError(
                            f"{name}: missing or duplicate intrinsic binding\n{diagnostic}"
                        )
                    refusals += 1
    print(
        f"native DOM JSON: {len(SOURCES)} sources, {len(expected)} Node/VM observations, "
        f"8 GCC/Clang binaries, lifetime sanitizer, both providers/policies/layouts, {refusals} refusals"
    )


if __name__ == "__main__":
    main()
