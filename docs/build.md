# Build speed, formatting, and the profiler

How long a build takes and why, the formatting gate, and how to measure the
engine's CPU cost at runtime.

> **The engine stopped being C++ modules on 2026-07-28.** Everything below
> about BMIs, module interfaces and the import graph is history now; the
> measurements it records were real and are kept because the reasoning still
> applies to headers, in the form at the bottom of this file. Read
> **"Headers, measured"** first if you only want the current numbers.

## Headers, measured (2026-07-28)

Same tree, same compiler (clang), same machine, clean build:

| | modules | headers, all inline | headers, split |
|---|---|---|---|
| wall | 89.1 s | 50.0 s | **46.5 s** |
| CPU | 243 s | 672 s | **303 s** |
| peak RSS | 1.14 GB | 0.64 GB | **0.51 GB** |

Read the middle column as the cost of doing nothing else. Dropping modules
halves wall time on its own - nothing serialises on scanning the import graph
any more - but it nearly TRIPLES total CPU, because every translation unit
re-parses what one BMI used to hold once.

The third column is after every significant header got a `.cpp`. That gives
back 55% of the CPU, leaving it 24% above the modules baseline while wall time
stays near half. The rule it comes from: a header declares, its `.cpp` defines,
and nobody pays to parse a body they do not call.

`script/compile` is the extreme case and the shape to copy - the header
declares one function, `compile.cpp` holds 1,689 lines of compiler, and no
consumer parses any of it.

**The rule that survives the modules era**: no third-party header in a public
header. It used to be about BMI size - `<boost/asio.hpp>` in the `:net`
interface made that BMI 27 MB, `<SDL3/SDL.h>` made `ctbrowser.app.pcm` 26 MB.
Now it is about every consumer re-parsing them. Same rule, same fix, different
reason: put them in the `.cpp`, as `core/cpu_time.cpp` does with `<windows.h>`.

## HTTP: libcurl, AND https ON WINDOWS (2026-07-31)

`fetch()` runs over **libcurl**, not the hand-written Asio client it replaced.
Asio is a socket; the request line, header folding, chunked decoding and
redirects were all written here, and that is the half a browser keeps needing
more of.

**The reason it is libcurl and not POCO** - which was written first, and does
cross-compile; that work is in the history:

* **TLS on Windows for free.** curl uses **Schannel**, the OS TLS stack, so the
  cross build gets `https://` with no OpenSSL cross-compiled. The Windows
  presets shipped `CTBROWSER_WITH_TLS=0` - no https at all - and POCO's NetSSL
  would have meant cross-building OpenSSL to change that.
* HTTP/2, brotli and zstd are already there, which retires the zlib work
  Content-Encoding was going to need.

POCO's mature WebSocket is what curl lacks; if that becomes a requirement it is
the reason to revisit, and `net.hpp`'s request/response interface is what makes
revisiting cheap - a transport is one `.cpp` behind one `fetch()`.

**Asio is gone entirely** (2026-07-31), not kept as a fallback. Keeping it meant
keeping the hand-written HTTP above it compiling and correct for a path nothing
took, and meant an engine that could silently fall back to a transport with NO
TLS - worse than one that refuses to configure. `find_package(CURL REQUIRED)`.

That removed it from two more places, which is where a dependency actually
lives: `tests/net_basics.cpp` stood up its loopback server with Asio, and
`examples/cli/ctdrive.cpp` ran its command socket on it. Both are plain BSD
sockets now - roughly fifty lines each, `#if defined(_WIN32)` for the Winsock
spelling - because a harness that reintroduces the dependency the engine just
dropped has not dropped it.

**TLS belongs to the transport.** `tls_available()` was a `constexpr` on
`CTBROWSER_WITH_TLS`, which `find_package(OpenSSL)` set - correct while the
engine linked OpenSSL itself, and wrong the moment curl brought its own, because
no OpenSSL probe can see Schannel. It is a runtime call now, and the libcurl
transport answers it by asking `curl_version_info` what the linked library
actually has.

