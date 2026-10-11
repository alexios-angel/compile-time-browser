# ctcompile 0.1.0 (alpha)

ctcompile does two separate jobs:

| Tool | What you get |
| --- | --- |
| `ctjs-translate` and `ctjs-opt` | The **JavaScript-to-C++ compiler**. They turn JavaScript into C++23 that uses ordinary C++ ownership and needs no VM. Anything they can't prove gets a compile-time diagnostic instead of code. |
| `ctcompile` | The **app packager**. It bundles a page's HTML, resources and precompiled JavaScript bytecode into one executable or `.ctapp` file. The JavaScript in a packaged app still runs in ctbrowser's VM. |

The two aren't connected yet. Packaging an app doesn't send its scripts through
the native compiler, and the packager doesn't precompile HTML or CSS either.
Both of those are on the plan, and neither is in this release.

## How the native compiler works

The input is ordinary JavaScript; you don't add type annotations. ctcompile
imports the script into `ctjs`, an MLIR dialect that models JavaScript
faithfully. Passes then try to prove facts about it:

- what type each value has;
- which function each call reaches;
- who owns each object and when it dies;
- what each call into the browser reads and writes.

Every function those proofs cover is lowered to the `ctnative` dialect, then to
MLIR's EmitC, and is finally printed as C++.

The types and ownership in the result are ordinary C++. Numbers are doubles, strings
are string values, arrays are `std::vector`s, objects with a fixed shape become
structs, JavaScript classes become C++ classes, and a callback whose target
is known becomes a direct call. DOM calls go straight into ctbrowser's C++
libraries.

The function bodies are harder to read. They come out of EmitC in single
static assignment form, so every intermediate value gets its own variable
(`v1`, `v2`, ...) rather than being folded into nested expressions.

The main rule is that **there is no fallback**. The native output never
links the VM or a garbage collector. If a function can't be proved, it's
refused, and the refusal names the function and the reason. Some examples:

- "parameter 0 is boxed - no caller proves it"
- "an object literal that escapes - it is returned"
- "the callback of Array.prototype.forEach captures binding 0, which is written
  after it is captured"

A program counts as native only when nothing in it is refused.

Correctness is checked by comparison. Most native tests run a script in
ctbrowser's VM, compile the same script to C++, build it with GCC and with
Clang, run both binaries, and require identical output.

### What works and what doesn't

Small programs and focused test pages compile completely. Large, minified
real-world scripts, full of classes, closures, event handlers and DOM access,
mostly don't yet: most of their functions are still refused, each with a
reason. Work on development branches gets further, but it hasn't been
validated for release yet.

Expect plenty of refusals on your own code. Arbitrary host APIs, many
exception and iterator patterns, and dynamic code are out of reach for now. If
a module comes out only partly lowered, it isn't a native program.

## Build and install

Requirements:

- CMake 3.23 or newer, Ninja, a C++23 compiler and initialized submodules.
- LLVM **23** (tested with **23.1.0**), including `llvm-tblgen`. LLVM is needed
  even with the native compiler switched off. The native compiler also needs
  the matching MLIR libraries, headers and `mlir-tblgen`. Other LLVM major
  versions are refused at configure time.
- The ctbrowser dependencies: Boost 1.88 or newer with Boost.URL, libcurl,
  libpng and zlib, libjpeg-turbo, simdutf, and mimalloc (or
  `-DCTBROWSER_USE_MIMALLOC=OFF`). The `ctrun` launcher and the tests also need
  Boost.ProgramOptions.
- Optionally, SDL3 for a windowed launcher (it runs headlessly without it),
  SDL3_ttf for outline fonts, and plutosvg/plutovg for SVG.
  [tools/Brewfile](../tools/Brewfile) lists them all.

With Homebrew, the two prefixes below are `brew --prefix` and
`brew --prefix llvm`. Check that the LLVM version is 23 first. CMake uses your
system compiler unless you set `CXX`.

```bash
git submodule update --init --recursive
deps=/path/to/dependencies
llvm=/path/to/llvm-23.1.0
cmake -S ctbrowser -B build-release -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_SHARED_LIBS=OFF \
  -DCMAKE_PREFIX_PATH="$deps" \
  -DLLVM_DIR="$llvm/lib/cmake/llvm" \
  -DMLIR_DIR="$llvm/lib/cmake/mlir" \
  -DCTBROWSER_ENABLE_PROJECTS=ctcompile \
  -DCTCOMPILE_ENABLE_MLIR=ON \
  -DCTBROWSER_BUILD_EXAMPLES=OFF -DBUILD_TESTING=OFF
cmake --build build-release --target \
  ctcompile-tool ctbrowser-tool-ctrun ctjs-opt ctjs-translate
cmake --install build-release --prefix "$PWD/install" --component ctcompile-tools
install/bin/ctcompile --version
```

