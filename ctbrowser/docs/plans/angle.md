# ANGLE as the WebGL back end

**The proposal: stop implementing OpenGL ES and start calling one.** ANGLE is
what Chrome and Firefox put behind WebGL — a complete GLES 2.0/3.0/3.1
implementation over Vulkan, D3D11, Metal or desktop GL. This engine currently
implements GLES itself, in software, and interprets the shaders. Replacing that
with ANGLE is the largest single change ever proposed for this tree, and this
document is what it would cost.

**Status: STAGE 0 IS DONE, BOTH PLATFORMS, and it passed.** ANGLE builds and
renders headless on Linux and Windows; it is **186x to 322x** the software
interpreter; an llvm-mingw `.exe` links its DLLs; and **both platforms render the
identical pixel**, which is the answer to the golden question this plan was most
worried about.

Published: `github.com/alexios-angel/angle`, release `ctbrowser-angle-25c80ccab4`
— five files per platform, the headers, and what each of them is for.

## Stage 0, measured 2026-08-04

Built from the fork at `alexios-angel/angle`, Vulkan back end only, on the
devbox. The spike is `tools/angle/spike.cpp` and its recipe is beside it:
surfaceless EGL, one full-screen triangle with a fragment shader that does real
per-pixel work, read back into memory the way a canvas would need.

```
EGL 1.5  vendor=Google Inc. (Google)
GL_RENDERER = ANGLE (Google, Vulkan 1.3.0 (SwiftShader Device (Subzero)), SwiftShader driver-5.0.0)
GL_VERSION  = OpenGL ES 3.1 (ANGLE 2.1.28528)

DRAW ONLY     191.53 M frag/s   (1.369 ms per 512x512 pass)
WITH READBACK 176.33 M frag/s   (1.487 ms per pass)

the software interpreter, for comparison:  1.03 M frag/s
```

**186x, and on SOFTWARE VULKAN.** That is SwiftShader — ANGLE's own CPU
implementation, no GPU involved at all. The comparison is therefore
software-against-software, which is the fairest one available on this machine
and a floor rather than a ceiling: `docs/platform.md` records that a Linux
binary here sees no GPU, and the Windows build gets an Intel Arc.

**And the readback is NOT the problem this document expected.** It costs 8% -
0.118 ms of a 1.487 ms pass at 512x512. The plan said it "could eat the win" and
it does not; that concern is retired.

**SwiftShader also answers the golden question better than lavapipe would.** It
ships WITH ANGLE rather than with the host's Mesa, so pinning it for the golden
tests makes the deterministic stack self-contained - one fewer thing that can
differ between a developer's machine and CI.

### Three things had to be got right, and each cost a build

* **ANGLE's own source fails its own `-Werror`**: `FixedVector.h` trips
  `-Wunsafe-buffer-usage` under the bundled clang. `treat_warnings_as_errors=false`.
* **`use_custom_libcxx=false` broke it.** Setting it - to match this tree's
  libstdc++ - failed in `Color.inc` on `std::strong_order`. It was never
  necessary: **EGL and GLES are C APIs**, so ANGLE's C++ standard library never
  crosses the boundary. This retires a risk this document raised, and see the
  Windows section for how far that reasoning carries.
* **`eglGetPlatformDisplayEXT` takes `EGLint`**, not `EGLAttrib`. Building the
  64-bit array and casting made every second word read as zero; the only symptom
  was `EGL_NO_DISPLAY` and nothing logged. That one was mine, not ANGLE's.

## Stage 0, the Windows half — DONE 2026-08-04

Built natively with clang-cl and the Windows SDK, then linked from llvm-mingw:

```
                     Linux x86_64      Windows x64
draw only            192.76 M frag/s   332.04 M frag/s
with readback        176.21 M frag/s   235.18 M frag/s
the interpreter        1.03 M frag/s     1.03 M frag/s
```

