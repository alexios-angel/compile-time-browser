# Phaser as a second corpus

## Read this first: nothing here is measured yet

The p5.js work and the WebGL 2 plan were both scoped by *reading the bundle* —
`rg` over 4.5 MB told us WebGL 2 needed four functions and no VAOs, and the p5
ratchet told us where the engine stopped before a line of code was written.

**None of that has happened for Phaser.** The network was down when this was
written, so the bundle could not be fetched and the engine has never seen it.
Everything below the harness design is therefore a *hypothesis about what a game
framework needs*, not a finding, and it is marked as such. Stage 0 replaces it
with facts, and stage 0 may well rewrite the rest — the lexer plan's stage 0
cancelled its own stages 1-5, which is the outcome to hope for rather than fear.

## Why a second library at all

p5.js is one library with one style. Every engine bug it found was real, but the
bugs it *cannot* find are the ones its idioms never touch, and that gap is
invisible from inside a corpus of one. The clearest example is already on the
record: `getProgramParameter` answered 0 to `ACTIVE_UNIFORMS` for as long as
WebGL existed here, and no page in the tree noticed, because every hand-written
page asks for uniforms **by name**. A *library* enumerates. It took somebody
else's code to find it, and it will take a second somebody to find the next one.

Phaser is a good second because it is unlike p5 in the ways that matter:

* **A game loop rather than a sketch** — `requestAnimationFrame` scheduling,
  fixed-step updates, delta timing. p5 drives its own loop through the engine's
  hooks; Phaser wants to own it.
* **Two renderers**, Canvas and WebGL, chosen at boot and both real code paths.
* **Asset loading as a first-class subsystem** — a loader with queues, retries
  and progress events, over `Image`, `XMLHttpRequest`, `fetch` and `Audio`.
* **Input as state**, polled per frame from keyboard, pointer and gamepad, where
  p5 mostly reacts to callbacks.
* **A build that predates nothing** — Phaser 3 ships ES5-compatible bundles as
  well as ESM, so it exercises different corners of the language than p5's
  modern-only build.

## The harness, which is the deliverable regardless

Modelled directly on the p5 one, because that shape has earned it: two
questions, two files, and a record only a tool may write.

**How FAR it gets** — `tests/phaser_ratchet.cpp` + `tests/phaser-ratchet.txt`,
the same ladder p5 climbs (`tests/p5_ratchet.cpp`):

```
1 read   2 lexed   3 parsed   4 compiled   5 operands fit   6 ran
7 defines Phaser   8 loads as a page   9 new Phaser.Game()  10 preload
11 create           12 update runs      13 and it is IN THE PIXELS
```

The rungs past 7 differ from p5's because the lifecycle differs — Phaser's is
`preload`/`create`/`update` against p5's `setup`/`draw`. The record stores the
level **and the blocker**, so the same rung with a different cause fails: that
is what stops a fix that trades one wall for another from reading as progress.

**How WIDE the surface is** — `tests/phaser_api.cpp` + a probe script, matching
`tests/p5_api.cpp`. Each probe calls one thing and **asserts on the result**,
because "it did not throw" passes in an engine that draws nothing.

Two things the p5 harness learned the hard way, to be built in from the start:

* **The runner must account for every probe.** A bracket in one failure message
  once truncated the JSON and five failures vanished from a report that still
  looked internally consistent. The runner reports how many probes it was handed
  and the harness fails if the lists do not add up.
* **`--advance` is the only writer.** A test that edits its own expectations
  cannot fail.

`tools/phaser-ratchet.py` and `tools/phaser-api.py` drive them, as their p5
counterparts do.

## Stage 0 — get the bundle and find the wall

Fetch Phaser 3 into `examples/assets/`, point the ratchet at it, and **report
the rung it stops at and why**. Nothing else. That number decides everything
after it, and guessing it would waste the work.

The bundle is large — Phaser 3 is roughly 1 MB minified, several MB
unminified — so this also re-measures the front-end cost that
`docs/performance.md` covers, on a second input. Compile time on p5 is ~0.65 s;
if Phaser is much worse per byte, that is itself a finding.

## Stages 1+ — provisional, pending stage 0

Listed so the shape is clear, **not** as commitments. Each is a guess at what a
game framework needs that a sketch library does not:

* **`requestAnimationFrame` owning the loop.** The engine drives frames; Phaser
  expects to schedule them. Whether that already works is unknown.
* **Timing.** `performance.now()`, and delta-time correctness across frames. The
  engine's clock is deliberately fixed for goldens
  (`context::fixed_epoch_base`), and a game that integrates velocity by delta
  will look wrong if that is not handled with care — a determinism question, not
  only a feature one.
* **The loader**: `XMLHttpRequest` (p5 needed only `fetch`), `Image.decode`,
  `Audio`, and progress events.
* **Input state**: `KeyboardEvent.code` as well as `.key`, pointer capture,
  `document.hidden` / visibility.
* **Canvas 2D corners** p5 never used — `globalCompositeOperation` beyond what
  `shell/composite.hpp` covers, `createPattern`, `ImageData` round-trips.
* **WebGL** — Phaser's renderer is its own; it may want WebGL 2, which
  `docs/webgl2-plan.md` covers, and it will certainly enumerate programs.

## Verification

The p5 ratchets and probes **must not move**. A second corpus is only worth
having if the first stays honest — and a change made for Phaser that quietly
costs p5 a rung is a regression, whatever the Phaser number does.

```bash
tools/p5-ratchet.py && tools/p5-api.py       # 12/12, 179 probes: unchanged
tools/phaser-ratchet.py                      # the new number
ctest --preset default                       # and asan
```

The ten goldens stay byte-identical throughout: nothing here should touch
rendering until a Phaser page has its own corpus page and golden, which is a
stage that only makes sense once it draws.

## Risks

* **Stage 0 says Phaser stops at rung 2 or 3.** Then this is a language-support
  project, not a browser-features one, and the plan above is the wrong plan.
  That is exactly why stage 0 exists.
* **The temptation to make p5 worse for Phaser.** Two corpora will disagree.
  When they do, the disagreement itself is the finding and should be written
  down rather than resolved by whichever is being worked on that day.
* **A second bundle in the repository** is real weight — p5 is already 4.5 MB.
  Worth checking whether the minified build serves the purpose before committing
  the full one.
* Phaser may need a real event loop that the engine's frame model does not
  offer, in which case the honest answer is to say so rather than to fake it.
