"""Install and relocate the tools component, then use only installed executables."""

import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--angle-dir", default="")
for option in (
    "cmake",
    "build",
    "source",
    "config",
    "bindir",
    "docdir",
    "version",
    "readelf",
    "cxx",
    "nm",
):
    parser.add_argument(f"--{option}", required=True)
for option in ("mlir", "launcher"):
    parser.add_argument(f"--{option}", type=int, choices=(0, 1), required=True)
args = parser.parse_args()
assert not Path(args.bindir).is_absolute(), "relocation requires a relative install bindir"
assert not Path(args.docdir).is_absolute(), "relocation requires a relative install docdir"

env = dict(
    os.environ,
    CTBROWSER_FONTS="font8x8",
    CTBROWSER_NETWORK="0",
    CTBROWSER_TEST_FRAMES="2",
    SDL_VIDEODRIVER="dummy",
    SDL_AUDIODRIVER="dummy",
)
for name in ("LD_LIBRARY_PATH", "LD_PRELOAD", "CTBROWSER_FONT_PATH", "DESTDIR"):
    env.pop(name, None)
env["PATH"] = os.pathsep.join(
    entry
    for entry in os.get_exec_path()
    if entry
    and not any(
        Path(entry).resolve().is_relative_to(Path(root).resolve())
        for root in (args.build, args.source)
    )
)


def run(*command, input=None, code=0):
    result = subprocess.run(
        list(map(str, command)),
        input=input,
        text=True,
        capture_output=True,
        env=env,
        cwd=work,
        timeout=120,
    )
    assert result.returncode == code, (command, result)
    return result.stdout


with tempfile.TemporaryDirectory(prefix="ctcompile install ") as directory:
    work = Path(directory)
    prefix = work / "original prefix"
    run(
        args.cmake,
        "--install",
        args.build,
        "--config",
        args.config,
        "--prefix",
        prefix,
        "--component",
        "ctcompile-tools",
    )
    installed = work / "relocated prefix"
    prefix.rename(installed)
    binaries = installed / args.bindir
    expected = {"ctcompile"}
    if args.mlir:
        expected.update(("ctjs-opt", "ctjs-translate"))
    if args.launcher:
        expected.add("ctrun")
    assert {path.name for path in binaries.iterdir()} == expected
    for name in ("LICENSE", "NOTICE"):
        assert (installed / args.docdir / name).stat().st_size > 0
    for name in expected:
        executable = binaries / name
        assert executable.is_file() and not executable.is_symlink()
        assert os.access(executable, os.X_OK)
        dynamic = run(args.readelf, "-d", executable)
        # ANGLE is an external host dependency, fetched beside the checkout.
        # A tools-only install retains that location just like the Brew prefix.
        if args.angle_dir:
            dynamic = dynamic.replace(str(Path(args.angle_dir).resolve()), "<external ANGLE>")
        assert all(
            str(Path(root).resolve()) not in dynamic for root in (args.build, args.source, prefix)
        ), dynamic

    compiler = binaries / "ctcompile"
    assert run(compiler, "--version").startswith(f"ctcompile {args.version} ")
    assert "application-directory" in run(compiler, "--help")
    app = work / "input application"
    app.mkdir()
    (app / "index.html").write_text("<html><body><script>var answer = 42;</script></body></html>")
    fonts = work / "empty fonts"
    fonts.mkdir()
    bundle = work / "application.ctapp"
    manifest = work / "manifest.json"
    run(compiler, app, "--bundle", "--fonts", fonts, "-o", bundle, "--manifest", manifest)
    record = json.loads(manifest.read_text())
    assert record["ctcompile"] == args.version and len(record["scripts"]) == 1
    assert bundle.stat().st_size > 0
    if args.launcher:
        executable = work / "application"
        run(compiler, app, "--fonts", fonts, "-o", executable)
        info = json.loads(run(executable, "--info"))
        assert info["entry"] == "index.html" and len(info["scripts"]) == 1
        app.rename(work / "moved source")
        run(executable)
        run(binaries / "ctrun", bundle)
        app.mkdir()
    (app / "index.html").write_text('<script type="module">var answer = 42;</script>')
    original_bundle = bundle.read_bytes()
    run(compiler, app, "--bundle", "--fonts", fonts, "-o", bundle, code=1)
    assert bundle.read_bytes() == original_bundle

    if args.mlir:
        translate = binaries / "ctjs-translate"
        opt = binaries / "ctjs-opt"
        source = work / "answer.js"
        source.write_text("var answer = 6 * 7;")
        raw = run(translate, "--ctbrowser-js-to-ctjs", source)
        assert "ctjs.func" in raw and "ctjs.skipped" not in raw
        pipeline = (
            "--pass-pipeline=builtin.module(ctjs-resolve-globals,ctjs-lift-to-scf,"
            "ctnative-lower-to-emitc,emitc.func(canonicalize,convert-scf-to-emitc,"
            "convert-arith-to-emitc,canonicalize,ctnative-prune-dead-stores,canonicalize))"
        )
        native = run(opt, pipeline, input=raw)
        assert "emitc.func" in native and "ctnative.not_native" not in native
        assert "ctjs.func" not in native
        cpp = run(translate, "--mlir-to-cpp", input=native)
        assert cpp.strip() and "answer" in cpp
        forbidden = ("ctbrowser::script", "ctbrowser::aot", "ct_aot_", "ctjs::")
        assert all(name not in cpp for name in forbidden)
        generated = work / "answer.cpp"
        generated.write_text(cpp)
        executable = work / "answer"
        run(
            args.cxx,
            "-std=c++23",
            "-O2",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-pedantic",
            "-Wconversion",
            "-ffp-contract=off",
            "-I" + str(Path(args.source) / "ctcompile/include"),
            "-I" + str(Path(args.source) / "ctbrowser/include"),
            generated,
            "-o",
            executable,
        )
        assert run(executable).strip() == "answer=42"
        symbols = run(args.nm, "-C", executable)
        assert all(name not in symbols for name in forbidden)
        source.write_text('var answer = eval("40+2");')
        raw = run(translate, "--ctbrowser-js-to-ctjs", source)
        refused = run(opt, pipeline, input=raw)
        assert "ctnative.not_native" in refused
        run(translate, "--mlir-to-cpp", input=refused, code=1)
        source.write_text("function broken(")
        run(translate, "--ctbrowser-js-to-ctjs", source, code=1)

print(f"Relocated installed tools passed (MLIR={args.mlir}, launcher={args.launcher})")
