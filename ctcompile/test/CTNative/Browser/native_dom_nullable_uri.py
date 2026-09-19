"""Saved nullable attribute guards before native URI success/failure continuations."""

import json
import re
from urllib.parse import quote_from_bytes

from CTNative.Browser import native_dom as dom
from CTNative.Browser import native_dom_numbers as numbers
from CTNative.harness import run

READ = "const t = element.getAttribute('data-bs-config');"
GUARD = "if ('string' != typeof t) return t;"
DECODE = "try { return decodeURIComponent(t); } catch (ignored) { return t; }"
HELPER = "function decode(t) { " + GUARD + DECODE + " } "
CASES = {
    "nullable_uri_original": READ + GUARD + DECODE,
    "nullable_uri_reverse": READ + "if (typeof t != 'string') return t;" + DECODE,
    "nullable_uri_strict": READ + "if ('string' !== typeof t) return t;" + DECODE,
    "nullable_uri_reverse_strict": READ + "if (typeof t !== 'string') return t;" + DECODE,
    "nullable_uri_equal_arm": READ + "if ('string' === typeof t) {" + DECODE + "} return t;",
    "nullable_uri_alias": READ
    + "const saved = t; if ('string' != typeof saved) return saved;"
    + DECODE.replace("return t;", "return saved;"),
    "nullable_uri_saved": READ + numbers.CHANGE + GUARD + DECODE,
    "nullable_uri_assignment": READ
    + GUARD
    + "let saved = 'before'; try { saved = t; saved = decodeURIComponent(t); } catch (ignored) { return saved; } return saved;",
    # Original M shape: the guard and the handler live in a local helper.
    "nullable_uri_helper_branch_call": READ
    + "if (t === null) return t; "
    + HELPER
    + "return decode(t);",
    "nullable_uri_helper": HELPER + "return decode(element.getAttribute('data-bs-config'));",
    "nullable_uri_helper_saved": READ + HELPER + "return decode(t);",
    "nullable_uri_helper_arrow": "const decode = t => { "
    + GUARD
    + DECODE
    + " }; return decode(element.getAttribute('data-bs-config'));",
}
VALUES = (
    (None, None),
    ("", ""),
    ("%", "%"),
    ("A%20B%00%C3%A9", "A B\0é"),
    ("%E0%80%80", "%E0%80%80"),
    ("%ED%A0%80", "%ED%A0%80"),
    ("%2500", "%00"),
    ("é", "é"),
    ("%f0%9f%98%80", "😀"),
    ("a\0b", "a\0b"),
)
SOURCES = tuple(
    (name, f"function {name}(element) {{ {body} }}\n", 1) for name, body in CASES.items()
)
RESULTS = [result for _ in CASES for _, result in VALUES]
EXPECTED = "".join(
    ("null" if value is None else numbers.encoded(value)) + "\n" for value in RESULTS
)


def check_oracles(args):
    observations = []
    for name in CASES:
        trace = "get:data-bs-config"
        if name == "nullable_uri_saved":
            trace += "|set:data-bs-config:37|remove:data-bs-config"
        for original, expected in VALUES:
            arguments = ", ".join(json.dumps(value) for value in (original, expected, trace, None))
            observations.append(
                f"var nullableUriObservation{len(observations):03} = numericObserve({name}, {arguments});"
            )
    source = (
        "".join(source for _, source, _ in SOURCES)
        + numbers.ORACLE_HELPER
        + "\n".join(observations)
    )
    node = args.work / "nullable-uri-node.js"
    node.write_text(
        source
        + "\n"
        + "\n".join(
            f"console.log(nullableUriObservation{i:03} === null ? 'null' : "
            f"Buffer.from(nullableUriObservation{i:03}).length + ':' + Buffer.from(nullableUriObservation{i:03}).toString('hex'));"
            for i in range(len(RESULTS))
        )
    )
    if run([args.node, str(node)]).stdout != EXPECTED:
        raise RuntimeError("nullable URI observations disagree with Node")
    vm = args.work / "nullable-uri-vm.js"
    vm.write_text(source)
    expected = "".join(
        f"nullableUriObservation{i:03}="
        + ("null" if value is None else '"' + quote_from_bytes(value.encode(), safe="") + '"')
        + "\n"
        for i, value in enumerate(RESULTS)
    )
    if run([args.reference, str(vm)]).stdout != expected:
        raise RuntimeError("nullable URI VM observations disagree with Node")


def prepare(args):
    return [
        (name, ir, dict(contract, initial_intrinsics=["decodeURIComponent"]))
        for name, source, count in SOURCES
        for ir, contract in [dom.prepare(args, name, source, count, entry_name=name)]
    ]


def optional_string(value):
    return (
        "std::nullopt"
        if value is None
        else f"std::string{{{numbers.cpp_string(value)}, {len(value.encode())}}}"
    )


CLIENT = """
#include <vector>
struct nullable_uri_sample {
    std::optional<std::string> input, expected;
};
static const std::array<nullable_uri_sample, @COUNT@> nullable_uri_samples{{
@VALUES@
}};
""".replace("@COUNT@", str(len(VALUES))).replace(
    "@VALUES@",
    ",\n".join(
        "    {" + optional_string(original) + ", " + optional_string(expected) + "}"
        for original, expected in VALUES
    ),
)


