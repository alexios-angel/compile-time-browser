# Phaser, vendored

**Phaser 4.2.1**, https://github.com/phaserjs/phaser — **MIT**, the full text in
`LICENSE` beside it.

Here as a CORPUS, not as a dependency: nothing the engine ships links or
includes it. `tests/phaser_ratchet.cpp` measures how far the bundle gets,
`tests/phaser_api.cpp` how wide the surface is, and
`examples/pages/phaser-invaders.html` runs a Space Invaders on it.

A SECOND corpus, because the bugs one library cannot reach are invisible from
inside a corpus of one — see `docs/script.md` for the five engine bugs this one
found that p5.js could not.

Its own directory with its own licence because it is separately licensed
software that happens to live in this tree.
