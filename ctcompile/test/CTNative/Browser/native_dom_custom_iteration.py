#!/usr/bin/env python3
"""Execute closed custom iterators with typed state and public DOM effects."""

import argparse
from pathlib import Path
import shutil

from CTNative.Browser import native_dom as dom
from CTNative.Browser.native_dom_element_iteration import INTRINSICS as SNAPSHOT_INTRINSICS
from CTNative.Browser.native_dom_json import quote
from CTNative.harness import find_compilers

INTRINSICS = [*SNAPSHOT_INTRINSICS, "Symbol", "Object"]
SOURCE = """function customElements(anchor) {
  const values = {
    [Symbol.iterator]() { return this; },
    next() {
      const done = anchor.hasAttribute('data-yielded');
      anchor.setAttribute('data-next', done);
      anchor.setAttribute('data-yielded', 'yes');
      return {done: done, value: anchor};
    },
    return() {
      anchor.setAttribute('data-closed', 'yes');
      return {};
    }
  };
  let count = 0;
  for (const node of values) {
    node.setAttribute('data-visited', 'yes');
    count++;
    @BREAK@
  }
  return count;
}
"""


def source(breaking):
    text = SOURCE.replace("@BREAK@", "if (anchor.hasAttribute('stop')) break;" if breaking else "")
    if breaking:
        text = (
            text.replace("  let count = 0;\n", "")
            .replace("    count++;\n", "")
            .replace("  return count;", "  return anchor.hasAttribute('data-visited');")
        )
    return text


def oracles(args):
    # These receivers record the same source calls. The native clients below
    # separately check those calls against public DOM and document ownership.
    script, observations = "", []
    for breaking in (False, True):
        function = "customElementsBreaking" if breaking else "customElementsNormal"
        script += source(breaking).replace("function customElements(", f"function {function}(")
        for state in ("normal", "stop", "already-yielded"):
            name = f"customObservation{len(observations)}"
            setup = ""
            if state == "stop":
                setup = "saved.stop = '';"
            elif state == "already-yielded":
                setup = "saved['data-yielded'] = 'yes';"
            script += f"""
var {name} = (function() {{
  const saved = {{}};
  {setup}
  let writes = '';
  const anchor = {{
    hasAttribute(name) {{ return name in saved; }},
    setAttribute(name, value) {{
      saved[name] = '' + value;
      writes += name + '=' + saved[name] + ';';
    }}
  }};
  const result = {function}(anchor);
  return result + ':' + writes;
}})();
"""
            expected = "0:data-next=true;data-yielded=yes;"
            if state != "already-yielded":
                expected = "1:data-next=false;data-yielded=yes;data-visited=yes;"
                expected += (
                    "data-closed=yes;"
                    if breaking and state == "stop"
                    else "data-next=true;data-yielded=yes;"
                )
            if breaking:
                expected = ("false" if state == "already-yielded" else "true") + expected[1:]
            observations.append((name, expected))
    node = args.work / "custom-iteration-node.js"
    node.write_text(script + "".join(f"console.log({name});\n" for name, _ in observations))
    expected = [value for _, value in observations]
    actual = dom.run([args.node, str(node)]).stdout.splitlines()
    if actual != expected:
        raise RuntimeError(f"Node custom iterator effects differ: {actual!r}")
    vm = args.work / "custom-iteration-vm.js"
    vm.write_text(script)
    actual = dom.run([args.reference, str(vm)]).stdout
    expected_vm = "".join(f'{name}="{quote(value)}"\n' for name, value in observations)
    if actual != expected_vm:
        raise RuntimeError(f"VM custom iterator effects differ: {actual!r}")
    return 2 * len(observations)


