# WebGL, rebuilt on ANGLE alone

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
src/shell/webgl.cpp                                                        1496
include/ctbrowser/raster/{glsl,glsl_translate,softgl,spirv,gles}.hpp        1057
include/ctbrowser/shell/webgl.hpp                                           560
tests/{glsl_basics,glsl_translate,softgl_basics,spirv_basics}.cpp
tests/{gles_basics,webgl_basics,webgl_angle}.cpp
```

~8,500 lines. The GLSL front end goes too: ANGLE has a real one, and ours existed
only to feed a software rasteriser that is also going.

## What stays, and why

- **`src/shell/webgl_bindings.cpp`** - the JavaScript surface. It is the
  SPECIFICATION of what a page can call and it is not the broken part. It gets
  rewired to the new context, and every call it makes that the context does not
  have becomes a compile error, which is the point.
- **`tests/webgl2-ratchet.txt`, `tests/webgl2-api.txt`, `tests/babylon-*.txt`,
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
   EGL or GLES type in a public header; `tests/api_surface` enforces it.
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
