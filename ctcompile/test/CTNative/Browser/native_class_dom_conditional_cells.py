#!/usr/bin/env python3
"""Scalarize a single conditional local store without losing its computed value."""

import itertools
import json
import re

from CTNative.Browser import native_dom as dom
from CTNative.Browser import native_class_dom_callbacks as callbacks
from CTNative.Browser.native_class_dom_parameter_defaults import budget_boundary, root_variants
from CTNative.Browser.native_dom_boolean_cells import source_operations
from CTNative.harness import run

# The shadowed callback formal forces conservative boxing without capturing
# the outer value. Its computed assignment and every read stay in one arm.
METHOD = """class Item {
  run(target, enabled) {
    let value;
    const label = 'done';
    const local = value => {
      value.setAttribute('helper', label);
      return true;
    };
    if (enabled) {
      value = target.hasAttribute('ready');
      target.setAttribute('inside', value);
      return local(target);
    }
    return true;
  }
}"""
CALLS = """
const item = new Item();
const first = item.run(element, false);
const second = item.run(other, true);
const third = item.run(element, true);
return first && second && third;
"""
STORE = "value = target.hasAttribute('ready');"
READ = "target.setAttribute('inside', value);"
CASES = {
    "then": METHOD,
    "else": METHOD.replace("if (enabled) {", "if (!enabled) { return true; } else {"),
    "nested-store": METHOD.replace(
        "if (enabled) {", "if (enabled) { if (target.hasAttribute('ready')) {"
    ).replace("return local(target);", "return local(target); }"),
    "nested-read": METHOD.replace(
        READ,
        "if (target.hasAttribute('ready')) { "
        + READ
        + " } else { target.setAttribute('absent', value); }",
    ),
    "effect-snapshot": METHOD.replace(
        STORE,
        "value = (target.setAttribute('computed', 'yes'), target.hasAttribute('ready'));"
        " target.setAttribute('ready', 'yes');",
    ),
}
BAD = {
    "second-write": METHOD.replace(STORE, STORE + " value = false;"),
    "other-arm-write": METHOD.replace(
        "    return true;\n  }", "    value = false; return true;\n  }"
    ),
    "initial-value": METHOD.replace("let value;", "let value = false;"),
    "before-read": METHOD.replace(STORE, READ + STORE),
    "nested-before-read": METHOD.replace(
        STORE, "if (target.hasAttribute('ready')) { " + READ + " } " + STORE
    ),
    "after-join-read": METHOD.replace(
        "    return true;\n  }", "    " + READ + " return true;\n  }"
    ),
    "sibling-read": METHOD.replace(
        "return local(target);\n    }", "return local(target);\n    } else { " + READ + " }"
    ),
    "captured": METHOD.replace("local = value =>", "local = node =>").replace(
        "value.setAttribute('helper', label)", "node.setAttribute('helper', value)"
    ),
    "hidden-writer": METHOD.replace(
        "const label", "const change = () => { value = false; }; const label"
    ),
    "descendant-writer": METHOD.replace(
        "const label",
        "const change = () => { const nested = () => { value = false; }; nested(); }; const label",
    ),
    "loop-store": METHOD.replace("if (enabled)", "while (enabled)").replace(
        "return local(target);", "local(target); enabled = target.hasAttribute('again');"
    ),
    "loop-read": METHOD.replace(
        READ,
        "while (target.hasAttribute('again')) { " + READ + " target.removeAttribute('again'); }",
    ),
    "effect-before": METHOD.replace(STORE, "ambient(); " + STORE),
    "effect-producer": METHOD.replace(STORE, "value = ambient();"),
    "effect-after": METHOD.replace(STORE, STORE + " ambient();"),
    "escaped-callback": METHOD.replace(
        "return local(target);", "target.saved = local; return true;"
    ),
    "timer": METHOD.replace("return local(target);", "setTimeout(local, 0); return true;"),
}
SCENARIOS = [(first, second, False) for first, second in itertools.product((False, True), repeat=2)]
SCENARIOS += [(False, False, True), (True, True, True)]


