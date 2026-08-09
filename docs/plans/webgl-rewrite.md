# WebGL, rebuilt on ANGLE alone

## START HERE

**Where it stands:** the rewrite is done and working. `webgl2_ratchet` reads
**9 of 10**, the ANGLE suite reads **65 of 73**, and the tree builds clean on
both presets. All 64 methods of the translator are written.

**The one remaining task** is rung 10: Babylon clears but draws no geometry.
The cause is understood as far as this - Babylon needs a `#version 300 es`
preamble (there is a `TODO(rung 10)` in `webgl_context::shader_source` with the
exact four lines), and adding it gets Babylon all the way to a real indexed draw
where **ANGLE then segfaults inside `updateOneUniformBuffer`**.

**Do this first**, before reading anything else here:

```bash
../infra/azure-build-server/server.sh start     # and allow-ip if the IP rotated
tools/remote-build.sh                            # ALWAYS build on the devbox
ssh devbox 'cd ~/projects/compile-time-browser &&
  CTBROWSER_GL_UBO=1 /tmp/build-angle/tests/ctbrowser-test-webgl2_ratchet 2>&1 | rg "^\[ubo\]"'
```

**Three hypotheses are already refuted** - do not spend a pass on any of them:

1. The UBO index/binding/buffer numbers disagree. **They agree.**
2. Babylon's shader source lacks the `Mesh` block. **It has it.**
3. `Mesh` answering -1 is a bug. **It is not** - GL drops an unread block and
   answers GL_INVALID_INDEX, which is correct and needs no buffer.

**The suspect that fits the stack** is a binding point with a buffer bound but no
storage behind it: `bindBufferBase` before `bufferData`. There is a
`TODO(rung 10)` in `webgl_context::bind_buffer_base` naming the one measurement
that settles it.

**Two diagnostics are permanent** and cost nothing when unset:
`CTBROWSER_GL_UBO=1` (index, binding and buffer together) and
`CTBROWSER_GL_SRC=1` (what actually reached the compiler).

**The discipline that this work kept failing on**, stated once: change ONE thing
between two runs and print both numbers side by side in the SAME command. Four
wrong conclusions in this file came from not doing that. And check a POSITIVE
signal - "tests passed", a linked binary - because an absent failure and an
absent run look identical, which turned an unreachable devbox into a reported
clean build.

**The ratchet records are targets, not scoreboards.** `tests/corpus/webgl2/webgl2-ratchet.txt`
still says 10 and the test reports going backwards. Do not lower it to match.

---

Decided 2026-08-04, after the ANGLE port spent five commits chasing a Babylon
scene that drew 267 times and painted nothing. **Delete the whole GL stack and
write it again against ANGLE only.** Tests and functionality regress on purpose;
the point is to break it where it is already broken and find out what has to be
fixed, rather than keep grafting onto a structure whose shape produces this class
of bug.

## What the five wasted commits were actually about

Every one of them was the same defect wearing a different name:

| commit | the call | what it did |
|---|---|---|
| `8f446a0` | `uniformBlockBinding`, `bindBufferBase` | recorded itself, did nothing |
| `ce222a5` | `cullFace` | recorded itself, did nothing |
| `ce222a5` | `frontFace` | updated software state, told ANGLE nothing, recorded nothing |
| (uncommitted) | `bufferSubData` | updated the software buffer, told ANGLE nothing |
| (uncommitted) | `bindVertexArray` and its trio | updated software state, told ANGLE nothing |

**Nineteen `webgl_context` methods have no ANGLE branch at all**, found by
scanning for method bodies that never mention `angle_`. The ledger cannot see
them, because a call that never learned it should forward also never learned to
record that it didn't.

THE STRUCTURAL CAUSE, which is the whole reason for the rewrite: `webgl_context`
keeps a SOFTWARE MIRROR of GL state (`state_`, `buffers_`, `programs_`) and then,
separately and by hand, forwards some calls to ANGLE. Two implementations of one
job, which is the exact fault this tree has already paid for twice - two URL
parsers, two base64 decoders - and both times the fix was to delete one.

## The rule the new design is built on