**Both render 162,91,138 from the same shader.** SwiftShader is deterministic
across platforms, so the four WebGL goldens can survive the move: the stack to
pin for tests is ANGLE-over-Vulkan-over-SwiftShader, and it ships WITH ANGLE
rather than depending on the host's Mesa.

### Four things had to be got right, and each cost a build

* **The pinned Windows SDK.** Chromium pins `10.0.28000.0`; the machine had
  `10.0.26100.0`. `gn gen` fails naming a version nobody chose.
* **`NTDDI_VERSION=NTDDI_WIN11_BR`** is defined only by that newer SDK. Against
  an older one it evaluates to nothing and every type behind a version guard
  disappears — surfacing as `unknown type name 'FILE_INFO_BY_HANDLE_CLASS'` from
  the SDK's own header. Both are OVERRIDABLE in the fork now, defaults
  unchanged, as a patch to Chromium's `build/` submodule rather than to ANGLE.
* **The consumer must link `-static`.** llvm-mingw links its own libc++ and
  libunwind dynamically, so the exe would not start for want of `libc++.dll` —
  its runtime, not ANGLE's. `cmake/toolchain-windows-x86_64.cmake` already does
  this for the engine.
* **`libvulkan.so.1` / `vulkan-1.dll` is ANGLE's own loader and must ship.**
  Omitting it fails with `Internal Vulkan error (-3)`, and omitting the ICD
  manifest fails with `Extension not supported: VK_KHR_surface` — neither names
  the missing file. This one was caught by testing the SHIPPING set rather than
  the build directory, which is the only reason it is not in the release.

## The Windows question — DECIDED 2026-08-04

**ANGLE is built natively on Windows and the llvm-mingw `.exe` links its DLLs
through the C API, dynamically.** That is the decision; what follows is what it
means.

### Why it works, and it is the same argument twice

`libEGL.dll` and `libGLESv2.dll` export **C** functions. On x86-64 there is no
name decoration for C, so an import library generated from the DLL - or the
`.lib` clang-cl already emits - links from mingw like any other C library, and
`LoadLibrary`/`GetProcAddress` is the fallback that always works. **The C++ ABI
never meets the boundary**, which is exactly what made `use_custom_libcxx`
irrelevant on Linux. One argument, two platforms.

### What it changes about how this repository ships

`docs/build.md` and the mingw scripts say an application should be **one .exe**,
and that is why Boost, zlib, libpng, libjpeg-turbo, mimalloc, simdutf and
cpptrace are all static. This relaxes it - but not from nothing: `windows-dist`
already collects **SDL3.dll** beside the exes and calls it "the ONE runtime
dependency". It becomes a few.

**A non-component build is what should ship.** The spike used
`is_component_build=true`, which produces eight or so DLLs - libc++, abseil,
zlib and the rest as separate objects. With it FALSE those link INTO
`libGLESv2.dll`, and the dist carries two or three files instead of eight. That
is a build-argument change and it belongs in the shipping recipe rather than in
the spike.

### And the build machine changes with it

`tools/remote-build.sh windows` cross-compiles everything on the Linux devbox,
which is what makes the sixteen verified binaries reproducible from one place.
ANGLE cannot join that: Chromium's GN has no llvm-mingw toolchain, so **the
ANGLE DLLs are built on a Windows host and the engine is still cross-compiled on
the devbox.** Two producers for one dist.

That is a real cost and it should be paid deliberately: the DLLs become a PINNED
ARTEFACT, built once per ANGLE revision and checked in or published the way
`clang-std-embed` releases already are - not something every developer rebuilds.
Otherwise "byte-identical on both platforms" quietly becomes "byte-identical if
your ANGLE matches mine".

## The Windows question, as it stood before that decision

**It cannot be answered on the devbox**, and that is worth stating rather than
leaving as an unfinished task. Chromium's GN has no llvm-mingw toolchain at all,
and cross-compiling to Windows from Linux needs the MSVC toolchain and the
Windows SDK, neither of which is here.

