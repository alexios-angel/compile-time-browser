# WebGL in ctbrowser — the plan

Written 2026-07-30, before any of it exists. Every measurement below is from
`examples/assets/p5/p5.js` (v2.3.1) and this tree, not from memory.

## The decision that shapes everything: ONE FRONT END, TWO BACK ENDS

WebGL has **no fixed pipeline**. `drawArrays` runs a vertex shader per vertex and
a fragment shader per fragment, and there is no path that draws anything without
executing them. So a WebGL context cannot be stubbed the way a canvas can — the
shaders *are* the renderer, and an implementation that accepts them and ignores
them draws nothing while reporting success. That is the failure this codebase
keeps finding and refusing to ship; it is also exactly what happens today, where
p5 quietly falls back to its 2D renderer.

The shape is the one this engine already uses everywhere else — **software
always, hardware when it is there**:

```
        GLSL ES source (from the page)
                  |
        preprocess / parse / type      <- stage 1, DONE, serves both
                  |
              the AST
              /            reference evaluator  SPIR-V emitter
    + software rasteriser        |
              |            SDL_GPU pipeline
        paint::bitmap            |
                            a GPU texture
```

**The software back end is not a placeholder for the GPU one.** It is the
reference, the fallback, and the oracle, for four reasons:

1. **Goldens are the test story.** Software rasterisation is identical on every
   machine, so a WebGL page gets a byte-compared golden like every other corpus
   page. A GPU render will *not* be bit-identical — different fill rules,
   different rounding, a different `sin` — so the goldens stay on software and
   the GPU path is verified against it with a tolerance. That is the same
   arrangement `svg_basics` already has for plutosvg.
2. **There is no GPU HARDWARE on the machine this is written on** - only
   lavapipe, a software Vulkan implementation (`docs/platform.md`). An earlier
   draft of this document read that as "no adapter at all"; that was wrong, and
   `gpu_basics` has been running SDL_GPU against lavapipe the whole time. What
   is true is that nothing here can be benchmarked meaningfully, and that
   software has to work regardless.
3. **`raster/` is already software-always** and `gpu/` is already "the fallback
   when there is none". This is the same split, not a new one.
4. **The front end is shared, so neither back end is wasted work.** SDL_GPU takes
   SPIR-V, not GLSL ES, so stage 1 has to exist for the GPU path too — it is a
   new *back end*, not a new *compiler*.

### What the GPU path actually requires

The existing GPU code compiles its shaders to SPIR-V **at build time**, with
`glslc`, and commits the result (`tools/gen-shaders.py`). **WebGL cannot do
that**: the shaders arrive from the page at run time, and shelling out to a
compiler that may not be installed is not an option for a browser engine.

So the GPU back end is a **SPIR-V emitter written in C++**, walking the same AST
stage 1 produces. That is the honest cost of this decision, and it is a real
piece of work — though a bounded and mechanical one: SPIR-V is a simple SSA
binary format, a header and a stream of fixed-layout words, and the AST is
already typed.

It is also the first thing in this engine to **rasterise** on the GPU. Today
`gpu/` does composition only: tiles are drawn on the CPU and uploaded as textured
quads. A WebGL draw call is the first workload where the GPU does the drawing,
which is why it needs a graphics pipeline rather than the existing blit.

**Lavapipe makes this developable here**, and one claim this document made about
it was **wrong and is corrected**.

It is a real Vulkan implementation - `gpu_basics` runs the compositor against it
and matches the software path byte for byte - so the pipeline plumbing genuinely
can be exercised here. Only the speed is unrepresentative.

But this document also said "a driver rejecting malformed SPIR-V is a loud
failure, which is exactly what is wanted". **It does not reject it.**
`SDL_CreateGPUShader` was measured, in `gpu_basics`, against deliberate garbage -
a valid magic number followed by `0xDEADBEEF` - and accepted it. Omitting the
entry-point interface, mismatching operand widths and storing a float into a
`vec4` were all accepted too. Acceptance proves the bytes reached the driver and
nothing more, and the test now PRINTS that rather than leaving it to a comment
that could go stale.

So validation is three things, honestly ranked:

  * **structure**, in `spirv_basics` - header, word counts, ids defined before
    use. Calibrated by requiring it to accept `glslc`'s own committed output,
    which is what stops a checker that passes everything.
  * **reaching the driver**, in `gpu_basics` - a smoke test, and labelled as one.
  * **real validation**, by `tools/check-spirv.py`, which runs `spirv-val` when
    it is installed and says plainly that it did not when it is not. Optional in
    the same way plutosvg and SDL3_image are.

