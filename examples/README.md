# Examples

Windowed applications - each is ONE HTML string, parsed at compile
time, running against SDL3. Build through the top-level CMake presets
(`cmake --build --preset default`). Under ctest they run headless
(`SDL_VIDEODRIVER=dummy`, `CTBROWSER_TEST_FRAMES=30`).

| File | Shows |
|------|-------|
| [`elements.cpp`](elements.cpp) | the element gallery: every HTML element rendered Firefox-style by the UA stylesheet — Gecko heading scale in bold Tinos, list markers, blockquote indents, `<pre>` layout, underlined links, bordered and plain tables, details disclosure — and FOUR typefaces in one document (serif/sans/mono + a page `@font-face`) |
| [`widgets.cpp`](widgets.cpp) | the interactive form gallery: type into text/password/textarea fields (real caret), toggle checkboxes and radios, pick from the select, submit/reset the form, all with Firefox's widget chrome and live `input`/`change`/`submit` events narrated to a status line |
| [`counter.cpp`](counter.cpp) | the hero: an interactive page - clicks flow into the page's own JS, the DOM mutates, CSS restyles, the layout reflows, the title updates. Initial styles are `static_assert`ed |
| [`game.cpp`](game.cpp) | the games proof: pong written in page JavaScript on a `<canvas>` - `onFrame(dt)` physics, `onKey` paddle control, `fillStyle`/`fillRect` drawing streamed through an SDL texture |
| [`invaders.cpp`](invaders.cpp) | the engine proof: space invaders - BMP sprite sheet (`drawImageRegion`), WAV sound (`playSound`), polled input (`isKeyDown`), `fillText` HUD, and a 320×240 playfield scaled pixel-perfect to the window (`logical_w/h`). Assets regenerate with `python3 tools/gen-assets.py` |
