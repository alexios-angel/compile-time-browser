> **Attribution:** the JavaScript parser comes from
> [compile-time-javascript](https://github.com/alexios-angel/compile-time-javascript).
> Its older interpreter, retained as a differential test oracle, uses
> [compile-time-containers](https://github.com/alexios-angel/compile-time-containers).
> ctbrowser parses CSS itself;
> [compile-time-css](https://github.com/alexios-angel/compile-time-css) remains
> as the comparison oracle for an opt-in benchmark. Presented through optional
> [SDL3](https://libsdl.org); text can use optional
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
[`examples/demos/counter.cpp`](examples/demos/counter.cpp) — forty lines, most of it the
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

Real fonts (SDL3_ttf over vendored OFL faces), images (BMP by hand, PNG through
libpng and JPEG through libjpeg-turbo, all of it SDL-free so tests can assert on
a decode), SVG through plutosvg, audio, and `fetch` over HTTP with **libcurl** —
which brings TLS with it, so the Windows cross-build has `https://` with no
OpenSSL. There was a hand-written Boost.Asio client here once; it is gone, and
`docs/build.md` says why.

It runs real libraries, which is the claim worth checking: **p5.js 2.3.1**,
**Phaser 4.2.1** and **Babylon.js 9.18.2** all load and draw. They live in
[`vendor/`](vendor) and the ratchets in `tests/corpus/` record how far each one
gets.

## Building

```bash
git submodule update --init --recursive   # ctjs (+ ctc for tests) + benchmark-only ctcss
cmake --preset default && cmake --build --preset default && ctest --preset default
```

Needs CMake 3.20 and a clang or gcc with C++23 — the system default will do.
SDL3 is found if installed; without it the engine still builds and still
renders — `run_app` runs headless.

```bash
# TSan needs mimalloc OFF: the engine's operator delete overrides collide with
# TSan's own in libclang_rt.tsan_cxx.a, and the link fails without this.
cmake --preset tsan -DCTBROWSER_USE_MIMALLOC=OFF && ctest --preset tsan
cmake --preset asan && ctest --preset asan
cmake --preset windows && cmake --build --preset windows    # llvm-mingw cross-build
cmake --build --preset windows --target windows-dist        # -> examples-windows/
```

The Windows executables are SELF-CONTAINED: a static SDL3 and SDL3_ttf, no DLL
beside them.

## Testing

`ctest` runs the suite headless: 83 tests, of which 48 are executables under
`tests/` and the rest are the examples, run bounded to a fixed frame count.
Fourteen of the example pages additionally byte-compare their render against a
golden in `tests/golden/` — plus `svg` and `imageformats`, which are gated on an
optional dependency and would otherwise fail for a reason that is not a
regression. `CTBROWSER_FONTS=font8x8` pins layout so the comparison does not
move with FreeType. `REGOLDEN=1` regenerates a golden — then **open the image
and look at it**; one accepted unseen is how the empty-button render shipped.

`tools/format.sh --check` is the formatting gate; `tools/check/check-package.sh`
installs to a temp prefix and builds a consumer against it with `find_package`,
which is the only proof the install actually works.

Profiling is built in: `CTBROWSER_PROFILE=out.csv CTBROWSER_PROFILE_SECONDS=10
./widgets` writes a record per loop iteration and prints CPU time against wall
time. `tests/bench/bench_interaction` is the headless half — what a mouse move, a
hover change and a scroll each cost.

## History

This repository began as a compile-time browser: the page was a structural
NTTP and the parsers ran in constant evaluation. That engine is gone from the
tree and lives in the git history; the CSS and JavaScript parsers it was built
on remain as submodules for different reasons. ctjs still parses JavaScript at
runtime; ctcss is only the comparison implementation in an opt-in benchmark.
[`docs/history/v1-retirement.md`](docs/history/v1-retirement.md) records what
the transition left behind.

## Documentation

[**`docs/README.md`**](docs/README.md) is the index — reference for how the
engine works today, [`docs/plans/`](docs/plans) for work that is not finished,
and [`docs/history/`](docs/history) for what was done or superseded. Several of
those last describe deleted code deliberately, and say so at the top.

[`CLAUDE.md`](CLAUDE.md) carries the invariants: the things that are easy to
break and expensive to notice.

## License

Apache License 2.0 with LLVM Exceptions. See [LICENSE](LICENSE) and
[NOTICE](NOTICE).
