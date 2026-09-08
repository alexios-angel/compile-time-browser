#!/usr/bin/env python3
"""Check associative lookup, ordered fallback, aliases, and owning Map values."""

import argparse
import importlib.util
import os
from pathlib import Path
import re
import shutil
import subprocess

spec = importlib.util.spec_from_file_location(
    "boundary", Path(__file__).resolve().parents[1] / "native-export-boundary.py")
boundary = importlib.util.module_from_spec(spec)
spec.loader.exec_module(boundary)

NODE_GLOBALS = r"""const fs = require('node:fs');
const vm = require('node:vm');
const context = vm.createContext({});
vm.runInContext(fs.readFileSync(process.argv[1], 'utf8'), context);
for (const name of Object.keys(context).sort()) {
    if (typeof context[name] === 'number') console.log(name + '=' + context[name]);
}
"""


def run(command, *, environment=None):
    result = subprocess.run(command, text=True, capture_output=True, timeout=120,
                            env=environment)
    if result.returncode:
        raise RuntimeError(f"{command!r}\n{result.stdout}{result.stderr}")
    return result.stdout


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fixtures", type=Path, required=True)
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--translate", required=True)
    parser.add_argument("--opt", required=True)
    parser.add_argument("--node")
    args = parser.parse_args()
    args.work.mkdir(parents=True, exist_ok=True)
    tests = Path(__file__).resolve().parents[2]
    node = boundary.node_executable(args)
    reference = boundary.reference_tool(args.opt)
    nm = shutil.which("nm")
    if not nm:
        raise RuntimeError("Map representation regression requires nm")
    compilers = []
    for choices in [("g++-13", "g++"), ("clang++-18", "clang++")]:
        compiler = next((shutil.which(name) for name in choices if shutil.which(name)), None)
        if not compiler:
            raise RuntimeError("Map representation regression requires " + " or ".join(choices))
        compilers.append(compiler)
    cases = [
        ("associative", False, "identityResult=42\nlifetimeResult=42\nmutationResult=1042\nsameValueZeroResult=5829\n"),
        ("ordered", False, "orderResult=30102188\nprojectionResult=30\nstringOrderResult=37\nzeroResult=1\n"),
        ("ordered", True, "orderResult=30102188\nprojectionResult=30\nstringOrderResult=37\nzeroResult=1\n"),
        ("payloads", False, "booleanResult=63\nnestedResult=1\nstringResult=63\n"),
        ("string-values", False, "keyResult=3\nsnapshotResult=3427\n"),
        ("string-values", True, "keyResult=3\nsnapshotResult=3427\n"),
        ("mixed-values", False, "result=2\n"),
    ]
    for fixture, deforest, expected in cases:
        name = fixture + ("-deforested" if deforest else "")
        if fixture in {"payloads", "string-values", "mixed-values"}:
            js = args.fixtures / (fixture + ".js")
            assert run([node, "-e", NODE_GLOBALS, str(js)]) == expected
            assert run([str(reference), str(js)]) == expected
        module = args.work / (name + ".mlir")
        run(["cmake", f"-DTRANSLATE={args.translate}", f"-DOPT={args.opt}",
             f"-DSOURCE={args.fixtures / (fixture + '.js')}", f"-DOUTPUT={module}",
             "-DOPTIMIZE=OFF", f"-DDEFOREST={'ON' if deforest else 'OFF'}",
             "-P", str(tests / "native-pipeline.cmake")])
        if deforest:
            projection = "true" if fixture == "string-values" else "false"
            assert f'ctnative::map_snapshot_at<{projection}>' in module.read_text()
        elif fixture == "ordered":
            # Common helper names do not license ordered projections if the
            # matching storage contract has been removed or replaced.
            forged = args.work / "missing-order-contract.mlir"
            original = module.read_text()
            altered = "\n".join(line for line in original.splitlines()
                                if "ctcompile: insertion order is observable" not in line) + "\n"
            assert altered != original
            forged.write_text(altered)
            refused = run([args.opt, "--ctnative-deforest", str(forged)])
            assert 'ctnative.deforest_reason = "native Map runtime contract is not present"' in refused
            assert 'ctnative::map_snapshot_at<' not in refused
        deduced = args.work / (name + "-deduced.mlir")
        run([args.opt, "--ctnative-print-deduced", "--mlir-print-debuginfo", str(module),
             "-o", str(deduced)])
        for label, ir in [("plain", module), ("deduced", deduced)]:
            cpp = run([args.translate, "--mlir-to-cpp", str(ir)])
            assert "using string_to_number_map = number_map<std::string>;" in cpp
            assert "inline std::shared_ptr<string_to_number_map> make_string_to_number_map()" in cpp
            if fixture in {"associative", "ordered"}:
                assert "ctnative::make_string_to_number_map()" in cpp
            assert "map->find(key)" in cpp
            assert "ctbrowser::script::" not in cpp
            if fixture in {"associative", "payloads", "mixed-values"}:
                assert "using map_storage = std::map<K, V, map_key_less<K>>;" in cpp
                assert "#include <map>" in cpp
                assert "map->entries" not in cpp and "struct map_storage" not in cpp
                if fixture == "associative":
                    assert "ctnative::string_to_number_map" in cpp
            else:
                assert "struct map_storage" in cpp and "std::vector<std::pair<K, V>> entries;" in cpp
                assert "std::map<" not in cpp
            if fixture == "payloads":
                assert "ctnative::make_map<double, bool>()" in cpp or "ctnative::make_map<js_num, bool>()" in cpp
                assert "ctnative::make_map<std::string, std::string>()" in cpp
                assert "ctnative::make_map<bool, std::string>()" in cpp
                assert "nullable_string map_get(" in cpp
            if fixture == "mixed-values":
                assert "ctnative::make_map<double, std::variant<bool, std::string>>()" in cpp
                assert len(re.findall(r"\bctnative::map_set\(", cpp)) == 2
                assert "ctnative::map_size(" in cpp
            if fixture == "string-values":
                assert "std::vector<std::string> map_values(" in cpp
                # String values stay owning snapshots even while a separate
                # numeric key projection is safely deforested.
                assert re.search(r"= ctnative::map_values\(", cpp)
            source = args.work / f"{name}-{label}.cpp"
            source.write_text(cpp)
            for index, compiler in enumerate(compilers):
                binary = (args.work / f"{name}-{label}-{index}").resolve()
                run([compiler, "-std=c++23", "-O2", "-Wall", "-Wextra", "-Werror",
                     "-Wconversion", "-pedantic", "-ffp-contract=off", str(source), "-o", str(binary)])
                actual = run([str(binary)])
                assert actual == expected, (name, label, actual, expected)
                assert "ctbrowser::script::" not in run([nm, "-C", str(binary)])
            if label == "plain":
                environment = dict(os.environ, ASAN_OPTIONS="detect_stack_use_after_return=1:detect_leaks=1",
                                   UBSAN_OPTIONS="halt_on_error=1")
                binary = source.with_suffix(".sanitized").resolve()
                run([compilers[1], "-std=c++23", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                     "-Wconversion", "-pedantic", "-ffp-contract=off", "-fno-omit-frame-pointer",
                     "-fsanitize=address,undefined", "-fsanitize-address-use-after-scope",
                     str(source), "-o", str(binary)])
                assert run([str(binary)], environment=environment) == expected
    for fixture in ["boolean-values"]:
        source = (args.fixtures / (fixture + ".js")).read_text()
        _, prepared, count = boundary.prepare(args, fixture, source)
        output, _ = boundary.native(args, prepared, fixture, count)
        before, after = prepared.read_text(), output.read_text()
        def calls(text):
            return [call.strip() for call in re.findall(
                r"^\s*(?:%[-\w.$]+ = )?ctjs\.(call(?:_direct)? [^\n{]+)", text, re.M)]
        assert calls(before) == calls(after), fixture
    print("native Maps: 14 observations, associative/ordered/deforested, GCC/Clang, "
          "plain/deduced, ASan/UBSan; Bool/string payloads and owning string snapshots "
          "and literal mixed storage agree with Node/interpreter; boolean snapshot refusal")


if __name__ == "__main__":
    main()
