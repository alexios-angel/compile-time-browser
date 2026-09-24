# compile-time-browser

This repository contains a browser engine and an experimental application
compiler, written in C++23.

- **ctbrowser** parses HTML and CSS, runs JavaScript, and renders pages in a
  window or without a display. You can embed it in a C++ application or open
  local pages with the `ctbrowse` program.
- **ctcompile** packages applications with their resources and JavaScript
  bytecode. Its native compiler tools can also turn a proved subset of
  JavaScript into C++ with ordinary ownership and no VM or garbage collector.
  The packager still runs JavaScript in the browser's VM.

ctcompile 0.1.0 is a developer preview. The native application driver and full
native Bootstrap execution are unfinished. See the [compiler README](ctcompile/README.md)
for examples and the [release notes](ctcompile/docs/release-0.1.0.md) for its
current limits.

## Build

You'll need CMake 3.23 or later, Ninja, and a C++23 compiler. The engine depends
on Boost 1.88 or later with Boost.URL, libcurl, libpng/zlib, libjpeg-turbo,
simdutf, and mimalloc. Use `-DCTBROWSER_USE_MIMALLOC=OFF` to use the system
allocator instead. [tools/Brewfile](tools/Brewfile) lists the development
packages.

ctcompile requires LLVM 23, including its headers, libraries and `llvm-tblgen`,
even when the native pipeline is disabled. The tested version is 23.1.0.

Run these commands from the repository root, replacing the two prefixes with
your dependency locations. CMake uses the system compiler; set `CXX` to choose
another one.

```bash
git submodule update --init --recursive
ctcompile_deps_prefix=/path/to/dependencies
ctcompile_llvm_prefix=/path/to/llvm-23.1.0
cmake -S ctbrowser -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$ctcompile_deps_prefix" \
  -DLLVM_DIR="$ctcompile_llvm_prefix/lib/cmake/llvm" \
  -DCTBROWSER_ENABLE_PROJECTS=ctcompile
cmake --build build
```

This builds the browser and bytecode packager. For the engine alone, set
`-DCTBROWSER_ENABLE_PROJECTS=` and omit `LLVM_DIR`; the engine doesn't need LLVM.
To build the native compiler tools, add `-DCTCOMPILE_ENABLE_MLIR=ON` and point
`MLIR_DIR` at the matching MLIR package. The
[compiler build instructions](ctcompile/README.md#build-and-install) cover
installation and standalone builds against an installed engine.

[SDL3](https://libsdl.org) enables windows; without it, the browser runs
headlessly. [SDL3_ttf](https://github.com/libsdl-org/SDL_ttf) enables outline
fonts, with the public-domain [font8x8](https://github.com/dhepper/font8x8)
bitmap text as the fallback. SVG rendering needs plutosvg. WebGL uses ANGLE
when it has been fetched and enabled; see the
[platform notes](ctbrowser/docs/platform.md) for setup and rendering details.

CMake presets live in `ctbrowser/` and require CMake 3.25 or later. The Linux
presets select a development compiler under `tools/clang-std-embed/`. To use
an installed compiler, pass `-DCMAKE_CXX_COMPILER="$(command -v c++)"` when
configuring a preset. Windows cross-build instructions are in the
[platform notes](ctbrowser/docs/platform.md); which DLLs need to ship depends
on the libraries enabled in that build.

## Open a page

After building, run:

```bash
build/tools/ctbrowse/ctbrowse ctbrowser/examples/pages/widgets.html
```

To render a fixed number of frames and save a PPM screenshot without a display:

```bash
SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy \
  build/tools/ctbrowse/ctbrowse ctbrowser/examples/pages/widgets.html \
  --size 900 700 --frames 30 --shot out.ppm
```

The [example pages](ctbrowser/examples/pages) exercise forms, canvas, images
and scripting. The [test corpora](ctbrowser/vendor/README.md) include p5.js 2.3.1,
Phaser 4.2.1, Babylon.js 9.18.2 and Bootstrap 5.3.8. Their regression checks
track specific behavior; support for a library is still a work in progress.

## Embed the browser

Include `<ctbrowser.hpp>` and link against `ctbrowser::ctbrowser`:

```cpp
#include <ctbrowser.hpp>

int main() {
    ctbrowser::app_options options;
    options.title = "Counter";
    options.width = 480;
    options.height = 320;
    return ctbrowser::run_app(R"(
        <body>
          <h1 id=n>0</h1>
          <button id=up>+1</button>
          <script>
            var n = 0;
            document.getElementById('up').addEventListener('click', function () {
              n = n + 1;
              document.getElementById('n').textContent = String(n);
            });
          </script>
        </body>
    )", options);
}
```

`run_app` owns the window, event loop, frame pacing and cleanup. Applications
can set `app_options::max_fps` to limit redraws while a page is changing.
See [counter.cpp](ctbrowser/examples/demos/counter.cpp) for a styled example.

The engine is split into DOM, style, layout, paint, raster, script, shell and
application libraries, with shared utilities in Core. Page rendering uses a
software tile rasterizer. The shell provides browser APIs such as forms,
canvas and `fetch`; the JavaScript VM handles script execution. Public headers
live under `ctbrowser/include/ctbrowser/`, and implementations under
`ctbrowser/lib/`. The [architecture guide](ctbrowser/docs/architecture.md)
explains the boundaries.

## Development and tests

Tests cover individual subsystems, application packaging, compiler behavior
and rendered output. The browser also has runners for
[web-platform-tests](ctbrowser/docs/wpt.md) and [test262](ctbrowser/docs/test262.md).
Those documents record dated measurements and known gaps.

List the tests registered in your build with `ctest --test-dir build -N`.
Follow the [focused test policy](CLAUDE.md#build--test) when selecting checks.
Contributors using the shared devbox should read [CLAUDE.md](CLAUDE.md) before
editing or building; it covers synchronization, build locks and validation.

Run `tools/format.sh --check` before committing. Render tests compare images
with the files in `ctbrowser/test/golden/`; `CTBROWSER_FONTS=font8x8` fixes the
font choice for repeatable comparisons. If you regenerate a golden with
`REGOLDEN=1`, inspect the image before accepting it.

The [browser documentation](ctbrowser/docs/README.md) covers profiling,
packaging checks and subsystem behavior. The [compiler handoff](ctcompile/docs/HANDOFF.md)
records the latest native work and its next boundary.

## History and credits

The original engine parsed pages during C++ constant evaluation. It was
retired in July 2026; the current browser parses pages at runtime. The
[retirement notes](ctbrowser/docs/history/v1-retirement.md) explain the change
and what remains in Git history.

[compile-time-javascript](https://github.com/alexios-angel/compile-time-javascript)
supplies the JavaScript parser. Its older interpreter, retained for differential
testing, uses [compile-time-containers](https://github.com/alexios-angel/compile-time-containers).
[compile-time-css](https://github.com/alexios-angel/compile-time-css) remains a
comparison implementation for an optional benchmark; the browser has its own
CSS parser.

Third-party libraries, fonts and test corpora are credited in [NOTICE](NOTICE).

## License

Apache License 2.0 with LLVM Exceptions. See [LICENSE](LICENSE) and
[NOTICE](NOTICE).