**For the cross build**, `build-curl-sysroot.sh` in the llvm-mingw repo installs
the static libcurl into `<toolchain>/<triple>/`. It delegates to that repo's
existing `build-curl.sh` rather than duplicating the pinned versions and TLS
decision - that script already builds this library, for clang's own
`std::fetch`. Run once; the artifact is 1.2 MB.

## BOOST: WHAT IS LINKED, AND WHAT WAS DELIBERATELY NOT TAKEN (2026-07-31)

Boost was header-only here until 2026-07-31. **Boost.URL ended that**, and it had
to: it has been compiled-only since 1.87, where `boost/url/src.hpp` became a hard
`#error` reading "src.hpp is discontinued". There is no header-only way to have
it, and the engine was carrying **two hand-written URL parsers that disagreed** -
`parse_url` in net.cpp and `split_url` in bindings.cpp, the second of which
reported hostname `[:` and port `1]` for `http://[::1]/` because it reached for
the last colon with no bracket guard.

**How it reaches the cross-build.** `tools/mingw/build-boost-mingw.sh` compiles
Boost.URL's 66 sources with the llvm-mingw compiler into
`tools/llvm-mingw/x86_64-w64-mingw32/lib/libboost_url.a`, beside the static SDL3,
SDL3_ttf and plutosvg already living there. **No b2** - Boost's build system is
not involved, because the sources are self-contained C++ whose only dependencies
are header-only Boost. The script reads the tag out of the Boost headers it
builds against, so a library built from one release against another's headers
cannot happen quietly.

**What it cost, measured:** `p5webgl.exe` went from 17,083,904 to 17,340,928
bytes, +251 KB, and the archive is 1.0 MB across 66 objects. All ten Windows
goldens stayed byte-identical.

## DEPENDENCIES COME FROM BREW, NOT APT (2026-08-01)

`tools/Brewfile` is the list, and `tools/remote-build.sh` converges it with
`brew bundle` before every build. **The reason is versions.** Two devbox builds
died on apt packages that were installed and unusable:

* Ubuntu 24.04 ships **libjpeg-turbo 2.1.5**, and the `tj3_*` API this engine's
  JPEG decoder was first written against arrived in **3.0**. Not a warning — the
  functions simply do not exist.
* apt packages **`boost_url` separately** from `libboost-dev`, so a box with
  Boost installed still fails at configure with "Could not find a package
  configuration file provided by boost_url".

Brew ships current versions *and the same ones the development machine has*,
which keeps the two in step — a cross-platform golden depends on that far more
than on either being newest. apt stays right for the box's own furniture:
`build-essential`, `git`, `rsync`, the toolchain.

The JPEG decoder is nevertheless written to libjpeg-turbo's **2.x API**, which
3.x still exports without deprecation, so it builds against either. Brew-first
removes the constraint; it does not make the portable spelling wrong.

## A SECOND COMPILER FOUND FOUR THINGS (2026-08-01)

Builds moved to the shared devbox, and **the first build there stopped four
times.** That box differs from this one in two ways that turned out to matter
more than the hardware: it compiles with **GCC 13** rather than clang, and it
has **no SDL at all** — so `CTBROWSER_WITH_TTF=0` and `CTBROWSER_WITH_SVG=0`,
configurations this repository claims to support and which no machine that had
ever built it actually used.

| what | why only there |
|---|---|
| `raster/ttf.cpp` defined a class its header did not declare | `ttf.hpp` guards `ttf_backend` behind `#if CTBROWSER_WITH_TTF`; the `.cpp` did not, and CMake compiles it unconditionally |
| `glsl_eval.cpp` range-for'd over a temporary | a ternary between two `initializer_list`s dangles before C++23; clang implements P2718R0, GCC 13 does not |
| the JPEG decoder used libjpeg-turbo's `tj3_*` API | that arrived in 3.0 and **Ubuntu 24.04 LTS ships 2.1.5**, so the engine could not build on a current LTS at all |
| a trailing `\` on a `//` line in `examples/corpus/p5events.cpp` | it splices the next line into the comment; GCC's `-Wcomment` + `-Werror` rejects it, clang says nothing |

