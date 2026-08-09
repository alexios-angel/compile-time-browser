# Babylon.js, from "renders a box" to functional

**Where it stands: `tests/corpus/webgl2/webgl2-ratchet.txt` reads 10/10 — Babylon builds an
engine, a scene, a camera, a light and a mesh, and the box lands on the canvas
in the right colour.** That is one rung. It is not a working renderer, and this
plan is about the distance between the two.

**Everything below is measured on this tree, today, not estimated.** The probe
that produced it built one page per feature, ran sixty frames, and read the
CANVAS rather than asking Babylon whether it was happy — because every one of
the failures here reports success from the JavaScript side.

## What already works

Confirmed by pixels, not by the absence of an exception:

| | evidence |
|---|---|
| meshes: box, sphere, ground, `MergeMeshes`, `clone`, `dispose` | a second mesh set apart renders in its own colour, 142 distinct colours |
| instancing | `createInstance` at a second position paints 1928 pixels where one mesh paints 1444 |
| world transforms | rotation, position and scaling each change the silhouette, set before frames AND changed during them |
| animation | `Animation.CreateAndStartAnimation` advances `rotation.y` to 1.48 over 60 frames, and `getDeltaTime` advances |
| cameras | `FreeCamera` and `ArcRotateCamera`; moving the camera during frames changes the picture |
| lights | hemispheric, point and directional all change shading; a point light gives 18 distinct colours |
| fog | `FOGMODE_EXP` produces a real gradient, 7 colours |
| picking | `scene.pick(32, 32)` reports `hit=true` on the box |
| construction of `ShadowGenerator`, `RenderTargetTexture`, `SceneLoader` | no throw; whether they *do* anything is untested and unclaimed |

**`getDeltaTime` returning 0 on the first frame and 16 after** is worth noting:
the render loop is being driven properly, which is what makes every
frame-dependent feature above measurable at all.

## What is broken, in the order the measurement puts them

### 1. Textures sample as BLACK — every textured material — FIXED

**`drawArrays` set a texture sampler on its draw request and `drawElements` did
not.** Every indexed draw sampled `(0, 0, 0, 1)` however correct the texture
was — and an indexed draw is what a mesh is. That is why the hand-written
`drawArrays` quad below sampled perfectly through the same context. There is one
sampler now, shared by both paths.

**How it was found, since the method transfers.** The box came back `0,0,0` with
a `DynamicTexture`, again with a `RawTexture`, and again with the texture on
`emissiveTexture` — while the mesh had UVs and `isReady()` was true. A plain
WebGL 2 textured quad through the *same context* rendered green, which said the
sampler, the texture unit and the rasteriser were all fine and moved the search
to what was different about Babylon's draw. What was different is that a mesh is
drawn with `drawElements`.

**And one of the two readings was the harness's fault**, which is worth knowing
before trusting a rung: ratchet rung 2 set `emissiveColor` to white and Babylon's
shader ADDS the emissive texture, so it saturated to white and read the same
before and after the fix.

### 2. A post-process blanks the canvas

`new BABYLON.PassPostProcess('pass', 1.0, camera)` — the *identity* post-process,
which is meant to copy the frame and change nothing — leaves the canvas
`0,0,0` across all 4096 pixels. Not the clear colour: black. The scene's own
render is gone too.

A post-process renders the scene into a framebuffer and then draws a full-screen
quad sampling it, so this is very likely the same fault as (1) reached from the
other side, plus framebuffer attachments. Worth doing straight after textures
and worth re-measuring before assuming they are one bug.

### 3. `wireframe` draws nothing

`material.wireframe = true` gives a canvas of pure clear colour. The rasteriser
has no line topology for filled meshes, so the draw silently produces no
fragments. **Refusing loudly would be better than the current silence** even
before it is implemented.

### 4. `PBRMaterial` throws: `ArrayBuffer.isView` is not a function — FIXED

One missing standard-library method, and the entire physically-based material
path was unreachable — the cheapest item on the list by a wide margin. Both PBR
materials construct now and the surface probe reads 41/43, with only the two
corpus gaps left.

It goes on the NATIVE's own property table: `define_native` makes a function
that is also an object, so casting it to an `object_object` writes the property
somewhere nothing will ever look for it.

### 5. `BABYLON.GUI` is not in the bundle at all

`babylon.js` is the core UMD build; the GUI ships as a separate
`babylon.gui.js`. Nothing is broken — the corpus simply does not include it.
**A corpus decision, not an engine one**, and it belongs at the end of the
ladder rather than the middle.

## The ladder

`tests/babylon_ratchet.cpp` measures, `tests/corpus/babylon/babylon-ratchet.txt` records,
`tools/corpus/babylon-ratchet.py` drives — the shape used four times now (p5, Phaser,
WebGL 2, modules).

**It starts where the WebGL 2 ratchet stops.** That one asks "does Babylon draw
at all" and is answered; this one asks what a scene can contain.

1. **a scene renders** — the box on the canvas, in the right colour. Green on
   day one, deliberately: a ladder whose first rung fails cannot tell a broken
   engine from a broken harness.
2. **a texture samples** — a `DynamicTexture` painted green shows up green.
3. **a second material and mesh** — two meshes with different materials, both
   in the picture, each its own colour.
4. **transforms animate** — the silhouette at frame 60 differs from frame 0
   under a running `Animation`.
5. **a directional light shades** — a lit face and an unlit face differ, which
   is what says the normal matrix arrived.
