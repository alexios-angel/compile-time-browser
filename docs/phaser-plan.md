# Phaser as a second corpus

## Status: DONE, 10/10, and stage 0 rewrote the rest as predicted

**This file was written with the network down and every stage below the harness
was a hypothesis.** Stage 0 replaced them with facts on 2026-08-01, and — as the
lexer plan's stage 0 did before it — the facts cancelled most of what follows.
It is kept because the reasoning about *why a second corpus* is the part that
was right, and because the guessed stages are a useful record of how far off a
plan written without measurement can be.

What actually happened:

| | |
|---|---|
| Rung reached on arrival | **7/10** — constructs a Game |
| Rung reached the same day | **10/10** — paints what the scene drew |
| Engine bugs found | **4**, none about games, none findable by p5 |
| Stages 1+ below that were needed | **none of them** |

The four were a `data:` URL that could be written but never read, a PNG that
would not decode without SDL, `+x` compiling to a register copy instead of
ToNumber, and `a.length = 0` being silently dropped. `docs/script.md` and
`docs/shell.md` have them in full. Not one appears in the guessed list below —
which named `requestAnimationFrame`, delta timing, the loader, input state and
Canvas 2D corners, and every one of those already worked.

The harness is what the plan got right, and it is all in place:
`tests/phaser_ratchet.cpp` (the ladder), `tests/phaser-ratchet.txt` (the record,
with a pawl that fails on a regression AND on a silently changed blocker),
`tools/phaser-ratchet.py --advance` (the only writer), and
`examples/pages/phaser-basic.html` + `tests/golden/phaserbasic.ppm` for rung 10
— byte-identical on Linux and on the Windows cross-build.

`tests/phaser_api.cpp` is built too — **114 probes, 33/33 namespaces**, and it
found a fifth engine bug the ratchet could not: `(5).hasOwnProperty` was
undefined, because numbers, booleans *and* strings never chained to
`Object.prototype`. Only arrays did.

## And then it played a game (2026-08-01)

`examples/pages/phaser-invaders.html` is a working Space Invaders: a
keyboard-driven ship, bullets, a formation that marches and descends, invaders
dropping bombs back, collisions in both directions, a score, three lives, and
both win and lose conditions. Every texture is drawn at boot with
`generateTexture`, so the page ships no assets.

That page is the point of the whole corpus. The ratchet, the probes and the
golden each answer a narrower question, and **all three were satisfied while
every arrow key did nothing** — Phaser matches keys on the legacy `keyCode`, and
the engine set only `code` and `key`. It took asking "is it playable?" to see
it, which is a question none of the three instruments asks.

`tests/phaser_invaders.cpp` asks both halves of it: that a held key moves the
ship *differently from the scripted sweep*, and that 700 unattended frames raise
the score and cost a life. The second is the one that would notice the loop
dying halfway — every other test here stops before frame 200.

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

## Stage 0 — get the bundle and find the wall — DONE

Fetch Phaser into `examples/assets/`, point the ratchet at it, and **report the
rung it stops at and why**. Nothing else. That number decides everything after
it, and guessing it would waste the work.

**Done, and it decided everything after it.** The bundle is Phaser **4.2.1**,
not 3. It stopped at 7/10 with `create() never ran` — and the cause was four
deep, each bug hidden by the one in front of it.

The bundle is large — Phaser 3 is roughly 1 MB minified, several MB
unminified — so this also re-measures the front-end cost that
`docs/performance.md` covers, on a second input. Compile time on p5 is ~0.65 s;
if Phaser is much worse per byte, that is itself a finding.

## Stages 1+ — provisional, pending stage 0 — AND ALL WRONG

Kept verbatim as written, because the gap between them and what stage 0 actually
found is the lesson. Every item below already worked; not one of the four real
bugs is on the list.

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
