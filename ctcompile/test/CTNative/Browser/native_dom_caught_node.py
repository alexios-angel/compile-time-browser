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
    "mixed-node-nested-state": "function customElements(anchor) { const flag = anchor.hasAttribute('data-closed'); const inner = anchor.hasAttribute('data-inner'); let saved = false; try { if (flag) { saved = true; if (inner) { throw anchor; } } else { saved = true; } } catch (error) { return error === anchor && saved; } return !saved; }\n",
    "mixed-node-nested-identity": "function customElements(anchor) { const flag = anchor.hasAttribute('data-closed'); const inner = anchor.hasAttribute('data-inner'); try { if (flag) { if (inner) { throw anchor; } } } catch (error) { return error === anchor; } return false; }\n",
    "mixed-node-nested-read": "function customElements(anchor) { const flag = anchor.hasAttribute('data-closed'); const inner = anchor.hasAttribute('data-inner'); try { if (flag) { if (inner) { throw anchor; } } } catch (error) { return error.hasAttribute('data-closed'); } return false; }\n",
    "protected-read": "function customElements(anchor) { try { anchor.hasAttribute('x'); throw anchor; } catch (error) { return error === anchor; } }\n",
    "mixed-protected-normal-read": "function customElements(anchor) { const flag = anchor.hasAttribute('data-closed'); try { if (flag) { throw anchor; } else { anchor.hasAttribute('x'); } } catch (error) { return error === anchor; } return false; }\n",
    "nested-protected-normal-read": "function customElements(anchor) {  const flag = anchor.hasAttribute('data-closed'); const inner = anchor.hasAttribute('data-inner'); let saved = false; try { if (flag) { saved = true; if (inner) { throw anchor; } } else { anchor.hasAttribute('x'); saved = true; } } catch (error) { return error === anchor && saved; } return !saved;  }\n",
    "protected-conditional-node-state": "function customElements(anchor) { let saved = false; try { if (anchor.hasAttribute('data-closed')) { saved = true; throw anchor; } else { throw anchor; } } catch (error) { return error === anchor && saved; } }\n",
    "invocation-return": "function customElements(anchor) { try { return anchor.hasAttribute('data-closed'); } catch (error) { return error === anchor; } }\n",
    "invocation-saved-state": "function customElements(anchor) { let saved = false; try { saved = anchor.hasAttribute('data-closed'); return saved; } catch (error) { return error === anchor && saved; } }\n",
    "invocation-conditional-return": "function customElements(anchor) { const flag = anchor.hasAttribute('data-closed'); let saved = false; try { if (flag) { saved = anchor.hasAttribute('data-closed'); } return saved; } catch (error) { return error === anchor && saved; } }\n",
    "invocation-multiple-reads": "function customElements(anchor) { try { const first = anchor.hasAttribute('data-closed'); const second = anchor.hasAttribute('x'); return first && !second; } catch (error) { return error === anchor; } }\n",
    "invocation-read-then-throw": "function customElements(anchor) { let saved = false; try { saved = anchor.hasAttribute('data-closed'); throw anchor; } catch (error) { return error === anchor && saved; } }\n",
    "write-throw": "function customElements(anchor) { try { anchor.setAttribute('data-written', 'yes'); throw anchor; } catch (error) { return error === anchor && anchor.hasAttribute('data-written'); } }\n",
    "write-return": "function customElements(anchor) { try { anchor.setAttribute('data-written', 'yes'); return true; } catch (error) { return false; } }\n",
    "write-conditional": "function customElements(anchor) { const flag = anchor.hasAttribute('data-closed'); try { if (flag) { anchor.setAttribute('data-written', 'yes'); } throw anchor; } catch (error) { return error === anchor && anchor.hasAttribute('data-written'); } }\n",
    "write-saved-state": "function customElements(anchor) { let saved = false; try { saved = anchor.hasAttribute('data-closed'); anchor.setAttribute('data-written', 'yes'); throw anchor; } catch (error) { return error === anchor && saved && anchor.hasAttribute('data-written'); } }\n",
    "write-boolean-constants": "function customElements(anchor) { try { anchor.setAttribute('data-first', true); anchor.setAttribute('data-written', false); throw anchor; } catch (error) { return error === anchor; } }\n",
    "write-boolean-read-return": "function customElements(anchor) { try { const saved = anchor.hasAttribute('data-closed'); anchor.setAttribute('data-written', saved); return saved; } catch (error) { return false; } }\n",
    "write-boolean-snapshot": "function customElements(anchor) { let saved = false; try { saved = anchor.hasAttribute('data-closed'); anchor.setAttribute('data-closed', true); anchor.setAttribute('data-written', saved); throw anchor; } catch (error) { return error === anchor && saved; } }\n",
    "write-boolean-conditional": "function customElements(anchor) { let saved = false; try { if (anchor.hasAttribute('data-closed')) { saved = anchor.hasAttribute('data-closed'); } anchor.setAttribute('data-written', saved); throw anchor; } catch (error) { return error === anchor && saved; } }\n",
}


