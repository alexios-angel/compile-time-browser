#!/usr/bin/env python3
"""Execute finite Map key/value storage and independently proved read types."""

import argparse
import importlib.util
import os
from pathlib import Path
import re
import shutil

from CTNative.harness import CORE_INCLUDE, RUNTIME_INCLUDE, find_compilers

spec = importlib.util.spec_from_file_location(
    "representation", Path(__file__).with_name("map-representation.py")
)
representation = importlib.util.module_from_spec(spec)
spec.loader.exec_module(representation)
boundary, run = representation.boundary, representation.run


def number_object_lifetime(cpp, ordered):
    factory = re.search(r"\b(ownedNumberObjectKeys_\d+)\(\)", cpp)
    assert factory
    changed, count = re.subn(r"\bmain\(\)", "ctnative_test_entry()", cpp)
    assert count == 1
    # Inspect the emitted Map's actual owners after its factory frame is gone.
    # Weak observations do not keep a deleted key alive themselves.
    observer = r"""
int main() {
    if (ctnative_test_entry() != 0) { return 90; }
    for (int round = 0; round < 128; ++round) {
        auto map = FACTORY();
        std::weak_ptr<ctnative::identity_object> key;
        for (const auto & entry : ENTRIES) {
            std::visit([&](const auto & alternative) {
                if constexpr (std::is_same_v<std::decay_t<decltype(alternative)>,
                                             std::shared_ptr<ctnative::identity_object>>) {
                    key = alternative;
                }
            }, entry.first);
        }
        if (key.expired() || map->size() != 2) { return 91; }
        auto alias = map;
        map.reset();
        if (key.expired()) { return 92; }
        if (round % 2 == 0) { alias->clear(); }
        else { alias.reset(); }
        if (!key.expired()) { return 93; }
    }
    return 0;
}
"""
    return changed + observer.replace("FACTORY", factory[1]).replace(
        "ENTRIES", "map->entries" if ordered else "*map"
    )


def forge_map_facts(text, read_type):
    def replace(match):
        attributes = (match[2] or "{}")[1:-1]
        attributes = re.sub(
            r"(?:,\s*)?ctnative\.map_(?:present(?:\s*=\s*(?:true|false))?"
            r'|(?:read_type|write_type|key_type)\s*=\s*"[^"]*")',
            "",
            attributes,
        )
        attributes = attributes.strip().removeprefix(",").strip()
        forged = (
            'ctnative.map_present = true, ctnative.map_read_type = "'
            + read_type
            + '", ctnative.map_write_type = "string", ctnative.map_key_type = "string"'
        )
        return match[1].rstrip() + " {" + forged + (", " + attributes if attributes else "") + "}"

    return re.sub(r"(^\s*%[-\w.$]+ = ctjs\.call [^\n{]+)(\{[^\n}]*\})?", replace, text, flags=re.M)


def check_nullable_read_facts(args, source):
    _, prepared, _ = boundary.prepare(args, "mixed-nullable-read-facts", source)
    proved = run([args.opt, "--ctnative-binding-time-analysis", str(prepared)]).stdout
    assert proved.count('ctnative.map_read_type = "nullable_string"') == 1
    assert proved.count('ctnative.map_read_type = "bool"') == 1
    # Derivation also runs before native types exist. Both a fresh bogus claim
    # and one inserted over prior derived facts must reproduce the live proof.
    for phase, text in [("fresh", prepared.read_text()), ("stale", proved)]:
        for tag in ("bool", "nullable_string"):
            forged = forge_map_facts(text, tag)
            assert forged != text
            path = args.work / f"mixed-nullable-read-{phase}-{tag}.mlir"
            path.write_text(forged)
            assert run([args.opt, "--ctnative-binding-time-analysis", str(path)]).stdout == proved


