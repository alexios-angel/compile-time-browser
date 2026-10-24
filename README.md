# compile-time-browser

A browser engine written from scratch in C++23, and a compiler that turns
JavaScript into plain C++.

This is an **alpha**. A lot works, a lot doesn't, and the interfaces will
change. It's here so people can poke at it, not so they can ship on it.

**The project is dormant.** Development is paused until AI coding tools are
good enough to carry it forward.

The repository holds two projects:

- **ctbrowser**, the browser engine. It parses HTML and CSS, runs JavaScript in
  its own VM, lays out and paints pages, and draws them in an SDL3 window or
  headlessly. You can open local pages with `ctbrowse` or embed the whole thing
  in a C++ program.
- **ctcompile**, the compiler. Its headline feature is a **JavaScript-to-C++
  transcompiler**: it reads ordinary JavaScript and writes C++23 that you build
  with GCC or Clang. The output has no VM, no garbage collector and no runtime
  interpreter behind it. ctcompile can also package a web app, with its HTML,
  resources and precompiled bytecode, into a single executable.

## The JavaScript-to-C++ compiler

Most ways of getting JavaScript into a native binary bundle an engine with the
script. ctcompile does something else. It tries to prove what each function
does: the type of every value, who owns every object, and what every call to
the browser touches. Then it writes C++ that does the same thing with ordinary
values, `std::vector`s, structs, classes and RAII.

If it can't prove something, it doesn't fall back to a slow path or tuck a
garbage collector under the code. It stops and tells you which function it
refused and why, as a compile-time error. A program is native only when every
function in it is.

The pipeline goes JavaScript, then an MLIR dialect for JavaScript (`ctjs`),
then a typed native dialect (`ctnative`), then MLIR's EmitC, then C++. It
covers numbers, strings, arrays, objects with a fixed shape, classes,
closures and callbacks, `Map`s, and calls into the browser's DOM, which go
straight to ctbrowser's C++ libraries. The tests check it the hard way: the
generated program is built with both GCC and Clang, run, and its output
compared with what the VM prints for the same script.

How far it gets today: small programs and test pages compile completely.
Large real-world scripts mostly don't yet. Most of their functions are still
refused, each with a reason, and closing that gap is the main work ahead.

[ctcompile/README.md](ctcompile/README.md) shows how to compile a small
program yourself.

## The browser

