#!/usr/bin/env python3
"""Prove well-known Symbol reads and compare their DOM effects with Node/VM."""

import argparse
import json
from pathlib import Path
import shutil

from CTNative.Browser import native_dom as dom
from CTNative.HostContract.contract import fingerprint
from CTNative.harness import find_compilers, run

# Independent language inventory: a missing catalog entry must fail this check.
KEYS = (
    "iterator",
    "asyncIterator",
    "hasInstance",
    "toPrimitive",
    "toStringTag",
    "unscopables",
    "dispose",
    "asyncDispose",
    "match",
    "matchAll",
    "replace",
    "search",
    "split",
    "isConcatSpreadable",
    "species",
)
OBSERVATIONS = {
    "data-type": "symbol",
    "data-same": "true",
    "data-different": "true",
    "data-loose": "true",
    "data-not": "false",
    "data-truth": "yes",
    **{f"data-{key.lower()}": "true" for key in KEYS},
}
SOURCE = (
    """function symbols(element) {
  const key = Symbol.hasInstance;
  const copy = key;
  element.setAttribute('data-type', typeof key);
  element.setAttribute('data-same', key === copy);
  element.setAttribute('data-different', key !== Symbol.iterator);
  element.setAttribute('data-loose', key == Symbol['hasInstance']);
  element.setAttribute('data-not', !key);
  if (key) element.setAttribute('data-truth', 'yes');
"""
    + "".join(
        f"  element.setAttribute('data-{key.lower()}', Symbol.{key} === Symbol.{key});\n"
        for key in KEYS
    )
    + """  return element.hasAttribute('data-other') ? key === Symbol.iterator : !!copy;
}
"""
)
CHECKS = (
    """
        (void)pressed; (void)foreign;
        assert(@ENTRY@(alias));
"""
    + "".join(
        f'        assert(doc.read().attribute_value(button, atoms.intern("{name}")) == "{value}");\n'
        for name, value in OBSERVATIONS.items()
    )
    + f"""
        assert(doc.take_writes().size() == {len(OBSERVATIONS)});
        assert(doc.set_attribute(button, atoms.intern("data-other"), ""));
        (void)doc.take_writes();
        assert(!@ENTRY@(element));
        assert(doc.take_writes().size() == {len(OBSERVATIONS)});
"""
)

REFUSALS = {
    "global-replacement": "Symbol=element; return false;",
    "member-replacement": "Symbol.hasInstance=Symbol.iterator; return false;",
    "alias-mutation": "const saved=Symbol; saved.hasInstance=Symbol.iterator; return false;",
    "escape-constructor": "return Symbol;",
    "dynamic-key": "return typeof Symbol[element.getAttribute('data-key')];",
    "unknown-key": "return typeof Symbol.unknown;",
    "registry": "return typeof Symbol.for('x');",
    "number": "return +Symbol.iterator;",
    "concat": "return '' + Symbol.iterator;",
    "property-key": "return element[Symbol.iterator];",
    "instanceof": "return element instanceof Symbol;",
    "mixed-compare": "return Symbol.iterator === 'iterator';",
    "mixed-join": "return element.hasAttribute('x') ? Symbol.iterator : 0;",
    "absent-join": "let key; if(element.hasAttribute('x')) key=Symbol.iterator; return key;",
    "mixed-loop": "let key=Symbol.iterator; for(let i=0;i<2;i++) key=0; return key;",
}


TRANSPORT = {
    "fresh": "return typeof Symbol();",
    "symbol-return": "return Symbol.iterator;",
    "join": "const key=element.hasAttribute('x') ? Symbol.iterator : Symbol.hasInstance; return !!key;",
    "loop": "let key=Symbol.iterator; for(let i=0;i<2;i++) key=Symbol.hasInstance; return !!key;",
}