**There is no second implementation, so there is nothing to drift.** A WebGL call
translates to a GLES call and returns; no mirror, no `state_`, no software
rasteriser to agree with. State that WebGL needs and GLES will not answer
(the `WebGLBuffer` wrapper objects a page holds, say) is a HANDLE TABLE and
nothing more - it stores identity, never behaviour.

That makes the failure mode above structurally impossible: there is no state to
update instead of forwarding, and a call that is not written is a compile error
at the binding site rather than a silent no-op.

## What goes

```
src/raster/glsl.cpp glsl_eval.cpp glsl_preprocess.cpp glsl_translate.cpp   3598
src/raster/softgl.cpp spirv.cpp                                            1110
src/raster/gles.cpp                                                         666
src/shell/page/webgl.cpp                                                        1496
include/ctbrowser/raster/{glsl,glsl_translate,softgl,spirv,gles}.hpp        1057
include/ctbrowser/shell/page/webgl.hpp                                           560
tests/{glsl_basics,glsl_translate,softgl_basics,spirv_basics}.cpp
tests/{gles_basics,webgl_basics,webgl_angle}.cpp
```

~8,500 lines. The GLSL front end goes too: ANGLE has a real one, and ours existed
only to feed a software rasteriser that is also going.

## What stays, and why

- **`src/shell/bindings/webgl.cpp`** - the JavaScript surface. It is the
  SPECIFICATION of what a page can call and it is not the broken part. It gets
  rewired to the new context, and every call it makes that the context does not
  have becomes a compile error, which is the point.
- **`tests/corpus/webgl2/webgl2-ratchet.txt`, `tests/corpus/webgl2/webgl2-api.txt`, `tests/corpus/babylon/babylon-*.txt`,
  the goldens** - these are the TARGETS TO RE-EARN, and deleting them would
  destroy the only record of what used to work. The ratchets will read lower for
  a while and that is honest; what must not happen is quietly lowering the
  recorded level to match. `--advance` only ever moves up.
- **`tools/fetch-angle.sh` and the published ANGLE build** - stage 0 measured
  192 M frag/s against the interpreter's 1.03 M and byte-identical pixels on
  Linux and Windows. That work stands.

## Order

1. **Delete, and let the build break.** One commit, nothing else in it, so the
   revert is a single `git revert`.
2. **A minimal `raster/gl.hpp`** - the ANGLE device, no software twin. Still no
   EGL or GLES type in a public header; `tests/lint/api_surface` enforces it.
3. **`webgl_context` as a pure translator**, rebuilt call by call from what
   `webgl_bindings.cpp` needs. Handle table only.
4. **Re-earn the ratchets in order**: `webgl_basics`, then p5's WEBGL cube, then
   Phaser, then Babylon's scene. Each one that comes back is a real measurement
   because the record was never lowered to meet it.
5. **Safe mode** - ANGLE's own device by default, SwiftShader selectable at
   runtime. `open_display` currently hard-codes SwiftShader; that inversion is a
   deliberate part of the rebuild, not a leftover.

## The measurement discipline, which is what actually cost the five commits

Change ONE thing between two runs and print both numbers side by side in the
SAME command. Two wrong conclusions came from not doing this: "neither backend
paints" compared an ANGLE run against a remembered software run, and "the example
binary is broken" compared an ANGLE run against `ctbrowse` runs that never set
`CTBROWSER_WEBGL` at all. Both times the difference was attributed to whichever
variable was being thought about.

`CTBROWSER_GL_LEDGER=1` (what a run failed to forward) and `CTBROWSER_GL_PROBE=1`
(whether GL itself painted, before the composite can lose it) are keepers from
that phase and should survive into the new code.

## Step 3: the contract, enumerated from the bindings

`webgl_bindings.cpp` calls **64 distinct methods**. That is the whole of what
the new `webgl_context` must provide - not a design decision to be made, a fact
to be read off the caller. Tick them off as they are written; each one is a
direct GLES call and a return, with no state kept that GL can be asked for.

**context and surface** (12)

- [x] `surface`
- [x] `height`
- [x] `set_version`
- [x] `clear`
- [x] `clear_color`
- [x] `clear_depth`
- [x] `scissor`
- [x] `set_enabled`
- [x] `take_error`
- [x] `refuse`
- [x] `refused`
- [x] `shader_error`

