"""The assume-admitted stage: a dry run of the stages after admission.

check-workload.py loads this file with `stage`, `SetupError`, `StepTimeout`
and `strip_refused` in its globals. A workload that names "assume-admitted"
in place of "admission" and "emit-cpp" (bootstrap-dry-run.json) lowers each
script through the native pipeline with -DALLOW_REFUSED=TRUE, deletes every
refused ctjs.func, and hands the C++ of what is left to build-native,
no-vm-gate and script-run (whose page-program probe it also lets through
refusals, by the stage's "dry_run_allow_refused" option). That shows what
those stages will report once admission reaches 100%, while it is still short
of it.

It is never evidence of DONE: the stage also records a MISSING item, so a dry
run's verdict is at best NOT DONE.
"""

from pathlib import Path
import re

EMITC_FUNC = re.compile(r"^  emitc\.func ", re.M)
MAIN = re.compile(r"^(?:int|int32_t) main\(", re.M)


@stage(
    "assume-admitted",
    "DRY RUN, never DONE: the native pipeline lowers the script with refusals allowed, the "
    "refused functions are deleted and ctjs-translate --mlir-to-cpp writes C++ for the rest",
    "This is a dry run; admission (check-workload.py <workload>) is the real gap. Read DETAILS "
    "for what the stages after admission would report today.",
    needs="discover",
)
def assume_admitted(ctx, name):
    translate = ctx.tool("ctcompile/tools/ctjs-translate/ctjs-translate", "ctjs-translate")
    opt = ctx.tool("ctcompile/tools/ctjs-opt/ctjs-opt", "ctjs-opt")
    pipeline = ctx.root / "ctcompile/test/CTNative/Checks/pipeline.cmake"
    ctx.add(
        name,
        "dry-run",
        "MISSING",
        "dry run: refused functions were deleted, so no later stage here is DONE evidence",
    )

    def body(item):
        script = ctx.scripts[item]
        # In the build tree, not the run's scratch directory, so the C++ stays for a
        # look (and a timed compile) after the run.
        base = ctx.build / "check-workload-dry-run" / script["path"].stem
        base.parent.mkdir(parents=True, exist_ok=True)
        module, stripped, cpp = (
            Path(f"{base}{suffix}") for suffix in (".emitc.mlir", ".stripped.emitc.mlir", ".cpp")
        )
        command = ["cmake", f"-DTRANSLATE={translate}", f"-DOPT={opt}"]
        command += [f"-DSOURCE={script['path']}", f"-DOUTPUT={module}", "-DALLOW_REFUSED=TRUE"]
        entries = ",".join(script.get("string_entries", []))
        if entries:
            command.append(f"-DSTRING_ENTRIES={entries}")
        code, output = ctx.run([*command, "-P", pipeline], name, item)
        if code or not module.exists():
            ctx.add(name, item, "FAIL", "pipeline.cmake failed", output.splitlines()[-20:])
            return
        text, removed = strip_refused(module.read_text(errors="replace"))
        stripped.write_text(text)
        native = len(EMITC_FUNC.findall(text))
        code, output = ctx.run([translate, "--mlir-to-cpp", stripped, "-o", cpp], name, item)
        if code or not cpp.exists():
            summary = f"--mlir-to-cpp failed on the {native} native function(s)"
            ctx.add(name, item, "FAIL", summary, output.splitlines()[-20:])
            return
        source = cpp.read_text(errors="replace")
        lines = len(source.splitlines())
        has_main = bool(MAIN.search(source))
        details = []
        if not has_main:
            # The top level was refused. An empty main() lets build-native link the
            # native part and no-vm-gate read it; script-run then reports every
            # global the top level would have printed as missing.
            cpp.write_text(
                source + "\n// Dry run: the top level was refused.\nint main() { return 0; }\n"
            )
            details.append("no main(): the top level was refused; an empty one was appended")
        ctx.artifacts[item] = {"cpp": cpp}
        summary = f"{native} native function(s), {removed} refused deleted, {lines} lines of C++"
        ctx.add(
            name,
            item,
            "PASS",
            summary + ("" if has_main else ", no main()"),
            details,
            {"native": native, "refused": removed, "lines": lines, "main": has_main},
        )

    ctx.each(name, list(ctx.scripts), body)