STATE = """function state(element) {
  let key = element.hasAttribute('data-other') ? Symbol.hasInstance : Symbol.iterator;
  const saved = key;
  const iterations = element.hasAttribute('data-empty') ? 0 : 3;
  for (let i = 0; i < iterations; i++) {
    element.setAttribute('data-copy', key === saved);
    key = key === Symbol.iterator ? Symbol.hasInstance : Symbol.iterator;
  }
  element.setAttribute('data-saved', saved === Symbol.iterator);
  return key;
}
"""
STATE_CHECKS = r"""
        (void)pressed; (void)foreign;
        static_assert(std::is_same_v<decltype(@ENTRY@(element)), ctnative::js_symbol_t>);
        const auto first = @ENTRY@(alias);
        assert(first == ctnative::Symbol.hasInstance);
        assert(doc.read().attribute_value(button, atoms.intern("data-copy")) == "true");
        assert(doc.read().attribute_value(button, atoms.intern("data-saved")) == "true");
        assert(doc.take_writes().size() == 4);
        assert(doc.set_attribute(button, atoms.intern("data-other"), ""));
        (void)doc.take_writes();
        assert(@ENTRY@(element) == ctnative::Symbol.iterator);
        assert(first == ctnative::Symbol.hasInstance);
        assert(doc.read().attribute_value(button, atoms.intern("data-copy")) == "true");
        assert(doc.read().attribute_value(button, atoms.intern("data-saved")) == "false");
        assert(doc.take_writes().size() == 4);
        assert(doc.set_attribute(button, atoms.intern("data-empty"), ""));
        (void)doc.take_writes();
        assert(@ENTRY@(element) == ctnative::Symbol.hasInstance);
        assert(first == ctnative::Symbol.hasInstance);
        assert(doc.take_writes().size() == 1);
"""


