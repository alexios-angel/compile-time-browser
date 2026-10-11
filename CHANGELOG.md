# Changelog

## v0.1.0-alpha.1, October 2026

This is the first public alpha. Both projects are usable enough to try and
nowhere near finished.

**ctbrowser** (the engine, internally version 2.0.0)

- The HTML parser, CSS engine, layout, software rasterizer and JavaScript VM
  are all written for this project. Pages render in an SDL3 window or
  headlessly, and the engine embeds in C++ through `ctbrowser::run_app`.
- test262: 47,425 of the 49,377 tests that ran pass (96.0%, September 2026).
- web-platform-tests `css/`: 1,596 of 2,926 runnable files pass.
- p5.js, Phaser, Babylon.js and Bootstrap pages run as test corpora, each
  partially.
- WebGL goes through ANGLE when it's available.

**ctcompile** (the compiler, version 0.1.0)

- A JavaScript-to-C++ compiler (`ctjs-translate` and `ctjs-opt`) built on
  MLIR. It turns proved JavaScript into C++23 with no VM and no garbage
  collector, and refuses the rest with a reason for each function. The
  generated programs are checked against the VM's output under GCC and Clang.
- An app packager (`ctcompile` and `ctrun`) that bundles a page's HTML,
  resources and JavaScript bytecode into one executable or `.ctapp` file.
  Packaged JavaScript still runs in the VM.

**Known limits**

- The native compiler handles small programs and focused tests completely, but
  only 21 of the 574 functions in Bootstrap's JavaScript bundle so far.
- The packager doesn't use the native compiler yet, and doesn't precompile HTML
  or CSS.
- Module scripts can't be packaged.
- LLVM 23 is required. The compiler's flags, dialects, generated C++ and file
  formats are all unstable.
- Linux is the main platform. The Windows build is a cross-compile and gets
  less testing.
