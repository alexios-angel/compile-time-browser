#!/usr/bin/env python3
"""Test parameter defaults (`enabled = !0`) on a parameter no callback captures.

The frontend lowers a default to "if the argument is undefined, store the
default into the parameter's cell". The native lowering may select the value
directly, but a supplied argument, the default's side effects and any read of
the parameter before the selection (a snapshot) must stay distinct. CASES must
match Node and the reference VM natively; BAD sources and mutated IR must be
refused.
"""

import json
import re
import subprocess

from CTNative.Browser.dom import dom
from CTNative.Browser.class_dom import callbacks
from CTNative.Browser.dom.boolean.cells import source_operations
from CTNative.harness import run, threshold

# The callback's formal `enabled` shadows the method's. The frontend's
# conservative name analysis boxes the outer parameter, but the callback
# captures only the fixed `label`.
METHOD = """class Item {
  run(target, enabled = !0) {
    const label = 'done';
    const local = enabled => {
      enabled.setAttribute('inside', label);
      return enabled.getAttribute('inside') === label;
    };
    if (enabled) return local(target);
    return true;
  }
}"""
CALLS = """
  const item = new Item();
  const first = item.run(element, false);
  const second = item.run(other, true);
  const third = item.run(element);
  const fourth = item.run(other, void 0);
  const fifth = item.run(element, false);
  return first && second && third && fourth && fifth;
"""
# Selects the default by hand and records whether the argument was missing.
SNAPSHOT = METHOD.replace("enabled = !0", "enabled").replace(
    "    const label",
    "    const snapshot = enabled;\n"
    "    if (enabled === void 0) { enabled = !0; target.setAttribute('selected', 'yes'); }\n"
    "    target.setAttribute('snapshot', snapshot === void 0);\n"
    "    const label",
)
# The defaulted function is a top-level arrow function the method calls.
HELPER = (
    "const invoke = (target, enabled = !0) => {"
    + METHOD.split("run(target, enabled = !0) {", 1)[1].rsplit("}", 2)[0]
    + "}; class Item { run(target, enabled) { return invoke(target, enabled); } }"
)
CASES = {
    "original": METHOD,
    "false-default": METHOD.replace("enabled = !0", "enabled = !1"),
    "effectful": METHOD.replace(
        "enabled = !0", "enabled = (target.setAttribute('default', 'yes'), !0)"
    ).replace("const label", "target.setAttribute('body', 'yes'); const label"),
    "snapshot": SNAPSHOT,
    "helper": HELPER,
}
BAD = {
    "later-write": METHOD.replace("const label", "enabled = false; const label"),
    "conditional-write": METHOD.replace(
        "const label", "if (target.hasAttribute('change')) enabled = false; const label"
    ),
    "captured-parameter": METHOD.replace("enabled =>", "node =>")
    .replace("enabled.setAttribute", "node.setAttribute")
    .replace("enabled.getAttribute", "node.getAttribute")
    .replace("return node.getAttribute", "return enabled && node.getAttribute"),
    "hidden-writer": METHOD.replace(
        "const label", "const change = () => { enabled = false; }; const label"
    ),
    "default-effect": METHOD.replace("enabled = !0", "enabled = (ambient(), !0)"),
    "later-effect": METHOD.replace("const label", "ambient(); const label"),
    "after-store-effect": SNAPSHOT.replace("target.setAttribute('selected', 'yes')", "ambient()"),
    "loose-guard": SNAPSHOT.replace("enabled === void 0", "enabled == null"),
    "opposite-guard": SNAPSHOT.replace("enabled === void 0", "enabled !== void 0"),
    "returned-callback": METHOD.replace("return local(target)", "return local"),
    "timer": METHOD.replace("return local(target)", "return setTimeout(local, 0)"),
}
# The top-level `scf.if` that stores the default, with its two arms.
BRANCH = re.compile(
    r"^    scf\.if (%\w+) \{\n(?P<then>.*?)^    \} else \{\n(?P<else>.*?)^    \}",
    re.M | re.S,
)


def shape(text, snapshot=False):
    """Find the default selection in `text`.

    Return (the scf.if match, its cell_set match, the formal the cell starts
    from, the function's start offset). With `snapshot`, also assert that the
    snapshot read and the later effect stay where the source put them.
    """
    branch = BRANCH.search(text)
    assert branch, "default lost its top-level conditional store"
    store = re.search(r"ctjs\.cell_set (%\w+), (%\w+)", branch["then"])
    assert store, "default lost its parameter assignment"
    start = text.rfind("\n  ctjs.func ", 0, branch.start())
    prefix = text[start : branch.start()]
    initial = re.search(rf"{re.escape(store[1])} = ctjs\.create_cell (%arg\d+)\b", prefix)
    assert initial, "default cell no longer holds an original formal"
    if snapshot:
        reads = re.findall(rf"(%\w+) = ctjs\.cell_get {re.escape(store[1])}\b", prefix)
        assert len(reads) >= 2, "snapshot no longer precedes default selection"
        suffix = text[branch.end() : text.find("\n  }", branch.end())]
        assert re.search(rf"ctjs\.compare strict_eq {re.escape(reads[0])},", suffix), suffix
        assert "ctjs.call" in branch["then"][store.end() :], "post-store effect moved"
    return branch, store, initial[1], start


