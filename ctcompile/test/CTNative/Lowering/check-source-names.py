"""Check native source names without changing what the generated program does."""

import argparse
from pathlib import Path
import re
import shutil
import subprocess


def run(command):
    result = subprocess.run(command, text=True, capture_output=True, timeout=120)
    if result.returncode:
        raise RuntimeError(f"{command!r}\n{result.stdout}{result.stderr}")
    return result.stdout


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--opt", required=True)
    parser.add_argument("--translate", required=True)
    parser.add_argument("--work", type=Path, required=True)
    args = parser.parse_args()
    args.work.mkdir(parents=True, exist_ok=True)
    tests = Path(__file__).resolve().parents[2]
    module = args.work / "native.mlir"
    run(["cmake", f"-DTRANSLATE={args.translate}", f"-DOPT={args.opt}",
         f"-DSOURCE={tests / 'native-source-names-fixture.js'}", f"-DOUTPUT={module}",
         "-DPARTIAL_EVALUATE=ON", "-P", str(tests / "native-pipeline.cmake")])
    deduced = args.work / "deduced.mlir"
    run([args.opt, "--ctnative-print-deduced", "--mlir-print-debuginfo", str(module),
         "-o", str(deduced)])
    expected = ("aliasScore=2510\nclosureScore=13\ncollisionScore=21\ninitialPrice=10\n"
                "loopScore=10\nmacroScore=12\nreusedScore=13\n")
    outputs = []
    for label, ir in [("plain", module), ("deduced", deduced)]:
        cpp = run([args.translate, "--mlir-to-cpp", str(ir)])
        assert re.search(r"double observePrice_\d+\([^\n]* const catalog\)", cpp), cpp
        sharing = cpp.split("// ctcompile: function observeSharing,", 1)[1]
        sharing = sharing.split("// ctcompile: function", 1)[0]
        assert "constexpr double score_1 = -1.0;" in sharing, sharing
        assert "return score_2;" in sharing, sharing
        assert re.search(r"double const score_2 = score_\d+;", sharing), sharing
        assert "double score_3;" in sharing, sharing
        assert re.search(r"(?:constexpr )?(?:double|auto)(?: const)? score_\d+ = score_\d+ [*+]", sharing), sharing
        assert re.search(r"score_\d+ = score_1;", sharing), sharing
        # The pin pass preserves identifiers, const and constexpr decisions.
        outputs.append(re.findall(r"\b(constexpr )?(?:double|auto)( const)? (score_\d+)\b", sharing))
        source = args.work / f"{label}.cpp"
        source.write_text(cpp)
        for name in ("g++", "clang++"):
            compiler = shutil.which(name)
            if not compiler:
                raise RuntimeError(f"source-name regression requires {name}")
            binary = args.work / f"{label}-{name}"
            run([compiler, "-std=c++23", "-O2", "-Wall", "-Wextra", "-Werror",
                 "-Wconversion", "-pedantic", "-ffp-contract=off", str(source), "-o", str(binary)])
            assert run([str(binary)]) == expected
    assert outputs[0] == outputs[1], outputs
    print("native source names: 7 observations agree under GCC and Clang, plain and deduced")


if __name__ == "__main__":
    main()
