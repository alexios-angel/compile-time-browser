#!/usr/bin/env python3
"""THE PRINTING GATE - ctcompile Phase 62½-E (part 24 Stages 53E and 53F).

Three modules of ONE program: the plain lowering, the same with
--ctnative-print-deduced applied, and the same with the pass's mutation. The
compilation-unit gate already runs the deduced module through the whole of
62½-D, so what is left to prove here is the printing itself:

  (a) every `auto` declaration has exactly one pin after it, and there is at
      least one - a pass that marks nothing would make (b) to (d) vacuous
  (b) the plain file has no `auto` and no pin at all
  (c) the two files DIFFER ONLY IN SPELLING: with the pin lines, the macro
      block and its include removed from the deduced file, and every
      declaration's type spelling normalised in both, the texts are
      identical, line for line
  (d) the byte counts - plain, deduced, deduced without its pins - are
      printed; the difference is the first number Phase 63 Step 4 reports
  (e) the MUTATED file, where one deduced double is pinned as int32_t, FAILS
      to compile and the compiler's message names the JavaScript site; the
      same file compiles CLEAN under -DCTCOMPILE_NO_TYPE_PINS, which proves
      both that the pin was the failure and that the switch strips it
"""

import argparse
import re
import subprocess
import sys
from pathlib import Path

AUTO = re.compile(r"^[ \t]*(constexpr )?auto( const)? [A-Za-z_0-9]+ = ")
PIN = re.compile(r"^[ \t]*CTCOMPILE_PIN\(")
# Qualification is part of the contract: normalise the carrier spelling but
# keep constexpr and const so explicit and deduced declarations agree. Direct
# calls can return owning Maps, including finite nested schemas; the whole
# initializer and every other line still compare.
CARRIERS = [
    re.compile(
        r"^([ \t]*(constexpr )?)(auto|double|js_num|bool|int32_t|int64_t|float|std::string"
        r"|ctnative::nullable_scalar|ctnative::nullable_string|ctnative::object_value)"
        r"(( const)? [A-Za-z_0-9]+ = )"
    ),
    re.compile(
        r"^([ \t]*(constexpr )?)std::shared_ptr<ctnative::(number_map|map_storage)<([^;=]+)>>"
        r"(( const)? [A-Za-z_0-9]+ = )"
    ),
    re.compile(
        r"^([ \t]*(constexpr )?)ctnative::ctn_env_[A-Za-z_0-9]+(( const)? [A-Za-z_0-9]+ = )"
    ),
    re.compile(
        r"^([ \t]*(constexpr )?)std::shared_ptr<ctnative::method_table_[A-Za-z_0-9]+>"
        r"(( const)? [A-Za-z_0-9]+ = )"
    ),
    re.compile(
        r"^([ \t]*(constexpr )?)std::shared_ptr<ctnative::identity_object>(( const)? [A-Za-z_0-9]+ = )"
    ),
    re.compile(
        r"^([ \t]*(constexpr )?)std::shared_ptr<ctnative::string_to_number_map>"
        r"(( const)? [A-Za-z_0-9]+ = )"
    ),
]
SITE = re.compile(r"ctcompile: [A-Za-z_0-9]+ @ [^\"\n]*\.js:[0-9]+:[0-9]+")
# The runtime header every native program includes lives in ctcompile/include.
RUNTIME_INCLUDE = "-I" + str(Path(__file__).resolve().parents[3] / "include")
FLAGS = [
    "-std=c++23",
    RUNTIME_INCLUDE,
    "-O2",
    "-Wall",
    "-Wextra",
    "-Werror",
    "-pedantic",
    "-Wconversion",
]


