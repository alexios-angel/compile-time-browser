#!/usr/bin/env python3
"""Keep sequential DOM Map keys sound for equal and distinct native inputs."""

import argparse
from pathlib import Path
import re
import shutil

from CTNative.Browser import native_class_dom_data as data
from CTNative.Browser import native_dom as dom
from CTNative.harness import run
from CTNative.HostContract import contract as host

CLIENT = (
    data.RECORD_CLIENT.replace("session.invoke(rejected)", "session.invoke(rejected, element)")
    .replace(
        "assert(caught);",
        """assert(caught);
                caught = false;
                try { (void)session.invoke(element, rejected); }
                catch (const std::exception &) { caught = true; }
                assert(caught);""",
        1,
    )
    .replace(
        "for (element_ref input : {element, alias, distinct, element})",
        """for (auto inputs : {std::array{element, alias}, std::array{element, distinct},
                            std::array{distinct, element}, std::array{alias, element}})""",
    )
    .replace("session.invoke(input)", "session.invoke(inputs[0], inputs[1])")
    .replace("second.invoke(foreign)", "second.invoke(foreign, foreign)")
    .replace("session.invoke(alias)", "session.invoke(alias, distinct)")
)

NODE = data.session.source.NODE.replace(
    "typeof value !== 'number'", "typeof value !== 'number' && typeof value !== 'undefined'"
).replace("key.startsWith('trace')", "key.startsWith('trace') || key === 'a'")


def sources():
    holder = (
        data.published_source()
        .replace("probe(element)", "probe(element, other)")
        .replace('e.set(element, "bs.item", second)', 'e.set(other, "bs.item", second)')
        .replace('e.get(element, "bs.item").n * 100', 'e.get(other, "bs.item").n * 100')
    )
    constructor = (
        data.published_source(True)
        .replace("probe(element)", "probe(element, other)")
        .replace("new Item(7, element)", "new Item(7, other)")
        .replace('e.get(element, "bs.item").n * 1000', 'e.get(other, "bs.item").n * 1000')
    )
    resident = holder.replace(
        '    e.set(other, "bs.item", second);',
        '    const fresh = {};\n    t.set("literal", savedChild);\n'
        '    t.set(fresh, savedChild);\n    e.set(other, "bs.item", second);',
    ).replace(
        "+ count;",
        '+ count + t.get("literal").get("late").n + t.get(fresh).get("late").n;',
    )
    replaced = (
        data.published_source()
        .replace("probe(element)", "probe(element, other)")
        .replace("const result = savedFirst.n", "const classResult = savedFirst.n")
        .replace(
            "  traceEntered = 1;",
            """  const savedCurrent = t.get(element);
  t.clear();
  t.set(other, savedCurrent);
  const otherSaved = t.get(other);
  t.set(other, savedChild);
  const replaced = t.get(other);
  t.delete(other);
  t.set(element, otherSaved);
  const result = classResult + otherSaved.get("bs.item").n + replaced.get("late").n
      + t.get(element).get("bs.item").n + t.delete(element);
  traceEntered = 1;""",
        )
    )
    return {
        "holder": (holder, 15927),
        "constructor": (constructor, 59112),
        "resident-keys": (resident, 15939),
        "clear-replacement": (replaced, 15951),
    }


