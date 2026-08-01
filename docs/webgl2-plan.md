# WebGL 2

## What this is, and what it is not

**Not the WebGL 2 specification.** That is transform feedback, uniform buffer
objects, 3D and 2D-array textures, multiple render targets, sampler objects,
query objects, sync objects, and more — on top of a software rasteriser that
manages 0.6 M fragments/sec. Claiming all of it would be a claim nobody could
check.

This is **the subset a real page uses**, arrived at by reading what p5.js v2.3.1
actually calls rather than by reading the specification and guessing. Everything
outside it refuses by name, the way `raster/spirv.hpp` already refuses loops and
structs — a page that reaches the edge is told.

The measurement that scoped it, `rg` over p5's 4.5 MB bundle for WebGL-2-only
entry points:

```
drawArraysInstanced      2
drawElementsInstanced    1
blitFramebuffer          1
createVertexArray        0      <-- p5 2.x uses NO vertex array objects
bindBufferBase           0
texStorage2D             0
drawBuffers              0
```

**Four functions.** The API delta is small; the language delta is the work.

## Why it is worth doing at all

`getContext('webgl2')` returns null today, deliberately (`docs/script.md`), and
p5 falls back to WebGL 1 and works. So this buys no page anything it cannot
already do — which is the honest starting position, and the reason to scope it
by what is used rather than by what exists.

What it does buy:

* **p5 takes its own preferred path.** `RendererGL` asks for `webgl2` first.
  Everything the engine has proven about p5 has been proven on its fallback.
* **GLSL ES 3.00**, which is what shader code on the web is written in now. A
  page bringing its own `#version 300 es` shader is refused outright today.
* **Instanced drawing**, which is the one genuine capability here rather than a
  spelling difference.

## The language, which is the actual work

p5's own sixteen shaders are **version-agnostic** — they are written to a
preamble that macros over the difference:

```glsl
#ifdef WEBGL2
  #define IN in
  #define OUT out
  #ifdef FRAGMENT_SHADER
  out vec4 outColor;            // a DECLARED output, not gl_FragColor
  #define OUT_COLOR outColor
  #endif
  #define TEXTURE texture       // not texture2D
#else
  ...attribute / varying / gl_FragColor / texture2D...
#endif
```

So the front end needs exactly four things, and `glsl::options::defines`
already exists to carry `WEBGL2`:

1. **`in` and `out` as storage qualifiers.** `in` is `attribute` in a vertex
   shader and `varying` in a fragment one; `out` is `varying` in a vertex shader
   and a fragment output in a fragment one. The existing `storage` enum already
   distinguishes those, so this is spelling, not semantics.
2. **A user-declared fragment output.** Today the rasteriser looks for
   `gl_FragColor` by name (`softgl.cpp`). It has to take the declared `out`
   variable instead — and with ES 3.00 there may be none named `gl_FragColor` at
   all.
3. **`texture()`** beside `texture2D()`. One name in the builtin table.
4. **`#version 300 es` honoured** rather than recorded and ignored
   (`glsl_preprocess.cpp:185`). It selects the language, so ignoring it is the
   one thing that cannot continue.

**LENIENTLY, accepting the union rather than enforcing the split.** Strict ES
3.00 removes `texture2D`, `attribute` and `varying`; strict ES 1.00 has no
`in`/`out`. Enforcing that would reject shaders that work in browsers and would
be the third time this tree learned that lesson — see the Boost.URL leniency
note in `shell/url.hpp` and the ctcss/ctjs leniency contract.

Derivatives (`dFdx`, `dFdy`, `fwidth`) are core in ES 3.00 rather than an
extension. **Already implemented** (`glsl_eval.cpp:1129`) — checked, not assumed.

## Stages

### 1. The language, behind a flag nothing sets yet
`in`/`out`, the declared fragment output, `texture()`, `#version 300 es`.
Extend `tests/glsl_basics.cpp`, and add ES 3.00 spellings of the existing
fixtures so the same shader is parsed both ways and compared.

This stage is invisible from JavaScript, which is deliberate: the language
change is the risky half and it lands where the tests can see it alone.

### 2. `getContext('webgl2')` returns a context
The same `webgl_context`, with a version on it. `WEBGL2` goes into
`glsl::options::defines` for every shader it compiles, which is what flips p5's
preamble. `getParameter(VERSION)` and `SHADING_LANGUAGE_VERSION` must say ES
3.0 / 3.00 — p5 reads them.

**`tests/p5-api-probe.js` asserts `webgl2` is null today, and that assertion is
correct until this stage lands.** It gets replaced, not deleted: the new probe
asserts a context comes back AND that the p5 renderer selects it.

### 3. The four functions
`drawArraysInstanced`, `drawElementsInstanced`, `vertexAttribDivisor` (which the
first two are useless without), and `blitFramebuffer`. Instancing is the only
one with real work behind it: `softgl.cpp` draws a vertex range, and it needs to
draw it N times with an instance index the attribute fetch can see.

### 4. Refuse the rest, by name
Transform feedback, UBOs, 3D textures, MRT, samplers, queries, sync. Each
returns null or sets `INVALID_OPERATION` **and says which it was** — the p5
WEBGL work was lost for a day to `getProgramParameter` answering 0 to a question
it did not understand, and the lesson was to make the unimplemented loud.

`getSupportedExtensions` shrinks: things that are extensions in WebGL 1 are core
in 2, and reporting both is how a page ends up taking a path neither supports.

## Verification

The p5 corpus is the test, because it is somebody else's code:

```bash
tools/p5-api.py            # 179 probes, and the webgl module is 21 of them
tools/p5-ratchet.py        # 12/12, both modes
ctest --preset default     # and asan
```

* **Every WEBGL probe must pass on the WebGL 2 path too.** They currently pass
  on the fallback; running them both ways is what proves the new path is not a
  worse version of the old one.
* **The ten goldens stay byte-identical.** `p5-webgl.html` renders through
  whichever context p5 chose, so if stage 2 changes what it chooses, that page's
  golden is the check that the two paths agree. **If it moves, the WebGL 2 path
  is wrong** — the same sketch must produce the same pixels.
* The Windows cross-build, because float codegen is where a second shader path
  would diverge silently.

## Risks

* **The golden moving is the outcome to expect and NOT to accept.** p5 will
  start choosing WebGL 2, so `p5-webgl.html` exercises new code. Re-recording it
  would erase the only evidence that the two paths agree.
* **Two language versions in one parser.** The union is lenient by design, but a
  shader that means different things under the two versions - `texture2D` in a
  `#version 300 es` shader - should be diagnosed rather than guessed at.
* **Instancing in a scanline rasteriser** is the one stage with real design in
  it. It may want deferring if stages 1-2 already let p5 take the WebGL 2 path,
  since p5 uses instancing in exactly three places.
* Scope creep toward "real WebGL 2". The refusal list in stage 4 is the defence,
  and it is worth writing before the temptation arrives.