**shaders and programs** (16)

- [x] `create_shader`
- [x] `shader_source`
- [x] `compile_shader`
- [x] `shader_compiled`
- [x] `shader_log`
- [x] `create_program`
- [x] `attach_shader`
- [x] `link_program`
- [x] `program_linked`
- [x] `program_log`
- [x] `use_program`
- [x] `active_attributes`
- [x] `active_uniforms`
- [x] `attribute_location`
- [x] `get_uniform_block_index`
- [x] `set_uniform`

**buffers and attributes** (14)

- [x] `create_buffer`
- [x] `bind_buffer`
- [x] `buffer_data`
- [x] `buffer_sub_data`
- [x] `bind_buffer_base`
- [x] `enable_attribute`
- [x] `attribute_pointer`
- [x] `attribute_at`
- [x] `attribute_divisor`
- [x] `create_vertex_array`
- [x] `bind_vertex_array`
- [x] `delete_vertex_array`
- [x] `is_vertex_array`
- [x] `bound_vertex_array`

**textures and framebuffers** (8)

- [x] `create_texture`
- [x] `bind_texture`
- [x] `active_texture`
- [x] `texture_image`
- [x] `texture_parameter`
- [x] `bind_framebuffer`
- [x] `framebuffer_texture`
- [x] `framebuffer_status`

**draws and pipeline state** (10)

- [x] `draw_arrays`
- [x] `draw_elements`
- [x] `draw_arrays_instanced`
- [x] `draw_elements_instanced`
- [x] `cull_face`
- [x] `front_face`
- [x] `depth_func`
- [x] `depth_mask`
- [x] `blend_func`
- [x] `delete_object`

**everything else** (4)

- [x] `texture_from_bitmap`
- [x] `uniform_block_binding`
- [x] `version`
- [x] `viewport`

### Rung 10: the source HAS Mesh, so the -1 is normal GL

`CTBROWSER_GL_SRC=1` prints what `shader_source` actually sends:

    [src] shader=1 bytes=8389  version=1 layout=1 Mesh=1
    [src] shader=2 bytes=13357 version=1 layout=1 Mesh=1

Babylon's shaders do contain the `Mesh` block and do get the preamble. Reading 1
is refuted.

WHICH MAKES THE -1 UNREMARKABLE: GL drops a uniform block the shader never
reads, and `glGetUniformBlockIndex` then answers GL_INVALID_INDEX. That is
correct behaviour, Babylon binds nothing for it, and ANGLE needs nothing for it.
So `Mesh` is a red herring - the third dead end on this rung, and worth writing
down so nobody spends another pass on it.

WHAT IS LEFT, and it fits the stack better than anything so far: ANGLE dies in
`updateOneUniformBuffer` for a binding point that HAS a buffer bound but whose
buffer has no STORAGE - `bindBufferBase` before `bufferData`, so the
`BufferHelper` is null. Light0 binds buffer 1 at binding 3; check whether buffer
1 ever received `bufferData` before the draw, and in what order.

That is one more log line - buffer name and size at `buffer_data` time, beside
the existing `[ubo] base` lines - and it is the next thing to run., and `Mesh` is the odd one

Logged with `CTBROWSER_GL_UBO=1`, one program, one run:

    Material -> 0,  bind 0 -> 0,  base index=0 buffer=7
    Scene    -> 1,  bind 1 -> 1,  base index=1 buffer=6
    Light0   -> 2,  bind 2 -> 3,  base index=3 buffer=1
    Mesh     -> -1   (no bind, no buffer)

So the mismatch hypothesis is WRONG: index, binding and buffer line up for every
block that has one. What stands out instead is `Mesh` answering -1 -
GL_INVALID_INDEX - while Babylon plainly expects that block to exist, and then
binds nothing for it.

TWO READINGS, and they need different fixes, so measure before choosing:

1. The shader genuinely has no `Mesh` block, because the preamble made it ES
   3.00 but something else about the source dropped the declaration. Then the
   bug is in what reaches the compiler - dump the exact source `shader_source`
   sends and read it.