def observe(args, name, text, suffix, expected):
    path = args.work / f"{name}.oracle.js"
    path.write_text(text + "\n" + suffix)
    for command in ([args.node, "-e", NODE, str(path)], [args.reference, str(path)]):
        actual = run(command).stdout
        if actual != expected:
            raise RuntimeError(f"{name}: alias partition observation changed: {actual!r}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("translate", "opt", "node", "reference", "clang"):
        parser.add_argument("--" + name, required=True)
    for name in ("build", "include", "work"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--nm", default=shutil.which("nm"))
    args = parser.parse_args()
    args.work = args.work.resolve()
    args.work.mkdir(parents=True, exist_ok=True)
    executions = refusals = 0
    variants = sources()
    for label, (text, expected) in variants.items():
        name = "class-dom-alias-" + label
        observe(
            args,
            name,
            text,
            """var first = {}, second = {};
probe(first, first); var traceEqual = traceAfter;
probe(first, second); var traceDistinct = traceAfter;
var traceReset = traceBefore === undefined;
probe(second, first); var traceReverse = traceAfter;
probe(first, first); var traceRepeat = traceAfter;
""",
            f"traceAfter={expected}\ntraceBefore=undefined\ntraceDistinct={expected}\n"
            f"traceEntered=1\ntraceEqual={expected}\ntraceOther={expected}\n"
            f"traceRepeat={expected}\ntraceReset=true\ntraceReverse={expected}\n",
        )
        ir, contract = data.published_prepare(args, name, text, parameters=2)
        prepared = data.classes.prepare(args, name, ir, contract, success=True)
        body = prepared.read_text()
        if body.count("ctjs.construct") != 5 or len(dom.FUNCTION.findall(body)) != 8:
            raise RuntimeError(f"{name}: preparation lost original class, Map or function owners")
        checked = dict(contract, module_sha256=host.fingerprint(args.opt, prepared))
        _, annotated = data.published_provider(args, name, prepared, checked)
        for optimize in (False, True):
            native = dom.lower(args, prepared, checked, f"{name}-{optimize}", optimize=optimize)
            executions += data.check_record_executable(
                args, f"{name}-{optimize}", native, expected, client=CLIENT
            )
        if label != "holder":
            continue
        # Carry the successful report into changed live IR. An otherwise
        # unused declared input gains no authority to be observed or returned.
        proved = annotated.read_text()
        entry = re.search(
            rf"(?ms)^  ctjs.func (?:private )?@{re.escape(checked['entry'])}"
            r"\(([^\n]+)\)[^\n]*\n.*?^  }",
            proved,
        )
        parameters = re.findall(r"(%\w+): !ctjs.value", entry[1]) if entry else []
        if len(parameters) != 5:
            raise RuntimeError("unused input controls lost the exact two-input entry")
        header = entry[0].split("\n", 1)[0] + "\n"
        observed_input = entry[0].replace(
            header,
            header
            + '    %alias_observer_key = ctjs.constant #ctjs.string<"n">\n'
            + f"    %alias_observer = ctjs.get_property {parameters[4]}[%alias_observer_key]\n",
            1,
        )
        returned_input, changed = re.subn(
            r"(?m)^    ctjs.return %\w+$", f"    ctjs.return {parameters[4]}", entry[0]
        )
        if changed != 1:
            raise RuntimeError("unused input control lost the original return")
        for suffix, modified in (
            ("input-observer", observed_input),
            ("input-return", returned_input),
        ):
            path = args.work / f"{name}-{suffix}.mlir"
            path.write_text(proved.replace(entry[0], modified, 1))
            fresh = dict(checked, module_sha256=host.fingerprint(args.opt, path))
            rejected, _, _ = host.analyze(args.opt, path, fresh, path.with_suffix(".check"))
            if (
                rejected["proved"]
                or not rejected["reason"]
                or any(slot["proved_edges"] for slot in rejected["slots"])
            ):
                raise RuntimeError(f"{suffix}: unused input retained forged provider proof")
            refusals += 1
            for optimize in (False, True):
                dom.lower(
                    args,
                    path,
                    fresh,
                    f"{name}-{suffix}-{optimize}",
                    optimize=optimize,
                    success=False,
                )
                refusals += 1
        for suffix, request, options in (
            ("stale", dict(contract, module_sha256="0" * 64), ""),
            ("no-second-input", dict(contract, element_parameters=[0]), ""),
            ("no-first-input", dict(contract, element_parameters=[1]), ""),
            ("no-map", dict(contract, initial_intrinsics=["__ctbrowser_class_defined"]), ""),
            ("budget", contract, "max-steps=0"),
            ("small-budget", contract, "max-steps=100"),
        ):
            data.classes.prepare(
                args, name + "-" + suffix, ir, request, success=False, options=options
            )
            refusals += 1
        for optimize in (False, True):
            dom.lower(
                args, ir, contract, f"{name}-raw-{optimize}", optimize=optimize, success=False
            )
            refusals += 1

    # These are the unchanged handoff witnesses. Distinct inputs reach a null
    # field read, so accepting their equal-input answer would erase a TypeError.
    rejected = {}
    for constructor in (False, True):
        text = data.source(constructor).replace("probe(element)", "probe(element, other)")
        text = (
            text.replace("new Item(7, element)", "new Item(7, other)")
            if constructor
            else text.replace(
                'e.set(element, "bs.item", second)', 'e.set(other, "bs.item", second)'
            )
        )
        name = "original-constructor-two-inputs" if constructor else "original-two-inputs"
        rejected[name] = text
        observe(
            args,
            name,
            text,
            """var a = -13, first = {}, second = {};
probe(first, first); var traceEqual = a;
a = -13;
var traceThrow = false;
try { probe(first, second); } catch (error) { traceThrow = error.name === 'TypeError'; }
var traceUnchanged = a === -13;
""",
            f"a=-13\ntraceEqual={59112 if constructor else 15927}\n"
            "traceThrow=true\ntraceUnchanged=true\n",
        )
    holder = variants["holder"][0]
    for label, effect in (
        ("live-has", "t.has(other);"),
        ("live-get", "t.get(other);"),
        ("live-delete", "t.delete(other);"),
        ("overlap-size", "t.set(other, t.get(element)); t.size;"),
    ):
        rejected[label] = holder.replace(
            '    e.set(element, "bs.item", first);',
            '    e.set(element, "bs.item", first);\n    ' + effect,
        )
    rejected["child-cleanup-only"] = holder.replace(
        'e.remove(element, "bs.item");',
        'e.remove(element, "missing"); savedChild.delete("bs.item");',
    )
    rejected["wrong-key-cleanup"] = holder.replace(
        'e.remove(element, "bs.item");',
        'e.remove(element, "missing"); t.delete("not-element");',
    )
    rejected["reoccupied-key"] = variants["clear-replacement"][0].replace(
        "  t.delete(other);", "  t.delete(other);\n  t.set(other, savedChild);"
    )
    for label, text in rejected.items():
        if text == holder:
            raise RuntimeError(f"{label}: refusal changed no source")
        name = "class-dom-alias-refused-" + label
        ir, contract = data.published_prepare(args, name, text, parameters=2)
        data.classes.prepare(
            args,
            name,
            ir,
            contract,
            success=False,
            diagnostic="class nested Map DOM inputs require an alias proof",
        )
        refusals += 1
    print(
        "DOM alias lifetimes: six Node/VM observations, four preparations, "
        f"four provider proofs, {executions} native executions, {refusals} refusals"
    )


if __name__ == "__main__":
    main()
