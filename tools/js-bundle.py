#!/usr/bin/env python3
"""js-bundle.py - a compile-time ES-module bundler for ctbrowser.

ctbrowser parses ONE HTML source entirely at compile time, and ctjs (its
JS engine) has no module system - it runs one script in one global scope.
Real-world apps, though, are written as ES modules (import/export across
many files) and pull library symbols from npm packages. Vite/rollup solve
this by bundling to a single script at BUILD time; this tool is that step
for ctbrowser: it resolves the module graph, strips import/export, maps
bare-package imports (e.g. `@babylonjs/core`) onto the globals ctbrowser
already provides (the `BABYLON` namespace from babylon.hpp), and emits ONE
self-contained HTML that the compile-time pipeline then parses verbatim.

ctjs already parses everything these modules USE - class fields (instance
+ static), getters/setters, computed names, ??/?./?.()/optional-index,
async/await - so NO syntax down-levelling happens here. We only linearise
the module graph.

Usage:
    js-bundle.py <entry.html> -o <out.html> [--base DIR] [--map pkg=GLOBAL ...]

The entry HTML's `<script type="module" src="...">` names the entry module;
its whole graph is inlined as one plain `<script>`. `.scss`/`.sass`
stylesheet links are compiled (via the `sass` CLI if present) and inlined
as `<style>`; other inline scripts/styles are preserved in place.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys

# Bare-specifier -> global object. `import {Engine} from "@babylonjs/core"`
# becomes `const Engine = BABYLON.Engine;`. GUI symbols live under BABYLON.GUI.
DEFAULT_PKG_MAP = {
    "@babylonjs/core": "BABYLON",
    "@babylonjs/gui": "BABYLON.GUI",
    "@babylonjs/loaders": None,   # side-effect only (glTF loader) - always on in ctbrowser
}


class Module:
    def __init__(self, path):
        self.path = path                 # absolute, normalised
        self.deps = []                   # ordered list of local module paths this imports
        self.body = ""                   # source with import/export stripped
        self.default_name = None         # canonical global name for `export default`
        self.has_default = False         # module contains `export default`
        self.default_locals = []         # local names importers bind its default to


def strip_line_comments_for_scan(src):
    """A cheap scrubber so import/export regexes don't fire inside strings or
    comments. Returns a same-length string with comment/string spans blanked."""
    out = []
    i, n = 0, len(src)
    while i < n:
        c = src[i]
        two = src[i:i + 2]
        if two == "//":
            while i < n and src[i] != "\n":
                out.append(" ")
                i += 1
        elif two == "/*":
            while i < n and src[i:i + 2] != "*/":
                out.append(" " if src[i] != "\n" else "\n")
                i += 1
            out.append("  ")
            i += 2
        elif c in "\"'`":
            q = c
            out.append(" ")
            i += 1
            while i < n and src[i] != q:
                if src[i] == "\\" and i + 1 < n:
                    out.append("  ")
                    i += 2
                    continue
                out.append(" " if src[i] != "\n" else "\n")
                i += 1
            if i < n:
                out.append(" ")
                i += 1
        else:
            out.append(c)
            i += 1
    return "".join(out)


IMPORT_RE = re.compile(
    r'^[ \t]*import\s+(?:'
    r'(?P<default>[A-Za-z_$][\w$]*)\s*(?:,\s*)?)?'
    r'(?:\{(?P<named>[^}]*)\})?'
    r'(?:\*\s*as\s*(?P<ns>[A-Za-z_$][\w$]*))?'
    r'\s*(?:from\s*)?["\'](?P<spec>[^"\']+)["\']\s*;?[ \t]*$',
    re.MULTILINE)

# side-effect-only:  import "spec";
IMPORT_SIDE_RE = re.compile(
    r'^[ \t]*import\s*["\'](?P<spec>[^"\']+)["\']\s*;?[ \t]*$', re.MULTILINE)


def resolve(spec, importer_dir, base):
    """Resolve a relative specifier to an absolute .js path."""
    if spec.startswith("."):
        cand = os.path.normpath(os.path.join(importer_dir, spec))
    else:
        cand = os.path.normpath(os.path.join(base, spec))
    for p in (cand, cand + ".js", cand + ".mjs", os.path.join(cand, "index.js")):
        if os.path.isfile(p):
            return os.path.normpath(p)
    return None


class Bundler:
    def __init__(self, base, pkg_map):
        self.base = base
        self.pkg_map = pkg_map
        self.modules = {}            # path -> Module
        self.order = []              # post-order (deps before dependents)
        self.pkg_named = {}          # global -> set(symbol) needed across all modules
        self.warnings = []

    def add(self, path):
        path = os.path.normpath(path)
        if path in self.modules:
            return self.modules[path]
        mod = Module(path)
        self.modules[path] = mod            # register before recursing (cycle-safe)
        src = open(path, encoding="utf-8").read()

        # Match imports on the ORIGINAL text (they're line-start statements, so
        # false hits inside strings/comments don't occur in practice). A match
        # with no default/named/namespace binding is a side-effect import.
        spans = []
        for m in IMPORT_RE.finditer(src):
            dflt, named, ns = m.group("default"), m.group("named"), m.group("ns")
            if not (dflt or named or ns):
                spans.append((m.start(), m.end(), ("side", m.group("spec"))))
            else:
                spans.append((m.start(), m.end(),
                              ("import", m.group("spec"), dflt, named, ns)))
        spans.sort()

        aliases = []   # extra `const local = Canonical;` lines for this module
        cut = []       # (start,end) spans to delete from body
        for span in spans:
            s, e = span[0], span[1]
            info = span[2]
            cut.append((s, e))
            if info[0] == "side":
                spec = info[1]
                if spec in self.pkg_map:
                    continue          # loaders etc: drop
                dep = resolve(spec, os.path.dirname(path), self.base)
                if dep:
                    self.add(dep)
                    mod.deps.append(dep)
                continue
            _, spec, dflt, named, ns = info
            if spec in self.pkg_map:
                glob = self.pkg_map[spec]
                if glob is None:
                    continue
                for sym in _split_named(named):
                    local, orig = sym
                    self.pkg_named.setdefault(glob, {})
                    self.pkg_named[glob][orig] = True
                    if local != orig:
                        aliases.append("const %s = %s.%s;" % (local, glob, orig))
                if dflt:
                    aliases.append("const %s = %s;" % (dflt, glob))
                if ns:
                    aliases.append("const %s = %s;" % (ns, glob))
                continue
            # local module
            dep = resolve(spec, os.path.dirname(path), self.base)
            if dep is None:
                self.warnings.append("unresolved import %r in %s" % (spec, path))
                continue
            depmod = self.add(dep)
            mod.deps.append(dep)
            if dflt and dflt not in depmod.default_locals:
                # record how importers name this module's default; the canonical
                # name is chosen (and the export rewritten) at emission time.
                depmod.default_locals.append(dflt)
            for sym in _split_named(named):
                local, orig = sym
                if local != orig:
                    aliases.append("const %s = %s;" % (local, orig))

        # build the stripped body; note (but don't yet rewrite) a default export
        body = _delete_spans(src, cut)
        if re.search(r'\bexport\s+default\b', body):
            mod.has_default = True
        # delete each `export ` prefix that precedes a declaration keyword
        body = re.sub(r'\bexport\s+(?=(?:class|function|const|let|var|async)\b)',
                      "", body)
        mod.body = ("\n".join(aliases) + "\n" + body) if aliases else body
        return mod

    def _emit_default(self, mod):
        """At emission time, canonicalise `export default EXPR` to the name
        importers use (consistent here), so no per-importer aliases are needed.
        Returns (rewritten_body, trailing_alias_lines)."""
        locals_ = mod.default_locals
        canon = locals_[0] if locals_ else _canon_name(mod.path)
        mod.default_name = canon
        body = re.sub(r'\bexport\s+default\b', "const %s =" % canon, mod.body, count=1)
        # any importer that binds the default under a *different* local name
        extra = ["const %s = %s;" % (loc, canon) for loc in locals_[1:]]
        return body, extra

    def bundle(self, entry):
        self.add(entry)
        seen = set()

        def visit(p):
            if p in seen:
                return
            seen.add(p)
            for d in self.modules[p].deps:
                visit(d)
            self.order.append(p)

        # entry visited last so its top-level `new X()` sees every class
        for p in self.modules:
            if p != os.path.normpath(entry):
                visit(p)
        visit(os.path.normpath(entry))

        parts = []
        # package prelude: const Engine = BABYLON.Engine; ...
        for glob in sorted(self.pkg_named):
            for sym in sorted(self.pkg_named[glob]):
                parts.append("const %s = %s.%s;" % (sym, glob, sym))
        prelude = "\n".join(parts)

        chunks = [prelude] if prelude else []
        for p in self.order:
            mod = self.modules[p]
            rel = os.path.relpath(p, self.base)
            chunks.append("/* ===== module: %s ===== */" % rel)
            if mod.has_default:
                body, extra = self._emit_default(mod)
                chunks.append(body.strip("\n"))
                chunks.extend(extra)
            else:
                chunks.append(mod.body.strip("\n"))
        return "\n".join(chunks)


def _split_named(named):
    """'A, B as C' -> [('A','A'), ('C','B')]  (local, original)."""
    out = []
    if not named:
        return out
    for piece in named.split(","):
        piece = piece.strip()
        if not piece:
            continue
        m = re.match(r'([A-Za-z_$][\w$]*)\s+as\s+([A-Za-z_$][\w$]*)', piece)
        if m:
            out.append((m.group(2), m.group(1)))
        else:
            out.append((piece, piece))
    return out


def _canon_name(path):
    base = os.path.basename(path)
    base = re.sub(r'\.(js|mjs)$', '', base)
    base = re.sub(r'[^0-9A-Za-z_$]', '_', base)
    if base and base[0].isdigit():
        base = "_" + base
    return "__def_" + base


def _delete_spans(src, spans):
    if not spans:
        return src
    spans = sorted(spans)
    out = []
    idx = 0
    for s, e in spans:
        out.append(src[idx:s])
        idx = e
    out.append(src[idx:])
    return "".join(out)


def compile_scss(path):
    """Compile .scss/.sass to CSS via the `sass` CLI; passthrough plain .css."""
    if path.endswith(".css"):
        return open(path, encoding="utf-8").read()
    sass = shutil.which("sass")
    if sass:
        try:
            return subprocess.check_output(
                [sass, "--no-source-map", "--style=expanded", path],
                stderr=subprocess.PIPE).decode("utf-8")
        except subprocess.CalledProcessError as e:
            sys.stderr.write("sass failed: %s\n" % e.stderr.decode("utf-8", "replace"))
    # no sass: emit the raw source (nested rules won't cascade, but the page
    # still parses - ctbrowser's ctcss ignores what it can't match)
    sys.stderr.write("note: `sass` not found; inlining %s raw\n" % path)
    return open(path, encoding="utf-8").read()


LINK_RE = re.compile(
    r'<link\b[^>]*\brel=["\']?stylesheet["\']?[^>]*\bhref=["\']([^"\']+)["\'][^>]*>',
    re.IGNORECASE)
MODULE_SCRIPT_RE = re.compile(
    r'<script\b[^>]*\btype=["\']module["\'][^>]*\bsrc=["\']([^"\']+)["\'][^>]*>\s*</script>',
    re.IGNORECASE)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("entry_html")
    ap.add_argument("-o", "--out", required=True)
    ap.add_argument("--base", help="module resolution root (default: entry dir)")
    ap.add_argument("--map", action="append", default=[],
                    help="pkg=GLOBAL specifier mapping (repeatable)")
    args = ap.parse_args()

    entry_html = os.path.abspath(args.entry_html)
    html_dir = os.path.dirname(entry_html)
    base = os.path.abspath(args.base) if args.base else html_dir
    pkg_map = dict(DEFAULT_PKG_MAP)
    for m in args.map:
        k, _, v = m.partition("=")
        pkg_map[k] = (v or None) if v != "" else None

    html = open(entry_html, encoding="utf-8").read()

    # inline stylesheet links (compile scss)
    def repl_link(m):
        href = m.group(1)
        if href.startswith("http"):
            return m.group(0)     # leave CDN links (ctbrowser ignores/​fetches)
        p = os.path.normpath(os.path.join(html_dir, href))
        if not os.path.isfile(p):
            return m.group(0)
        css = compile_scss(p)
        return "<style>\n%s\n</style>" % css
    html = LINK_RE.sub(repl_link, html)

    # inline the module graph
    mmod = MODULE_SCRIPT_RE.search(html)
    if not mmod:
        sys.stderr.write("no <script type=module src=...> found in %s\n" % entry_html)
        sys.exit(2)
    entry_mod = resolve(mmod.group(1), html_dir, base)
    if entry_mod is None:
        sys.stderr.write("cannot resolve entry module %r\n" % mmod.group(1))
        sys.exit(2)

    b = Bundler(base, pkg_map)
    code = b.bundle(entry_mod)
    for w in b.warnings:
        sys.stderr.write("warning: %s\n" % w)
    html = html[:mmod.start()] + "<script>\n%s\n</script>" % code + html[mmod.end():]

    os.makedirs(os.path.dirname(os.path.abspath(args.out)) or ".", exist_ok=True)
    open(args.out, "w", encoding="utf-8").write(html)
    n = len(b.modules)
    sys.stderr.write("bundled %d module(s) -> %s (%d bytes)\n"
                     % (n, args.out, len(html)))


if __name__ == "__main__":
    main()