2. `Mesh` exists but the lookup ran against the wrong program, or before the
   link. Then the -1 is real but premature.

Reading 1 is testable in one command and should be done first.

### SUPERSEDED: the #version crash is INSIDE ANGLE

The segfault has a stack now, and it is not in `versioned()`:

    rx::vk::DescriptorSetDescBuilder::updateOneUniformBuffer
    rx::ProgramExecutableVk::updateUniformBuffersDescInfo
    rx::ContextVk::handleDirtyGraphicsUniformBuffers
    rx::ContextVk::setupDraw -> setupIndexedDraw -> drawElements
    GL_DrawElements

So the preamble WORKS: with it, Babylon's shaders compile as ES 3.00, declare
their uniform BLOCKS, and reach a real indexed draw. The crash is ANGLE
dereferencing the buffer for a block that has no buffer bound to its binding
point - which means `getUniformBlockIndex`, `uniformBlockBinding` and
`bindBufferBase` are not agreeing on a number, not that any of them is missing.
All three are forwarded (group 2 and group 3).

That is the LAST rung and it is now a specific, small question: log the block
index, the binding point and the buffer for one program, in one run, and find
which of the three disagrees. Not a suspect list - three numbers that must match.

Reverted for now, because a segfaulting tree is worse than a 9/10 one. Put it
back with the binding bug fixed, not before.

### 9/10, and the suite is 65/73

Two wires, both of them the same omission in different places: the rewrite moved
readback out of every draw and then called it from nowhere.

- `present_webgl_contexts()` runs at the END OF A FRAME, after the
  requestAnimationFrame callbacks, which is the only place that is a frame
  rather than a draw.
- `readPixels` presents FIRST. A page calls it in the middle of the frame it
  just drew, so reading the canvas bitmap returned the previous frame - nothing
  at all on the first one, which looks exactly like a draw that missed. That one
  line took the ratchet from 5/10 to 9/10.

REMAINING: rung 10. Babylon clears (the blue reaches the pixels) and draws no
geometry - `matReady=false`, so its material never becomes ready, which is a
SHADER COMPILE failure rather than a draw failure.

The known cause is the `#version` preamble the deleted code supplied: a shader
using `layout(...)` is ES 3.00 and the directive is mandatory, but Babylon
assembles its bodies without one and relies on the browser's preamble.
Restoring it in `shader_source` SEGFAULTS, so it is reverted rather than
committed. Find out why before putting it back - the crash is new information
about the rewrite, not an obstacle to route around, and a segfaulting tree is
worse than a 9/10 one.

### SUPERSEDED: WORKING TO 5/10

Measured, both drivers side by side, by the new `tests/unit/gl_basics.cpp`:

    fastest        ok=yes  ANGLE (Google, Vulkan 1.3.0 (SwiftShader Device ...))
    deterministic  ok=yes  ANGLE (Google, Vulkan 1.3.0 (SwiftShader Device ...))

`fastest` could not come up at all before that - VK_EXT_headless_surface and
VK_KHR_surface are simply absent on a box with no GPU - so it now FALLS BACK to
SwiftShader. Safe mode as a floor rather than a cliff. `available()` had the same
bug and asked only about `fastest`, which reported the whole subsystem missing on
a machine where software rendering works perfectly.

webgl2_ratchet now reads **5/10** where it read "WebGL not supported": the
context comes up, shaders compile, programs link, draws issue.

THE BLOCKER IS present() AND NOTHING ELSE CALLS IT. The rewrite moved the
readback out of every draw - the old code copied the whole surface 267 times for
one Babylon frame - and put it in `present()`, once per frame. Nothing invokes
it. So the device paints correctly and the canvas never receives a pixel, which
is exactly the "drew but did not paint" symptom this whole rewrite started from,
now with a known cause instead of five wrong guesses.

Next: call `present()` where a frame ends rather than where a draw ends, and
confirm with the ratchet rather than the call count.

### SUPERSEDED: WITH ANGLE: 59/72, and the likely cause is the driver DEFAULT

The ANGLE build compiles clean and runs. Thirteen failures, all WebGL-dependent:
the four render tests, webgl2/babylon/p5 ratchets and API probes, plus
bindings_basics and widgets_basics.

