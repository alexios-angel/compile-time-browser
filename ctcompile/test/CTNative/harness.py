"""What the CTNative python drivers share: a checked subprocess run, the two
compilers (the same pair Target/Cpp/harness.py finds) and their flags, the
usual command line, the JS import and lift, native build-and-run, work-limit
bisection, the Node observer of named globals, and running independent cases
on every CPU (run_parallel) or only those CTCOMPILE_FIXTURES selects
(selected_fixtures).

lit puts ctcompile/test on PYTHONPATH (lit.cfg.py), which is also what lets a
driver import a sibling from another directory as, say,
`from CTNative.HostContract import contract`. A hand run needs the same
PYTHONPATH. Node and the interpreter reference are not discovered here: every
RUN line passes `--node %node --reference %native_reference`, both configured
by CMake (lit.site.cfg.py.in).
"""

import argparse
from concurrent.futures import ThreadPoolExecutor
import fnmatch
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

from Target.Cpp.harness import (
    CORE_INCLUDE,
    FLAGS,
    RUNTIME_INCLUDE,
    find_compilers,
    slotted_run,
)  # noqa: F401  (re-exported)

# What follows ctnative-lower-to-emitc in every native pipeline.
CLEANUP = (
    "emitc.func(canonicalize,convert-scf-to-emitc,convert-arith-to-emitc,"
    "canonicalize,ctnative-prune-dead-stores,canonicalize)"
)
# A refused function's reason, and a source function's symbol.
REFUSAL = re.compile(r'ctnative\.not_native = "((?:[^"\\]|\\.)*)"')
FUNCTION = re.compile(r"\bctjs\.func (?:private )?@([^ (]+)\(")
# An interpreter or boxed-AOT symbol, in nm -C output or generated C++.
VM_SYMBOL = re.compile(r"ctbrowser::(?:script|aot)::|\bct_aot_")

RUNTIME_HEADER = (
    Path(__file__).resolve().parents[2] / "include/ctcompile/CTNative/Runtime/ctnative.hpp"
)
RUNTIME_INCLUDE_LINE = '#include "ctcompile/CTNative/Runtime/ctnative.hpp"'


def inline_runtime(cpp):
    """The emitted C++ with the runtime header's text in place of its include.

    A driver that instruments a helper (the lifetime harnesses rewrite make_map
    to record a weak_ptr of every Map) edits text, and the text has to be in the
    file it edits. #pragma once goes, since the text is now the main file.
    """
    if cpp.count(RUNTIME_INCLUDE_LINE) != 1:
        raise RuntimeError("the emitted C++ does not include the runtime header exactly once")
    return cpp.replace(
        RUNTIME_INCLUDE_LINE, RUNTIME_HEADER.read_text().replace("#pragma once\n", "")
    )


def forge_module(text, attributes):
    """`text` with `attributes` first in its module's attribute dictionary: the
    report a pass would have written, forged, which a proof must not trust."""
    text, count = re.subn(
        r"\bmodule( attributes)? \{",
        lambda match: "module attributes {" + attributes + (", " if match[1] else "} {"),
        text,
        count=1,
    )
    if count != 1:
        raise RuntimeError("the forged attributes found no module")
    return text


def run(command, *, success=True, environment=None, input_text=None):
    """Run `command` in a job slot (Target/Cpp/harness.py) with a 120 s timeout
    and return the result; raise unless it exited 0 (or, with `success=False`,
    unless it failed)."""
    result = slotted_run(
        command, capture_output=True, text=True, timeout=120, env=environment, input=input_text
    )
    if (result.returncode == 0) != success:
        raise RuntimeError(
            f"unexpected tool status {result.returncode}: {command!r}\n{result.stdout}{result.stderr}"
        )
    return result


# The CMake script that runs the native lowering pipeline, as the build does.
PIPELINE = Path(__file__).resolve().parent / "Checks/pipeline.cmake"