Plus one in the suite: three tests in `chrome_basics.cpp` asserted
`use_real_fonts()` outright, which is false without SDL3_ttf.

**The lesson is not "GCC is stricter".** Three of the four are real defects that
clang is entitled to accept — a dangling pointer, an API that does not exist on
the target, a class with no declaration. What the second machine bought was a
*different set of assumptions*, and every one of these had been invisible for as
long as there was only one.

## THE IMAGE CODECS REACH IT THE SAME WAY (2026-08-01)

`tools/mingw/build-image-libs-mingw.sh` is that script's sibling and builds **zlib,
libpng and libjpeg-turbo** into the same sysroot. PNG and JPEG moved out of the
optional SDL3_image hook and into the SDL-free engine, so the cross build needs
them; `docs/shell.md` has why, and the short version is that `tests/` is
SDL-free by an invariant, so the whole suite saw a PNG as a zero-sized image and
nothing said so until Phaser arrived.

Three things worth knowing before running it:

* **The versions are PINNED** (`zlib_tag`, `libpng_tag`, `turbo_tag`). A cross
  build that quietly follows upstream's default branch is how the Windows half
  of a byte-compared golden starts disagreeing with the Linux half for a reason
  nobody can see. zlib is only there because libpng needs it.
* **zlib builds a DLL whatever you ask it.** `BUILD_SHARED_LIBS=OFF` does not
  stop it, and `FindZLIB` prefers the import library's name - so every `.exe`
  would want a `zlib1.dll` beside it, which is the folder-of-DLLs this sysroot
  exists to avoid. The script deletes them and keeps `libzlibstatic.a`.
* **No NASM means no SIMD in libjpeg-turbo**, which it warns about and then
  builds anyway. Correct either way, slower on Windows than on Linux; install
  `nasm` before running it if that matters.

Verified end to end: the Windows preset configures, links, and **29 of 29 test
executables pass when run through WSL**, `data_url` among them - which is the
one that decodes a PNG and a JPEG and compares the PNG against a BMP pixel for
pixel.

**Two bugs the script itself had first**, both the silent kind. `src/*.cpp`
matches 27 of the 66 files, so the first archive linked, looked healthy and had
`url_impl` *undefined inside it*; and the compile loop was `... & done; wait`,
which discards every exit code, so a failed compile would have been archived
around without a word. It is recursive and checks each PID now.

### Rejected, with reasons

Not everything Boost offers is an improvement, and these were each considered and
turned down rather than overlooked:

* **Boost.Locale** — needs ICU, and is **locale-aware by definition**. This
  repository byte-compares renders across Linux and Windows; host-dependent
  collation would make a golden depend on `LC_ALL`. (Same reasoning retires
  `std::strtod`, which respects `LC_NUMERIC`, in favour of `std::from_chars`,
  which does not.)

  > **CORRECTED 2026-07-31.** This entry used to reject
  > `boost::algorithm::iequals` alongside it, on the same locale grounds. That
  > was too broad and it is now what `core/algorithms.hpp` is built on. Only the
  > DEFAULT overload is locale-aware — it takes `std::locale()`, the global one.
  > Passing `std::locale::classic()` gives ASCII-only folding that is
  > deterministic by the standard's definition of the C locale, which is exactly
  > what HTTP field names and HTML tag names want. Verified rather than
  > reasoned: with the classic locale it folds A-Z, leaves bytes above 127
  > alone, and never merges two different UTF-8 sequences.
* **Boost.Context / Coroutine / Fiber** — per-ABI assembly, and what actually
  broke the cross-build. Lifting the rule for portable C++ says nothing about
  these. `fetch` is already asynchronous by another route.
