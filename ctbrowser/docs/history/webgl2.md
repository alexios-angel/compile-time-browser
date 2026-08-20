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

  The engine used to return `null` from `getExtension` and `[]` from
  `getSupportedExtensions`, so Phaser took the no-extension path for all four.
  It now answers truthfully for the first three, and Phaser takes the VAO and
  instancing paths — which is how rung 7 going green is what broke rung 8, and
  exactly the corpus pressure the "one implementation, two spellings" decision
  was chosen to buy.
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

1. **A `webgl2` context.** `getContext('webgl2')` used to return null by an
   explicit decision; it returns a context now, and a canvas keeps ONE context
   type for ever — a later request for a different id gets null rather than
   converting what is there.
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
lesson `shell/net/url.hpp` records for Boost.URL and ctcss/ctjs record as their
leniency contract.

Derivatives (`dFdx`, `dFdy`, `fwidth`) are core in ES 3.00 rather than an
extension and are **already implemented** (`glsl_eval.cpp:1129`) — checked, not
assumed. That is also what makes `OES_standard_derivatives` cheap: the extension
is a name over a capability that already exists.

## Where this stands (2026-08-02)

**Stages 0–5 are done. The ratchet reads 8 of 9 and the API probes 31 of 31.**

The remaining rung is Babylon, and it is not blocked on WebGL at all: the
bundle needs **generators** (622 `function*`, 803 `yield`, from TypeScript's
`__awaiter` transpilation). That is a VM feature — suspending and resuming a
frame — and the machinery half exists already, because `await` on an unsettled
promise saves a frame into a coroutine object (`heap_kind::coroutine`). It
wants its own decision rather than being taken as a step here.

**What the corpora found, which is the part worth keeping.** Four defects stood
between Phaser's WebGL renderer and a painted pixel, and *not one of them
raised a GL error or threw*:

| | |
|---|---|
| `bufferSubData` | did not exist |
| `TRIANGLE_STRIP` / `FAN` | refused — and STRIP is Phaser's default topology |
| a fresh VAO's attribute table | **empty**, so every `vertexAttribPointer` after binding one was dropped by its own bounds guard |
| typed arrays | owned values instead of viewing bytes, so four views over one `ArrayBuffer` were the same object and only the last one's kind survived |

The last is the interesting one: it is a **JavaScript engine** bug, found only
because a WebGL corpus wrote floats and read them back as bytes. Phaser's vertex
positions were being stored as integers and read back as denormal floats — 64
became 9e-44 — so 288 bytes of vertex data arrived at the rasteriser as zeros,
with every WebGL call along the way behaving perfectly.

**The p5 goldens did not move, on either platform.** `p5-webgl.html` is
byte-identical on Linux and on the Windows cross-build, which is what the risk
section below asks for.

## Stages

### 0 — the harness, before any engine work — DONE
`tests/corpus/webgl2/webgl2_ratchet.cpp` + `tests/corpus/webgl2/webgl2-ratchet.txt` +
`tools/corpus/webgl2-ratchet.py` for how FAR; `tests/corpus/webgl2/webgl2_api.cpp` +
`tests/corpus/webgl2/webgl2-api-probe.js` + `tests/corpus/webgl2/webgl2-api.txt` + `tools/corpus/webgl2-api.py`
for how WIDE. Its first reading is the scoping measurement, the way stage 0 was
for the lexer and Phaser plans — both of which cancelled their own later stages,
which is the outcome to hope for rather than fear.

### 1 — the language, behind a flag nothing sets yet — DONE
`in`/`out`, the declared fragment output, `texture()`, `#version 300 es`.
Extend `tests/glsl_basics.cpp` and add ES 3.00 spellings of existing fixtures so
the same shader is parsed both ways and compared. Invisible from JavaScript on
purpose: the language change is the risky half and it lands where the tests can
see it alone.

### 2 — the extensions, which need no context work — DONE
`getSupportedExtensions` lists the three; `getExtension` returns an object whose
methods are the same C++ the WebGL 2 spellings will call. **Phaser starts using
them the moment they appear**, so this is the stage the corpus can check.