def observations(name):
    """Return the expected (attribute, value) writes on the first element, the
    second element, and the first element when it is passed as both arguments."""
    first, second, alias = [], [], []
    for owner, supplied in (
        (first, False),
        (second, True),
        (first, None),
        (second, None),
        (first, False),
    ):
        missing = supplied is None
        events = []
        if name == "effectful":
            if missing:
                events.append(("default", "yes"))
            events.append(("body", "yes"))
        if name == "snapshot":
            if missing:
                events.append(("selected", "yes"))
            events.append(("snapshot", str(missing).lower()))
        selected = (name != "false-default") if missing else supplied
        if selected:
            events.append(("inside", "done"))
        owner.extend(events)
        alias.extend(events)
    return first, second, alias


def checks(name):
    """Return the C++ checks that compare the native entry's DOM writes with
    observations(name)."""
    first, second, alias = observations(name)
    result = r"""
        (void)classes;
        (void)pressed;
        foreign_doc.log_writes(true);
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
        assert(@ENTRY@(element, foreign));
"""

    def inspect(owner, atoms, node, events):
        names = ", ".join(json.dumps(key) for key, _ in events)
        text = f"check({owner}, {atoms}, {node}, {{{names}}});\n"
        for key, value in dict(events).items():
            text += (
                f"assert({owner}.read().attribute_value({node}, {atoms}.intern({json.dumps(key)}))"
                f" == {json.dumps(value)});\n"
            )
        return text

    result += inspect("doc", "atoms", "button", first)
    result += inspect("foreign_doc", "foreign_atoms", "other_button", second)
    result += "assert(@ENTRY@(alias, element));\n"
    result += inspect("doc", "atoms", "button", alias)
    return result + "assert(foreign_doc.take_writes().empty());\n"


def oracle(args, name, text):
    """Check observations(name) under the reference VM and Node."""
    first, second, alias = observations(name)
    observation = text + f"var first = {callbacks.ELEMENT}, second = {callbacks.ELEMENT};\n"
    observation += "var result = probe(first, second);\n"
    for variable, events in (("first", first), ("second", second)):
        trace = "".join(key + "," for key, _ in events)
        observation += f"result = result && {variable}.trace === {json.dumps(trace)};\n"
        for key, value in dict(events).items():
            observation += (
                f"result = result && {variable}.attributes.{key} === {json.dumps(value)};\n"
            )
    trace = "".join(key + "," for key, _ in alias)
    observation += f"first.trace = ''; result = result && probe(first, first) && first.trace === {json.dumps(trace)};\n"
    path = args.work / f"{name}.oracle.js"
    path.write_text(observation)
    assert run([args.reference, str(path)]).stdout == "result=true\n", name
    path.write_text(observation + "console.log('result=' + result);\n")
    assert run([args.node, str(path)]).stdout == "result=true\n", name


def mutations(text):
    """Yield (label, IR) edits of the default selection that must be refused:
    a different guard, initial value or arm, a second write or an early read."""
    branch, store, formal, start = shape(text)
    prefix = text[start : branch.start()]
    truth = re.search(rf"{re.escape(branch[1])} = ctjs\.truthy (%\w+)", prefix)
    compare = re.search(rf"{re.escape(truth[1])} = ctjs\.compare strict_eq (%\w+), (%\w+)", prefix)
    assert compare
    changes = {
        "guard-kind": prefix.replace(compare[0], compare[0].replace("strict_eq", "eq")),
        "guard-formal": prefix.replace(
            compare[0], compare[0].replace(compare[1] + ",", formal + ",")
        ),
        "guard-value": prefix.replace(
            f"{compare[2]} = ctjs.constant #ctjs.undefined",
            f"{compare[2]} = ctjs.constant #ctjs.null",
        ),
        "nonformal-cell": prefix.replace(
            f"{store[1]} = ctjs.create_cell {formal}",
            f"%different_initial = ctjs.constant #ctjs.boolean<false>\n    {store[1]} = ctjs.create_cell %different_initial",
        ),
    }
    for name, replacement in changes.items():
        assert replacement != prefix, name
        yield name, text[:start] + replacement + text[branch.start() :]
    yield (
        "second-write",
        text[: branch.end()] + f"\n    ctjs.cell_set {store[1]}, {formal}" + text[branch.end() :],
    )
    yield (
        "read-in-default",
        text[: branch.start("then")]
        + f"      %during_default = ctjs.cell_get {store[1]}\n"
        + text[branch.start("then") :],
    )
    yield (
        "wrong-arm",
        (
            text[: branch.start("then")]
            + branch["else"]
            + text[branch.end("then") : branch.start("else")]
            + branch["then"]
            + text[branch.end("else") :]
        ),
    )
    for name, changed in root_variants(text, store[1], start).items():
        if name != "valid":
            yield name, changed


