> **Attribution:** the CSS and JavaScript parsers come from
> [compile-time-css](https://github.com/alexios-angel/compile-time-css) and
> [compile-time-javascript](https://github.com/alexios-angel/compile-time-javascript)
> (with [compile-time-containers](https://github.com/alexios-angel/compile-time-containers)
> underneath them); rendered with [SDL3](https://libsdl.org), text with
> [SDL3_ttf](https://github.com/libsdl-org/SDL_ttf) or the public-domain
> [font8x8](https://github.com/dhepper/font8x8) fallback.
> Apache License 2.0 with LLVM Exceptions; see [NOTICE](NOTICE).

# ctbrowser

A browser engine in C++23: HTML, CSS and JavaScript in, pixels out. It parses
its own HTML (the WHATWG tokenizer and tree builder), resolves a
real cascade, lays out block, inline and table formatting contexts, records a
display list, rasterises it in tiles across a thread pool, and runs the page's
script on a register-based bytecode VM with a standard library.

MDN's breakout tutorial runs unmodified, start to game over.

## The whole API

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
              document.getElementById('n').setText(String(n));
            });
          </script>
        </body>
    )", options);
}
```

One include, one link target, no SDL header. `run_app` owns the window, the
event loop, the clock, frame pacing, screenshots and teardown. See
[`examples/counter.cpp`](examples/counter.cpp) — forty lines, most of it the
page.

An idle page BLOCKS on the event queue rather than polling, so it costs
nothing; `app_options::max_fps` caps a busy one and is linear in CPU.

## The browser

`ctbrowse` is the engine as a program:

```bash
ctbrowse page.html                                     # a window
ctbrowse page.html --headless out.ppm --size 900 700   # no display at all
```

## What is in it

| | |
|---|---|
| **DOM** | its own WHATWG tokenizer and tree builder — implied tags, foster parenting, the adoption agency. Generation-tagged handles, so a stale reference fails a lookup instead of corrupting memory |
| **Style** | selector matching with an ancestor filter, a real cascade (origin, importance, specificity, order), the `style` attribute at Chrome/Firefox precedence, shorthand expansion |
| **Layout** | block, inline and table formatting contexts as a concept; baseline alignment; real line breaking, `white-space: pre`, generated content |
| **Paint** | a display list of layers, so a scroll re-composites rather than re-recording |
| **Raster** | tiles, drawn in parallel; a software backend always, `SDL_GPUDevice` when there is one, byte-identical between them |
| **Script** | register-based bytecode VM, NaN-boxed values, mark-and-sweep GC, and a standard library (`Math`, `Array`, `String`, `Number`, `Object`, `JSON`, `Promise`) |
| **Shell** | the assembly — forms, canvas 2D, the scrollbar, selection, the clipboard, `<select>` popups, context menus. Entirely SDL-free |
| **App** | the only part that knows SDL exists, and it is optional at build time |

Real fonts (SDL3_ttf over vendored OFL faces), images (BMP built in, more with
SDL3_image), audio, and `fetch` over HTTP with Boost.Asio.

## Building

```bash
git submodule update --init --recursive   # ctcss + ctjs
cmake --preset default && cmake --build --preset default && ctest --preset default
```

Needs CMake 3.20 and a clang or gcc with C++23 — the system default will do.
SDL3 is found if installed; without it the engine still builds and still
renders — `run_app` runs headless.

```bash
cmake --preset tsan && ctest --preset tsan     # the thread-safe DOM's real test
cmake --preset asan && ctest --preset asan
cmake --preset windows && cmake --build --preset windows    # llvm-mingw cross-build
cmake --build --preset windows --target windows-dist        # -> examples-windows/
```

The Windows executables are SELF-CONTAINED: a static SDL3 and SDL3_ttf, no DLL
beside them.

## Testing

`ctest` runs the suite headless. Two of the example pages additionally
byte-compare their render against a golden in `tests/golden/`, with
`CTBROWSER_FONTS=font8x8` so the comparison pins layout rather than moving with
FreeType. `REGOLDEN=1` regenerates them.

`tools/format.sh --check` is the formatting gate; `tools/check-package.sh`
installs to a temp prefix and builds a consumer against it with `find_package`,
which is the only proof the install actually works.

Profiling is built in: `CTBROWSER_PROFILE=out.csv CTBROWSER_PROFILE_SECONDS=10
./widgets` writes a record per loop iteration and prints CPU time against wall
time. `tests/bench_interaction` is the headless half — what a mouse move, a
hover change and a scroll each cost.

## History

This repository began as a compile-time browser: the page was a structural
NTTP and the parsers ran in constant evaluation. That engine is gone from the
tree and lives in the git history; the CSS and JavaScript parsers it was built
on remain, as submodules, doing their parsing at runtime.
[`docs/`](docs) records what the transition left behind.

## License

Apache License 2.0 with LLVM Exceptions. See [LICENSE](LICENSE) and
[NOTICE](NOTICE).