### 3 — VAOs and instancing underneath both — DONE
`softgl.cpp` draws a vertex range and needs to draw it N times with an instance
index the attribute fetch can see. VAOs are state capture — bindings and
enables — which the context already holds in one place.

### 4 — `getContext('webgl2')` returns a context — DONE
`WEBGL2` into `glsl::options::defines`, which flips p5's preamble.
`getParameter(VERSION)` and `SHADING_LANGUAGE_VERSION` must say ES 3.0 / 3.00 —
p5 reads them. `tests/corpus/p5/p5-api-probe.js` asserts `webgl2` is null today and that
assertion is correct until this stage; it gets REPLACED, not deleted.

### 5 — refuse the rest, by name — DONE
Each unimplemented entry point returns null or sets `INVALID_OPERATION` **and
says which it was**. The p5 WEBGL work was lost for a day to
`getProgramParameter` answering 0 to a question it did not understand; the
lesson was to make the unimplemented loud. Babylon is the page that will find
out, and it should be told rather than left to guess.

## Babylon as the third corpus

**Vendored 2026-08-02**, at `vendor/babylon/` with its Apache-2.0
licence — 11.6 MB against p5's 4.6 and Phaser's 8.8, which is real weight it
earns by being the only witness to the half of WebGL this engine has never been
asked for. It degrades cleanly (52 `_webGLVersion` gates), so it can be pointed
at the engine today and simply do less; its top rung in the ratchet measures how
much less.

## Verification

The p5 and Phaser ratchets and probes **must not move**, and the twelve goldens
stay byte-identical on Linux and on the Windows cross-build.

```bash
tools/corpus/webgl2-ratchet.py && tools/corpus/webgl2-api.py
tools/corpus/p5-ratchet.py && tools/corpus/p5-api.py          # 12/12, 179 probes: unchanged
tools/corpus/phaser-ratchet.py && tools/corpus/phaser-api.py  # 10/10, 114 probes: unchanged
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


## Uniform buffer objects are IN SCOPE, and the scoping decision was wrong

This plan put UBOs out of scope on the grounds that p5 does not use them and the
subset was chosen by measuring p5. That reasoning held for p5 and broke on
Babylon, which is the corpus the plan added *because* it exercises WebGL 2:
**WebGL 2 delivers uniforms through buffers, and Babylon uses them for
everything the moment it sees a WebGL 2 context.**

Refused by name, the failure is the worst available shape. The shaders link, the
draws are issued, no call errors after the three UBO entry points, and every
matrix reads as zero - so the scene collapses to a point and the canvas shows
exactly the colour it was cleared to. "Out of scope" was not a smaller
implementation; it was a wrong picture with a clean bill of health.

What landed:

* `getUniformBlockIndex`, `uniformBlockBinding`, `bindBufferBase`.
* `layout(...)` qualifiers, and `uniform Name { ... };` blocks in the GLSL front
  end. Without an instance name the members are declared as ordinary uniforms,
  because that is how the body refers to them; with one they become a STRUCT
  type and a single uniform of it, which is machinery the front end already had.
* std140 offsets, computed from the member types. The page fills the buffer to
  that layout, so it is not a choice: a vec3 strides as 16 bytes, a mat4 is four
  16-byte columns, and a mat3 is three 16-byte columns with one float wasted in
  each - read as nine contiguous floats it gives a rotation built out of the
  wrong numbers.
* `uint`, `uvec2..4` and the `1u` literal suffix, mapped onto signed integers.
  **The limit is stated rather than hidden**: this evaluator holds every value
  as a float, so `uint` differs from `int` at the top bit and for `>>`, which is
  arithmetic here and logical in GLSL.

**Still out of scope and still refused by name:** 3D textures, MRT, sampler
objects, query objects, sync objects and transform feedback. Babylon calls them
and degrades; if one of them turns out to be load-bearing the same way UBOs
were, the fix is to implement it, not to widen the stub.
