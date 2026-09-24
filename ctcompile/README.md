# ctcompile 0.1.0 — developer preview

ctcompile packages ctbrowser applications and develops a native C++ backend for
JavaScript. These are currently two separate workflows:

| Tool | What it produces today |
| --- | --- |
| `ctcompile` | An application bundle or launcher executable containing HTML, resources and precompiled JavaScript bytecode. JavaScript still runs in the ctbrowser VM. |
| `ctjs-translate` + `ctjs-opt` | C++ for a proved JavaScript subset, with ordinary C++ ownership and no Script/VM dependency in native output. Unsupported functions receive diagnostics. |

The native application driver and full native Bootstrap execution remain
unfinished. Packaging does not yet compile HTML into a document blueprint or
CSS into a style program; those are plan items. HTML/CSS parsing, layout and
rendering remain runtime work. See the [release notes](docs/release-0.1.0.md)
for the preview's limits and validation status.

## Build and install

Build on a sufficiently provisioned build host, from the monorepo root.
Contributors using the shared devbox follow [CLAUDE.md](../CLAUDE.md).

Requirements:

- CMake 3.23+ (the engine uses header file sets), Ninja, a C++23 compiler and
  initialized submodules.
- LLVM **23**, tested with **23.1.0**, including `llvm-tblgen`. LLVM is required
  even when MLIR is disabled. Native tools also require matching MLIR libraries,
  headers and `mlir-tblgen`; other LLVM major versions are refused at configure.
- ctbrowser dependencies: Boost 1.88+ with Boost.URL, libcurl, libpng/zlib,
  libjpeg-turbo, simdutf, and mimalloc (or `-DCTBROWSER_USE_MIMALLOC=OFF`).
- SDL3 for a windowed launcher; without it the launcher runs headlessly.
  SDL3_ttf enables outline fonts, and plutosvg/plutovg enable SVG rendering.
  [tools/Brewfile](../tools/Brewfile) records the development dependencies.

Set the two prefixes to your installed dependencies. With Homebrew they are
`brew --prefix` and `brew --prefix llvm`, respectively; check the LLVM version
before configuring. The direct CMake command uses the system compiler; set
`CXX` to select another C++23 compiler.

```bash
git submodule update --init --recursive
ctcompile_deps_prefix=/path/to/dependencies
ctcompile_llvm_prefix=/path/to/llvm-23.1.0
cmake -S ctbrowser -B build-release -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_SHARED_LIBS=OFF \
  -DCMAKE_PREFIX_PATH="$ctcompile_deps_prefix" \
  -DLLVM_DIR="$ctcompile_llvm_prefix/lib/cmake/llvm" \
  -DMLIR_DIR="$ctcompile_llvm_prefix/lib/cmake/mlir" \
  -DCTBROWSER_ENABLE_PROJECTS=ctcompile \
  -DCTCOMPILE_ENABLE_MLIR=ON \
  -DCTBROWSER_BUILD_EXAMPLES=OFF -DBUILD_TESTING=OFF
cmake --build build-release --target \
  ctcompile-tool ctbrowser-tool-ctrun ctjs-opt ctjs-translate
cmake --install build-release --prefix "$PWD/install" --component ctcompile-tools
install/bin/ctcompile --version
```

For packaging only, use `-DCTCOMPILE_ENABLE_MLIR=OFF`, omit `MLIR_DIR`, and build
only `ctcompile-tool` and `ctbrowser-tool-ctrun`. The install component contains
the compiler tools and, in a monorepo build, the matching `ctrun` launcher.
It does not bundle external shared libraries, fonts or a compiler development
SDK. Installed tools and packaged executables retain their host library
requirements; this is not a portable binary distribution.
If ANGLE was fetched, its libraries remain at `third-party/angle/linux-x86_64`
in the checkout and that path is retained by the installation. Keep that
directory, or configure with `-DCTBROWSER_WITH_ANGLE=OFF` to omit WebGL support.
Moving a packaged executable to another machine requires matching host libraries.

Standalone configuration against an installed engine is also supported:
`cmake -S ctcompile -B build-ctcompile -Dctbrowser_DIR=<prefix>/lib/cmake/ctbrowser
-DLLVM_DIR=<llvm-prefix>/lib/cmake/llvm`. Add the MLIR options above for native
tools. A standalone compiler build does not build `ctrun`; supply a matching
launcher with `--launcher` or use `--bundle`.

## Package an application

This example uses the repository's small packaging fixture. Replace its path
with your application directory; the default entry is `index.html`.

```bash
install/bin/ctcompile ctcompile/test/Packaging/app \
  --fonts "$PWD/ctbrowser/resources/fonts" \
  --manifest "$PWD/example-app.json" -o "$PWD/example-app"
./example-app --info
./example-app
```

For a separate bundle, add `--bundle -o example.ctapp`, then run
`install/bin/ctrun example.ctapp`. Use `--entry page.html` for a different entry
page. `ctcompile --help` lists the other options.

Packaging executes the page to discover its scripts and resources. It stops
after requests settle, with a 60-frame ceiling; resources requested only by
later interactions can be absent. The packaged resource registry is sealed.
Check warnings about missing resources or fallback fonts, and exercise the
packaged application's actual interactions. Module scripts are currently
refused. Classic scripts are stored as bytecode, and the launcher refuses a
startup that would silently recompile them from source.

## Compile a small native program

The following source uses the native scalar subset. Run this from the checkout
after installing the MLIR tools above. The generated program prints its global
observations; this is the native development entry point, not a browser app.

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

Expected output: `answer=42`. Keep `-ffp-contract=off` for JavaScript numeric
semantics. More complex native output may require public ctbrowser subsystem
libraries; browser APIs must call those libraries directly. An unproved type,
ownership or host effect remains a compile-time refusal. The boxed EmitC
pipeline is a separate development backend and retains a VM dependency.

## Development status

[HANDOFF.md](docs/HANDOFF.md) records current measured native progress and the
next boundary. The [Bootstrap Data probe](docs/bootstrap-data-probe.md)
describes the application-derived scope. [Native optimization defaults](docs/native-optimization-defaults.md)
documents the two default optimizations; the [optimization roadmap](docs/native-pe-roadmap.md)
tracks opt-in work. See the [source layout](docs/source-layout.md) and
[focused test instructions](test/README.md) for development.

The monorepo is licensed under [Apache-2.0](../LICENSE); see [NOTICE](../NOTICE)
for third-party notices.
