#!/usr/bin/env python3
"""Compile a typed DOM action and identity query against the real DOM/Core.

This is an action-only browser entry gate, not original Bootstrap construction,
Data storage, event delivery or native initialization of the vendor bundle.
"""

import argparse
import json
from pathlib import Path
import re
import shutil
import subprocess

from CTNative.harness import find_compilers, run
from CTNative.HostContract.contract import fingerprint
from Target.Cpp.harness import FLAGS

ACTION = """function toggle(element) {
  const active = element.classList.toggle('active');
  element.setAttribute('aria-pressed', active);
  return active;
}
"""
IDENTITY = "function same(first, second) { return first === second; }\n"
LABEL = """function label(element) {
  element.setAttribute('DATA-State', 'false');
  element.setAttribute('data-empty', '');
  return element === element;
}
"""
INVALID_TOKEN = """function invalidToken(element) {
  element.classList.toggle('a b');
  element.setAttribute('aria-pressed', 'after');
  return true;
}
"""
FUNCTION = re.compile(r"\bctjs\.func (?:private )?@([^ (]+)\(")
NATIVE = re.compile(r"\bemitc\.func @([^ (]+)\(")
VM = re.compile(r"ctbrowser::(?:script|aot)::|\bct_aot_|ctbrowser/(?:script/|aot/aot\.hpp)")
CLEANUP = (
    "emitc.func(canonicalize,convert-scf-to-emitc,convert-arith-to-emitc,"
    "canonicalize,ctnative-prune-dead-stores,canonicalize)"
)

CLIENT = r"""
#include <cassert>
#include <exception>
#include <iostream>
#include <type_traits>

int main() {
    using namespace ctbrowser;
    static_assert(std::is_trivially_copyable_v<element_ref>);
    for (int lifetime = 0; lifetime < 16; ++lifetime) {
        atom_table atoms, foreign_atoms;
        document doc{atoms}, foreign_doc{foreign_atoms};
        const auto button = doc.create_element(atoms.intern("button"));
        const auto other_button = foreign_doc.create_element(foreign_atoms.intern("button"));
        assert(button == other_button); // Equal bits are not equal DOM identity.
        assert(doc.append_child(doc.root(), button));
        assert(foreign_doc.append_child(foreign_doc.root(), other_button));
        const auto child = doc.create_element(atoms.intern("span"));
        assert(doc.append_child(button, child));
        const auto classes = atoms.intern("class");
        const auto pressed = atoms.intern("aria-pressed");
        assert(doc.set_attribute(button, classes, "btn"));
        assert(foreign_doc.set_attribute(other_button, foreign_atoms.intern("class"), "btn"));
        const element_ref element{&doc, button};
        const auto alias = element;
        const element_ref foreign{&foreign_doc, other_button};
        doc.log_writes(true);
        @CHECKS@
        assert(doc.read().parent(child) == button);
        // All borrowed handles leave scope before their documents free nodes.
    }
    std::cout << "native DOM @NAME@: passed\n";
}
"""

ACTION_CHECKS = r"""
        for (const bool active : {true, false}) {
            assert(@ENTRY@(alias) == active);
            assert(doc.read().attribute_value(button, classes) == (active ? "btn active" : "btn"));
            assert(doc.read().attribute_value(button, pressed) == (active ? "true" : "false"));
            const auto writes = doc.take_writes();
            assert(writes.size() == 2);
            assert(writes[0].node == button && writes[0].name == classes);
            assert(writes[1].node == button && writes[1].name == pressed);
        }
        assert(@ENTRY@(foreign));
        assert(foreign_doc.read().attribute_value(other_button, foreign_atoms.intern("class")) == "btn active");
        assert(foreign_doc.read().attribute_value(other_button, foreign_atoms.intern("aria-pressed")) == "true");
        assert(doc.read().attribute_value(button, classes) == "btn");
        assert(doc.read().attribute_value(button, pressed) == "false");
        assert(doc.remove_child(button));
        assert(!doc.read().parent(button));
        assert(@ENTRY@(element));
        assert(doc.read().attribute_value(button, classes) == "btn active");
        assert(!@ENTRY@(alias));
        assert(doc.read().attribute_value(button, pressed) == "false");
        (void)doc.take_writes();
        const auto text = doc.create_text("not an element");
        for (const element_ref invalid : {
                 element_ref{nullptr, button}, element_ref{&doc, {}},
                 element_ref{&doc, {button.slot, button.generation + 2}},
                 element_ref{&doc, text}}) {
            bool rejected = false;
            try { (void)@ENTRY@(invalid); }
            catch (const std::exception &) { rejected = true; }
            assert(rejected);
            assert(doc.take_writes().empty());
            assert(doc.read().attribute_value(button, classes) == "btn");
            assert(doc.read().attribute_value(button, pressed) == "false");
        }
"""