def root_variants(text, cell, start):
    """Return IR variants that add a `ctjs.root` of `cell`, keyed by name.

    "valid" roots the cell in its own frame right after creation and must
    still lower. "after-frame-exit" roots it after the frame has exited and
    "repeated-frame" roots it in a second, unrelated frame; both pass the
    verifier but must be refused.
    """
    end = text.find("\n  }", start)
    body = text[start:end]
    frame = re.search(r"(%\w+) = ctjs\.frame_enter \d+", body)[1]
    created = re.search(rf"^    {re.escape(cell)} = ctjs\.create_cell [^\n]+", body, re.M)
    exited = re.search(rf"^( +)ctjs\.frame_exit {re.escape(frame)}\b", body, re.M)
    assert created and exited

    def insert(after, extra):
        at = start + after.end()
        return text[:at] + "\n" + extra + text[at:]

    return {
        "valid": insert(created, f"    ctjs.root {cell} in {frame}"),
        "after-frame-exit": insert(exited, f"{exited[1]}ctjs.root {cell} in {frame}"),
        # A malformed-frame control independent of the root's position; the
        # after-exit variant above tests the root's lifetime.
        "repeated-frame": insert(
            created,
            "    %foreign_frame = ctjs.frame_enter 1\n"
            f"    ctjs.root {cell} in %foreign_frame\n"
            "    ctjs.frame_exit %foreign_frame",
        ),
    }


def budget_boundary(args, ir, contract, *, subject="Class parameter-default"):
    """Find the smallest host-max-steps budget at which `ir` lowers natively.

    Asserts that budget 0 refuses, 1000000 succeeds, and the budget one below
    the threshold refuses with "budget exhausted" and leaves the source
    operations unchanged. Returns the number of refusals observed.
    """
    config = args.work / "budget-boundary.json"
    config.write_text(json.dumps(contract) + "\n")
    original = source_operations(args, ir.read_text())
    refusals = 0

    def lowers(limit, *, preserve=False):
        nonlocal refusals
        output = args.work / f"budget-{limit}.mlir"
        flags = f"host-manifest={config} optimize=false host-max-steps={limit}"
        result = subprocess.run(
            [
                args.opt,
                str(ir),
                f"--pass-pipeline=builtin.module(ctnative-lower-to-emitc{{{flags}}},{dom.CLEANUP})",
                "-o",
                str(output),
            ],
            capture_output=True,
            text=True,
            timeout=120,
        )
        assert result.returncode in (0, 1), result.stderr
        text = output.read_text() if result.returncode == 0 else result.stderr
        if result.returncode == 0 and dom.NATIVE.search(text):
            assert not dom.FUNCTION.search(text), text
            return True
        assert "budget exhausted" in text, text
        if preserve or result.returncode == 0:
            assert source_operations(args, text) == original, text
        refusals += 1
        return False

    assert not lowers(0) and lowers(1000000)
    first = threshold(lowers, high=1000000)
    assert not lowers(first - 1, preserve=True) and lowers(first)
    print(f"{subject} minimum work budget: {first}; preceding cutoff preserves source")
    return refusals


def main(argv=None):
    args = dom.parse_args(__doc__, oracles=True, argv=argv)
    includes, libraries = dom.link_options(args)
    executions = refusals = 0
    for name, method in CASES.items():
        text = callbacks.source(method + CALLS)
        oracle(args, name, text)
        ir, contract = dom.prepare(args, name, text, 2, entry_name="probe")
        shape(ir.read_text(), snapshot=name == "snapshot")
        declared = dict(contract, initial_intrinsics=["__ctbrowser_class_defined"])
        for owned, optimize, manifest in dom.variants(declared):
            label = f"{name}-{owned}-{optimize}"
            native = dom.lower(args, ir, manifest, label, optimize=optimize, max_steps=1000000)
            dom.standalone(args, native, label, checks(name), args.compilers, includes, libraries)
            executions += 4  # Explicit and deduced C++, each built by both compilers.
        dom.refuse(args, ir, contract, name + "-undeclared")
        dom.exhaust(args, ir, declared, name, [0, 8], "{budget} did not exhaust default proof")
        refusals += 4  # Two undeclared-intrinsic refusals and two exhausted budgets (0, 8).
        if name == "original":
            refusals += budget_boundary(args, ir, declared)
            _, store, _, start = shape(ir.read_text())
            # The same cell, rooted in its own frame, must still lower.
            rooted = args.work / "rooted-default.mlir"
            rooted.write_text(root_variants(ir.read_text(), store[1], start)["valid"])
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
        declared = dict(contract, initial_intrinsics=["__ctbrowser_class_defined"])
        original = source_operations(args, ir.read_text()) if "effect" in name else None
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
            if "effect" in name:
                assert source_operations(args, diagnostic) == original, diagnostic
            refusals += 1
    print(
        f"DOM class parameter defaults: {len(CASES)} Node/VM sources, {executions} native executions, {refusals} refusals"
    )


def test_parameter_defaults(lit):
    main(lit.args("%dom_driver --node %node --reference %native_reference"))


if __name__ == "__main__":
    main()
