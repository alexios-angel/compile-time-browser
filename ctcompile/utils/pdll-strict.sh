#!/bin/sh
#===- pdll-strict.sh - mlir-pdll, minus the ways it succeeds while lying ---===#
#
# mlir-pdll EXITS 0 ON THINGS THAT SILENTLY CHANGE WHAT A PATTERN MATCHES, and
# this wrapper is the whole reason a .pdll in this tree may name a `ctjs`
# operation at all. Two holes were measured against the pinned LLVM 23.1.0 on
# 2026-09-02, both with exit status 0:
#
#   AN ATTRIBUTE LITERAL OF AN UNREGISTERED DIALECT IS DROPPED.
#     op<ctjs.binary> {kind = attr<"#ctjs.binary_kind<add>">}
#   prints
#     error: #"ctjs"<"binary_kind<add>"> : 'none' attribute created with
#     unregistered dialect ...
#   on stderr, EXITS 0, and emits a pattern whose `kind` operand is a bare
#   `%1 = attribute` - no constraint at all. The build succeeds and the pattern
#   now fires on `ctjs.binary sub` as well. mlir-pdll has no flag that
#   registers an out-of-tree dialect: its only inputs are -I and the ODS it
#   parses.
#
#   AN OPERATION NAME THAT ODS DOES NOT KNOW IS NOT DIAGNOSED AT ALL.
#     #include "ctcompile/CTJS/IR/CTJSOps.td"
#     Pattern P { let root = op<ctjs.binry>; replace root with root; }
#   compiles with exit 0 and an EMPTY stderr. The ODS include does buy
#   checking - `op<ctjs.unary>(a: Value, b: Value)` is a hard error naming the
#   record - but only for an operation whose name ODS recognises. An unknown
#   name is taken as an unchecked string, and the pattern never fires. A
#   forgotten `#include` has exactly the same shape.
#
# SO THE WRAPPER DOES TWO THINGS. It refuses any run that printed a diagnostic
# even though it succeeded, and it re-runs the tool with --dump-ods to check
# that every operation the emitted pattern NAMES is an operation some ODS file
# on the include path defines. The second run costs about 11 ms and needs no
# argument parsing: mlir-pdll's -o, -d and -x are ordinary cl::opt scalars, so
# the copies appended after "$@" win.
#
# It is installed as MLIR_PDLL_TABLEGEN_EXE by ctcompile/CMakeLists.txt, so
# every add_mlir_pdll_library in the tree gets it and no .pdll can opt out.
#
#   usage: pdll-strict.sh <path to mlir-pdll> <mlir-pdll arguments...>
#
#===----------------------------------------------------------------------===#
set -u

if [ "$#" -lt 2 ]; then
    echo "pdll-strict.sh: usage: pdll-strict.sh <mlir-pdll> <mlir-pdll arguments...>" >&2
    exit 2
fi

pdll=$1
shift

work=$(mktemp -d "${TMPDIR:-/tmp}/ctcompile-pdll.XXXXXX") || exit 2
# shellcheck disable=SC2064  # $work is wanted at trap-setting time, not later
trap "rm -rf '$work'" EXIT HUP INT TERM

# --- 1. THE REAL RUN, AND A DIAGNOSTIC IS A FAILURE EVEN AT EXIT 0 ----------
"$pdll" "$@" 2>"$work/stderr"
status=$?
cat "$work/stderr" >&2
if [ "$status" -ne 0 ]; then
    exit "$status"
fi
if [ -s "$work/stderr" ]; then
    cat >&2 <<'MESSAGE'
pdll-strict.sh: mlir-pdll exited 0 AFTER printing the diagnostic above, which
pdll-strict.sh: means it dropped what it could not parse and emitted a pattern
pdll-strict.sh: without it. An attribute literal of this project's own dialect
pdll-strict.sh: is the known case - write a native Constraint instead, where a
pdll-strict.sh: mistake is a C++ compile error. Refusing the build.
MESSAGE
    exit 1
fi

# --- 2. EVERY OPERATION THE PATTERN NAMES MUST BE ONE ODS DEFINES -----------
#
# --dump-ods prints to STDERR (measured), and the emitted PDL spells each
# matched or created operation as `operation "<dialect>.<name>"` - in the C++
# output too, where the pattern is carried verbatim as a raw string. Asking for
# the MLIR form here keeps the extraction one sed expression whatever -x the
# real run used.
"$pdll" --dump-ods "$@" -x=mlir -o "$work/pattern.mlir" -d "$work/ods.d" \
    2>"$work/ods.txt"
census=$?
if [ "$census" -ne 0 ] || [ ! -f "$work/pattern.mlir" ]; then
    echo "pdll-strict.sh: the ODS census run failed where the real run did not:" >&2
    cat "$work/ods.txt" >&2
    exit 1
fi

sed -n 's/.*Operation `\([^`]*\)`.*/\1/p' "$work/ods.txt" | sort -u >"$work/known"
sed -n 's/.*operation "\([^"]*\)".*/\1/p' "$work/pattern.mlir" | sort -u >"$work/named"
unknown=$(comm -23 "$work/named" "$work/known")

if [ -n "$unknown" ]; then
    echo "pdll-strict.sh: this pattern names operations no ODS file on the include" >&2
    echo "pdll-strict.sh: path defines, and mlir-pdll does not diagnose that - it" >&2
    echo "pdll-strict.sh: takes the name as an unchecked string and the pattern then" >&2
    echo "pdll-strict.sh: never fires:" >&2
    echo "$unknown" | sed 's/^/pdll-strict.sh:     /' >&2
    echo "pdll-strict.sh: fix the spelling, or #include the .td that defines it." >&2
    exit 1
fi