**But the C-API argument above changes the shape of the risk.** This document
originally said llvm-mingw and clang-cl are different C++ ABIs and therefore
incompatible. That is true of C++ and irrelevant here: `libEGL.dll` and
`libGLESv2.dll` export **C** functions, and a mingw binary can link an import
library or `LoadLibrary` them exactly as any other C DLL. The same reasoning
that made `use_custom_libcxx` a non-issue makes the Windows ABI a non-issue.

So the Windows path is probably:

1. build ANGLE **natively on Windows** with clang-cl and the Windows SDK, or
   take a prebuilt one, and
2. link the llvm-mingw `.exe` against the DLLs through their C API.

That is a different pipeline from `tools/remote-build.sh windows`, which builds
everything on the Linux devbox - so it is real work and a real change to how
this repository ships. **It is now the only open question in stage 0**, and it
should be answered before anything is deleted.

## What it replaces, counted

| | lines | what happens to it |
|---|---|---|
| `ctbrowser/lib/Shell/page/webgl.cpp` | 1,202 | **deleted** — the GL state machine, draw paths and framebuffers become ANGLE's |
| `ctbrowser/lib/Raster/glsl.cpp` + `glsl_eval.cpp` + `glsl_preprocess.cpp` | ~2,900 | **no longer on the WebGL path** — ANGLE compiles the shaders |
| `ctbrowser/lib/Raster/softgl.cpp` | 377 | **deleted** — its only caller is `webgl.cpp` |
| `ctbrowser/lib/Raster/spirv.cpp` | 733 | **dead** — it exists to feed a GPU path ANGLE would own |
| `ctbrowser/lib/Raster/glsl_translate.cpp` | 471 | **dead** — ANGLE's translator does exactly this job |
| `ctbrowser/lib/Shell/bindings/webgl.cpp` | 1,264 | **stays**, and this is the important row |

**About 5,700 lines go and 1,264 stay**, and the split is not arbitrary.
`webgl_bindings.cpp` is the JavaScript surface — 72 `method(...)` entry points,
the argument coercions, the wrapper objects, and the refusal lists that
`ctbrowser/test/corpus/webgl2/webgl2-api.txt` pins. None of that is about how GL is implemented; all of
it is about what a page can call. It would forward to real GLES instead of to
`webgl_context`, and the ratchets that measure it would not know the difference
except by getting further.

## What ANGLE gives, from its own table

OpenGL ES **3.0 complete** on D3D11, desktop GL, GL ES, Vulkan and Metal; **3.1
complete** on desktop GL, GL ES and Vulkan; 3.2 in progress. This engine's
WebGL 2 support is a measured subset — `docs/history/webgl2.md` records what was
scoped out and `ctbrowser/test/corpus/webgl2/webgl2-api.txt` records that transform feedback, 3D
textures, MRT, samplers, queries and sync are all still refused by name.

**ANGLE would close all of those at once**, and it is the reason the proposal is
worth taking seriously rather than a preference about implementations.

Licence: BSD-style, ANGLE Project Authors. Compatible.

## The four costs, hardest first

### 1. The build, and it is the top risk

> **Stage 0 answered the Linux half of this and CORRECTED the ABI claim below.**
> ANGLE builds and runs here; and EGL/GLES being C APIs means the llvm-mingw
> versus clang-cl ABI difference does not matter. What remains is the build
> PIPELINE, not the ABI. See "The Windows question" above. The original
> reasoning is kept because it is why the spike was run.

ANGLE has **no CMake anywhere**. It is GN plus `depot_tools` plus `gclient`,
which is the Chromium build system. There is no brew formula and no apt package.

This tree pins every dependency and builds it into an llvm-mingw sysroot with
CMake — `build-boost-mingw.sh` and its five siblings do exactly that, and the
newest of them, `build-shaderc-mingw.sh`, configured and built in one command.
ANGLE will not.

