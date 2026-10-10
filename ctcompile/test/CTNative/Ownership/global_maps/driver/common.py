"""Shared helpers for the global-Map ownership driver.

Census checks (exact function and call counts in the MLIR before and after
preparation) and Node/VM observation of printed globals. main() lives in
__main__.py; the argparse description is execution.py's docstring.
"""

import argparse
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess

from CTNative.Ownership.globals import CONSTANT_GLOBAL_NODE

from ..sources import *
from ..harness import *


def check_census(args, ir, name, cases, what, *, branched=False):
    """Require a five-function case of `cases()` to keep its exact census.

    The census is the function count and the source-call counts of the raw and
    prepared MLIR (`raw_calls`, `prepared_calls` in the row). When `branched`,
    the prepared IR must also keep an scf.if with both arms. Names not in
    `cases()` are skipped.
    """
    row = cases().get(name)
    if row is None:
        return
    raw = (args.work / f"{name}.raw.mlir").read_text()
    prepared = ir.read_text()
    if (
        len(boundary.FUNCTION.findall(raw)) != 5
        or len(source_calls(raw)) != row["raw_calls"]
        or len(source_calls(prepared)) != row["prepared_calls"]
    ):
        raise RuntimeError(f"{name}: changed the exact {what} function/call census")
    if branched and ("scf.if" not in prepared or prepared.count("scf.yield") < 2):
        raise RuntimeError(f"{name}: lost a structural branch before the {what} proof")


def observe(args, name, source, values):
    """Node and the VM must both print exactly `values` (Numbers, bools, "null" or
    "undefined") for `source`, with those tags. Returns the printed text."""
    js = args.work / f"{name}-observed.js"
    js.write_text(source)
    expected = "".join(f"{key}={str(value).lower()}\n" for key, value in sorted(values.items()))
    tags = [
        (
            "boolean"
            if isinstance(value, bool)
            else value
            if value in ("null", "undefined")
            else "number"
        )
        for value in values.values()
    ]
    counts = ", ".join(
        f"{tags.count(tag)} {tag}" for tag in ("number", "boolean", "string", "null", "undefined")
    )
    result = host.run([args.node, "-e", CONSTANT_GLOBAL_NODE, str(js), json.dumps(sorted(values))])
    if result.stdout != expected or result.stderr:
        raise RuntimeError(
            f"{name}: typed Node observation changed\n{result.stdout}{result.stderr}"
        )
    result = host.run([str(args.reference), str(js)])
    if result.stdout != expected or f"({counts})" not in result.stderr:
        raise RuntimeError(f"{name}: typed VM observation changed\n{result.stdout}{result.stderr}")
    return expected


def distinguish(args, name, source, old, replacement, expected, names=("trace",)):
    """Require Node to fail, or print other than `expected`, once `old` becomes
    `replacement`; this proves the observer notices that detail of `source`."""
    assert source.count(old) == 1, (name, old)
    js = args.work / f"{name}-blind.js"
    js.write_text(source.replace(old, replacement))
    result = subprocess.run(
        [args.node, "-e", CONSTANT_GLOBAL_NODE, str(js), json.dumps(sorted(names))],
        capture_output=True,
        text=True,
        timeout=30,
    )
    if not result.returncode and result.stdout == expected:
        raise RuntimeError(f"{name}: observer cannot distinguish {replacement}")


def numeric_reference_output(name, value):
    """Return what the reference interpreter prints for numeric global `value`."""
    return scalar_global_output(name, value)


def numeric_node_observer(value):
    """Return the Node observer script for `value`.

    boundary.NODE accepts only finite Numbers, so booleans, NaN and -Infinity
    get a copy with that type check replaced; String values use the typed
    constant-global observer limited to `trace`.
    """
    if isinstance(value, StringValue):
        return CONSTANT_GLOBAL_NODE.replace("JSON.parse(process.argv[2])", '["trace"]')
    if not isinstance(value, bool) and value not in {"NaN", "-Infinity"}:
        return boundary.NODE
    predicate = "typeof trace !== 'number' || !Number.isFinite(trace)"
    if boundary.NODE.count(predicate) != 1:
        raise RuntimeError("exact NaN observer lost the shared finite-number control")
    condition = (
        "typeof trace !== 'boolean'"
        if isinstance(value, bool)
        else (
            "typeof trace !== 'number' || !Number.isNaN(trace)"
            if value == "NaN"
            else "typeof trace !== 'number' || trace !== -Infinity"
        )
    )
    return boundary.NODE.replace(predicate, condition)
