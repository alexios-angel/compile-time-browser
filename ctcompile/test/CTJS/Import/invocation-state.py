#!/usr/bin/env python3
"""Pin importer call snapshots while source throwing calls remain refused."""

import argparse
import importlib.util
from pathlib import Path
import re
import struct


spec = importlib.util.spec_from_file_location(
    "exceptions", Path(__file__).parents[2] / "CTNative/Lowering/native-exceptions.py")
exceptions = importlib.util.module_from_spec(spec)
spec.loader.exec_module(exceptions)
run = exceptions.run
SSA = re.compile(r"%[\w.$-]+")
EDGE = re.compile(r"\^(\w+)\(([^)]*)\)")
CASES = {
    "assignment": (3, {"caught42": 42, "normal20": 20}, [10]),
    "sequential": (3, {"caught52": 52, "normal20": 20}, [0, ("call", 0)]),
    "argument": (3, {"caught28": 28, "normal20": 20}, [14]),
    "receiver": (7, {"caught42": 42, "normal20": 20, "order": 12345,
                     "throwingOrder12345": 12345, "normalOrder12345": 12345},
                 [10, 10, 10, 10]),
}


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def guarded_body(text):
    functions = re.split(r"(?=^  ctjs\.func\b)", text, flags=re.M)
    found = [body for body in functions if re.match(
        r"  ctjs\.func (?:private )?@guarded\$\d+\(", body)]
    require(len(found) == 1, "source guarded function was lost")
    return found[0]


def snapshots(text):
    """Follow only this fixture's straight normal CFG; never execute a helper."""
    body = guarded_body(text)
    blocks = {"entry": ([], [])}
    block = blocks["entry"]
    for line in body.splitlines()[1:]:
        header = re.match(r"  \^(\w+)\(([^)]*)\):", line)
        if header:
            block = (SSA.findall(header[2]), [])
            blocks[header[1]] = block
        elif line.startswith("    "):
            block[1].append(line.strip())
    width = int(re.search(r"ctjs\.frame_enter (\d+)", body)[1])
    landings = [entry for entry in blocks.values()
                if any("ctjs.catch_land" in line for line in entry[1])]
    require(len(landings) == 1, "expected one catch landing")
    landing_args, landing_ops = landings[0]
    caught_add = next(line for line in landing_ops if "ctjs.binary add" in line)
    mark = SSA.findall(caught_add.split("ctjs.binary add", 1)[1])[0]
    require(mark in landing_args, "catch lost its saved mark register")
    mark_slot = landing_args.index(mark)
    signature = re.search(r"@guarded\$\d+\(([^)]*)\)", body)[1]
    values = {argument: ("parameter", index)
              for index, argument in enumerate(SSA.findall(signature))}
    calls, events = [], []
    current, incoming, seen = "entry", [], set()
    while True:
        require(current not in seen, "fixture unexpectedly contains a loop")
        seen.add(current)
        arguments, operations = blocks[current]
        require(len(arguments) == len(incoming), "register edge lost its width")
        values.update(zip(arguments, incoming))
        call = None
        for line in operations:
            definition = re.match(r"(%[\w.$-]+) = (.*)", line)
            if definition:
                result, op = definition.groups()
                number = re.match(r"ctjs\.constant #ctjs\.number<(\d+)>", op)
                boolean = re.match(r"ctjs\.constant #ctjs\.boolean<(true|false)>", op)
                global_name = re.match(r'ctjs\.load_global "([^"]+)"', op)
                if number:
                    values[result] = struct.unpack("d", struct.pack("Q", int(number[1])))[0]
                elif boolean:
                    values[result] = ("boolean", boolean[1])
                elif op.startswith("ctjs.constant #ctjs.undefined"):
                    values[result] = ("undefined",)
                elif global_name:
                    values[result] = ("global", global_name[1])
                elif op.startswith("ctjs.get_property "):
                    obj, key = [values[value] for value in SSA.findall(op)]
                    values[result] = ("property", obj, key)
                    events.append("get_property")
                elif op.startswith(("ctjs.call ", "ctjs.call_direct ")):
                    operands = [values[value] for value in SSA.findall(op)]
                    if op.startswith("ctjs.call_direct "):
                        receiver, new_target, callee, *args = operands
                        target = re.search(r"@([^($]+)\$\d+", op)[1]
                        require(callee == ("global", target), "resolver changed actual callee")
                        require(new_target == ("undefined",), "ordinary call gained new.target")
                    else:
                        callee, receiver, *args = operands
                    require(call is None, "multiple calls share one status edge")
                    index = len(calls)
                    values[result] = ("call", index)
                    call = {"result": result, "callee": callee, "receiver": receiver,
                            "args": args, "index": index}
                    calls.append(call)
                    events.append("call:" + (callee[1] if callee[0] == "global" else "property"))
                elif not op.startswith("ctjs.frame_enter "):
                    raise RuntimeError("unexpected fixture computation: " + line)
            if line.startswith("ctjs.check "):
                edges = EDGE.findall(line)
                require(len(edges) == 2, "check lost its normal or unwind edge")
                normal = SSA.findall(edges[0][1].split(":", 1)[0])
                unwind = SSA.findall(edges[1][1].split(":", 1)[0])
                require(len(normal) == len(unwind) == width, "check lost complete registers")
                if call is not None:
                    call["check"] = line
                    require(unwind == arguments, "call unwind is not its pre-instruction state")
                    require(call["result"] not in unwind, "unwind reads an unavailable call result")
                    require(normal.count(call["result"]) == 1, "normal edge lost call result")
                    call["before"] = values[unwind[mark_slot]]
                    call["normal_mark"] = values[normal[mark_slot]]
                    require(call["normal_mark"] == call["before"],
                            "call published its assignment before normal continuation")
            if line.startswith(("ctjs.check ", "ctjs.push_handler ", "cf.br ")):
                edge = EDGE.search(line)
                require(edge is not None, "fixture edge lost its registers")
                current = edge[1]
                incoming = [values[value] for value in SSA.findall(edge[2].split(":", 1)[0])]
                break
            if line.startswith("ctjs.return "):
                returned = values[SSA.findall(line)[0]]
                require(returned == ("call", len(calls) - 1),
                        "completed assignment did not publish the last normal result")
                return calls, events
        else:
            raise RuntimeError("fixture normal path has no successor")


