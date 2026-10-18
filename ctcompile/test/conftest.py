"""Run the ctcompile Python test drivers under pytest, beside lit.

A driver keeps its `main(argv=None)` and its lit `.test`; converting it adds
a `test_<case>(lit)` function to the driver itself, one per lit case the
driver serves, that calls `main` with that case's RUN line arguments, copied
verbatim:

    def test_reads(lit):
        main(lit.args("--translate ctjs-translate --opt ctjs-opt --work %t"))

The `lit` fixture expands those arguments the way lit.cfg.py does, from the
same generated lit.site.cfg.py the build writes, so both runners use the same
tools, compilers, Node and interpreter reference. The build tree is
`--ctcompile-build` or $CTCOMPILE_BUILD, by default `build/` at the repository
root.

Selection matches lit's names. A test's lit name is its driver's folder plus
its case with `_` read as `-` (lit case names use hyphens, modules
underscores): `test_reads` in Provider/reads.py is `ctcompile ::
CTNative/HostContract/Provider/reads.test`, and `test_button_probe` is
`.../button-probe.test`. A test parametrized over cases uses the parameter id
as the case instead. The suffix is `.mlir` when that file exists (a few
cases are .mlir files holding only a RUN line and comments), `.test` otherwise.
`--lit-filter REGEX` keeps only the tests whose name it matches, so the
`--filter` that tools/check/round-checks.py prints selects the same cases in
both runners. Each test may run for an hour, as each lit case may
(lit.cfg.py); pytest-timeout enforces it, and a run without that plugin stops
rather than running without the limit.
"""

import importlib
import os
from pathlib import Path
import re
import shlex
import shutil
import sys
import types
from unittest import mock

import pytest

HERE = Path(__file__).resolve().parent
TIMEOUT = 3600  # lit.cfg.py's maxIndividualTestTime

# Drivers import each other as packages rooted here (`from CTNative.harness
# import run`), in this process and in the Python subprocesses they start.
sys.path.insert(0, str(HERE))
os.environ["PYTHONPATH"] = os.pathsep.join(filter(None, [str(HERE), os.environ.get("PYTHONPATH")]))


def pytest_addoption(parser):
    group = parser.getgroup("ctcompile")
    group.addoption(
        "--ctcompile-build",
        type=Path,
        default=os.environ.get("CTCOMPILE_BUILD", HERE.parents[1] / "build"),
        help="the CMake build tree whose ctcompile/test/lit.site.cfg.py to read",
    )
    group.addoption("--lit-filter", help="keep tests whose lit name matches this regex (re.search)")


def pytest_configure(config):
    if not config.pluginmanager.hasplugin("timeout"):
        raise pytest.UsageError(
            "the ctcompile drivers need pytest-timeout for their one-hour limit "
            "(uv run --with pytest --with pytest-timeout pytest ...)"
        )


class Driver(pytest.Module):
    """A driver module, imported under its package name relative to this
    directory, which is also how sibling drivers import it: one module
    object, and no clash between same-named drivers in different folders."""

    def _getobj(self):
        return importlib.import_module(".".join(self.path.relative_to(HERE).with_suffix("").parts))


def is_driver(path):
    """A converted driver: one with a test function. A text check, so an
    unconverted driver is never imported."""
    return (
        path.suffix == ".py"
        and path.name != "conftest.py"
        and re.search(r"^def test_", path.read_text(errors="replace"), re.M) is not None
    )


def pytest_collect_file(file_path, parent):
    # A file named on the command line is pytest's own to collect; it asks
    # pytest_pycollect_makemodule below for the module, so it is not collected twice.
    if is_driver(file_path) and not parent.session.isinitpath(file_path):
        return Driver.from_parent(parent, path=file_path)
    return None


def pytest_pycollect_makemodule(module_path, parent):
    if is_driver(module_path):
        return Driver.from_parent(parent, path=module_path)
    return None


def lit_name(item):
    callspec = getattr(item, "callspec", None)
    case = callspec.id if callspec else item.originalname.removeprefix("test_")
    stem = item.path.parent / case.replace("_", "-")
    # A few cases are .mlir files holding only a RUN line and comments; .test
    # otherwise, which also names the case once its .test file has been deleted.
    suffix = ".mlir" if stem.with_suffix(".mlir").is_file() else ".test"
    return f"ctcompile :: {stem.relative_to(HERE).as_posix()}{suffix}"


def pytest_collection_modifyitems(config, items):
    pattern = config.getoption("lit_filter")
    if pattern:
        kept = [i for i in items if re.search(pattern, lit_name(i))]
        config.hook.pytest_deselected(items=[i for i in items if i not in kept])
        items[:] = kept
    if config.getoption("timeout", None) is None:
        for item in items:
            if item.get_closest_marker("timeout") is None:
                item.add_marker(pytest.mark.timeout(TIMEOUT))