**And the Windows half is worse than the Linux half.** ANGLE on Windows is built
with clang-cl against the MSVC runtime; this engine cross-compiles with
llvm-mingw. Those are different ABIs for C++ — different exception model,
different standard library, different name mangling. The D3D11 back end is the
most MSVC-shaped part of ANGLE, so **the Vulkan back end is the one to aim at on
both platforms**, and whether ANGLE's Vulkan back end builds under llvm-mingw at
all is the first question a spike has to answer. If it does not, this proposal
stops there for Windows, and `tools/remote-build.sh windows` — which produces
the sixteen verified binaries — has nothing to produce.

### 2. The goldens, and there is a way to keep them

Sixteen goldens byte-compare across Linux and Windows. Four of them go through
WebGL: `webgltriangle`, `p5webgl`, `babylonscene`, `babylonorbit`. A GPU
rasteriser is not byte-identical to another GPU rasteriser — fill rules,
interpolation precision and filtering all differ legitimately between drivers —
so on the face of it those four goldens cannot survive.

**But there is a combination that would keep them: ANGLE over Vulkan over
lavapipe.** lavapipe is Mesa's software Vulkan device, it is deterministic, and
it is available on both platforms. Pinning *that stack* for the golden tests
while shipping hardware Vulkan to users would keep the byte comparison exactly
as it is — the tests would be measuring ANGLE's own determinism rather than a
driver's.

That is worth designing for rather than discovering: it decides whether the
tests keep their teeth. `docs/platform.md` already records that a Linux binary
here sees only lavapipe, so half of it is the status quo.

### 3. There is no window, and the pixels have to come back — MEASURED AT 8%

A page's canvas is a `paint::bitmap` that the software painter composites. ANGLE
renders into a GL framebuffer, so every frame needs `glReadPixels` into that
bitmap — a GPU-to-CPU transfer, which is the one direction GPUs are bad at.

**Stage 0 measured this and it does not eat the win: 8%**, 0.118 ms of a
1.487 ms pass at 512x512. The paragraph below is what was expected beforehand
and is kept because the expectation was wrong in a useful direction.

**This could eat the win**, and the plan must not assume otherwise. A 420x300
canvas is 504 KB per frame; at 60 Hz that is 30 MB/s of readback plus a pipeline
stall. Against an interpreter at 1.03 M fragments per second it is still an
enormous improvement, but the honest shape is "much faster, with a fixed cost
per frame that small canvases feel most" — and it is measurable before anything
is committed to.

The context itself needs no window: EGL pbuffer or surfaceless plus an FBO.

### 4. The SDL-free rule, which is enforced

`ctbrowser/test/lint/api_surface` lints that only `app/app.cpp` knows SDL exists, and that
no third-party header appears in a public header. ANGLE brings EGL and GLES
headers, and they would have to be confined the same way — one `.cpp` owning
them behind a two-function header, which is the pattern `core/cpu_time.hpp` and
the image decoders already follow. This is a constraint the tree has machinery
for rather than a problem, but the lint will fail loudly if it is ignored.

## What this buys that the current path cannot

* **The whole of GLES 3.1**, including everything `webgl2-api.txt` records as
  refused. Babylon's ratchet stops at 10/12 partly on features that are simply
  not implemented here; several would arrive at once.
* **Correctness by delegation.** Every shader-language question — precision,
  built-in behaviour, integer semantics, the `uint` mapping this tree currently
  documents as a deliberate lie — becomes ANGLE's answer rather than ours.
* **Hardware acceleration**, which is the reason the question was asked. The
  interpreter is 1.03 M fragments per second; a real device is two to three
  orders faster.

## What it costs that is not build effort

* **The engine stops being one.** A large part of what this repository is — a
  GLES implementation and a shader interpreter, written here and tested against
  three real corpora — becomes a call into somebody else's. That is the right
  trade for a browser and it should be made with eyes open rather than as a
  refactor.
* **The measuring apparatus survives, which is the saving grace.** p5 12/12,
  Phaser 10/10, WebGL 2 10/10, Babylon 10/12, four API surfaces and sixteen
  goldens are all backend-agnostic: they drive pages and read pixels. They would
  re-run against ANGLE unchanged and say immediately whether it is better. **Very
  little of this tree's testing is wasted by this change**, which is the
  strongest argument that it can be attempted safely.

