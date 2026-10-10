"""Compile Bootstrap's unmodified browser UMD wrapper to native code and check it.

The wrapper publishes globalThis.bootstrap. The host-prefix pass must select
its browser branch, the native program must print the same observations as
the JS with no VM symbols linked, and every source mutation, missing contract
field, stale fingerprint and too-small step budget must be refused.
"""

import hashlib
import importlib.util
import json
from pathlib import Path
import re

from CTNative.HostContract.prefix import specialize as specialize_prefix

from .common import *
from .recorder import recorder_future_source, recorder_lifetime_cpp
from ..harness.map_results import run_sanitized_lifetime


def umd_source():
    """Return (UMD source, expected observations, host-prefix module).

    Extracts the browser UMD wrapper from the vendored Bootstrap bundle with
    tools/check/bootstrap-data-probe.py and fails if it is no longer the
    pinned 3218-byte program. parents[6] is the repository root.
    """
    root = Path(__file__).resolve().parents[6]
    modules = []
    for name in ("bootstrap-data-probe", "bootstrap-host-prefix"):
        spec = importlib.util.spec_from_file_location(name, root / "tools/check" / f"{name}.py")
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        modules.append(module)
    probe, prefix = modules
    fragment, _ = probe.extract(
        (root / "ctbrowser/vendor/bootstrap/bootstrap.bundle.js").read_text()
    )
    source = probe.generate(fragment, "browser")
    if (len(source.encode()), hashlib.sha256(source.encode()).hexdigest()) != (
        3218,
        prefix.PROVIDER_PROGRAM_SHA256["browser"],
    ):
        raise RuntimeError("browser UMD source changed its original 3218-byte pin")
    return source, probe.EXPECTED, prefix


def umd_contract(args, ir, name, observations):
    """Write the host contract for the UMD program: which globals are absent and
    which are observed, with globalThis.bootstrap as the owned root."""
    config = args.work / f"{name}.contract.json"
    value = host.manifest(
        args.opt, ir, absent=("define", "exports", "module", "self"), undefined=("undefined",)
    )
    value.update(
        roots=[{"binding": "globalThis", "properties": ["bootstrap"]}],
        observations=sorted(observations),
        initial_intrinsics=["Map", "Array"],
        realm_global_this=True,
    )
    config.write_text(json.dumps(value, indent=2) + "\n")
    return config


def prepare_umd(args, name="umd-browser", source=None):
    """Prepare the UMD source, or `source` when given, and run the host-prefix
    pass on it.

    Returns (js, prepared IR, specialized IR, expected observations).
    """
    pinned, values, prefix = umd_source()
    source = source or pinned
    js, prepared, functions = boundary.prepare(args, name, source.removesuffix("\n"))
    if js.read_bytes() != source.encode() or functions != 7:
        raise RuntimeError(f"{name}: preparation changed its source or seven functions")
    config = umd_contract(args, prepared, name, values)
    report_file = args.work / f"{name}.prefix.json"
    specialized = args.work / f"{name}.specialized.mlir"
    host.run(
        [
            args.opt,
            str(prepared),
            f"--ctnative-specialize-host-prefix=manifest={config} output={report_file} "
            + boundary.FOLLOW
            + " follow-provider-objects=true",
            "-o",
            str(specialized),
        ]
    )
    report = json.loads(report_file.read_text())
    prefix.check_provider_objects(report)
    if (
        not report["valid"]
        or report["full_host_contract_claimed"]
        or report["selected_branches"] != 5
        or report["targets"] != ["fn$3", *prefix.PROVIDER_OBJECT_TARGETS]
    ):
        raise RuntimeError("browser prefix no longer proves the exact UMD selection")
    before, after = prepared.read_text(), specialized.read_text()
    wrapper = re.search(r"ctjs\.func @fn\$2\(.*?(?=\n  ctjs\.func |\n})", after, re.S)
    if (
        len(boundary.FUNCTION.findall(after)) != functions
        or not wrapper
        or "scf.if" in wrapper[0]
        or len(re.findall(r"ctjs.call_direct @fn\$3\([^,]+, [^,]+, %arg4\)", wrapper[0])) != 1
        or not re.search(r"ctjs.call_direct @fn\$2\([^,]+, [^,]+, [^,]+, %arg0, [^)]+\)", after)
    ):
        raise RuntimeError("browser prefix lost the original factory/fallback operands")
    for operation in ("construct", "create_cell", "create_closure", "store_global"):
        if before.count("ctjs." + operation) != after.count("ctjs." + operation):
            raise RuntimeError(f"browser prefix removed runtime {operation}")
    return js, prepared, specialized, values


