"""Proved URI completions share the DOM String gate's source/native harness."""

import json
from urllib.parse import quote_from_bytes

from CTNative.Browser import native_dom as dom
from CTNative.Browser.native_dom_numbers import cpp_string, encoded
from CTNative.harness import run

SELECT = "const text = element.hasAttribute('good') ? 'A%20B%00%C3%A9' : '%E0%80%80';"
CASES = {
    "uri_text": (
        SELECT + "try { return decodeURIComponent(text); } catch (ignored) { return text; }",
        ("%E0%80%80", "A B\0é"),
    ),
    "uri_empty": (
        "const text = element.hasAttribute('good') ? '' : '%'; try { return decodeURIComponent(text); } catch (ignored) { return 'caught'; }",
        ("caught", ""),
    ),
    "uri_assignment": (
        SELECT
        + "let saved = 'before'; try { saved = decodeURIComponent(text); } catch (ignored) { return saved; } return saved;",
        ("before", "A B\0é"),
    ),
    "uri_pre_call": (
        SELECT
        + "let saved = 'before'; try { saved = 'inside'; saved = decodeURIComponent(text); } catch (ignored) { return saved; } return saved;",
        ("inside", "A B\0é"),
    ),
    "uri_prefix": (
        "if (element.hasAttribute('skip')) return 'early';"
        + SELECT
        + "try { return decodeURIComponent(text); } catch (ignored) { return text; }",
        ("%E0%80%80", "A B\0é"),
    ),
}
for index, (good, expected, bad) in enumerate(
    (
        ("%2500", "%00", "%2"),
        ("+%2f%3F", "+/?", "%xy"),
        ("%f0%9f%98%80", "😀", "%ED%A0%80"),
        ("%F4%8F%BF%BF", "\U0010ffff", "%F4%90%80%80"),
        ("é", "é", "%C0%AF"),
    )
):
    CASES[f"uri_bytes_{index}"] = (
        f"const text = element.hasAttribute('good') ? {json.dumps(good)} : {json.dumps(bad)};"
        "try { return decodeURIComponent(text); } catch (ignored) { return 'caught'; }",
        ("caught", expected),
    )
SOURCES = tuple(
    (name, f"function {name}(element) {{ {body} }}\n", 1) for name, (body, _) in CASES.items()
)
RESULTS = [
    "early" if name == "uri_prefix" and choice & 2 else values[choice & 1]
    for name, (_, values) in CASES.items()
    for choice in range(4)
]
EXPECTED = "".join(encoded(value) + "\n" for value in RESULTS)


def check_oracles(args):
    source = "".join(source for _, source, _ in SOURCES)
    for index, (name, choice) in enumerate((name, choice) for name in CASES for choice in range(4)):
        queries = (
            "skip"
            if name == "uri_prefix" and choice & 2
            else "skip|good" if name == "uri_prefix" else "good"
        )
        source += f"""
var uriObservation{index:03} = (function() {{
  const queries = [];
  const result = {name}({{hasAttribute(key) {{ queries.push(key); return key === 'good' ? {str(bool(choice & 1)).lower()} : {str(bool(choice & 2)).lower()}; }}}});
  if (result !== {json.dumps(RESULTS[index])} || queries.join('|') !== {json.dumps(queries)}) throw new Error('URI result or prefix order');
  return result;
}})();
"""
    node = args.work / "uri-node.js"
    node.write_text(
        source
        + "\n".join(
            f"console.log(Buffer.from(uriObservation{i:03}).length + ':' + Buffer.from(uriObservation{i:03}).toString('hex'));"
            for i in range(len(RESULTS))
        )
    )
    if run([args.node, str(node)]).stdout != EXPECTED:
        raise RuntimeError("URI observations disagree with Node")
    vm = args.work / "uri-vm.js"
    vm.write_text(source)
    expected = "".join(
        f'uriObservation{i:03}="{quote_from_bytes(value.encode(), safe='')}"\n'
        for i, value in enumerate(RESULTS)
    )
    if run([args.reference, str(vm)]).stdout != expected:
        raise RuntimeError("URI VM observations disagree with Node")


def prepare(args):
    return [
        (name, ir, dict(contract, initial_intrinsics=["decodeURIComponent"]))
        for name, source, count in SOURCES
        for ir, contract in [dom.prepare(args, name, source, count)]
    ]


