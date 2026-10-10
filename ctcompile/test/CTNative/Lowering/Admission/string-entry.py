#!/usr/bin/env python3
"""Call a string entry from C++ by its JavaScript name.

Lowers a one-function program through Checks/pipeline.cmake with
STRING_ENTRIES naming the function, translates it to C++ and compiles a driver
that runs the script's top level and then calls the entry with two Strings, as
an embedder would. Each compiler's binary must print the JavaScript answer.
The entry must not be `static`: an embedder calls it from another translation
unit.
"""

import argparse
from pathlib import Path
import re

from CTNative.harness import FLAGS, find_compilers, native_pipeline, run

SOURCE = 'function greet(first, second) { return first + "," + second; }\n'

# The emitted `main` runs the script's top level; the driver renames it so it
# can run it first and then call the entry.
DRIVER = """
#include <cstdio>
int main() {
    if (script_main() != 0) { return 1; }
    const ctnative::js_string joined = greet(ctnative::js_string("a"), ctnative::js_string("b"));
    std::printf("%s\\n", joined.value().c_str());
    return 0;
}
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("translate", "opt"):
        parser.add_argument("--" + name, required=True)
    parser.add_argument("--work", type=Path, required=True)
    args = parser.parse_args()
    args.work.mkdir(parents=True, exist_ok=True)

    js = args.work / "greet.js"
    js.write_text(SOURCE)
    emitc = args.work / "greet.emitc.mlir"
    native_pipeline(args, js, emitc, STRING_ENTRIES="greet")
    cpp = args.work / "greet.cpp"
    run([args.translate, "--mlir-to-cpp", str(emitc), "-o", str(cpp)])
    if re.search(r"\bstatic\b[^;{]*\bgreet\(", cpp.read_text()):
        raise RuntimeError(f"{cpp}: the string entry greet is static")

    driver = args.work / "driver.cpp"
    driver.write_text(f'#define main script_main\n#include "{cpp}"\n#undef main\n{DRIVER}')
    for index, compiler in enumerate(find_compilers()):
        binary = args.work / f"driver-{index}"
        run([compiler, *FLAGS, str(driver), "-o", str(binary)])
        answer = run([str(binary)]).stdout
        if answer != "a,b\n":
            raise RuntimeError(f"{binary} printed {answer!r}, not 'a,b'")
    print("string entry: greet called from C++ with both compilers")


if __name__ == "__main__":
    main()