def shape(text):
    for store in re.finditer(r"^( {6,})ctjs\.cell_set (%\w+), (%\w+)", text, re.M):
        start = text.rfind("\n  ctjs.func ", 0, store.start())
        end = text.find("\n  }", store.end())
        body = text[start:end]
        cell = store[2]
        made = re.search(rf"^    {re.escape(cell)} = ctjs\.create_cell (%\w+)", body, re.M)
        if not made or not re.search(
            rf"{re.escape(made[1])} = ctjs\.constant #ctjs.undefined", body
        ):
            continue
        assert len(re.findall(rf"ctjs\.cell_set {re.escape(cell)},", body)) == 1, body
        assert re.search(rf"ctjs\.cell_get {re.escape(cell)}\b", text[store.end() : end]), body
        assert not re.search(rf"ctjs\.create_closure[^\n]*, {re.escape(cell)}\b", body), body
        assert "scf.if" in body[: store.start() - start], body
        return cell, store, start, end
    raise AssertionError("source lost its undefined local and conditional single store")


def observations(name, first, second, aliased):
    ready = [first, first if aliased else second]
    events = [[], []]
    for selected in (1, 0):
        index = 0 if aliased else selected
        value = ready[index]
        if name == "nested-store" and not value:
            continue
        if name == "effect-snapshot":
            events[index] += [("computed", "yes"), ("ready", "yes")]
            ready[index] = True
        key = "absent" if name == "nested-read" and not value else "inside"
        events[index] += [(key, str(value).lower()), ("helper", "done")]
    return events


def checks(name):
    result = r"""
        (void)classes;
        (void)pressed;
        foreign_doc.log_writes(true);
        const auto ready = [&](document & owner, atom_table & table, node_id id, bool value) {
            if (value) { assert(owner.set_attribute(id, table.intern("ready"), "")); }
            else { (void)owner.remove_attribute(id, table.intern("ready")); }
            (void)owner.take_writes();
        };
        const auto check = [&](document & owner, atom_table & table, node_id id,
                               std::initializer_list<const char *> names) {
            const auto writes = owner.take_writes();
            assert(writes.size() == names.size());
            size_t index = 0;
            for (const auto * name : names) {
                assert(writes[index].node == id && writes[index].name == table.intern(name));
                ++index;
            }
        };
    """
    for first, second, aliased in SCENARIOS:
        result += f"ready(doc, atoms, button, {str(first).lower()});\n"
        result += f"ready(foreign_doc, foreign_atoms, other_button, {str(second).lower()});\n"
        result += "assert(@ENTRY@(element, " + ("alias" if aliased else "foreign") + "));\n"
        owners = (("doc", "atoms", "button"), ("foreign_doc", "foreign_atoms", "other_button"))
        for (owner, table, node), events in zip(owners, observations(name, first, second, aliased)):
            keys = ", ".join(json.dumps(key) for key, _ in events)
            result += f"check({owner}, {table}, {node}, {{{keys}}});\n"
            for key, value in dict(events).items():
                result += (
                    f"assert({owner}.read().attribute_value({node}, {table}.intern({json.dumps(key)}))"
                    f" == {json.dumps(value)});\n"
                )
    return result


def oracle(args, name, text):
    text += "var result = true;\n"
    for first, second, aliased in SCENARIOS:
        text += f"var first = {callbacks.ELEMENT}, second = {callbacks.ELEMENT};\n"
        if first:
            text += "first.attributes.ready = '';\n"
        if second:
            text += "second.attributes.ready = '';\n"
        text += "result = probe(first, " + ("first" if aliased else "second") + ") && result;\n"
        for variable, events in zip(
            ("first", "second"), observations(name, first, second, aliased)
        ):
            trace = "".join(key + "," for key, _ in events)
            text += f"result = result && {variable}.trace === {json.dumps(trace)};\n"
            for key, value in dict(events).items():
                text += f"result = result && {variable}.attributes.{key} === {json.dumps(value)};\n"
    path = args.work / f"{name}.oracle.js"
    path.write_text(text)
    assert run([args.reference, str(path)]).stdout == "result=true\n", name
    path.write_text(text + "console.log('result=' + result);\n")
    assert run([args.node, str(path)]).stdout == "result=true\n", name


