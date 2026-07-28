# Build speed, formatting, and the profiler

How long a build takes and why, the formatting gate CI runs, and how to
measure the engine's CPU cost at runtime.

> **The engine stopped being C++ modules on 2026-07-28.** Everything below
> about BMIs, module interfaces and the import graph is history now; the
> measurements it records were real and are kept because the reasoning still
> applies to headers, in the form at the bottom of this file. Read
> **"Headers, measured"** first if you only want the current numbers.

## Headers, measured (2026-07-28)

Same tree, same compiler (clang), same 22-core machine, clean build:

| | modules | headers |
|---|---|---|
| wall | 89.1 s | **50.0 s** |
| CPU | 243 s | **672 s** |
| peak RSS | 1.14 GB | 0.64 GB |
| ninja targets | 272 | 73 |

Wall time nearly halved: nothing serialises on scanning the module graph any
more, so 22 cores are actually usable. Total CPU nearly tripled: 38 translation
units re-parse what one BMI used to hold. Wall time is what a person waits for
and it got better; the CPU figure is the bill for header-only subsystems, and
it is paid down by moving definitions into `.cpp` files.

Only `core`, `script`, `shell` and `app` have done that so far.
`script/compile` is the shape to copy: the header declares one function,
`compile.cpp` holds 1,689 lines of compiler, and no consumer parses any of it.

**The rule that survives the modules era**: no third-party header in a public
header. It used to be about BMI size - `<boost/asio.hpp>` in the `:net`
interface made that BMI 27 MB, `<SDL3/SDL.h>` made `ctbrowser.app.pcm` 26 MB.
Now it is about every consumer re-parsing them. Same rule, same fix, different
reason: put them in the `.cpp`, as `core/cpu_time.cpp` does with `<windows.h>`.

## BUILD SPEED (2026-07-25)

Measured, then fixed. A clean the engine build was **143.7 s wall / 237.9 s CPU**; it is
now **116.7 s / 221 s**, and the parts a user waits on moved most:
`tests` 221 s → 127 s, `examples` 138 s → 36 s. A bare
`import ctbrowser;` with an empty `main` costs **0.89 s**, so the umbrella is
cheap — a consumer pays for what it USES.

Three things did it:

1. **lld.** The same executable links in 0.65 s against 4.31 s with the default
   `ld`, and the engine builds twenty-six of them. `CTBROWSER_USE_LLD=OFF` opts out;
   `ctbrowser_target()` is the one place that decides how the engine is built.
2. **Module implementation units.** `script/{vm,builtins}.cpp`,
   `shell/{app,net}.cpp` — interfaces DECLARE, `module X;` units DEFINE. Every
   TU that imported the engine used to re-instantiate and re-optimise it:
   `vm_basics.cpp.o` was 1.6 MB / 2698 symbols, of which `install_builtins` was
   21 KB and the VM's `run_loop` 15 KB.
3. **THIRD-PARTY HEADERS ARE NOT ALLOWED IN AN INTERFACE'S GMF.** This is the
   one to remember: a module's global module fragment is **serialized into its
   BMI**. `#include <boost/asio.hpp>` in `:net` made that BMI **27 MB**, and
   `<SDL3/SDL.h>` made `ctbrowser.app.pcm` 26 MB — for headers whose types
   neither module exposes. Moving them into the implementation units took
   net.pcm to 3.7 MB. Put Boost/SDL/FreeType includes in a `.cpp`, never in a
   `.cppm` interface, unless the type is genuinely in the public API.

**One archive:** `libctbrowser.a` merges all nine engine libraries (an `ar`
merge of the same objects, not a rebuild), so a non-CMake build links ONE file.
`CTBROWSER_SINGLE_LIB=OFF` skips it; CMake users keep using
`ctbrowser::ctbrowser`, which also carries the include paths and BMIs an archive
cannot.

**The opt-out:** a modules project has no header-only mode, so the knob that
buys back what an all-inline engine gave is `CTBROWSER_LTO=ON` — inlining
across the library boundary at LINK time rather than by recompiling the engine
in every TU. Off by default, because the default is meant to be fast to build.

`tools/check-package.sh` is what catches the other half of this: an exported
target that links `Freetype::Freetype` needs a matching `find_dependency` in the
installed config, and stage 6 shipped without one.

## FORMATTING (2026-07-27)

**`tools/format.sh`**, and `--check` in CI on its own runner. `.clang-format`
is **LLVM with five deviations**, and the deviations are not preferences - they
are what the repository was measured to already be: tabs (30,000 tab-indented
lines against 2,600), 100 columns, `const rect & box` (881 against 16), one-line
`if (x) { return; }` (1,057 of them), and unindented namespaces. Stock LLVM
would rewrite every line; the point of a formatter is to be a no-op on code
that is already right.

