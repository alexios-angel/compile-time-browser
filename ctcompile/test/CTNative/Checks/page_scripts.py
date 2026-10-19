"""Check appended classic-script boundaries through the real page reference."""

import argparse
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference", required=True)
    parser.add_argument("--work", required=True, type=Path)
    args = parser.parse_args()
    work = args.work
    before = "before 17 undefined\n"
    after = "after 17 19 36 1 1 kept 1\nsloppy 23\n"
    cases = [
        (
            "appended",
            "listener.html",
            ["a.js", "b.js"],
            before + "onerror 1 true\nerror 1 true\n" + after,
            [("a.js", "first-script")],
        ),
        (
            "inline",
            "inline-error.html",
            ["b.js"],
            before + "onerror 1 false\nerror 1 false\ninline-next 17\n" + after,
            [("inline-error.html", "inline-script")],
        ),
        (
            "syntax",
            "listener.html",
            ["bad.js", "good.js", "b.js"],
            "onerror 1 false\nerror 1 false\n" + before + after,
            [("bad.js", "parse error: expression - at 1:14")],
        ),
        (
            "multiple-errors",
            "listener.html",
            ["bad.js", "a.js", "b.js"],
            "onerror 1 false\nerror 1 false\n"
            + before
            + "onerror 2 true\nerror 2 true\n"
            + after.replace("36 1 1 kept 1", "36 2 2 kept 2"),
            [("bad.js", "parse error: expression - at 1:14"), ("a.js", "first-script")],
        ),
        (
            "success",
            "listener.html",
            ["good.js", "b.js"],
            before + after.replace("36 1 1 kept 1", "36 0 0 kept null"),
            [],
        ),
    ]
    # HTML reports the actual exception, including undefined, before restoring
    # currentScript. Each case is a fresh page/realm (HTML report an exception).
    payload_setup = (
        "var token = {}, seen = [], fifth = [], IntrinsicSyntaxError = SyntaxError;"
        "window.onerror = function(m,u,l,c,e) { fifth.push(e === token); };"
        "window.addEventListener('error', function(e) {"
        "seen.push(e.error === token ? 'token' : e.error instanceof IntrinsicSyntaxError"
        " ? 'syntax' : String(e.error)); });"
    )
    witnesses = [
        (
            "payload-inline",
            [payload_setup, "throw token;", "console.log(seen.join(','), fifth.join(','));"],
            [],
            "token true\n",
            [(1, "uncaught")],
        ),
        (
            "payload-appended",
            [payload_setup],
            ["throw token;", "console.log(seen.join(','), fifth.join(','));"],
            "token true\n",
            [("append0", "uncaught")],
        ),
        (
            "payload-syntax",
            [
                payload_setup,
                "throw token;",
                "var broken = ;",
                "console.log(seen.join(','), fifth.join(','));",
            ],
            [],
            "token,syntax true,false\n",
            [(1, "uncaught")],
        ),
        (
            "payload-appended-syntax",
            [payload_setup],
            ["throw token;", "var broken = ;", "console.log(seen.join(','), fifth.join(','));"],
            "token,syntax true,false\n",
            [("append0", "uncaught"), ("append1", "parse error")],
        ),
        (
            "payload-primitives",
            [
                payload_setup,
                "throw undefined;",
                "throw 37;",
                "console.log(seen.join(','), fifth.join(','));",
            ],
            [],
            "undefined,37 false,false\n",
            [(1, "uncaught")],
        ),
        (
            "payload-intrinsic",
            [
                payload_setup + "SyntaxError = function() { throw 99; };",
                "var broken = ;",
                "console.log(seen.join(','));",
            ],
            [],
            "syntax\n",
            [(1, "parse error")],
        ),
        (
            "payload-listener-control",
            [
                payload_setup,
                "var t = new EventTarget(); t.addEventListener('go', function() { throw token; });"
                "t.dispatchEvent(new Event('go')); console.log(seen.join(','), fifth.join(','));",
            ],
            [],
            "token true\n",
            [],
        ),
        (
            "payload-nested",
            [
                payload_setup
                + "var at = ''; window.onerror = function() { at = document.currentScript.id; };"
                "document.write('<script id=inner>throw token;<' + '/script>');"
                "console.log(seen.join(','), at, document.currentScript.id);"
            ],
            [],
            "token inner s0\n",
            [(0, "uncaught")],
        ),
        (
            "payload-current-script",
            [
                "window.onerror = function(m,u,l,c,e) { console.log(document.currentScript.id, e.name); };",
                "throw new TypeError('current');",
                "var broken = ;",
                "console.log(document.currentScript.id);",
            ],
            [],
            "s1 TypeError\ns2 SyntaxError\ns3\n",
            [(1, "current")],
        ),
        (
            "tdz-parse-control",
            [
                "let pending = ;",
                "console.log(typeof pending); let later = 1;",
                "let pending = 2; console.log(pending);",
            ],
            [],
            "undefined\n2\n",
            [(0, "parse error")],
        ),
    ]
    witnesses += [
        (
            "payload-inserted",
            [
                payload_setup,
                "var at = ''; window.onerror = function() { at = document.currentScript.id; };"
                "var s = document.createElement('script'); s.id = 'inserted';"
                "s.textContent = 'throw token;'; document.body.appendChild(s);"
                "console.log(seen.join(','), at, document.currentScript.id);",
            ],
            [],
            "token inserted s1\n",
            [],
        ),
        (
            "payload-inserted-syntax",
            [
                payload_setup,
                "var at = ''; window.onerror = function() { at = document.currentScript.id; };"
                "var s = document.createElement('script'); s.id = 'inserted';"
                "s.textContent = 'let broken = ;'; document.body.appendChild(s);"
                "console.log(seen.join(','), at, document.currentScript.id);",
            ],
            [],
            "syntax inserted s1\n",
            [],
        ),
        (
            "payload-nested-syntax",
            [
                payload_setup
                + "var at = ''; window.onerror = function() { at = document.currentScript.id; };"
                "document.write('<script id=inner>let broken = ;<' + '/script>');"
                "console.log(seen.join(','), at, document.currentScript.id);"
            ],
            [],
            "syntax inner s0\n",
            [(0, "parse error")],
        ),
        (
            "tdz-global-object",
            [
                "globalThis.pending = 42;",
                "throw 0; let pending = 1;",
                "try { console.log(typeof pending); } catch(e) { console.log(e.name); }"
                "console.log(globalThis.pending);",
            ],
            [],
            "ReferenceError\n42\n",
            [(1, "uncaught")],
        ),
        (
            "tdz-named-element",
            [
                "throw 0; let pending = 1;",
                "var el = document.createElement('div'); el.id = 'pending'; document.body.appendChild(el);"
                "try { console.log(typeof pending); } catch(e) { console.log(e.name); }"
                "console.log(window.pending === el);",
            ],
            [],
            "ReferenceError\ntrue\n",
            [(0, "uncaught")],
        ),
        (
            "tdz-nested",
            [
                payload_setup + "document.write('<script>throw 0; let pending = 1;<' + '/script>');"
                "try { console.log(typeof pending); } catch(e) { console.log(e.name); }"
            ],
            [],
            "ReferenceError\n",
            [(0, "uncaught")],
        ),
        (
            "payload-declaration",
            [
                payload_setup + "let pending = 1;",
                "let pending = 2;",
                "console.log(seen.join(','));",
            ],
            [],
            "syntax\n",
            [(1, "SyntaxError")],
        ),
    ]
    for declaration in ("let pending = 1;", "const pending = 1;", "class pending {}"):
        kind = declaration.split()[0]
        witnesses.append(
            (
                "tdz-" + kind,
                [
                    "throw 0; " + declaration,
                    "try { console.log(typeof pending); } catch(e) { console.log(e.name); }"
                    "try { pending = 9; console.log('bad assignment'); } catch(e) { console.log(e.name); }",
                    "try { console.log(typeof pending); } catch(e) { console.log(e.name); }"
                    "console.log(typeof neverDeclared);",
                ],
                [],
                "ReferenceError\nReferenceError\nReferenceError\nundefined\n",
                [(0, "uncaught")],
            )
        )
    for name, sources, appended, expected, errors in witnesses:
        page = name + ".html"
        (work / page).write_text(
            "<!doctype html><body>"
            + "".join(f'<script id="s{i}">{source}</script>' for i, source in enumerate(sources))
            + "</body>"
        )
        scripts = []
        for i, source in enumerate(appended):
            script = name + f"-append{i}.js"
            (work / script).write_text(source)
            scripts.append(script)
        diagnostics = [
            (name + f"-{site}.js" if isinstance(site, str) else page, message)
            for site, message in errors
        ]
        cases.append((name, page, scripts, expected, diagnostics))
    failures = []
    for name, page, scripts, expected, errors in cases:
        command = [args.reference, "--page", str(work / page)]
        for script in scripts:
            command += ["--script", str(work / script)]
        result = subprocess.run(command, text=True, capture_output=True, timeout=30, check=False)
        expected_status = 1 if errors else 0
        assert result.returncode == expected_status, (name, result)
        if name in {"appended", "inline", "syntax", "multiple-errors", "success"}:
            assert result.stdout == expected, (name, expected, result)
        if result.stdout != expected:
            failures.append((name, expected, result.stdout, result.stderr))
        diagnostics = [
            line for line in result.stderr.splitlines() if line.startswith("native reference:")
        ]
        assert len(diagnostics) == len(errors), (name, errors, result)
        for diagnostic, (filename, message) in zip(diagnostics, errors):
            assert str(work / filename) in diagnostic and message in diagnostic, (name, result)
        if not errors:
            assert not result.stderr, (name, result)
        matched = "PASS" if result.stdout == expected else "FAIL"
        print(f"{name}: {matched}, status={expected_status}, ordered console and error reporting")
    assert not failures, failures


if __name__ == "__main__":
    main()