def mutations(text):
    cell, store, start, end = shape(text)
    yield "second-write", text[: store.end()] + "\n" + store[0] + text[store.end() :]
    yield "stored-cell-identity", (
        text[: store.start()] + f"{store[1]}ctjs.cell_set {cell}, {cell}" + text[store.end() :]
    )
    yield "before-read", (
        text[: store.start()]
        + f"{store[1]}%before_store = ctjs.cell_get {cell}\n"
        + f"{store[1]}ctjs.cell_set {cell}, %before_store"
        + text[store.end() :]
    )
    made = re.search(rf"^    {re.escape(cell)} = ctjs\.create_cell (%\w+)", text[start:end], re.M)
    yield "non-undefined-initial", (
        text[: start + made.start()]
        + "    %conditional_initial = ctjs.constant #ctjs.boolean<false>\n"
        + f"    {cell} = ctjs.create_cell %conditional_initial"
        + text[start + made.end() :]
    )
    returned = re.search(r"^    ctjs\.return (%\w+)", text[start:end], re.M)
    at = start + returned.start()
    yield "after-join-read", (
        text[:at]
        + f"    %after_join = ctjs.cell_get {cell}\n    ctjs.return %after_join"
        + text[start + returned.end() :]
    )
    for name, changed in root_variants(text, cell, start).items():
        if name != "valid":
            yield name, changed


def main():
    args = dom.parse_args(__doc__, oracles=True)
    includes, libraries = dom.link_options(args)
    executions = refusals = 0
    for name, method in CASES.items():
        text = callbacks.source(method + CALLS)
        oracle(args, name, text)
        ir, contract = dom.prepare(args, name, text, 2, entry_name="probe")
        cell, _, start, _ = shape(ir.read_text())
        declared = dict(contract, initial_intrinsics=["__ctbrowser_class_defined"])
        for owned, optimize, manifest in dom.variants(declared):
            label = f"{name}-{owned}-{optimize}"
            native = dom.lower(args, ir, manifest, label, optimize=optimize, max_steps=1000000)
            dom.standalone(args, native, label, checks(name), args.compilers, includes, libraries)
            executions += 4
        dom.refuse(args, ir, contract, name + "-undeclared")
        dom.exhaust(
            args, ir, declared, name, [0, 8], "{budget} did not exhaust conditional-cell proof"
        )
        refusals += 4
        if name == "then":
            refusals += budget_boundary(args, ir, declared, subject="Class conditional-cell")
            rooted = args.work / "rooted-cell.mlir"
            rooted.write_text(root_variants(ir.read_text(), cell, start)["valid"])
            rooted_contract = dict(declared, module_sha256=dom.fingerprint(args.opt, rooted))
            for owned, optimize, manifest in dom.variants(rooted_contract):
                dom.lower(args, rooted, manifest, f"rooted-{owned}-{optimize}", optimize=optimize)
            for label, changed in mutations(ir.read_text()):
                path = args.work / f"mutated-{label}.mlir"
                path.write_text(changed)
                manifest = dict(declared, module_sha256=dom.fingerprint(args.opt, path))
                dom.refuse(args, path, manifest, label)
                refusals += 2
    for name, method in BAD.items():
        ir, contract = dom.prepare(
            args, name, callbacks.source(method + CALLS), 2, entry_name="probe"
        )
        if name.startswith("loop-"):
            assert "scf.while" in ir.read_text(), "loop refusal lost its actual loop"
        original = source_operations(args, ir.read_text()) if name.startswith("effect-") else None
        declared = dict(contract, initial_intrinsics=["__ctbrowser_class_defined"])
        for owned, optimize, manifest in dom.variants(declared):
            diagnostic = dom.lower(
                args,
                ir,
                manifest,
                f"bad-{name}-{owned}-{optimize}",
                optimize=optimize,
                success=False,
                max_steps=1000000,
            )
            if original is not None:
                assert source_operations(args, diagnostic) == original, diagnostic
            refusals += 1
    print(
        f"DOM class conditional cells: {len(CASES)} Node/VM sources, "
        f"{executions} native executions, {refusals} refusals"
    )


if __name__ == "__main__":
    main()
