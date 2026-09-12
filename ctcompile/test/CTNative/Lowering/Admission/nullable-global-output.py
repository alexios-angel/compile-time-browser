#!/usr/bin/env python3
"""Execute nullable global observations without changing their source type proof."""

import argparse
import json
from pathlib import Path
import re
import shutil
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "Ownership"))
from native_owned_global_maps.driver_common import CONSTANT_GLOBAL_NODE, boundary, host, owned

SOURCE = r"""function scalar(which) {
    return which === 0 ? void 0 : which === 1 ? null : which === 2 ? -0 :
        which === 3 ? 0 / 0 : which === 4 ? false : true;
}
function text(which) {
    return which === 0 ? void 0 : which === 1 ? null : which === 2 ? '' : 'nan;=%\u0000';
}
function clearNumber() { changedNumber = -0; }
function clearText() { changedString = null; }
function rightGuard(value) { return value === void 0 ? 41 : value; }
function wrongGuard(value) { return value === null ? 41 : value; }
function field(which) {
    var object = {seen: 1};
    if (which) { object.hit = 7; }
    return object.hit;
}
function savedField() {
    var object = {seen: 1};
    var previous = object.later;
    object.later = 7;
    return previous;
}
var undefinedValue = scalar(0);
var nullValue = scalar(1);
var negativeZero = scalar(2);
var nanValue = scalar(3);
var falseValue = scalar(4);
var trueValue = scalar(5);
var missingString = text(0);
var nullString = text(1);
var emptyString = text(2);
var ownedString = text(3);
var changedNumber = false;
clearNumber();
var changedString = 'before';
clearText();
var uninitialized;
var before = uninitialized;
var keptNull = rightGuard(null);
var fixedUndefined = rightGuard(void 0);
var wrongUndefined = wrongGuard(void 0);
var fixedNull = wrongGuard(null);
var missingField = field(false);
var presentField = field(true);
var earlierField = savedField();
"""
EXPECTED = {
    "undefinedValue": "undefined",
    "nullValue": "null",
    "negativeZero": "-0",
    "nanValue": "nan",
    "falseValue": "false",
    "trueValue": "true",
    "missingString": "undefined",
    "nullString": "null",
    "emptyString": '""',
    "ownedString": '"nan%3B%3D%25%00"',
    "changedNumber": "-0",
    "changedString": "null",
    "uninitialized": "undefined",
    "before": "undefined",
    "keptNull": "null",
    "fixedUndefined": "41",
    "wrongUndefined": "undefined",
    "fixedNull": "41",
    "missingField": "undefined",
    "presentField": "7",
    "earlierField": "undefined",
}


def normalized(output):
    return output.replace("=-nan\n", "=nan\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--translate", required=True)
    parser.add_argument("--opt", required=True)
    parser.add_argument("--node")
    parser.add_argument("--work", type=Path, required=True)
    args = parser.parse_args()
    args.work.mkdir(parents=True, exist_ok=True)
    compilers = [
        next((shutil.which(c) for c in choices if shutil.which(c)), None)
        for choices in (("g++-13", "g++"), ("clang++-18", "clang++"))
    ]
    reference, node = boundary.reference_tool(args.opt), boundary.node_executable(args)
    nm = shutil.which("nm")
    assert all(compilers) and nm and owned.VM.search(host.run([nm, "-C", str(reference)]).stdout)
    js, prepared, count = boundary.prepare(args, "nullable-global-output", SOURCE)
    assert count == 9
    expected = "".join(f"{name}={EXPECTED[name]}\n" for name in sorted(EXPECTED))
    assert (
        host.run([node, "-e", CONSTANT_GLOBAL_NODE, str(js), json.dumps(sorted(EXPECTED))]).stdout
        == expected
    )
    oracle = host.run([str(reference), str(js)])
    assert normalized(oracle.stdout) == expected
    assert (
        "21 globals printed (6 number, 2 boolean, 2 string, 4 null, 7 undefined)" in oracle.stderr
    )

    forged = args.work / "forged-scalar-reports.mlir"
    text, reports = re.subn(
        r"^(\s*ctjs\.store_global [^\n{}]+)$",
        r'\1 {ctnative.scalar_global = "number", ctnative.inferred_result = "number", '
        r"ctnative.host_proved = true, ctnative.host_owner_proved = true}",
        prepared.read_text(),
        flags=re.M,
    )
    assert reports >= len(EXPECTED)
    forged.write_text(text)
    for policy, options in (("default", ""), ("disabled", "optimize=false")):
        outputs = []
        for name, source in (("plain", prepared), ("forged", forged)):
            output = args.work / f"{policy}.{name}.mlir"
            pipeline = f"builtin.module(ctnative-lower-to-emitc{{{options}}},{owned.CLEANUP})"
            host.run([args.opt, str(source), "--pass-pipeline=" + pipeline, "-o", str(output)])
            assert len(boundary.NATIVE.findall(output.read_text())) == count
            assert not boundary.FUNCTION.search(output.read_text())
            assert not boundary.REFUSAL.search(output.read_text())
            cpp = host.run([args.translate, "--mlir-to-cpp", str(output)]).stdout
            outputs.append(
                "\n".join(line for line in cpp.splitlines() if not line.startswith("// ctcompile:"))
            )
            if name == "plain":
                native = output
        assert outputs[0] == outputs[1], "forged report changed independently typed native output"
        deduced = args.work / f"{policy}.deduced.mlir"
        host.run([args.opt, str(native), "--ctnative-print-deduced", "-o", str(deduced)])
        for mode, ir in (("explicit", native), ("deduced", deduced)):
            cpp = host.run([args.translate, "--mlir-to-cpp", str(ir)]).stdout
            assert not owned.VM.search(cpp)
            assert "ctnative::print_scalar(" in cpp and "ctnative::nullable_string" in cpp
            source = args.work / f"{policy}.{mode}.cpp"
            source.write_text(cpp)
            for index, compiler in enumerate(compilers):
                binary = source.with_suffix(f".{index}").resolve()
                host.run([compiler, *owned.FLAGS, str(source), "-o", str(binary)])
                assert not owned.VM.search(host.run([nm, "-C", str(binary)]).stdout)
                assert normalized(host.run([str(binary)]).stdout) == expected
    print(
        "nullable globals: 21 typed observations, Null/Undefined guards, mixed stores, "
        "forged reports, default/disabled, explicit/deduced, GCC/Clang and no VM"
    )


if __name__ == "__main__":
    main()
