#!/usr/bin/env python3
"""Extract p5.js's own GLSL shaders into tests/glsl/ as a parse corpus.

The point is that this is SOMEBODY ELSE'S GLSL. A parser tested only against
shaders written to suit it passes by construction; these are the shaders the
target actually ships, written without any knowledge of this implementation, and
they are what decided the language subset in docs/webgl-plan.md - structs, a
preprocessor, and uniform arrays walked by loops.

They live in the bundle as JavaScript string literals:

    var phongVert = "precision highp int;\\n\\n#define HOOK_DEFINES\\n...";

so this decodes the literal and writes the real text out. Regenerate with:

    tools/gen-glsl-fixtures.py

Committed rather than extracted at build time, for the same reason
examples/assets is committed: a test that regenerates its own inputs from a 4.5
MB dependency fails for reasons that have nothing to do with the code.
"""

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
BUNDLE = ROOT / "examples/assets/p5/p5.js"
OUT = ROOT / "tests/glsl"

# `  var phongVert = "....";` - the whole literal on one line, which is how the
# bundler emits them.
DECLARATION = re.compile(r'^\s*var ([A-Za-z0-9_]+) = ("(?:[^"\\]|\\.)*");\s*$', re.M)


def decode(literal: str) -> str:
    """A JavaScript double-quoted string literal to its text."""
    out = []
    i = 1  # past the opening quote
    end = len(literal) - 1
    while i < end:
        c = literal[i]
        if c != "\\":
            out.append(c)
            i += 1
            continue
        i += 1
        e = literal[i]
        i += 1
        if e == "n":
            out.append("\n")
        elif e == "t":
            out.append("\t")
        elif e == "r":
            out.append("\r")
        elif e == "u":
            out.append(chr(int(literal[i:i + 4], 16)))
            i += 4
        elif e == "x":
            out.append(chr(int(literal[i:i + 2], 16)))
            i += 2
        else:
            out.append(e)  # \\ \" and anything else is itself
    return "".join(out)


def extension(name: str, body: str) -> str:
    """.vert or .frag, decided by the shader rather than by the name.

    A vertex shader writes gl_Position and a fragment shader does not; the name
    is a hint and the body is the fact.
    """
    if "gl_Position" in body:
        return "vert"
    if "gl_FragColor" in body or "gl_FragDepth" in body or "OUT vec4" in body:
        return "frag"
    return "vert" if name.lower().endswith("vert") else "frag"


def main() -> int:
    if not BUNDLE.exists():
        sys.exit(f"gen-glsl-fixtures: {BUNDLE.relative_to(ROOT)} is missing")
    source = BUNDLE.read_text(errors="replace")

    OUT.mkdir(parents=True, exist_ok=True)
    for stale in OUT.glob("*.vert"):
        stale.unlink()
    for stale in OUT.glob("*.frag"):
        stale.unlink()
    (OUT / "preamble.glsl").unlink(missing_ok=True)

    # THE PREAMBLE p5 PREPENDS TO EVERY SHADER, written out beside them.
    #
    # Without it the corpus does not parse and should not: `IN vec4 vColor;`
    # only means anything once IN is a macro. p5 assembles
    # webgl2CompatibilityShader + the shader, with WEBGL2 defined only when it
    # got a WebGL 2 context - which this engine never gives it, so the corpus
    # test compiles the same text a real p5 run would.
    preamble = re.search(r'var webgl2CompatibilityShader = ("(?:[^"\\]|\\.)*");', source)
    if not preamble:
        sys.exit("gen-glsl-fixtures: the shader preamble is missing - has the bundle changed?")
    (OUT / "preamble.glsl").write_text(decode(preamble.group(1)))
    print(f"  {(OUT / 'preamble.glsl').relative_to(ROOT)}  the macros p5 prepends")

    written = 0
    for name, literal in DECLARATION.findall(source):
        body = decode(literal)
        # A shader, not any other string that happens to be declared this way:
        # every one of them declares a main().
        if "void main" not in body:
            continue
        path = OUT / f"{name}.{extension(name, body)}"
        path.write_text(body)
        written += 1
        print(f"  {path.relative_to(ROOT)}  {len(body):>6} bytes, "
              f"{body.count(chr(10)) + 1:>4} lines")

    if written == 0:
        sys.exit("gen-glsl-fixtures: found no shaders - has the bundle's shape changed?")
    print(f"\ngen-glsl-fixtures: wrote {written} shaders to {OUT.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