* **Boost.Endian** — the byte-packing sites in softgl, webgl and svg are ARGB
  *channel* packing, not byte order. Right-looking, wrong tool.
* **Boost.Regex** — would close the two gaps `script/regex.hpp` names (lookbehind
  and backreferences), but JS `RegExp` semantics are not Perl's, and swapping a
  working 455-line engine for a subtly different one risks p5.js for a feature no
  page here uses. Revisit only with tests pinning behaviour first.
* **Boost.Filesystem, Boost.Test, Boost.ProgramOptions, Boost.Process** —
  `std::filesystem` and the repo's own harness already cover the first two;
  nothing here parses argv or spawns a process.
* **Base64** — Boost has it only in `beast::detail`, a private namespace with no
  stability promise.

### Taken since (2026-08-08)

* **Boost.CRC** — `boost::crc_32_type` replaces the 256-entry CRC table
  `encode_png` rebuilt on every call. It IS the PNG polynomial (CRC-32/ISO-HDLC,
  0xEDB88320 reflected), so this is the same checksum from a library rather than
  from memory, and `tools/check/check-png.py` verifies the bytes with Python's own
  zlib independently of it. Header-only, so the cross-build needs nothing.
  **`encode_png` moved to `src/shell/images.cpp` first**: it was `inline` in a
  public header, which made it the odd one out beside the BMP/PNG/JPEG decoders,
  and the rule against third-party headers in public ones is the reason the
  split came before the swap rather than after it.
* **`boost::hash_combine` / `boost::hash`** — two hand-rolled FNV-1a loops
  (`style/computed.hpp`'s `hash_declarations`, `shell/svg.cpp`'s content and
  raster-cache keys, one of which open-coded `hash_combine`'s golden-ratio mix
  verbatim). FNV walks a string a byte at a time; this is the same argument
  `core/containers.hpp` already makes for preferring `boost::hash` to
  `std::hash`, applied where nobody had.

### Rejected AGAIN, on a measurement (2026-08-08)

* **Boost.Unordered's `boost::concurrent_flat_map`** — the obvious answer for
  `atom_table`, which is read-hot, write-rare and behind a `shared_mutex`. The
  profile says the atom table is **0.01% of a Phaser frame** and that interning
  does not appear at all. A lock-free version of nothing is nothing.
* **Boost.Bloom** — as a negative filter ahead of the prototype-chain walk. The
  chain is 2.25 levels deep and the change that removes the cost is interning
  property names as atoms, not putting a filter in front of the same string
  compare.
* **Boost.Charconv** — the obvious answer when `script/number_format.cpp`
  retired `std::to_string(double)`, `std::stod` and three printf conversions.
  It loses on the only ground that matters here: **the standard library already
  does this on both toolchains.** Every floating-point `std::to_chars` overload,
  including the format-and-precision forms, is present in libstdc++ (since GCC
  11) and exported from llvm-mingw's libc++ — verified in the cross toolchain's
  own `__charconv/to_chars_floating_point.h`, all nine of them
  `_LIBCPP_EXPORTED_FROM_ABI`. Boost.Charconv is a COMPILED library, so taking
  it would have meant a sixth `tools/build-*-mingw.sh`, another
  `find_package` component, a `CTBROWSER_CONFIG_DEPS` entry and raising the
  floor 1.80 -> 1.85, to buy nothing. Its case would return only on a toolchain
  whose `<charconv>` is incomplete.

**So the version floor stays at 1.80.** All three would have needed it raised
(1.84, 1.89 and 1.85), and that is nearly free here - `tools/mingw/build-boost-mingw.sh` reads the
tag out of the host's headers and brew ships 1.90 on both machines - but raising
a floor to admit a library the measurement says not to use is a cost with no
purchase. It is a one-line change whenever something earns it.

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

`tools/check/check-package.sh` is what catches the other half of this: an exported
target that links `Freetype::Freetype` needs a matching `find_dependency` in the
installed config, and stage 6 shipped without one.

