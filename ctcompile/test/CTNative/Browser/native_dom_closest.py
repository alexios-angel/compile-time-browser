#!/usr/bin/env python3
"""Execute guarded closest borrows against real DOM/Style and reject unchecked uses."""

import argparse
from pathlib import Path
import re
import shutil

from CTNative.Browser import native_dom as dom
from CTNative.harness import find_compilers, run
from CTNative.HostContract.contract import fingerprint
from Target.Cpp.harness import FLAGS

SOURCE = """function activate(first, second) {
  const found = first.closest('button');
  if (!found) return false;
  const target = found.closest('[data-bs-toggle="button"]');
  if (target) {
    const peer = second.closest('button');
    if (!!peer) peer.toggleAttribute('data-hover', peer.matches(':hover'));
    target.setAttribute('data-hover', target.matches(':hover'));
    const active = target.classList.toggle('active');
    target.setAttribute('aria-pressed', active);
    return target.contains(first);
  }
  return false;
}
"""

CLIENT = r"""
#include <cassert>
#include <iostream>
#include <memory>
#include <type_traits>

int main() {
    using namespace ctbrowser;
    @SETUP@
    const auto button = doc.create_element(doc.atoms().intern("button"));
    const auto peer = other.create_element(other.atoms().intern("button"));
    const auto child = doc.create_element(doc.atoms().intern("span"));
    const auto missing = doc.create_element(doc.atoms().intern("div"));
    assert(doc.append_child(doc.root(), button));
    assert(doc.append_child(button, child));
    assert(other.append_child(other.root(), peer));
    assert(doc.append_child(doc.root(), missing));
    const auto key = doc.atoms().intern("data-bs-toggle");
    const auto classes = doc.atoms().intern("class");
    const auto pressed = doc.atoms().intern("aria-pressed");
    const auto hover = doc.atoms().intern("data-hover");
    assert(doc.set_attribute(button, key, "button"));
    assert(doc.set_attribute(button, classes, "btn"));
    assert(selectors.set_state(button, style::engine::state_hover, true));
    const element_ref input{&doc, child}, peer_input{&other, peer};
    doc.log_writes(true);
    other.log_writes(true);
    auto drain = [&] { (void)doc.take_writes(); (void)other.take_writes(); };
    drain();
    assert(!invoke(element_ref{&doc, missing}, peer_input));
    assert(doc.take_writes().empty() && other.take_writes().empty());
    for (bool active : {true, false}) {
        assert(invoke(input, peer_input));
        assert(doc.read().attribute_value(button, classes) == (active ? "btn active" : "btn"));
        assert(doc.read().attribute_value(button, pressed) == (active ? "true" : "false"));
        assert(doc.read().attribute_value(button, hover) == "true");
        assert(!other.read().has_attribute(peer, other.atoms().intern("data-hover")));
        const auto writes = doc.take_writes();
        assert(writes.size() == 3);
        assert(writes[0].node == button && writes[0].name == hover);
        assert(writes[1].node == button && writes[1].name == classes);
        assert(writes[2].node == button && writes[2].name == pressed);
        assert(other.take_writes().empty());
    }
    // The second closest has its own nullable result, even after found was guarded.
    assert(doc.remove_attribute(button, key));
    drain();
    assert(!invoke(input, peer_input));
    assert(doc.take_writes().empty() && other.take_writes().empty());
    assert(doc.set_attribute(button, key, "button"));
    assert(doc.remove_child(button));
    drain();
    assert(invoke(input, peer_input)); // Detached ancestors keep their identities.
    drain();
    assert(selectors.set_state(button, style::engine::state_hover, false));
    assert(peer_selectors.set_state(peer, style::engine::state_hover, true));
    assert(invoke(input, peer_input));
    assert(doc.read().attribute_value(button, hover) == "false");
    assert(other.read().has_attribute(peer, other.atoms().intern("data-hover")));
    drain();
    // A bad second input is rejected before any first-input source effect.
    bool invalid = false;
    try { (void)invoke(input, element_ref{&other, {}}); }
    catch (const std::exception &) { invalid = true; }
    assert(invalid && doc.take_writes().empty() && other.take_writes().empty());
    @VALIDATION@
    std::cout << "guarded closest passed\n";
}
"""

BORROWED = """atom_table atoms, other_atoms;
    document doc{atoms}, other{other_atoms};
    style::engine selectors{atoms}, peer_selectors{other_atoms};
    auto invoke = [&](element_ref first, element_ref second) {
        return @ENTRY@(first, second, selectors, peer_selectors);
    };"""
OWNED = """using session_type = @OWNER@;
    static_assert(!std::is_move_constructible_v<session_type>);
    static_assert(!std::is_copy_constructible_v<session_type>);
    session_type session;
    auto & doc = session.document();
    auto & other = doc;
    auto & selectors = session.selectors();
    auto & peer_selectors = selectors;
    auto invoke = [&](element_ref first, element_ref second) {
        return session.invoke(first, second);
    };"""
DOMAIN_CHECK = """element_ref dangling;
    {
        atom_table temporary_atoms;
        auto temporary = std::make_unique<document>(temporary_atoms);
        dangling = {temporary.get(), temporary->create_element(temporary_atoms.intern("button"))};
    }
    bool rejected = false;
    try { (void)invoke(element_ref{&doc, {}}, dangling); }
    catch (const std::invalid_argument &) { rejected = true; }
    assert(rejected && doc.take_writes().empty());"""
STYLE_CHECK = """bool rejected = false;
    try { (void)@ENTRY@(input, peer_input, selectors, selectors); }
    catch (const std::invalid_argument &) { rejected = true; }
    assert(rejected && doc.take_writes().empty() && other.take_writes().empty());"""