def observe_umd(args, js, values):
    return observe(args, js.stem, js.read_text(), values)


def umd_mutations(text):
    """Return {name: (mutated IR, expected refusal reason)} for the UMD wrapper."""
    match = re.search(r"ctjs\.func @fn\$2\(.*?(?=\n  ctjs\.func |\n})", text, re.S)
    if not match:
        raise RuntimeError("UMD mutations lost the actual wrapper")
    wrapper = match[0]
    exports = re.search(r'(%[-\w.$]+) = ctjs.load_global "exports"(?: \{[^}\n]*\})?', wrapper)
    publication = re.search(r"ctjs.set_property (%[-\w.$]+)\[", wrapper)
    if not exports or "typeof_lookup = true" not in exports[0] or not publication:
        raise RuntimeError("UMD mutations lost the retained absent load or publication")
    cases = {
        "fallback_returned": (
            re.sub(r"ctjs.return %[-\w.$]+", "ctjs.return %arg3", wrapper),
            None,
        ),
        "fallback_published": (
            wrapper.replace(publication[0], "ctjs.set_property %arg3["),
            None,
        ),
        "absent_bare_read": (
            wrapper.replace(
                exports[0],
                exports[0] + '\n    %umd_hard_read = ctjs.load_global "exports"\n'
                '    ctjs.store_global "traceErrorCount", %umd_hard_read',
            ),
            None,
        ),
        "absent_hard_typeof": (
            wrapper.replace(exports[0], exports[0].replace(" {typeof_lookup = true}", "")),
            None,
        ),
        "absent_explicit_hard_typeof": (
            wrapper.replace(
                exports[0], exports[0].replace("typeof_lookup = true", "typeof_lookup = false")
            ),
            None,
        ),
        "absent_written": (
            wrapper.replace(
                exports[0],
                "%umd_written = ctjs.constant #ctjs.undefined\n"
                '    ctjs.store_global "exports", %umd_written\n    ' + exports[0],
            ),
            "an absent host binding has a source write",
        ),
    }
    return {
        name: (text[: match.start()] + changed + text[match.end() :], reason)
        for name, (changed, reason) in cases.items()
    }


