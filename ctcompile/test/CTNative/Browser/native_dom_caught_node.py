#!/usr/bin/env python3
"""Confined node catches preserve identity and reads without a native throw."""

import argparse
from pathlib import Path
from urllib.parse import unquote

from CTNative.Browser import native_dom as dom
from CTNative.Browser.native_dom_custom_iteration import INTRINSICS
from CTNative.harness import find_compilers, run

SOURCES = {
    "identity": "function customElements(anchor) { try { throw anchor; } catch (error) { return error === anchor; } }\n",
    "read": "function customElements(anchor) { try { throw anchor; } catch (error) { return error.hasAttribute('data-closed'); } }\n",
    "conditional-node-identity": "function customElements(anchor) { const flag = anchor.hasAttribute('data-closed'); try { if (flag) { throw anchor; } else { throw anchor; } } catch (error) { return error === anchor; } }\n",
    "conditional-node-read": "function customElements(anchor) { const flag = anchor.hasAttribute('data-closed'); try { if (flag) { throw anchor; } else { throw anchor; } } catch (error) { return error.hasAttribute('data-closed'); } }\n",
    "conditional-node-state": "function customElements(anchor) { const flag = anchor.hasAttribute('data-closed'); let saved = false; try { if (flag) { saved = true; throw anchor; } else { throw anchor; } } catch (error) { return error === anchor && saved; } }\n",
    "mixed-node-identity": "function customElements(anchor) { const flag = anchor.hasAttribute('data-closed'); try { if (flag) { throw anchor; } } catch (error) { return error === anchor; } return false; }\n",
    "mixed-node-read": "function customElements(anchor) { const flag = anchor.hasAttribute('data-closed'); try { if (flag) { throw anchor; } } catch (error) { return error.hasAttribute('data-closed'); } return false; }\n",
    "mixed-node-state": "function customElements(anchor) { const flag = anchor.hasAttribute('data-closed'); let saved = false; try { if (flag) { saved = true; throw anchor; } else { saved = true; } } catch (error) { return error === anchor && saved; } return !saved; }\n",
    "mixed-node-normal-return": "function customElements(anchor) { const flag = anchor.hasAttribute('data-closed'); try { if (flag) { throw anchor; } else { return false; } } catch (error) { return error === anchor; } }\n",
    "mixed-node-reversed-state": "function customElements(anchor) { const flag = anchor.hasAttribute('data-closed'); let saved = false; try { if (flag) { saved = true; } else { saved = true; throw anchor; } } catch (error) { return error === anchor && saved; } return !saved; }\n",
    "conditional": "function customElements(anchor) { try { if (anchor) throw anchor; } catch (error) { return error === anchor; } }\n",
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
            if name in ("identity", "conditional-node-identity", "conditional")
            else 'result0="false1"\nresult1="true1"\n'
        )
        if name.startswith("conditional-"):
            expected = {
                "conditional-node-identity": 'result0="true1"\nresult1="true1"\n',
                "conditional-node-read": 'result0="false2"\nresult1="true2"\n',
                "conditional-node-state": 'result0="false1"\nresult1="true1"\n',
            }[name]
        if name == "mixed-node-read":
            expected = 'result0="false1"\nresult1="true2"\n'
        if name == "mixed-node-reversed-state":
            expected = 'result0="true1"\nresult1="false1"\n'
        for command in ([args.node, str(node)], [args.reference, str(oracle)]):
            result = run(command)
            if result.stdout != expected:
                raise RuntimeError(f"{name}: catch oracle differs: {result}")
        ir, contract = dom.prepare(args, name, source, 1, entry_name="customElements")
        checks = (
            "assert(@ENTRY@(element)); assert(@ENTRY@(alias)); assert(@ENTRY@(foreign));"
            if name in ("identity", "conditional-node-identity", "conditional")
            else "assert(!@ENTRY@(element)); "
            'assert(doc.set_attribute(button, atoms.intern("data-closed"), "yes")); '
            "assert(@ENTRY@(alias)); assert(!@ENTRY@(foreign));"
        )
        if name == "conditional-node-identity":
            checks += ' assert(doc.set_attribute(button, atoms.intern("data-closed"), "yes")); assert(@ENTRY@(alias));'
        if name == "mixed-node-reversed-state":
            checks = (
                "assert(@ENTRY@(element)); "
                + 'assert(doc.set_attribute(button, atoms.intern("data-closed"), "yes")); '
                + "assert(!@ENTRY@(alias)); assert(@ENTRY@(foreign));"
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
    }
    refusals.update(
        {
            "mixed-protected-normal-read": "const flag = anchor.hasAttribute('data-closed'); try { if (flag) { throw anchor; } else { anchor.hasAttribute('x'); } } catch (error) { return error === anchor; } return false;",
            "mixed-protected-throw-call": "const flag = anchor.hasAttribute('data-closed'); try { if (flag) { decodeURIComponent('%'); throw anchor; } } catch (error) { return error === anchor; } return false;",
            "mixed-rethrow": "const flag = anchor.hasAttribute('data-closed'); try { if (flag) { throw anchor; } } catch (error) { throw error; } return false;",
            "mixed-return-borrow": "const flag = anchor.hasAttribute('data-closed'); try { if (flag) { throw anchor; } } catch (error) { return error; } return false;",
            "mixed-node-nested-state": "const flag = anchor.hasAttribute('data-closed'); const inner = anchor.hasAttribute('data-inner'); let saved = false; try { if (flag) { saved = true; if (inner) { throw anchor; } } else { saved = true; } } catch (error) { return error === anchor && saved; } return !saved;",
        }
    )
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
    check_outer_catches(args)
    print("confined node catch: 176 native executions, 44 refusals, 26 Node/VM observations")


