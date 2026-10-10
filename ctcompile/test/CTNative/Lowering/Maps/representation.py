#!/usr/bin/env python3
"""Check associative lookup, ordered fallback, aliases, and owning Map values."""

import re

from CTNative.harness import build_and_run, native_args, native_pipeline, native_tools, run
from CTNative.Exports import boundary

NODE_GLOBALS = r"""const fs = require('node:fs');
const vm = require('node:vm');
const context = vm.createContext({});
vm.runInContext(fs.readFileSync(process.argv[1], 'utf8'), context);
for (const name of Object.keys(context).sort()) {
    if (typeof context[name] === 'number') console.log(name + '=' + context[name]);
}
"""


def check_zero_keys(cpp):
    renamed, count = re.subn(r"\bmain\(\)", "ctnative_test_entry()", cpp)
    assert count == 1
    return (
        renamed
        + r"""
template <class K> bool check_zero_keys(K negative) {
    const K positive{0.0};
    auto map = ctnative::make_number_map<K>();
    ctnative::map_set(map, negative, js_num{-0.0});
    for (int step = 0; step < 3; ++step) {
        const auto found = map->find(positive);
        if (found == map->end() || map->size() != 1 || !std::signbit(found->second)) {
            return false;
        }
        if constexpr (std::is_same_v<K, js_num>) {
            if (std::signbit(found->first)) { return false; }
        } else {
            if (std::signbit(std::get<js_num>(found->first))) { return false; }
        }
        if (step == 0) { ctnative::map_set(map, positive, js_num{-0.0}); }
        if (step == 1) {
            if (!ctnative::map_delete(map, positive)) { return false; }
            ctnative::map_set(map, negative, js_num{-0.0});
        }
    }
    return true;
}
int main() {
    if (ctnative_test_entry() != 0) { return 90; }
    if (!check_zero_keys(js_num{-0.0})) { return 91; }
    if (!check_zero_keys(std::variant<ctnative::js_boolean_t, js_num>{-0.0})) { return 92; }
    return 0;
}
"""
    )