6. **specular** — a highlight exists: the brightest pixel is brighter than the
   diffuse term alone.
7. **alpha blending** — a half-transparent mesh over another shows both.
8. **a post-process runs** — `PassPostProcess` leaves the scene visible.
9. **a shadow lands** — a `ShadowGenerator` darkens the ground under the box.
10. **PBR** — a `PBRMaterial` renders.
11. **glTF** — `SceneLoader.ImportMesh` of a small embedded asset appears.
12. **GUI** — a `babylon.gui.js` control draws.

**It reads 7/12.** Rung 8, the post-process, is the last purely-engine item;
9 and 10 follow it. 11 and 12 need corpus additions and are named so the ladder
does not have to be rewritten when they arrive.

## The surface probe

`tests/babylon_api.cpp` + `tests/corpus/babylon/babylon-api-probe.js` + `tests/corpus/babylon/babylon-api.txt`,
to `p5_api.cpp`'s shape: **how WIDE the working surface is, as opposed to how
far one scene gets.** The ratchet stops at its first failure and tells you one
thing; the probe runs everything and tells you the shape of the gap.

Both are needed here for the reason `phaser-api.py` was written: the Phaser
ratchet read 10/10 while `(5).hasOwnProperty` was undefined, because nothing on
the ladder happened to ask a number for a property.

**Probes that are expected to fail are included on purpose**, exactly as
`webgl2-api-probe.js` does it, and the recorded file says which. Recording "not
implemented" as a fact beats discovering it later as a wrong answer.

## The interactive demo, and the two faults it found

`examples/pages/babylon-orbit.html` is a cube, a sphere and a plane under them,
lit by a directional light that is nowhere in the picture: **drag to orbit,
wheel to zoom**, with nothing in the page handling a mouse — the camera is an
ArcRotateCamera with `attachControl`, so the whole path is Babylon's own input
manager reading DOM events off the canvas.

`tests/babylon_interaction.cpp` is the claim behind it, and it asserts TWO
things every time: the camera moved AND the picture changed. Those are not the
same — a camera whose angle updates while the render does not follow is exactly
the half-working this corpus keeps producing.

**Nothing dispatched a `wheel` event at all.** The notch went straight to the
document scroller, so a page could not zoom, could not scroll its own canvas and
could not refuse the page scroll. `deltaY` is in pixels and its sign is the
opposite of this engine's `wheel_y`, which is why the test asserts the
DIRECTION rather than that the number moved.

**And `'onwheel' in element` was false.** Every element had the handler
properties *assignable* but not *present*, so feature detection failed —
Babylon picks its wheel event name with

```js
"onwheel" in document.createElement("div") ? "wheel"
  : document.onmousewheel !== undefined ? "mousewheel" : "DOMMouseScroll"
```

and fell through to `DOMMouseScroll`, a Firefox-only name nothing here sends.
Every listener was attached, every event was dispatched, and the two sets had
different names — the same shape as the `pointerdown`/`mousedown` fault
`bindings.cpp` already records. The handler properties exist now, and
deliberately only for events this engine can actually dispatch: a detection that
answers yes for an event that never fires is worse than one that answers no.

## The example page

`examples/pages/babylon-scene.html`, with a golden. Not a duplicate of the
tests: an example is what a HUMAN looks at, and the goldens are what catch a
render changing by accident. It is deterministic for the same reason the other
example pages are — `Math.random` is seeded — and it draws a scene that
exercises what the ladder has reached rather than what it has not.

## Lighting, which is measured rather than eyeballed

`tests/babylon_lighting.cpp` computes what a Lambert surface should be and
compares: a plane whose normal is exactly `(0, 0, -1)` under one directional
light, with no specular, no emissive and no ambient, so

    colour = diffuseColor * intensity * max(0, dot(N, -L))

is the whole answer. It reads **204 for `dot = 1`, 102 at 60 degrees, 144 at 45,
0 facing away** and 102 for a material at half the diffuse colour — every one
exact.

That is a different claim from ratchet rung 5, which only asks whether a light
produces two different shades. A transposed normal matrix, an intensity applied
twice or a Lambert term clamped in the wrong place would all still give two
shades and all still be wrong. **`examples/pages/babylon-scene.html` is lit for
the same reason** — no `disableLighting` anywhere, a directional key light and a
dim hemispheric fill, a box turned so three faces catch the light at three
angles, and a sphere carrying the specular highlight.

## Verification, and the thing most likely to break

* **The thirteen existing goldens must stay byte-identical.** Texture work
  touches the sampler path, and `p5-webgl.html` and the Phaser pages draw
  through it. If one of those moves, the change is wrong.
* p5 12/12, Phaser 10/10, invaders, and every API surface unmoved.
* `tools/check/compare.py` against Chrome for anything where the right answer is a
  judgement call rather than a fact — shading in particular. There is an oracle
  and it is already in the repository.

## What this plan does NOT promise

Babylon is enormous — physics, WebXR, node materials, particle systems, sprite
managers, morph targets, skeletal animation, GPU particles, screen-space
reflections. **"Fully functional" is not a state this engine will reach**, and
saying so up front is cheaper than discovering it at rung 40. What the ladder
above buys is a scene a person would recognise as a 3D scene: textured, lit,
animated, shadowed, and composited. Anything past rung 12 should be added
because a corpus asked for it, which is how every other ladder in this tree grew.