def check_state(text, name, before):
    calls, events = snapshots(text)
    require([call["before"] for call in calls] == before, name + ": wrong pre-call mark")
    if name == "receiver":
        require(events == ["call:methodReceiver", "call:methodKey", "get_property",
                           "call:methodArgument", "call:property"],
                "receiver/key/getter/argument/call evaluation order changed")
        require(calls[-1]["callee"] == ("property", ("call", 0), ("call", 1)) and
                calls[-1]["receiver"] == ("call", 0) and
                calls[-1]["args"] == [("call", 2)], "method call lost its receiver or arguments")
    else:
        require(all(call["callee"] == ("global", "choose") and
                    call["receiver"] == ("undefined",) for call in calls),
                name + ": direct call boundary changed")
        expected_args = [[("parameter", 3), 14]] if name == "argument" else (
            [[("boolean", "false")], [("parameter", 3)]] if name == "sequential"
            else [[("parameter", 3)]])
        require([call["args"] for call in calls] == expected_args,
                name + ": argument evaluation changed")
    return calls


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--translate", required=True)
    parser.add_argument("--opt", required=True)
    parser.add_argument("--fixtures", type=Path, required=True)
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--node")
    args = parser.parse_args()
    args.work.mkdir(parents=True, exist_ok=True)
    node = exceptions.node_executable(args)
    reference = exceptions.build_path(args.opt, "test/ctcompile-test-native-reference")
    for name, (denominator, expected, before) in CASES.items():
        source = args.fixtures / (name + ".js")
        exceptions.node_oracle(node, source.read_text(), expected, name + "/Node")
        exceptions.compare(run([str(reference), str(source)]).stdout,
                           exceptions.expected_text(expected), name + "/interpreter")
        raw, resolved, prepared = [args.work / f"{name}.{stage}.mlir"
                                   for stage in ("raw", "resolved", "prepared")]
        imported = run([args.translate, "--ctbrowser-js-to-ctjs", str(source), "-o", str(raw)])
        require(len(exceptions.CTJS_FUNCTION.findall(raw.read_text())) == denominator and
                "ctjs.skipped" not in raw.read_text() and "is not compiled:" not in imported.stderr,
                name + ": importer lost a source function")
        run([args.opt, str(raw), "--ctjs-resolve-globals", "-o", str(resolved)])
        for path in (raw, resolved):
            check_state(path.read_text(), name, before)
        run([args.opt, str(resolved), "--ctjs-lift-to-scf", "-o", str(prepared)])
        if name != "receiver":
            calls = check_state(prepared.read_text(), name, before)
            # Falsify the observation by injecting the unavailable result into
            # the actual imported call's unwind edge. This mutation is never run.
            original = calls[0]["check"]
            corrupted = re.sub(r"( caught \^\w+\()(%[\w.$-]+)",
                               lambda match: match[1] + calls[0]["result"], original, count=1)
            require(corrupted != original, "snapshot mutation did not change the call edge")
            broken = prepared.read_text().replace(original, corrupted, 1)
            try:
                check_state(broken, name, before)
            except RuntimeError:
                pass
            else:
                raise RuntimeError("unavailable-result detector accepted a corrupted snapshot")
        for mode, options in (("plain", "optimize=false"), ("default", "")):
            previous = prepared
            for repeat in range(2):
                output = args.work / f"{name}.{mode}.{repeat}.mlir"
                option = "--ctnative-lower-to-emitc" + ("=" + options if options else "")
                run([args.opt, str(previous), option, "-o", str(output)])
                exceptions.refused(previous, output, denominator, name)
                if name != "receiver":
                    require("native try/catch needs an explicit throw in its active handler"
                            in output.read_text(), name + ": throwing-call guard changed")
                    check_state(output.read_text(), name, before)
                previous = output
        print(f"{name}: {denominator} source functions, {len(expected)} reference observations; "
              "call snapshots and native refusal retained")


if __name__ == "__main__":
    main()
