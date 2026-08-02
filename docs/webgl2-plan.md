# WebGL 2, and the WebGL 1 extensions that are the same thing

## What changed, and why this was rewritten (2026-08-02)

The previous version scoped WebGL 2 by reading **p5.js** and concluded: four
functions, and no vertex array objects. That was accurate about p5 and still is.
It is rewritten because two more corpora arrived, and between them they say
three different things.

**Measured, not assumed** — WebGL-2-only entry points, counted over each bundle:

| | p5.js 2.3.1 | Phaser 4.2.1 | Babylon.js |
|---|---|---|---|
| asks for `getContext('webgl2')` | **yes**, first | **no**, never | **yes**, first |
| falls back to WebGL 1 | yes | n/a | yes, gated in **52** places |
| `#version 300 es` shaders | 0 | **0** | **5** |
| `createVertexArray` / `bindVertexArray` | 0 / 0 | 1 / 2 | 3 / 3 |
| `drawArraysInstanced` / `vertexAttribDivisor` | 2 / 0 | 1 / 1 | 1 / 5 |
| `drawBuffers` | 0 | 0 | **6** |
| `texImage3D` / `texStorage2D` / `texStorage3D` | 0 | 0 | **5 / 1 / 1** |
| `createSampler` | 0 | 0 | **4** |
| `bindBufferBase` / `getUniformBlockIndex` / `uniformBlockBinding` | 0 | 0 | **2 / 2 / 2** |
| `createQuery` / `fenceSync` | 0 | 0 | **4 / 1** |
| `beginTransformFeedback` | 0 | 0 | **3** |
| `blitFramebuffer` | 1 | 0 | 2 |
| `renderbufferStorageMultisample` | 0 | 0 | 1 |

Three positions, and each one is useful for a different reason:

* **p5 wants WebGL 2 and barely uses it.** It asks first and falls back
  silently, so everything this engine has ever proven about p5's WEBGL mode has
  been proven on the fallback path.
* **Phaser never asks.** It requests `getContext('webgl')` and takes the
  WebGL-2-looking features from WebGL 1 **extensions**, each gated on
  `getSupportedExtensions()`:

  ```
  ANGLE_instanced_arrays        drawArraysInstancedANGLE, vertexAttribDivisorANGLE
  OES_vertex_array_object       createVertexArrayOES, bindVertexArrayOES, ...
  OES_standard_derivatives      dFdx/dFdy/fwidth in GLSL ES 1.00
  KHR_parallel_shader_compile   optional; Phaser disables skipUnreadyShaders without it
  ```

  The engine returns `null` from `getExtension` and `[]` from
  `getSupportedExtensions` today, so Phaser takes the no-extension path for all
  four and degrades gracefully — which is why its renderer works at all.
* **Babylon uses nearly the whole specification**, and is the only corpus that
  would exercise this work properly. It also degrades: `_webGLVersion` gates 52
  sites, so it runs on WebGL 1 and simply does less.

**WebGL 2 buys Phaser nothing and buys p5 almost nothing. Babylon is what it is
for.** That is the finding this rewrite exists to record.

## The decision: one implementation, two spellings

`createVertexArray` on a WebGL 2 context and `createVertexArrayOES` on an
`OES_vertex_array_object` object are one operation under two names. So are
`drawArraysInstanced` / `drawArraysInstancedANGLE` and `vertexAttribDivisor` /
`vertexAttribDivisorANGLE`.

Implement once, bind both names:

* **The corpus can verify the WebGL 2 work.** Phaser exercises the extension
  spelling on every frame it draws, so the VAO and instancing machinery gets a
  workout from somebody else's renderer. Otherwise that machinery is tested only
  by pages written to test it.
* **No second implementation to drift.** This tree has paid for two
  implementations of one job twice — two URL parsers, two base64 decoders — and
  both times the copies disagreed before anyone noticed.

## In scope

The subset the corpora actually use. Everything outside refuses BY NAME, the way
`raster/spirv.hpp` refuses loops and structs, so a page reaching the edge is told
rather than quietly given a wrong answer.

1. **A `webgl2` context.** `getContext('webgl2')` returns null today by explicit
   decision (`shell/bindings.cpp:1322`).
2. **The WebGL 2 constants.** Dull and load-bearing: one arriving as `undefined`
   makes every comparison against it silently false.
3. **GLSL ES 3.00** — the language work, and most of the job. Babylon ships five
   such shaders; p5 macros over the difference; Phaser has none.
4. **Vertex array objects**, both spellings.
5. **Instanced drawing**, both spellings.
6. **`getSupportedExtensions` / `getExtension`** answering truthfully for
   `OES_vertex_array_object`, `ANGLE_instanced_arrays` and
   `OES_standard_derivatives` — and `null` for everything else, which is what a
   driver without one returns and what a page checks for.

## NOT in scope, and Babylon is the reason to say so out loud

Uniform buffer objects, 3D and 2D-array textures, multiple render targets,
sampler objects, query objects, sync objects, transform feedback,
`renderbufferStorageMultisample`, `blitFramebuffer`.

**Babylon calls every one of them.** The previous plan could list these as "not
in scope" cheaply because no corpus touched them; that is no longer true, and
the honest statement is that this engine is choosing not to implement things a
real library will ask for. Babylon degrades to WebGL 1 when it does not get
them, so it still runs — and its `_webGLVersion` gating is a ready-made list of
exactly what it would have done differently.

`KHR_parallel_shader_compile` is out for a different reason: it lets a page poll
whether a shader finished compiling on another thread, and compilation here is
synchronous. Phaser disables the feature that uses it when absent, which is
correct.

