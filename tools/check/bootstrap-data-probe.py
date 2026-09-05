#!/usr/bin/env python3
"""Check source-derived Bootstrap Data after CommonJS/browser/AMD publication.

The upstream UMD wrapper and Data declaration remain verbatim. Only the rest
of the factory is replaced with `return e`, exposing Data rather than the full
component exports. Reference observations and native compile coverage are
separate: a passing probe does not mean Bootstrap has a native executable.
Generated sources, provenance, observations and native censuses stay in --work.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys


FACTORY = '}(this, function() {\n    "use strict";\n    const t = new Map,\n        e = {\n'
BOUNDARY = '\n        },\n        i = "transitionend",'
ENVIRONMENTS = {
    "commonjs": ("var module = {exports: {}}; var exports = module.exports;\n", "module.exports"),
    "browser": ("var globalThis = {};\n", "globalThis.bootstrap"),
    "amd": (
        "var pending; function define(factory) { pending = factory; } define.amd = 1;\n",
        "published",
    ),
}
EXPECTED = {"traceGet": "42", "traceOther": "21", "traceReplacement": "43", "traceRemoved": "1"}


class ProbeError(Exception):
    pass


def sha256(data: str) -> str:
    return hashlib.sha256(data.encode("utf-8")).hexdigest()


def unique_position(source: str, marker: str, label: str) -> int:
    count = source.count(marker)
    if count != 1:
        raise ProbeError(f"source structure changed: expected one {label}, found {count}")
    return source.index(marker)


def extract(source: str) -> tuple[str, dict]:
    """Slice the actual vendor bytes, refusing a bundle with unfamiliar structure."""
    version = re.match(r"/\*!\n \* Bootstrap v([^ ]+) \(https://getbootstrap.com/\)\n", source)
    if not version:
        raise ProbeError("source structure changed: Bootstrap license/version header missing")
    factory = unique_position(source, FACTORY, "UMD factory/Data start")
    boundary = unique_position(source, BOUNDARY, "Data/transitionend boundary")
    wrapper = source[:factory]
    for marker in (
        "! function(t, e) {\n",
        '"object" == typeof exports && "undefined" != typeof module ? module.exports = e()',
        '"function" == typeof define && define.amd ? define(e)',
        '(t = "undefined" != typeof globalThis ? globalThis : t || self).bootstrap = e()',
    ):
        unique_position(wrapper, marker, "UMD publication branch")
    if boundary <= factory + len(FACTORY):
        raise ProbeError("source structure changed: Data must follow the UMD factory")
    data = source[factory + len(FACTORY):boundary]
    for marker in ("set(e, i, n) {", "get: (e, i) =>", "remove(e, i) {"):
        unique_position(data, marker, "Data method")
    # Keep everything through Data's closing brace. Its declaration separator
    # becomes a semicolon, and the factory returns Data instead of continuing.
    end = boundary + len("\n        }")
    fragment = source[:end]
    return fragment + ";\n    return e;\n});\n", {
        "bootstrap_version": version.group(1),
        "vendor_sha256": sha256(source),
        "vendor_fragment_sha256": sha256(fragment),
        "vendor_fragment_bytes": len(fragment.encode("utf-8")),
    }


def generate(fragment: str, mode: str) -> str:
    prelude, api = ENVIRONMENTS[mode]
    delayed = (
        'var traceDelayed = typeof pending === "function" ? 1 : 0; var published = pending();\n'
        if mode == "amd" else ""
    )
    return prelude + fragment + delayed + f"""var element = {{}}; var other = {{}};
{api}.set(element, "bs.alert", 42);
{api}.set(other, "bs.alert", 21);
var traceGet = {api}.get(element, "bs.alert");
var traceOther = {api}.get(other, "bs.alert");
{api}.set(element, "bs.alert", 43);
var traceReplacement = {api}.get(element, "bs.alert");
{api}.remove(element, "bs.alert");
var traceRemoved = {api}.get(element, "bs.alert") === null ? 1 : 0;
"""


def run(command: list[str]) -> subprocess.CompletedProcess:
    result = subprocess.run(command, capture_output=True, text=True, timeout=120)
    if result.returncode:
        raise ProbeError(f"command failed ({result.returncode}): {command[0]}\n{result.stdout}{result.stderr}")
    return result


def check(args: argparse.Namespace) -> None:
    # read_bytes preserves upstream newlines for both extraction and hashes.
    source = args.bootstrap.read_bytes().decode("utf-8")
    if args.negative_control == "source-boundary":
        source = source.replace(BOUNDARY, BOUNDARY.replace("transitionend", "changed"), 1)
    fragment, provenance = extract(source)
    generated = generate(fragment, args.mode)
    if args.negative_control == "trace":
        # Change an actual executed observation; the reference must report it
        # and the gate must reject it by name, before native census begins.
        generated += "traceReplacement = traceReplacement + 1;\n"
    args.work.mkdir(parents=True, exist_ok=True)
    stem = args.work / f"bootstrap-data-{args.mode}"
    program = stem.with_suffix(".js")
    program.write_bytes(generated.encode("utf-8"))
    provenance.update({
        "mode": args.mode,
        "vendor": str(args.bootstrap),
        "program": str(program),
        "program_sha256": sha256(generated),
    })
    stem.with_suffix(".provenance.json").write_text(json.dumps(provenance, indent=2) + "\n")
    if args.generate_only:
        print(f"bootstrap-data ({args.mode}): generated {program}")
        return

    reference = run([args.reference, str(program)])
    stem.with_suffix(".reference.txt").write_text(reference.stdout)
    stem.with_suffix(".reference.log").write_text(reference.stderr)
    observed = {}
    for line in reference.stdout.splitlines():
        name, separator, value = line.partition("=")
        if not separator or name in observed:
            raise ProbeError(f"invalid reference observation: {line!r}")
        observed[name] = value
    expected = EXPECTED | ({"traceDelayed": "1"} if args.mode == "amd" else {})
    for name, value in expected.items():
        if observed.get(name) != value:
            raise ProbeError(f"observation {name}: expected {value}, got {observed.get(name)!r}")
    if observed.keys() != expected.keys():
        raise ProbeError(f"unexpected reference observations: {sorted(observed.keys() - expected.keys())}")

    claims_path = stem.with_suffix(".claims.json")
    claims = run([
        sys.executable, str(Path(__file__).with_name("native-claims.py")),
        "--translate", args.translate, "--opt", args.opt,
        "--corpus", str(program), "--name", f"bootstrap-data-{args.mode}",
        "--json", str(claims_path), "--min-claimed", "0", "--timeout", "120",
    ])
    stem.with_suffix(".claims.log").write_text(claims.stdout + claims.stderr)
    census = json.loads(claims_path.read_text())
    expected_total = 7 if args.mode == "amd" else 6
    if census["total"] != expected_total or census["skipped"] != 0:
        raise ProbeError(
            f"source functions lost: expected {expected_total} imported, "
            f"got {census['total']} total with {census['skipped']} skipped"
        )
    # native-claims already requires a diagnostic for every refusal. Increasing
    # native coverage is allowed; this checks source semantics and compile
    # coverage, not native execution of an entire Bootstrap factory.
    report = {**provenance, "reference_observations": observed, "native_census": census}
    stem.with_suffix(".json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"bootstrap-data ({args.mode}): post-factory observations {observed}")
    print(claims.stdout, end="")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bootstrap", type=Path, required=True)
    parser.add_argument("--mode", choices=ENVIRONMENTS, required=True)
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--reference")
    parser.add_argument("--translate")
    parser.add_argument("--opt")
    parser.add_argument("--generate-only", action="store_true")
    parser.add_argument("--negative-control", choices=("source-boundary", "trace"))
    args = parser.parse_args()
    if not args.generate_only and not all((args.reference, args.translate, args.opt)):
        parser.error("checking requires --reference, --translate and --opt")
    if args.generate_only and args.negative_control:
        parser.error("--generate-only cannot run a negative control")
    expected_error = {
        "source-boundary": "source structure changed: expected one Data/transitionend boundary, found 0",
        "trace": "observation traceReplacement: expected 43, got '44'",
    }.get(args.negative_control)
    try:
        check(args)
    except ProbeError as error:
        if expected_error and str(error) == expected_error:
            print(f"bootstrap-data negative control caught: {error}")
            return
        sys.exit(f"bootstrap-data: {error}")
    except (OSError, ValueError, subprocess.TimeoutExpired) as error:
        sys.exit(f"bootstrap-data: {error}")
    if expected_error:
        sys.exit(f"bootstrap-data: negative control did not fail with {expected_error!r}")


if __name__ == "__main__":
    main()
