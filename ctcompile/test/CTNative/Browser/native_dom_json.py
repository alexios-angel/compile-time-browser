#!/usr/bin/env python3
"""Original M's protected body, JSON.parse(decodeURIComponent(t)), as native json_value.

The decode and parse calls become nested invokes on one success path; either
failure returns the original String. Native results are ctbrowser::json_value
from the shared public Core parser, compared against Node and the VM.
"""

import argparse
import json
import os
from pathlib import Path
import re
import shutil
from urllib.parse import quote_from_bytes

from CTNative.Browser import native_dom as dom
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
OBSERVE = """
function observe(result) { return typeof result + ':' + JSON.stringify(result); }
"""


def check_oracles(args):
    source = "".join(SOURCES.values()) + OBSERVE
    names = [f"jsonObservation{i:03}" for i in range(2 * len(CASES))]
    observations = "".join(
        f"var {names[2 * i + good]} = (function() {{ "
        f"const queries = []; const result = {name}({{hasAttribute(key) {{ "
        f"queries.push(key); return {str(bool(good)).lower()}; }}}}); "
        "if (queries.join('|') !== 'good') throw new Error('JSON prefix order'); "
        "return observe(result); })();\n"
        for i, name in enumerate(CASES)
        for good in (0, 1)
    )
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
#include <iostream>
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
        std::string operator()(double number) const { return number_to_string(number); }
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


def client(entry, owned):
    setup = (
        f"{entry}_session session; auto & doc = session.document();"
        if owned
        else "atom_table atoms; document doc{atoms};"
    )
    call = "session.invoke(element)" if owned else entry + "(element)"
    return f"""
    {{
        std::vector<json_value> survivors;
        {{
            {setup}
            const auto node = doc.create_element(doc.atoms().intern("button"));
            const element_ref element{{&doc, node}};
            const auto good = doc.atoms().intern("good");
            doc.log_writes(true);
            for (bool present : {{false, true}}) {{
                if (present) {{ assert(doc.set_attribute(node, good, "")); }}
                else {{ assert(doc.remove_attribute(node, good)); }}
                (void)doc.take_writes();
                const auto version = doc.version();
                auto result = {call};
                static_assert(std::is_same_v<decltype(result), json_value>);
                assert(doc.version() == version && doc.take_writes().empty());
                assert(doc.set_attribute(node, good, "later"));
                survivors.push_back(std::move(result));
            }}
        }}
        for (const auto & result : survivors) {{ observe(result); }}
    }}
"""


REFUSALS = {
    # A nullable saved read joins optional String with json_value: not yet proved.
    "json_nullable_join": "const saved = element.getAttribute('x'); if ('string' != typeof saved) return saved; "
    + BODY.replace("text", "saved"),
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
    expected = check_oracles(args)
    compilers = find_compilers()
    compilers[1] = args.clang
    includes, libraries = dom.link_options(args)
    intrinsics = ["decodeURIComponent", "JSON"]
    prepared = [
        (name, ir, dict(contract, initial_intrinsics=intrinsics))
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
                        initial_intrinsics=list(reversed(intrinsics)) if owned else intrinsics,
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
                    args, layouts[layout], namespace, optional_read=False, uri_call=True
                )
                if (
                    any(
                        token not in cpp
                        for token in ("ctbrowser::parse_json", "ctbrowser::json_value", "std::move")
                    )
                    or "catch" in cpp
                ):
                    raise RuntimeError(
                        f"{namespace}/{layout}: expected shared Core JSON parsing\n{cpp}"
                    )
                headers.update(re.findall(r"^#include[^\n]*", cpp, re.M))
                body = re.sub(r"^#include[^\n]*\n?", "", cpp, flags=re.M)
                bodies.append(f"namespace {namespace} {{\n{body}\n}}\n")
                runs.append(client(namespace + "::" + symbol, owned))
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
    _, ir, contract = prepared[0]
    for index, names in enumerate(
        (
            [],
            ["decodeURIComponent"],
            ["JSON"],
            ["JSON", "JSON", "decodeURIComponent"],
            ["JSON", "decodeURIComponent", "decodeURIComponent"],
        )
    ):
        for provider in ("ctbrowser-dom-v1", "ctbrowser-dom-session-v1"):
            for optimize in (False, True):
                diagnostic = dom.lower(
                    args,
                    ir,
                    dict(contract, provider=provider, initial_intrinsics=names),
                    f"json-premise-{index}-{provider}-{optimize}",
                    optimize=optimize,
                    success=False,
                )
                if "DOM" not in diagnostic and "initial_intrinsics" not in diagnostic:
                    raise RuntimeError(
                        f"json_premise: missing or duplicate intrinsic binding\n{diagnostic}"
                    )
                refusals += 1
    print(
        f"native DOM JSON: {len(CASES)} sources, {len(expected)} Node/VM observations, "
        f"8 GCC/Clang binaries, lifetime sanitizer, both providers/policies/layouts, {refusals} refusals"
    )


if __name__ == "__main__":
    main()
