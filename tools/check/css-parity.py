#!/usr/bin/env python3
"""How far is a Bootstrap page from Chrome? Per element, per property, in numbers.

    tools/check/compare.py setup                 # once: the venv Playwright needs
    tools/check/css-parity.py                     # every fixture
    tools/check/css-parity.py --all examples/pages/bootstrap-grid.html
    tools/check/css-parity.py --advance           # record the numbers, deliberately

Drives ctbrowser and Chrome through compare.py's daemon, runs ONE dump script
(css-dump.js) in both, and diffs the answers. Two numbers per fixture, and both
are ratcheted:

    differ        values that disagree            -> falls as layout gets right
    substituted   values ctbrowser did not give   -> falls as properties get modelled
    cells         coarse screen cells that differ -> falls as the RENDER gets right

The third is the guard against mistaking the first two for the picture: they
score each element's own computed values, so neither can see a box that reports
everything correctly and is never painted, or one that is drawn on top of
another. Both of those have shipped here. See the note on CELL_COLUMNS.

The second one is the guard against mistaking silence for success. A property
ctbrowser cannot answer comes back empty, this tool substitutes the CSS initial
value, and if that happens to equal Chrome's answer there is no diff - which
would read as "implemented" when nothing is implemented at all.

NOT A CTEST, deliberately: docs/build.md - "a browser-versus-browser diff should
be read, not silently failed." The numbers are ratcheted so a regression is
caught; the READING is a person's job. tests/unit/bootstrap_layout.cpp is the
automatic half and needs no Chrome.
"""
import argparse
import importlib.util
import json
import math
import re
import subprocess
import sys
import time
from collections import defaultdict
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
RECORD = HERE / "css-parity.txt"
PROPS_FILE = HERE / "css-parity-props.txt"

# Chrome's LayoutUnit is 1/64 px, so that is the smallest difference Chrome can
# represent. NOT A KNOB: anything below it is not a disagreement about layout,
# anything at or above it is. The ratchet is on the COUNT of differences, never on
# this number - a 0.5px difference is a recorded difference, not "within
# tolerance". Raising it would be a visible one-line edit to a file whose header
# says only --advance writes here, which is the point.
EPSILON_PX = 1.0 / 64.0

# --- the visual instrument ---------------------------------------------------
#
# THE PROPERTY DIFF HAS A BLIND SPOT, and it has been walked into twice. It scores
# each element's own computed values, so it cannot see:
#
#   * a box that is the right size, at the right place, reporting the right
#     colour, and NEVER PAINTED. `parse_color` matched `rgb(` case-sensitively
#     while Bootstrap writes `RGBA(`, so every badge on the page drew nothing -
#     and the numbers did not move by one when it was fixed, because this tool
#     normalises both sides' text and the two spellings already compared equal.
#   * a box drawn ON TOP OF another. `.h-100` collapsed three cards to their
#     headers and the table below them drew straight through the wreckage; the
#     numbers moved by FOUR.
#
# So there is a third number, and it measures the pixels.
#
# NOT A PIXEL DIFF. Pixel-identical to Chrome is not reachable and never will be -
# a different text rasteriser is a different set of pixels - and a metric that can
# never reach zero is a metric nobody reads. Instead both screenshots are reduced
# to a COARSE GRID of mean colours, and the number is how many cells disagree.
# That is deliberately blind to glyph antialiasing and deliberately loud about a
# missing fill, a box in the wrong place, or a colour that is simply wrong: one
# 32x24 cell is 32x32 pixels, far larger than any rasteriser difference and far
# smaller than any component.
#
# The threshold is a MEAN over the cell, so a handful of differently-antialiased
# glyph edges move it by a few units where a missing background moves it by
# hundreds. 24 is about 9% of the channel range, chosen so that the same page
# rendered by the same engine twice scores 0 and a missing `.badge` scores its
# whole area.
CELL_COLUMNS = 32
CELL_ROWS = 24
CELL_TOLERANCE = 24