def native_pipeline(args, source, output, **defines):
    """Lower `source` into `output` with the build's own script, Checks/pipeline.cmake."""
    run(
        [
            "cmake",
            f"-DTRANSLATE={args.translate}",
            f"-DOPT={args.opt}",
            f"-DSOURCE={source}",
            f"-DOUTPUT={output}",
            "-DOPTIMIZE=OFF",
            *(f"-D{name}={value}" for name, value in defines.items()),
            "-P",
            str(PIPELINE),
        ]
    )


def without_intrinsic(manifest, name):
    """`manifest` with the intrinsic `name` withheld from its initial_intrinsics."""
    kept = [item for item in manifest["initial_intrinsics"] if item != name]
    return dict(manifest, initial_intrinsics=kept)


def native_args(description, *paths, parser=None, argv=None):
    """The usual driver command line, every option required: the four tools,
    then --work (created) and `paths` as Paths; `parser` may bring more options.
    `argv` is the command line, sys.argv by default."""
    parser = parser or argparse.ArgumentParser(description=description)
    for name in ("translate", "opt", "node", "reference"):
        parser.add_argument("--" + name, required=True)
    for name in ("work", *paths):
        parser.add_argument("--" + name, type=Path, required=True)
    args = parser.parse_args(argv)
    args.work.mkdir(parents=True, exist_ok=True)
    return args


def native_tools(reference):
    """Both compilers, and an nm shown to find the interpreter in `reference`
    before it is trusted to find none in a native binary."""
    nm = shutil.which("nm") or shutil.which("llvm-nm")
    if not nm or not VM_SYMBOL.search(run([nm, "-C", str(reference)]).stdout):
        raise RuntimeError("the VM symbol check cannot see the interpreter in its positive control")
    return find_compilers(), nm


def threshold(passes, low=0, high=100000):
    """The least work limit in (low, high] that `passes`, by bisection: `passes`
    is monotone, false at `low` and true at `high`."""
    while high - low > 1:
        middle = (low + high) // 2
        if passes(middle):
            high = middle
        else:
            low = middle
    return high


def prepare_js(args, js, raw, ir, *options, complete=False):
    """Import `js` into `raw`, then resolve globals and lift structure into `ir`,
    both tools given `options`; `complete` refuses an import that skipped a
    function. Returns the raw import's text."""
    imported = run([args.translate, "--ctbrowser-js-to-ctjs", str(js), *options, "-o", str(raw)])
    text = raw.read_text()
    if complete and ("ctjs.skipped" in text or "is not compiled:" in imported.stderr):
        raise RuntimeError(f"{js}: the importer skipped a source function\n{imported.stderr}")
    lift = ["--ctjs-resolve-globals", "--ctjs-lift-to-scf", *options]
    run([args.opt, str(raw), *lift, "-o", str(ir)])
    return text


# Node's side of the output convention (CTNative/Checks/compilation-unit.py) for
# the globals named by argv[2], a JSON list, in that order.
CONSTANT_GLOBAL_NODE = r"""const fs = require('node:fs');
const vm = require('node:vm');
const context = vm.createContext({});
vm.runInContext(fs.readFileSync(process.argv[1], 'utf8'), context);
for (const name of JSON.parse(process.argv[2])) {
    const value = vm.runInContext(name, context);
    let text;
    if (typeof value === 'number') {
        text = Object.is(value, -0) ? '-0' : String(value);
    } else if (typeof value === 'string') {
        text = '"' + encodeURIComponent(value).replace(/[!'()*]/g,
            c => '%' + c.charCodeAt(0).toString(16).toUpperCase()) + '"';
    } else if (typeof value === 'boolean' || value === undefined || value === null) {
        text = String(value);
    } else {
        throw new Error(name + ': unexpected scalar tag');
    }
    process.stdout.write(name + '=' + text + '\n');
}
"""


def node_globals(node, js, names):
    """Node's `<name>=<value>` lines for the named globals of `js`, checked to run."""
    return run([node, "-e", CONSTANT_GLOBAL_NODE, str(js), json.dumps(list(names))])


