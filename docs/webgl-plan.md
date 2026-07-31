# WebGL in ctbrowser — the plan

Written 2026-07-30, before any of it exists. The measurements below are from
`examples/assets/p5.js` (v2.3.1) and this tree, not from memory.

## The decision that shapes everything: software

WebGL has **no fixed pipeline**. `drawArrays` runs a vertex shader per vertex and
a fragment shader per fragment, and there is no path that draws anything without
executing them. So a WebGL context cannot be stubbed the way a canvas can — the
shaders *are* the renderer, and an implementation that accepts them and ignores
them draws nothing while reporting success. That is the failure this codebase
keeps finding and refusing to ship; it is also exactly what happens today, where
p5 quietly falls back to its 2D renderer.

It will be a **software rasteriser**, for four reasons that all point the same
way:

1. **There is no GPU here.** This machine reports no adapter at all
   (`docs/platform.md`); Linux binaries see only lavapipe. A hardware path could
   not be tested on the machine it is written on.
2. **Goldens are the test story.** Software rasterisation is identical on every
   machine, so a WebGL page can have a byte-compared golden the way every other
   corpus page does. A GPU path cannot.
3. **`raster/` is already software-always.** Glyphs, tiles and SVG all rasterise
   in software with the GPU as an optional compositor. This is the same shape.
4. **SDL_GPU would not save the hard part.** It takes SPIR-V, not GLSL ES — so a
   GLSL front end has to exist either way. The rasteriser is the *smaller* half.

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

## Stages

Each stage is independently testable and independently committable, and each one
ends with something that can fail loudly rather than a milestone that can only be
demonstrated by the next stage.

### 1. GLSL front end — `include/ctbrowser/raster/glsl.hpp`, `src/raster/glsl.cpp`

Preprocessor, lexer, parser, and the type model (`base` × rows × cols, plus
structs and arrays). No execution.

- **Test**: parse every built-in shader p5 ships. They are string literals in the
  bundle, so `tools/gen-glsl-fixtures.py` extracts them into
  `tests/glsl/*.vert|.frag` and the test parses each one and asserts no error.
  That is a real corpus written by someone else, which is worth more than
  shaders I invent to suit my parser.
- Also: a syntax error must produce a MESSAGE with a line number, because
  `getShaderInfoLog` is what a page shows its user.

### 2. Interpreter — same files

A tree-walking evaluator with the built-in function library (≈60 functions:
`normalize`, `dot`, `cross`, `mix`, `clamp`, `smoothstep`, `texture2D`, the
`lessThan` family, …). Uniforms, attributes and varyings come in through an
`environment` the caller fills.

- **Test**: run shaders with known inputs and assert outputs numerically —
  matrix multiply order, swizzle assignment (`v.xz = ...`), integer vs float
  division, `discard`. Pure unit tests, no pixels.
- **Matrices are column-major**, which is what `uniformMatrix4fv` hands over.
  Getting it transposed looks like a plausible wrong rotation rather than an
  error, so it gets its own test.

### 3. Rasteriser — `include/ctbrowser/raster/softgl.hpp`

Triangle setup, the viewport transform, perspective-correct varying
interpolation, a depth buffer, face culling, scissor, and the blend equations
(which `shell/composite.hpp` already has the formulas for — same maths, different
caller). Draws into the `paint::bitmap` a canvas already owns.

- **Test**: draw known triangles and assert pixels — a full-screen quad, a
  z-fight resolved by `depthFunc`, a back face culled, a blended overlap.
- Perspective-correct interpolation gets its own test with a strongly perspective
  quad: interpolating in screen space instead looks *almost* right, which is the
  kind of wrong this codebase cares about.

### 4. The context — `include/ctbrowser/shell/webgl.hpp`, bound in `bindings.cpp`

The 79 methods, the constant table, and the objects: buffers, textures,
programs, shaders, framebuffers, renderbuffers. `getContext('webgl')` returns it
instead of throwing.

- **Test**: `examples/pages/webgl-triangle.html` — raw WebGL, no p5: compile a
  shader, upload a buffer, draw a triangle, with a golden. This is the first
  point at which the whole stack is provably real.
- The refusal that exists today stays for `webgl2` and moves out of the way for
  `webgl`.

### 5. p5 on top

`createCanvas(w, h, WEBGL)` genuinely selects `RendererGL`. Then the p5 probe
grows a WEBGL module: `box`, `sphere`, `plane`, `rotateX/Y/Z`, `camera`,
`ambientLight`/`directionalLight`, `texture`, `createShader`.

- **Test**: `examples/pages/p5-webgl.html` with a golden, opened and looked at.
- The known-failing `webgl/createCanvas(WEBGL) refuses` probe is replaced by one
  that asserts it *works*.

## Risks, named now

**Speed.** A tree-walking interpreter runs the fragment shader per pixel. At
200×200 that is 40,000 fragment invocations per full-screen draw, each walking an
AST. This will be slow — think seconds per frame, not milliseconds. Mitigations,
in order: keep corpus pages small; only shade fragments that pass the depth and
scissor tests; cache the AST per program. If it is still too slow to be useful,
the evaluator is the replaceable part — a bytecode compiler for GLSL is a
contained follow-up, which is why the parser and the type model are separate from
it. **This will be measured and reported, not hoped about.**

**Scope creep into 3D maths.** p5 supplies its own matrices through uniforms, so
this engine needs no camera or projection maths of its own. Resisting the urge to
write a matrix library that duplicates p5's is a deliberate constraint.

**The `raster` subsystem grows a language.** `glsl.cpp` will be the largest file
in `raster/`. It is the right home — it is software rasterisation, and it must
stay SDL-free — but `docs/architecture.md` needs a line saying so.

## What is deliberately NOT in scope

Cube maps beyond sampling one face; derivatives (`dFdx`/`dFdy`/`fwidth`, which
need neighbouring fragments a scanline rasteriser does not have in hand);
multisampling; instanced drawing beyond the call existing; transform feedback;
WebGL 2 in any form. Each gets named in `docs/raster.md` rather than discovered.

## Definition of done for the first milestone

`examples/pages/webgl-triangle.html` draws a shaded triangle through the real
WebGL API — compile, link, buffer, uniform, `drawArrays` — with a committed
golden that matches on Linux and on the Windows cross-build. p5 comes after that,
because a stack that cannot draw a triangle cannot draw a sphere, and finding out
which of five stages is wrong is much easier with four of them already pinned.
