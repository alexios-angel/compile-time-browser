#pragma once
#include <cstddef>
#include <span>

#include <ctbrowser/paint/paint.hpp>

// JPEG, decoded WITHOUT SDL - the same move `png.hpp` makes, for the same
// reason.
//
// The engine decoded BMP itself and got every other format from a hook only the
// application layer fills in from SDL3_image. `tests/` is SDL-free by an
// invariant `tests/api_surface` lints for, so the entire suite saw a JPEG as a
// zero-sized image. PNG came out of that hook first because Phaser's boot
// textures are PNGs and the framework stopped dead without them; JPEG follows
// because the argument is not about PNG. A format whose result depends on
// whether SDL happened to be found is one no golden can compare.
//
// NOTHING THIRD-PARTY IS INCLUDED ABOVE, the rule url.hpp states for Boost.URL
// and net.hpp for curl.h. `src/shell/jpeg.cpp` is the only translation unit
// that has heard of libjpeg-turbo.

namespace ctbrowser::shell {

// An empty bitmap for anything that is not a JPEG this can read - truncated,
// corrupt, or not a JPEG at all. Baseline and progressive, greyscale and
// colour, and the subsampling modes the format allows, all arrive as the
// engine's 8-bit ARGB with alpha fully opaque: JPEG has no transparency, and
// leaving the alpha byte to chance is how an image decodes and then draws as
// nothing.
[[nodiscard]] paint::bitmap decode_jpeg(std::span<const std::byte> data);

// The SOI marker, checked before decoding is attempted. Cheap enough to ask of
// every load, which is what lets `image_store` try formats in order without a
// decode attempt per format.
[[nodiscard]] bool looks_like_jpeg(std::span<const std::byte> data) noexcept;

} // namespace ctbrowser::shell