## Staging

### 0 — the spike that decides it
Build ANGLE for Linux with GN, make a surfaceless EGL context, draw one triangle
into a pbuffer, read it back into a `paint::bitmap`, and measure fragments per
second against the 1.03 M the interpreter manages. **Then try the same build
under llvm-mingw for Windows.** If that fails, the answer to this whole document
is "Linux only", and that has to be known before anything is deleted.

### 1 — the context, behind the existing interface — DONE 2026-08-04
`raster/gles.hpp` + `gles.cpp`: an offscreen ES 3.1 device that owns the EGL
display, the pbuffer and the readback. **No EGL or GLES type appears in the
header**, so `ctbrowser/test/lint/api_surface` stays satisfied and nothing that includes it
inherits them — the `cpu_time.hpp` pattern.

`tests/gles_basics.cpp` is the whole of what it claims: a device comes up,
reports ES 3, a clear reaches the pixels, a second clear replaces the first
(which is what catches a stale readback), and two devices coexist with
`make_current` putting the first one back.

**Nothing in the engine calls it**, deliberately — stage 2 is where a switch
appears. `77/77 with it OFF and 77/77 with it ON`, which is the point of it being
optional: `tools/fetch-angle.sh` pulls the pinned release, and a checkout that
has not run it builds and passes exactly as before.

Two details worth keeping:

* **The bitmap is ARGB and GL hands back RGBA, bottom row first.** Both are
  fixed in `read_pixels`, and both produce a picture that is *almost* right when
  they are not - swapped red and blue, or upside down - which survives any test
  that only counts pixels.
* **`libvulkan.so.1` and the ICD manifest are found through the link rpath**, so
  no `VK_ICD_FILENAMES` is needed. Verified by running without it.

### 2 — one binding at a time — FIRST INCREMENT DONE 2026-08-04
`browser::prefer_angle_webgl` picks the back end before a page loads;
`webgl_context` forwards to a real GLES device when it is on. **Both paths live
at once**, which is what makes this reversible.

**The calls `webgl-triangle.html` makes are forwarded, and both back ends draw
the identical picture** — 8721 pixels of geometry in the identical colour,
compared by `tests/webgl_angle.cpp`. Not a byte comparison: that is stage 3.

**The ledger is the important half.** Thirteen entry points that ANGLE mode does
NOT forward record themselves rather than doing nothing, and the test asserts
the list is empty for the page it runs. That check existed and was VACUOUS for
an afternoon - `note_unforwarded` was written and never called - which is
exactly how the real bug survived: the page sets a `mat3` through
`uniformMatrix3fv`, nothing forwarded it, the uniform stayed all zeros, every
vertex collapsed to the origin, and the canvas came back a perfectly plausible
empty scene with `getError` clean.

Two smaller things worth keeping:

* **`getParameter` still reads software state.** On ANGLE, `CURRENT_PROGRAM` and
  `ARRAY_BUFFER_BINDING` answer from the software context, so a page that asks
  gets the wrong answer. Not yet forwarded, and on the ledger.
* **The VAO is bound at context creation, not at draw.** Creating it lazily in
  `draw_arrays` discards every attribute enable and pointer set before it -
  which draws nothing and reports nothing.

### 3 — the goldens — THE QUESTION IS ANSWERED, and it is YES
**`webgltriangle` rendered through ANGLE is BYTE-IDENTICAL between Linux and the
Windows cross build.** Not one pixel of a whole scene differs. That was the
thing this plan was most worried about, and it settles it: ANGLE over
SwiftShader can be goldened exactly as the software rasteriser is.

Also measured: **ANGLE is deterministic run to run** on the same machine, which
is the cheaper precondition and worth checking before the expensive one.

