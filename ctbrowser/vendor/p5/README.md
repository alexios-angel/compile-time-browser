# p5.js, vendored

**p5.js v2.3.1**, https://github.com/processing/p5.js — **LGPL-2.1**, the full
text in `LICENSE` beside it.

Here as a CORPUS, not as a dependency: nothing the engine ships links or
includes it. `tests/p5_ratchet.cpp` measures how far the bundle gets through
the engine and `tests/p5_api.cpp` how wide the working surface is, and several
pages in `ctbrowser/examples/pages/` draw with it. Somebody else's code is the only kind
worth testing a browser engine against.

Its own directory with its own licence because it is separately licensed
software that happens to live in this tree — the same reason `phaser/` and
`ctbrowser/examples/assets/fonts/` are directories rather than loose files.