If you only want the packager, use `-DCTCOMPILE_ENABLE_MLIR=OFF`, leave out
`MLIR_DIR`, and build just `ctcompile-tool` and `ctbrowser-tool-ctrun`.

The install isn't a portable binary distribution. It doesn't bundle shared
libraries or fonts, and a packaged app needs the same libraries on whatever
machine runs it. If ANGLE was fetched, the installed tools keep pointing at
`third-party/angle/linux-x86_64` in your checkout. Keep that directory, or
configure with `-DCTBROWSER_WITH_ANGLE=OFF` to build without WebGL.

You can also build ctcompile on its own against an installed engine:

```bash
cmake -S ctcompile -B build-ctcompile -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
  -DCMAKE_PREFIX_PATH="$deps" \
  -Dctbrowser_DIR=/path/to/engine/lib/cmake/ctbrowser \
  -DLLVM_DIR="$llvm/lib/cmake/llvm"
cmake --build build-ctcompile --target ctcompile-tool
```

For the native tools, add the MLIR options from the first command and build
`ctjs-opt` and `ctjs-translate`. Keep `BUILD_TESTING=OFF` here, since the tests
need the rest of the monorepo. A standalone build doesn't include `ctrun`, so
either pass a launcher with `--launcher` or produce a bundle with `--bundle`.

## Compile JavaScript to C++

Here's the smallest example, run from the checkout after installing the tools
above:

```bash
mkdir -p native-example
cat > native-example/answer.js <<'JS'
var answer = 6 * 7;
JS
install/bin/ctjs-translate --ctbrowser-js-to-ctjs \
  native-example/answer.js -o native-example/answer.ctjs.mlir
install/bin/ctjs-opt native-example/answer.ctjs.mlir \
  '--pass-pipeline=builtin.module(ctjs-resolve-globals,ctjs-lift-to-scf,ctnative-lower-to-emitc,emitc.func(canonicalize,convert-scf-to-emitc,convert-arith-to-emitc,canonicalize,ctnative-prune-dead-stores,canonicalize))' \
  -o native-example/answer.emitc.mlir
install/bin/ctjs-translate --mlir-to-cpp \
  native-example/answer.emitc.mlir -o native-example/answer.cpp
c++ -std=c++23 -O2 -Wall -Wextra -Werror -pedantic -Wconversion \
  -ffp-contract=off -Ictcompile/include -Ictbrowser/include \
  native-example/answer.cpp -o native-example/answer
./native-example/answer
```

It prints `answer=42`. The program prints its global variables when it
finishes, because this is the compiler's development entry point rather than a
browser app. `answer.cpp` is worth opening to see what came out.

Keep `-ffp-contract=off`. Without it, the C++ compiler may fuse floating-point
operations and get answers JavaScript wouldn't. Bigger programs that touch the
DOM also need ctbrowser's public libraries on the link line.

## Package an app

This uses the small app in the test directory. Point it at your own app
directory instead; the entry page defaults to `index.html`.

```bash
install/bin/ctcompile ctcompile/test/Packaging/app \
  --fonts "$PWD/ctbrowser/resources/fonts" \
  --manifest "$PWD/example-app.json" -o "$PWD/example-app"
./example-app --info
./example-app
```

To get a separate bundle instead, add `--bundle -o example.ctapp`, then run it
with `install/bin/ctrun example.ctapp`. `--entry page.html` picks a different
entry page, and `ctcompile --help` lists everything else.

The packager finds an app's scripts and resources by actually running the
page. It stops once network requests settle, or after 60 frames at most, so
anything a page only loads after a click can be missed. Read the warnings about
missing resources and fallback fonts, and click through the packaged app
yourself. Module scripts aren't supported yet. Classic scripts are stored as
bytecode, and the launcher refuses to start rather than quietly recompile them
from source.

## Development

Tests live in [test/](test/README.md). Treat the compiler's flags, its MLIR
dialects, the generated C++ and the packaged file formats as unstable in this
release; any of them can change. Build the packager and the launcher together
from the same tree.

ctcompile is part of the monorepo and shares its license,
[Apache-2.0 with LLVM Exceptions](../LICENSE). Third-party notices are in
[NOTICE](../NOTICE).
