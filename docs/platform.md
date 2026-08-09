# Platform — GPU, the Windows cross-build, and the working environment

Where the code runs and what each host can actually do. Read before
concluding anything about GPU behaviour from a Linux run here.

## GPU: Linux binaries here see no adapter — WINDOWS ONES DO (2026-07-25)

`src/gpu` (SDL3 `SDL_GPUDevice`) builds and RUNS under this WSL2, but the only
Vulkan ICD that survives loading is **lavapipe** (`lvp_icd.json`) — every
hardware ICD is dropped with "not having any physical devices". `/dev/dxg` and
`/usr/lib/wsl/lib/libd3d12.so` exist, but no `dzn`/`d3d12` Vulkan ICD bridges to
them. `SDL_GetGPUDeviceDriver` says "vulkan" either way — the adapter name
(`SDL_PROP_GPU_DEVICE_NAME_STRING`, exposed as `sdl_gpu_backend::adapter()`) is
what tells you, and `adapter_is_software()` checks it.

**The cross-compiled .exe sees the real GPU.** Run under WSL interop,
`build-windows/src/tests/ctbrowser-test-gpu_basics.exe` selects
**`Intel(R) Arc(TM) Graphics`** and its render matches the software one exactly
(0 of 120000 pixels differ). So GPU **correctness** is verifiable both ways, and
GPU **performance** numbers must come from the Windows build — `bench_gpu`
prints a loud banner on Linux here because its numbers would be two CPU
implementations racing. Headless GPU runs need `SDL_VIDEODRIVER=offscreen`;
`dummy` has no Vulkan surface support and fails device creation outright.

## THE ANGLE GOLDENS ARE PINNED TO A SOFTWARE RASTERISER (2026-08-08)

`tests/golden/angle/*.ppm` - babylonscene, babylonorbit, p5webgl, webgltriangle -
were made on **SwiftShader**. "ANGLE" is not one rasteriser: it is whatever
Vulkan device the loader hands it, so a machine with something else renders
something else, legitimately, and the byte comparison fails for a reason that is
not a regression.

**Measured on 2026-08-08, and the honest version is narrower than the first
guess.** What actually matters is SOFTWARE versus HARDWARE, not which software:

| device | vs the golden |
|---|---|
| SwiftShader (what they were made on) | identical |
| Mesa **llvmpipe** | **identical** - byte for byte, checked directly |
| Windows, real **Intel Arc** | webgltriangle **54.19%** of pixels, babylonscene 0.82%, p5webgl **5 of 93,600** |

Two CPU rasterisers doing exact IEEE math with no multisampling agree exactly;
a GPU does not. The 5-pixel figure is the signature to recognise - single digits
mean two rasterisers disagreeing at triangle edges, and it is the same number
`examples/CMakeLists.txt` recorded when the ANGLE goldens were split out.

**The fix is one line in `tools/check/check-render.cmake`**: it now sets
`CTBROWSER_GL_DRIVER=deterministic` whenever `BACKEND` is given, so the golden
runs ask for SwiftShader on every machine. Before that it set the back end and
never the device. With it, the Windows exes match **all sixteen** goldens
byte-for-byte - twelve software and four ANGLE - and so does a Linux workstation
that has Mesa installed.

It had never been caught for a reason that is not reassuring: neither the devbox
nor the old CI had any other Vulkan device to lose to.

### `gl_basics` is the one that still needs the ICD, and that is correct

`gl_basics` asserts the renderer STRING contains "SwiftShader", which is a
stricter question than the goldens ask - it is checking WHICH device answered,
not what it drew. Asking ANGLE for the SwiftShader device type is not enough
when the Vulkan *loader* only offers Mesa, because the loader enumerates ICDs
before ANGLE's preference applies. Point it at the bundled one:

```bash
ICD="$PWD/third_party/angle/linux-x86_64/vk_swiftshader_icd.json"
VK_ICD_FILENAMES="$ICD" VK_DRIVER_FILES="$ICD" ctest --preset default
```

