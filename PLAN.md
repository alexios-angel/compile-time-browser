# PLAN.md — Does the ctbrowser engine need a garbage collector?

## Answer: **Yes — for reference *cycles*.** (Measured, not assumed.)

ctjs manages every JS value (`object`, `array`, `function`, `environment`)
with **reference counting** — `ctjs::rc<T>`, a constexpr `shared_ptr` analogue
(`include/ctjs/rc.hpp`). Reference counting reclaims acyclic garbage promptly and
deterministically, and needs **no** collector. It **cannot** reclaim reference
*cycles*: `a.o = b; b.o = a` leaves both counts ≥ 1 forever. Real JS — and this
engine's own bindings — create cycles constantly.

### Evidence (all measured on the std::embed build server)

**1. Pure ctjs, cyclic vs acyclic (scratch `cycle_probe`):**
```
acyclic 40k objects  -> heap delta   +1 KB     (refcount frees them)
cyclic  40k objects  -> heap delta +44,374 KB  (never freed)
cyclic  40k (again)  -> heap delta +44,375 KB  (linear, unbounded)
```
Acyclic garbage is reclaimed to the byte; cyclic garbage leaks ~1.1 KB per pair,
without bound. This is a property of refcounting, reproduced directly.

**2. The real Space-Invaders game, 4000 frames, firing/moving (`mem_probe`):**
```
frame   heapMB   handle_nodes  tracked  onclick  click_lst  timers  raf
    0     9.2         171        171       0         1        133     1
  500    16.2        172        172       0         1        131     1
 3000    41.0        188        188       1         1        131     1
```
Heap climbs **+25 MB over 2500 frames (~10 KB/frame, ~2.6 GB/hour at 60fps)**
while every C++ registry stays flat (`handle_nodes` 171→188). The leak is in the
**JS value graph and the babylon world**, not the DOM bindings.

### The two concrete leak sources

- **(A) ctjs reference cycles.** Closures are the main culprit. `make_fn`
  (`vinterp.hpp:521`) captures the closure `environment` **and** the lexical
  `this` *inside* the type-erased lambda: `[self, fn_node, closure, is_arrow,
  lexical_this]`. The game is full of the resulting object↔closure cycles:
  `mesh.onDispose = (m) => this.playerHit(m)`,
  `UI.onclick = () => this.playAgainPressed = true`, bullet observers,
  `setInterval` callbacks — each captures an object it is then stored back onto.
  **Refcounting can never free these.**
- **(B) babylon retains disposed meshes.** `world.meshes` is append-only
  (`babylon.hpp`: 3 × `push_back`, **zero** `erase`). `mesh.dispose()` sets a
  `disposed` flag and clears `before_render`, but keeps the mesh's geometry, its
  texture, and its JS handle forever (stable integer `__mesh` indices forbid
  removal). Every bullet/explosion mesh ever created is retained.

(A) and (B) interlock: a disposed bullet's handle is pinned by **both** the C++
`world.meshes[ix].handle` **and** the JS `bullet ↔ mesh ↔ onDispose` cycle.
Freeing it needs the C++ root dropped **and** the JS cycle broken — i.e. both a
babylon fix and a cycle collector.

---

## Design

