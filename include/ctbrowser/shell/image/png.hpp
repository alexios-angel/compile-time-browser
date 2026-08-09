#pragma once
#include <cstddef>
#include <span>

#include <ctbrowser/paint/paint.hpp>

// PNG, decoded WITHOUT SDL.
//
// The engine decoded BMP itself and got everything else from a hook the
// application layer fills in from SDL3_image. That looked like a reasonable
// split until a second corpus arrived: `tests/` is SDL-free by an invariant this
// repository lints for, so every test in the suite saw a PNG as a zero-sized
// image, and nothing said so. Phaser's texture manager loads three base64 PNGs
// during boot and will not start until all three settle - so the framework
// stopped dead at `new Phaser.Game()` reporting `isBooted` true and `isRunning`
// false, which is not a diagnosis. p5.js never found it, because the pages that
// use it load BMPs.
//
// NOTHING THIRD-PARTY IS INCLUDED ABOVE, the same rule url.hpp states for
// Boost.URL and net.hpp for curl.h. `src/shell/image/png.cpp` is the only translation
// unit that has heard of libpng - the format's reference implementation, which
// is what a decoder full of filter types, palettes, tRNS and interlace passes
// should be. See docs/build.md for the cross-build.

namespace ctbrowser::shell {

// An empty bitmap for anything that is not a PNG this can read - truncated,
// corrupt, or not a PNG at all. Every colour type and bit depth the format has
// arrives as the engine's 8-bit ARGB, interlaced images included, because the
// alternative is a decoder that is right about most PNGs.
[[nodiscard]] paint::bitmap decode_png(std::span<const std::byte> data);

// The eight-byte signature, checked before decoding is attempted. Cheap enough
// to ask of every load, which is what lets `image_store` try formats in order
// without a decode attempt per format.
[[nodiscard]] bool looks_like_png(std::span<const std::byte> data) noexcept;

} // namespace ctbrowser::shell
