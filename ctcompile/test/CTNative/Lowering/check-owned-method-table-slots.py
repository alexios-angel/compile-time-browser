#!/usr/bin/env python3
"""Check confined owning table slots, lifetime, admission and refusal boundaries."""

import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess


CTJS_FUNCTION = re.compile(r"^\s*ctjs\.func\b", re.M)
NATIVE_FUNCTION = re.compile(r"^\s*emitc\.func\b", re.M)
REFUSAL = re.compile(r'ctnative\.not_native = "((?:[^"\\]|\\.)*)"')
VM_SYMBOL = re.compile(r"ctbrowser::(?:script|aot)::")


def run(command, *, environment=None):
    result = subprocess.run(command, text=True, capture_output=True, timeout=120,
                            env=environment)
    if result.returncode:
        raise RuntimeError(f"{command!r}\n{result.stdout}{result.stderr}")
    return result


def sibling_test_tool(opt, name):
    executable = Path(shutil.which(opt) or opt).resolve()
    for parent in executable.parents:
        candidate = parent / "test" / name
        if candidate.is_file():
            return candidate
    raise RuntimeError(f"build {name} before running this execution regression")


def prepare(args, source, name):
    raw = args.work / f"{name}.raw.mlir"
    prepared = args.work / f"{name}.prepared.mlir"
    imported = run([args.translate, "--ctbrowser-js-to-ctjs", str(source),
                    "--mlir-print-debuginfo", "-o", str(raw)])
    text = raw.read_text()
    if "ctjs.skipped" in text or "is not compiled:" in imported.stderr:
        raise RuntimeError(f"{name}: a source function was skipped\n{imported.stderr}")
    count = len(CTJS_FUNCTION.findall(text))
    if not count:
        raise RuntimeError(f"{name}: no source functions imported")
    run([args.opt, str(raw), "--ctjs-resolve-globals", "--ctjs-lift-to-scf",
         "--mlir-print-debuginfo", "-o", str(prepared)])
    return prepared, count


def refused(args, prepared, name, *, imported=None):
    output = args.work / f"{name}.refused.mlir"
    run([args.opt, str(prepared), "--ctnative-lower-to-emitc=optimize=false",
         "--mlir-print-debuginfo", "-o", str(output)])
    text = output.read_text()
    remaining = len(CTJS_FUNCTION.findall(text))
    reasons = REFUSAL.findall(text)
    if not remaining or len(reasons) != remaining or re.search(r"\bemitc\.func @main\(", text):
        raise RuntimeError(f"{name}: unsafe unit was admitted or lacks refusal reasons\n{text}")
    if imported is not None and remaining + len(NATIVE_FUNCTION.findall(text)) != imported:
        raise RuntimeError(f"{name}: refusal silently changed the source denominator")
    if name == "mutable_capture":
        expected = ("returned closure capture 0 is a mutable or late-initialized binding; "
                    "it needs an owning shared cell")
        if not any(expected in reason for reason in reasons):
            raise RuntimeError(f"{name}: did not exercise mutable-capture ownership: {reasons}")
    elif name == "escaping_resource":
        expected = "returned method table field requires one closure created and stored only here"
        if not any(expected in reason for reason in reasons):
            raise RuntimeError(f"{name}: did not reject the resource-valued table field: {reasons}")
    elif not any("method table" in reason or "method field" in reason or
                 "owned method-table" in reason for reason in reasons):
        raise RuntimeError(f"{name}: did not exercise the table boundary: {reasons}")
    return output