**A SECOND GOLDEN, NOT A TOLERANCE.** The two back ends differ on
`webgltriangle` in 1.0% of pixels - the edges of the triangle, where two
rasterisers legitimately disagree. Comparing them to each other would need a
fudge factor, and a fudge factor is a golden that has stopped catching things.
Each back end holds still against ITSELF instead: `ctbrowser/test/golden/` for software,
`ctbrowser/test/golden/angle/` for ANGLE, and `CTBROWSER_WEBGL=angle` selects which.

**AND ONLY ONE PAGE IS GOLDENED, because only one renders.** p5webgl,
babylonscene and babylonorbit were goldened first and the files were OPENED: all
three were BLANK WHITE. They draw with `drawElements`, which stage 2 records as
unforwarded and does not forward - so each page runs, reports no error, and
paints nothing. **A run reporting "100% tests passed" would have committed three
goldens of an empty canvas.** They come back one at a time as the forwarding
does.

### 4 — the corpora — STARTED, and it found the next class of bug
Forwarded: `drawElements`, textures (`createTexture`, `bindTexture`,
`activeTexture`, `texImage2D`, `texParameteri`, and the bitmap upload path an
`<img>` or a canvas takes), `depthFunc`, `depthMask`, `blendFunc`.

**Both p5's and Babylon's ledgers are now EMPTY - every call they make is
forwarded - and NEITHER DRAWS.** That is the finding, and it is worth more than
the forwarding was:

```
p5-webgl        errors={}  draws={}
babylon-scene   errors={}  draws={"linkProgram":3}
```

No GL errors. p5 issues no draw at all; Babylon links three programs and then
issues none. **They are DECIDING not to draw**, and the reason is that
`getProgramParameter(ACTIVE_ATTRIBUTES)` and its neighbours answer from
`webgl_context`'s own tables - which ANGLE mode never fills, because the program
belongs to GL. A library that ENUMERATES a program instead of asking for names
it already knows is told the shader declares nothing, so it binds nothing and
draws nothing.

`webgl_bindings.cpp` already carries a comment saying this exact class of bug
bit once before, for the software path, in the same function.

**THE LEDGER DOES NOT CATCH THIS, and that is a hole in the instrument rather
than an oversight in the list.** It records ACTIONS that were not forwarded. A
QUERY that answers from the wrong place is not missing - it returns, promptly,
with a confident wrong answer. Anything stage 4 does next has to cover the
introspection calls: `getProgramParameter` for the ACTIVE_* pnames,
`getActiveAttrib`, `getActiveUniform`, and `getParameter` for the bindings that
`webgl_angle.cpp` already noticed answering from software state.

**The introspection is forwarded now and p5 DRAWS.** `getProgramParameter`'s
ACTIVE_* pnames, `getActiveAttrib` and `getActiveUniform` ask the program
itself - `glGetActiveAttrib` and friends - instead of tables ANGLE mode never
fills. p5's WEBGL cube and sphere appear where the canvas was empty.

`active_variable` carries a **GL type code** rather than a `glsl::type` now,
which both back ends can answer in and which is what `getActiveUniform` reports
to a page anyway. That also unpicks one of the threads tying the bindings to the
software GLSL front end - see stage 5.

**Babylon COMPILES AND DRAWS now** - 267 draw calls, `isReady()` true,
`material.isReady()` true - and it took one line of translation.

Its processed vertex shader arrives with fifty lines of `#define`, then
`layout(std140, column_major) uniform;`, and **no `#version` directive at all**.
ANGLE compiles that as ESSL 1.00, where the word does not exist:

```
VERTEX SHADER ERROR: 0:54: 'layout' : syntax error
```

This engine's own front end is LENIENT and accepts `layout(` whatever the
version claims, which is why the software path never noticed. `webgl.cpp`
supplies `#version 300 es` when a shader uses `layout(` and says nothing about
its version - narrowly, because `attribute` and `varying` are ES 1.00 spellings
that ES 3.00 REMOVED, so promoting p5's shaders would break what already works.