IDENTITY_CHECKS = r"""
        const element_ref child_element{&doc, child};
        assert(@ENTRY@(element, alias));
        assert(!@ENTRY@(element, child_element));
        assert(!@ENTRY@(element, foreign));
        assert(!@ENTRY@(foreign, element));
        assert(@ENTRY@(foreign, foreign));
        assert(doc.remove_child(button));
        assert(!doc.read().parent(button));
        assert(@ENTRY@(element, alias));
        assert(!@ENTRY@(element, foreign));
        assert(doc.take_writes().empty());
        for (const bool invalid_first : {false, true}) {
            bool rejected = false;
            try {
                (void)@ENTRY@(invalid_first ? element_ref{} : element,
                              invalid_first ? element : element_ref{});
            } catch (const std::exception &) { rejected = true; }
            assert(rejected);
        }
        (void)pressed;
"""


LABEL_CHECKS = r"""
        assert(@ENTRY@(alias));
        const auto state = atoms.intern("data-state");
        const auto empty = atoms.intern("data-empty");
        assert(doc.read().attribute_value(button, state) == "false");
        assert(!doc.read().has_attribute(button, atoms.intern("DATA-State")));
        assert(doc.read().has_attribute(button, empty));
        assert(doc.read().attribute_value(button, empty).empty());
        const auto writes = doc.take_writes();
        assert(writes.size() == 2);
        assert(writes[0].node == button && writes[0].name == state);
        assert(writes[1].node == button && writes[1].name == empty);
        (void)foreign;
        (void)pressed;
"""

INVALID_TOKEN_CHECKS = r"""
        bool rejected = false;
        try { (void)@ENTRY@(alias); }
        catch (const std::bad_expected_access<token_error> & error) {
            rejected = error.error() == token_error::whitespace;
        }
        assert(rejected);
        assert(doc.take_writes().empty());
        assert(doc.read().attribute_value(button, classes) == "btn");
        assert(!doc.read().has_attribute(button, pressed));
        (void)foreign;
"""


def prepare(args, name, source, parameters):
    js = args.work / f"{name}.js"
    raw = args.work / f"{name}.raw.mlir"
    ir = args.work / f"{name}.mlir"
    js.write_text(source)
    imported = run([args.translate, "--ctbrowser-js-to-ctjs", str(js), "-o", str(raw)])
    if "ctjs.skipped" in raw.read_text() or "is not compiled:" in imported.stderr:
        raise RuntimeError(f"{name}: source was not completely imported")
    run([args.opt, str(raw), "--ctjs-resolve-globals", "--ctjs-lift-to-scf", "-o", str(ir)])
    entries = [symbol for symbol in FUNCTION.findall(ir.read_text()) if symbol != "_script_$0"]
    if len(entries) != 1 or len(FUNCTION.findall(ir.read_text())) != 2:
        raise RuntimeError(f"{name}: expected one source entry and its declaration wrapper")
    contract = {
        "version": 1,
        "provider": "ctbrowser-dom-v1",
        "module_sha256": fingerprint(args.opt, ir),
        "entry": entries[0],
        "element_parameters": list(range(parameters)),
    }
    return ir, contract