def read_site(path):
    """The `config` attributes the generated lit.site.cfg.py sets, read by
    running it with lit stubbed out (its last lines hand over to lit.cfg.py)."""
    if not path.is_file():
        raise pytest.UsageError(
            f"{path} does not exist: configure the build, or pass --ctcompile-build"
        )
    config = types.SimpleNamespace()
    lit_config = types.SimpleNamespace(substitute=lambda s: s, load_config=lambda *a: None)
    lit = types.ModuleType("lit")
    lit.llvm = types.ModuleType("lit.llvm")
    lit.llvm.initialize = lambda *a: None
    with mock.patch.dict(sys.modules, {"lit": lit, "lit.llvm": lit.llvm}):
        exec(
            compile(path.read_text(), str(path), "exec"),
            {"config": config, "lit_config": lit_config},
        )
    return config


def first_found(*candidates):
    return next((shutil.which(c) for c in candidates if shutil.which(c)), candidates[-1])


@pytest.fixture(scope="session")
def lit_site(pytestconfig):
    """lit.cfg.py's substitutions and tool paths for the configured build tree."""
    c = read_site(pytestconfig.getoption("ctcompile_build") / "ctcompile/test/lit.site.cfg.py")
    exec_root = os.path.join(c.ctcompile_obj_root, "test")
    checks = HERE / "CTNative" / "Checks"
    runtime = f"-I {c.ctcompile_src_root}/include -I {c.ctbrowser_include}"
    strict = "-Wall -Wextra -Werror -Wconversion -pedantic"
    substitutions = {
        "%compilation_unit": f"python3 {checks}/compilation-unit.py --translate ctjs-translate"
        f" --cxx {c.host_cxx} --nm {c.nm} --reference {c.native_reference}"
        " --vm-linked ctcompile-test-type-oracle",
        "%compile_clean": f"python3 {checks}/compile-clean.py --translate ctjs-translate"
        f" --compilers {c.clean_compilers}",
        "%print_deduced": f"python3 {checks}/print-deduced.py --translate ctjs-translate"
        f" --cxx {c.host_cxx}",
        "%dom_driver": "--translate ctjs-translate --opt ctjs-opt --clang %dom_clang --build %build"
        " --include %{monorepo}/ctbrowser/include --work %t --nm %nm",
        "%cxx_exe": f"{c.host_cxx} -std=c++23 {runtime}",
        "%cxx": f"{c.host_cxx} -std=c++23 -fsyntax-only {runtime}",
        "%gxx": f"{first_found('g++-13', 'g++')} -std=c++23 {runtime} {strict}",
        "%clangxx": f"{first_found('clang++-18', 'clang++')} -std=c++23 {runtime} {strict}",
        "%node": c.node or shutil.which("node") or "node",
        "%native_reference": c.native_reference,
        "%{obj}": exec_root,
        "%{monorepo}": c.monorepo_root,
        "%host_cxx": c.host_cxx,
        "%nm": c.nm,
        "%cmake": c.cmake,
        "%dom_clang": c.dom_clang,
        "%build": c.build_root,
        "%python": sys.executable,
    }
    test_executables = (
        "type-oracle type-claims escape-claims escape-oracle-aot escape-oracle-aot-return "
        "launcher-vm launcher-aot launcher-page-vm launcher-page-aot native-vm-linked "
        "native-values-vm-linked app_bundle"
    ).split()
    tool_dirs = [
        (("ctjs-opt", "ctjs-translate", "ctcompile"), c.ctcompile_tools_dirs),
        (tuple("ctcompile-test-" + n for n in test_executables), [exec_root]),
        (("ctrun", "ctbrowse"), [c.ctbrowser_tools_dir]),
        (("mlir-translate", "not", "split-file", "FileCheck", "count"), [c.llvm_tools_dir]),
    ]
    tools = {}
    for names, dirs in tool_dirs:
        for name in names:
            # Unresolved stays the bare name, as lit's unresolved="ignore" does,
            # so a missing executable fails its own test by name.
            tools[name] = shutil.which(name, path=os.pathsep.join(dirs)) or name
    return substitutions, tools


class Lit:
    """One test's view of the substitutions: %t is its own temporary
    directory, %S and %s its driver's folder and lit case."""

    def __init__(self, site, path, tmp):
        substitutions, self.tools = site
        self.substitutions = dict(
            substitutions,
            **{"%t": str(tmp), "%S": str(path.parent), "%s": str(path.with_suffix(".test"))},
        )

    def expand(self, text):
        """`text` with every %-substitution expanded; repeated, because some
        expand to text holding others (%dom_driver holds %build and %t)."""
        keys = sorted(self.substitutions, key=len, reverse=True)  # %cxx_exe before %cxx
        while True:
            expanded = text
            for key in keys:
                expanded = expanded.replace(key, self.substitutions[key])
            if expanded == text:
                return text
            text = expanded

    def args(self, text):
        """A RUN line's argument text as an argv list, tools as their paths."""
        return [self.tools.get(word, word) for word in shlex.split(self.expand(text))]


@pytest.fixture
def lit(lit_site, request, tmp_path):
    return Lit(lit_site, request.path, tmp_path)