# Flags for lifetime checks: AddressSanitizer and UBSan, with leak and
# use-after-return detection enabled by SANITIZER_ENVIRONMENT.
SANITIZE = [
    "-O1",
    "-g",
    "-fno-omit-frame-pointer",
    "-fsanitize=address,undefined",
    "-fsanitize-address-use-after-scope",
]
SANITIZER_ENVIRONMENT = {
    "ASAN_OPTIONS": "detect_stack_use_after_return=1:detect_leaks=1",
    "UBSAN_OPTIONS": "halt_on_error=1:print_stacktrace=1",
}


def build_and_run(
    compiler,
    source,
    binary,
    *,
    flags=FLAGS,
    libraries=(),
    nm=None,
    vm=VM_SYMBOL,
    sanitize=False,
    clean=False,
):
    """Compile `source` into `binary` (plus SANITIZE when asked; with `clean` the
    compiler must say nothing), refuse a binary in whose `nm` symbols `vm` is
    found, and run it: the checked result, under SANITIZER_ENVIRONMENT when
    sanitized."""
    extra = SANITIZE if sanitize else []
    built = run([compiler, *flags, *extra, str(source), *libraries, "-o", str(binary)])
    if clean and (built.stdout or built.stderr):
        raise RuntimeError(f"{source} did not compile silently\n{built.stdout}{built.stderr}")
    if nm and vm.search(run([nm, "-C", str(binary)]).stdout):
        raise RuntimeError(f"{binary} links what it must not ({vm.pattern})")
    environment = dict(os.environ, **SANITIZER_ENVIRONMENT) if sanitize else None
    return run([str(binary)], environment=environment)


def run_sanitized(compiler, source, binary, flags):
    """Build `source` with `flags` plus SANITIZE, run it, and return the result unchecked."""
    run([compiler, *flags, *SANITIZE, str(source), "-o", str(binary)])
    return subprocess.run(
        [str(binary)],
        capture_output=True,
        text=True,
        timeout=60,
        env=dict(os.environ, **SANITIZER_ENVIRONMENT),
    )


def run_parallel(items, function, workers=None):
    """`function` over `items` on `workers` threads (every CPU by default), the
    results in the order of `items`. Threads, because a case's time is spent in
    the tools it runs as subprocesses. Every case runs even when one fails, so
    one run reports them all; then the failures are raised together, in the
    order of `items`. A case must write only files named after itself and
    return what it would print, so the output does not depend on scheduling."""
    items = list(items)
    with ThreadPoolExecutor(workers or os.cpu_count() or 1) as pool:
        futures = [pool.submit(function, item) for item in items]
    results, failures = [], []
    for item, future in zip(items, futures):
        try:
            results.append(future.result())
        except Exception as error:  # noqa: BLE001 (reported below, with the case)
            failures.append(f"{item}: {type(error).__name__}: {error}")
    if failures:
        raise RuntimeError(
            f"{len(failures)} of {len(items)} case(s) failed:\n" + "\n".join(failures)
        )
    return results


FIXTURE_FILTER = "CTCOMPILE_FIXTURES"


def selected_fixtures(names):
    """The fixture names a run checks: all of `names`, or, when
    CTCOMPILE_FIXTURES holds comma-separated globs (fnmatch, case-sensitive),
    those matching one. A filter that matches nothing is an error, so a
    misspelt filter never passes by checking nothing; a filtered run says so
    in its output. lit passes the variable through (lit.cfg.py)."""
    names = list(names)
    patterns = [p.strip() for p in os.environ.get(FIXTURE_FILTER, "").split(",") if p.strip()]
    if not patterns:
        return names
    chosen = [name for name in names if any(fnmatch.fnmatchcase(name, p) for p in patterns)]
    if not chosen:
        raise RuntimeError(f"{FIXTURE_FILTER}={os.environ[FIXTURE_FILTER]!r} matches no fixture")
    print(
        f"{len(chosen)} of {len(names)} fixtures selected "
        f"({FIXTURE_FILTER}={os.environ[FIXTURE_FILTER]})"
    )
    return chosen