> **Superseded on the tab count (2026-07-27, commit `3a7de2a`).** `UseTab` went
> `ForIndentation` -> `Never` and the whole tree was reformatted: it is SPACES
> now, `IndentWidth: 4`. The other four deviations stand. Read `.clang-format`
> for the current settings rather than this paragraph.

Two settings are non-obvious and were both wrong on the first pass:
`AllowShortIfStatementsOnASingleLine` must be `WithoutElse`, not `Never` -
`AllowShortBlocksOnASingleLine` governs the block but the `if` is governed
here, and `Never` overrules it - and `AccessModifierOffset` must be **-4**,
since LLVM's -2 assumes a 2-space indent. Getting those two right halved the
diff, from 14,394 lines to 7,065.

**What is NOT formatted is in `.clang-format-ignore`**: generated files
(font8x8, entities, the SPIR-V blobs), vendored ones (stb), submodules and the
fetched toolchain. Formatting a generated file makes "the generator changed"
and "the formatter ran" indistinguishable in a diff.

Sanitizer suppressions grew alongside: the one test that drives `run_app`
initialises SDL, which reaches libdbus (a lock-order inversion TSan reports)
and leaves EGL allocated (a leak LSan reports). Both suppressed BY LIBRARY in
`tests/{tsan,lsan}.supp`, and both files say they were verified by planting
a fault in our own code and confirming it is still caught - which was actually
done, for the leak, in the commit that added it.

Two repository problems the formatting turned up, neither of them formatting:
**`build-timing/` was committed** - 457 files, 408 MB, from a `git add -A` -
and is untracked now, though the history still carries it. And **the goldens
were never tracked at all**: `*.ppm` in `.gitignore` swallowed
`tests/golden/`, so every golden test would have failed on a fresh clone
with "no golden". They are tracked now, and the rule that swallowed them is
gone: the ignore files are per-directory and the root's patterns are anchored
(`/*.ppm`), so nothing reaches into `tests/golden/` to need an exception —
a negation would not have worked anyway, since git cannot re-include a file
whose parent directory is excluded.

## CPU AND THE PROFILER (2026-07-27)

**`CTBROWSER_PROFILE=out.csv CTBROWSER_PROFILE_SECONDS=10 ./widgets.exe`** — a
record per loop iteration (poll / tick / frame / present / asleep, layouts,
whether it drew), a summary on stdout, and **CPU time against wall time**,
because "it uses 65% of my CPU" is not the same question as frames per second.
`tests/bench_interaction` is the headless half: what a mouse move, a hover
change and a scroll each COST, with the implied CPU at 60 fps printed beside
them.

Measuring first was worth it — every guess was wrong.

**The pool's idle poll was not the problem** (it looked like the obvious one: a
worker waking every millisecond on every core, forever). It is fixed anyway —
idle workers now sleep on a global "is there work anywhere" condition, since
`submit` notifies one queue and a worker finds STEALABLE work by looking — but
it measured well under 1%.

**Nor was the engine.** A hover change is 0.9 ms and a scroll 0.5 ms; 60 fps of
hover changes is about 5% of a core.

**It was that the loop never stopped drawing.** An idle page redrew because
nothing distinguished "nothing happened" from "nothing was asked for", and a
busy page ignored `max_fps` entirely.

**An idle application now BLOCKS.** `browser::needs_frame()` and
`next_wakeup_ms()` are the contract: the loop asks the page whether anything
changed and how long until it next has something to do on its own — a timer, an
animation frame, the caret's next blink — and blocks on the event queue for
exactly that long. Idle widgets.exe went 0.8% → **0.2% of one core**, and a
caret blinks ON TIME instead of whenever the next event happens to arrive,
which was the same shape as the scrollbar that did not appear until you moved
the mouse.

**`max_fps` IS the throttle**, and it did nothing before: pacing asked "is
there more to draw", which is false the instant a frame finishes, so every
frame took the wait branch and an animating page ran at whatever vsync allowed.
It asks "did we draw" now. Measured on pong.exe with a real window: 60 fps =
12.8%, 30 = 7.5%, 15 = 3.9%. `CTBROWSER_MAX_FPS` sets it without a rebuild.

**`SDL_WaitEventTimeout(&event, ...)` then `SDL_PushEvent` is not a wait.** SDL
posts an internal poll sentinel to bound PollEvent loops, and pushing that back
re-arms it, so the wait returns instantly forever - eight million iterations in
ten seconds, and a 306 MB profile. Pass **NULL** to wait without taking the
event. The profiler's history is capped for the same reason: a profile of a
loop that has gone wrong is exactly when it explodes.

