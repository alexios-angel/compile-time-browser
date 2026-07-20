# PLAN — run johnpitchers/Space-Invaders in ctbrowser

Goal: compile and run the full **Space-Invaders** game
(<https://github.com/johnpitchers/Space-Invaders>, CC0) — a Vite + BabylonJS
4.2 ES-module game — inside ctbrowser, rendering with the software 3D
rasterizer in `babylon.hpp`. No changes to the game's source: it is bundled
verbatim (`tools/js-bundle.py`) and parsed BY VALUE at runtime.

Target mode: **"Traditional 2D" (mode 0)** — a near-flat perspective 3D view.
(Modes 1/2 add orthographic camera + glow + fog; cosmetic, done later.)

## What the game actually is (from source recon)

- **No sprite gameplay.** Aliens/player/ship/bullets/barriers are all **3D
  meshes** (GLB models + `MeshBuilder.CreateBox`). "2D" = camera framing only.
  Sprites are used ONLY for the cosmetic starfield.
- **Frame driver:** `engine.runRenderLoop(cb)`; inside, a `switch(State.state)`
  state machine; a busy-wait on `Date.now()` throttles FPS; then
  `scene.render()`.
- **All gameplay logic runs in `scene.onBeforeRenderObservable` callbacks**
  (movement, spawning, firing) — fired inside `scene.render()`.
- **Collision is load-bearing:** `mesh.moveWithCollisions(v)` must set
  `mesh.collider.collidedMesh` (filtered by `collisionGroup`/`collisionMask`)
  so bullet↔alien/barrier/player hits resolve via `.metadata`.
- **Startup gate:** the loop only runs once `gameAssets.isComplete` — which
  flips true after all 15 async asset callbacks fire (5 `SceneLoader` +
  10 `Sound`) then a `setTimeout(1)`. So Sound/SceneLoader must RESOLVE.
- Assets: 5 GLB models, 10 WAV sounds, 2 PNG starfield sprites (CC0).

## Feature checklist

### A. General web platform (dom.hpp / script.hpp) — reusable, not shims — DONE
- [x] `document.querySelector` / element `querySelector` — tag / `#id` /
      `.class` / compound / descendant combinator (`dom.hpp query_selector`).
      (validated: dom/webapi/pong regression green)
- [x] element `.classList` object: `add` / `remove` / `contains` / `toggle`.
- [x] element `.append(text)` (used by the loading dots script).
- [x] `window.localStorage`: `getItem` / `setItem` / `removeItem` (JS-object backed).
- [x] `navigator.userAgent` (desktop UA, drives mobile detection = false).
- [x] `window.scrollTo` (no-op), `window.onresize` settable.
- [x] `document.documentElement` handle.
- [ ] `querySelectorAll` (returns array) — not yet; add if the game needs it.

### A2. Compile-time architecture (bricks) — DONE, validated on the std::embed clang
- [x] **ctcss BY VALUE**: `ctcss::parse_value(sv)` + `query` (ctcss/value.hpp);
      lenient linear parser (skips @font-face/@keyframes, recurses @media).
      Engine resolves styles via it (`page::style_text()` + `engine::css_sheet`).
      Full suite green through the value-CSS path (incl. render golden).
- [x] **CTJS_NO_GRAMMAR / CTHTML_NO_GRAMMAR / CTCSS_NO_GRAMMAR**: a `#define` that
      skips the lark/Earley grammar table build entirely for value-only TUs. Wins:
      ctjs tens-of-min → 4.2 s; cthtml 14.4 s → 0.8 s; ctcss 4.3 s → 0.8 s. (Value
      paths made grammar-independent: shared helpers relocated to grammar-free
      headers — types.hpp/classify.hpp; value.hpp/vparse/vinterp need no grammar.)

### B. BABYLON core surface (babylon.hpp) — extends the existing shim
Existing: Engine/Scene/ArcRotate+Free+UniversalCamera/Hemispheric+Directional
Light/StandardMaterial/MeshBuilder(Box/Sphere/Ground/Cylinder)/Vector3/Color3/
Color4/glTF loader + `ImportMeshAsync`.

- [ ] **`onBeforeRenderObservable`** on Scene AND per-mesh: real `add(cb)→handle`
      / `remove(handle)`, fired every frame at the top of `scene.render()`.
      (Currently a no-op — THE critical gap: no gameplay without it.)
- [ ] **Mesh surface**: `metadata` (free object), `onDispose`, `visibility`,
      `isVisible`, `material`, collision flags (`checkCollisions`,
      `collisionGroup`, `collisionMask`, `collisionResponse`,
      `collisionRetryCount`), `instancedBuffers`, `registerInstancedBuffer`.
- [ ] **Mesh methods**: `clone(name)`, `createInstance(name)`, `dispose()`
      (fires `onDispose`), `moveWithCollisions(v)`, `calcMovePOV(x,y,z)`,
      `translate(axis,dist,space)`, `rotate(axis,amount,space)`.
- [ ] **Collision detection**: AABB per mesh (geometry bounds × scaling +
      position); `moveWithCollisions` sets `collider.collidedMesh` to the first
      overlapping mesh where `(mover.collisionMask & other.collisionGroup)!=0`.
- [ ] **`Scalar`**: `Lerp(a,b,t)`, `RandomRange(min,max)` (+ Clamp).
- [ ] **`Axis`** (X/Y/Z = Vector3), **`Space`** (WORLD/LOCAL consts).
- [ ] **`Camera.ORTHOGRAPHIC_CAMERA`** const; **UniversalCamera** ortho props
      (`mode`, `orthoTop/Bottom/Left/Right`, `width/height/ratio`), `rotation`,
      `position.x` mutation, `setTarget`. (Perspective works today; ortho later.)
- [ ] **Engine additions**: `audioEngine.unlock()` (static), `onResizeObservable`,
      `getRenderWidth/getRenderHeight`.
- [ ] **Scene additions**: `collisionsEnabled`, fog fields, `meshes` array,
      `deltaTime`, `actionManager` slot, `FOGMODE_LINEAR` const.
- [ ] **`SceneLoader.ImportMeshAsync`** → resolved promise `{meshes:[root,body]}`.
      Milestone 1: return primitive **box** meshes (playable, no asset pipeline).
      Milestone 3: return real GLB meshes.
- [ ] **`AssetContainer`**: `.meshes` array, `removeAllFromScene()`.

### C. BABYLON GUI (babylon.hpp) — HUD overlay
- [ ] `AdvancedDynamicTexture.CreateFullscreenUI` → `addControl` / `dispose`,
      `_canvas.width/height`.
- [ ] `TextBlock`: `text`/`color`/`fontFamily`/`fontSize`/`left`/`top`/
      `textVerticalAlignment`/`textHorizontalAlignment`; rendered into the canvas
      (font8x8) after the 3D pass — score/level/lives/high HUD.
- [ ] `Control` alignment consts (`VERTICAL/HORIZONTAL_ALIGNMENT_*`).

### D. Cosmetic stubs (must construct + not throw; visuals later)
- [ ] `Sound` — constructor CALLS its onLoaded callback (startup gate!);
      `play`/`stop` no-op (milestone 3: wire the WAV mixer).
- [ ] `Sprite` / `SpriteManager` — construct + props; starfield unrendered first.
- [ ] `GlowLayer` — `intensity` + `customEmissiveColorSelector` no-op.
- [ ] `ActionManager` — construct only.

### E. Omit (imported-but-unused / dead code)
`ExecuteCodeAction`, `AssetsManager`, GUI `Style`/`Rectangle`, `TestCode.js`.

## Scaffolding & build
- [x] Bundle verified: `js-bundle.py index.html` → 21 modules, 66 KB, node-clean.
- [x] `examples/space-invaders.html` (minimal head + game body + inlined bundle,
      canvas sized 900×700; heavy CSS stripped) → `space-invaders.inc`.
- [x] `examples/space-invaders.cpp` + Makefile `.inc` rule.
- [ ] CMake `examples/CMakeLists.txt` entry (if per-example listing).
- [ ] Credit upstream (CC0) in NOTICE/README.

## Milestones (each builds + runs headless on the Azure server; babylon.hpp is
in the PCH → ~90 s rebake per iteration, so batch changes)
1. **Loads** — full surface stubbed; bundle runs with zero ReferenceErrors;
   assets complete; reaches TITLESCREEN.
2. **Renders + playable (box graphics)** — observables pumped, box aliens/player/
   bullets move, collision resolves hits, HUD draws, keyboard controls the ship.
3. **Assets** — real GLB models via `ImportMeshAsync`, WAV audio via the mixer,
   starfield sprites; then modes 1/2 (ortho camera + glow + fog).

## Out of scope (BabylonJS parity)
textures/PBR, shadows, particles, post-processing, physics, animations,
WebGL/shader parity, `@babylonjs/loaders` beyond single-mesh GLB.