WRITE_EXPECTED = {
    "write-throw": 'result0="true1:yes:1"\nresult1="true1:yes:1"\n',
    "write-return": 'result0="true0:yes:1"\nresult1="true0:yes:1"\n',
    "write-conditional": 'result0="false2:undefined:0"\nresult1="true2:yes:1"\n',
    "write-saved-state": 'result0="false1:yes:1"\nresult1="true2:yes:1"\n',
    "write-boolean-constants": 'result0="true0:false:2"\nresult1="true0:false:2"\n',
    "write-boolean-read-return": 'result0="false1:false:1"\nresult1="true1:true:1"\n',
    "write-boolean-snapshot": 'result0="false1:false:2"\nresult1="true1:true:2"\n',
    "write-boolean-conditional": 'result0="false1:false:1"\nresult1="true2:true:1"\n',
}

WRITE_OBSERVER = """function observe(closed) {
  const saved = {};
  if (closed) saved['data-closed'] = 'yes';
  let reads = 0, writes = 0;
  const anchor = {
    hasAttribute(name) { reads++; return name in saved; },
    setAttribute(name, value) { writes++; saved[name] = '' + value; }
  };
  const result = customElements(anchor);
  return '' + result + reads + ':' + saved['data-written'] + ':' + writes;
}
var result0 = observe(false), result1 = observe(true);
"""


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
        if name.startswith("write-"):
            script = source + WRITE_OBSERVER
        nested = name.startswith("mixed-node-nested-") or name == "nested-protected-normal-read"
        if nested:
            script = (
                script.replace("observe(closed)", "observe(closed, inner)")
                .replace(
                    "key === 'data-closed' && closed",
                    "(key === 'data-closed' && closed) || (key === 'data-inner' && inner)",
                )
                .replace(
                    "var result0 = observe(false), result1 = observe(true);",
                    "var result0 = observe(false, false), result1 = observe(false, true), "
                    "result2 = observe(true, false), result3 = observe(true, true);",
                )
            )
        oracle = args.work / f"{name}.oracle.js"
        oracle.write_text(script)
        node = args.work / f"{name}.node.js"
        node.write_text(
            script
            + "console.log('result0=' + JSON.stringify(result0));\n"
            + "console.log('result1=' + JSON.stringify(result1));\n"
            + (
                "console.log('result2=' + JSON.stringify(result2));\n"
                "console.log('result3=' + JSON.stringify(result3));\n"
                if nested
                else ""
            )
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
        if name == "invocation-multiple-reads":
            expected = 'result0="false2"\nresult1="true2"\n'
        if name == "protected-read":
            expected = 'result0="true1"\nresult1="true1"\n'
        if name == "mixed-protected-normal-read":
            expected = 'result0="false2"\nresult1="true1"\n'
        if name in ("mixed-node-read", "invocation-conditional-return"):
            expected = 'result0="false1"\nresult1="true2"\n'
        if name == "mixed-node-reversed-state":
            expected = 'result0="true1"\nresult1="false1"\n'
        if nested:
            expected = 'result0="false2"\nresult1="false2"\nresult2="false2"\n' + (
                'result3="true3"\n' if name == "mixed-node-nested-read" else 'result3="true2"\n'
            )
            if name == "nested-protected-normal-read":
                expected = expected.replace('result0="false2"', 'result0="false3"').replace(
                    'result1="false2"', 'result1="false3"'
                )
        if name in WRITE_EXPECTED:
            expected = WRITE_EXPECTED[name]
        for command in ([args.node, str(node)], [args.reference, str(oracle)]):
            result = run(command)
            output = unquote(result.stdout) if command[0] == args.reference else result.stdout
            if output != expected:
                raise RuntimeError(f"{name}: catch oracle differs: {result}")
        ir, contract = dom.prepare(args, name, source, 1, entry_name="customElements")
        checks = (
            "assert(@ENTRY@(element)); assert(@ENTRY@(alias)); assert(@ENTRY@(foreign));"
            if name in ("identity", "conditional-node-identity", "conditional", "protected-read")
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
        if nested:
            checks = (
                "assert(!@ENTRY@(element)); "
                'assert(doc.set_attribute(button, atoms.intern("data-inner"), "yes")); '
                "assert(!@ENTRY@(alias)); "
                'assert(doc.remove_attribute(button, atoms.intern("data-inner"))); '
                'assert(doc.set_attribute(button, atoms.intern("data-closed"), "yes")); '
                "assert(!@ENTRY@(alias)); "
                'assert(doc.set_attribute(button, atoms.intern("data-inner"), "yes")); '
                "assert(@ENTRY@(alias)); assert(!@ENTRY@(foreign));"
            )
        if name.startswith("write-"):
            answer = "closed" if name in ("write-conditional", "write-saved-state") else "true"
            count = "(closed ? 1u : 0u)" if name == "write-conditional" else "1u"
            text = '"yes"'
            first_name = '"data-written"'
            extra = ""
            if name.startswith("write-boolean-"):
                answer = "true" if name == "write-boolean-constants" else "closed"
                count = (
                    "2u" if name in ("write-boolean-constants", "write-boolean-snapshot") else "1u"
                )
                text = (
                    '"false"'
                    if name == "write-boolean-constants"
                    else '(closed ? "true" : "false")'
                )
                if name == "write-boolean-constants":
                    first_name = '"data-first"'
                elif name == "write-boolean-snapshot":
                    first_name = '"data-closed"'
                if count == "2u":
                    extra = 'assert(writes[1].name == written); assert(doc.read().attribute_value(button, atoms.intern(@FIRST@)) == "true");'.replace(
                        "@FIRST@", first_name
                    )
            checks = (
                """
                const auto written = atoms.intern("data-written");
                for (bool closed : {false, true}) {
                    assert(doc.remove_attribute(button, written));
                    if (closed) {
                        assert(doc.set_attribute(button, atoms.intern("data-closed"), "yes"));
                    }
                    (void)doc.take_writes();
                    assert(static_cast<bool>(@ENTRY@(alias)) == @ANSWER@);
                    const auto writes = doc.take_writes();
                    assert(writes.size() == @COUNT@);
                    assert(doc.read().has_attribute(button, written) == !writes.empty());
                    if (!writes.empty()) {
                        assert(writes[0].name == atoms.intern(@FIRST@));
                        assert(doc.read().attribute_value(button, written) == @TEXT@);
                        @EXTRA@
                    }
                }
                (void)foreign;
            """.replace("@ANSWER@", answer)
                .replace("@COUNT@", count)
                .replace("@FIRST@", first_name)
                .replace("@TEXT@", text)
                .replace("@EXTRA@", extra)
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
        "write-boolean-unknown-value": "try { anchor.setAttribute('data-written', anchor.unknown); throw anchor; } catch (error) { return error === anchor; }",
        "write-boolean-forged-read": "try { const saved = ({hasAttribute() { return true; }}).hasAttribute('data-closed'); anchor.setAttribute('data-written', saved); throw anchor; } catch (error) { return error === anchor; }",
        "write-boolean-mixed-join": "let saved = anchor; try { if (anchor.hasAttribute('data-closed')) { saved = true; } anchor.setAttribute('data-written', saved); throw anchor; } catch (error) { return error === anchor; }",
        "write-boolean-invalid-name": "try { anchor.setAttribute('bad name', true); throw anchor; } catch (error) { return error === anchor; }",
        "write-invalid-name": "try { anchor.setAttribute('bad name', 'yes'); throw anchor; } catch (error) { return error === anchor; }",
        "write-coercing-value": "try { anchor.setAttribute('data-written', anchor); throw anchor; } catch (error) { return error === anchor; }",
        "write-forged-method": "try { ({setAttribute() { throw false; }}).setAttribute('data-written', 'yes'); throw anchor; } catch (error) { return error === anchor; }",
        "write-wrong-receiver": "try { const write = anchor.setAttribute; write('data-written', 'yes'); throw anchor; } catch (error) { return error === anchor; }",
        "invocation-coercing-argument": "try { return anchor.hasAttribute(1); } catch (error) { return error === anchor; }",
        "invocation-wrong-receiver": "try { const read = anchor.hasAttribute; return read('data-closed'); } catch (error) { return error === anchor; }",
        "invocation-forged-method": "try { return ({hasAttribute() { throw false; }}).hasAttribute('data-closed'); } catch (error) { return error === anchor; }",
        "invocation-unknown-call": "try { return decodeURIComponent('%'); } catch (error) { return error === anchor; }",
        "invocation-without-contract": "try { return anchor.hasAttribute('data-closed'); } catch (error) { return error === anchor; }",
        "protected-forged-method": "try { ({hasAttribute() { throw false; }}).hasAttribute('x'); throw anchor; } catch (error) { return error === anchor; }",
        "rethrow": "try { throw anchor; } catch (error) { throw error; }",
        "return-borrow": "try { throw anchor; } catch (error) { return error; }",
        "protected-call": "try { decodeURIComponent('%'); throw anchor; } catch (error) { return error === anchor; }",
    }
    refusals.update(
        {
            "mixed-protected-throw-call": "const flag = anchor.hasAttribute('data-closed'); try { if (flag) { decodeURIComponent('%'); throw anchor; } } catch (error) { return error === anchor; } return false;",
            "mixed-rethrow": "const flag = anchor.hasAttribute('data-closed'); try { if (flag) { throw anchor; } } catch (error) { throw error; } return false;",
            "mixed-return-borrow": "const flag = anchor.hasAttribute('data-closed'); try { if (flag) { throw anchor; } } catch (error) { return error; } return false;",
        }
    )
    nested_body = SOURCES["mixed-node-nested-state"].split("{", 1)[1].rsplit("}", 1)[0]
    refusals["nested-protected-throw-call"] = nested_body.replace(
        "throw anchor;", "decodeURIComponent('%'); throw anchor;"
    )
    for name, body in refusals.items():
        ir, contract = dom.prepare(
            args,
            name,
            f"function customElements(anchor) {{ {body} }}\n",
            1,
            entry_name="customElements",
        )
        for provider in ("ctbrowser-dom-v1", "ctbrowser-dom-session-v1"):
            for optimize in (False, True):
                manifest = dict(
                    contract, provider=provider, initial_intrinsics=["decodeURIComponent"]
                )
                if name == "invocation-without-contract":
                    manifest["element_parameters"] = []
                dom.lower(
                    args,
                    ir,
                    manifest,
                    f"{name}-{provider}-{optimize}",
                    optimize=optimize,
                    success=False,
                )
    check_outer_catches(args)
    print("confined node catch: 496 native executions, 92 refusals, 74 Node/VM observations")


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