MOST LIKELY CAUSE, and it is a consequence of a deliberate change rather than a
mystery: `driver::fastest` now asks for ANGLE's own device selection, where the
deleted code hard-coded SwiftShader. The devbox has no GPU, so a real Vulkan
device may simply not come up - and since `getContext` now correctly returns null
when the device fails, every one of these pages takes the "WebGL not supported"
branch and fails for one reason wearing thirteen names.

THE CHECK, and do it before any other change: construct a `raster::gl::device`
with each driver in turn and print `ok()` and `renderer()` for both, in ONE
command, side by side. If `fastest` is false and `deterministic` is true, the fix
is that `fastest` falls back to SwiftShader rather than failing - which is what a
browser does and what makes safe mode a floor rather than a cliff.

Do NOT change the driver default and re-run the suite hoping the number moves.
That is the mistake this document opens by describing.

### SUPERSEDED, and still true of the no-ANGLE build: 62/71, and the 9 failures are ONE cause

The tree compiles and links again. `ctest --preset default` reads **62 of 71**,
and every failure is the same thing: the default build has no ANGLE, and there is
no software fallback any more, so `create_vertex_array` returns 0 and the page
says "Unable to create VAO".

That is the rewrite's central trade arriving on schedule rather than a defect.
The old tree answered WebGL calls without a driver because it had a rasteriser of
its own; this one cannot, and should not pretend to. Two consequences to settle,
in this order:

1. **A context that cannot come up must return null from `getContext`**, so a
   page takes its own no-WebGL path instead of throwing halfway through a frame.
   That is what a real browser does and what these nine failures are really
   reporting.
2. **The default build should fetch ANGLE**, or those render tests should skip
   with a reason the way `svg_basics` does without plutosvg - loudly, never
   silently.

### SUPERSEDED: groups 4 and 5 have never been compiled

The devbox became unreachable during the group 4-5 build, and `rg "error:"` over
a failed ssh matches nothing - so an empty result looked exactly like a clean
build and was reported as one. It is not evidence of anything.

BEFORE ANYTHING ELSE next session: `../infra/azure-build-server/server.sh start`
(then `allow-ip` if the home IP rotated), rebuild, and find out. Expect real
errors - nineteen methods and a changed `delete_object` signature went in
untested.

The check that would have caught it is to grep for a POSITIVE result rather than
the absence of a negative one - "tests passed", a linked binary - because an
absent failure and an absent run look the same.

### The blocker group 2 found: the bindings are NOT backend-neutral

`webgl_bindings.cpp` passes uniforms as `raster::glsl::value`, and names
`raster::glsl::base::f` in its own uniform-shape table - types from the GLSL
front end that this rewrite deleted. So "the bindings are the specification and
they stay untouched" was half right: they specify the CALLS correctly, but they
carry a type from the layer being removed.

That is not a reason to bring the GLSL header back. `set_uniform` needs a shape
and some floats or ints, which is a small POD that belongs in `shell/page/webgl.hpp`:

    struct uniform_value {
        int rows = 1;         // 1 is a scalar, 3 a vec3, 3x3 a mat3
        int cols = 1;
        bool integer = false;
        std::vector<float> data;   // ints widen; a uniform is at most a mat4
    };

The bindings' `uniform_shape` table and `uniform_value()` helper are retargeted
to it. That is the ONLY edit the bindings need, it is mechanical, and it removes
their last dependency on the deleted stack.

DO THIS BEFORE THE REST OF GROUP 2, because the other fifteen methods compile
against a header that still has to grow this type and there is no sense writing
them twice.

THE HANDLE TABLE is the only state. A page holds `WebGLBuffer` and
`WebGLProgram` objects, and those need identity that survives across calls - but
identity ONLY. The moment the table starts caching what a buffer contains or
which program is bound, it is the mirror again and the drift is back.

`take_error`, `refuse` and `refused` are the WebGL error contract and stay: a
page reads `getError`, and the leniency rules in docs/history/webgl2.md are about
what this engine refuses BY NAME rather than silently. That is a different thing
from the silent no-ops this rewrite exists to eliminate, and it is worth keeping.