def normalise(lines):
    out = []
    for line in lines:
        for carrier in CARRIERS:
            # The declaration's suffix is the last group of every pattern.
            line = carrier.sub(lambda m: f"{m.group(1)}T{m.group(m.re.groups - 1)}", line)
        out.append(line)
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--translate", required=True)
    parser.add_argument("--plain", required=True)
    parser.add_argument("--deduced", required=True)
    parser.add_argument("--mutated", required=True)
    parser.add_argument("--cxx", required=True)
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--name", required=True)
    args = parser.parse_args()

    def fail(message):
        sys.exit(f"print-deduced ({args.name}): {message}")

    work = args.work / f"print-deduced-{args.name}"
    work.mkdir(parents=True, exist_ok=True)

    def translate(module, out):
        result = subprocess.run(
            [args.translate, "--mlir-to-cpp", module], capture_output=True, text=True
        )
        if result.returncode != 0:
            fail(f"the emitter refused {module}:\n{result.stderr}")
        (work / out).write_text(result.stdout)
        return result.stdout.split("\n")

    plain_lines = translate(args.plain, "plain.cpp")
    deduced_lines = translate(args.deduced, "deduced.cpp")
    translate(args.mutated, "mutated.cpp")

    # (a) and (b): count, and pair every auto with the pin on the next line.
    n_auto = n_pin = 0
    expect_pin = in_macro = False
    stripped = []  # the deduced file without pins, macro block, its include
    for line in deduced_lines:
        if line.startswith("#ifndef CTCOMPILE_NO_TYPE_PINS"):
            # PrintDeduced adds one include immediately before its macro, even
            # when the program already includes type_traits. Remove only that
            # added line; genuine program includes must still compare exactly.
            if not stripped:
                fail("the pin macro has no preceding include")
            pin_include = stripped.pop()
            if pin_include != "#include <type_traits>":
                fail(f"expected the pin include immediately before its macro, got: {pin_include}")
            in_macro = True
        if in_macro:
            if line.startswith("#endif"):
                in_macro = False
            continue
        if PIN.match(line):
            if not expect_pin:
                fail(f"a pin with no auto declaration before it: {line}")
            n_pin += 1
            expect_pin = False
            continue
        if expect_pin:
            fail(f"an auto declaration without its pin: {line}")
        if AUTO.match(line):
            n_auto += 1
            expect_pin = True
        stripped.append(line)
    n_plain_auto = sum(bool(AUTO.match(line) or "CTCOMPILE_PIN" in line) for line in plain_lines)
    if n_plain_auto != 0:
        fail(
            f"the plain file has {n_plain_auto} auto/pin line(s); the policy leaked into the default emitter"
        )
    if n_auto == 0:
        fail("the policy marked nothing; the gate would be vacuous")
    if n_auto != n_pin:
        fail(f"{n_auto} auto declarations but {n_pin} pins")

    # (c) spelling only.
    plain_norm, deduced_norm = normalise(plain_lines), normalise(stripped)
    if len(plain_norm) != len(deduced_norm):
        fail(
            f"after normalising, {len(plain_norm)} plain lines vs {len(deduced_norm)} deduced "
            "lines - the files differ in more than spelling"
        )
    for i, (a, b) in enumerate(zip(plain_norm, deduced_norm)):
        if a != b:
            fail(f"line {i + 1} differs in more than spelling:\n  plain:   {a}\n  deduced: {b}")

    # (d) sizes.
    b_plain = (work / "plain.cpp").stat().st_size
    b_deduced = (work / "deduced.cpp").stat().st_size
    (work / "deduced-no-pins.cpp").write_text("\n".join(stripped) + "\n")
    b_nopins = (work / "deduced-no-pins.cpp").stat().st_size

    # (e) the mutation.
    def compile_mutated(*extra):
        return subprocess.run(
            [
                args.cxx,
                *FLAGS,
                "-ffp-contract=off",
                "-fsyntax-only",
                *extra,
                str(work / "mutated.cpp"),
            ],
            capture_output=True,
            text=True,
        )

    result = compile_mutated()
    if result.returncode == 0:
        fail("the mutated file (one double pinned as int32_t) COMPILED - the pins do not bite")
    # A JAVASCRIPT site: the file the program came from, its line and column. A
    # site in the .mlir file (locations dropped between passes) or "unknown" (a
    # location the printer did not unwrap) both fail here by name.
    site = SITE.search(result.stdout + result.stderr)
    if not site:
        fail(
            "the mutated file failed, but not with a pin naming a JavaScript site "
            f"(file.js:line:col):\n{result.stdout}{result.stderr}"
        )
    result = compile_mutated("-DCTCOMPILE_NO_TYPE_PINS")
    if result.returncode != 0:
        fail(
            "the mutated file does not compile under -DCTCOMPILE_NO_TYPE_PINS, so the failure "
            f"above was not (only) the pin:\n{result.stdout}{result.stderr}"
        )
    print(
        f"print-deduced ({args.name}): {n_auto} deduced declarations, each pinned; plain "
        f"{b_plain} bytes, deduced {b_deduced} bytes ({b_nopins} without pins); the mutation "
        f'failed the build at "{site.group(0)}"'
    )


if __name__ == "__main__":
    main()