def check_isolated_nullable_helpers(args, source, node, reference, compilers, nm):
    # Either nullable keys or nullable payloads must request their helpers alone.
    # Reuse existing functions without expanding the full layout/sanitizer matrix.
    cases = [
        (
            "nullable-numbers-only",
            "nullableNumberKeys",
            "traceNullableNumbers",
            41234,
            "ctnative::number_map<ctnative::nullable_string>",
            "nullableNumberKeys()",
        ),
        (
            "nullable-payloads-only",
            "nullablePayloadTags",
            "traceNullablePayloadTags",
            1023,
            "map_storage<double, ctnative::nullable_string>",
            "nullablePayloadTags()",
        ),
        (
            "mixed-nullable-read-only",
            "mixedNullableRead",
            "traceMixedNullableRead",
            17182024,
            "map_storage<std::string, std::variant<bool, ctnative::nullable_string>>",
            "mixedNullableRead(true, false) * 1000000 + mixedNullableRead(true, true) * 10000"
            " + mixedNullableRead(false, false) * 100 + mixedNullableRead(false, true)",
        ),
    ]
    for name, symbol, trace, value, carrier, expression in cases:
        function = re.search(r"^function " + symbol + r"\([^)]*\) \{\n.*?^\}", source, re.M | re.S)
        assert function
        js = args.work / f"{name}.js"
        js.write_text(function[0] + f"\nvar {trace} = {expression};\n")
        expected = f"{trace}={value}\n"
        assert run([node, "-e", representation.NODE_GLOBALS, str(js)]).stdout == expected
        assert run([str(reference), str(js)]).stdout == expected
        if symbol == "mixedNullableRead":
            check_nullable_read_facts(args, js.read_text())
        ir = args.work / f"{name}.mlir"
        run(
            [
                "cmake",
                f"-DTRANSLATE={args.translate}",
                f"-DOPT={args.opt}",
                f"-DSOURCE={js}",
                f"-DOUTPUT={ir}",
                "-DOPTIMIZE=OFF",
                "-P",
                str(Path(__file__).resolve().parents[3] / "CTNative/Checks/pipeline.cmake"),
            ]
        )
        cpp = run([args.translate, "--mlir-to-cpp", str(ir)]).stdout
        assert '#include "ctcompile/CTNative/Runtime/ctnative.hpp"' in cpp
        assert carrier in cpp
        assert "ctbrowser::script" not in cpp
        if symbol == "mixedNullableRead":
            assert "ctnative::map_get_present_nullable_as<ctnative::nullable_string>" in cpp
        out = args.work / f"{name}.cpp"
        out.write_text(cpp)
        for index, compiler in enumerate(compilers):
            binary = args.work / f"{name}-{index}"
            run(
                [
                    compiler,
                    "-std=c++23",
                    RUNTIME_INCLUDE,
                    CORE_INCLUDE,
                    "-O2",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-Wconversion",
                    "-pedantic",
                    "-ffp-contract=off",
                    str(out),
                    "-o",
                    str(binary),
                ]
            )
            assert run([str(binary)]).stdout == expected
            assert "ctbrowser::script::" not in run([nm, "-C", str(binary)]).stdout


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fixtures", type=Path, required=True)
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--translate", required=True)
    parser.add_argument("--opt", required=True)
    parser.add_argument("--node", required=True)
    parser.add_argument("--reference", required=True)
    args = parser.parse_args()
    args.work.mkdir(parents=True, exist_ok=True)
    node, reference = args.node, args.reference
    compilers = find_compilers()
    nm = shutil.which("nm")
    assert all(compilers) and nm
    source = (args.fixtures / "mixed.js").read_text()
    for ordered in (False, True):
        name = "ordered" if ordered else "associative"
        text = source + ((args.fixtures / "snapshot.js").read_text() if ordered else "")
        js = args.work / f"{name}.js"
        js.write_text(text)
        expected = (
            "traceBranches=23\ntraceDead=2\ntraceGuardBoolean=12\ntraceGuardDisjoint=117\n"
            "traceGuardNumber=133\ntraceGuardString=12\n"
            "traceMixedNullableBranches=1111\ntraceMixedNullableJoin=1111\n"
            "traceMixedNullablePayload=3131\ntraceMixedNullableRead=17182024\n"
            "traceMixedNullableTags=2047\n"
            "traceNullableNumbers=41234\ntraceNullableOwned=151515\n"
            "traceNullablePayloadOwned=111\ntraceNullablePayloadTags=1023\n"
            "traceNullablePerUse=3131\ntraceNullableTags=4095\n"
            "traceNumberObjectKeys=63\ntraceNumberObjectOwned=2\ntraceNumberObjectPayload=1\n"
            "traceNumbers=1334\ntraceRewrite=1\n"
            "traceSaved=1\ntraceSavedAlias=82\ntraceSavedBoolean=1\ntraceSavedBranch=11\n"
            "traceSavedCall=1\ntraceSavedJoinBoolean=12\ntraceSavedJoinNumber=56\n"
            "traceSavedJoinString=12\ntraceSavedNumber=42\n"
            "traceShortBoolean=11\ntraceShortFalsy=12\ntraceShortNumber=1133\ntraceShortSaved=11\n"
            "traceShortString=111\ntraceShortTemporary=12\n"
        )
        if ordered:
            expected += "traceSnapshot=1\n"
        assert run([node, "-e", representation.NODE_GLOBALS, str(js)]).stdout == expected
        assert run([str(reference), str(js)]).stdout == expected
        ir = args.work / f"{name}.mlir"
        run(
            [
                "cmake",
                f"-DTRANSLATE={args.translate}",
                f"-DOPT={args.opt}",
                f"-DSOURCE={js}",
                f"-DOUTPUT={ir}",
                "-DOPTIMIZE=OFF",
                "-P",
                str(Path(__file__).resolve().parents[3] / "CTNative/Checks/pipeline.cmake"),
            ]
        )
        deduced = args.work / f"{name}-deduced.mlir"
        run([args.opt, "--ctnative-print-deduced", str(ir), "-o", str(deduced)])
        for label, module in [("plain", ir), ("deduced", deduced)]:
            cpp = run([args.translate, "--mlir-to-cpp", str(module)]).stdout
            assert "ctbrowser::script" not in cpp
            assert "std::variant<bool," in cpp and "map_get_present_as<" in cpp
            assert "ctnative::number_map<ctnative::nullable_string>" in cpp
            assert "map_storage<ctnative::nullable_string, std::variant<bool, std::string>>" in cpp
            assert "std::variant<bool, ctnative::nullable_string>" in cpp
            assert "map_storage<double, ctnative::nullable_string>" in cpp
            assert "map_storage<ctnative::nullable_string, ctnative::nullable_string>" in cpp
            assert "map_storage<std::string, std::variant<bool, ctnative::nullable_string>>" in cpp
            assert "ctnative::map_get_present_nullable_as<ctnative::nullable_string>" in cpp
            key_carrier = "std::variant<double, std::shared_ptr<ctnative::identity_object>>"
            assert f"ctnative::number_map<{key_carrier}>" in cpp
            assert f"ctnative::map_storage<{key_carrier}, ctnative::object_value>" in cpp
            assert ("#define CTNATIVE_ORDERED_MAPS 1" in cpp) == ordered
            out = args.work / f"{name}-{label}.cpp"
            out.write_text(number_object_lifetime(cpp, ordered))
            for index, compiler in enumerate(compilers):
                binary = args.work / f"{name}-{label}-{index}"
                run(
                    [
                        compiler,
                        "-std=c++23",
                        RUNTIME_INCLUDE,
                        CORE_INCLUDE,
                        "-O2",
                        "-Wall",
                        "-Wextra",
                        "-Werror",
                        "-Wconversion",
                        "-pedantic",
                        "-ffp-contract=off",
                        str(out),
                        "-o",
                        str(binary),
                    ]
                )
                assert run([str(binary)]).stdout == expected
                assert "ctbrowser::script::" not in run([nm, "-C", str(binary)]).stdout
            if label == "plain":
                binary = args.work / f"{name}-sanitized"
                run(
                    [
                        compilers[1],
                        "-std=c++23",
                        RUNTIME_INCLUDE,
                        CORE_INCLUDE,
                        "-O1",
                        "-g",
                        "-fno-omit-frame-pointer",
                        "-fsanitize=address,undefined",
                        "-fsanitize-address-use-after-scope",
                        str(out),
                        "-o",
                        str(binary),
                    ]
                )
                environment = dict(
                    os.environ,
                    ASAN_OPTIONS="detect_leaks=1:detect_stack_use_after_return=1",
                    UBSAN_OPTIONS="halt_on_error=1",
                )
                assert run([str(binary)], environment=environment).stdout == expected
    check_isolated_nullable_helpers(args, source, node, reference, compilers, nm)
    for fixture in args.fixtures.glob("*-refused.js"):
        name = fixture.stem
        js, ir, count = boundary.prepare(args, name, fixture.read_text())
        expected = run([node, "-e", representation.NODE_GLOBALS, str(js)]).stdout
        assert run([str(reference), str(js)]).stdout == expected
        before = ir.read_text()
        forged = forge_map_facts(before, "bool")
        assert forged != before
        variants = [("original", before), ("forged", forged)]
        if name.startswith("mixed-nullable"):
            variants.append(("nullable-forged", forge_map_facts(before, "nullable_string")))
        for label, contents in variants:
            current = args.work / f"{name}-{label}.mlir"
            current.write_text(contents)
            for repeat in range(2):
                output = args.work / f"{name}-{label}-{repeat}.mlir"
                run(
                    [
                        args.opt,
                        "--ctnative-lower-to-emitc=optimize=false",
                        str(current),
                        "-o",
                        str(output),
                    ]
                )
                result = output.read_text()
                assert not re.search(r"\bemitc.func @main\(", result), name
                assert (
                    len(boundary.FUNCTION.findall(result)) + len(boundary.NATIVE.findall(result))
                    == count
                )
                if name == "number-object-extra-alternative-refused":
                    assert "native Map needs supported keys" in result, name
                    assert "!ctnative.variant<" in result, name
                elif name in {"saved-missing-refused", "saved-join-missing-refused"}:
                    assert "native Map needs supported keys" in result, name
                    assert "!ctnative.opt<!ctnative.variant<" in result, name
                elif name in {
                    "saved-join-tags-refused",
                    "short-truthy-bool-refused",
                    "short-wrong-condition-refused",
                }:
                    assert (
                        "mixed native Map write needs one proved scalar alternative" in result
                    ), name
                elif name in {
                    "nullable-number-key-refused",
                    "nullable-boolean-key-refused",
                    "nullable-number-payload-refused",
                    "nullable-boolean-payload-refused",
                }:
                    assert "native Map needs supported keys" in result, name
                    assert "!ctnative.opt<" in result, name
                elif name in {
                    "nullable-snapshot-refused",
                    "mixed-nullable-snapshot-refused",
                    "nullable-payload-snapshot-refused",
                    "mixed-nullable-payload-snapshot-refused",
                }:
                    assert (
                        "native Map snapshot requires confined numeric or string elements" in result
                    ), name
                elif name in {
                    "mixed-nullable-temporary-refused",
                    "mixed-nullable-payload-temporary-refused",
                }:
                    assert "a value of type !ctnative.opt<!ctnative.variant<" in result, name
                elif name == "mixed-nullable-branch-callee-refused":
                    assert (
                        "native Map instance escapes or is mutated through `ctjs.call`" in result
                    ), name
                else:
                    assert (
                        "mixed native Map read needs independent present payload type evidence"
                        in result
                    ), name
                current = output
                if label == "nullable-forged" and repeat == 0:
                    stale = forge_map_facts(result, "nullable_string")
                    assert stale != result
                    current = args.work / f"{name}-nullable-stale.mlir"
                    current.write_text(stale)
    print(
        "mixed Maps: 75 associative/ordered observations, owning nullable String and Boolean/String keys/payloads, "
        "Number/Object identity keys with unchanged object payloads and 128 owner-release rounds; "
        "Node/interpreter, GCC/Clang, plain/deduced and ASan/UBSan; "
        "isolated nullable-key, nullable-payload and mixed nullable-read helpers; "
        "fresh/stale finite nullable read proofs; "
        "40 storage/read/write-proof refusals with forged key/nullable facts and reruns"
    )


if __name__ == "__main__":
    main()
