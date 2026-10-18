"""Checks for check-cmake-hygiene.py, run by pytest and by the script itself."""

from pathlib import Path
import tempfile

from selftest import load

hygiene = load(Path(__file__).with_name("check-cmake-hygiene.py"))


def example_tree(root):
    """Plant ctcompile/lib/Example/example.cpp under `root`; return its folder."""
    folder = root / "ctcompile/lib/Example"
    folder.mkdir(parents=True)
    (folder / "example.cpp").touch()
    return folder


def test_reports_missing_cmakelists():
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        example_tree(root)
        assert hygiene.check(root) == ["ctcompile/lib/Example: missing CMakeLists.txt"]


def test_reports_parent_relative_path():
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        cmake = example_tree(root) / "CMakeLists.txt"
        cmake.write_text("target_sources(example PRIVATE ../example.cpp)\n")
        assert hygiene.check(root) == [
            "ctcompile/lib/Example/CMakeLists.txt:1: parent-relative path"
        ]


def test_skips_vendor_directories():
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        cmake = example_tree(root) / "CMakeLists.txt"
        cmake.write_text("target_sources(example PRIVATE example.cpp)\n")
        vendor = root / "ctbrowser/vendor/example"
        vendor.mkdir(parents=True)
        (vendor / "external.hpp").touch()
        assert hygiene.check(root) == []
