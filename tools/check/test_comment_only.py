"""Checks for comment-only.py, run by pytest and by `comment-only.py --self-test`.

Each case is (name, path, old text, new text, comment_only): check() returns
None for a comment-only edit and a reason otherwise.
"""

from pathlib import Path

from selftest import load

comment_only = load(Path(__file__).with_name("comment-only.py"))

CASES = [
    ("cpp_line_comment", "a.cpp", "int x; // A\n", "int x; // b\n", True),
    ("cpp_code", "a.cpp", "int x; // A\n", "int y; // A\n", False),
    ("cpp_raw_string", "a.cpp", 'auto s = R"(// x)";\n', 'auto s = R"(// y)";\n', False),
    ("cpp_block_to_doc_comment", "a.cpp", "/* a\nb */ int x;\n", "/// a\nint x;\n", True),
    (
        "py_docstring_and_comment",
        "a.py",
        '"""Doc."""\nx = 1  # a\n',
        '"""Better doc."""\nx = 1  # b\n',
        True,
    ),
    ("py_string", "a.py", 'S = """a"""\n', 'S = """b"""\n', False),
    ("test_hash_run_line", "t/a.test", "# RUN: x\n# note\n", "# RUN: y\n# note\n", False),
    ("mlir_comment_lines", "t/a.mlir", "// note\nfunc\n", "// a\n// b\nfunc\n", False),
    ("mlir_comment", "t/a.mlir", "// note\nfunc\n", "// better\nfunc\n", True),
    ("cmake_comment", "CMakeLists.txt", "add(x) # a\n", "add(x) # b\n", True),
    (
        "test_slash_note",
        "t/b.test",
        "// RUN: x\n// note\nfoo\n",
        "// RUN: x\n// better\nfoo\n",
        True,
    ),
    ("test_slash_body", "t/b.test", "// note\nfoo\n", "// note\nbar\n", False),
    ("cpp_comment_and_whitespace", "a.cpp", "namespace { // x\n}\n", "namespace {}\n", True),
    ("cpp_string_whitespace", "a.cpp", 'f("a  b");\n', 'f("a b");\n', False),
    ("cpp_code_whitespace", "a.cpp", "a + b;\n", "a +b;\n", True),
    ("cpp_operator", "a.cpp", "a + b;\n", "a - b;\n", False),
]


def make_test(path, old, new, allowed):
    def test():
        if allowed:
            assert comment_only.check(path, old, new) is None
        else:
            assert comment_only.check(path, old, new)

    return test


for name, *case in CASES:
    globals()[f"test_{name}"] = make_test(*case)