Without it this box is **74/75**, the only failure being `gl_basics`; with it,
75/75. Keep the assertion: it is what turns "four mystery pixel diffs" into one
line naming the cause, which is exactly how the table above got measured.

### Running the Windows exes from WSL

`WSLENV` or nothing reaches the process - it opens a window instead of rendering
headlessly. Path variables need the `/p` suffix:

```bash
cd examples-windows
WSLENV="CTBROWSER_TEST_FRAMES:CTBROWSER_SCREENSHOT/p:CTBROWSER_FONTS:CTBROWSER_NETWORK:CTBROWSER_WEBGL:CTBROWSER_GL_DRIVER:SDL_VIDEODRIVER:SDL_AUDIODRIVER" \
CTBROWSER_TEST_FRAMES=150 CTBROWSER_FONTS=font8x8 CTBROWSER_NETWORK=0 \
SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy \
CTBROWSER_SCREENSHOT="$PWD/out.ppm" ./phaserinvaders.exe
```

**The frame count is per example and it is not optional**: 30 for a drawing,
**150 for phaserinvaders**, **90 for the two Babylon pages** - a game and a
promise-driven scene have not reached a comparable state at 30.
`examples/CMakeLists.txt` holds those numbers. Comparing at the wrong one gives a
3.4% pixel diff that looks exactly like a regression and is not; that is one of
the two false alarms this section cost before the numbers above were right.

## Windows cross-build (2026-07-25)

`cmake --preset windows && cmake --build --preset windows && cmake --build
--preset windows --target windows-dist` → **`examples-windows/`**, carrying the
exes and the pages/assets they load, laid out repo-relatively so they work from
its root. The exes import **only the system UCRT** — no libc++, no libunwind,
and no SDL3.dll (see below).

**The exes are SELF-CONTAINED — no SDL3.dll.** `../llvm-mingw/build-sdl3.sh`
builds SDL3 and SDL3_ttf as STATIC libraries into the toolchain's own
`<triple>/` sysroot (run on the devbox, artifacts rsynced into
`tools/llvm-mingw/`), and the toolchain file puts that sysroot FIRST on
`CMAKE_FIND_ROOT_PATH` — which it must also be ON, or `find_package` escapes to
linuxbrew's ELF SDL3 and fails with "IMPORTED_IMPLIB not set". libsdl's official
mingw devel package (`~/projects/sdl3-mingw`) is the fallback, and a build that
lands there ships the DLL. `CTBROWSER_SDL3_STATIC=OFF` forces it.
`ctbrowser_pick_sdl_target()` chooses `SDL3::SDL3-static` over
`SDL3::SDL3-shared` and tells `windows-dist` whether a DLL has to travel.
Cost: 3.5 MB → 7.2 MB per exe.

