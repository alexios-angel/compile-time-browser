#!/usr/bin/env python3
"""Check callable-body inlining, retained functions, forwarding and metadata."""

import argparse
from pathlib import Path
import re
import shutil
import subprocess


MAIN = """
int main() {
    const auto first = ctn_bind_inline(40);
    const auto alias = first;
    if (first(1) != 42 || alias(1) != 42 || first(9) != 50 || direct_caller(40) != 42) { return 1; }
    if (unmarked_creation(40)(1) != 42) { return 1; }
    const auto changing = ctn_bind_mutable(5);
    const auto separate = ctn_bind_mutable(100);
    if (changing() != 6 || changing() != 6 || separate() != 101 || mutable_target(5) != 6) { return 2; }
    const auto fallback = marked_mutable(5);
    if (fallback() != 6 || fallback() != 6) { return 2; }
    const auto unknown = ctn_bind_unknown(40);
    if (unknown() != 42 || unknown() != 42) { return 3; }
    if (address_pointer()(20, 22) != 42) { return 4; }
    if (literal_pointer()(42) != 42) { return 5; }
    if (creation_sites(std::string(80, 'x')) != 287 || creation_sites("") != 47) { return 6; }
    const auto nested = nested_creation(std::string(70, 'y'));
    if (nested() != 72 || nested() != 72) { return 7; }
    const auto recursive = recursive_creation(42);
    if (recursive() != 42 || recursive() != 42) { return 8; }
    const auto markedLiteral = deferred_literal_marked();
    const auto unmarkedLiteral = deferred_literal_unmarked();
    if (markedLiteral() != 11 || unmarkedLiteral() != 11 || markedLiteral() != unmarkedLiteral()) { return 9; }
    const auto markedExpression = inline_expression_marked();
    const auto unmarkedExpression = inline_expression_unmarked();
    if (markedExpression() != 31 || unmarkedExpression() != 31 || markedExpression() != unmarkedExpression()) { return 10; }
    return 0;
}
"""


def run(command):
    result = subprocess.run(command, text=True, capture_output=True, timeout=120)
    if result.returncode:
        raise RuntimeError(f"{command!r}\n{result.stdout}{result.stderr}")
    return result.stdout