REFUSALS = {
    "unguarded": "return first.closest('button').matches(':hover');",
    "wrong-guard": "const a=first.closest('button'); const b=second.closest('button'); "
    "if(a) return b.matches(':hover'); return false;",
    "after-guard": "const a=first.closest('button'); if(a) a.setAttribute('ok','yes'); "
    "return a.matches(':hover');",
    "absent-arm": "const a=first.closest('button'); if(!a) return a.matches(':hover'); return false;",
    "next-unguarded": "const a=first.closest('button'); "
    "if(a) return a.closest('.missing').matches(':hover'); return false;",
    "returned": "const a=first.closest('button'); if(a) return a; return first;",
    "retained": "const a=first.closest('button'); if(a) first.saved=a; return false;",
    "joined": "const a=first.closest('button'); const b=a ? a : first; return b.matches(':hover');",
    "null-comparison": "return first.closest('button') === null;",
    "dataset": "const a=first.closest('button'); if(a) return Object.keys(a.dataset).length; return 0;",
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("translate", "opt", "clang"):
        parser.add_argument("--" + name, required=True)
    for name in ("build", "include", "work"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--nm", default=shutil.which("nm"))
    args = parser.parse_args()
    args.work = args.work.resolve()
    args.work.mkdir(parents=True, exist_ok=True)
    compilers = find_compilers()
    compilers[1] = args.clang
    includes, libraries = dom.link_options(args, selectors=True)
    ir, contract = dom.prepare(args, "closest", SOURCE, 2)
    executions = 0
    for owned in (False, True):
        manifest = dict(
            contract, provider="ctbrowser-dom-session-v1" if owned else "ctbrowser-dom-v1"
        )
        for optimize in (False, True):
            name = f"closest-{owned}-{optimize}"
            native = dom.lower(args, ir, manifest, name, optimize=optimize)
            if (
                dom.FUNCTION.search(native.read_text())
                or len(dom.NATIVE.findall(native.read_text())) != 1
            ):
                raise RuntimeError("closest did not lower the complete entry")
            entry = dom.NATIVE.findall(native.read_text())[0]
            deduced = args.work / f"{name}.deduced.mlir"
            run([args.opt, str(native), "--ctnative-print-deduced", "-o", str(deduced)])
            for mode, module in (("explicit", native), ("deduced", deduced)):
                cpp = run([args.translate, "--mlir-to-cpp", str(module)]).stdout
                if dom.VM.search(cpp) or "nullable_scalar" in cpp:
                    raise RuntimeError("closest introduced a VM or scalar value carrier")
                for call in (
                    "ctnative::Element.prototype.closest.call(",
                    "ctnative::Element.prototype.matches.call(",
                    "ctnative::toggle_class(",
                ):
                    if call not in cpp:
                        raise RuntimeError("closest bypassed the public browser helpers")
                setup = OWNED if owned else BORROWED
                if owned:
                    owner = re.findall(r"class (\w+_session) \{", cpp)
                    if len(owner) != 1:
                        raise RuntimeError("closest lost its document owner")
                    setup = setup.replace("@OWNER@", owner[0])
                client = (
                    CLIENT.replace("@SETUP@", setup)
                    .replace("@VALIDATION@", DOMAIN_CHECK if owned else STYLE_CHECK)
                    .replace("@ENTRY@", entry)
                )
                path = args.work / f"{name}.{mode}.cpp"
                path.write_text(cpp + client)
                for index, compiler in enumerate(compilers):
                    binary = path.with_suffix(f".{index}")
                    compiled = run(
                        [compiler, *FLAGS, *includes, str(path), *libraries, "-o", str(binary)]
                    )
                    if compiled.stderr or dom.VM.search(run([args.nm, "-C", str(binary)]).stdout):
                        raise RuntimeError("closest did not compile cleanly without Script/AOT")
                    if run([str(binary)]).stdout != "guarded closest passed\n":
                        raise RuntimeError("closest client did not complete")
                    executions += 1
    for name, body in REFUSALS.items():
        source = f"function refused(first, second) {{ {body} }}\n"
        refused, manifest = dom.prepare(args, name, source, 2)
        if name == "dataset":
            manifest.update(initial_intrinsics=["Object"], dataset_parameters=[0, 1])
        for optimize in (False, True):
            dom.lower(
                args, refused, manifest, name + str(optimize), optimize=optimize, success=False
            )
    for budget in (0, 1, 20, 100):
        dom.lower(args, ir, contract, f"budget-{budget}", success=False, max_steps=budget)
    stale = dict(contract, module_sha256="0" * 64)
    if "fingerprint mismatch" not in dom.lower(args, ir, stale, "stale", success=False):
        raise RuntimeError("closest accepted a stale host contract")
    # Printed proof claims do not make the wrong branch safe.
    forged, manifest = dom.prepare(
        args, "forged", "function bad(first, second) { " + REFUSALS["absent-arm"] + " }", 2
    )
    text, count = re.subn(
        r"\bmodule( attributes)? \{",
        lambda match: "module attributes {ctnative.dom_nonnull = true"
        + ("," if match.group(1) else "} {"),
        forged.read_text(),
        count=1,
    )
    if count != 1:
        raise RuntimeError("forged-proof control did not inject its attribute")
    forged.write_text(text)
    manifest["module_sha256"] = fingerprint(args.opt, forged)
    dom.lower(args, forged, manifest, "forged-proof", success=False)
    print(
        f"guarded closest: {executions} native executions, {2 * len(REFUSALS) + 6} refusal controls; DOM/Core/Style only"
    )


if __name__ == "__main__":
    main()