def main():
    args = native_args(__doc__, "fixtures")
    node, reference = args.node, args.reference
    compilers, nm = native_tools(reference)
    cases = [
        (
            "associative",
            False,
            "identityResult=42\nlifetimeResult=42\nmutationResult=1042\nsameValueZeroResult=5829\n",
        ),
        (
            "ordered",
            False,
            "orderResult=30102188\nprojectionResult=30\nstringOrderResult=37\nzeroResult=0\n",
        ),
        (
            "ordered",
            True,
            "orderResult=30102188\nprojectionResult=30\nstringOrderResult=37\nzeroResult=0\n",
        ),
        ("payloads", False, "booleanResult=63\nnestedResult=1\nstringResult=63\n"),
        ("string-values", False, "keyResult=3\nsnapshotResult=3427\n"),
        ("string-values", True, "keyResult=3\nsnapshotResult=3427\n"),
        ("mixed-values", False, "result=2\n"),
    ]
    for fixture, deforest, expected in cases:
        name = fixture + ("-deforested" if deforest else "")
        js = args.fixtures / (fixture + ".js")
        assert run([node, "-e", NODE_GLOBALS, str(js)]).stdout == expected
        assert run([str(reference), str(js)]).stdout == expected
        module = args.work / (name + ".mlir")
        native_pipeline(
            args, args.fixtures / (fixture + ".js"), module, DEFOREST="ON" if deforest else "OFF"
        )
        if deforest:
            projection = "true" if fixture == "string-values" else "false"
            assert f"ctnative::map_snapshot_at<{projection}>" in module.read_text()
        elif fixture == "ordered":
            # Common helper names do not license ordered projections if the
            # matching storage contract has been removed or replaced.
            forged = args.work / "missing-order-contract.mlir"
            original = module.read_text()
            # The ordered contract is the define in front of the runtime include.
            altered = (
                "\n".join(
                    line
                    for line in original.splitlines()
                    if "#define CTNATIVE_ORDERED_MAPS 1" not in line
                )
                + "\n"
            )
            assert altered != original
            forged.write_text(altered)
            refused = run([args.opt, "--ctnative-deforest", str(forged)]).stdout
            assert (
                'ctnative.deforest_reason = "native Map runtime contract is not present"' in refused
            )
            assert "ctnative::map_snapshot_at<" not in refused
        deduced = args.work / (name + "-deduced.mlir")
        run(
            [
                args.opt,
                "--ctnative-print-deduced",
                "--mlir-print-debuginfo",
                str(module),
                "-o",
                str(deduced),
            ]
        )
        for label, ir in [("plain", module), ("deduced", deduced)]:
            cpp = run([args.translate, "--mlir-to-cpp", str(ir)]).stdout
            # The helpers are the runtime header's now; the program decides only
            # the storage, by the define in front of its include.
            assert '#include "ctcompile/CTNative/Runtime/ctnative.hpp"' in cpp
            if fixture in {"associative", "ordered"}:
                assert "ctnative::make_string_to_number_map()" in cpp
            assert "ctbrowser::script::" not in cpp
            if fixture in {"associative", "payloads", "mixed-values"}:
                assert "#define CTNATIVE_ORDERED_MAPS" not in cpp
                if fixture == "associative":
                    assert "ctnative::string_to_number_map" in cpp
            else:
                assert "#define CTNATIVE_ORDERED_MAPS 1" in cpp
                assert "std::map<" not in cpp
            if fixture == "payloads":
                assert (
                    "ctnative::make_map<double, ctnative::js_boolean_t>()" in cpp
                    or "ctnative::make_map<js_num, ctnative::js_boolean_t>()" in cpp
                )
                assert "ctnative::make_map<std::string, std::string>()" in cpp
                assert "ctnative::make_map<ctnative::js_boolean_t, std::string>()" in cpp
                # The String payload read is the runtime header's; the program
                # spells its owning carrier, not the helper's definition.
                assert "ctnative::nullable_string" in cpp
            if fixture == "mixed-values":
                assert (
                    "ctnative::make_map<double, std::variant<ctnative::js_boolean_t, std::string>>()"
                    in cpp
                )
                assert len(re.findall(r"\bctnative::map_set\(", cpp)) == 2
                assert "ctnative::map_size(" in cpp
            if fixture == "string-values":
                assert "std::vector<std::string>" in cpp
                # String values stay owning snapshots even while a separate
                # numeric key projection is safely deforested.
                assert re.search(r"= ctnative::map_values\(", cpp)
            source = args.work / f"{name}-{label}.cpp"
            source.write_text(
                check_zero_keys(cpp) if fixture in {"associative", "ordered"} else cpp
            )
            for index, compiler in enumerate(compilers):
                binary = (args.work / f"{name}-{label}-{index}").resolve()
                actual = build_and_run(compiler, source, binary, nm=nm).stdout
                assert actual == expected, (name, label, actual, expected)
            if label == "plain":
                binary = source.with_suffix(".sanitized").resolve()
                assert build_and_run(compilers[1], source, binary, sanitize=True).stdout == expected
    for fixture in ["boolean-values"]:
        source = (args.fixtures / (fixture + ".js")).read_text()
        _, prepared, count = boundary.prepare(args, fixture, source)
        output, _ = boundary.native(args, prepared, fixture, count)
        before, after = prepared.read_text(), output.read_text()

        def calls(text):
            return [
                call.strip()
                for call in re.findall(
                    r"^\s*(?:%[-\w.$]+ = )?ctjs\.(call(?:_direct)? [^\n{]+)", text, re.M
                )
            ]

        assert calls(before) == calls(after), fixture
    print(
        "native Maps: 14 observations, associative/ordered/deforested, GCC/Clang, "
        "plain/deduced, ASan/UBSan; Bool/string payloads and owning string snapshots "
        "and literal mixed storage agree with Node/interpreter; boolean snapshot refusal"
    )


if __name__ == "__main__":
    main()
