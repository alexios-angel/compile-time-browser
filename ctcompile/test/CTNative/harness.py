"""What the CTNative python drivers share: a checked subprocess run and the two
compilers (the same pair Target/Cpp/harness.py finds).

lit puts ctcompile/test on PYTHONPATH (lit.cfg.py), which is also what lets a
driver import a sibling from another directory as, say,
`from CTNative.HostContract import contract`. A hand run needs the same
PYTHONPATH. Node and the interpreter reference are not discovered here: every
RUN line passes `--node %node --reference %native_reference`, both configured
by CMake (lit.site.cfg.py.in).
"""

import json
import os
from pathlib import Path
import re
import subprocess

from Target.Cpp.harness import (
    CORE_INCLUDE,
    FLAGS,
    RUNTIME_INCLUDE,
    find_compilers,
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
    result = subprocess.run(
        command, capture_output=True, text=True, timeout=120, env=environment, input=input_text
    )
    if (result.returncode == 0) != success:
        raise RuntimeError(
            f"unexpected tool status {result.returncode}: {command!r}\n{result.stdout}{result.stderr}"
        )
    return result


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
        text = Number.isNaN(value) ? 'nan' : Object.is(value, -0) ? '-0' :
            value === -Infinity ? '-inf' : String(value);
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


# The lifetime gate: AddressSanitizer + UBSan, leaks and use-after-return on.
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
