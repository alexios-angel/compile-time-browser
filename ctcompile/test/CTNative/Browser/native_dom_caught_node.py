#!/usr/bin/env python3
"""Confined node catches preserve identity and reads without a native throw."""

import argparse
from pathlib import Path

from CTNative.Browser import native_dom as dom
from CTNative.harness import find_compilers, run

SOURCES = {
    "identity": "function customElements(anchor) { try { throw anchor; } catch (error) { return error === anchor; } }\n",
    "read": "function customElements(anchor) { try { throw anchor; } catch (error) { return error.hasAttribute('data-closed'); } }\n",
}


def main():
    parser = argparse.ArgumentParser()
    for name in ("translate", "opt", "clang", "nm", "node", "reference"):
        parser.add_argument("--" + name, required=True)
    for name in ("build", "include", "work"):
        parser.add_argument("--" + name, required=True, type=Path)
    args = parser.parse_args()
    args.work = args.work.resolve()
    args.work.mkdir(parents=True, exist_ok=True)
    compilers = find_compilers()
    compilers[1] = args.clang
    includes, libraries = dom.link_options(args)
    for name, source in SOURCES.items():
        script = (
            source
            + "function observe(closed) { let reads = 0; "
            + "const anchor = { hasAttribute(key) { reads++; return key === 'data-closed' && closed; } }; "
            + "return '' + customElements(anchor) + reads; }\n"
            + "var result0 = observe(false), result1 = observe(true);\n"
        )
        oracle = args.work / f"{name}.oracle.js"
        oracle.write_text(script)
        node = args.work / f"{name}.node.js"
        node.write_text(
            script
            + "console.log('result0=' + JSON.stringify(result0));\n"
            + "console.log('result1=' + JSON.stringify(result1));\n"
        )
        expected = (
            'result0="true0"\nresult1="true0"\n'
            if name == "identity"
            else 'result0="false1"\nresult1="true1"\n'
        )
        for command in ([args.node, str(node)], [args.reference, str(oracle)]):
            result = run(command)
            if result.stdout != expected:
                raise RuntimeError(f"{name}: catch oracle differs: {result}")
        ir, contract = dom.prepare(args, name, source, 1, entry_name="customElements")
        checks = (
            "assert(@ENTRY@(element)); assert(@ENTRY@(alias)); assert(@ENTRY@(foreign));"
            if name == "identity"
            else "assert(!@ENTRY@(element)); "
            'assert(doc.set_attribute(button, atoms.intern("data-closed"), "yes")); '
            "assert(@ENTRY@(alias)); assert(!@ENTRY@(foreign));"
        )
        checks += " (void)pressed; (void)alias;"
        for provider in ("ctbrowser-dom-v1", "ctbrowser-dom-session-v1"):
            for optimize in (False, True):
                label = f"caught-node-{name}-{provider}-{optimize}"
                native = dom.lower(
                    args, ir, dict(contract, provider=provider), label, optimize=optimize
                )
                cpp = run([args.translate, "--mlir-to-cpp", str(native)]).stdout
                # The existing session wrapper rejects foreign documents with
                # std::invalid_argument; the source catch emits no C++ exception.
                source_cpp = cpp.replace(
                    'throw std::invalid_argument("DOM element belongs to another session");', ""
                )
                if "throw " in source_cpp or "catch (" in source_cpp:
                    raise RuntimeError("confined node escaped into a native exception")
                dom.standalone(args, native, label, checks, compilers, includes, libraries)
    refusals = {
        "rethrow": "try { throw anchor; } catch (error) { throw error; }",
        "return-borrow": "try { throw anchor; } catch (error) { return error; }",
        "protected-read": "try { anchor.hasAttribute('x'); throw anchor; } catch (error) { return error === anchor; }",
        "protected-call": "try { decodeURIComponent('%'); throw anchor; } catch (error) { return error === anchor; }",
        "conditional": "try { if (anchor) throw anchor; } catch (error) { return error === anchor; }",
    }
    for name, body in refusals.items():
        ir, contract = dom.prepare(args, name, f"function customElements(anchor) {{ {body} }}\n", 1)
        for provider in ("ctbrowser-dom-v1", "ctbrowser-dom-session-v1"):
            for optimize in (False, True):
                manifest = dict(
                    contract, provider=provider, initial_intrinsics=["decodeURIComponent"]
                )
                dom.lower(
                    args,
                    ir,
                    manifest,
                    f"{name}-{provider}-{optimize}",
                    optimize=optimize,
                    success=False,
                )
    print("confined node catch: 32 native executions, 20 refusals, 4 Node/VM observations")


if __name__ == "__main__":
    main()
