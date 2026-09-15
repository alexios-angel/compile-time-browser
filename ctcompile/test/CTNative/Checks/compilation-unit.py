#!/usr/bin/env python3
"""THE COMPILATION-UNIT GATE - ctcompile Phase 62½-D. The test that DEFINES "native".

Part 24 §1.2: a program compiles to native only when the output links neither
the interpreter nor the collector. This is that sentence as a test, run over
ONE EmitC module and the JavaScript program it is the lowering of:

  (a) the module goes through the FORKED emitter to C++ (ctjs-translate
      --mlir-to-cpp, in its default mode: --declare-variables-at-top would
      declare a variable for a `do` loop's condition and never assign it,
      which -Wall -Werror rejects; native functions are single-block and do
      not need it)
  (b) that C++ is compiled by the configured compiler STANDALONE: no include
      path into ctbrowser/, no library on the link line, -Wall -Wextra -Werror
      -pedantic, and -ffp-contract=off because JavaScript has no fused
      multiply-add and the interpreter's separate opcodes never fuse one
  (c) `nm -C` on the binary shows no `ctbrowser::script::` symbol (nor a
      `ctbrowser::aot::` one - the boxed tier's helper ABI links the
      interpreter). THE CHECK IS PROVED LOAD-BEARING FIRST: the same nm is run
      against a binary that does link the interpreter and must find such a
      symbol there, or a wrong nm spelling, a stripped binary or a wrong
      pattern would pass this gate vacuously
  (d) the binary runs and prints its globals
  (e) the interpreter runs the same JavaScript and prints its globals
      (ctcompile-test-native-reference) - after a probe program with known
      answers has shown the reference formats, sorts and CLASSIFIES as the
      convention says, one of every kind, so that a reference which has lost
      the ability to print a boolean cannot agree with a binary that never
      printed one either
  (f) both texts are checked against the convention's own grammar, then
      compared line by line; a difference NAMES the global
  (g) the counters are asserted and one line is printed

THE OUTPUT CONVENTION, which both sides print and this script checks: one line
per global the program created or changed, ascending bytewise by name,
`<name>=<value>`, the value one of five pairwise-disjoint forms:

    Number      printf("%.17g", d)          1   -0   2.5   inf   -inf   nan
    Boolean     true | false
    undefined   undefined
    null        null
    String      "<percent-encoded bytes>"   "text"   ""   "a%3D1"

Strings are BYTES (a lone surrogate is WTF-8, NUL is legal), so every byte
outside RFC 3986's unreserved set is `%` and two UPPERCASE hex digits: total,
injective, printable ASCII with no whitespace and no `=`, so the first `=` on
a line is always the separator. `-nan` and `nan` compare equal: a NaN's sign
is not observable in JavaScript. The grammar is checked on both texts before
they are compared, so a shared mistake in the escaping cannot pass by
corrupting both sides the same way.

NEGATIVE PROOFS, so the gate's teeth stay in the suite (each is a lit RUN line
under `not`, with FileCheck on the reason):
  --mutate <global>   after (a), the emitted C++ is perturbed so that ONE
                      global prints a different value - the gate must then
                      FAIL naming <global>
  --mutate-as <kind>  which perturbation: `<g> = <g> + 1` is a no-op on
                      everything that is not a number. Defaults to `number`.
  --prebuilt <exe>    skip (a) and (b) and gate this binary instead; used
                      with ctcompile-test-native-vm-linked, the fixture's own
                      C++ plus one object that reaches the interpreter - the
                      gate must FAIL on its nm check
"""

import argparse
import re
import subprocess
import sys
from pathlib import Path

# THE CONVENTION AS A REGEX - the five forms of the value, in one place.
GRAMMAR = re.compile(
    r"^[A-Za-z_$][A-Za-z0-9_$]*=(true|false|undefined|null|-?(inf|nan)"
    r'|-?[0-9]+(\.[0-9]+)?(e[-+][0-9]+)?|"([-A-Za-z0-9._~]|%[0-9A-F][0-9A-F])*")$'
)
NAMED = re.compile(r"^([A-Za-z_$][A-Za-z0-9_$]*)=(.*)$")
COUNTS = re.compile(
    r"([0-9]+) globals printed \(([0-9]+) number, ([0-9]+) boolean, ([0-9]+) string, "
    r"([0-9]+) null, ([0-9]+) undefined\), ([0-9]+) function globals skipped, "
    r"([0-9]+) other globals skipped"
)