CHECKS = r"""
        (void)pressed;
        const auto exercise = [&](ctbrowser::document & target, node_id id, auto && call) {
            auto & names = target.atoms();
            const auto next = names.intern("data-next"), yielded = names.intern("data-yielded");
            const auto visited = names.intern("data-visited"), closed = names.intern("data-closed");
            const auto stop = names.intern("stop");
            target.log_writes(true);
            const auto clear = [&] {
                for (const auto name : {next, yielded, visited, closed, stop}) {
                    assert(target.remove_attribute(id, name));
                }
                (void)target.take_writes();
            };
            const auto check_writes = [&](std::initializer_list<atom> expected) {
                const auto writes = target.take_writes();
                assert(writes.size() == expected.size());
                std::size_t at = 0;
                for (const auto name : expected) {
                    assert(writes[at].node == id && writes[at].name == name && !writes[at].text);
                    ++at;
                }
            };
            for (const bool stopping : {false, true}) {
                clear();
                if (stopping) { assert(target.set_attribute(id, stop, "")); }
                (void)target.take_writes();
                assert(@FIRST_RESULT@);
                assert(target.read().attribute_value(id, yielded) == "yes");
                assert(target.read().attribute_value(id, visited) == "yes");
                if (@BREAKING@ && stopping) {
                    check_writes({next, yielded, visited, closed});
                    assert(target.read().attribute_value(id, next) == "false");
                    assert(target.read().attribute_value(id, closed) == "yes");
                } else {
                    check_writes({next, yielded, visited, next, yielded});
                    assert(target.read().attribute_value(id, next) == "true");
                    assert(!target.read().has_attribute(id, closed));
                }
                assert(target.remove_attribute(id, visited));
                assert(target.remove_attribute(id, closed));
                (void)target.take_writes();
                // The iterator is already exhausted. It still calls next once,
                // including that method's effects, and never enters or closes.
                assert(@SECOND_RESULT@);
                check_writes({next, yielded});
                assert(target.read().attribute_value(id, next) == "true");
                assert(!target.read().has_attribute(id, visited));
                assert(!target.read().has_attribute(id, closed));
            }
            clear();
        };
        exercise(doc, button, [&] { return @ENTRY@(alias); });
        assert(doc.remove_child(button));
        assert(!doc.read().parent(button));
        exercise(doc, button, [&] { return @ENTRY@(alias); });
        exercise(foreign_doc, other_button, [&] { return @ENTRY@(foreign); });
        assert(doc.take_writes().empty());
        const auto invalid_text = doc.create_text("invalid input");
        for (const element_ref invalid : {
                 element_ref{}, element_ref{&doc, {}}, element_ref{&doc, invalid_text},
                 element_ref{&doc, {button.slot, button.generation + 2}}}) {
            bool rejected = false;
            try { (void)@ENTRY@(invalid); }
            catch (const std::exception &) { rejected = true; }
            assert(rejected && doc.take_writes().empty());
        }
"""

OWNED_CHECKS = r"""
        @ENTRY@_session session;
        auto & owned = session.document();
        const auto owned_node = owned.create_element(owned.atoms().intern("button"));
        const element_ref input{&owned, owned_node};
        exercise(owned, owned_node, [&] { return session.invoke(input); });
        owned.set_document_element(owned_node);
        exercise(owned, owned_node, [&] { return session.invoke(input); });
        bool foreign_rejected = false;
        try { (void)session.invoke(foreign); }
        catch (const std::invalid_argument &) { foreign_rejected = true; }
        assert(foreign_rejected && owned.take_writes().empty());
"""