**BABYLON ISSUES DRAWS AND THEY PAINT NOTHING**, and the earlier claim here -
that it "draws" - was about CALL COUNTS and not pixels. Corrected: the example
issues 150+ `drawElements` with real index counts (36 for the box, 8112 for the
sphere) and the canvas still shows only the clear colour. Under a test harness
it issues 267 and reports `material.isReady()`; nobody had looked at that
harness's pixels either.

**The cause is named and is the same shape as the last two.** Babylon on WebGL 2
delivers every uniform through UNIFORM BUFFER OBJECTS, and
`uniformBlockBinding` and `bindBufferBase` are on the unforwarded ledger - they
record themselves and do nothing. So every matrix in the block reads ZERO and
the geometry collapses to a point, exactly as it did when `uniformMatrix3fv` was
missing and exactly as it did before UBOs existed in the software path at all
(see ctbrowser/test/corpus/webgl2/webgl2-ratchet.txt, which records the same collapse from the other
side).

The ledger DID name it this time - the entries have been there since stage 2 -
and it went unread because Babylon could not reach that code until the
`#version` fix let its shaders compile. **A ledger only helps when it is
checked after every change that lets a page get further.**

`uniformBlockBinding`, `bindBufferBase` and `getUniformBlockIndex` are forwarded
now - **and the pixels did not change.** Still one colour. So the UBO calls were
a real gap and were NOT the whole cause either, which is the third time on this
page that a named, plausible, genuinely-missing piece turned out not to be the
last one.

**The ledger is now EMPTY for this page**, and the pixels still did not change.

Reading it was worth doing anyway, because it refuted the guess above rather
than confirming it: `bindFramebuffer`, `framebufferTexture2D` and the two
instanced draws are **never called** by babylonscene, so the RenderTargetTexture
theory was wrong. The only entry the run produced was `cullFace`.

It also exposed a gap the ledger COULD NOT SEE: `front_face` set software state
and forwarded nothing while never noting itself unforwarded. Both are forwarded
now. A ledger only covers the calls that remember to record themselves, so an
empty ledger is weaker evidence than it looks.

WHERE THIS LEAVES IT: every call the page makes reaches ANGLE, 267 draws issue
with real index counts, and the canvas is 93,600 pixels of exactly the clear
colour. The next step must SPLIT the problem rather than name another suspect -
read pixels straight off the `gles::device` after a draw, bypassing the canvas
composite. That answers whether GL painted and the readback lost it, or GL
painted nothing, and no amount of reasoning about the call list will.

## THE CONTROL, DONE PROPERLY: it is ANGLE, and only ANGLE

One binary, one page, one variable:

| | |
|---|---|
| `babylonscene` | **2200 colours** |
| `babylonscene` with `CTBROWSER_WEBGL=angle` | **1 colour** |

So Babylon renders correctly on the software path and paints nothing on ANGLE.
That is a clean A/B with a working oracle, which is what this needed from the
start.

TWO WRONG CONCLUSIONS GOT HERE, both from the same error - changing more than
one thing between runs. "Neither backend paints" compared an ANGLE run against a
remembered software run. "The example binary is broken" compared an ANGLE run
against `ctbrowse` runs that never set the variable at all. Each time the
harness, the page and the backend moved together and the difference was
attributed to whichever one was being thought about.

THE RULE, which is cheap and would have saved all of it: change ONE thing between
two runs and print both numbers side by side in the SAME command. Not
"run it a second way" - that was the previous lesson and it was too vague to
prevent this.

Next: the A/B is stable and reproducible, so bisect the frame. The device probe
already says GL produces no fragments, so the question is what differs in the
state ANGLE sees - viewport, depth range, program in use, or attribute buffers -
at the first `drawElements` of a scene that works and one that does not.

## SUPERSEDED: "the page was never broken, the EXAMPLE BINARY is"

`examples/pages/babylon-scene.html` rendered through `ctbrowse` gives **2200
distinct colours**. It paints. The same page through the `babylonscene` example
binary gives ONE.

