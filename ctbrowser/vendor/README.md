# vendor/ — somebody else's code, committed on purpose

Four library bundles, 25 MB, checked in:

| | version | licence | size |
|---|---|---|---|
| `p5/p5.js` | p5.js 2.3.1 | LGPL-2.1 (see `p5/LICENSE`) | 4.5 MB |
| `phaser/phaser.js` | Phaser 4.2.1 | MIT (see `phaser/LICENSE`) | 8.4 MB |
| `babylon/babylon.js` | Babylon.js 9.18.2 | Apache-2.0 (see `babylon/LICENSE`) | 12 MB |
| `bootstrap/bootstrap.css` | Bootstrap 5.3.8 | MIT (see `bootstrap/LICENSE`) | 291 KB |
| `bootstrap/bootstrap.bundle.js` | Bootstrap 5.3.8 | MIT (same file) | 131 KB |

`NOTICE` carries the full attributions. **The licence file stays beside its
bundle** — that is the part that matters legally, and every consumer below
copies the directory rather than the file for exactly that reason.

## These are TEST CORPORA first and example assets second

That ordering is the point of this directory existing. Until 2026-08-09 the
bundles lived under `examples/assets/`, beside a generated sprite sheet and a
WAV file, which read as "data three demos happen to load". What they actually
are is the engine's primary evidence that it runs real JavaScript:

- `../test/corpus/*/` — the ratchets (how FAR a bundle gets) and the API probes
  (how WIDE the working surface is). Four of them.
- `tools/*-ratchet.py`, `tools/*-api.py` — the loops that drive those tests and
  record the levels with `--advance`.
- `examples/pages/*.html` — the demo pages, which load them with
  `<script src="../../vendor/<lib>/<lib>.js">`.

Somebody else's code is the only kind worth measuring an engine against: it was
not written to this engine's strengths, and every gap it found was a gap a
hand-written test did not think to look for. Phaser found four engine bugs in a
day that p5 could not. Babylon needed uniform buffer objects, which were "out of
scope" right up until it turned out that meant every matrix read zero while
nothing errored.

**Bootstrap is the same argument applied to CSS**, and it needed a different
harness. The three JavaScript bundles reach the engine through a canvas, so they
say almost nothing about the cascade and nothing at all about layout — every CSS
test here before Bootstrap arrived was a hand-written inline literal of under ten
lines, written by somebody who already knew what the engine supported. So instead
of a rung ladder in C++, Bootstrap is measured by **diffing computed styles and
box geometry against Chrome**: `tools/check/css-parity.py` locally where Chrome
is, and `tests/unit/bootstrap_layout.cpp` against a text baseline everywhere
else. See `bootstrap/README.md` and `docs/plans/bootstrap.md`.

## Why not `third-party/`

`third-party/` is for dependencies that are **downloaded and never committed** —
its `.gitignore` says so, and today it holds the ANGLE release `tools/fetch-angle.sh` unpacks alongside the two submodules that moved there from `external/`
`tools/fetch-angle.sh` unpacks. These are the opposite: committed, pinned by
being in the tree, and diffable. Two different jobs, two directories.

## Updating a bundle

Replace the `.js`, update `NOTICE` and the table above, and then **run the
ratchets** — do not assume a newer bundle clears the same rungs:

```
tools/corpus/p5-ratchet.py        # and phaser-, babylon-, webgl2-, module-
tools/corpus/p5-api.py --coverage
tools/check/css-parity.py --all   # bootstrap, and it needs Chrome
```

A drop is a real finding, not a reason to reach for `--advance`. `--advance` is
the only thing that writes a record file, deliberately: a test that edits its
own expectations cannot fail.