### The invariant this must not break

**The engine is SDL-free** outside `shell/app.cpp` and `gpu/`, and
`tests/api_surface` lints it. So the WebGL *context* lives in `shell/` and knows
nothing about SDL; the GPU back end lives in `gpu/` behind an interface the shell
calls through. Same shape as `raster::backend` already has for the compositor.

### Choosing between them

Software by default, because that is what makes a golden. The GPU back end is
selected by the embedder (`app_options`) or by `CTBROWSER_WEBGL=gpu`, and falls
back with a message rather than silently when there is no device or no SPIR-V
support — a renderer that quietly is not the one you asked for is the failure
this whole document is about.

`tools/check-render.cmake` forces software, so every committed golden means
software and says so.

**Version: WebGL 1 only.** p5 asks for `webgl2` first and falls back to `webgl`
(p5.js:73000), and its shaders are written with `IN`/`OUT` macros that expand to
`attribute`/`varying` for ES 1.00. Advertising only `webgl` therefore costs p5
nothing and removes a whole language version, integer textures, transform
feedback and MRT from the surface. `getContext('webgl2')` keeps returning null.

## What the target actually needs

`p5.js` calls **79 distinct `gl.*` methods**. That is the API surface, and it is
bounded. The language is the hard part. p5's *real* built-in shaders — not the
doc-comment examples — use:

- a **preprocessor**: `#define`, `#ifdef` / `#else` / `#endif`, and `#version`
- **structs**: `struct Vertex { vec3 position; vec3 normal; ... }`, passed to and
  returned from user functions
- **uniform arrays walked by loops**: `for (int i = 0; i < 8; i++) { ... uAmbientColor[i] }`
- user functions, `bool` uniforms, `mat3`/`mat4`, swizzles, and the usual
  built-in library

So the subset is bigger than "vec4 arithmetic". Discovering that before writing
the parser is why this document exists.

---

# Performance: the architecture, not an afterthought

A tree-walking interpreter over a 200×200 canvas runs the fragment shader 40,000
times per full-screen draw, each walk chasing pointers through an AST. That is
seconds per frame. **Speed is structural here, so it is designed in from the
first commit** — retrofitting the two decisions below (bytecode, and batching
over fragments) would mean rewriting the evaluator, the register model and the
rasteriser's inner loop together.

What is *not* designed in from the first commit is a hand-tuned anything. The
order below is by payoff per unit of risk, and each step is measured before the
next is started.

## 0a. OpenMP is OPTIONAL, and it is a speed switch not a behaviour switch

`find_package(OpenMP)` and use it when it is there. It must not become a build
requirement: this tree's stated invariant is that the build asks nothing unusual
of the compiler, and the mingw cross-build cannot be assumed to have libgomp.

The hard rule that follows: **the goldens must be byte-identical with OpenMP on
and off.** That is directly testable rather than asserted — the render tests run
in CI with it enabled and the same comparison is available with
`-DCTBROWSER_WITH_OPENMP=OFF`, and any divergence is a bug in how a pragma was
used, not a tolerance to widen. It is what keeps §0 true.

## 0. The constraint that rules several optimisations out

**Every float result must be bit-identical on every platform**, because
`tests/golden/*.ppm` are byte-compared and the Windows cross-build is expected to
match Linux exactly. This tree already gets that for free — there is no
`-ffast-math`, no `-Ofast` and no `-march=` anywhere in the build — and WebGL
must not be the thing that breaks it.

So, ruled out: fast-math, reassociation of float expressions, FMA contraction
(`-ffp-contract=off` if any compiler here defaults otherwise), and any reduction
whose summation order is not fixed. `dot()` and matrix multiply sum in a
**defined order**, written down in the code, because "the compiler picked one" is
not a specification.

Ruled *in* and safe: everything below. Lane-parallel SIMD is bit-identical to
scalar when lanes are independent, which is why it is the first big win rather
than a risky one.

## 1. A bytecode VM — yes, and it fits better than the JS one

The engine already has a register VM for JavaScript, and this follows its shape:
fixed-width instructions, an opcode and three operands, a register file per
invocation. The differences all make GLSL **easier and faster** than JS:

| | JS VM | GLSL VM |
|---|---|---|
| Types | dynamic, NaN-boxed, checked per op | **static, resolved at compile time** |
| Dispatch | branch on value kind inside each op | none — the opcode encodes the shape |
| Memory | GC heap, allocation per closure | **one flat `float` array, no allocation** |
| Frame size | grows with the call | **known when the shader compiles** |
| Control flow | exceptions, closures, generators | `if`/`for`/`return`/`discard` only |

GLSL is statically typed, so `a * b` resolves at compile time to exactly one of
`mul_f`, `mul_v3_f`, `mul_mat4_v4`, … and the interpreter never asks what
anything is. There is no GC, nothing to box, and no dynamic property lookup — the
three things that make the JS VM's inner loop expensive.

**Instruction shape**: 8 bytes, matching `script/bytecode.hpp` — opcode plus
three 16-bit operands, where an operand is a *register index* and registers are
allocated at compile time. Constants and uniforms live in the same register file,
written before the program runs.

Expected: **10–40×** over a tree walk, on its own.

## 2. Run the shader over N fragments at once — the real win

This is what every production software rasteriser does (llvmpipe, SwiftShader),
and it is the single largest factor. Instead of running the shader once per
fragment, run it over a **packet of N fragments simultaneously**, with the
register file laid out **structure-of-arrays**:

```
register file:  float regs[num_regs][4][N]     // component-major, then lane
                                ^^^ up to vec4  ^ N fragments in flight
```

An `add_v3` is then 3 × N independent float adds over contiguous memory — which
vectorises without a single intrinsic, and stays portable across the
Linux/mingw/ARM matrix this repo builds for. `N = 8` targets AVX2; `N = 4` is the
SSE/NEON baseline; `N = 1` is the scalar fallback and the correctness oracle.

**`#pragma omp simd` is how those loops are made to vectorise reliably.** Leaving
it to the auto-vectoriser means the speed of this depends on whether a particular
compiler noticed a particular loop, which is not a thing to build a renderer on.
`omp simd` states the intent — these lanes are independent — in one portable line
per loop, and `declare simd` does the same for the built-in library so a call to
`normalize` inside a packet does not fall back to scalar.

**What OpenMP must NOT be used for here: reductions.** `reduction(+:x)`
reassociates the summation, so `dot()` would give a different answer depending on
lane count and thread count — and a different answer means a different golden on
a different machine. §0 is not negotiable. Horizontal reductions are written by
hand in a defined order; `omp simd` is used only on the lane-independent loops,
where it cannot change a result.

The consequences to design for, not discover:

- **Branches become masks.** `if (c) A else B` evaluates both sides and blends by
  a per-lane mask. Cheap for the small conditionals shaders actually contain, and
  it is why shaders avoid heavy branching in the first place.
- **Loops run until every lane exits**, with inactive lanes masked off.
- **`discard` is a lane mask**, not a control-flow exit — which is what it
  already is on a GPU.
- **Shade in 2×2 quads.** The natural packet unit. This has a bonus worth
  calling out: `dFdx`/`dFdy`/`fwidth` are listed below as out of scope precisely
  because a scanline rasteriser has no neighbouring fragments — but a
  quad-shading rasteriser has them **by construction**, so derivatives become
  nearly free. Designing for quads now turns a "not in scope" into "falls out".

Expected: **4–8×** on top of the bytecode VM.

## 3. Hoist everything that does not change per fragment

Uniforms are constant for the whole draw call. Any subexpression depending only
on uniforms and literals can be computed **once per draw** instead of once per
fragment. In p5's own shaders that is a lot: `uProjectionMatrix * uModelViewMatrix`,
the whole ambient-light accumulation loop, every `uMaterialColor` read.

Implementation: mark each AST node uniform-invariant during compilation, lift
maximal invariant subtrees into a **preamble program** that runs once per draw
and writes its results into constant registers. This is loop-invariant code
motion with a trivially-known loop.

Expected: **1.5–3×** on real shaders, more on lighting ones.

## 4. Specialise on uniform *values*

A step past hoisting, and it suits p5 unusually well. `if (uUseVertexColor)` is a
`bool` uniform; `for (i = 0; i < 8; i++) if (i < uAmbientLightCount)` is a loop
whose real trip count is a uniform. With the uniform values known at draw time:

- dead branches are eliminated entirely
- the loop is unrolled to its actual count
- `uniform1i` samplers fold into a direct texture pointer

