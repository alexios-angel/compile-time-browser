# Running page shaders on the GPU, with libshaderc

**The question, asked 2026-08-03: compile a page's GLSL to SPIR-V at run time
with libshaderc so Vulkan can execute it on hardware, instead of interpreting
it. Measured rather than argued about, and the answer is that it works, the
translation is small, and the compiler is the easy part.**

An earlier note in `docs/webgl-plan.md` answered a narrower question — could
shaderc replace `raster/spirv.cpp` or the interpreter as-is — and concluded no
because glslang refuses `#version 300 es`. That is true and it is not the
obstacle it looked like: **the version line is an input-dialect problem with a
mechanical fix**, which is what browsers' shader translators do all day.

## The prize, measured

`tests/glsl_basics --bench`, on p5's own `lightTextureFrag`, which is a shader
somebody actually ships:

```
program::run() - prepared once      1.03 M frag/s    38.9 ms per draw
```

**One million fragments per second.** A 420x300 canvas is 126,000 fragments, so
a single full-screen pass costs about 120 ms — before any overdraw, and Babylon
draws the scene more than once. That is the ceiling every WebGL page in this
tree is living under, and it is why `webgl-triangle.html` is 160x160.

For scale: lavapipe — the CPU Vulkan driver, which is all a Linux binary sees
here (`docs/platform.md`) — is a JIT'd, multithreaded rasteriser in the hundreds
of millions of fragments per second. A real GPU is two orders beyond that. **The
expected win is 100x to 1000x, and it is the difference between a demo and a
renderer.**

## The translation, measured

What a WebGL 2 page sends, and what glslang says about it:

```
#version 300 es                      ES shaders for SPIR-V require version 310 or higher
uniform Scene { mat4 vp; };          'binding' : uniform/buffer blocks require layout(binding=X)
in vec3 position;                    'location' : SPIR-V requires location for user input/output
```

Three complaints, all mechanical, and **this engine already knows every number
it is asking for**:

| what glslang wants | where it already exists |
|---|---|
| `#version 310 es` instead of `300 es` | a header rewrite |
| `layout(binding=N)` on each uniform block | `gl_program::blocks` and `block_bindings`, filled in by `uniformBlockBinding` |
| `layout(location=N)` on each in/out | `gl_program::attribute_names`, assigned at link time — it is exactly what `getAttribLocation` reports |

With those three injected, the same shader compiles:

```
vertex   (2 uniform blocks, 3 attributes, 3 varyings)   -> 2160 bytes of SPIR-V
fragment (uniform block, sampler2D, texture())          -> 1760 bytes
```

**Compile cost: ~40 ms per shader through the command line, including process
spawn.** The library form has no spawn and is a fraction of that, it happens
once per shader variant, and Babylon already compiles its effects
asynchronously. This is not a per-frame cost.

## Two routes to the SPIR-V, and why shaderc is the better one

`raster/spirv.cpp` already emits SPIR-V from this engine's own AST — 733 lines,
and no non-test callers. So there is a choice:

* **Finish `spirv.cpp`.** No new dependency, and it already fits the tree. But
  it means implementing the whole of GLSL ES lowering — every operator, every
  built-in, every control-flow shape — correctly enough that a driver does not
  reject or, worse, quietly miscompile it. That is a large surface with no
  oracle.
* **Emit translated GLSL TEXT from the AST and hand it to shaderc.** The front
  end this tree already has does the parsing, validation and leniency; shaderc
  does the lowering and the optimisation. The translation is a text rewrite of
  the three things above, driven by tables the linker already builds.

**The second is the better division of labour.** Our front end is good at
reading what pages actually send — it is lenient in the ways
`docs/raster.md` records, and it has a corpus of p5, Phaser and Babylon shaders
behind it. glslang is good at lowering correct GLSL to correct SPIR-V. Neither
job is one the other wants.

## Should the fork be MODIFIED? Measured: no — and it does not need to be

There is a fork at `github.com/alexios-angel/shaderc`. The useful question is
what it should carry, and the measurements say **a packaging fork, not a
compiler fork.**

### The restriction is not shaderc's

`libshaderc_combined.a` contains **18,841 glslang and SPIRV-Tools symbols**.
shaderc is a thin C/C++ API over glslang; the "ES shaders for SPIR-V require
version 310" rule lives in glslang's version checking. So "modify shaderc" means
"modify glslang" — a compiler of well over a hundred thousand lines, in a
security-relevant position, that would then need rebasing forever.

### And upstream already does the job, with two flags

Measured with stock shaderc 2026.3, no patches:

```
-std=310es                     forces the version, overriding the shader's own
                               `#version 300 es` — a WARNING, not an error