Toolchain, fetched rather than built: llvm-mingw (`tools/llvm-mingw/`, 84 MB —
still the std::embed build, which is simply the one that is there; nothing needs
the builtin now) and **Boost as an isolated include dir**
(`~/projects/boost-inc/boost` symlinked at the host's) — there is no BoostConfig
for the cross target and none is needed, since the engine links `Boost::headers`
and nothing else.

Degrades as designed: no OpenSSL for mingw → `fetch` does http:// only and says
so; no SDL3_image → `<img>` reads BMP only. Asio needs `ws2_32`/`mswsock`, which
nothing links implicitly.

**SVG is the exception to that BMP-only line**, and it is not decoded through
SDL3_image on either platform. `browser::load_images` sniffs the bytes and sends
SVG to plutosvg before `image_store` sees them, so `<img src=x.svg>` works on
Windows *despite* there being no SDL3_image there — plutosvg is already in the
sysroot, put there by `build-sdl3.sh` for SDL3_ttf's colour glyphs. It also means
both platforms rasterise through the same code at the same version, which is what
lets `tests/golden/svg.ppm` compare across them.

**Verified 2026-08-03**: all FOURTEEN renderable examples produce screenshots
BYTE-IDENTICAL to the Linux goldens, including the new `babylonscene.exe` — so
uniform buffer objects, render-to-texture and the shared texture sampler all
behave the same on the cross-build. `tools/remote-build.sh windows` is the whole
process; running one from WSL still needs the `WSLENV` line below.

**Earlier verified**: all 19 the engine tests pass as Windows binaries WITH NO DLL BESIDE THEM
(gpu_basics.exe failed that way before), the five renderable examples produce
screenshots BYTE-IDENTICAL to the Linux ones, and counter.exe runs alone in an
otherwise empty directory.

**Running a Windows exe from WSL needs `WSLENV`** or none of the
`CTBROWSER_*`/`SDL_*` environment variables reach it — and the flag is
`/w` (Win32 invoked from WSL), not `/u`:
`WSLENV=CTBROWSER_TEST_FRAMES/w:CTBROWSER_SCREENSHOT/w:SDL_VIDEODRIVER/w`.
Without it the app opens a real window and never exits, because it never sees
the frame cap.

## ⚠️ Working environment & in-flight work (READ FIRST — 2026-07-22)

**Builds are fast locally now** — nothing folds a page in the constant
evaluator any more, so the old OOM risk is gone with the engine that caused it.
The devbox is still the faster machine for a full matrix. `rsync` from `/mnt/c`
into it is flaky (symlink + DrvFs). The devbox
(github.com/alexios-angel/infra, sibling checkout `../infra`) replaced the old
per-project build server: 8 vCPU / 32 GB, Ubuntu 24.04, apt toolchain (GLM,
cmake 3.28, LLVM 18 suite), **no SDL3** (so examples skip there). It
**deallocates itself after 30 idle min** — `../infra/azure-build-server/
server.sh start` wakes it (lifecycle: `server.sh
{start|stop|status|ip|ssh|ssh-config|allow-ip}`; ssh timeout after a network
change = your IP rotated → `server.sh allow-ip`). Reach it as `ssh devbox`
(alias written by `server.sh ssh-config`, IdentityAgent included). After a
local reboot the SSH agent is gone: `ssh-agent -a ~/.ssh/build-agent.sock &&
SSH_AUTH_SOCK=~/.ssh/build-agent.sock ssh-add ~/.ssh/id_ed25519` — the
`devbox` alias finds the sock by itself after that.
**Clean clones live at `~/projects/` on the box** (`compile-time-browser`
with submodules init'd + clang toolchain installed, and `embed`) — ssh in and
work there directly, or sync this tree with `./tools/remote-build.sh
[target]`, which runs the CMake `default` preset in
`~/projects/compile-time-browser`. NOTE it still converges the old
clang-std-embed toolchain and GLM, neither of which this project needs any
more — that script has not been revisited since the compile-time engine went.

**The Windows cross-build is the `windows` preset** +
`cmake/toolchain-windows-x86_64.cmake` (llvm-mingw, a STATIC SDL3 and SDL3_ttf
in the toolchain's own sysroot, an isolated Boost include dir; env
LLVM_MINGW / SDL3_MINGW override the `~/projects/*` defaults).
`windows-dist` collects the exes and the pages they load into
`examples-windows/`. `./tools/remote-build.sh windows` runs it on the devbox
and rsyncs the exes back. The exes are SELF-CONTAINED - no DLL beside them,
and since 2026-07-28 no `fonts/` either: the preset sets
`CTBROWSER_EMBED_FONTS`, so the three UA families are inside each exe.

**AND NO CONSOLE.** A Windows exe is a console application unless told
otherwise, so double-clicking one opened a terminal beside the page. The preset
sets `CTBROWSER_WINDOWS_CONSOLE=OFF`, which links the graphical examples with
`-mwindows` - the Windows subsystem, no terminal. Nothing in the sources
changes: mingw-w64's startup provides the `WinMain` that calls `main`, and
anything printed still arrives when a console is attached on purpose.

`ctbrowse` and `ctdrive` keep their console whatever the option says, and that
is not an oversight - both are CLIs whose output IS the product. `ctbrowse`
prints its usage and a page's script errors, and `ctdrive` prints the port it is
listening on, which is the only way its client ever learns it. Give those two
the GUI subsystem and the comparison rig hangs waiting for a line that can no
longer be written.