def execute_umd(args, output, name, expected, values, compilers, nm):
    """Translate `output` to C++, check its 23 Data calls, and run it standalone
    and under the sanitized lifetime harness."""
    deduced = args.work / f"{name}.deduced.mlir"
    host.run([args.opt, str(output), "--ctnative-print-deduced", "-o", str(deduced)])
    source, _, _ = umd_source()
    expected_calls = re.findall(r"globalThis\.bootstrap\.(set|get|remove)\(", source)
    for mode, ir in (("explicit", output), ("deduced", deduced)):
        cpp = host.run([args.translate, "--mlir-to-cpp", str(ir)]).stdout
        entry = re.search(r"\bmain\(\)\s*\{(.*?)^\}", cpp, re.M | re.S)
        if owned.VM.search(cpp) or not entry or "std::shared_ptr<ctn_bootstrap>" not in cpp:
            raise RuntimeError(f"{name}/{mode}: missing standalone UMD owner")
        methods_by_value = dict(
            re.findall(r"(\w+)\s*=\s*ctnative::method_get<&[^>\n]+::m_(\w+)>\(", entry[1])
        )
        calls = re.findall(r"ctnative::invoke_callable\((\w+)([^;\n]*)\);", entry[1])
        if (
            [methods_by_value[callee] for callee, _ in calls if callee in methods_by_value]
            != expected_calls
            or len(expected_calls) != 23
            # 76616c7565 is the hex spelling of the field name "value".
            or len(re.findall(r"ctnative::object_get_field_76616c7565\(", cpp)) != 1
        ):
            raise RuntimeError(f"{name}/{mode}: changed the 23 Data calls or payload field read")
        native = args.work / f"{name}.{mode}.cpp"
        native.write_text(cpp)
        for index, compiler in enumerate(compilers):
            binary = native.with_suffix(f".{index}").resolve()
            host.run([compiler, *owned.FLAGS, str(native), "-o", str(binary)])
            if owned.VM.search(host.run([nm, "-C", str(binary)]).stdout):
                raise RuntimeError(f"{name}/{mode}: linked VM symbols")
            result = host.run([str(binary)])
            if result.stdout != expected or result.stderr:
                raise RuntimeError(f"{name}/{mode}: standalone UMD observation mismatch")

        # Reuse the lifetime observer from the Bootstrap Data test, pointing its
        # final access at g_globalThis->bootstrap (where UMD publishes) instead
        # of g_host->slot. The emitted C++ is not edited.
        observed, separator, observer = recorder_lifetime_cpp(cpp, values, True).rpartition(
            "\nint main() {"
        )
        if not separator:
            raise RuntimeError("UMD lifetime observer lost its appended entry")
        lifetime = args.work / f"{name}.{mode}.lifetime.cpp"
        lifetime.write_text(
            observed
            + separator
            + observer.replace("g_host", "g_globalThis").replace("->slot", "->bootstrap")
        )
        binary = lifetime.with_suffix(".sanitized").resolve()
        result = run_sanitized_lifetime(lifetime, binary, compilers[1])
        if owned.VM.search(host.run([nm, "-C", str(binary)]).stdout):
            raise RuntimeError(f"{name}/{mode}: linked VM symbols in UMD lifetime")
        if result.returncode or result.stdout != expected * 2 or result.stderr:
            raise RuntimeError(f"{name}/{mode}: UMD future/reentry/final-owner lifetime failed")


def check_umd_preparation(args, compilers, nm):
    """Check UMD-shaped wrappers that preparation may admit or must refuse."""
    source = (
        SOURCE.replace(
            "var host = {};",
            "var host = {}; var tracePre = 0; var traceKind = typeof host === 'object' ? 1 : 0;",
        )
        .replace("function(factory)", "function(unused, factory)")
        .replace("})(function()", "})((tracePre = 1, this), function()")
    )
    js, ir, functions = boundary.prepare(args, "umd-safe-preparation", source.removesuffix("\n"))
    if functions != 4 or js.read_text() != source or "tracePre = 1" not in source:
        raise RuntimeError("safe UMD preparation changed its four-function source")
    expected = observe_umd(args, js, {"trace": 0, "traceKind": 1, "tracePre": 1})

    def configure(subject, name):
        config = contract(args, subject, name)
        value = json.loads(config.read_text())
        value["observations"] = ["trace", "traceKind", "tracePre"]
        config.write_text(json.dumps(value, indent=2) + "\n")
        return config

    config = configure(ir, "umd-safe-preparation")
    for policy, options in (("default", ""), ("disabled", "optimize=false")):
        output = owned.lower(args, ir, "umd-safe-" + policy, config, options=options)
        methods.census(output, 4, policy, admitted=4)
        owned.standalone(args, output, "umd-safe-" + policy, expected, compilers, nm)
    refusals = {
        "returned": source.replace(
            "host.slot = factory();", "host.slot = factory(); return unused;"
        ),
        "arguments": source.replace(
            "host.slot = factory();", "host.slot = factory(); tracePre = arguments.length;"
        ),
        "unproved_increment": source.replace("tracePre = 1, this", "tracePre = tracePre + 1, this"),
    }
    for name, changed in refusals.items():
        js, subject, count = boundary.prepare(args, "umd-safe-" + name, changed.removesuffix("\n"))
        if count != 4:
            raise RuntimeError("observed UMD actual lost its original four functions")
        observe_umd(
            args, js, {"trace": 0, "traceKind": 1, "tracePre": 2 if name == "arguments" else 1}
        )
        fresh = configure(subject, "umd-safe-" + name)
        for policy, options in (("default", ""), ("disabled", "optimize=false")):
            output = methods.refused(
                args,
                subject,
                "umd-safe-" + name + "-" + policy,
                fresh,
                options=options,
                admitted=0,
            )
            check_call_preservation(subject.read_text(), output.read_text(), name)


