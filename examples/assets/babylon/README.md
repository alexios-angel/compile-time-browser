# Babylon.js, vendored

**Babylon.js 9.18.2** (`babylon.max.js`, the unminified UMD build with comments
stripped), https://github.com/BabylonJS/Babylon.js — **Apache-2.0**, the full
text in `LICENSE` beside it.

Here as a CORPUS, not as a dependency: nothing the engine ships links or
includes it.

## Why a third one

p5.js and Phaser both answer WebGL questions, and neither can answer the WebGL 2
question. Measured over the three bundles:

| | p5.js | Phaser | Babylon |
|---|---|---|---|
| asks for `getContext('webgl2')` | yes, first | **never** | yes, first |
| `#version 300 es` shaders | 0 | 0 | **5** |
| UBOs, 3D textures, MRT, transform feedback, queries, sync, samplers | 0 | 0 | **all** |

p5 asks for WebGL 2 and barely uses it; Phaser never asks and takes VAOs and
instancing from WebGL 1 extensions instead. **Babylon is the only library here
that would exercise a WebGL 2 implementation**, and it gates 52 sites on
`_webGLVersion`, so it degrades to WebGL 1 cleanly rather than refusing to run.
That combination — uses everything, survives having none of it — is exactly what
a corpus for this work needs.

See `docs/webgl2-plan.md`, which was rewritten around those measurements, and
`tests/webgl2_ratchet.cpp`, which measures how far it gets.

It is 11.6 MB, which is real weight; it earns it by being the only witness to
the half of WebGL this engine has never been asked for.