def body(text, name):
    match = re.search(r"\b" + re.escape(name) + r"\([^;{}]*\)\s*\{", text)
    if not match:
        raise RuntimeError(f"missing definition of {name}\n{text}")
    start, depth = match.end(), 1
    for at in range(start, len(text)):
        depth += (text[at] == "{") - (text[at] == "}")
        if depth == 0:
            return text[start:at]
    raise RuntimeError(f"unterminated definition of {name}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fixtures", type=Path, required=True)
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--translate", required=True)
    args = parser.parse_args()
    args.work.mkdir(parents=True, exist_ok=True)
    compilers = []
    for choices in [("g++-13", "g++"), ("clang++-18", "clang++")]:
        compiler = next((shutil.which(name) for name in choices if shutil.which(name)), None)
        if not compiler:
            raise RuntimeError("callable-body regression requires " + " or ".join(choices))
        compilers.append(compiler)
    flags = ["-std=c++23", "-O2", "-Wall", "-Wextra", "-Werror",
             "-Wconversion", "-pedantic", "-I", str(args.fixtures)]
    emitted = {}
    for label in ["callables", "hoisted"]:
        cpp = (args.fixtures / f"{label}.cpp").read_text()
        emitted[label] = cpp
        inlined = body(cpp, "ctn_bind_inline")
        assert "increment(argument_delta)" in inlined, inlined
        assert "inline_target(" not in inlined, inlined
        assert "](int32_t argument_delta) -> int32_t" in inlined, inlined
        assert "capture_seed = std::move(capture_seed)" in inlined, inlined
        assert "increment(" in body(cpp, "inline_target"), cpp
        assert "inline_target(" in body(cpp, "direct_caller"), cpp
        assert "ctn_bind_inline(" in body(cpp, "unmarked_creation"), cpp
        assert "ctn_bind_mutable(" in body(cpp, "marked_mutable"), cpp
        for name in ["mutable", "unknown"]:
            forwarded = body(cpp, f"ctn_bind_{name}")
            assert f"return {name}_target(capture_seed);" in forwarded, forwarded
            assert "mutable {" not in forwarded and "[&" not in forwarded, forwarded
            assert body(cpp, f"{name}_target"), cpp
        assert "&address_target" in body(cpp, "address_pointer"), cpp
        assert "&literal_target" in body(cpp, "literal_pointer"), cpp
        assert body(cpp, "address_target") and body(cpp, "literal_target"), cpp
        created = body(cpp, "creation_sites")
        assert created.count("[capture_text = text]") == 2, created
        assert "std::move(text)" not in created and "[&" not in created, created
        assert "ctn_bind_string(" not in created and "string_target(" not in created, created
        assert created.count("text_length(capture_text)") == 2, created
        assert "append_marker(text)" in created, created
        if label == "callables":
            assert "constexpr int32_t seed = 40;" in created, created
            assert "constexpr int32_t offset = 2;" in created, created
            assert "constexpr int32_t after = seed + offset;" in created, created
            assert re.search(r"auto const second = ctnative::ctn_env_string\s*[({]", created), created
            assert 'CTCOMPILE_PIN(second, "callable-creation.js:4:1", ctnative::ctn_env_string const);' in created, created
        else:
            assert "constexpr " not in created and "CTCOMPILE_PIN" not in created, created
        nested = body(cpp, "nested_creation")
        assert "[capture_text = text]" in nested, nested
        assert "[capture_text = capture_text]" in nested, nested
        assert "ctn_bind_nested(" not in nested and "ctn_bind_string(" not in nested, nested
        recursive = body(cpp, "recursive_creation")
        assert "ctn_bind_recursive(" in recursive and len(recursive) < 32768, recursive
        for prefix in ["deferred_literal", "inline_expression"]:
            marked = body(cpp, prefix + "_marked")
            assert "[capture_seed = " in marked, marked
            assert "classify_capture(capture_seed)" in marked, marked
            assert "ctn_bind_classified(" not in marked, marked
            unmarked = body(cpp, prefix + "_unmarked")
            assert "ctn_bind_classified(" in unmarked, unmarked
        source = args.work / f"{label}.cpp"
        source.write_text(cpp + MAIN)
        for index, compiler in enumerate(compilers):
            binary = (args.work / f"{label}-{index}").resolve()
            run([compiler, *flags, str(source), "-o", str(binary)])
            run([str(binary)])
    before = '"callable-creation.js:4:1", ctnative::ctn_env_string const);'
    after = '"callable-creation.js:4:1", ctnative::ctn_env_inline const);'
    assert emitted["callables"].count(before) == 1
    source = args.work / "wrong-creation-pin.cpp"
    source.write_text(emitted["callables"].replace(before, after, 1) + MAIN)
    for index, compiler in enumerate(compilers):
        rejected = subprocess.run([compiler, *flags, "-fsyntax-only", str(source)],
                                  text=True, capture_output=True, timeout=120)
        assert rejected.returncode != 0 and "callable-creation.js:4:1" in rejected.stderr, rejected.stderr
        binary = (args.work / f"pin-disabled-{index}").resolve()
        run([compiler, *flags, "-DCTCOMPILE_NO_TYPE_PINS", str(source), "-o", str(binary)])
        run([str(binary)])
    for fixture, diagnostic in [
        ("invalid.mlir", "invalid native callable body description"),
        ("invalid-creation.mlir", "invalid native callable creation"),
        ("invalid-signature.mlir", "invalid native callable creation"),
    ]:
        rejected = subprocess.run([args.translate, "--mlir-to-cpp", str(args.fixtures / fixture)],
                                  text=True, capture_output=True, timeout=120)
        assert rejected.returncode != 0, rejected.stdout
        assert diagnostic in rejected.stderr, rejected.stderr
    print("callable bodies: inline creation, capture conversions, exact pins, forwarding and diagnostics agree under GCC/Clang")


if __name__ == "__main__":
    main()
