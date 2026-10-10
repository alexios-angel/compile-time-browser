#!/usr/bin/env python3
"""Check that every change since a base commit touches only comments.

usage: tools/check/comment-only.py [REPO] BASE [PATH...]
       tools/check/comment-only.py --self-test

REPO defaults to the current directory and BASE is any revision. For each file
that changed since BASE (limited to PATHs when given), the file's code with its
comments removed must be the same before and after:

- C, C++, JavaScript, MLIR and TableGen are compared token by token, so layout
  may change but string literals, raw strings included, must not.
- Python is compared token by token without comments or docstrings (a string
  that is a whole statement).
- CMake, shell and lit `.test` files lose their `#` comments, or `//` comments
  for lit tests that use them.

Lit and fixture inputs (.mlir, .test, .ll, and .js/.html/.css under a test
directory) are stricter, because tests read their line numbers: the line count
must not change, no lit directive line (RUN:, CHECK, expected-, //---,
REQUIRES, ...) may change, and only the comment text of a line may differ.

Added, deleted and renamed files fail. Exit status 0 means the change is
comment-only. Built for the 2026-10-10 repository-wide comment rewrite.
"""

import io
import re
import subprocess
import sys
import tokenize
from pathlib import Path

C_LIKE = {
    ".c",
    ".cc",
    ".cpp",
    ".h",
    ".hpp",
    ".inc",
    ".def",
    ".td",
    ".js",
    ".mjs",
    ".ts",
    ".mlir",
    ".ll",
    ".css",
}
HASH = {".cmake", ".sh", ".test", ".txt", ".yml", ".yaml", ".toml", ".cfg", ".in"}
DIRECTIVE = re.compile(
    r"RUN:|CHECK|expected-|//---|REQUIRES|UNSUPPORTED|XFAIL|ALLOW_RETRIES|"
    r"END\.|DEFINE:|REDEFINE:|NOLINT|clang-format|IWYU|@LINE"
)


def strip_c(text):
    """`text` with // and /* */ comments removed, string, char and raw string
    literals kept verbatim, and each comment's newlines kept."""
    out, i, n = [], 0, len(text)
    while i < n:
        c = text[i]
        raw = re.match(r'(?:u8|u|U|L)?R"([^()\\ ]{0,16})\(', text[i : i + 24])
        if raw and (i == 0 or not (text[i - 1].isalnum() or text[i - 1] == "_")):
            end = text.find(")" + raw[1] + '"', i)
            end = n if end < 0 else end + len(raw[1]) + 2
            out.append(text[i:end])
            i = end
        elif c in "\"'`":
            j = i + 1
            while j < n and text[j] != c and not (text[j] == "\n" and c != "`"):
                j += 2 if text[j] == "\\" else 1
            out.append(text[i : j + 1])
            i = j + 1
        elif text.startswith("//", i):
            j = text.find("\n", i)
            i = n if j < 0 else j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append("\n" * text.count("\n", i, j))
            i = j
        else:
            out.append(c)
            i += 1
    return "".join(out)


def strip_hash(text):
    lines = []
    for line in text.split("\n"):
        quote = None
        for k, ch in enumerate(line):
            if quote:
                quote = None if ch == quote else quote
            elif ch in "\"'":
                quote = ch
            elif ch == "#" and (k == 0 or line[k - 1] in " \t;"):
                line = line[:k]
                break
        lines.append(line)
    return "\n".join(lines)


def strip_python(text):
    """Tokens other than comments and docstrings (a string that is a whole
    statement), as one string; layout-insensitive."""
    keep, previous = [], tokenize.NEWLINE
    toks = list(tokenize.generate_tokens(io.StringIO(text).readline))
    for k, tok in enumerate(toks):
        if tok.type in (tokenize.COMMENT, tokenize.NL):
            continue
        if tok.type == tokenize.STRING and previous in (
            tokenize.NEWLINE,
            tokenize.INDENT,
            tokenize.DEDENT,
            tokenize.ENCODING,
        ):
            nxt = next(
                (t for t in toks[k + 1 :] if t.type not in (tokenize.COMMENT, tokenize.NL)), None
            )
            if nxt is None or nxt.type in (tokenize.NEWLINE, tokenize.ENDMARKER):
                previous = tok.type
                continue
        keep.append(tok.string)
        previous = tok.type
    return " ".join(keep)


def normalise(text):
    return re.sub(r"\s+", " ", text).strip()


C_TOKEN = re.compile(
    r'(?:u8|u|U|L)?R"([^()\\ ]{0,16})\(.*?\)\1"|"(?:\\.|[^"\\\n])*"|'
    r"'(?:\\.|[^'\\\n])*'|\w+|\S",
    re.DOTALL,
)