def refusals():
    text = source(True)
    loop = "  for (const node of values) {"
    visited = "    node.setAttribute('data-visited', 'yes');"
    variants = {
        "escaping-holder": text.replace(loop, "  external(values);\n" + loop),
        "retained-holder": text.replace(loop, "  anchor.saved=values;\n" + loop),
        "mutated-next": text.replace(loop, "  values.next=anchor;\n" + loop),
        "mutated-return": text.replace(loop, "  values.return=anchor;\n" + loop),
        "mutated-iterator": text.replace(loop, "  values[Symbol.iterator]=anchor;\n" + loop),
        "nonidentity-iterator": text.replace("return this;", "return anchor;"),
        "lexical-this-iterator": text.replace(
            "[Symbol.iterator]() { return this; }", "[Symbol.iterator]: () => this"
        ),
        "fresh-iterator-factory": text.replace(
            "return this;", "return {next() {return {done:true,value:anchor};}};"
        ),
        "primitive-next-result": text.replace("return {done: done, value: anchor};", "return 1;"),
        "primitive-return-result": text.replace("return {};", "return 1;"),
        "missing-done": text.replace(
            "return {done: done, value: anchor};", "return {value:anchor};"
        ),
        "missing-value": text.replace("return {done: done, value: anchor};", "return {done:done};"),
        "invalid-next": text.replace("next() {\n      const done", "other() {\n      const done"),
        "replaced-symbol": text.replace("  const values", "  Symbol=anchor;\n  const values"),
        "replaced-symbol-iterator": text.replace(
            "  const values", "  Symbol.iterator=anchor;\n  const values"
        ),
        "unknown-next-effect": text.replace(
            "      const done", "      external(anchor); const done"
        ),
        "escaping-value": text.replace(visited, "    anchor.saved=node;"),
        # The source compiler does not close iterators on body return or throw.
        # Keep both refused until that source completion boundary is proved.
        "body-return": text.replace(visited, "    return anchor.hasAttribute('data-visited');"),
        "body-throw": text.replace(visited, "    throw 1;"),
        # Preserve the original counted break source: completion lowering still
        # refuses its live accumulator crossing the importer's break exit.
        "counted-break-exit": SOURCE.replace("@BREAK@", "if (anchor.hasAttribute('stop')) break;"),
    }
    for helper in SNAPSHOT_INTRINSICS[4:]:
        variants["replaced-" + helper] = text.replace(
            "  const values", f"  {helper}=anchor;\n  const values"
        )
    return variants


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
    observations = oracles(args)
    compilers = find_compilers()
    compilers[1] = args.clang
    includes, libraries = dom.link_options(args)
    executions = refused = 0
    for breaking in (False, True):
        ir, contract = dom.prepare(
            args, f"custom-{breaking}", source(breaking), 1, entry_name="customElements"
        )
        contract.update(initial_intrinsics=INTRINSICS)
        for owned in (False, True):
            manifest = dict(
                contract, provider="ctbrowser-dom-session-v1" if owned else "ctbrowser-dom-v1"
            )
            for optimize in (False, True):
                name = f"custom-iteration-{breaking}-{owned}-{optimize}"
                native = dom.lower(args, ir, manifest, name, optimize=optimize)
                cpp = dom.run([args.translate, "--mlir-to-cpp", str(native)]).stdout
                if any(helper in cpp for helper in SNAPSHOT_INTRINSICS[4:]):
                    raise RuntimeError(f"{name}: custom iteration retained the VM protocol")
                checks = CHECKS + (OWNED_CHECKS if owned else "")
                checks = checks.replace("@BREAKING@", "true" if breaking else "false")
                checks = checks.replace(
                    "@FIRST_RESULT@",
                    "static_cast<bool>(call())" if breaking else "call().value() == 1",
                ).replace(
                    "@SECOND_RESULT@",
                    "!static_cast<bool>(call())" if breaking else "call().value() == 0",
                )
                dom.standalone(args, native, name, checks, compilers, includes, libraries)
                executions += 2 * len(compilers)
                controls = [("budget", manifest, 0)]
                for intrinsic in ["Symbol", "Object", *SNAPSHOT_INTRINSICS[4:]]:
                    controls.append(
                        (
                            "missing-" + intrinsic,
                            dict(
                                manifest,
                                initial_intrinsics=[
                                    item for item in INTRINSICS if item != intrinsic
                                ],
                            ),
                            None,
                        )
                    )
                for suffix, bad, budget in controls:
                    dom.lower(
                        args,
                        ir,
                        bad,
                        name + "-" + suffix,
                        optimize=optimize,
                        max_steps=budget,
                        success=False,
                    )
                    refused += 1
    for name, text in refusals().items():
        ir, contract = dom.prepare(args, name, text, 1, entry_name="customElements")
        contract.update(initial_intrinsics=INTRINSICS)
        for optimize in (False, True):
            dom.lower(
                args, ir, contract, f"refused-{name}-{optimize}", optimize=optimize, success=False
            )
            refused += 1
    print(
        f"Closed custom DOM iteration: {executions} native executions, {refused} refusals; "
        f"{observations} Node/VM source-double observations; DOM/Core only"
    )


if __name__ == "__main__":
    main()