## THERE IS NO CI (2026-08-08)

`.github/workflows/tests.yml` is deleted. Nothing runs the suite, the formatting
gate or the goldens automatically any more - **a person does, or nobody does.**

It had been **failing since 2026-08-04** and nobody was reading it, which is the
worst state for a gate to be in: red for a reason unrelated to the code, so
every real failure after it looked the same. The cause was that the `default`
preset pins `CMAKE_CXX_COMPILER` to `tools/clang-std-embed/bin/clang++`, which a
GitHub runner does not have - so it died at `project()` and never reached
`find_package(Boost)`, let alone a test.

That is also why it could not simply be repaired: the preset the tree actually
builds with wants a toolchain from a release, mimalloc and simdutf from brew,
plutosvg pinned to the sysroot's version, and ANGLE fetched - and the goldens
additionally want SDL3 *and* SwiftShader over Mesa (`docs/platform.md`). A
runner reproducing that is most of what the devbox already is.

**So the gate is `tools/remote-build.sh`**, and the discipline is manual:

```bash
./tools/format.sh --check          # formatting
./tools/remote-build.sh            # GCC 13, ANGLE, 72/72 - no SDL, so no goldens
# goldens need a box with SDL3; see docs/platform.md for the SwiftShader ICD
```

The devbox is the second set of assumptions CI used to be - a different compiler
and a different dependency set - so what was actually load-bearing about CI is
still there. What is gone is the part that ran without being asked. If that
matters again, a `pre-commit` hook running `format.sh --check` is the cheap half.

## FORMATTING (2026-07-27)

**`tools/format.sh`**, and `--check` to gate. `.clang-format`
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

**`frame` is broken into styles / layout / record / raster since 2026-08-08.**
It was one bucket for all four, so the profiler could say a frame was slow and
never which part of it was - and those four are exactly what the dirty level
chooses between, which makes their SUM the least informative way to report them.
A skipped stage reads 0, and that is the number to look at: a scroll or a caret
blink should leave three of them at zero, and "it didn't" is a regression in the
dirty-level design that nothing else in the tree would catch. The engine times
itself (`browser::last_frame_timing()`); the app layer only copies the numbers
out, so the split costs four clock reads on a path that then rasterises the
viewport.
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


## COMPARING AGAINST A REAL BROWSER (2026-07-28)

The goldens prove ctbrowser renders the same as it did yesterday. They say
nothing about whether it matches a browser, and parity with Chrome/Firefox is
the goal — so `tools/check/compare.py` opens the same page in ctbrowser AND a real
browser and sends both the same clicks and keystrokes, **one command at a
time**.

```bash
tools/check/compare.py setup                       # once: a venv holding Playwright
tools/check/compare.py start --engine=ctbrowse --engine=chrome \
                 --headed --delay 400 examples/pages/widgets.html
tools/check/compare.py click 132 99
tools/check/compare.py type Claude
tools/check/compare.py key Tab
tools/check/compare.py shot after-tab              # build/compare/shots/after-tab/*.png
tools/check/compare.py stop
```

`--headed --delay` is the point rather than a debugging aid: both windows are
open and the pace is human, so someone watching can say "that click missed"
while it is still happening and the next command can correct it. Leave them off
and the same verbs run headless and instantly.

**`--engine=`** picks who is driven — `ctbrowse` (alias `normal`), `chrome`
(alias `chromium`), `firefox` — repeatable and comma-accepting, default
`ctbrowse,chrome`. Each stands alone: `--engine=chrome` asks what a real
browser does before deciding what ctbrowser should do, and `--engine=ctbrowse`
drives the engine by hand with no venv and nothing downloaded.

**The engines cannot live in one invocation** — every command is a new process —
so `start` leaves a daemon holding them and every other verb is a one-line
client. Loopback TCP and a port file rather than a unix socket: the repo sits on
a DrvFs mount, which does not support unix sockets at all.