-fauto-map-locations           assigns the locations glslang was demanding
-fauto-bind-uniforms           assigns the block bindings
```

With those, the ES 3.00 shader that upstream rejected **compiles unmodified to
the same 2160 bytes** as the hand-annotated version. For WebGL 2 there is no
source translation to do at all.

### But we should inject our OWN locations rather than use `-fauto-map-locations`

Not because auto-mapping fails — because of what this engine has already told
the page. Locations here are assigned at link time in declaration order,
`getAttribLocation` reports them, and `bindAttribLocation` is deliberately a
no-op that says so. **The page then uses those numbers in
`vertexAttribPointer`.** If glslang picks its own, the SPIR-V's inputs and the
page's buffer bindings agree only by coincidence. Injecting
`layout(location=N)` from `gl_program::attribute_names` makes them agree by
construction, and the same argument applies to `layout(binding=)` against
`block_bindings`.

So the auto flags are the proof that no patch is needed; the shipped path should
still pass our own numbers.

### WebGL 1 is where real translation is needed, and it belongs HERE

ES 1.00 uses `attribute`, `varying` and `gl_FragColor`, all removed in ES 3.00:

```
es1b.frag:3: error: 'varying' : no longer supported in es profile; removed in version 300
es1b.frag:6: error: 'gl_FragColor' : undeclared identifier
```

That is a dialect translation — `attribute` to `in`, `varying` to `in`/`out` by
stage, `gl_FragColor` to a declared output. **It belongs in ctbrowser and not in
a compiler fork**, because it needs this tree's own tables: which stage is being
compiled, which varyings the linker matched, and what `shader::fragment_output`
already records. It is also then testable against the p5, Phaser and Babylon
shaders already sitting in `tests/glsl/`, which a patch inside glslang would not
be.

### What the fork IS worth carrying

* **Pinned dependencies.** Upstream's build runs `git-sync-deps`, a Python
  script that fetches glslang, SPIRV-Tools and SPIRV-Headers from the network at
  configure time. This tree pins everything and builds it into a sysroot —
  `tools/build-boost-mingw.sh` and its four siblings — and a fork that vendors
  those at fixed revisions fits that exactly.
* **A trimmed build.** HLSL, the `glslc` command line, tests, examples and the
  fuzzers are all build options, not code changes: `SHADERC_SKIP_TESTS`,
  `SHADERC_SKIP_EXAMPLES`, `ENABLE_HLSL=OFF`. The Homebrew bottle is 41.7 MB
  installed and this engine wants the library only, in a Windows dist that is
  already 469 MB.
* **The llvm-mingw cross build**, which is the reason four other libraries in
  this tree have a build script each.

None of that is a change to the compiler. If a real gap turns up later, the
trade to weigh is "carry a patch against glslang forever" against "carry a few
hundred lines of translator we own and can test" — and this tree has been much
happier owning the small thing.

## The compiler is the SMALL part

Compiling the shader is one piece of running a page's draw on the GPU. The rest,
none of which exists:

* **Vertex input state.** `vertexAttribPointer` describes a buffer layout; SDL_GPU
  wants a `SDL_GPUVertexInputState` built at pipeline creation. The VAO tables
  are there; the mapping is not.
* **Uniform delivery.** The software path reads uniforms and std140 blocks by
  name; SDL_GPU wants push constants or bound uniform buffers, with the
  bindings decided at translation time.
* **Textures and samplers.** `gl_texture` holds a `paint::bitmap`; a GPU path
  needs uploads, sampler objects and descriptor bindings.
* **Render passes and framebuffers.** The render-to-texture support added on
  2026-08-03 targets a `paint::bitmap`. A GPU path needs real attachments, and
  the post-process and shadow paths depend on them.
* **Getting the picture back.** The page composites through the software
  painter, so a GPU-rendered canvas has to be read back into a bitmap — or the
  whole compositor has to move, which is a much larger change.
* **A pipeline cache.** Every state combination is a pipeline object; building
  one per draw would give back everything the GPU won.

## The consequence nobody gets to skip: THE GOLDENS

All sixteen goldens are **byte-compared, on Linux and on the Windows
cross-build**. GPU rasterisation will not be byte-identical to the software
rasteriser — fill rules, interpolation precision and texture filtering all
differ, legitimately, between implementations and between drivers.

**So the GPU path cannot replace the software one; it has to be an opt-in fast
path with the software rasteriser as the reference.** That is not a hedge, it is
the design constraint: the goldens are how this tree knows anything, and a
change that makes them non-deterministic removes the instrument while adding the
feature. The software path stays the oracle, and the GPU path is checked against
it with a tolerance rather than a byte compare — which is a different kind of
test and has to be built too.

## And the measurement has to happen on Windows

`docs/platform.md`: a Linux binary here sees only lavapipe. The Windows
cross-build gets an Intel Arc, and `tools/remote-build.sh windows` already
produces and verifies those binaries. **A speedup measured on lavapipe is a
lower bound, not the answer** — and it is still worth having, because if
lavapipe is 100x the interpreter the case is made regardless of what the Arc
does.

## Staging, if it is taken up

0. **A bench that isolates the claim**, before any of it: the same shader and
   the same fragment count through the interpreter and through a Vulkan
   pipeline, reported side by side. The 1.03 M frag/s above is half of it.
1. **The translator**: AST -> desktop GLSL text with locations and bindings
   injected from the link tables. Testable on its own against the p5, Phaser and
   Babylon shader corpora already in `tests/glsl/` — every one should come out
   the other side and compile.
2. **libshaderc as a library**, cross-compiled for llvm-mingw the way Boost,
   mimalloc, simdutf and cpptrace already are. It is the largest of them.
3. **One pipeline, one draw**: `webgl-triangle.html` on the GPU, compared to its
   golden with a tolerance.
4. **Uniforms, textures, then render targets**, in that order, each with the
   software path as the oracle.
5. **p5, then Phaser, then Babylon**, which is the order they were adopted and
   the order of how much they ask for.

**This is a bigger piece of work than the WebGL 2 support was**, and the honest
reason to do it is not tidiness: it is that 1 M fragments per second is a
ceiling no amount of tuning the interpreter will lift. `docs/performance.md`
records six changes that between them took a Phaser frame down 41%; this is the
change that takes a shader down 99%.