So Babylon is fine, the bindings are fine, ANGLE is fine, and the software
rasteriser is fine. Everything below this line was looking in the wrong place,
and the mistake was methodological rather than technical: the page was only ever
run through ONE harness, so every "the page does not paint" reading was really
"that binary does not paint" and nobody had checked which. A second harness costs
a minute and would have redirected four commits of work.

THE STANDING LESSON, since this is the same shape as the two-URL-parsers and
two-base64-decoders findings this tree has already paid for: when something does
not work, run it a second way BEFORE forming a theory about why. The next step is
to diff `examples/corpus/babylonscene.cpp` against `examples/cli/ctbrowse.cpp` - options,
canvas sizing, frame pumping - not to read any more GL.

The measurements below remain TRUE and are worth keeping; they were just aimed at
the wrong target.

**MEASURED: GL ITSELF PAINTS NOTHING (in the example binary).** `CTBROWSER_GL_PROBE=1` reads the device's
own framebuffer after every `drawElements` and it holds ONE colour throughout, so
the readback and the canvas composite are exonerated - the draws produce zero
fragments inside ANGLE.

Put beside a fact already established, that reframes this investigation
entirely: **the software rasteriser does not paint this page either.** Two
rasterisers sharing no code fail identically, which means the fault is almost
certainly UPSTREAM OF BOTH - in what Babylon computes, or in what the bindings
feed it - and every hour spent on the ANGLE facade was looking in the wrong
layer. Four wrong guesses in a row is what that looks like from inside.

And there IS a working oracle, which is the cheapest possible next step:
`tools/corpus/webgl2-ratchet.py` reads 10/10 and its rung is "Babylon renders a scene".
So a Babylon scene DOES paint somewhere in this tree. Diff the ratchet's page
against `examples/pages/babylon-scene.html` - camera, canvas size, engine
options, the frame loop - rather than reading any more GL code. The difference
between a page that works and one that does not beats another suspect.

The superseded note:

**Six entries remain on the ledger**, and the next attempt should start by
printing them for the babylonscene page specifically rather than reasoning about
which matters - that is what the ledger is for, and reasoning has now been wrong
three times running. The one structural suspicion worth recording: Babylon
renders through a `RenderTargetTexture` and post-process chain, and
`bindFramebuffer`/`framebufferTexture2D` are among the six - a scene drawn into a
framebuffer nobody reads back would look exactly like this.

The order stays p5, then Phaser, then Babylon; the ratchets say where each gets
to, and anything that goes BACKWARDS is a blocker rather than a note.

### 5 — retire the software path — REQUESTED, and blocked on stage 4
**The decision is made: the software GLSL interpreter goes, ANGLE is the only
back end, and "safe mode" becomes a runtime choice of SwiftShader over whatever
device ANGLE would pick.** Worth noting that the current code has this
backwards - `open_display` asks for SwiftShader unconditionally - so the default
has to become ANGLE's own choice and SwiftShader the deliberate fallback.

**It cannot happen until stage 4 finishes.** Today p5 draws through ANGLE and
Babylon does not; deleting the software path now would take p5 12/12, WebGL 2
10/10 and Babylon 10/12 to zero and leave three goldens with nothing to compare.
The order is: finish the forwarding, get each corpus rendering, re-golden, THEN
delete.

What goes when it does: `raster/glsl.cpp`, `glsl_eval.cpp`, `glsl_preprocess.cpp`,
`softgl.cpp`, `spirv.cpp`, `glsl_translate.cpp` and their headers, plus the
software half of `webgl.cpp`.

### 5 (original) — retire the software path
Only after 2, 3 and 4 are green on both platforms, and only if the readback cost
measured in stage 0 held up at real canvas sizes. **The software rasteriser is
kept until then, and possibly for ever as the reference the goldens compare
against** — deleting the oracle is not part of adopting a driver.

## The decision this plan does not make

**Whether ANGLE builds for llvm-mingw is unknown**, and everything past stage 0
depends on it. This tree ships a Windows binary that is verified byte-identical
to the Linux one, sixteen times over; a change that can only be made on one
platform is a different proposal from this one, and should be re-argued rather
than assumed.
