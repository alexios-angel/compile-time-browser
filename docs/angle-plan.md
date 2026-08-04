# ANGLE as the WebGL back end

**The proposal: stop implementing OpenGL ES and start calling one.** ANGLE is
what Chrome and Firefox put behind WebGL — a complete GLES 2.0/3.0/3.1
implementation over Vulkan, D3D11, Metal or desktop GL. This engine currently
implements GLES itself, in software, and interprets the shaders. Replacing that
with ANGLE is the largest single change ever proposed for this tree, and this
document is what it would cost.

**Status: planned, nothing started.** The measurements below are from this tree
and from ANGLE's own build documentation, taken 2026-08-03.

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

### 3. There is no window, and the pixels have to come back

A page's canvas is a `paint::bitmap` that the software painter composites. ANGLE
renders into a GL framebuffer, so every frame needs `glReadPixels` into that
bitmap — a GPU-to-CPU transfer, which is the one direction GPUs are bad at.

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