def check_umd(args, node, reference, compilers, nm):
    """Run the full browser UMD check described in the module docstring."""
    js, prepared, specialized, values = prepare_umd(args)
    expected = observe_umd(args, js, values)
    raw_config = umd_contract(args, prepared, "umd-raw", values)
    config = umd_contract(args, specialized, "umd-specialized", values)
    source, _, _ = umd_source()
    future = args.work / "umd-browser-future.js"
    future.write_text(
        source
        + recorder_future_source(True)
        .replace("host.slot", "globalThis.bootstrap")
        .replace("host = {};", "globalThis = {};")
    )
    future_values = dict(
        values,
        traceFuture=1,
        traceErrorCount=int(values["traceErrorCount"]) + 2048,
        traceErrorMessage=0,
    )
    observe_umd(args, future, future_values)

    def refused(subject, name, manifest, options, reason=None):
        output = methods.refused(
            args, subject, name, manifest, options=options, reason=reason, admitted=0
        )
        check_call_preservation(subject.read_text(), output.read_text(), name)
        signatures = r"ctjs.func @[^\n]+?\) -> !ctjs.value"
        if re.findall(signatures, subject.read_text()) != re.findall(
            signatures, output.read_text()
        ):
            raise RuntimeError(f"{name}: failed proof changed original function arguments")
        return output

    def native(subject, name, manifest, options):
        output = owned.lower(args, subject, name, manifest, options=options)
        text = methods.census(output, 7, name, admitted=7)
        if "ctnative.host_owner_proved = true" not in text:
            raise RuntimeError(f"{name}: original UMD lost complete ownership")
        return output

    def without(manifest, field, name):
        missing = args.work / f"{name}.json"
        value = json.loads(manifest.read_text())
        value[field] = []
        missing.write_text(json.dumps(value, indent=2) + "\n")
        return missing

    def expect(subject, name, manifest, options, admitted, reason=None):
        """Return None when `name` lowers as expected, else what was observed."""
        try:
            if admitted:
                native(subject, name, manifest, options)
            else:
                refused(subject, name, manifest, options, reason)
            return None
        except Exception as error:
            output = args.work / f"{name}.native.mlir"
            count = len(boundary.NATIVE.findall(output.read_text())) if output.exists() else "no"
            first = str(error).splitlines()[0] if str(error) else type(error).__name__
            return f"{name}: expected {7 if admitted else 0}/7 native, observed {count}: {first}"

    # ResolveGlobals derives absence itself (NativeRealm.h): `define`,
    # `exports` and `module` are never stored, never declared and not
    # provided by the native realm, so their `typeof` reads fold to
    # "undefined" before the host contract is read, and an empty
    # absent_bindings list leaves no read unproved. `undefined` is provided
    # by every realm and is never derived, so its contract entry is still
    # required. A `var define;` declaration keeps `define` a binding of the
    # realm; absence is then not derived and the contract entry is required
    # again, while the full contract still proves the program.
    mismatches = []
    try:
        declared = prepare_umd(args, "umd-declared-define", "var define;\n" + source)[2]
        declared_config = umd_contract(args, declared, "umd-declared-define", values)
    except Exception as error:
        declared = None
        mismatches.append(f"umd-declared-define: preparation failed: {error}")
    # Because the fold proves `define`, `exports` and `module` absent, their
    # typeof loads no longer exist in the plain UMD IR, and the source
    # mutations and the explicit-false hard-read control below edit the
    # `exports` load.
    # Their subject is the wrapper with `var exports;` in front: the
    # declaration keeps `exports` a realm binding, so its typeof load
    # survives with the same lookup mode, still ahead of every branch, and
    # the full contract still proves the unmutated program.
    try:
        _, mutable_prepared, mutable, _ = prepare_umd(
            args, "umd-declared-exports", "var exports;\n" + source
        )
        mutable_config = umd_contract(args, mutable, "umd-declared-exports", values)
    except Exception as error:
        mutable = None
        mismatches.append(f"umd-declared-exports: preparation failed: {error}")
    for policy, options in (("default", ""), ("disabled", "optimize=false")):
        flags = options + " host-max-steps=1000000"
        for field, admitted in (("absent_bindings", True), ("undefined_bindings", False)):
            name = f"umd-{policy}-missing-{field}"
            mismatches.append(
                expect(specialized, name, without(config, field, name), flags, admitted)
            )
        if mutable:
            name = f"umd-{policy}-declared-exports"
            mismatches.append(expect(mutable, name, mutable_config, flags, True))
        if not declared:
            continue
        name = f"umd-{policy}-declared-define"
        mismatches.append(expect(declared, name, declared_config, flags, True))
        mismatches.append(
            expect(
                declared,
                name + "-missing-absent_bindings",
                without(declared_config, "absent_bindings", name + "-missing-absent_bindings"),
                flags,
                False,
                "unproved host binding `define`",
            )
        )
    # Every variant runs before any fails, so one run reports all of them.
    mismatches = [mismatch for mismatch in mismatches if mismatch]
    if mismatches:
        raise RuntimeError("umd contract variants:\n" + "\n".join(mismatches))

    for policy, options in (("default", ""), ("disabled", "optimize=false")):
        flags = options + " host-max-steps=1000000"
        refused(prepared, "umd-raw-" + policy, raw_config, flags)
        output = native(specialized, "umd-" + policy, config, flags)
        execute_umd(args, output, "umd-" + policy, expected, values, compilers, nm)
        refused(specialized, "umd-" + policy + "-default-budget", config, options)
        for steps in (0, 32, 100_000):
            refused(
                specialized,
                f"umd-{policy}-budget-{steps}",
                config,
                options + f" host-max-steps={steps}",
                "budget" if steps < 100_000 else None,
            )
        forged = args.work / f"umd-{policy}-forged.mlir"
        forged.write_text(forge_leaf_evidence(specialized.read_text()))
        refused(forged, f"umd-{policy}-forged-stale", config, flags, "fingerprint mismatch")
        fresh = umd_contract(args, forged, f"umd-{policy}-forged-fresh", values)
        checked = native(forged, f"umd-{policy}-forged-fresh", fresh, flags)
        if comparable_provenance(
            host.run([args.translate, "--mlir-to-cpp", str(checked)]).stdout, forged
        ) != comparable_provenance(
            host.run([args.translate, "--mlir-to-cpp", str(output)]).stdout, specialized
        ):
            raise RuntimeError(f"umd-{policy}: fresh forged reports changed emitted C++")
        for name, (text, reason) in umd_mutations(mutable.read_text()).items():
            subject = args.work / f"umd-{policy}-{name}.mlir"
            unprinted = subject.with_suffix(".unprinted.mlir")
            unprinted.write_text(text)
            # Reprint through opt so SSA names are canonical; otherwise a
            # mutation that inserts a result before existing calls would break
            # the operand-preservation comparison.
            host.run([args.opt, str(unprinted), "-o", str(subject)])
            refused(
                subject, f"umd-{policy}-{name}-stale", mutable_config, flags, "fingerprint mismatch"
            )
            fresh = umd_contract(args, subject, f"umd-{policy}-{name}", values)
            refused(subject, f"umd-{policy}-{name}", fresh, flags, reason)

    # The host-prefix pass may fold `typeof exports` only when the load is in
    # typeof mode, where an absent global gives "undefined". A plain read of an
    # absent global throws ReferenceError, so the pass must stop there even when
    # a TypeOf op is its only user. Check the pass's report directly, because a
    # later native refusal would hide a wrong fold.
    ordinary_sources = {
        "comma": source.replace("typeof exports", "typeof (0, exports)", 1),
        "alias": source.replace(
            "! function(t, e) {\n", "! function(t, e) {\n    var savedExports = exports;\n", 1
        ).replace("typeof exports", "typeof savedExports", 1),
    }
    subjects = {}
    for name, changed in ordinary_sources.items():
        if changed == source:
            raise RuntimeError(f"{name}: failed to construct a source hard-read control")
        _, subject, count = boundary.prepare(args, "umd-hard-" + name, changed.removesuffix("\n"))
        if count != 7:
            raise RuntimeError(f"{name}: source hard read changed the seven-function denominator")
        observed = args.work / f"umd-hard-{name}-observed.js"
        observed.write_text(
            "function probe() {\n" + changed + "}\nvar traceCaught = 0;\n"
            "try { probe(); } catch (error) { traceCaught = error.name === 'ReferenceError' ? 1 : 2; }\n"
        )
        observe_umd(args, observed, {"traceCaught": 1})
        subjects[name] = subject
    explicit = args.work / "umd-hard-explicit-false.mlir"
    text, replacements = re.subn(
        r'(ctjs.load_global "exports" \{)typeof_lookup = true(\})',
        r"\1typeof_lookup = false\2",
        mutable_prepared.read_text(),
    )
    if replacements != 1:
        raise RuntimeError("explicit hard-read control lost its actual source lookup mode")
    explicit.write_text(text)
    subjects["explicit-false"] = explicit
    for name, subject in subjects.items():
        fresh = umd_contract(args, subject, "umd-hard-" + name, values)
        report, stopped, _ = specialize_prefix(
            args.opt,
            subject,
            json.loads(fresh.read_text()),
            args.work / f"umd-hard-{name}-prefix",
            options=boundary.FOLLOW + " follow-provider-objects=true",
        )
        if (
            not report["valid"]
            or report["full_host_contract_claimed"]
            or report["selected_branches"]
            or report["summarized_factories"]
            or report["summarized_provider_calls"]
            or report["publication_writes"]
            or "absent binding lacks source typeof lookup mode" not in report["boundary"]
        ):
            raise RuntimeError(f"{name}: prefix silently passed an ordinary absent read: {report}")
        check_call_preservation(subject.read_text(), stopped.read_text(), name)
        reads = r'ctjs.load_global "[^"\n]*"'
        if stopped.read_text().count("scf.if") != subject.read_text().count("scf.if") or re.findall(
            reads, stopped.read_text()
        ) != re.findall(reads, subject.read_text()):
            raise RuntimeError(f"{name}: prefix erased a read or branch after a throwing read")
        fresh = umd_contract(args, stopped, "umd-hard-" + name + "-stopped", values)
        for policy, options in (("default", ""), ("disabled", "optimize=false")):
            refused(
                stopped, f"umd-hard-{name}-{policy}", fresh, options + " host-max-steps=1000000"
            )
    check_umd_preparation(args, compilers, nm)
    print(
        "original browser UMD: 3218 unchanged bytes, 19 typed observations, 23 live Data calls; "
        "7/7 native at explicit 1m, both policies/layouts/GCC/Clang and lifetime sanitizers; "
        "raw/default/missing/stale/observed and hard absent reads refuse; prefix stops at "
        "comma/alias/explicit-false reads; four-function evaluated-actual/root-typeof control passes"
    )