def client(name, entry, owned):
    setup = (
        f"{entry}_session session; auto & doc = session.document();"
        if owned
        else "atom_table atoms; document doc{atoms};"
    )
    call = "session.invoke(value)" if owned else entry + "(value)"
    writes = "assert(writes.empty() && doc.version() == version);"
    if name == "nullable_uri_saved":
        writes = """assert(writes.size() == 1 && doc.version() == version + 2);
                assert(writes[0].node == node && writes[0].name == state);
                assert(!doc.read().has_attribute(node, state));"""
    return f"""
    {{
        std::vector<std::optional<std::string>> survivors;
        {{
            {setup}
            const auto node = doc.create_element(doc.atoms().intern("button"));
            const element_ref element{{&doc, node}};
            const auto state = doc.atoms().intern("data-bs-config");
            auto invoke = [&](element_ref value) {{ return {call}; }};
            doc.log_writes(true);
            for (const auto & sample : nullable_uri_samples) {{
                if (sample.input) {{ assert(doc.set_attribute(node, state, *sample.input)); }}
                else {{ assert(doc.remove_attribute(node, state)); }}
                (void)doc.take_writes();
                const auto version = doc.version();
                const auto result = invoke(element);
                static_assert(std::is_same_v<std::remove_cv_t<decltype(result)>,
                                             std::optional<std::string>>);
                assert(result == sample.expected);
                const auto writes = doc.take_writes();
                {writes}
                assert(doc.set_attribute(node, state, "later"));
                assert(result == sample.expected);
                observe(result);
                survivors.push_back(result);
            }}
            (void)doc.take_writes();
            bool rejected = false;
            try {{ (void)invoke(element_ref{{}}); }}
            catch (const std::exception &) {{ rejected = true; }}
            assert(rejected && doc.take_writes().empty());
        }}
        assert(survivors.size() == nullable_uri_samples.size());
        for (std::size_t i = 0; i < survivors.size(); ++i) {{
            assert(survivors[i] == nullable_uri_samples[i].expected);
        }}
    }}
"""


REFUSALS = {
    "nullable_uri_unguarded": READ + DECODE,
    "nullable_uri_opposite": READ + "if ('string' === typeof t) return t;" + DECODE,
    "nullable_uri_sibling": READ
    + "const other = element.getAttribute('other'); if ('string' != typeof other) return other;"
    + DECODE,
    "nullable_uri_after_join": READ
    + "if ('string' === typeof t) { element.hasAttribute('present'); }"
    + DECODE,
    "nullable_uri_different_snapshot": "if ('string' != typeof element.getAttribute('data-bs-config')) return null;"
    + READ
    + DECODE,
    "nullable_uri_reassigned": READ.replace("const t", "let t")
    + GUARD
    + "t = element.getAttribute('other');"
    + DECODE,
    "nullable_uri_fake_predicate": READ + "if ('string' != 'string') return t;" + DECODE,
    "nullable_uri_replaced": READ + GUARD + "decodeURIComponent = element;" + DECODE,
    "nullable_uri_dead_replacement": READ
    + GUARD
    + "if (false) decodeURIComponent = element;"
    + DECODE,
    "nullable_uri_reentry": READ + GUARD + "unknown();" + DECODE,
    "nullable_uri_payload": READ + GUARD + DECODE.replace("return t;", "return ignored;"),
    "nullable_uri_payload_read": READ
    + GUARD
    + DECODE.replace("return t;", "return ignored.message;"),
    "nullable_uri_helper_payload": HELPER.replace("return t; }", "return ignored; }")
    + "return decode(element.getAttribute('data-bs-config'));",
    "nullable_uri_helper_unguarded": "function decode(t) { "
    + DECODE
    + " } return decode(element.getAttribute('data-bs-config'));",
}


def refusal_checks(args, prepared):
    cases = [
        (name, ir, dict(contract, initial_intrinsics=["decodeURIComponent"]))
        for name, body in REFUSALS.items()
        for ir, contract in [
            dom.prepare(
                args, name, f"function invalid(element) {{ {body} }}\n", 1, entry_name="invalid"
            )
        ]
    ]
    _, original, contract = next(row for row in prepared if row[0] == "nullable_uri_original")
    text = original.read_text()
    negated = re.findall(r"(%\w+) = ctjs\.unary not (%\w+)", text)
    if len(negated) != 1:
        raise RuntimeError("nullable URI predicate mutation anchor changed")
    result, operand = negated[0]
    text, count = re.subn(
        r"ctjs\.truthy " + re.escape(result) + r"\b", "ctjs.truthy " + operand, text
    )
    if count != 1:
        raise RuntimeError("nullable URI branch predicate anchor changed")
    text, count = re.subn(
        r"\bmodule( attributes)? \{",
        lambda match: 'module attributes {ctnative.host_proved = true, ctnative.dom_entry = "trusted"'
        + (", " if match[1] else "} {"),
        text,
        count=1,
    )
    if count != 1:
        raise RuntimeError("nullable URI forged-report mutation anchor changed")
    forged = args.work / "nullable-uri-forged-predicate.mlir"
    forged.write_text(text)
    cases.append(
        (
            "nullable_uri_forged_predicate",
            forged,
            dict(contract, module_sha256=dom.fingerprint(args.opt, forged)),
        )
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
                    raise RuntimeError(f"{name}: missing nullable URI refusal\n{diagnostic}")
    return 4 * len(cases)
