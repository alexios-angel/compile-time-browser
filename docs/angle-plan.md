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
| `src/shell/webgl.cpp` | 1,202 | **deleted** — the GL state machine, draw paths and framebuffers become ANGLE's |
| `src/raster/glsl.cpp` + `glsl_eval.cpp` + `glsl_preprocess.cpp` | ~2,900 | **no longer on the WebGL path** — ANGLE compiles the shaders |
| `src/raster/softgl.cpp` | 377 | **deleted** — its only caller is `webgl.cpp` |
| `src/raster/spirv.cpp` | 733 | **dead** — it exists to feed a GPU path ANGLE would own |
| `src/raster/glsl_translate.cpp` | 471 | **dead** — ANGLE's translator does exactly this job |
| `src/shell/webgl_bindings.cpp` | 1,264 | **stays**, and this is the important row |

**About 5,700 lines go and 1,264 stay**, and the split is not arbitrary.
`webgl_bindings.cpp` is the JavaScript surface — 72 `method(...)` entry points,
the argument coercions, the wrapper objects, and the refusal lists that
`tests/webgl2-api.txt` pins. None of that is about how GL is implemented; all of
it is about what a page can call. It would forward to real GLES instead of to
`webgl_context`, and the ratchets that measure it would not know the difference
except by getting further.

## What ANGLE gives, from its own table

OpenGL ES **3.0 complete** on D3D11, desktop GL, GL ES, Vulkan and Metal; **3.1
complete** on desktop GL, GL ES and Vulkan; 3.2 in progress. This engine's
WebGL 2 support is a measured subset — `docs/webgl2-plan.md` records what was
scoped out and `tests/webgl2-api.txt` records that transform feedback, 3D
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

`tests/api_surface` lints that only `shell/app.cpp` knows SDL exists, and that
no third-party header appears in a public header. ANGLE brings EGL and GLES
headers, and they would have to be confined the same way — one `.cpp` owning
them behind a two-function header, which is the pattern `core/cpu_time.hpp` and
the image decoders already follow. This is a constraint the tree has machinery
for rather than a problem, but the lint will fail loudly if it is ignored.

## What this buys that the current path cannot

* **The whole of GLES 3.1**, including everything `webgl2-api.txt` records as
  refused. Babylon's ratchet stops at 8/12 partly on features that are simply
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
  Phaser 10/10, WebGL 2 10/10, Babylon 8/12, four API surfaces and sixteen
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

### 1 — the context, behind the existing interface
An `egl_context` that owns the EGL display, the pbuffer and the readback, behind
a header that mentions no ANGLE type — the `cpu_time.hpp` pattern. Nothing calls
it yet.

### 2 — one binding at a time
`webgl_bindings.cpp` gains a switch: forward to ANGLE or to `webgl_context`.
**Both paths live at once**, and the WebGL ratchet runs against each. That is
what makes this reversible and what turns "is ANGLE better" into a measurement
rather than a belief.

### 3 — the goldens
Pin ANGLE-over-Vulkan-over-lavapipe for the four WebGL goldens and confirm they
are byte-identical across Linux and Windows. If they are not, the tolerance
comparison has to be designed before going further.

### 4 — the corpora
p5, Phaser and Babylon, in that order. The ratchets say where each one gets to;
anything that goes BACKWARDS is a blocker, not a note.

### 5 — retire the software path
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
