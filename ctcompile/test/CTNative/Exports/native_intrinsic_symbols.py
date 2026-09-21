#!/usr/bin/env python3
"""Check Symbol-only native exports against Node/VM without a DOM input or runtime."""

import argparse
import json
from pathlib import Path
import re
import shutil

from CTNative.Browser import native_dom as dom
from CTNative.HostContract.contract import fingerprint
from CTNative.harness import find_compilers, run
from Target.Cpp.harness import FLAGS

STATE = """function state() {
  let key = Symbol.iterator;
  const saved = key;
  for (let i = 0; i < 3; i++) {
    key = key === Symbol.iterator ? Symbol.hasInstance : Symbol.iterator;
  }
  return saved === Symbol.iterator ? key : Symbol.toStringTag;
}
"""
OBSERVE = """function observe() {
  const key = Symbol.hasInstance;
  return typeof key +
    (key === Symbol.hasInstance ? ':same' : ':wrong') +
    (key !== Symbol.iterator ? ':different' : ':wrong') +
    (key == Symbol['hasInstance'] ? ':loose' : ':wrong') +
    (key ? ':truthy' : ':wrong') +
    (!key ? ':wrong' : ':false');
}
"""
TRANSCRIPT = "symbol:same:different:loose:truthy:false"
CASES = {
    "state": (
        STATE,
        """
    static_assert(std::is_same_v<decltype(@ENTRY@()), ctnative::js_symbol_t>);
    const auto saved = @ENTRY@();
    assert(saved == ctnative::Symbol.hasInstance);
    assert(@ENTRY@() == saved);
    assert(saved != ctnative::Symbol.iterator);
    std::cout << "true\\n";
""",
        "true\n",
    ),
    "observe": (
        OBSERVE,
        f"""
    static_assert(std::is_same_v<decltype(@ENTRY@()), ctnative::js_string>);
    const auto saved = @ENTRY@();
    assert(saved.value() == "{TRANSCRIPT}");
    assert(@ENTRY@() == saved);
    std::cout << saved.value() << '\\n';
""",
        TRANSCRIPT + "\n",
    ),
}
REFUSALS = {
    "global-replacement": "Symbol=0; return Symbol.iterator;",
    "member-replacement": "Symbol.hasInstance=Symbol.iterator; return false;",
    "alias-mutation": "const saved=Symbol; saved.hasInstance=Symbol.iterator; return false;",
    "escape-constructor": "return Symbol;",
    "constructor-typeof": "return typeof Symbol;",
    "dynamic-key": "return Symbol[typeof Symbol.iterator];",
    "unknown-key": "return Symbol.unknown;",
    "fresh": "return typeof Symbol();",
    "registry": "return typeof Symbol.for('x');",
    "description": "return Symbol.iterator.description;",
    "method": "return Symbol.iterator.toString();",
    "object": "return {};",
    "symbol-field": "const item={}; item[Symbol.iterator]=1; return false;",
    "number": "return +Symbol.iterator;",
    "concat": "return '' + Symbol.iterator;",
    "mixed-compare": "return Symbol.iterator === 'iterator';",
    "mixed-join": "return Symbol.iterator ? Symbol.iterator : 0;",
    "instanceof": "return Symbol.iterator instanceof Symbol;",
    "external-call": "unknown(); return Symbol.iterator;",
}
FORBIDDEN = re.compile(
    r"CTNATIVE_DOM|ctbrowser::(?:dom|script|aot)::|ctbrowser::(?:element_ref|document_ref)"
    r"|\bct_aot_|ctbrowser/(?:dom|script|aot)/"
)


def prepare(args, name, source, *, entry_name=None):
    ir, contract = dom.prepare(args, name, source, 0, entry_name=entry_name)
    del contract["element_parameters"]
    contract.update(provider="ctbrowser-intrinsics-v1", initial_intrinsics=["Symbol"])
    return ir, contract


def oracle(args):
    source = STATE + OBSERVE + f"""
var symbol01State = state() === Symbol.hasInstance;
var symbol02Repeat = state() === Symbol.hasInstance;
var symbol03Observe = observe() === {json.dumps(TRANSCRIPT)};
"""
    vm = args.work / "oracle.js"
    vm.write_text(source)
    expected = "symbol01State=true\nsymbol02Repeat=true\nsymbol03Observe=true\n"
    actual = run([args.reference, str(vm)]).stdout
    if actual != expected:
        raise RuntimeError(f"VM Symbol export observations differ: {actual}")
    node = args.work / "oracle.cjs"
    node.write_text(
        source
        + "\nconsole.log('symbol01State=' + symbol01State);"
        + "\nconsole.log('symbol02Repeat=' + symbol02Repeat);"
        + "\nconsole.log('symbol03Observe=' + symbol03Observe);\n"
    )
    if run([args.node, str(node)]).stdout != expected:
        raise RuntimeError("Node Symbol export observations differ")


