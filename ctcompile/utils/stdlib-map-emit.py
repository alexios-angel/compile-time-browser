#!/usr/bin/env python3
"""Turn StdLibMap.td's records into the X-macro header the tests walk.

WHY A SCRIPT AND NOT A TABLEGEN BACKEND. `mlir-tblgen` has a generator for
every ODS concept - ops, types, attributes, interfaces, rewriters - and none for
"emit my own records", because there is no such generic backend. `llvm-tblgen
--dump-json` is the escape hatch upstream provides for exactly this, and it is
forty lines of JSON walking rather than a C++ TableGen backend that would have
to be built and installed alongside llvm-tblgen itself.

WHAT THIS BUYS. The table stays in TableGen, which is where Phase 53's `ctnative`
dialect needs it: a declarative rewrite rule is a `Pat<>` in a .td, and a .td
that already holds the rows can grow the patterns beside them. The alternative -
a .def now and a .td later - is two tables that can disagree, which is the bug
this whole layering exists to prevent.

  stdlib-map-emit.py --tblgen <llvm-tblgen> --input <StdLibMap.td> --output <.inc>
"""

import argparse
import json
import subprocess
import sys

VERDICTS = ("exact", "divergent", "refused")


def c_string(text):
    """A C string literal. Escaped rather than raw: several `why` strings quote
    the sources they cite, so both quotes and backslashes really occur."""
    out = ['"']
    for ch in text:
        if ch in ('"', "\\"):
            out.append("\\" + ch)
        elif ch == "\n":
            out.append("\\n")
        else:
            out.append(ch)
    out.append('"')
    return "".join(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--tblgen", required=True)
    ap.add_argument("--input", required=True)
    ap.add_argument("--output", required=True)
    args = ap.parse_args()

    raw = subprocess.run([args.tblgen, "--dump-json", args.input],
                         check=True, capture_output=True, text=True).stdout
    records = json.loads(raw)

    # THE `Row` INSTANCE LIST, WHICH IS THE ONLY THING READ. A json dump also
    # carries the class definitions and the anonymous records TableGen makes for
    # the Exact/Divergent/Refused shorthands; asking for instanceof Row picks
    # exactly the named defs and nothing else.
    names = records.get("!instanceof", {}).get("Row", [])
    if not names:
        sys.exit("stdlib-map-emit: no records deriving from Row in " + args.input)

    lines = [
        "/* GENERATED from ctcompile/include/ctcompile/StdLib/StdLibMap.td.",
        " * Do not edit; edit the .td. See ctcompile/docs/plans/stdlib-mapping.md.",
        " *",
        " * CT_STDLIB_ROW(name, js, target, header, verdict, witness, why)",
        " */",
    ]
    counts = {v: 0 for v in VERDICTS}
    for name in names:
        row = records[name]
        verdict = row["Verdict"]
        if verdict not in VERDICTS:
            sys.exit("stdlib-map-emit: %s has verdict %r, which is not one of %s"
                     % (name, verdict, ", ".join(VERDICTS)))
        # A divergent row without a witness is a claim with nothing behind it,
        # and the whole point of the classification is that the claim is checked.
        if verdict == "divergent" and not row["Witness"]:
            sys.exit("stdlib-map-emit: %s is divergent and names no witness" % name)
        if verdict != "divergent" and row["Witness"]:
            sys.exit("stdlib-map-emit: %s is %s but names a witness" % (name, verdict))
        counts[verdict] += 1
        lines.append("CT_STDLIB_ROW(%s, %s, %s, %s, %s, %s,\n              %s)"
                     % (name, c_string(row["JS"]), c_string(row["Target"]),
                        c_string(row["Header"]), verdict,
                        c_string(row["Witness"]), c_string(row["Why"])))

    # THE COUNTS AS BUILD CONSTANTS, so the test asserts a number it did not
    # write down: a row deleted from the .td changes what the test demands.
    lines.append("")
    lines.append("#define CT_STDLIB_ROW_COUNT %d" % len(names))
    for verdict in VERDICTS:
        lines.append("#define CT_STDLIB_%s_COUNT %d" % (verdict.upper(), counts[verdict]))

    with open(args.output, "w") as out:
        out.write("\n".join(lines) + "\n")
    print("stdlib-map-emit: %d rows (%s)"
          % (len(names), ", ".join("%d %s" % (counts[v], v) for v in VERDICTS)))


if __name__ == "__main__":
    main()
