#!/usr/bin/env python3
"""Compile typed actions and queries against the real DOM/Core/Style.

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
FORCED = """function forced(element) {
  const wanted = element.hasAttribute('DATA-Force');
  const active = element.classList.toggle('active', wanted);
  return element.toggleAttribute('DISABLED', active);
}
"""
ATTRIBUTES = """function attributes(element) {
  const added = element.toggleAttribute('DATA-Empty');
  const seen = element.hasAttribute('data-empty');
  element.toggleAttribute('DATA-Copy', added);
  element.setAttribute('data-seen', seen);
  element.removeAttribute('DATA-Empty');
  return element.hasAttribute('data-empty');
}
"""
ATTRIBUTE_NOOPS = """function attributeNoops(element) {
  const present = element.hasAttribute('DATA-Empty');
  element.toggleAttribute('DATA-Empty', true);
  element.toggleAttribute('data-missing', false);
  element.removeAttribute('bad name');
  return present;
}
"""
EXPLICIT_FORCES = """function explicitForces(element) {
  element.classList.toggle('missing', false);
  element.classList.toggle('active', true);
  element.classList.toggle('active', undefined);
  const absent = element.toggleAttribute('data-missing', undefined);
  element.setAttribute('data-missing-was', absent);
  element.toggleAttribute('disabled', true);
  return element.toggleAttribute('disabled', undefined);
}
"""
INVALID_ATTRIBUTE = """function invalidAttribute(element) {
  element.toggleAttribute('bad name', false);
  element.setAttribute('aria-pressed', 'after');
  return true;
}
"""
CONTAINS = "function contains(first, second) { return first.contains(second); }\n"
MATCHES = """function matches(element) {
  const matched = element.matches('button.btn:hover');
  element.toggleAttribute('data-matched', matched);
  return matched;
}
"""
CLOSEST = """function closest(element, expected) {
  return element.closest('[data-bs-toggle="button"]') === expected;
}
"""
CLOSEST_STATE = """function hovered(element, expected) {
  return element.closest('button:hover') === expected;
}
"""
CLOSEST_SCOPE = "function scope(element) { return element.closest(':scope') === element; }\n"
CLOSEST_MISSES = """function misses(first, second) {
  return first.closest('.missing') === second.closest('.missing');
}
"""
INVALID_SELECTOR = """function invalidSelector(element) {
  element.setAttribute('data-before', 'yes');
  element.matches('[');
  element.setAttribute('data-after', 'no');
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
            assert(static_cast<bool>(@ENTRY@(alias)) == active);
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
        assert(doc.take_writes().empty());
        assert(doc.remove_child(button));
        (void)doc.take_writes();
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

FORCED_CHECKS = r"""
        const auto force = atoms.intern("data-force");
        const auto disabled = atoms.intern("disabled");
        assert(doc.set_attribute(button, classes, " btn\tbtn  "));
        (void)doc.take_writes();
        auto version = doc.version();
        assert(!@ENTRY@(alias));
        assert(doc.version() == version && doc.take_writes().empty());
        assert(doc.read().attribute_value(button, classes) == " btn\tbtn  ");
        assert(!doc.read().has_attribute(button, disabled));
        assert(doc.set_attribute(button, force, ""));
        (void)doc.take_writes();
        assert(@ENTRY@(alias));
        auto writes = doc.take_writes();
        assert(writes.size() == 2 && writes[0].name == classes && writes[1].name == disabled);
        assert(doc.read().attribute_value(button, classes) == "btn active");
        assert(doc.read().has_attribute(button, disabled));
        assert(doc.read().attribute_value(button, disabled).empty());
        assert(doc.set_attribute(button, classes, " active\tbtn active "));
        assert(doc.set_attribute(button, disabled, "kept"));
        (void)doc.take_writes();
        version = doc.version();
        assert(@ENTRY@(element));
        assert(doc.version() == version && doc.take_writes().empty());
        assert(doc.read().attribute_value(button, classes) == " active\tbtn active ");
        assert(doc.read().attribute_value(button, disabled) == "kept");
        assert(doc.remove_attribute(button, force));
        (void)doc.take_writes();
        version = doc.version();
        assert(!@ENTRY@(alias));
        writes = doc.take_writes();
        assert(doc.version() == version + 2);
        assert(writes.size() == 1 && writes[0].name == classes);
        assert(doc.read().attribute_value(button, classes) == "btn");
        assert(!doc.read().has_attribute(button, disabled));
        assert(doc.remove_child(button));
        (void)doc.take_writes();
        version = doc.version();
        assert(!@ENTRY@(element));
        assert(doc.version() == version && doc.take_writes().empty());
        assert(!@ENTRY@(foreign));
        (void)pressed;
"""

ATTRIBUTE_CHECKS = r"""
        const auto empty = atoms.intern("data-empty");
        const auto copy = atoms.intern("data-copy");
        const auto seen = atoms.intern("data-seen");
        for (int call = 0; call < 2; ++call) {
            const auto version = doc.version();
            assert(!@ENTRY@(alias));
            assert(!doc.read().has_attribute(button, empty));
            assert(doc.read().has_attribute(button, copy));
            assert(doc.read().attribute_value(button, copy).empty());
            assert(doc.read().attribute_value(button, seen) == "true");
            const auto writes = doc.take_writes();
            assert(doc.version() == version + (call == 0 ? 4u : 3u));
            assert(writes.size() == (call == 0 ? 3u : 2u));
            assert(writes.front().name == empty && writes.back().name == seen);
            if (call == 0) { assert(writes[1].name == copy); }
        }
        (void)foreign;
        (void)pressed;
"""

ATTRIBUTE_NOOP_CHECKS = r"""
        const auto empty = atoms.intern("data-empty");
        const auto missing = atoms.intern("data-missing");
        for (const auto text : {"", "kept"}) {
            assert(doc.set_attribute(button, empty, text));
            (void)doc.take_writes();
            const auto version = doc.version();
            assert(@ENTRY@(alias));
            assert(doc.version() == version && doc.take_writes().empty());
            assert(doc.read().attribute_value(button, empty) == text);
            assert(!doc.read().has_attribute(button, missing));
        }
        assert(doc.remove_attribute(button, empty));
        (void)doc.take_writes();
        assert(!@ENTRY@(element));
        assert(doc.read().has_attribute(button, empty));
        assert(doc.read().attribute_value(button, empty).empty());
        const auto writes = doc.take_writes();
        assert(writes.size() == 1 && writes[0].name == empty);
        (void)foreign;
        (void)pressed;
"""

EXPLICIT_FORCE_CHECKS = r"""
        const auto missing = atoms.intern("data-missing");
        const auto was_missing = atoms.intern("data-missing-was");
        const auto disabled = atoms.intern("disabled");
        for (const bool active : {true, false}) {
            assert(doc.set_attribute(button, classes,
                active ? " btn\tbtn active active " : "btn missing"));
            (void)doc.take_writes();
            const auto version = doc.version();
            assert(!@ENTRY@(alias));
            assert(doc.read().attribute_value(button, classes) == "btn");
            assert(!doc.read().has_attribute(button, missing));
            assert(doc.read().attribute_value(button, was_missing) == "false");
            assert(!doc.read().has_attribute(button, disabled));
            const auto writes = doc.take_writes();
            assert(doc.version() == version + (active ? 4u : 6u));
            assert(writes.size() == (active ? 3u : 5u));
            const auto class_writes = writes.size() - 2;
            for (std::size_t at = 0; at < class_writes; ++at) {
                assert(writes[at].name == classes);
            }
            assert(writes[class_writes].name == was_missing);
            assert(writes.back().name == disabled);
        }
        (void)foreign;
        (void)pressed;
"""

INVALID_ATTRIBUTE_CHECKS = r"""
        const auto version = doc.version();
        bool rejected = false;
        try { (void)@ENTRY@(alias); }
        catch (const std::bad_expected_access<dom_error> & error) {
            rejected = error.error() == dom_error::invalid_attribute_name;
        }
        assert(rejected);
        assert(doc.version() == version && doc.take_writes().empty());
        assert(!doc.read().has_attribute(button, pressed));
        (void)foreign;
"""


CONTAINS_CHECKS = r"""
        const element_ref descendant{&doc, child};
        assert(@ENTRY@(element, alias));
        assert(@ENTRY@(element, descendant));
        assert(!@ENTRY@(descendant, element));
        assert(!@ENTRY@(element, foreign));
        assert(!@ENTRY@(foreign, element));
        assert(doc.remove_child(button));
        assert(@ENTRY@(element, descendant));
        assert(doc.remove_child(child));
        assert(!@ENTRY@(element, descendant));
        assert(doc.append_child(button, child));
        for (const bool invalid_first : {false, true}) {
            bool rejected = false;
            try { (void)@ENTRY@(invalid_first ? element_ref{} : element,
                               invalid_first ? element : element_ref{}); }
            catch (const std::bad_expected_access<dom_error> &) { rejected = true; }
            assert(rejected);
        }
        (void)pressed;
"""

MATCHES_CHECKS = r"""
        style::engine selectors{atoms}, foreign_selectors{foreign_atoms};
        const auto matched = atoms.intern("data-matched");
        assert(!@ENTRY@(element, selectors));
        assert(selectors.set_state(button, style::engine::state_hover, true));
        assert(@ENTRY@(alias, selectors));
        assert(doc.read().has_attribute(button, matched));
        assert(!@ENTRY@(foreign, foreign_selectors));
        assert(doc.remove_child(button));
        assert(@ENTRY@(element, selectors));
        assert(selectors.set_state(button, style::engine::state_hover, false));
        assert(!@ENTRY@(element, selectors));
        assert(!doc.read().has_attribute(button, matched));
        (void)doc.take_writes();
        bool rejected = false;
        try { (void)@ENTRY@(element, foreign_selectors); }
        catch (const std::invalid_argument &) { rejected = true; }
        assert(rejected && doc.take_writes().empty());
        (void)pressed;
"""

CLOSEST_CHECKS = r"""
        style::engine selectors{atoms}, foreign_selectors{foreign_atoms};
        const element_ref descendant{&doc, child};
        const auto toggle = atoms.intern("data-bs-toggle");
        assert(doc.set_attribute(button, toggle, "button"));
        assert(@ENTRY@(descendant, element, selectors));
        assert(@ENTRY@(element, alias, selectors));
        assert(!@ENTRY@(descendant, foreign, selectors));
        assert(!@ENTRY@(element, descendant, selectors));
        assert(!@ENTRY@(foreign, foreign, foreign_selectors));
        assert(doc.remove_child(button));
        assert(@ENTRY@(descendant, element, selectors));
        assert(doc.remove_attribute(button, toggle));
        assert(!@ENTRY@(descendant, element, selectors));
        assert(doc.set_attribute(button, toggle, "button"));
        const auto host = doc.create_element(atoms.intern("div"));
        assert(doc.append_child(button, host));
        const auto shadow = doc.attach_shadow(host, true).value();
        const auto shadow_child = doc.create_element(atoms.intern("span"));
        assert(doc.append_child(shadow, shadow_child));
        assert(!@ENTRY@(element_ref{&doc, shadow_child}, element, selectors));
        (void)doc.take_writes();
        bool rejected = false;
        try { (void)@ENTRY@(descendant, element, foreign_selectors); }
        catch (const std::invalid_argument &) { rejected = true; }
        assert(rejected && doc.take_writes().empty());
        (void)pressed;
"""

CLOSEST_STATE_CHECKS = r"""
        style::engine selectors{atoms};
        const element_ref descendant{&doc, child};
        assert(!@ENTRY@(descendant, element, selectors));
        assert(selectors.set_state(button, style::engine::state_hover, true));
        assert(@ENTRY@(descendant, element, selectors));
        assert(doc.remove_child(button));
        assert(@ENTRY@(descendant, element, selectors));
        assert(selectors.set_state(button, style::engine::state_hover, false));
        assert(!@ENTRY@(descendant, element, selectors));
        (void)alias;
        (void)foreign;
        (void)pressed;
"""

CLOSEST_SCOPE_CHECKS = r"""
        style::engine selectors{atoms}, foreign_selectors{foreign_atoms};
        assert(@ENTRY@(element, selectors));
        assert(@ENTRY@(foreign, foreign_selectors));
        assert(doc.remove_child(button));
        assert(@ENTRY@(element, selectors));
        (void)alias;
        (void)pressed;
"""

CLOSEST_MISSES_CHECKS = r"""
        style::engine selectors{atoms}, foreign_selectors{foreign_atoms};
        assert(@ENTRY@(element, foreign, selectors, foreign_selectors));
        assert(doc.set_attribute(button, classes, "missing"));
        assert(!@ENTRY@(element, foreign, selectors, foreign_selectors));
        assert(foreign_doc.set_attribute(other_button,
            foreign_atoms.intern("class"), "missing"));
        assert(!@ENTRY@(element, foreign, selectors, foreign_selectors));
        assert(@ENTRY@(element, alias, selectors, selectors));
        (void)pressed;
"""

INVALID_SELECTOR_CHECKS = r"""
        style::engine selectors{atoms};
        bool rejected = false;
        try { (void)@ENTRY@(element, selectors); }
        catch (const std::invalid_argument &) { rejected = true; }
        assert(rejected);
        const auto writes = doc.take_writes();
        assert(writes.size() == 1 && writes[0].name == atoms.intern("data-before"));
        assert(doc.read().attribute_value(button, atoms.intern("data-before")) == "yes");
        assert(!doc.read().has_attribute(button, atoms.intern("data-after")));
        (void)foreign;
        (void)alias;
        (void)pressed;
"""


def prepare(args, name, source, parameters, *, entry_name=None):
    js = args.work / f"{name}.js"
    raw = args.work / f"{name}.raw.mlir"
    ir = args.work / f"{name}.mlir"
    js.write_text(source)
    imported = run([args.translate, "--ctbrowser-js-to-ctjs", str(js), "-o", str(raw)])
    if "ctjs.skipped" in raw.read_text() or "is not compiled:" in imported.stderr:
        raise RuntimeError(f"{name}: source was not completely imported")
    run([args.opt, str(raw), "--ctjs-resolve-globals", "--ctjs-lift-to-scf", "-o", str(ir)])
    entries = [symbol for symbol in FUNCTION.findall(ir.read_text()) if symbol != "_script_$0"]
    if entry_name is not None:
        entries = [symbol for symbol in entries if symbol.rsplit("$", 1)[0] == entry_name]
    elif len(FUNCTION.findall(ir.read_text())) != 2:
        raise RuntimeError(f"{name}: expected one source entry and its declaration wrapper")
    if len(entries) != 1:
        raise RuntimeError(f"{name}: expected one source entry and its declaration wrapper")
    contract = {
        "version": 1,
        "provider": "ctbrowser-dom-v1",
        "module_sha256": fingerprint(args.opt, ir),
        "entry": entries[0],
        "element_parameters": list(range(parameters)),
    }
    return ir, contract


def lower(args, ir, contract, name, *, optimize=False, success=True, max_steps=None):
    config = args.work / f"{name}.json"
    output = args.work / f"{name}.native.mlir"
    config.write_text(json.dumps(contract, indent=2) + "\n")
    flags = f"host-manifest={config} optimize={'true' if optimize else 'false'}"
    if max_steps is not None:
        flags += f" host-max-steps={max_steps}"
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


def link_options(args, *, selectors=False, core_only=False):
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
    libraries = [args.build / "lib/Core/libctbrowser-core.a"]
    if not core_only:
        libraries.insert(0, args.build / "lib/DOM/libctbrowser-dom.a")
    if selectors:
        libraries.insert(0, args.build / "lib/Style/libctbrowser-style.a")
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
        # The runtime header's helpers are the shared browser API's callers
        # (toggle_class -> ctbrowser::toggle_token, and so on); the program
        # must reach the DOM through them and nothing else.
        if "action" in name and (
            "ctnative::toggle_class" not in cpp or "ctnative::set_attribute" not in cpp
        ):
            raise RuntimeError(f"{name}/{mode}: action bypasses the shared browser API\n{cpp}")
        if name.startswith(("forced-", "attributes-", "attribute-noops-", "explicit-forces-")):
            if "ctnative::toggle_attribute" not in cpp:
                raise RuntimeError(f"{name}/{mode}: attribute toggle bypasses the shared DOM API")
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
    entries = (
        (
            "observed-remove-result",
            "function remove(element) { return element.removeAttribute('disabled'); }",
            1,
            'assert(doc.set_attribute(button, atoms.intern("disabled"), ""));\n'
            "static_assert(std::is_void_v<decltype(@ENTRY@(element))>);\n"
            "@ENTRY@(element);\n"
            "(void)pressed; (void)alias; (void)foreign;\n"
            'assert(!doc.read().has_attribute(button, atoms.intern("disabled")));',
        ),
        ("action", ACTION, 1, ACTION_CHECKS),
        ("identity", IDENTITY, 2, IDENTITY_CHECKS),
        ("contains", CONTAINS, 2, CONTAINS_CHECKS),
        ("matches", MATCHES, 1, MATCHES_CHECKS),
        ("closest", CLOSEST, 2, CLOSEST_CHECKS),
        ("closest-state", CLOSEST_STATE, 2, CLOSEST_STATE_CHECKS),
        ("closest-scope", CLOSEST_SCOPE, 1, CLOSEST_SCOPE_CHECKS),
        ("closest-misses", CLOSEST_MISSES, 2, CLOSEST_MISSES_CHECKS),
        ("invalid-selector", INVALID_SELECTOR, 1, INVALID_SELECTOR_CHECKS),
        (
            "invalid-closest",
            INVALID_SELECTOR.replace(".matches(", ".closest("),
            1,
            INVALID_SELECTOR_CHECKS,
        ),
        ("reserved-name", IDENTITY.replace("same(", "_script_("), 2, IDENTITY_CHECKS),
        ("label", LABEL, 1, LABEL_CHECKS),
        ("invalid-token", INVALID_TOKEN, 1, INVALID_TOKEN_CHECKS),
        ("forced", FORCED, 1, FORCED_CHECKS),
        ("attributes", ATTRIBUTES, 1, ATTRIBUTE_CHECKS),
        ("attribute-noops", ATTRIBUTE_NOOPS, 1, ATTRIBUTE_NOOP_CHECKS),
        ("explicit-forces", EXPLICIT_FORCES, 1, EXPLICIT_FORCE_CHECKS),
        (
            "written-undefined",
            EXPLICIT_FORCES.replace(
                "  element.classList", "  undefined = true;\n  element.classList", 1
            ),
            1,
            EXPLICIT_FORCE_CHECKS,
        ),
        ("invalid-attribute", INVALID_ATTRIBUTE, 1, INVALID_ATTRIBUTE_CHECKS),
    )
    for name, source, parameters, checks in entries:
        ir, contract = prepare(args, name, source, parameters)
        prepared[name] = ir, contract
        for optimize in (False, True):
            label = f"{name}-{'optimized' if optimize else 'unoptimized'}"
            native = lower(args, ir, contract, label, optimize=optimize)
            selected_includes, selected_libraries = (
                link_options(args, selectors=True)
                if ".matches(" in source or ".closest(" in source
                else (includes, libraries)
            )
            standalone(
                args, native, label, checks, compilers, selected_includes, selected_libraries
            )

    # The frontend erases sloppy writes to the fixed undefined binding. An
    # explicit source IR write must still withdraw the complete DOM proof.
    ir, manifest = prepared["written-undefined"]
    if 'ctjs.store_global "undefined"' in ir.read_text():
        raise RuntimeError("sloppy undefined assignment unexpectedly survived import")
    text, count = re.subn(
        r'^( +)(%[-\w.$]+) = ctjs\.load_global "undefined"[^\n]*',
        lambda match: match[0] + f'\n{match[1]}ctjs.store_global "undefined", {match[2]}',
        ir.read_text(),
        count=1,
        flags=re.M,
    )
    if count != 1:
        raise RuntimeError("raw undefined store control lost its source load")
    written = args.work / "raw-written-undefined.mlir"
    written.write_text(text)
    manifest = dict(manifest, module_sha256=fingerprint(args.opt, written))
    for optimize in (False, True):
        diagnostic = lower(
            args,
            written,
            manifest,
            f"raw-written-undefined-{optimize}",
            optimize=optimize,
            success=False,
        )
        if "DOM" not in diagnostic:
            raise RuntimeError(f"raw undefined store lost its DOM refusal\n{diagnostic}")

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
    refusals = [
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
        (
            "closest-explicit-null",
            "function invalid(element) { return element.closest('.btn') === null; }",
            "DOM",
        ),
        (
            "borrowed-closest-return",
            "function invalid(element) { return element.closest('.btn'); }",
            "DOM",
        ),
        (
            "nullable-closest-receiver",
            "function invalid(element) { return element.closest('.btn').matches('.btn'); }",
            "DOM",
        ),
        (
            "nullable-contains-argument",
            "function invalid(element) { return element.contains(element.closest('.btn')); }",
            "DOM",
        ),
        (
            "closest-retention",
            "function invalid(element) { element.saved = element.closest('.btn'); return true; }",
            "DOM",
        ),
        (
            "selector-coercion",
            "function invalid(element) { return element.matches(element); }",
            "DOM",
        ),
        (
            "contains-coercion",
            "function invalid(element) { return element.contains('button'); }",
            "DOM",
        ),
        (
            "selector-lost-receiver",
            "function invalid(element) { const query = element.matches; return query('.btn'); }",
            "DOM",
        ),
        (
            "invoked-entry",
            ACTION + "toggle({});\n",
            "DOM entry initialization contains an observable source operation",
        ),
        (
            "replaced-undefined",
            EXPLICIT_FORCES.replace("explicitForces(", "undefined("),
            "DOM entry initialization has an unknown or repeated export",
        ),
        (
            "unknown-global-force",
            EXPLICIT_FORCES.replace("undefined", "unknownForce"),
            "DOM",
        ),
        (
            "retained-key",
            "function retain(element) { const data = new Map(); data.set(element, true); return true; }",
            "DOM",
        ),
        (
            "retained-field",
            "function retain(element) { element.saved = element; return true; }",
            "DOM",
        ),
    ]
    for method in (
        "classList.toggle",
        "toggleAttribute",
        "hasAttribute",
        "removeAttribute",
        "contains",
        "matches",
        "closest",
    ):
        label = method.replace(".", "-")
        toggle = method in ("classList.toggle", "toggleAttribute")
        extra = "'active', true, false" if toggle else "'active', true"
        for arity, arguments in (("missing", ""), ("extra", extra)):
            refusals.append(
                (
                    f"{label}-{arity}",
                    f"function invalid(element) {{ element.{method}({arguments}); return true; }}",
                    "DOM",
                )
            )
        if toggle:
            for force_label, force in (("element", "element"), ("string", "'false'")):
                refusals.append(
                    (
                        f"{label}-force-{force_label}",
                        f"function invalid(element) {{ return element.{method}('active', {force}); }}",
                        "DOM",
                    )
                )
    for name, source, reason in refusals:
        ir, manifest = prepare(args, name, source, 1)
        for optimize in (False, True):
            diagnostic = lower(
                args, ir, manifest, f"{name}-{optimize}", optimize=optimize, success=False
            )
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
        f"native DOM: {len(entries)} action/identity/attribute/query/error entries, "
        f"both policies/layouts, GCC/Clang, DOM/Core and selector-only Style; {len(refusals) + 7} refusal controls"
    )


if __name__ == "__main__":
    main()