**`examples/cli/ctdrive.cpp` is the ctbrowser half**, and the two engine changes it
needed are worth knowing. `app_options::on_frame` is called every loop
iteration — `on_ready` fires once and the loop was otherwise closed, which is
why `ctbrowse` cannot be scripted — and commands are applied from it, on the
loop's own thread, because a browser driven from any other while this one ticks
and draws it is a data race. `app_options::caret_blink_ms` simply had no way
through: `browser_options` always had it, `run_app` never passed it.

**Coordinates, not selectors, and deliberately.** A selector would let each
engine resolve its own geometry, which hides the thing being hunted: when the
same click lands on different elements, that IS the finding. The first run
against `widgets.html` showed it — ctbrowser puts the name field at y=86 and
Chrome at y=60, so one click typed into the field and the other missed entirely.

**What is by design and should not be read as a difference:** ctbrowser's
`Math.random` is a fixed-seed xorshift and a real browser's is not; its
`wheel_step` (53), `wheel_lines` (3) and `scrollbar_width` (15) are its own
numbers; and antialiasing and hinting will never match. Fonts are handled —
`compare.py` points the reference browser at the repo's own Tinos/Fira
Sans/Cousine through `FONTCONFIG_FILE`, since otherwise every glyph differs and
substitution buries everything else. `--system-fonts` opts out.

Not a ctest, for the reason the benchmarks are not: a browser-versus-browser
diff should be read, not silently failed.

## FONTS IN THE BINARY (2026-07-28)

`-DCTBROWSER_EMBED_FONTS=ON` bakes the twelve vendored OFL faces into the
executable with `#embed`, so it runs from anywhere with no `fonts/` beside it.
Off by default.

The registry-before-filesystem order was always designed for this — *"a binary
that baked them in never touches the disk"* — and nothing had ever baked them
in. `register_embedded_fonts` puts them in the registry under the same names
`use_real_fonts` asks for, so nothing downstream knows which side they came
from.

**OPPORTUNISTIC, by probe rather than by version check.** `#embed` is C23 and
C++26: clang has had it since 19 and the llvm-mingw cross toolchain has it, but
GCC 13 — which builds this tree perfectly well — does not. CMake compiles the
directive and looks; when it is not there the function registers nothing, says
so through `have_embedded_fonts()`, and the loader reads the directory exactly
as before.

Three things that cost time if you assume otherwise:

- **`#embed <name>` with `--embed-dir`, not `#embed "name"` with `-I`.** The
  quoted form searches beside the source file and ignores `--embed-dir`, and
  `-I` does not feed the embed search path at all. They are separate paths.
- **`-pedantic -Werror` rejects it.** In C++23 mode clang calls `#embed` a C23
  extension and the repo's flags turn that into an error, so the one file that
  uses it silences `-Wc23-extensions` locally — and nowhere else.
- **It is 5.2 MB, per binary.** That is why it is off by default: every test
  executable links the shell, so turning it on unconditionally would put about
  160 MB of fonts into `build/` for something only a distributable build wants.

**The `windows` preset turns it ON**, which is what it is for: those exes
already carry a static SDL3 and no DLL, and the last thing they needed beside
them was a font directory. `windows-dist` therefore stops copying `fonts/` -
shipping 5.2 MB nothing reads - and copies only the OFL licences, which still
have to travel. `examples/assets/fonts` is untouched either way: PressStart2P is
a PAGE's font, fetched by `@font-face` through a URL, and embedding covers the
three families `use_real_fonts` loads and nothing else.

Verified end to end by running two builds of `ctdrive` from a directory with no
`fonts/` in it: the ordinary one falls back to font8x8 and draws blocky
bitmaps, the embedded one renders Tinos. And on the Windows side by rendering
`widgets.exe` from a dist with no `.ttf` in it at all - bold serif heading,
Fira Sans labels, Cousine textarea.