A **full tracing GC is the wrong tool**: ctjs runs at **compile time** (constexpr
interpretation is the project's whole point), where a runtime tracing GC is both
impossible and unnecessary (constant-evaluation frees its arena when it ends).
The right tool for a refcounted runtime is a **cycle collector layered on top of
refcounting** — the approach CPython, PHP and (historically) WebKit's DOM use.
It runs **only at runtime** (`if !consteval`), leaving the constexpr path byte-
for-byte unchanged.

### Algorithm: synchronous Bacon–Rajan trial deletion

Bacon & Rajan, *"Concurrent Cycle Collection in Reference Counted Systems"*
(2001), synchronous variant. It needs no explicit root set — it infers external
references from the refcounts themselves:

1. **Candidate buffer.** When an `rc` decrement leaves `count > 0`, the object
   *might* be in a cycle: color it **purple** and add it to a roots buffer (once).
   When a decrement hits `0`, free normally and remove from the buffer.
2. **collect()** (run periodically):
   - **MarkRoots** — for each purple root, `MarkGray`: DFS coloring gray and
     **decrementing each child's count** (trial deletion: remove internal edges).
   - **ScanRoots** — for each root, `Scan`: if gray with `count > 0` it is
     externally referenced → `ScanBlack` (restore: black, re-increment children);
     if gray with `count == 0` → white, recurse.
   - **CollectRoots** — free every white object (recursively), restoring edges.

White objects are exactly the members of unreachable cycles.

### What has to change in ctjs (the hard parts)

1. **Make every collectable type *traceable*** — a `gc_trace(const T&, visit)`
   that enumerates a T's outgoing `rc` edges:
   - `object_t` → each `props` value + `proto`  — already reachable
   - `array_t`  → each element                   — already reachable
   - `environment` → `parent` + each `vars` value — already reachable
   - `function_t` → `props` + **its closure `env` + bound `this`** — **NOT
     currently reachable**; sealed inside the `cfunction` lambda.
2. **Lift the closure captures out of the lambda** (prerequisite for tracing
   closures — the cycles that actually leak). Give `function_t` explicit fields
   `env_ptr env; value bound_this;`; have `make_fn` store them there and capture
   only PODs `[self, fn_node, is_arrow]`; pass the live `function_t` to the call
   via `context` (e.g. `cx.current_callee`) so `call_user` reads `env`/`this`
   from it. **This touches the hot call path and `this`-binding — the highest-
   risk change; it must not regress arrows / methods / `new`.**
3. **GC header on the rc block** — `{ color; buffered; registry links; trace
   fn-ptr }` beside the existing `count`. All bookkeeping (registry insert/remove,
   roots buffer, coloring) is **guarded `if !consteval`** so the compile-time
   interpreter is untouched (it still allocates and frees per the existing
   refcount; cycles at compile time die with the evaluation arena).
4. **Driver** — `ctjs::gc::collect()` exposed on `run_result`; ctbrowser's
   `engine::tick` calls it on a budget (every N frames, or when the roots buffer
   crosses a threshold). `do_reload`/`reset` force a full collection.

### Companion fix (babylon, independent of the GC)

- **Free disposed meshes' heavy data.** In `mesh.dispose()`, after `onDispose`
  fires, release `geom` and `tex` (disposed meshes are never rendered). *Safe and
  unconditional* — **done in Phase 0 below.**
- **Drop the C++ handle root.** Once the cycle collector exists, `dispose()` can
  also null `world.meshes[ix].handle`, so a bullet whose JS cycle the collector
  breaks is fully reclaimed. (Slot *reuse* is unsafe while stale JS handles may
  survive in a cycle — index aliasing — so tombstone slots stay, just emptied.)

---

## Phases

- **Phase 0 — babylon: free disposed mesh geometry/texture.** Safe, local, no GC.
  Reduces retention immediately (model meshes: 605–1094 verts + 40 KB textures).
  **Implemented; re-measured (see Status).**
- **Phase 1 — traceability.** Add `gc_trace` for object/array/environment; lift
  closure `env`/`this` onto `function_t`; rework the call path; keep the full
  suite (pong/webapi/invaders/select/…) green. No collector yet — behavior-
  preserving refactor.
- **Phase 2 — the collector.** rc GC header + registry + roots buffer (all
  `if !consteval`); Bacon–Rajan `collect()`. Verify with `cycle_probe`: cyclic
  heap delta must drop to ~0 after `collect()`. Stress for use-after-free.
- **Phase 3 — integration + policy.** `engine::tick` collection budget; `reset`
  full-collect; drop babylon handle root on dispose. Re-run `mem_probe`: game
  heap must plateau instead of climbing.

## Verification

- `cycle_probe`: cyclic allocations reclaimed after `collect()` (heap returns to
  baseline); acyclic path unchanged.
- Full headless suite stays green (no premature free / this-binding regressions)
  — pong, webapi, invaders_smoke, select, onclick, scene_teardown.
- `mem_probe` over ≥8000 frames: heap plateaus.
- **Constexpr untouched**: `tests/dom.cpp` (whole DOM+layout in a `static_assert`)
  and the babylon compile-time render still compile — proves the GC never runs at
  compile time.

## Risk

The Phase-1 closure refactor is the crux: it changes `this`-binding and the call
path, where a subtle bug is a silent wrong-answer, and a Phase-2 collector bug is
a use-after-free (worse than the leak it fixes). This is deliberate, staged,
heavily-tested brick surgery — not a one-shot change.

---

## Status

- **Diagnosis: complete** (measurements above).
- **Phase 0: implemented** — `mesh.dispose()` now frees `geom` + `tex`. Re-measure
  over the same 4000-frame game: heap 15.8 → 40.0 MB, i.e. **still +24 MB, only
  ~1 MB better than before.** This is the expected result and it *confirms* the
  diagnosis: per-bullet geometry is tiny (small boxes); the retained weight is the
  JS **handle objects** (each carrying dozens of native closures + Vector3s),
  pinned by both `world.meshes[ix].handle` and the JS closure cycle. Freeing
  geometry cannot touch them. **Phase 0 is a correct, safe reduction for model
  meshes but does not fix the game leak — the cycle collector is the real fix.**
  Full headless suite stays green (11/11).
- **Phases 1–3: designed, not yet implemented** — the ctjs cycle collector is the
  substantial next step and is scoped above. It is correctness-critical brick
  surgery (closure-capture lift changes `this`-binding; a collector bug is a
  use-after-free), so it is staged and heavily tested, not a one-shot change.
