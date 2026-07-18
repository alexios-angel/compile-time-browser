# Examples

Windowed applications - each is ONE HTML string, parsed at compile
time, running against SDL3. Build with `make` here (needs
`pkg-config sdl3`; reuses the repo-root grammar PCH) or through the
top-level CMake; `make run` builds and launches. Under ctest they run
headless (`SDL_VIDEODRIVER=dummy`, `CTBROWSER_TEST_FRAMES=30`).

| File | Shows |
|------|-------|
| [`counter.cpp`](counter.cpp) | the hero: an interactive page - clicks flow into the page's own JS, the DOM mutates, CSS restyles, the layout reflows, the title updates. Initial styles are `static_assert`ed |
| [`game.cpp`](game.cpp) | the games proof: pong written in page JavaScript on a `<canvas>` - `onFrame(dt)` physics, `onKey` paddle control, `fillStyle`/`fillRect` drawing streamed through an SDL texture |