# THE COMPARED SET, and the only copy of it. css-dump.js gets it prepended as a
# literal, and tests/unit/bootstrap_layout.cpp reads css-parity-props.txt written
# by --emit-props, so there is no second list to drift.
#
# Chosen by four rules: the property appears in bootstrap.css; it is a LONGHAND
# (Chrome reconstructs shorthands with rules that differ between engines and
# between its own versions); it changes geometry or visible decoration; and both
# engines could in principle produce it. That excludes cursor, pointer-events,
# user-select, appearance, -webkit-*, and font-family - Chrome returns the
# specified list there and ctbrowser resolves to one name, so it always differs
# and measures nothing.
PROPS_VERSION = 1
PROPS = [
    # box
    "display", "box-sizing", "width", "height",
    "min-width", "max-width", "min-height", "max-height",
    "margin-top", "margin-right", "margin-bottom", "margin-left",
    "padding-top", "padding-right", "padding-bottom", "padding-left",
    # positioning
    "position", "top", "right", "bottom", "left", "z-index",
    # flow
    "float", "clear",
    # flex
    "flex-direction", "flex-wrap", "flex-grow", "flex-shrink", "flex-basis",
    "justify-content", "align-items", "align-self", "align-content", "order",
    "row-gap", "column-gap",
    # text
    "line-height", "text-align", "vertical-align", "white-space",
    "font-size", "font-weight",
    # paint
    "color", "background-color",
]
GEOMETRY = ["@x", "@y", "@w", "@h"]

# The CSS initial value, substituted when ctbrowser answers nothing. `display` is
# NOT here: its initial value is `inline` but almost nothing renders as that - a
# div is `block` because a UA sheet says so - so ctbrowser reports display from
# the box tree instead (src/shell/bindings/computed_style.cpp) and an empty answer
# there is a real finding rather than a missing default.
INITIAL = {
    "box-sizing": "content-box",
    "width": "auto", "height": "auto",
    # 0px, THOUGH CSS SIZING 3 SAYS THE INITIAL VALUE IS `auto`. Chrome reports
    # `auto` for a FLEX ITEM and `0px` for everything else, because `min-width:
    # auto` only has a meaning - the automatic minimum size - inside a flex or grid
    # container. Setting `auto` here to match the spec moved 566 differences the
    # wrong way, which is a good measurement of how much of Bootstrap is flex: the
    # remaining min-width differences are flex items and they close at the flex
    # rung, not in this table.
    "min-width": "0px", "max-width": "none", "min-height": "0px", "max-height": "none",
    "margin-top": "0px", "margin-right": "0px", "margin-bottom": "0px", "margin-left": "0px",
    "padding-top": "0px", "padding-right": "0px", "padding-bottom": "0px", "padding-left": "0px",
    "position": "static",
    "top": "auto", "right": "auto", "bottom": "auto", "left": "auto", "z-index": "auto",
    "float": "none", "clear": "none",
    "flex-direction": "row", "flex-wrap": "nowrap",
    "flex-grow": "0", "flex-shrink": "1", "flex-basis": "auto",
    "justify-content": "normal", "align-items": "normal", "align-self": "auto",
    "align-content": "normal", "order": "0",
    "row-gap": "normal", "column-gap": "normal",
    "line-height": "normal", "text-align": "start", "vertical-align": "baseline",
    "white-space": "normal", "font-size": "16px", "font-weight": "400",
    "color": "rgb(0, 0, 0)", "background-color": "rgba(0, 0, 0, 0)",
}

FIXTURES = ["box", "type", "grid", "components", "position", "kitchen"]
VIEWPORT = (1024, 768)
CHUNK = 40


# --- talking to the rig ------------------------------------------------------