Compile a specialised program keyed by a hash of the uniforms that appear in
branch conditions, and cache it. p5 changes those rarely — the same specialised
program serves whole frames.

Expected: **1.5–4×** on p5's lighting and material shaders specifically.

## 5. Do not shade what cannot be seen

Cheaper than making shading fast:

- **Early-Z**: run the depth test *before* the fragment shader whenever the
  shader neither discards nor writes `gl_FragDepth` — both knowable at compile
  time. For a scene with any overdraw this skips whole fragments.
- **Backface culling and scissor** before shading, not after.
- **Tile binning with per-tile Z bounds**, so a tile entirely behind what is
  already drawn is rejected without touching a fragment.

Expected: proportional to overdraw — **2–5×** on a scene like a sphere, ~1× on a
single quad.

## 6. Rasterise tiles in parallel — on the scheduler, not on OpenMP

`core/scheduler.hpp` already has a `parallel_for`, and `raster/` already runs
tiles across it. Same shape: bin triangles into tiles, then shade tiles
concurrently.

**Threading stays on the existing scheduler even though OpenMP is available**,
and the reason is oversubscription: the engine already owns a worker pool sized
to the machine, and an `omp parallel for` inside a task that is already running
on one of those workers spawns a second pool of the same size. Two pools of N
threads on N cores is slower than one, and the failure is a performance cliff
that looks like nothing in particular.

So the division is: **OpenMP for SIMD (`omp simd`, inside one thread), the
scheduler for threads.** Each does the thing it is better at and they do not
contend.

**Determinism is preserved by construction**: each tile owns its pixels
exclusively and its triangle list stays in submission order, so the result does
not depend on how the work was scheduled. That is the property goldens need, and
it is why tile-parallel is safe where a parallel-over-triangles scheme would not
be.

Expected: **~cores**, minus binning overhead.

## 7. The small structural ones

- **Per-triangle setup once**: edge functions and varying gradients computed
  once, then incremented per pixel — never a barycentric solve per fragment.
- **Perspective correction per quad**, not per pixel: interpolate `attr/w` and
  `1/w` linearly, one reciprocal per quad.
- **Sampler closures resolved at bind time** — filter and wrap mode decided when
  the texture is bound, not re-branched per sample.
- **One execution context per worker thread**, reset per packet. No allocation
  anywhere in the inner loop.

## What is deliberately NOT being done

- **Machine-code JIT for the software path.** It would be the next multiplier
  after the bytecode VM, and it is not worth it: it breaks the cross-compile
  matrix, needs W^X handling, and is a large amount of platform-specific code to
  maintain. The GPU back end is the answer to "make it much faster", and it
  arrives by emitting SPIR-V rather than machine code.
- **DXIL and MSL.** SPIR-V means the Vulkan driver, which is the same limit
  `gpu/device.hpp` already documents for the compositor. Direct3D and Metal want
  their own back ends and belong with the Windows and macOS platform work.
- **A native fast path for p5's specific shaders** — recognising `phongVert` and
  substituting hand-written C++. It would be fast and it is cheating: it diverges
  silently the moment p5 changes a shader. If it is ever needed, the only
  acceptable form is one *differentially tested against the interpreter*, and
  that is a decision for after measurement, not before.
- Derivatives are back **in** scope (see §2). Still out: multisampling,
  transform feedback, cube maps beyond one face, WebGL 2.

## How this is kept honest

**A scalar reference evaluator is written first and kept forever.** It is the
simplest possible tree-walker, it is the oracle, and every optimised path is
**differentially tested against it**: same shader, same inputs, results must
match bit for bit. That is what makes it safe to write a masked SIMD interpreter
with uniform specialisation and still believe the output.

The reference implementation is not wasted work on the way to the fast one. It is
the thing that proves the fast one.

**A benchmark exists from stage 2**, reporting fragments/second for a fixed
shader, so every claim above becomes a number in the commit that makes it. The
targets, to be confirmed or corrected by measurement:

| | fragments/sec | 200×200 full-screen draw |
|---|---|---|
| tree walk, heap-allocating values (stage 2) | 0.64 M | 63 ms |
| **+ inline storage, prepared programs (MEASURED, stage 3a)** | **0.83 M** | **48 ms** |
| bytecode VM, scalar — NOT BUILT | ~10 M | ~4 ms |
| + 8-wide packets — NOT BUILT | ~60 M | ~0.7 ms |

