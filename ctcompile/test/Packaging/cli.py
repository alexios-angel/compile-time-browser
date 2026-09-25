"""Focused CLI compatibility checks for the three standalone ctcompile tools."""

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

compiler, baseline, pageload = map(lambda arg: str(Path(arg).resolve()), sys.argv[1:])
env = dict(os.environ, CTBROWSER_FONTS="font8x8", CTBROWSER_NETWORK="0")


def run(tool, *args, code=0, cwd=None, **kwargs):
    result = subprocess.run(
        [tool, *map(str, args)],
        text=True,
        capture_output=True,
        env=env,
        cwd=cwd,
        timeout=60,
        **kwargs,
    )
    assert result.returncode == code, (tool, args, result)
    return result.stdout


for tool in (compiler, baseline, pageload):
    assert "USAGE:" in run(tool, "--help")
    run(tool, code=2)
    run(tool, "--not-an-option", code=2)

assert run(compiler, "-v") == run(compiler, "--version")
assert "application-directory" in run(compiler, "-h")
assert "bytecode" in run(compiler, "--help")
assert "does not compile JavaScript to native C++" in run(compiler, "--help")
for option in ("--output", "-o", "--entry", "--manifest", "--fonts", "--launcher"):
    run(compiler, option, code=2)

with tempfile.TemporaryDirectory(prefix="ctcompile cli ") as directory:
    root = Path(directory)
    page = root / "index.html"
    page.write_text("<html><body><script>var answer = 42;</script></body></html>")
    css = root / "input.css"
    css.write_text("body { color: red; }")
    script = root / "input.js"
    script.write_text("var answer = 42;")
    output = root / "output.ctapp"
    manifest = root / "manifest.json"

    run(compiler, root, root, code=2)
    run(compiler, root, "--entry=", code=2)
    run(compiler, root, "-o", output, "--output", output, code=2)
    run(compiler, root, "--bundle", "-o", output, "--not-an-option", code=2)
    assert not output.exists()
    run(compiler, root, "--bundle", "--output=", code=2)
    run(compiler, root, "--bundle", "-o", output, "--manifest=", code=2)

    # Reject collisions before touching either output, including filesystem aliases.
    run(
        compiler,
        root,
        "--bundle",
        "-o",
        "new.ctapp",
        "--manifest",
        root / "new.ctapp",
        cwd=root,
        code=2,
    )
    assert not (root / "new.ctapp").exists()
    original_page = page.read_bytes()
    for option in ("--output", "--manifest"):
        run(compiler, root, "--bundle", option, page, code=2, cwd=root)
        assert page.read_bytes() == original_page
    output.write_bytes(b"previous application")
    for alias in (output, root / "subdir" / ".." / output.name):
        (root / "subdir").mkdir(exist_ok=True)
        run(compiler, root, "--bundle", "-o", output, "--manifest", alias, code=2)
        assert output.read_bytes() == b"previous application"
    for link in (os.link, os.symlink):
        alias = root / "output-alias"
        link(output, alias)
        run(compiler, root, "--bundle", "-o", output, "--manifest", alias, code=2)
        assert output.read_bytes() == alias.read_bytes() == b"previous application"
        alias.unlink()
        link(page, alias)
        run(compiler, root, "--bundle", "-o", alias, code=2)
        assert page.read_bytes() == original_page
        alias.unlink()
    launcher = root / "launcher"
    launcher.write_bytes(b"launcher template")
    for option in ("--output", "--manifest"):
        run(compiler, root, "--launcher", launcher, option, launcher, code=2, cwd=root)
        assert launcher.read_bytes() == b"launcher template"

    if sys.platform == "linux":
        import resource
        import signal

        def limit_output():
            signal.signal(signal.SIGXFSZ, signal.SIG_IGN)
            resource.setrlimit(resource.RLIMIT_FSIZE, (64, 64))

        before = set(root.iterdir())
        run(compiler, root, "--bundle", "-o", output, code=1, preexec_fn=limit_output)
        assert output.read_bytes() == b"previous application"
        assert set(root.iterdir()) == before, "failed write left a temporary file"
    output.unlink()
    run(pageload, page, page, code=2)
    run(pageload, root / "missing.html", code=2)
    run(baseline, root / "missing.js", code=1)

    runs = json.loads(run(baseline, f"html={page}", f"css={css}", script))["runs"]
    assert [item["corpus"] for item in runs] == ["html", "css", str(script)]
    assert all(item["stages"] for item in runs)
    assert all(stage["completed"] for item in runs for stage in item["stages"])
    assert "(compiled 0 of 1 scripts)" in run(pageload, page)

    # Exercise positional/hidden application spellings and attached/equals outputs.
    for application, output_arg in (
        ((root,), f"-o{output}"),
        (("--application", root), f"--output={output}"),
    ):
        run(
            compiler,
            "--bundle",
            output_arg,
            "--entry=index.html",
            "--manifest",
            manifest,
            *application,
        )
        record = json.loads(manifest.read_text())
        assert record["entry"] == "index.html"
        assert len(record["scripts"]) == 1
        assert output.stat().st_size > 0
        output.unlink()

    dashed = root / "--not-an-option"
    dashed.mkdir()
    (dashed / "index.html").write_text(page.read_text())
    run(compiler, "--bundle", "-o", output, "--", dashed.name, cwd=root)
    assert output.stat().st_size > 0
    run(compiler, root, "--bundle", "-o", "-", cwd=root)
    assert (root / "-").stat().st_size > 0

print("CLI parsing, bundle output, baseline inputs and page images passed")
