#!/usr/bin/env python3
"""Execute finite Map key/value storage and independently proved read types."""
import argparse
import importlib.util
import os
from pathlib import Path
import re
import shutil
import subprocess

spec = importlib.util.spec_from_file_location(
    "representation", Path(__file__).with_name("check-map-representation.py"))
representation = importlib.util.module_from_spec(spec)
spec.loader.exec_module(representation)
boundary, run = representation.boundary, representation.run


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fixtures", type=Path, required=True)
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--translate", required=True)
    parser.add_argument("--opt", required=True)
    parser.add_argument("--node")
    args = parser.parse_args()
    args.work.mkdir(parents=True, exist_ok=True)
    node = boundary.node_executable(args)
    reference = boundary.reference_tool(args.opt)
    compilers = [next((shutil.which(c) for c in choices if shutil.which(c)), None)
                 for choices in [("g++-13", "g++"), ("clang++-18", "clang++")]]
    nm = shutil.which("nm")
    assert all(compilers) and nm
    source = (args.fixtures / "mixed.js").read_text()
    for ordered in (False, True):
        name = "ordered" if ordered else "associative"
        text = source + ((args.fixtures / "snapshot.js").read_text() if ordered else "")
        js = args.work / f"{name}.js"
        js.write_text(text)
        expected = "traceBranches=23\ntraceDead=2\ntraceNumbers=1334\ntraceSaved=1\n"
        if ordered:
            expected += "traceSnapshot=1\n"
        assert run([node, "-e", representation.NODE_GLOBALS, str(js)]) == expected
        assert run([str(reference), str(js)]) == expected
        ir = args.work / f"{name}.mlir"
        run(["cmake", f"-DTRANSLATE={args.translate}", f"-DOPT={args.opt}",
             f"-DSOURCE={js}", f"-DOUTPUT={ir}", "-DOPTIMIZE=OFF", "-P",
             str(Path(__file__).resolve().parents[2] / "native-pipeline.cmake")])
        deduced = args.work / f"{name}-deduced.mlir"
        run([args.opt, "--ctnative-print-deduced", str(ir), "-o", str(deduced)])
        for label, module in [("plain", ir), ("deduced", deduced)]:
            cpp = run([args.translate, "--mlir-to-cpp", str(module)])
            assert "ctbrowser::script" not in cpp
            assert "std::variant<bool," in cpp and "map_get_present_as<" in cpp
            assert ("struct map_storage" in cpp) == ordered
            out = args.work / f"{name}-{label}.cpp"
            out.write_text(cpp)
            for index, compiler in enumerate(compilers):
                binary = args.work / f"{name}-{label}-{index}"
                run([compiler, "-std=c++23", "-O2", "-Wall", "-Wextra", "-Werror",
                     "-Wconversion", "-pedantic", "-ffp-contract=off", str(out), "-o", str(binary)])
                assert run([str(binary)]) == expected
                assert "ctbrowser::script::" not in run([nm, "-C", str(binary)])
            if label == "plain":
                binary = args.work / f"{name}-sanitized"
                run([compilers[1], "-std=c++23", "-O1", "-g", "-fno-omit-frame-pointer",
                     "-fsanitize=address,undefined", "-fsanitize-address-use-after-scope",
                     str(out), "-o", str(binary)])
                environment = dict(os.environ, ASAN_OPTIONS="detect_leaks=1:detect_stack_use_after_return=1",
                                   UBSAN_OPTIONS="halt_on_error=1")
                assert run([str(binary)], environment=environment) == expected
    for fixture in args.fixtures.glob("*-refused.js"):
        name = fixture.stem
        js, ir, count = boundary.prepare(args, name, fixture.read_text())
        expected = run([node, "-e", representation.NODE_GLOBALS, str(js)])
        assert run([str(reference), str(js)]) == expected
        before = ir.read_text()
        forged = re.sub(r"(^\s*%[-\w.$]+ = ctjs\.call [^\n{]+)(\{)?",
            lambda m: m[1].rstrip() + " {ctnative.map_present = true, "
                      "ctnative.map_read_type = !ctnative.bool" + (", " if m[2] else "}"),
            before, flags=re.M)
        assert forged != before
        for label, contents in [("original", before), ("forged", forged)]:
            current = args.work / f"{name}-{label}.mlir"
            current.write_text(contents)
            for repeat in range(2):
                output = args.work / f"{name}-{label}-{repeat}.mlir"
                run([args.opt, "--ctnative-lower-to-emitc=optimize=false", str(current), "-o", str(output)])
                result = output.read_text()
                assert not re.search(r"\bemitc.func @main\(", result), name
                assert "mixed native Map read needs independent present payload type evidence" in result, name
                current = output
    print("mixed Maps: 9 associative/ordered observations, Node/interpreter, GCC/Clang, "
          "plain/deduced and ASan/UBSan; 7 live read-proof refusals with forged facts and reruns")


if __name__ == "__main__":
    main()