OUTER_SOURCES = {
    "caught-node-getter-identity": "function customElements(anchor) {\n  const values = {\n    [Symbol.iterator]() { return this; },\n    next() {\n      const done = anchor.hasAttribute('data-yielded');\n      anchor.setAttribute('data-next', done);\n      anchor.setAttribute('data-yielded', 'yes');\n      return {done: done, value: anchor};\n    },\n    get return() {\n      anchor.setAttribute('data-closed', 'yes');\n      throw anchor;\n    }\n  };\n  try {\n  for (const node of values) {\n    return 1;\n    if (anchor.hasAttribute('stop')) break;\n  }\n  } catch (error) { return error === anchor; }\n  return anchor.hasAttribute('data-visited');\n}\n",
    "caught-node-method-identity": "function customElements(anchor) {\n  const values = {\n    [Symbol.iterator]() { return this; },\n    next() {\n      const done = anchor.hasAttribute('data-yielded');\n      anchor.setAttribute('data-next', done);\n      anchor.setAttribute('data-yielded', 'yes');\n      return {done: done, value: anchor};\n    },\n    return() {\n      anchor.setAttribute('data-closed', 'yes');\n      throw anchor;\n    }\n  };\n  try {\n  for (const node of values) {\n    return 1;\n    if (anchor.hasAttribute('stop')) break;\n  }\n  } catch (error) { return error === anchor; }\n  return anchor.hasAttribute('data-visited');\n}\n",
}


def check_outer_catches(args):
    for name, source in OUTER_SOURCES.items():
        script = source + """
function observe(exhausted) {
  const saved = {};
  if (exhausted) saved['data-yielded'] = 'yes';
  let trace = '';
  const anchor = {
    hasAttribute(name) {
      const value = name in saved;
      trace += 'read:' + name + '=' + value + ';';
      return value;
    },
    setAttribute(name, value) {
      saved[name] = '' + value;
      trace += 'write:' + name + '=' + saved[name] + ';';
    }
  };
  try { return 'return:' + customElements(anchor) + ':' + trace; }
  catch (error) { return 'throw:' + (error === anchor) + ':' + trace; }
}
var outcome0 = observe(false), outcome1 = observe(true);
"""
        oracle = args.work / f"{name}.oracle.js"
        oracle.write_text(script)
        node = args.work / f"{name}.node.js"
        node.write_text(
            script
            + "console.log('outcome0=' + JSON.stringify(outcome0));\n"
            + "console.log('outcome1=' + JSON.stringify(outcome1));\n"
        )
        expected = (
            'outcome0="return:true:read:data-yielded=false;write:data-next=false;'
            'write:data-yielded=yes;write:data-closed=yes;"\n'
            'outcome1="return:false:read:data-yielded=true;write:data-next=true;'
            'write:data-yielded=yes;read:data-visited=false;"\n'
        )
        for command in ([args.node, str(node)], [args.reference, str(oracle)]):
            result = run(command)
            output = unquote(result.stdout) if command[0] == args.reference else result.stdout
            if output != expected:
                raise RuntimeError(f"{name}: outer catch oracle differs: {result}")
        ir, contract = dom.prepare(args, name, source, 1, entry_name="customElements")
        for provider in ("ctbrowser-dom-v1", "ctbrowser-dom-session-v1"):
            for optimize in (False, True):
                result = dom.lower(
                    args,
                    ir,
                    dict(contract, provider=provider, initial_intrinsics=INTRINSICS),
                    f"{name}-{provider}-{optimize}",
                    optimize=optimize,
                    success=False,
                )
                if (
                    "DOM iterator observing catch requires call/check payload and state proof"
                    not in result
                ):
                    raise RuntimeError(f"{name}: lost observing catch diagnostic: {result}")


if __name__ == "__main__":
    main()