def tokens_c(text):
    """C-like code as tokens: literals exact, layout ignored."""
    return [m.group(0) for m in C_TOKEN.finditer(text)]


def kind(path):
    name = path.rsplit("/", 1)[-1]
    ext = "." + name.rsplit(".", 1)[-1] if "." in name else ""
    if ext == ".py":
        return "py"
    if name == "CMakeLists.txt" or ext in HASH:
        return "hash"
    if ext in C_LIKE:
        return "c"
    return None


def fixture(path):
    ext = "." + path.rsplit(".", 1)[-1]
    return ext in {".mlir", ".test", ".ll"} or (
        ext in {".js", ".html", ".css"}
        and re.search(r"(^|/)(test|tests|unittests|Inputs|Fixtures)/", path)
    )


def check(path, old, new):
    k = kind(path)
    if k is None:
        return "not a source type this checker knows; edit by hand"
    strip = {"c": strip_c, "hash": strip_hash, "py": strip_python}[k]
    if path.endswith(".test"):
        # A lit .test file is commented with # or with //, depending on its tool.
        same = lambda f: normalise(f(old)) == normalise(f(new))
        strip = strip_c if not same(strip_hash) and same(strip_c) else strip_hash
    compare = tokens_c if strip is strip_c else normalise
    try:
        if compare(strip(old)) != compare(strip(new)):
            return "code changed, not only comments"
    except (tokenize.TokenError, IndentationError, SyntaxError) as error:
        return f"cannot tokenize: {error}"
    if fixture(path) or path.endswith(".test"):
        a, b = old.split("\n"), new.split("\n")
        if len(a) != len(b):
            return f"test input changed line count {len(a)} -> {len(b)}"
        for number, (x, y) in enumerate(zip(a, b), 1):
            if x != y and (DIRECTIVE.search(x) or DIRECTIVE.search(y)):
                return f"line {number}: a lit/tool directive changed"
            if x != y and normalise(strip(x)) != normalise(strip(y)):
                return f"line {number}: code on the line changed"
    return None


def main():
    arguments = sys.argv[1:]
    if len(arguments) == 1 or (arguments and not Path(arguments[0]).is_dir()):
        arguments = [".", *arguments]
    if len(arguments) < 2:
        sys.exit(__doc__)
    repo, base, *paths = arguments
    git = lambda *a: (
        subprocess.run(["git", "-C", repo, *a], capture_output=True, text=True, check=True).stdout
    )
    status = git("diff", "--name-status", "--no-renames", base, "--", *paths)
    status += "".join(
        f"A\t{p}\n" for p in git("ls-files", "--others", "--exclude-standard", "--", *paths).split()
    )
    bad = 0
    for line in status.splitlines():
        flag, path = line.split("\t", 1)
        if flag != "M":
            print(f"FAIL {path}: file {flag} (only modifications are allowed)")
            bad += 1
            continue
        old = git("show", f"{base}:{path}")
        new = Path(repo, path).read_text(encoding="utf-8", errors="surrogateescape")
        why = check(path, old, new)
        if why:
            print(f"FAIL {path}: {why}")
            bad += 1
    print(f"comment-only: {bad} failing file(s)")
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    if sys.argv[1:2] == ["--self-test"]:
        assert check("a.cpp", "int x; // A\n", "int x; // b\n") is None
        assert check("a.cpp", "int x; // A\n", "int y; // A\n")
        assert check("a.cpp", 'auto s = R"(// x)";\n', 'auto s = R"(// y)";\n')
        assert check("a.cpp", "/* a\nb */ int x;\n", "/// a\nint x;\n") is None
        assert check("a.py", '"""Doc."""\nx = 1  # a\n', '"""Better doc."""\nx = 1  # b\n') is None
        assert check("a.py", 'S = """a"""\n', 'S = """b"""\n')
        assert check("t/a.test", "# RUN: x\n# note\n", "# RUN: y\n# note\n")
        assert check("t/a.mlir", "// note\nfunc\n", "// a\n// b\nfunc\n")
        assert check("t/a.mlir", "// note\nfunc\n", "// better\nfunc\n") is None
        assert check("CMakeLists.txt", "add(x) # a\n", "add(x) # b\n") is None
        assert check("t/b.test", "// RUN: x\n// note\nfoo\n", "// RUN: x\n// better\nfoo\n") is None
        assert check("t/b.test", "// note\nfoo\n", "// note\nbar\n")
        assert check("a.cpp", "namespace { // x\n}\n", "namespace {}\n") is None
        assert check("a.cpp", 'f("a  b");\n', 'f("a b");\n')
        assert check("a.cpp", "a + b;\n", "a +b;\n") is None
        assert check("a.cpp", "a + b;\n", "a - b;\n")
        print("self-test ok")
    else:
        main()