**The software rasteriser is the real ceiling.** 0.6 M fragments/sec is the
measured number (`docs/performance.md`); transform feedback and MRT are not
spelling problems on top of that, they are throughput problems.

## The language, which is the actual work

p5's sixteen shaders are **version-agnostic** — written to a preamble that
macros over the difference:

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

Babylon does not macro over it: its ES 3.00 shaders say `#version 300 es`
outright, which is the case that cannot be faked.

The front end needs exactly four things, and `glsl::options::defines` already
exists to carry `WEBGL2`:

1. **`in` and `out` as storage qualifiers.** `in` is `attribute` in a vertex
   shader and `varying` in a fragment one; `out` is `varying` in a vertex shader
   and a fragment output in a fragment one. The existing `storage` enum already
   distinguishes those, so this is spelling, not semantics.
2. **A user-declared fragment output.** Today the rasteriser looks for
   `gl_FragColor` by name (`softgl.cpp`); it must take the declared `out`
   variable instead, and under ES 3.00 there may be no `gl_FragColor` at all.
3. **`texture()`** beside `texture2D()`. One name in the builtin table.
4. **`#version 300 es` honoured** rather than recorded and ignored
   (`glsl_preprocess.cpp:185`). It selects the language, so ignoring it is the
   one thing that cannot continue.

**LENIENTLY, accepting the union rather than enforcing the split.** Strict ES
3.00 removes `texture2D`, `attribute` and `varying`; strict ES 1.00 has no
`in`/`out`. Enforcing that would reject shaders that work in browsers — the
lesson `shell/url.hpp` records for Boost.URL and ctcss/ctjs record as their
leniency contract.

Derivatives (`dFdx`, `dFdy`, `fwidth`) are core in ES 3.00 rather than an
extension and are **already implemented** (`glsl_eval.cpp:1129`) — checked, not
assumed. That is also what makes `OES_standard_derivatives` cheap: the extension
is a name over a capability that already exists.

## Stages

### 0 — the harness, before any engine work
`tests/webgl2_ratchet.cpp` + `tests/webgl2-ratchet.txt` +
`tools/webgl2-ratchet.py` for how FAR; `tests/webgl2_api.cpp` +
`tests/webgl2-api-probe.js` + `tests/webgl2-api.txt` + `tools/webgl2-api.py`
for how WIDE. Its first reading is the scoping measurement, the way stage 0 was
for the lexer and Phaser plans — both of which cancelled their own later stages,
which is the outcome to hope for rather than fear.

### 1 — the language, behind a flag nothing sets yet
`in`/`out`, the declared fragment output, `texture()`, `#version 300 es`.
Extend `tests/glsl_basics.cpp` and add ES 3.00 spellings of existing fixtures so
the same shader is parsed both ways and compared. Invisible from JavaScript on
purpose: the language change is the risky half and it lands where the tests can
see it alone.

### 2 — the extensions, which need no context work
`getSupportedExtensions` lists the three; `getExtension` returns an object whose
methods are the same C++ the WebGL 2 spellings will call. **Phaser starts using
them the moment they appear**, so this is the stage the corpus can check.

### 3 — VAOs and instancing underneath both
`softgl.cpp` draws a vertex range and needs to draw it N times with an instance
index the attribute fetch can see. VAOs are state capture — bindings and
enables — which the context already holds in one place.

### 4 — `getContext('webgl2')` returns a context
`WEBGL2` into `glsl::options::defines`, which flips p5's preamble.
`getParameter(VERSION)` and `SHADING_LANGUAGE_VERSION` must say ES 3.0 / 3.00 —
p5 reads them. `tests/p5-api-probe.js` asserts `webgl2` is null today and that
assertion is correct until this stage; it gets REPLACED, not deleted.

### 5 — refuse the rest, by name
Each unimplemented entry point returns null or sets `INVALID_OPERATION` **and
says which it was**. The p5 WEBGL work was lost for a day to
`getProgramParameter` answering 0 to a question it did not understand; the
lesson was to make the unimplemented loud. Babylon is the page that will find
out, and it should be told rather than left to guess.

## Whether Babylon becomes a corpus

It is 11.6 MB unminified, against p5's 4.6 and Phaser's 8.8, and the two
existing corpora are already 13 MB of this repository. It is the only library
that would exercise stages 3-5 properly, and it degrades cleanly, so it can be
pointed at the engine without being committed to it. **Not vendored yet** — that
is a deliberate open question rather than an oversight, and the ratchet is
written so a third corpus can be added as one more rung.

## Verification

The p5 and Phaser ratchets and probes **must not move**, and the twelve goldens
stay byte-identical on Linux and on the Windows cross-build.

```bash
tools/webgl2-ratchet.py && tools/webgl2-api.py
tools/p5-ratchet.py && tools/p5-api.py          # 12/12, 179 probes: unchanged
tools/phaser-ratchet.py && tools/phaser-api.py  # 10/10, 114 probes: unchanged
tools/remote-build.sh                           # and on GCC, without SDL
```

## Risks

* **p5 switching paths is the real one.** It asks for `webgl2` first; the day it
  gets one, every p5 golden is drawn by code that has never run here.
  `p5-webgl.html`'s golden moving is the outcome to EXPECT and NOT to accept —
  re-recording it would erase the only evidence the two paths agree.
* **GLSL ES 3.00 is a language, not a flag.** The front end is an ES 1.00
  parser and the whole fixture corpus in `tests/glsl/` is ES 1.00.
* **Babylon will ask for what is not there.** That is the point of measuring it,
  and stage 5 is the answer: it must be told, not guessed at.
* **Scope creep toward "real WebGL 2".** The NOT-in-scope list is the defence,
  written before the temptation arrives rather than after.