def oracle(args):
    # This receiver records calls; actual platform behavior is checked by the
    # native client against public DOM. Both execute the identical source body.
    assertions = " && ".join(
        f"saved[{json.dumps(name)}] === {json.dumps(value)}" for name, value in OBSERVATIONS.items()
    )
    helper = f"""
function observe(other) {{
  const saved = {{}};
  let writes = 0;
  const receiver = {{
    setAttribute: function(name, value) {{ saved[name] = '' + value; writes++; }},
    hasAttribute: function(name) {{ return other; }}
  }};
  const result = symbols(receiver);
  return result === !other && writes === {len(OBSERVATIONS)} && {assertions};
}}
var symbolFirst = observe(false);
var symbolSecond = observe(true);
"""
    vm = args.work / "oracle.js"
    transport_source = "\n".join(
        f"function {name.replace('-', '_')}(element) {{ {body} }}"
        for name, body in TRANSPORT.items()
    )
    transport_oracle = """
function observeState(other, empty) {
  const saved = {};
  let writes = 0;
  const receiver = {
    setAttribute: function(name, value) { saved[name] = '' + value; writes++; },
    hasAttribute: function(name) { return name === 'data-empty' ? empty : other; }
  };
  const result = state(receiver);
  return result === (other && !empty ? Symbol.iterator : Symbol.hasInstance) &&
    writes === (empty ? 1 : 4) && (empty || saved['data-copy'] === 'true') &&
    saved['data-saved'] === (other ? 'false' : 'true') &&
    join(receiver) && loop(receiver) && symbol_return(receiver) === Symbol.iterator;
}
var symbolTransportFirst = observeState(false, false);
var symbolTransportSecond = observeState(true, false);
var symbolTransportZero = observeState(true, true);
"""
    vm.write_text(SOURCE + STATE + transport_source + helper + transport_oracle)
    if (
        run([args.reference, str(vm)]).stdout
        != "symbolFirst=true\nsymbolSecond=true\nsymbolTransportFirst=true\nsymbolTransportSecond=true\nsymbolTransportZero=true\n"
    ):
        raise RuntimeError("VM Symbol identity/effect observations differ")
    node = args.work / "oracle.cjs"
    node.write_text(
        vm.read_text()
        + "\nif (!symbolFirst || !symbolSecond || !symbolTransportFirst || !symbolTransportSecond || !symbolTransportZero) throw Error('Symbol oracle');\n"
    )
    run([args.node, str(node)])


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
    includes, libraries = dom.link_options(args)
    ir, manifest = dom.prepare(args, "symbols", SOURCE, 1)
    manifest["initial_intrinsics"] = ["Symbol"]
    for optimize in (False, True):
        name = f"symbols-{optimize}"
        native = dom.lower(args, ir, manifest, name, optimize=optimize)
        text = native.read_text()
        if "ctnative::js_symbol_t" not in text or any(
            "ctnative::Symbol." + key not in text for key in KEYS
        ):
            raise RuntimeError("Symbol reads lost their typed property spelling")
        dom.standalone(args, native, name, CHECKS, compilers, includes, libraries)
        dom.lower(
            args,
            ir,
            dict(manifest, initial_intrinsics=[]),
            name + "-missing",
            optimize=optimize,
            success=False,
        )
        if not optimize:
            # Mutate a proved identity in the native artifact; the observable
            # unequal comparison must catch it after successful compilation.
            mutated = args.work / "mutated.native.mlir"
            mutated.write_text(
                text.replace("ctnative::Symbol.hasInstance", "ctnative::Symbol.iterator", 1)
            )
            try:
                dom.standalone(
                    args, mutated, "symbols-mutated", CHECKS, compilers[:1], includes, libraries
                )
            except RuntimeError as error:
                if "unexpected tool status -6:" not in str(error) or "Assertion" not in str(error):
                    raise
            else:
                raise RuntimeError("Symbol identity mutation escaped observation")
    for name, body in TRANSPORT.items():
        original, contract = dom.prepare(
            args, "original-" + name, f"function original(element) {{ {body} }}\n", 1
        )
        contract["initial_intrinsics"] = ["Symbol"]
        native = dom.lower(args, original, contract, "original-" + name)
        expected = " == ctnative::Symbol.iterator" if name == "symbol-return" else ""
        if name == "fresh":
            expected = '.value() == "symbol"'
        checks = f"(void)pressed; (void)foreign; (void)alias; assert(@ENTRY@(element){expected});"
        dom.standalone(args, native, "original-" + name, checks, compilers, includes, libraries)
    state_ir, state_contract = dom.prepare(args, "state", STATE, 1)
    state_contract["initial_intrinsics"] = ["Symbol"]
    for optimize in (False, True):
        name = f"state-{optimize}"
        native = dom.lower(args, state_ir, state_contract, name, optimize=optimize)
        if "std::optional<ctnative::js_symbol_t>" not in native.read_text():
            raise RuntimeError("Symbol structured storage was not exercised")
        dom.standalone(args, native, name, STATE_CHECKS, compilers, includes, libraries)
    for name, body in REFUSALS.items():
        bad, contract = dom.prepare(args, name, f"function bad(element) {{ {body} }}\n", 1)
        contract["initial_intrinsics"] = ["Symbol"]
        for optimize in (False, True):
            dom.lower(args, bad, contract, name + str(optimize), optimize=optimize, success=False)
    for budget in (0, 1):
        dom.lower(args, ir, manifest, f"budget-{budget}", max_steps=budget, success=False)
    stale_ir = args.work / "stale.mlir"
    stale_ir.write_text(ir.read_text().replace('"hasInstance"', '"iterator"', 1))
    if "fingerprint mismatch" not in dom.lower(args, stale_ir, manifest, "stale", success=False):
        raise RuntimeError("changed Symbol source accepted a stale fingerprint")
    forged, contract = dom.prepare(
        args, "forged", "function bad(element) { return typeof Symbol.for('x'); }", 1
    )
    forged.write_text(
        forged.read_text().replace(
            "module attributes {",
            'module attributes {ctnative.host_proved = true, ctnative.host_reason = "", ',
            1,
        )
    )
    if "ctnative.host_proved" not in forged.read_text():
        raise RuntimeError("forged metadata control was not installed")
    contract.update(module_sha256=fingerprint(args.opt, forged), initial_intrinsics=["Symbol"])
    dom.lower(args, forged, contract, "forged-refused", success=False)
    print(
        f"Symbol DOM: {len(OBSERVATIONS)} attributes and 2 returns agree with Node/VM; "
        f"{16 + 4 * len(TRANSPORT)} native executions, {2 * len(REFUSALS) + 6} refusals, 1 mutation; DOM/Core only"
    )


if __name__ == "__main__":
    main()