# THE PROBE HAS ONE OF EVERYTHING THE CONVENTION HAS TO GET RIGHT: two numbers
# out of source order (the sort), a negative zero, a NaN, BOTH booleans, null
# beside `nan`, an unassigned `var`, the empty string, the string "nan", a
# string holding every character that would break a layer between the two
# programs, a NUL/two-byte/newline string, a lone surrogate, a function
# (skipped and counted) and an OBJECT - the kind the convention has no form
# for, counted under `other`, which keeps that path alive: a real program must
# have ZERO of them, so without the probe the check would never be seen to fire.
PROBE = r"""function f(x) { return x; }
var z = f(2);
var a = 1;
var m = -0;
var q = 0 / 0;
var b = true;
var c = false;
var n = null;
var u;
var s = "text";
var e = "";
var d = "nan";
var p = "a=1;b\\c\"d%e";
var w = "\u0000\u00ff\n";
var g = "\ud800";
var o = {};
"""
PROBE_EXPECTED = """a=1
b=true
c=false
d="nan"
e=""
g="%ED%A0%80"
m=-0
n=null
p="a%3D1%3Bb%5Cc%22d%25e"
q=nan
s="text"
u=undefined
w="%00%C3%BF%0A"
z=2
"""
PROBE_COUNTS = (
    "14 globals printed (4 number, 2 boolean, 6 string, 1 null, 1 undefined), "
    "1 function globals skipped, 1 other globals skipped"
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--translate", required=True)
    parser.add_argument("--module", required=True)
    parser.add_argument("--js", required=True)
    parser.add_argument("--cxx", required=True)
    parser.add_argument("--nm", required=True)
    parser.add_argument("--reference", required=True)
    parser.add_argument("--vm-linked", required=True)
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--name", required=True)
    parser.add_argument("--mutate")
    parser.add_argument("--mutate-as", default="number")
    parser.add_argument("--prebuilt")
    args = parser.parse_args()
    name = args.name

    def fail(message):
        sys.exit(f"{name}: {message}")

    def run(command):
        return subprocess.run(command, capture_output=True, text=True)

    work = args.work / f"native-unit-{name}"
    work.mkdir(parents=True, exist_ok=True)

    # --- (a) EmitC -> C++ through the forked emitter, (b) standalone compile ---
    if args.prebuilt:
        exe = args.prebuilt
        print(f"{name}: gating a prebuilt binary, {exe}")
    else:
        emitted = run([args.translate, "--mlir-to-cpp", args.module])
        if emitted.returncode != 0:
            fail(f"the emitter refused {args.module} (exit {emitted.returncode})\n{emitted.stderr}")
        cpp = emitted.stdout
        if not cpp:
            fail(f"the emitter produced nothing for {args.module}")
        # A translation unit that includes ctbrowser/ and happens to link is one
        # inline function away from the interpreter.
        if re.search(r'#include[ \t]*[<"]ctbrowser', cpp):
            fail("the emitted C++ includes a ctbrowser header - that is not native")
        if args.mutate:
            cpp = mutate(cpp, args.mutate, args.mutate_as, fail)
        src = work / "unit.cpp"
        src.write_text(cpp)
        exe = str(work / "unit")
        # NOTHING OF ctbrowser'S ON THIS LINE. No -I, no -l, no library: the
        # whole point. Clean under the same flags as the Phase 63 gate: a
        # warning here is a ctcompile bug, not something to suppress.
        compiled = run(
            [args.cxx, "-std=c++23", "-O2", "-Wall", "-Wextra", "-Werror", "-pedantic"]
            + ["-Wconversion", "-ffp-contract=off", "-o", exe, str(src)]
        )
        if compiled.returncode != 0:
            fail(
                f"the emitted C++ does not compile standalone (exit {compiled.returncode})\n"
                f"{compiled.stdout}{compiled.stderr}"
            )

    # --- (c) the symbol check, proved load-bearing before it is trusted ------
    control = run([args.nm, "-C", args.vm_linked])
    if control.returncode != 0:
        fail(f"{args.nm} failed on the control binary {args.vm_linked}\n{control.stderr}")
    control_count = sum("ctbrowser::script::" in line for line in control.stdout.splitlines())
    if control_count == 0:
        fail(
            f"the nm check is NOT load-bearing - {args.vm_linked} links the interpreter and "
            f"`{args.nm} -C` found no ctbrowser::script:: symbol in it. The invocation, the "
            "demangling or the pattern is wrong, and a pass on the native binary would mean nothing."
        )
    symbols = run([args.nm, "-C", exe])
    if symbols.returncode != 0:
        fail(f"{args.nm} failed on {exe}\n{symbols.stderr}")
    if not re.search(r"[ \t]main\n", symbols.stdout):
        fail(
            f"`{args.nm} -C` listed no `main` in {exe} - it did not read the binary\n{symbols.stdout}"
        )
    hits = [
        line
        for line in symbols.stdout.splitlines()
        if re.search(r"ctbrowser::(script|aot)::", line)
    ]
    if hits:
        fail(
            f"the binary reaches the interpreter - {len(hits)} ctbrowser symbol(s) (the control "
            f"binary had {control_count}); the first is: {hits[0].strip()}"
        )

    # --- (d) run the binary ------------------------------------------------------
    native = run([exe])
    if native.returncode != 0:
        fail(f"the native binary exited {native.returncode}\n{native.stdout}{native.stderr}")

    # --- (e) the reference: first proved on a probe, then run on the program ----
    probe = work / "probe.js"
    probe.write_text(PROBE)
    probed = run([args.reference, str(probe)])
    if probed.returncode != 0:
        fail(f"the reference failed on the probe (exit {probed.returncode})\n{probed.stderr}")
    if probed.stdout.replace("=-nan", "=nan") != PROBE_EXPECTED:
        fail(
            "the reference does not print the convention - on the probe it printed:\n"
            f"{probed.stdout}expected:\n{PROBE_EXPECTED}"
        )
    # THE CLASSIFICATION, ASSERTED SEPARATELY FROM THE TEXT: the reference must
    # know WHY, per kind, which the real program's run relies on.
    if PROBE_COUNTS not in probed.stderr:
        fail(
            "the reference miscounted the probe - it must find 4 numbers, 2 booleans, "
            f"6 strings, 1 null, 1 undefined, 1 function and 1 other:\n{probed.stderr}"
        )
    reference = run([args.reference, args.js])
    if reference.returncode != 0:
        fail(
            f"the interpreter failed on {args.js} (exit {reference.returncode})\n{reference.stderr}"
        )
    counts = COUNTS.search(reference.stderr)
    if not counts:
        fail(f"the reference did not report its counts:\n{reference.stderr}")
    printed, numbers, booleans, strings, nulls, undefineds, functions, others = map(
        int, counts.groups()
    )
    if others != 0:
        fail(
            f"{args.js} left {others} global(s) of a kind this convention has no form for - "
            "an object, an array, a symbol or a bigint. A phase that wants one has to extend "
            f"the convention first:\n{reference.stderr}"
        )
    if printed <= 0:
        fail("the interpreter left no global to print - nothing was compared")

    # --- (f) check the grammar, then compare, naming the global -------------------
    def lines_of(text):
        text = text.replace("=-nan", "=nan")
        text = text[:-1] if text.endswith("\n") else text
        return text.split("\n") if text else []

    native_lines, reference_lines = lines_of(native.stdout), lines_of(reference.stdout)
    if len(reference_lines) != printed:
        fail(f"the reference said {printed} globals and printed {len(reference_lines)} lines")
    for side, lines in (("native", native_lines), ("reference", reference_lines)):
        for line in lines:
            if not GRAMMAR.match(line):
                fail(
                    f"the {side} side printed a line that is not in the output convention: '{line}'"
                )
    agree = 0
    for i in range(max(len(native_lines), len(reference_lines))):
        n = native_lines[i] if i < len(native_lines) else ""
        r = reference_lines[i] if i < len(reference_lines) else ""
        if n == r:
            agree += 1
            continue
        transcript = f"native:\n{native.stdout}interpreter:\n{reference.stdout}"
        n_named, r_named = NAMED.match(n), NAMED.match(r)
        if n_named and r_named and n_named.group(1) == r_named.group(1):
            fail(
                f"global '{n_named.group(1)}' differs - native printed {n_named.group(2)}, "
                f"the interpreter printed {r_named.group(2)}\n{transcript}"
            )
        fail(
            f"line {i} differs - native printed '{n}', the interpreter printed '{r}' (a missing "
            f"or extra global, or one out of order: the convention sorts by name)\n{transcript}"
        )

    # --- (g) the counters, then the one line --------------------------------------
    if agree != printed:
        fail(f"{agree} lines agreed but the interpreter printed {printed} globals")
    if len(native_lines) != printed:
        fail(f"the native binary printed {len(native_lines)} globals, the interpreter {printed}")
    print(
        f"native unit ({name}): {agree} globals agree ({numbers} number, {booleans} boolean, "
        f"{strings} string, {nulls} null, {undefineds} undefined), 0 ctbrowser symbols "
        f"({functions} functions; the control binary showed {control_count} interpreter symbols "
        "to the same nm)"
    )


def mutate(cpp, target, kind, fail):
    """THE PERTURBATION IS PER KIND. `<g> = <g> + 1` is a no-op on a NaN and a
    compile error or a no-op on every other kind, so each gets the smallest
    edit that changes the one line it prints: number - an off-by-one before
    the first print; boolean - the printed value becomes `!<g>`; string - the
    printed value gains a prefix; undefined/null - the print call is swapped,
    because those globals have no storage and the only thing a wrong compiler
    can get wrong about one is which constant it prints. Every branch asserts
    that its edit landed, and the substituting branches that the text they
    replace occurs EXACTLY ONCE."""
    if kind == "number":
        # INSIDE main, AND THAT IS ASSERTED: the value-printing prelude is
        # written with fputs/putchar so a helper never carries the printf the
        # insertion anchors on. Qualified or bare `printf(`, because the
        # hand-written module and a generated one spell it differently, and
        # the insertion goes BEFORE the qualifier or `std::<assignment>printf`
        # would not compile - a negative proof that passes for the wrong reason.
        main_at = cpp.find("int32_t main() {")
        if main_at < 0:
            fail(
                "cannot mutate - no `int32_t main() {` in the emitted C++, the output convention has changed under this script"
            )
        from_main = cpp[main_at:]
        relative = from_main.find("printf(")
        if relative < 0:
            fail(
                "cannot mutate - no printf( inside main in the emitted C++, the output convention has changed under this script"
            )
        if relative >= 5 and from_main[relative - 5 : relative] == "std::":
            relative -= 5
        at = main_at + relative
        head, tail = cpp[:at], cpp[at:]
        # THE INSERTION MUST PRECEDE THE LOAD, or it changes nothing: the
        # printing reads each global into a temporary (`<type> vN = <g>;`), so
        # a global already read above the insertion point prints its old value
        # and the negative test reports only "PASSED where it had to fail".
        # Under whichever name the emitter gave it - `g_total` generated,
        # `total` hand-written.
        symbol = f"g_{target}" if f"= g_{target};" in cpp else target
        if f"= {symbol};" not in tail:
            # ONE WORD IS THE ANCHOR, so a FileCheck line cannot miss it.
            fail(
                f"VACUOUS - mutating {symbol} here would change nothing the binary prints, "
                "because the printing has already read it above the insertion point. Mutate a "
                "global that sorts later than the first one printed, or use a name-directed "
                "--mutate-as."
            )
        # Generated numeric globals keep a tag so a missing store cannot look
        # like a present NaN. Handwritten fixtures still use plain doubles.
        increment = f"{symbol} + 1"
        if re.search(rf"ctnative::nullable_scalar {re.escape(symbol)}[; =]", cpp):
            increment = f"ctnative::global_number({symbol}) + 1"
        mutation = f"{symbol} = {increment};"
        cpp = f"{head}{mutation}\n  {tail}"
        if mutation not in cpp:
            fail(f"the mutation of {target} did not apply")
        print(f"MUTATED - {target} is one more than the program computed")
        return cpp
    edits = {
        "boolean": (
            f'ctnative::print_boolean("{target}", ',
            f'ctnative::print_boolean("{target}", !',
            f"{target} prints the boolean the program did NOT compute",
        ),
        "string": (
            f'ctnative::print_string("{target}", ',
            f'ctnative::print_string("{target}", std::string("ctcompile-mutation-") + ',
            f"{target} prints a string the program did not build",
        ),
        "undefined": (
            f'ctnative::print_undefined("{target}")',
            f'ctnative::print_null("{target}")',
            f"{target} is undefined but prints as null",
        ),
        "null": (
            f'ctnative::print_null("{target}")',
            f'ctnative::print_undefined("{target}")',
            f"{target} is null but prints as undefined",
        ),
    }
    if kind not in edits:
        fail(
            f"--mutate-as {kind} is not one of the kinds this convention has (number, boolean, string, undefined, null)"
        )
    find, into, why = edits[kind]
    if find not in cpp:
        fail(
            f"cannot mutate {target} as a {kind} - the emitted C++ contains no `{find}`, so the output convention has changed under this script"
        )
    if cpp.count(find) > 1:
        fail(
            f"`{find}` occurs more than once in the emitted C++ - this mutation would not be about one global"
        )
    cpp = cpp.replace(find, into)
    if into not in cpp:
        fail(f"the mutation of {target} as a {kind} did not apply")
    print(f"MUTATED - {why}")
    return cpp


if __name__ == "__main__":
    main()
