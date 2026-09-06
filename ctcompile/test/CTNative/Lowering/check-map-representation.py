#!/usr/bin/env python3
"""Check associative lookup, ordered fallback, aliases, and owning Map values."""

import argparse
import os
from pathlib import Path
import shutil
import subprocess


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
    args = parser.parse_args()
    args.work.mkdir(parents=True, exist_ok=True)
    tests = Path(__file__).resolve().parents[2]
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
    ]
    for fixture, deforest, expected in cases:
        name = fixture + ("-deforested" if deforest else "")
        module = args.work / (name + ".mlir")
        run(["cmake", f"-DTRANSLATE={args.translate}", f"-DOPT={args.opt}",
             f"-DSOURCE={args.fixtures / (fixture + '.js')}", f"-DOUTPUT={module}",
             "-DOPTIMIZE=OFF", f"-DDEFOREST={'ON' if deforest else 'OFF'}",
             "-P", str(tests / "native-pipeline.cmake")])
        if deforest:
            assert 'ctnative::map_snapshot_at<false>' in module.read_text()
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
            assert "ctnative::make_string_to_number_map()" in cpp
            assert "map->find(key)" in cpp
            if fixture == "associative":
                assert "using map_storage = std::map<K, V, map_key_less<K>>;" in cpp
                assert "#include <map>" in cpp
                assert "map->entries" not in cpp and "struct map_storage" not in cpp
                assert "ctnative::string_to_number_map" in cpp
            else:
                assert "struct map_storage" in cpp and "std::vector<std::pair<K, V>> entries;" in cpp
                assert "std::map<" not in cpp
            source = args.work / f"{name}-{label}.cpp"
            source.write_text(cpp)
            for index, compiler in enumerate(compilers):
                binary = (args.work / f"{name}-{label}-{index}").resolve()
                run([compiler, "-std=c++23", "-O2", "-Wall", "-Wextra", "-Werror",
                     "-Wconversion", "-pedantic", "-ffp-contract=off", str(source), "-o", str(binary)])
                actual = run([str(binary)])
                assert actual == expected, (name, label, actual, expected)
            if label == "plain":
                environment = dict(os.environ, ASAN_OPTIONS="detect_stack_use_after_return=1:detect_leaks=1",
                                   UBSAN_OPTIONS="halt_on_error=1")
                binary = source.with_suffix(".sanitized").resolve()
                run([compilers[1], "-std=c++23", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                     "-Wconversion", "-pedantic", "-ffp-contract=off", "-fno-omit-frame-pointer",
                     "-fsanitize=address,undefined", "-fsanitize-address-use-after-scope",
                     str(source), "-o", str(binary)])
                assert run([str(binary)], environment=environment) == expected
    print("native Maps: 8 observations, associative/ordered/deforested, GCC/Clang, plain/deduced, ASan/UBSan")


if __name__ == "__main__":
    main()