ctbrowser doesn't wrap Chromium, WebKit or Gecko. The parser, style system,
layout, rasterizer and JavaScript VM are all written for this project, with
the JavaScript parser coming from its sibling project,
[compile-time-javascript](https://github.com/alexios-angel/compile-time-javascript).

Some numbers, measured in September 2026:

- **test262:** 47,425 of the 49,377 tests that ran pass (96.0%).
- **web-platform-tests, `css/`:** 1,596 of 2,926 runnable files pass, with
  70,328 passing subtests.

Pages are painted by a software tile rasterizer, and WebGL goes through ANGLE
when it's available.

## Building

You need CMake 3.23 or newer, Ninja, and a C++23 compiler. The engine depends
on Boost 1.88 or newer (with Boost.URL), libcurl, libpng and zlib,
libjpeg-turbo, simdutf and mimalloc. Pass `-DCTBROWSER_USE_MIMALLOC=OFF` to use
the system allocator instead. The command-line tools and the tests also want
Boost.ProgramOptions. [tools/Brewfile](tools/Brewfile) lists everything I use
for development.

ctcompile needs LLVM 23, including its headers, libraries and `llvm-tblgen`,
even if you never use the native compiler. It's tested against 23.1.0, and
other major versions are refused at configure time.

From the repository root, with your own dependency paths:

```bash
git submodule update --init --recursive
deps=/path/to/dependencies
llvm=/path/to/llvm-23.1.0
cmake -S ctbrowser -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$deps" \
  -DLLVM_DIR="$llvm/lib/cmake/llvm" \
  -DCTBROWSER_ENABLE_PROJECTS=ctcompile
cmake --build build
```

That builds the browser and the app packager. A few variations:

- **Just the engine:** set `-DCTBROWSER_ENABLE_PROJECTS=` and leave out
  `LLVM_DIR`. The engine itself doesn't need LLVM.
- **The JavaScript-to-C++ compiler:** add `-DCTCOMPILE_ENABLE_MLIR=ON` and
  point `MLIR_DIR` at the MLIR that matches your LLVM. The
  [compiler README](ctcompile/README.md#build-and-install) has the full
  command.
- **A window:** [SDL3](https://libsdl.org) gives you one. Without it the
  browser runs headlessly. [SDL3_ttf](https://github.com/libsdl-org/SDL_ttf)
  adds outline fonts (otherwise you get the public-domain
  [font8x8](https://github.com/dhepper/font8x8) bitmap font), and plutosvg adds
  SVG.

The CMake presets in `ctbrowser/` need CMake 3.25. The Linux presets pick a
development compiler from `tools/clang-std-embed/`. If you'd rather use the one
you have, add `-DCMAKE_CXX_COMPILER="$(command -v c++)"`.

Linux is the main platform. A Windows build cross-compiles with llvm-mingw, but
it gets less attention.

## Opening a page

```bash
build/tools/ctbrowse/ctbrowse ctbrowser/examples/pages/widgets.html
```

Or render 30 frames without a display and save a screenshot:

```bash
SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy \
  build/tools/ctbrowse/ctbrowse ctbrowser/examples/pages/widgets.html \
  --size 900 700 --frames 30 --shot out.ppm
```

There are more pages in [ctbrowser/examples/pages](ctbrowser/examples/pages):
forms, canvas, images, scripting and a few games. If you just want to see
something run without building, there are
[prebuilt Linux examples](https://github.com/alexios-angel/compile-time-browser/releases/tag/examples-linux-x64)
from July 2026. They're older than this release, but they do run.

## Embedding the browser

Include `<ctbrowser.hpp>` and link `ctbrowser::ctbrowser`:

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

`run_app` takes care of the window, the event loop, frame pacing and cleanup.
Set `app_options::max_fps` if you want to cap redraws while the page is
changing. [counter.cpp](ctbrowser/examples/demos/counter.cpp) is the same idea
with some styling.

The engine is split into libraries for the DOM, style, layout, paint, raster,
script, shell and app layers, plus shared utilities in Core. Public headers are
in `ctbrowser/include/ctbrowser/` and the implementations in `ctbrowser/lib/`.

## Tests

There are tests for each subsystem, for app packaging, for the compiler, and
for rendered output, plus runners for test262 and web-platform-tests. To see
what your build registered:

```bash
ctest --test-dir build -N
```

The full suite is big. Run the tests for the part you changed.

Render tests compare screenshots against `ctbrowser/test/golden/`. Setting
`CTBROWSER_FONTS=font8x8` pins the font so the images come out the same
everywhere. If you regenerate a golden with `REGOLDEN=1`, look at the image
before you keep it. Before committing, run `tools/format.sh --check`.

## Where this came from

The name is historical. The first version of the engine parsed pages while the
C++ compiler was running, as constant evaluation, so one HTML string compiled
straight into the application. That design hit its limits and was retired in
July 2026, and the browser now parses at runtime like any other. The compiler
half of the project is where the "compile time" idea lives on.

Sibling projects:
[compile-time-javascript](https://github.com/alexios-angel/compile-time-javascript)
provides the JavaScript parser. Its older interpreter, still used for
differential testing, is built on
[compile-time-containers](https://github.com/alexios-angel/compile-time-containers).
[compile-time-css](https://github.com/alexios-angel/compile-time-css) stays
around as a comparison point for one optional benchmark; the browser has its
own CSS parser.

Third-party libraries, fonts and test corpora are credited in [NOTICE](NOTICE).

## License

Apache License 2.0 with LLVM Exceptions. See [LICENSE](LICENSE) and
[NOTICE](NOTICE).