def load_compare():
    """compare.py as a module, so there is one client and one protocol."""
    spec = importlib.util.spec_from_file_location("ctcompare", HERE / "compare.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def dump_script(from_index: int, count: int) -> str:
    body = (HERE / "css-dump.js").read_text()
    head = (
        f"var PROPS = {json.dumps(PROPS)};\n"
        f"var PROPS_VERSION = {PROPS_VERSION};\n"
        f"var FROM = {from_index};\n"
        f"var COUNT = {count};\n"
    )
    return head + body


def collect(cmp, engines: list[str]) -> dict:
    """Every element from every engine, chunk by chunk.

    Chunked because a reply is one line per element and a 300-element page is a
    few hundred kilobytes; 40 keeps a reply around 40 KB, which is comfortable for
    both transports. ctdrive writes a large reply correctly either way - see
    tools/check/ctdrive-reply.py - but a smaller reply is easier to read when
    something goes wrong.
    """
    rows = {name: {} for name in engines}
    order = {name: [] for name in engines}
    heads = {}
    at = 0
    total = None
    while total is None or at < total:
        answer = cmp.request({"verb": "eval", "args": [dump_script(at, CHUNK)]})
        for name in engines:
            side = answer.get(name)
            if side is None:
                raise RuntimeError(f"engine {name!r} did not answer")
            if side.get("error"):
                raise RuntimeError(f"engine {name!r} threw: {side['error']}")
            for line in side.get("console", []):
                if line.startswith("#head "):
                    heads[name] = json.loads(line[len("#head "):])
                    continue
                path, _, rest = line.partition("|")
                ident, _, payload = rest.partition("|")
                if not payload:
                    continue
                rows[name][path] = (ident, json.loads(payload))
                order[name].append(path)
        counts = {h["n"] for h in heads.values()}
        if total is None:
            if not heads:
                raise RuntimeError("no header line: the dump did not run")
            total = max(counts)
        at += CHUNK
    return {"rows": rows, "order": order, "heads": heads}


# --- normalising -------------------------------------------------------------

LENGTH_RE = re.compile(r"^-?(?:\d+\.?\d*|\.\d+)px$")
NUMBER_RE = re.compile(r"^-?(?:\d+\.?\d*|\.\d+)$")
RGB_RE = re.compile(r"^rgba?\(([^)]*)\)$")
HEX_RE = re.compile(r"^#([0-9a-fA-F]{3,8})$")
NAMED = {
    "transparent": (0, 0, 0, 0.0), "black": (0, 0, 0, 1.0), "white": (255, 255, 255, 1.0),
    "red": (255, 0, 0, 1.0), "lime": (0, 255, 0, 1.0), "blue": (0, 0, 255, 1.0),
    "gray": (128, 128, 128, 1.0), "grey": (128, 128, 128, 1.0),
}


def quantise(value: float) -> float:
    return round(value / EPSILON_PX) * EPSILON_PX


def normalise(prop: str, raw: str):
    """One normaliser, applied to BOTH engines.

    Neither engine imitates the other's serialiser: Chrome prints 13.3333px and
    its colour output has drifted across versions, and chasing that byte for byte
    measures nothing about layout. Returns a float for a length or a number and a
    string otherwise, so the comparison can be numeric where that is meaningful.
    """
    # getBoundingClientRect gives JSON NUMBERS, while every property gives a
    # string - so this takes both rather than making the dump stringify and this
    # side parse back, which is one more place for the two engines to differ.
    if isinstance(raw, (int, float)) and not isinstance(raw, bool):
        return quantise(float(raw))
    text = (raw or "").strip().lower()
    if text == "":
        return ""
    if prop in GEOMETRY:
        try:
            return quantise(float(text))
        except ValueError:
            return text
    if LENGTH_RE.match(text):
        return quantise(float(text[:-2]))
    if text == "0":
        return 0.0
    if NUMBER_RE.match(text):
        return float(text)
    hit = RGB_RE.match(text)
    if hit:
        parts = [p.strip() for p in re.split(r"[,\s/]+", hit.group(1)) if p.strip()]
        try:
            nums = [float(p[:-1]) * 2.55 if p.endswith("%") else float(p) for p in parts]
        except ValueError:
            return text
        if len(nums) >= 3:
            a = nums[3] if len(nums) > 3 else 1.0
            return colour_text(nums[0], nums[1], nums[2], a)
        return text
    hit = HEX_RE.match(text)
    if hit:
        digits = hit.group(1)
        if len(digits) in (3, 4):
            digits = "".join(c * 2 for c in digits)
        if len(digits) in (6, 8):
            vals = [int(digits[i:i + 2], 16) for i in range(0, len(digits), 2)]
            a = vals[3] / 255.0 if len(vals) > 3 else 1.0
            return colour_text(vals[0], vals[1], vals[2], a)
        return text
    if text in NAMED:
        r, g, b, a = NAMED[text]
        return colour_text(r, g, b, a)
    # A keyword or a list: collapse whitespace and drop the quotes engines
    # disagree about putting round a font or content string.
    return " ".join(text.replace('"', "").replace("'", "").split())


def colour_text(r, g, b, a) -> str:
    r, g, b = (int(round(max(0.0, min(255.0, v)))) for v in (r, g, b))
    if a >= 1.0:
        return f"rgb({r}, {g}, {b})"
    return f"rgba({r}, {g}, {b}, {round(a, 3)})"


def same(prop: str, mine, theirs) -> bool:
    if isinstance(mine, float) and isinstance(theirs, float):
        return abs(mine - theirs) < EPSILON_PX
    return mine == theirs


def show(value) -> str:
    if isinstance(value, float):
        return f"{value:.6f}".rstrip("0").rstrip(".") or "0"
    return "-" if value == "" else str(value)


# --- the comparison ----------------------------------------------------------

class Finding:
    __slots__ = ("path", "ident", "prop", "mine", "theirs", "substituted")

    def __init__(self, path, ident, prop, mine, theirs, substituted):
        self.path, self.ident, self.prop = path, ident, prop
        self.mine, self.theirs, self.substituted = mine, theirs, substituted


def compare_page(data, mine_name, theirs_name):
    mine, theirs = data["rows"][mine_name], data["rows"][theirs_name]
    only_mine = [p for p in data["order"][mine_name] if p not in theirs]
    only_theirs = [p for p in data["order"][theirs_name] if p not in mine]

    findings, substituted = [], 0
    shared = [p for p in data["order"][theirs_name] if p in mine]
    for path in shared:
        ident, ours = mine[path]
        _, chrome = theirs[path]
        for prop in GEOMETRY + PROPS:
            raw_ours = ours.get(prop, "")
            was_empty = isinstance(raw_ours, str) and raw_ours.strip() == ""
            if was_empty and prop in INITIAL:
                raw_ours = INITIAL[prop]
                substituted += 1
            elif was_empty:
                substituted += 1
            a = normalise(prop, raw_ours)
            b = normalise(prop, chrome.get(prop, ""))
            if not same(prop, a, b):
                findings.append(Finding(path, ident, prop, a, b, was_empty))
    return {
        "findings": findings,
        "substituted": substituted,
        "elements": len(shared),
        "only_mine": only_mine,
        "only_theirs": only_theirs,
        "compared": len(shared) * len(GEOMETRY + PROPS),
    }


def rank(result):
    """Group so the biggest CAUSE surfaces first, each rule consuming what it explains.

    Without this the report is dominated by consequences: one wrong number high in
    the tree moves every descendant, and a missing feature repeats on every element
    that uses it.
    """
    findings = result["findings"]
    claimed = set()
    groups = []

    # 1. A whole subtree shifted by the same amount is ONE finding. This is the
    #    answer to "a 1/64 error at the root shows up 400 times".
    by_path_axis = defaultdict(list)
    for f in findings:
        if f.prop in ("@x", "@y"):
            by_path_axis[(f.path, f.prop)].append(f)
    shifts = defaultdict(list)
    for f in findings:
        if f.prop in ("@x", "@y") and isinstance(f.mine, float) and isinstance(f.theirs, float):
            shifts[(f.prop, round(f.theirs - f.mine, 4))].append(f)
    for (prop, delta), members in sorted(shifts.items(), key=lambda kv: -len(kv[1])):
        if len(members) < 8:
            continue
        root = min((f.path for f in members), key=len)
        groups.append((f"{len(members)} elements share a {prop} offset of {delta:+g}px",
                       f"first at {root}", members))
        claimed.update(id(f) for f in members)

    # 2. By property: which FEATURE is missing.
    by_prop = defaultdict(list)
    for f in findings:
        if id(f) not in claimed:
            by_prop[f.prop].append(f)
    for prop, members in sorted(by_prop.items(), key=lambda kv: -len(kv[1])):
        subs = sum(1 for f in members if f.substituted)
        note = f"{subs} of them not answered at all" if subs else "all answered, all wrong"
        groups.append((f"{prop}: {len(members)} of {result['elements']} differ", note, members))
        claimed.update(id(f) for f in members)
    return groups


def by_component(findings):
    """Which class token the differing elements share - i.e. which COMPONENT is broken.

    Best-effort and explicitly not a cascade attribution: it intersects the class
    lists in the element keys. It answers "the .btn rows are wrong", which is what
    tells you where to look, not "this rule did it".
    """
    counts = defaultdict(set)
    for f in findings:
        for token in re.findall(r"\.([A-Za-z0-9_-]+)", f.ident.split("#")[-1]):
            counts[token].add(f.path)
    return sorted(((len(v), k) for k, v in counts.items()), reverse=True)


# --- the record file --------------------------------------------------------

HEADER = f"""\
# How far Bootstrap 5.3.8 is from Chrome, per fixture. tools/check/css-parity.py
# measures it; this file records it.
#
# `differ` and `substituted` may not go UP, and `elements` may not CHANGE, without
# the tool failing. Only --advance writes here, deliberately: a test that edits its
# own expectations cannot fail.
#
# elements     compared on both sides
# differ       values that disagree, at 1/64px for lengths
# substituted  values ctbrowser did not answer, so the CSS initial value was used
props-version={PROPS_VERSION}
epsilon-px={EPSILON_PX}
viewport={VIEWPORT[0]}x{VIEWPORT[1]}
"""


def cell_means(width: int, height: int, rgb: bytes) -> list:
    """The mean colour of each cell of a CELL_COLUMNS x CELL_ROWS grid."""
    out = []
    for row in range(CELL_ROWS):
        y0 = row * height // CELL_ROWS
        y1 = max(y0 + 1, (row + 1) * height // CELL_ROWS)
        for col in range(CELL_COLUMNS):
            x0 = col * width // CELL_COLUMNS
            x1 = max(x0 + 1, (col + 1) * width // CELL_COLUMNS)
            r = g = b = n = 0
            # Every fourth row and column: a mean over a 32x32 cell is the same
            # number to within a rounding either way, and this is 16x less work
            # on six fixtures' worth of megapixels.
            for y in range(y0, y1, 4):
                base = y * width * 3
                for x in range(x0, x1, 4):
                    at = base + x * 3
                    r += rgb[at]
                    g += rgb[at + 1]
                    b += rgb[at + 2]
                    n += 1
            out.append((r / n, g / n, b / n) if n else (0.0, 0.0, 0.0))
    return out


def cells_that_differ(cmp, name: str, engines: list[str]) -> int:
    """How many cells of the two renders disagree. See the note on CELL_COLUMNS.

    Screenshots BOTH engines through the running session, which is why this lives
    in the same loop as the property dump rather than in a tool of its own: the
    two measurements are of the same page in the same state, and a second session
    would be a second chance for them to disagree about something that is not the
    engine.
    """
    answer = cmp.request({"verb": "shot", "args": [f"parity-{name}"]})
    shots = {}
    for engine in engines:
        side = answer.get(engine) or {}
        if not side.get("ok") or not side.get("path"):
            raise RuntimeError(f"{engine} could not be screenshotted: {side.get('error')}")
        shots[engine] = Path(side["path"])
    w1, h1, a = cmp.read_png(shots[engines[0]])
    w2, h2, b = cmp.read_png(shots[engines[1]])
    if (w1, h1) != (w2, h2):
        # Different sizes is a rig failure, not a difference: there is no
        # meaningful cell-to-cell correspondence between two different viewports.
        raise RuntimeError(f"screenshots differ in size: {w1}x{h1} vs {w2}x{h2}")
    ours = cell_means(w1, h1, a)
    ref = cell_means(w2, h2, b)
    bad = [i for i, (p, q) in enumerate(zip(ours, ref))
           if max(abs(p[0] - q[0]), abs(p[1] - q[1]), abs(p[2] - q[2])) > CELL_TOLERANCE]
    # WHERE, not just how many. A count says there is something to look at; the
    # band of rows says where to point the screenshot, and that is the difference
    # between a number and a lead.
    where = ""
    if bad:
        rows = sorted({i // CELL_COLUMNS for i in bad})
        cols = sorted({i % CELL_COLUMNS for i in bad})
        top = rows[0] * h1 // CELL_ROWS
        bottom = (rows[-1] + 1) * h1 // CELL_ROWS
        left = cols[0] * w1 // CELL_COLUMNS
        right = (cols[-1] + 1) * w1 // CELL_COLUMNS
        where = f"y {top}-{bottom}, x {left}-{right}"
    return len(bad), where


def read_record() -> dict:
    if not RECORD.exists():
        return {}
    out = {}
    for line in RECORD.read_text().splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            if line.startswith("props-version=") or line.startswith("epsilon-px=") \
                    or line.startswith("viewport="):
                key, _, value = line.partition("=")
                out[key] = value
            continue
        if line.startswith("props-version=") or line.startswith("epsilon-px=") \
                or line.startswith("viewport="):
            key, _, value = line.partition("=")
            out[key] = value
            continue
        hit = re.match(r"^\[(?P<name>[^\]]+)\]\s+(?P<rest>.*)$", line)
        if not hit:
            continue
        fields = dict(re.findall(r"(\w+)=(\S+)", hit.group("rest")))
        out[hit.group("name")] = {k: int(v) for k, v in fields.items()}
    return out


def write_record(results: dict) -> None:
    body = [HEADER]
    for name in FIXTURES:
        got = results.get(name)
        if got is None:
            continue
        cells = "" if got.get("cells") is None else f"  cells={got['cells']}"
        body.append(
            f"[bootstrap-{name}.html]  elements={got['elements']}  "
            f"differ={len(got['findings'])}  substituted={got['substituted']}{cells}"
        )
    RECORD.write_text("\n".join(body) + "\n")


# --- reporting ---------------------------------------------------------------

def report(name, result, limit, show_all):
    print(f"\n=== bootstrap-{name}.html "
          f"({result['elements']} elements, {len(GEOMETRY + PROPS)} properties) ===")
    if result["only_mine"] or result["only_theirs"]:
        print(f"  TREE SHAPE: {len(result['only_mine'])} ctbrowser-only, "
              f"{len(result['only_theirs'])} chrome-only elements")
        for path in (result["only_mine"] + result["only_theirs"])[:5]:
            print(f"    {path}")
    groups = rank(result)
    if groups:
        print("\n  BY CAUSE")
        for headline, note, members in groups[:12]:
            print(f"    {headline:<58} {note}")
    findings = result["findings"]
    if findings:
        components = by_component(findings)[:6]
        if components:
            print("\n  BY COMPONENT   " +
                  ", ".join(f".{token} ({n})" for n, token in components))
        print(f"\n  {'ELEMENT':<44} {'PROPERTY':<17} {'ctbrowser':<16} chrome")
        for f in findings[: (len(findings) if show_all else limit)]:
            ident = f.ident if len(f.ident) <= 43 else f.ident[:40] + "..."
            print(f"  {ident:<44} {f.prop:<17} {show(f.mine):<16} {show(f.theirs)}")
        if not show_all and len(findings) > limit:
            print(f"  ... {len(findings) - limit} more (--all)")
    print(f"\n  {result['elements']} elements, {len(GEOMETRY + PROPS)} properties: "
          f"{len(findings)} differ / {result['compared']}, substituted={result['substituted']}")
    if result.get("cells") is not None:
        total = CELL_COLUMNS * CELL_ROWS
        where = result.get("cells_where") or ""
        print(f"  {result['cells']} of {total} screen cells look different"
              + (f", around {where}" if where else "")
              + " - the number the property diff cannot see")


def main() -> int:
    ap = argparse.ArgumentParser(prog="css-parity.py", description=__doc__.splitlines()[0])
    ap.add_argument("pages", nargs="*", help="fixture paths; default is all six")
    ap.add_argument("--all", action="store_true", help="print every difference, not the first 40")
    ap.add_argument("--limit", type=int, default=40)
    ap.add_argument("--advance", action="store_true", help="record the numbers (the only writer)")
    ap.add_argument("--emit-props", action="store_true",
                    help="write css-parity-props.txt for the C++ test and exit")
    ap.add_argument("--engine", default="chrome", help="the reference engine")
    ap.add_argument("--keep", action="store_true", help="leave the session running")
    ap.add_argument("--remote", nargs="?", const="devbox", default="", metavar="HOST",
                    help="drive ctdrive on HOST rather than a local build (see compare.py)")
    args = ap.parse_args()

    if args.emit_props:
        PROPS_FILE.write_text(
            "# GENERATED by tools/check/css-parity.py --emit-props. Do not edit.\n"
            f"# version={PROPS_VERSION}\n" + "\n".join(GEOMETRY + PROPS) + "\n")
        print(f"wrote {PROPS_FILE.relative_to(REPO)} ({len(GEOMETRY + PROPS)} columns)")
        return 0

    cmp = load_compare()
    names = args.pages or [f"examples/pages/bootstrap-{f}.html" for f in FIXTURES]
    record = read_record()
    if record.get("props-version") and int(record["props-version"]) != PROPS_VERSION:
        print(f"css-parity: record file is props-version={record['props-version']}, "
              f"this tool is {PROPS_VERSION}. Re-baseline deliberately (--advance).",
              file=sys.stderr)
        return 2

    results, rc = {}, 0
    for page in names:
        stem = Path(page).stem.replace("bootstrap-", "")
        subprocess.run([sys.executable, str(HERE / "compare.py"), "stop"],
                       capture_output=True, check=False)
        start = subprocess.run(
            [sys.executable, str(HERE / "compare.py"), "start", page,
             "--engine", f"ctbrowse,{args.engine}",
             "--size", str(VIEWPORT[0]), str(VIEWPORT[1])]
            + (["--remote", args.remote] if args.remote else []),
            capture_output=True, text=True, check=False)
        if start.returncode != 0:
            print(f"css-parity: could not start a session for {page}:\n{start.stderr}",
                  file=sys.stderr)
            return 2
        time.sleep(1.0)
        # THE SESSION STAYS OPEN UNTIL BOTH MEASUREMENTS ARE TAKEN. The property
        # dump and the screenshot must be of the same page in the same state, and
        # a second session would be a second chance for them to disagree about
        # something that is not the engine.
        cells = None
        cells_where = ""
        try:
            data = collect(cmp, ["ctbrowse", args.engine])
            cells, cells_where = cells_that_differ(cmp, stem, ["ctbrowse", args.engine])
        except Exception as why:
            # A screenshot failure is a RIG failure and must not read as parity:
            # scoring zero cells because no image arrived would be the best
            # possible number for the worst possible reason.
            print(f"css-parity: RIG FAILURE on {page}: {why}", file=sys.stderr)
            return 2
        finally:
            if not args.keep:
                subprocess.run([sys.executable, str(HERE / "compare.py"), "stop"],
                               capture_output=True, check=False)

        heads = data["heads"]
        widths = {name: h.get("cw") for name, h in heads.items()}
        if len(set(widths.values())) > 1:
            print(f"css-parity: RIG FAILURE on {page}: the two engines disagree about the "
                  f"viewport ({widths}). Every @x and @w would differ for one reason; "
                  f"fix the scrollbar/ICB difference before reading a property diff.",
                  file=sys.stderr)
            return 2

        result = compare_page(data, "ctbrowse", args.engine)
        result["cells"] = cells
        result["cells_where"] = cells_where
        results[stem] = result
        report(stem, result, args.limit, args.all)

        was = record.get(f"bootstrap-{stem}.html")
        if was and not args.advance:
            for field, now in (("differ", len(result["findings"])),
                               ("substituted", result["substituted"]),
                               ("cells", result["cells"])):
                if now > was.get(field, now):
                    print(f"  RATCHET: {field} rose {was[field]} -> {now}")
                    rc = 1
            if was.get("elements") not in (None, result["elements"]):
                print(f"  RATCHET: elements changed {was['elements']} -> {result['elements']}; "
                      f"the fixture or the parser moved and the other numbers are "
                      f"incomparable. Re-baseline deliberately.")
                rc = 1

    if args.advance:
        write_record(results)
        print(f"\nwrote {RECORD.relative_to(REPO)}")
    return rc


if __name__ == "__main__":
    sys.exit(main())