def refusal_sources(factory):
    ordinary_run = "function run() { const table = publish(40); table.set(42); return table.get(); }\nvar result = run();\n"
    bodies = {
        "alias_rewrite": "const ns = {exports: makeData(seed)}; const alias = ns; alias.exports = makeData(seed + 1); return ns.exports;",
        "direct_rewrite": "const ns = {exports: makeData(seed)}; ns.exports = makeData(seed + 1); return ns.exports;",
        "early_read": "const ns = {}; const early = ns.exports; ns.exports = makeData(seed); return early;",
        "conditional_initialization": "const ns = {}; if (seed > 0) { ns.exports = makeData(seed); } return ns.exports;",
        "loop_initialization": "const ns = {}; for (var index = 0; index < seed; ++index) { ns.exports = makeData(index); } return ns.exports;",
        "passed_owner": "const ns = {exports: makeData(seed)}; inspect(ns); return ns.exports;",
        "unknown_effect": "const ns = {exports: makeData(seed)}; foreign(ns); return ns.exports;",
        "captured_owner": "const ns = {exports: makeData(seed)}; const read = () => ns.exports; return read();",
        "owner_identity": "const ns = {exports: makeData(seed)}; if (ns === ns) { return ns.exports; } return ns.exports;",
        "global_owner": "savedOwner = {exports: makeData(seed)}; return savedOwner.exports;",
        "realm_owner": "globalThis.exports = makeData(seed); return globalThis.exports;",
        "delete_slot": "const ns = {exports: makeData(seed)}; delete ns.exports; return ns.exports;",
        "prototype_change": "const ns = {exports: makeData(seed)}; ns.__proto__ = {}; return ns.exports;",
        "unrelated_prototype_change": "const ns = {exports: makeData(seed)}; const unrelated = {}; unrelated.__proto__ = {}; return ns.exports;",
        "accessor": "const ns = {exports: makeData(seed)}; Object.defineProperty(ns, 'exports', {get() { return makeData(seed); }}); return ns.exports;",
    }
    sources = {}
    for name, body in bodies.items():
        prelude = ""
        if name == "passed_owner":
            prelude = "function inspect(owner) { return 0; }\n"
        if name == "global_owner":
            prelude = "var savedOwner;\n"
        sources[name] = factory + prelude + f"function publish(seed) {{ {body} }}\n" + ordinary_run
    sources["escaped_owner"] = (factory +
        "function publish(seed) { const ns = {exports: makeData(seed)}; return ns; }\n"
        "function run() { const ns = publish(40); const table = ns.exports; table.set(42); return table.get(); }\nvar result = run();\n")
    sources["dynamic_key"] = (factory +
        "function publish(seed, key) { const ns = {exports: makeData(seed)}; return ns[key]; }\n"
        "function run() { const table = publish(40, 'exports'); table.set(42); return table.get(); }\nvar result = run();\n")
    sources["table_scalar_schema"] = (factory +
        "function choose(seed) { if (seed > 0) { return makeData(seed); } return 0; }\n"
        "function publish(seed) { const ns = {exports: choose(seed)}; return ns.exports; }\n" + ordinary_run)
    sources["different_table_schemas"] = (factory +
        "function other(seed) { return {get() { return seed; }, set(value) { return value; }}; }\n"
        "function choose(seed) { if (seed > 0) { return makeData(seed); } return other(seed); }\n"
        "function publish(seed) { const ns = {exports: choose(seed)}; return ns.exports; }\n" + ordinary_run)
    mutable = factory.replace("const state = new Map();", "let state = new Map();").replace(
        'set(value) { state.set("value", value);', 'set(value) { state = new Map(); state.set("value", value);')
    sources["mutable_capture"] = (mutable +
        "function publish(seed) { const ns = {exports: makeData(seed)}; return ns.exports; }\n" + ordinary_run)
    escaped_resource = factory.replace("return {", "return {resource: state,", 1)
    sources["escaping_resource"] = (escaped_resource +
        "function publish(seed) { const ns = {exports: makeData(seed)}; return ns.exports; }\n" + ordinary_run)
    return sources


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--translate", required=True)
    parser.add_argument("--opt", required=True)
    parser.add_argument("--reference")
    parser.add_argument("--specimen", type=Path, required=True)
    parser.add_argument("--fixtures", type=Path, required=True)
    parser.add_argument("--work", type=Path, required=True)
    args = parser.parse_args()
    args.work.mkdir(parents=True, exist_ok=True)
    tests = Path(__file__).resolve().parents[2]
    reference = args.reference or sibling_test_tool(args.opt, "ctcompile-test-native-reference")
    compilers = []
    for choices in [("g++-13", "g++"), ("clang++-18", "clang++")]:
        compiler = next((shutil.which(name) for name in choices if shutil.which(name)), None)
        if not compiler:
            raise RuntimeError("owning-slot regression requires " + " or ".join(choices))
        compilers.append(compiler)
    nm = shutil.which("nm") or shutil.which("llvm-nm")
    if not nm or not VM_SYMBOL.search(run([nm, "-C", str(reference)]).stdout):
        raise RuntimeError("VM symbol check cannot detect the interpreter in its positive control")

    programs = [
        ("specimen", args.specimen, 6, "result=4211\n", ["exports"]),
        ("lifetime", args.fixtures / "lifetime.js", 14,
         "independent101=101\nlifetime42=42\nnested42=42\nshared15=15\nstrings11=11\n", ["payload", "item"]),
        ("families", args.fixtures / "families.js", 10, "families4223=4223\n", []),
    ]
    flags = ["-std=c++23", "-Wall", "-Wextra", "-Werror", "-Wconversion", "-pedantic",
             "-ffp-contract=off"]
    sanitizer_environment = dict(os.environ,
        ASAN_OPTIONS="detect_stack_use_after_return=1:detect_leaks=1",
        UBSAN_OPTIONS="halt_on_error=1")
    for name, js, denominator, expected, fields in programs:
        _, imported = prepare(args, js, name)
        if imported != denominator:
            raise RuntimeError(f"{name}: expected {denominator} source functions, imported {imported}")
        oracle = run([str(reference), str(js)])
        if oracle.stdout != expected:
            raise RuntimeError(f"{name}: interpreter disagrees with fixture oracle\n{oracle.stdout}")
        module = args.work / f"{name}.emitc.mlir"
        run(["cmake", f"-DTRANSLATE={args.translate}", f"-DOPT={args.opt}",
             f"-DSOURCE={js}", f"-DOUTPUT={module}", "-DOPTIMIZE=OFF",
             "-P", str(tests / "native-pipeline.cmake")])
        native = module.read_text()
        if CTJS_FUNCTION.search(native) or REFUSAL.search(native) or len(NATIVE_FUNCTION.findall(native)) != denominator:
            raise RuntimeError(f"{name}: expected native {denominator}/{denominator}\n{native}")
        if name == "families":
            if len(re.findall(r"\bemitc\.class @ctn_exports\b", native)) != 1 or not re.search(
                    r'emitc\.field @exports : !emitc\.opaque<"T[0-9]+">', native):
                raise RuntimeError(f"{name}: expected one field-family template\n{native}")
        deduced = args.work / f"{name}.deduced.mlir"
        run([args.opt, str(module), "--ctnative-print-deduced", "--mlir-print-debuginfo",
             "-o", str(deduced)])
        for mode, ir in [("explicit", module), ("deduced", deduced)]:
            cpp = run([args.translate, "--mlir-to-cpp", str(ir)]).stdout
            if VM_SYMBOL.search(cpp) or re.search(r"#include [<\"]ctbrowser/", cpp):
                raise RuntimeError(f"{name}/{mode}: native output reaches the VM")
            for field in fields:
                if not re.search(r"std::shared_ptr<ctnative::method_\w+>\s+" + field + r"\s*;", cpp):
                    raise RuntimeError(f"{name}/{mode}: {field} does not own its method-table carrier\n{cpp}")
            if name == "families":
                tables = set(re.findall(r"ctn_exports<std::shared_ptr<ctnative::(method_\w+)>>", cpp))
                scalar = re.search(r"ctn_exports<(?:double|js_num)>", cpp)
                if len(tables) != 2 or not scalar:
                    raise RuntimeError(f"{name}/{mode}: missing distinct table/scalar instantiations\n{cpp}")
            source = args.work / f"{name}.{mode}.cpp"
            source.write_text(cpp)
            for index, compiler in enumerate(compilers):
                binary = (args.work / f"{name}.{mode}.{index}").resolve()
                run([compiler, *flags, "-O2", str(source), "-o", str(binary)])
                if VM_SYMBOL.search(run([nm, "-C", str(binary)]).stdout):
                    raise RuntimeError(f"{name}/{mode}: compiled binary reaches the VM")
                observed = run([str(binary)]).stdout
                if observed != expected:
                    raise RuntimeError(f"{name}/{mode}/{compiler}: {observed!r} != {expected!r}")
            sanitized = (args.work / f"{name}.{mode}.sanitized").resolve()
            run([compilers[1], *flags, "-O1", "-g", "-fno-omit-frame-pointer",
                 "-fsanitize=address,undefined", "-fsanitize-address-use-after-scope",
                 str(source), "-o", str(sanitized)])
            observed = run([str(sanitized)], environment=sanitizer_environment).stdout
            if observed != expected:
                raise RuntimeError(f"{name}/{mode}: lifetime/sanitizer execution changed output")

    specimen = args.specimen.read_text()
    factory = specimen[:specimen.index("function publish(")]
    prepared = {}
    sources = refusal_sources(factory)
    for name, source in sources.items():
        js = args.work / f"{name}.js"
        js.write_text(source)
        ir, imported = prepare(args, js, name)
        prepared[name] = ir
        refused(args, ir, name, imported=imported)

    # Supplied markers are not a live slot proof. Check both initial admission
    # and another lowering run over the already-refused result.
    unsafe = prepared["alias_rewrite"]
    forged = args.work / "forged.mlir"
    text = unsafe.read_text()
    marker = ' {ctnative.owned_method_table_slot = "forged", ctnative.method_table = "forged"}'
    text, changed = re.subn(r"\bctjs\.create_object(?!\s*\{)", "ctjs.create_object" + marker, text)
    if not changed:
        raise RuntimeError("forgery control did not mark any object producer")
    forged.write_text(text)
    first = refused(args, forged, "forged-first")
    refused(args, first, "forged-rerun")
    print(f"owned method-table slots: native 6/6 specimen=4211, 5 lifetime observations, "
          f"2 table schemas plus scalar share one field family, "
          f"{len(sources)} refusals, forged/rerun controls; explicit/deduced GCC/Clang and ASan/UBSan agree")


if __name__ == "__main__":
    main()