def standalone(args, native, name, checks, expected, compilers, includes, libraries):
    text = native.read_text()
    entries = dom.NATIVE.findall(text)
    if (
        len(entries) != 1
        or entries[0] == "main"
        or dom.FUNCTION.search(text)
        or "ctnative.not_native" in text
    ):
        raise RuntimeError(f"{name}: expected one typed native export without a launcher")
    deduced = args.work / f"{name}.deduced.mlir"
    run([args.opt, str(native), "--ctnative-print-deduced", "-o", str(deduced)])
    client = (
        "\n#include <cassert>\n#include <iostream>\n#include <type_traits>\nint main() {\n"
        + checks.replace("@ENTRY@", entries[0])
        + "}\n"
    )
    for mode, ir in (("explicit", native), ("deduced", deduced)):
        cpp = run([args.translate, "--mlir-to-cpp", str(ir)]).stdout
        if FORBIDDEN.search(cpp) or re.search(r"\bmain\s*\(", cpp):
            raise RuntimeError(f"{name}/{mode}: intrinsic export acquired a DOM/VM dependency")
        source = args.work / f"{name}.{mode}.cpp"
        source.write_text(cpp + client)
        for index, compiler in enumerate(compilers):
            binary = args.work / f"{name}.{mode}.{index}"
            run([compiler, *FLAGS, *includes, str(source), *libraries, "-o", str(binary)])
            if FORBIDDEN.search(run([args.nm, "-C", str(binary)]).stdout):
                raise RuntimeError(f"{name}/{mode}: intrinsic binary links DOM/Script/AOT")
            if run([str(binary)]).stdout != expected:
                raise RuntimeError(f"{name}/{mode}: native Symbol observations differ")


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
    oracle(args)
    compilers = find_compilers()
    compilers[1] = args.clang
    includes, libraries = dom.link_options(args, core_only=True)
    accepted = {}
    for name, (source, checks, expected) in CASES.items():
        ir, contract = prepare(args, name, source)
        accepted[name] = ir, contract
        for optimize in (False, True):
            output_name = f"{name}-{optimize}"
            native = dom.lower(args, ir, contract, output_name, optimize=optimize)
            standalone(args, native, output_name, checks, expected, compilers, includes, libraries)

    refusals = 0

    def refuse(ir, contract, name, **options):
        nonlocal refusals
        result = dom.lower(args, ir, contract, name, success=False, **options)
        refusals += 1
        return result

    for name, body in REFUSALS.items():
        ir, contract = prepare(args, name, f"function bad() {{ {body} }}\n")
        for optimize in (False, True):
            refuse(ir, contract, name + str(optimize), optimize=optimize)
    source_refusals = {
        "parameters": "function bad(value) { return Symbol.iterator; }",
        "wrapper-effect": "var changed=1; function bad() { return Symbol.iterator; }",
        "wrapper-replacement": "Symbol=0; function bad() { return Symbol.iterator; }",
        "intrinsic-declaration": "function Symbol() { return 1; }",
        "helper": "function helper() { return Symbol.iterator; } function bad() { return helper(); }",
    }
    for name, source in source_refusals.items():
        entry = "Symbol" if name == "intrinsic-declaration" else "bad"
        ir, contract = prepare(args, name, source, entry_name=entry)
        refuse(ir, contract, name + "-refused")

    ir, contract = accepted["state"]
    refuse(ir, dict(contract, entry="_script_$0"), "script-entry")
    for name, fields in {
        "empty": {"initial_intrinsics": []},
        "extra": {"initial_intrinsics": ["Symbol", "Number"]},
        "duplicate": {"initial_intrinsics": ["Symbol", "Symbol"]},
        "roots": {"roots": []},
        "elements": {"element_parameters": []},
        "datasets": {"dataset_parameters": []},
        "realm": {"realm_global_this": True},
    }.items():
        refuse(ir, dict(contract, **fields), "schema-" + name)
    missing = dict(contract)
    del missing["initial_intrinsics"]
    refuse(ir, missing, "schema-missing")
    for budget in (0, 1):
        refuse(ir, contract, f"budget-{budget}", max_steps=budget)

    stale = args.work / "stale.mlir"
    changed = ir.read_text().replace('"hasInstance"', '"iterator"', 1)
    if changed == ir.read_text():
        raise RuntimeError("stale fingerprint control did not mutate an identity")
    stale.write_text(changed)
    if "fingerprint mismatch" not in refuse(stale, contract, "stale"):
        raise RuntimeError("changed Symbol identity accepted a stale fingerprint")

    forged, manifest = prepare(args, "forged", "function bad() { return typeof Symbol(); }")
    text, count = re.subn(
        r"\bmodule( attributes)? \{",
        lambda match: 'module attributes {ctnative.host_proved = true, ctnative.host_reason = ""'
        + (", " if match[1] else "} {"),
        forged.read_text(),
        count=1,
    )
    if count != 1:
        raise RuntimeError("forged metadata control was not installed")
    forged.write_text(text)
    manifest["module_sha256"] = fingerprint(args.opt, forged)
    refuse(forged, manifest, "forged-refused")
    print(
        f"Symbol exports: typed branch/loop return and primitive transcript agree with Node/VM; "
        f"16 native executions, {refusals} refusals; Core only, no DOM inputs"
    )


if __name__ == "__main__":
    main()
