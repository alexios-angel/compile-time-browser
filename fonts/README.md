# fonts/ — the default typefaces

Vendored from google/fonts @ 9fab8b6cc7b2f20376914fd765d918c698c66d75
(https://github.com/google/fonts, ofl/ collection). All three families
are licensed under the SIL Open Font License 1.1:

- **Tinos** (serif) — metric-compatible with Times New Roman, the face
  Firefox's default `serif` maps to. Copyright The Tinos Project
  Authors (Steve Matteson / Monotype). License: OFL 1.1.
- **Fira Sans** (sans-serif) — Mozilla's own typeface, designed for
  Firefox OS. Copyright The Fira Sans Project Authors. License:
  OFL-FiraSans.txt.
- **Cousine** (monospace) — metric-compatible with Courier New.
  Copyright The Cousine Project Authors. License: OFL-Cousine.txt.

ctbrowser std::embed's these at compile time (include/ctbrowser/
fonts.hpp) as the engine's default serif / sans-serif / monospace
faces; a checkout without this directory still builds and falls back
to a probed system font or the built-in 8x8 bitmap face.