**The first two rows are measured; the rest are still predictions.**
`ctbrowser-test-glsl_basics --bench` reports it, on a shader with the shape of
real work — a normalize, a dot, two multiplies and a clamp, which is what a
diffuse term costs.

Two corrections the measurement forced, recorded rather than quietly fixed:

- The original table said **~1 s** for the tree walk. That was arithmetic I got
  wrong, not a mis-estimate: 1 M fragments/sec over 40,000 fragments is 40 ms,
  not a second. The fragments/sec guess was about right; the time column was
  nonsense.
- **63 ms per draw changes the ordering.** The reference evaluator is not
  unusable at corpus size — it is roughly fifteen frames a second for a
  full-screen shader, and far better than that for the small triangles a real
  scene draws. So the rasteriser and the WebGL context (stages 4 and 5) can be
  built **on the reference evaluator**, and the bytecode VM can follow once
  there is a whole pipeline to measure end to end. Optimising the one stage
  that has a benchmark, before the four that do not, is how effort goes to the
  wrong place.

  It bites at real sizes: 800×600 is 480,000 fragments, or about 750 ms a
  frame. That is what stage 3 is for, and it is now scheduled after the thing
  that will tell it which parts matter.

---

## Stages

Each is independently testable and independently committable, and each ends with
something that can fail loudly rather than a milestone only the next stage can
demonstrate.

### 1. GLSL front end — `raster/glsl.hpp`, `src/raster/glsl.cpp`

Preprocessor, lexer, parser, type model (base × rows × cols, structs, arrays).
No execution.

**Test**: parse every built-in shader p5 ships. They are string literals in the
bundle, so `tools/gen-glsl-fixtures.py` extracts them into `tests/glsl/*.vert|.frag`
and the test parses each and asserts no error — a corpus written by someone else,
worth more than shaders invented to suit my own parser. A syntax error must carry
a line number, because `getShaderInfoLog` is what a page shows its user.

### 2. Reference evaluator + benchmark

The scalar tree-walker and the built-in library (~60 functions). Numeric unit
tests: matrix multiply order (**column-major**, as `uniformMatrix4fv` hands it
over — transposed looks like a plausible wrong rotation rather than an error),
swizzle assignment, integer vs float division, `discard`.

The benchmark lands here, so stage 3 has a baseline to beat.

### 3a. What profiling actually said — DONE

Before building the VM, the pipeline was measured. Three findings, two of which
contradicted what this document previously assumed:

1. **Per-fragment SETUP is not the bottleneck.** An early probe suggested it was
   - a shader with twenty constants and twenty functions its `main` never touched
   ran 3.9× slower than the same `main` alone - but that shader was artificial.
   p5's `lightTextureFrag` declares eight things, and preparing it once rather
   than per fragment is worth only **1.1×**. The `glsl::program` type that came
   out of it is still right (it is the shape the VM needs, and it costs nothing),
   but the claim was over-general and is corrected here rather than quietly
   dropped.

2. **The cost is PER AST NODE, and the arithmetic is free.** A hundred `vec4`
   operations cost only 15% more than a hundred `float` ones - four times the
   data for almost nothing - so essentially all of the ~0.2 µs per node was the
   machinery around the arithmetic rather than the arithmetic.

3. **Most of that machinery was one heap allocation per node.** Every `value`
   held a `std::vector<float>`, so every intermediate result allocated and freed.
   Sixteen floats of inline storage - a `mat4`, the largest thing that is not an
   array or a struct - is worth a measured **1.5×**, controlled A/B, minimum of
   seven runs.

**Measuring on this machine needs care**: run-to-run variance is ±10%, which is
wider than most changes worth making. Every number above is the minimum of seven
runs, which is the robust estimator because noise only ever adds.

### 3b. The bytecode VM — SCOPED, NOT BUILT

The remaining ~10× needs the real thing: an AST-to-bytecode compiler with a flat
register file, which removes the per-node allocation entirely, the name lookups,
and the recursive dispatch in one go. Finding (2) above is precisely the argument
for it - the overhead is structural, and no amount of tuning the tree walker
reaches it.

It is not built. It is a large piece of work - a compiler covering structs,
arrays, overloads and `out` parameters, plus a VM, plus the differential test
against the reference evaluator - and the GPU back end (stage 7) makes the
software path's speed much less critical, so it is honest to say it is scoped
rather than to half-build it.

