"""Check objects with String fields read directly, through parameters and forwarded calls.

Each fixture in primitive-fields.mlir must print its expected `a=` value under
Node and the reference interpreter. The fixtures in the NATIVE tuple must also
lower to native code and print the same value; every other fixture must be
refused by native lowering under both optimization settings.
"""

from CTNative.harness import native_args, run
from CTNative.Lowering.Objects.constructor_refusals import NODE, check_native


def main():
    args = native_args(__doc__, "fixtures")
    checked = refused = 0
    # Fixture name -> the value of `a` that Node and the reference interpreter
    # must both print.
    for name, expected in {
        "strings": 375,
        "borrowed": 5,
        "mixed": 15,
        "absent": 12,
        "equality": 111,
        "borrowed-equality": 5,
        "borrowed-saved": 137,
        "borrowed-multiple": 151,
        "borrowed-method": 5,
        "method-saved": 137,
        "method-multiple": 15,
        "method-missing": 12,
        "method-mixed": 14,
        "method-identity": 1,
        "borrowed-parameters": 1441,
        "borrowed-before": 12,
        "borrowed-missing": 21,
        "borrowed-conditional": 12,
        "borrowed-mixed": 14,
        "borrowed-undefined": 1,
        "borrowed-delete": 1,
        "borrowed-escape": 5,
        "borrowed-forwarded": 5,
        "forwarded-chain": 4,
        "forwarded-saved": 137,
        "forwarded-multiple": 151,
        "forwarded-parameters": 4114,
        "forwarded-before": 12,
        "forwarded-missing": 21,
        "forwarded-conditional": 12,
        "forwarded-mixed": 14,
        "forwarded-undefined": 1,
        "forwarded-alias-delete": 1,
        "forwarded-escape": 5,
        "forwarded-recursive": 5,
        "forwarded-depth-limit": 5,
        "forwarded-mixed-actual": 21,
        "forwarded-short-call": 51,
        "forwarded-callable-escape": 5,
        "borrowed-alias-delete": 1,
        "declared-forwarded": 151,
        "declared-saved": 137,
        "declared-mixed-actual": 21,
        "declared-short-call": 51,
        "declared-callable-escape": 5,
        "declared-replaced": 9,
        "declared-alias-delete": 1,
        "borrowed-delete-kept": 1,
        "borrowed-alias-delete-kept": 1,
        "declared-alias-delete-kept": 1,
        "forwarded-alias-delete-kept": 1,
        "utf8-length": 4,
        "empty-length": 0,
        "unknown-property": 0,
    }.items():
        source = args.fixtures / f"{name}.js"
        if name == "forwarded-depth-limit":
            # A chain of 64 forwarding functions above the reader, deeper than
            # native lowering follows, so it must be refused.
            source = args.work / f"{name}.js"
            source.write_text(
                "function forwarded() {\n"
                "var f0 = function(value) { return value.text.length; };\n"
                + "".join(
                    f"var f{i} = function(value) {{ return f{i - 1}(value); }};\n"
                    for i in range(1, 65)
                )
                + 'return f64({text: "owned"});\n}\nvar a = forwarded();\n'
            )
        assert run([args.node, "-e", NODE, str(source)]).stdout == f"a={expected}\n", name
        assert run([args.reference, str(source)]).stdout == f"a={expected}\n", name
        # NATIVE: the fixtures that must lower and run natively.
        if name in (
            "strings",
            "equality",
            "utf8-length",
            "empty-length",
            "borrowed",
            "borrowed-equality",
            "borrowed-saved",
            "borrowed-multiple",
            "borrowed-method",
            "method-saved",
            "method-multiple",
            "borrowed-parameters",
            "borrowed-forwarded",
            "forwarded-chain",
            "forwarded-saved",
            "forwarded-multiple",
            "forwarded-parameters",
            "declared-forwarded",
            "declared-saved",
            # A delete makes the literal a table (Inference/Dictionaries.cpp),
            # shared by reference, so a delete through an alias is seen
            # through the other name. The `-kept` copies also keep the literal
            # in an array, which no table rule takes, and stay refused.
            "borrowed-delete",
            "borrowed-alias-delete",
            "declared-alias-delete",
            "forwarded-alias-delete",
        ):
            checked += check_native(args, source, name, expected)
            continue
        raw = args.work / f"{name}.mlir"
        run([args.translate, "--ctbrowser-js-to-ctjs", str(source), "-o", str(raw)])
        assert "ctjs.skipped" not in raw.read_text(), name
        for optimize in (False, True):
            text = run(
                [
                    args.opt,
                    str(raw),
                    "--ctjs-resolve-globals",
                    "--ctjs-lift-to-scf",
                    f"--ctnative-lower-to-emitc=optimize={str(optimize).lower()}",
                ]
            ).stdout
            assert "ctnative.not_native" in text and "emitc.func @main" not in text, name
            if name in ("mixed", "absent"):
                assert "string field requires definite string reads and writes" in text, name
            refused += 1
    print(f"local string fields: {checked} native executions, {refused} refusals")


if __name__ == "__main__":
    main()