def client(name, entry, owned):
    values = [
        "early" if name == "uri_prefix" and choice & 2 else CASES[name][1][choice & 1]
        for choice in range(4)
    ]
    expected = ", ".join(
        f"std::string{{{cpp_string(value)}, {len(value.encode())}}}" for value in values
    )
    setup = (
        f"{entry}_session session; auto & doc = session.document();"
        if owned
        else "atom_table atoms; document doc{atoms};"
    )
    call = "session.invoke(element)" if owned else entry + "(element)"
    return f"""
    {{
        const std::array<std::string, 4> expected{{{expected}}};
        std::string survivor;
        {{
            {setup}
            const auto node = doc.create_element(doc.atoms().intern("button"));
            const element_ref element{{&doc, node}};
            for (unsigned choice = 0; choice < 4; ++choice) {{
                for (unsigned bit = 0; bit < 2; ++bit) {{
                    const auto name = doc.atoms().intern(bit ? "skip" : "good");
                    if (choice & (1u << bit)) {{ assert(doc.set_attribute(node, name, "")); }}
                    else {{ assert(doc.remove_attribute(node, name)); }}
                }}
                const auto version = doc.version();
                const auto result = {call};
                static_assert(std::is_same_v<std::remove_cv_t<decltype(result)>, std::string>);
                assert(result == expected[choice] && doc.version() == version);
                assert(doc.set_attribute(node, doc.atoms().intern("later"), "changed"));
                assert(result == expected[choice]);
                observe(result);
                survivor = result;
            }}
        }}
        assert(survivor == expected.back());
    }}
"""


REFUSALS = {
    "uri_payload": "try { return decodeURIComponent(text); } catch (error) { return error; }",
    "uri_payload_read": "try { return decodeURIComponent(text); } catch (error) { return error.message; }",
    "uri_rethrow": "try { return decodeURIComponent(text); } catch (error) { throw error; }",
    "uri_store_payload": "try { return decodeURIComponent(text); } catch (error) { element.saved = error; return text; }",
    "uri_object": "try { return decodeURIComponent(element); } catch (error) { return text; }",
    "uri_nullable": "try { return decodeURIComponent(element.getAttribute('x')); } catch (error) { return text; }",
    "uri_extra": "try { return decodeURIComponent(text, text); } catch (error) { return text; }",
    "uri_receiver": "try { return decodeURIComponent.call(element, text); } catch (error) { return text; }",
    "uri_missing": "try { return decodeURIComponent(); } catch (error) { return text; }",
    "uri_protected_write": "try { element.setAttribute('bad name', 'x'); return decodeURIComponent(text); } catch (error) { return text; }",
    "uri_normal_write": "try { const value = decodeURIComponent(text); element.setAttribute('x', value); return value; } catch (error) { return text; }",
    "uri_caught_call": "try { return decodeURIComponent(text); } catch (error) { unknown(); return text; }",
    "uri_prefix_reentry": "unknown(); try { return decodeURIComponent(text); } catch (error) { return text; }",
    "uri_replaced": "decodeURIComponent = element; try { return decodeURIComponent(text); } catch (error) { return text; }",
    "uri_dead_replacement": "if (false) decodeURIComponent = element; try { return decodeURIComponent(text); } catch (error) { return text; }",
    "uri_sequential": "try { return decodeURIComponent(decodeURIComponent(text)); } catch (error) { return text; }",
    "uri_number_unauthorized": "Number(text); try { return decodeURIComponent(text); } catch (error) { return text; }",
}


def refusal_checks(args, prepared):
    cases = []
    for name, body in REFUSALS.items():
        ir, contract = dom.prepare(
            args, name, f"function invalid(element) {{ {SELECT} {body} }}\n", 1
        )
        cases.append((name, ir, dict(contract, initial_intrinsics=["decodeURIComponent"])))
    _, ir, contract = next(row for row in prepared if row[0] == "uri_text")
    for names in ([], ["Number"], ["decodeURIComponent", "decodeURIComponent"], ["JSON"]):
        cases.append(
            ("uri_premise_" + str(len(cases)), ir, dict(contract, initial_intrinsics=names))
        )
    ir, contract = dom.prepare(
        args,
        "uri_entry_replacement",
        "function decodeURIComponent(element) {"
        + SELECT
        + REFUSALS["uri_payload"].replace("return error;", "return text;")
        + "}",
        1,
    )
    cases.append(
        ("uri_entry_replacement", ir, dict(contract, initial_intrinsics=["decodeURIComponent"]))
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
                if "DOM" not in diagnostic and "initial_intrinsics" not in diagnostic:
                    raise RuntimeError(f"{name}: missing URI source refusal\n{diagnostic}")
    return 4 * len(cases)