Note that this stage is for the SOFTWARE back end only. The GPU back end does not
want it: a shader that reaches SPIR-V is executed by the driver, and the fastest
interpreter is the one that never runs.

Compiler from AST to the 8-byte instruction set, SoA register file, N-wide
packets, masked control flow. **Differential test against stage 2** over the p5
shader corpus with randomised inputs.

### 4. Rasteriser — `raster/softgl.hpp`

Triangle setup, viewport transform, perspective-correct interpolation, depth
buffer, culling, scissor, blending (the equations `shell/composite.hpp` already
has — same maths, different caller). Quad shading, early-Z, tile binning,
`parallel_for`.

**Test**: known triangles, asserted pixels — a full-screen quad, a z-fight
resolved by `depthFunc`, a culled back face, a blended overlap. Perspective
correction gets its own test with a strongly perspective quad, because
interpolating in screen space looks *almost* right.

### 5. The context — `shell/webgl.hpp`, bound in `bindings.cpp`

The 79 methods, the constant table, and the objects: buffers, textures, programs,
shaders, framebuffers, renderbuffers. `getContext('webgl')` returns it instead of
throwing; `webgl2` keeps refusing.

**Test**: `examples/pages/webgl-triangle.html` — raw WebGL, no p5, with a golden.
The first point at which the whole stack is provably real.

### 6. Uniform hoisting and specialisation

Layered on once the pipeline is correct and measured, each behind the same
differential test.

### 7. The GPU back end — `raster/spirv.hpp` (DONE), the pipeline (NOT DONE)

A SPIR-V emitter over the stage-1 AST, and an SDL_GPU graphics pipeline built
from the result. Written after the software path works end to end, so there is
something correct to compare against — and so the shape of what a back end needs
is known from having built one rather than guessed.

**Verification, and this is the part that needs saying.** A GPU render is not
bit-identical to a software one, so it cannot share a golden. Instead:

  * the emitted SPIR-V is validated structurally, and by lavapipe accepting it -
    a driver rejecting malformed SPIR-V is a loud failure and a useful test even
    on a machine with no real GPU;
  * `tests/webgl_parity.cpp` renders the same scene through both back ends and
    asserts a per-channel difference under a tolerance, and SKIPS with a message
    when there is no device - the arrangement `svg_basics` already uses for
    plutosvg;
  * the committed goldens stay on software, and `check-render.cmake` forces it.

The tolerance is a real weakening and it is bounded deliberately: a few units per
channel catches "the shader ran and drew the right thing", which is what this
test is for. Anything finer than that belongs to the software path, where exact
is achievable and therefore required.

### 8. p5 on top — DONE (2026-07-31), and it found what nothing else could

`createCanvas(w, h, WEBGL)` selects `RendererGL`, and `box()` and `sphere()`
render. `examples/pages/p5-webgl.html` has a golden — a rotated cube and a
sphere, opened and looked at, byte-identical on Linux and on the Windows
cross-build. Two probes replace the known-failing one: the renderer is selected,
and a sketch actually draws geometry.

**Four engine bugs sat between "the context works" and "p5 draws", and every one
was silent.** They are listed in `docs/script.md`. The one worth reading this far
for is `getProgramParameter`, because it is the hole this plan's own testing
strategy could not have found:

> Every WebGL page in this tree, and every test in `webgl_basics`, asks for
> uniforms and attributes **by name** — because a page that wrote the shader
> already knows what is in it. A **library** does the opposite: it asks how many
> there are and walks them. `getProgramParameter` answered 0 to every question it
> did not recognise, so p5 was told the shader declared nothing, bound no
> attributes, set no matrices, and drew a cube with no vertices. One
> correct-looking `drawElements` of 36 indices, no GL error, empty canvas.

The corpus page written by hand could never have caught it, and neither could a
larger corpus of hand-written pages. **Running somebody else's library is a
different test from running more of your own pages** — that is the transferable
result, and it is why the GLSL parse corpus is p5's sixteen shaders rather than
sixteen shaders written here.

## Definition of done for the first milestone

`examples/pages/webgl-triangle.html` draws a shaded triangle through the real
WebGL API — compile, link, buffer, uniform, `drawArrays` — with a committed
golden matching on Linux and on the Windows cross-build, and a benchmark number
in the commit message. p5 comes after, because a stack that cannot draw a
triangle cannot draw a sphere, and finding out which of six stages is wrong is
much easier with five of them already pinned.
