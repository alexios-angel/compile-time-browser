#!/usr/bin/env python3
"""Check associative lookup, ordered fallback, aliases, and owning Map values."""

import argparse
import os
from pathlib import Path
import re
import shutil

from CTNative.harness import RUNTIME_INCLUDE, find_compilers, run
from CTNative.Exports import boundary

NODE_GLOBALS = r"""const fs = require('node:fs');
const vm = require('node:vm');
const context = vm.createContext({});
vm.runInContext(fs.readFileSync(process.argv[1], 'utf8'), context);
for (const name of Object.keys(context).sort()) {
    if (typeof context[name] === 'number') console.log(name + '=' + context[name]);
}
"""


def check_zero_keys(cpp):
    renamed, count = re.subn(r"\bmain\(\)", "ctnative_test_entry()", cpp)
    assert count == 1
    return renamed + r"""
template <class K> bool check_zero_keys(K negative) {
    const K positive{0.0};
    auto map = ctnative::make_number_map<K>();
    ctnative::map_set(map, negative, js_num{-0.0});
    for (int step = 0; step < 3; ++step) {
        const auto found = map->find(positive);
        if (found == map->end() || map->size() != 1 || !std::signbit(found->second)) {
            return false;
        }
        if constexpr (std::is_same_v<K, js_num>) {
            if (std::signbit(found->first)) { return false; }
        } else {
            if (std::signbit(std::get<js_num>(found->first))) { return false; }
        }
        if (step == 0) { ctnative::map_set(map, positive, js_num{-0.0}); }
        if (step == 1) {
            if (!ctnative::map_delete(map, positive)) { return false; }
            ctnative::map_set(map, negative, js_num{-0.0});
        }
    }
    return true;
}
int main() {
    if (ctnative_test_entry() != 0) { return 90; }
    if (!check_zero_keys(js_num{-0.0})) { return 91; }
    if (!check_zero_keys(std::variant<bool, js_num>{-0.0})) { return 92; }
    return 0;
}
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fixtures", type=Path, required=True)
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--translate", required=True)
    parser.add_argument("--opt", required=True)
    parser.add_argument("--node", required=True)
    parser.add_argument("--reference", required=True)
    args = parser.parse_args()
    args.work.mkdir(parents=True, exist_ok=True)
    tests = Path(__file__).resolve().parents[3]
    node, reference = args.node, args.reference
    nm = shutil.which("nm")
    if not nm:
        raise RuntimeError("Map representation regression requires nm")
    compilers = find_compilers()
    cases = [
        (
            "associative",
            False,
            "identityResult=42\nlifetimeResult=42\nmutationResult=1042\nsameValueZeroResult=5829\n",
        ),
        (
            "ordered",
            False,
            "orderResult=30102188\nprojectionResult=30\nstringOrderResult=37\nzeroResult=0\n",
        ),
        (
            "ordered",
            True,
            "orderResult=30102188\nprojectionResult=30\nstringOrderResult=37\nzeroResult=0\n",
        ),
        ("payloads", False, "booleanResult=63\nnestedResult=1\nstringResult=63\n"),
        ("string-values", False, "keyResult=3\nsnapshotResult=3427\n"),
        ("string-values", True, "keyResult=3\nsnapshotResult=3427\n"),
        ("mixed-values", False, "result=2\n"),
    ]
    for fixture, deforest, expected in cases:
        name = fixture + ("-deforested" if deforest else "")
        js = args.fixtures / (fixture + ".js")
        assert run([node, "-e", NODE_GLOBALS, str(js)]).stdout == expected
        assert run([str(reference), str(js)]).stdout == expected
        module = args.work / (name + ".mlir")
        run(
            [
                "cmake",
                f"-DTRANSLATE={args.translate}",
                f"-DOPT={args.opt}",
                f"-DSOURCE={args.fixtures / (fixture + '.js')}",
                f"-DOUTPUT={module}",
                "-DOPTIMIZE=OFF",
                f"-DDEFOREST={'ON' if deforest else 'OFF'}",
                "-P",
                str(tests / "CTNative/Checks/pipeline.cmake"),
            ]
        )
        if deforest:
            projection = "true" if fixture == "string-values" else "false"
            assert f"ctnative::map_snapshot_at<{projection}>" in module.read_text()
        elif fixture == "ordered":
            # Common helper names do not license ordered projections if the
            # matching storage contract has been removed or replaced.
            forged = args.work / "missing-order-contract.mlir"
            original = module.read_text()
            # The ordered contract is the define in front of the runtime include.
            altered = (
                "\n".join(
                    line
                    for line in original.splitlines()
                    if "#define CTNATIVE_ORDERED_MAPS 1" not in line
                )
                + "\n"
            )
            assert altered != original
            forged.write_text(altered)
            refused = run([args.opt, "--ctnative-deforest", str(forged)]).stdout
            assert (
                'ctnative.deforest_reason = "native Map runtime contract is not present"' in refused
            )
            assert "ctnative::map_snapshot_at<" not in refused
        deduced = args.work / (name + "-deduced.mlir")
        run(
            [
                args.opt,
                "--ctnative-print-deduced",
                "--mlir-print-debuginfo",
                str(module),
                "-o",
                str(deduced),
            ]
        )
        for label, ir in [("plain", module), ("deduced", deduced)]:
            cpp = run([args.translate, "--mlir-to-cpp", str(ir)]).stdout
            # The helpers are the runtime header's now; the program decides only
            # the storage, by the define in front of its include.
            assert '#include "ctcompile/CTNative/Runtime/ctnative.hpp"' in cpp
            if fixture in {"associative", "ordered"}:
                assert "ctnative::make_string_to_number_map()" in cpp
            assert "ctbrowser::script::" not in cpp
            if fixture in {"associative", "payloads", "mixed-values"}:
                assert "#define CTNATIVE_ORDERED_MAPS" not in cpp
                if fixture == "associative":
                    assert "ctnative::string_to_number_map" in cpp
            else:
                assert "#define CTNATIVE_ORDERED_MAPS 1" in cpp
                assert "std::map<" not in cpp
            if fixture == "payloads":
                assert (
                    "ctnative::make_map<double, bool>()" in cpp
                    or "ctnative::make_map<js_num, bool>()" in cpp
                )
                assert "ctnative::make_map<std::string, std::string>()" in cpp
                assert "ctnative::make_map<bool, std::string>()" in cpp
                # The String payload read is the runtime header's; the program
                # spells its owning carrier, not the helper's definition.
                assert "ctnative::nullable_string" in cpp
            if fixture == "mixed-values":
                assert "ctnative::make_map<double, std::variant<bool, std::string>>()" in cpp
                assert len(re.findall(r"\bctnative::map_set\(", cpp)) == 2
                assert "ctnative::map_size(" in cpp
            if fixture == "string-values":
                assert "std::vector<std::string>" in cpp
                # String values stay owning snapshots even while a separate
                # numeric key projection is safely deforested.
                assert re.search(r"= ctnative::map_values\(", cpp)
            source = args.work / f"{name}-{label}.cpp"
            source.write_text(
                check_zero_keys(cpp) if fixture in {"associative", "ordered"} else cpp
            )
            for index, compiler in enumerate(compilers):
                binary = (args.work / f"{name}-{label}-{index}").resolve()
                run(
                    [
                        compiler,
                        "-std=c++23",
                        RUNTIME_INCLUDE,
                        "-O2",
                        "-Wall",
                        "-Wextra",
                        "-Werror",
                        "-Wconversion",
                        "-pedantic",
                        "-ffp-contract=off",
                        str(source),
                        "-o",
                        str(binary),
                    ]
                )
                actual = run([str(binary)]).stdout
                assert actual == expected, (name, label, actual, expected)
                assert "ctbrowser::script::" not in run([nm, "-C", str(binary)]).stdout
            if label == "plain":
                environment = dict(
                    os.environ,
                    ASAN_OPTIONS="detect_stack_use_after_return=1:detect_leaks=1",
                    UBSAN_OPTIONS="halt_on_error=1",
                )
                binary = source.with_suffix(".sanitized").resolve()
                run(
                    [
                        compilers[1],
                        "-std=c++23",
                        RUNTIME_INCLUDE,
                        "-O1",
                        "-g",
                        "-Wall",
                        "-Wextra",
                        "-Werror",
                        "-Wconversion",
                        "-pedantic",
                        "-ffp-contract=off",
                        "-fno-omit-frame-pointer",
                        "-fsanitize=address,undefined",
                        "-fsanitize-address-use-after-scope",
                        str(source),
                        "-o",
                        str(binary),
                    ]
                )
                assert run([str(binary)], environment=environment).stdout == expected
    for fixture in ["boolean-values"]:
        source = (args.fixtures / (fixture + ".js")).read_text()
        _, prepared, count = boundary.prepare(args, fixture, source)
        output, _ = boundary.native(args, prepared, fixture, count)
        before, after = prepared.read_text(), output.read_text()

        def calls(text):
            return [
                call.strip()
                for call in re.findall(
                    r"^\s*(?:%[-\w.$]+ = )?ctjs\.(call(?:_direct)? [^\n{]+)", text, re.M
                )
            ]

        assert calls(before) == calls(after), fixture
    print(
        "native Maps: 14 observations, associative/ordered/deforested, GCC/Clang, "
        "plain/deduced, ASan/UBSan; Bool/string payloads and owning string snapshots "
        "and literal mixed storage agree with Node/interpreter; boolean snapshot refusal"
    )


if __name__ == "__main__":
    main()