def lower(args, ir, contract, name, *, optimize=False, success=True):
    config = args.work / f"{name}.json"
    output = args.work / f"{name}.native.mlir"
    config.write_text(json.dumps(contract, indent=2) + "\n")
    flags = f"host-manifest={config} optimize={'true' if optimize else 'false'}"
    command = [
        args.opt,
        str(ir),
        f"--pass-pipeline=builtin.module(ctnative-lower-to-emitc{{{flags}}},{CLEANUP})",
        "-o",
        str(output),
    ]
    if success:
        run(command)
        return output
    result = subprocess.run(command, capture_output=True, text=True, timeout=120)
    if result.returncode not in (0, 1):
        raise RuntimeError(f"{name}: refusal crashed: {result.stderr}")
    text = output.read_text() if result.returncode == 0 else result.stderr
    if result.returncode == 0:
        if NATIVE.search(text) or len(FUNCTION.findall(text)) != len(
            FUNCTION.findall(ir.read_text())
        ):
            raise RuntimeError(f"{name}: refused proof emitted or lost a source function\n{text}")
        if text.count("ctnative.not_native =") != len(FUNCTION.findall(text)):
            raise RuntimeError(f"{name}: refusal lacks source diagnostics\n{text}")
    elif "error:" not in text:
        raise RuntimeError(f"{name}: missing failure diagnostic\n{text}")
    return text


def link_options(args):
    cache = dict(
        re.findall(
            r"^([A-Za-z0-9_]+):[^=\n]+=(.*)$", (args.build / "CMakeCache.txt").read_text(), re.M
        )
    )
    includes = ["-I" + str(args.include)]
    for name in ("CTBROWSER_BOOST_INCLUDE_DIR", "Boost_INCLUDE_DIR", "CTBROWSER_SIMDUTF_INCLUDE"):
        value = cache.get(name, "")
        if value and not value.endswith("-NOTFOUND"):
            includes += ["-isystem", value]
    libraries = [
        args.build / "lib/DOM/libctbrowser-dom.a",
        args.build / "lib/Core/libctbrowser-core.a",
    ]
    for name in ("CTBROWSER_SIMDUTF", "CTBROWSER_MIMALLOC"):
        if name == "CTBROWSER_MIMALLOC" and cache.get("CTBROWSER_USE_MIMALLOC") != "ON":
            continue
        value = cache.get(name, "")
        if not value or value.endswith("-NOTFOUND"):
            raise RuntimeError(f"missing configured native dependency: {name}")
        libraries.append(Path(value))
    for library in libraries:
        if not library.is_file():
            raise RuntimeError(f"missing native library: {library}")
    rpaths = [
        "-Wl,-rpath," + str(path) for path in sorted({library.parent for library in libraries})
    ]
    return includes, [*map(str, libraries), "-pthread", *rpaths]


