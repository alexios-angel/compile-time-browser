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
import multiprocessing
import re
import subprocess
import sys
import tokenize
from concurrent.futures import ThreadPoolExecutor
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


# One scan finds every construct that matters, leftmost first, exactly as a
# character-by-character reader would: a raw string (not preceded by an
# identifier character), a string, character or template literal (", ' and `
# literals end at their newline, a template does not), a line comment or a block
# comment. Literals are kept verbatim; comments go, keeping their newlines.
C_CONSTRUCT = re.compile(
    r"""(?P<raw>(?<![A-Za-z0-9_])(?:u8|u|U|L)?R"(?P<delim>[^()\\ ]{0,16})\(.*?(?:\)(?P=delim)"|\Z))"""
    r"""|(?P<literal>"(?:\\.|\\\Z|[^"\\\n])*(?:"|\n|\Z)"""
    r"""|'(?:\\.|\\\Z|[^'\\\n])*(?:'|\n|\Z)"""
    r"""|`(?:\\.|\\\Z|[^`\\])*(?:`|\Z))"""
    r"""|(?P<line>//[^\n]*)"""
    r"""|(?P<block>/\*.*?(?:\*/|\Z))""",
    re.DOTALL,
)


def strip_c(text):
    """`text` with // and /* */ comments removed, string, char and raw string
    literals kept verbatim, and each comment's newlines kept."""

    def keep(match):
        if match["line"] is not None:
            return ""
        if match["block"] is not None:
            return "\n" * match[0].count("\n")
        return match[0]

    return C_CONSTRUCT.sub(keep, text)


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


def read_blobs(repo, specs):
    """The text of each object named in `specs`, from one `git cat-file --batch`
    process (a process per file costs most of the run on a slow filesystem).
    Decoded and newline-normalised as `Path.read_text` would."""
    if not specs:
        return []
    batch = subprocess.run(
        ["git", "-C", repo, "cat-file", "--batch"],
        input="".join(spec + "\n" for spec in specs).encode(),
        capture_output=True,
        check=True,
    ).stdout
    texts, at = [], 0
    for spec in specs:
        end = batch.index(b"\n", at)
        header = batch[at:end].split()
        if header[-1] == b"missing":
            raise SystemExit(f"comment-only: {spec} is missing")
        size = int(header[2])
        body = batch[end + 1 : end + 1 + size]
        at = end + 1 + size + 1
        text = body.decode("utf-8", errors="surrogateescape")
        texts.append(text.replace("\r\n", "\n").replace("\r", "\n"))
    return texts


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
    # --raw gives each file's old blob, so the blobs can be read by hash: resolving
    # `rev:path` walks the tree again for every file, which costs seconds per
    # thousand files. -z keeps unusual paths unquoted.
    raw = git("diff", "--raw", "-z", "--no-abbrev", "--no-renames", base, "--", *paths).split("\0")
    changes, blobs = [], {}
    for meta, path in zip(raw[0::2], raw[1::2]):
        _, _, old_blob, _, flag = meta.split(" ")
        changes.append((flag, path))
        blobs[path] = old_blob
    untracked = git("ls-files", "-z", "--others", "--exclude-standard", "--", *paths)
    changes += [("A", path) for path in untracked.split("\0") if path]
    modified = [path for flag, path in changes if flag == "M"]
    originals = dict(zip(modified, read_blobs(repo, [blobs[path] for path in modified])))
    # Opening a file is slow on a mounted Windows drive, so read them on threads.
    read = lambda path: Path(repo, path).read_text(encoding="utf-8", errors="surrogateescape")
    with ThreadPoolExecutor(16) as pool:
        currents = dict(zip(modified, pool.map(read, modified)))
    # Comparing is pure CPU, so a large change set is spread over processes.
    jobs = [(path, originals[path], currents[path]) for path in modified]
    if len(jobs) > 100:
        with multiprocessing.get_context("fork").Pool() as pool:
            verdicts = dict(zip(modified, pool.starmap(check, jobs, chunksize=16)))
    else:
        verdicts = {path: check(*job) for path, job in zip(modified, jobs)}
    bad = 0
    for flag, path in changes:
        if flag != "M":
            print(f"FAIL {path}: file {flag} (only modifications are allowed)")
            bad += 1
            continue
        why = verdicts[path]
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