def standalone(args, native, name, checks, compilers, includes, libraries):
    text = native.read_text()
    entries = NATIVE.findall(text)
    if (
        len(entries) != 1
        or entries[0] == "main"
        or FUNCTION.search(text)
        or "ctnative.not_native" in text
    ):
        raise RuntimeError(f"{name}: expected exactly the typed entry without a launcher\n{text}")
    deduced = args.work / f"{name}.deduced.mlir"
    run([args.opt, str(native), "--ctnative-print-deduced", "-o", str(deduced)])
    client = (
        CLIENT.replace("@CHECKS@", checks).replace("@ENTRY@", entries[0]).replace("@NAME@", name)
    )
    for mode, ir in (("explicit", native), ("deduced", deduced)):
        cpp = run([args.translate, "--mlir-to-cpp", str(ir)]).stdout
        if VM.search(cpp) or "ctbrowser::element_ref" not in cpp or re.search(r"\bmain\s*\(", cpp):
            raise RuntimeError(f"{name}/{mode}: missing typed standalone DOM entry\n{cpp}")
        if "nullable_scalar" in cpp:
            raise RuntimeError(f"{name}/{mode}: Boolean DOM entry carries a scalar value model")
        if "action" in name and (
            "ctbrowser::toggle_token" not in cpp or "ctbrowser::set_element_attribute" not in cpp
        ):
            raise RuntimeError(f"{name}/{mode}: action bypasses the shared browser API\n{cpp}")
        source = args.work / f"{name}.{mode}.cpp"
        source.write_text(cpp + client)
        for index, compiler in enumerate(compilers):
            binary = args.work / f"{name}.{mode}.{index}"
            run([compiler, *FLAGS, *includes, str(source), *libraries, "-o", str(binary)])
            if VM.search(run([args.nm, "-C", str(binary)]).stdout):
                raise RuntimeError(f"{name}/{mode}: binary links Script/AOT")
            if run([str(binary)]).stdout != f"native DOM {name}: passed\n":
                raise RuntimeError(f"{name}/{mode}: real DOM observations were not completed")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--translate", required=True)
    parser.add_argument("--opt", required=True)
    parser.add_argument("--clang", help="Clang with the configured DOM's C++23 library support")
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--include", type=Path, required=True)
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--nm", default=shutil.which("nm"))
    args = parser.parse_args()
    args.work = args.work.resolve()
    args.work.mkdir(parents=True, exist_ok=True)
    if not args.nm:
        raise RuntimeError("native DOM gate requires nm")
    compilers = find_compilers()
    if args.clang:
        compilers[1] = args.clang
    includes, libraries = link_options(args)
    prepared = {}
    for name, source, parameters, checks in (
        ("action", ACTION, 1, ACTION_CHECKS),
        ("identity", IDENTITY, 2, IDENTITY_CHECKS),
        ("reserved-name", IDENTITY.replace("same(", "_script_("), 2, IDENTITY_CHECKS),
        ("label", LABEL, 1, LABEL_CHECKS),
        ("invalid-token", INVALID_TOKEN, 1, INVALID_TOKEN_CHECKS),
    ):
        ir, contract = prepare(args, name, source, parameters)
        prepared[name] = ir, contract
        for optimize in (False, True):
            label = f"{name}-{'optimized' if optimize else 'unoptimized'}"
            native = lower(args, ir, contract, label, optimize=optimize)
            standalone(args, native, label, checks, compilers, includes, libraries)

    action, contract = prepared["action"]
    raw = run([args.opt, str(action), "--ctnative-lower-to-emitc=optimize=false"]).stdout
    if "ctnative.not_native" not in raw or "ctbrowser::element_ref" in raw:
        raise RuntimeError("an unproved parameter acquired a DOM entry without a manifest")
    changed = action.read_text().replace('"active"', '"changed"', 1)
    if changed == action.read_text():
        raise RuntimeError("stale-contract control did not change the source token")
    stale = args.work / "stale-input.mlir"
    stale.write_text(changed)
    diagnostic = lower(args, stale, contract, "stale", success=False)
    if "fingerprint mismatch" not in diagnostic:
        raise RuntimeError("stale DOM contract did not name the fingerprint mismatch")
    for name, source, reason in (
        (
            "unknown-receiver",
            "function toggle(element) { const other = {}; return other.classList.toggle('active'); }",
            "DOM",
        ),
        (
            "prototype-write",
            ACTION.replace("  const active", "  element.__proto__ = {};\n  const active"),
            "DOM",
        ),
        (
            "method-write",
            ACTION.replace(
                "  const active", "  element.classList.toggle = element;\n  const active"
            ),
            "DOM",
        ),
        ("invoked-entry", ACTION + "toggle({});\n", "wrapper"),
    ):
        ir, manifest = prepare(args, name, source, 1)
        diagnostic = lower(args, ir, manifest, name, success=False)
        if reason not in diagnostic:
            raise RuntimeError(f"{name}: missing intended proof refusal\n{diagnostic}")
        if name == "unknown-receiver":
            forged = args.work / "forged.mlir"
            text, count = re.subn(
                r"\bmodule( attributes)? \{",
                lambda match: 'module attributes {ctnative.host_proved = true, ctnative.dom_entry = "trusted"'
                + (", " if match[1] else "} {"),
                ir.read_text(),
                count=1,
            )
            if count != 1:
                raise RuntimeError("forged-evidence control did not add its report attributes")
            forged.write_text(text)
            fresh = dict(manifest, module_sha256=fingerprint(args.opt, forged))
            lower(args, forged, fresh, "fresh-forged", success=False)
    for name, changed in (
        ("unknown-provider", dict(contract, provider="magic-dom")),
        ("supplied-proof", dict(contract, nonthrowing=True)),
        ("missing-parameter", dict(contract, element_parameters=[])),
    ):
        lower(args, action, changed, name, success=False)
    print(
        "native DOM: 5 action/identity/name/string/error entries, both policies/layouts, GCC/Clang, DOM/Core-only; 10 refusal controls"
    )


if __name__ == "__main__":
    main()
