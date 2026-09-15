#pragma once
#include <cstddef>
#include <memory>
#include <span>
#include <string>

#include <ctbrowser/raster/draw.hpp>

// Real fonts, through SDL3_ttf.
//
// NOTE WHAT IS NOT INCLUDED ABOVE. Like raster/svg.hpp and plutosvg, SDL3_ttf
// is confined to lib/Raster/text/ttf.cpp: this header declares an interface
// and a factory, and no consumer of it parses an SDL header. It used to inline
// the whole glyph cache with <SDL_ttf.h> at the top, paid for with an entry in
// test/lint/api_surface's allow-list and `#if CTBROWSER_WITH_TTF` blocks in the
// shell that owned it.
//
// The trade: SDL3_ttf is one dependency where FreeType plus a glyph pipeline
// was two, and it is the same library the window and the audio already come
// from. TTF_Init needs no video subsystem, so real text is still testable with
// no display - which is what made this swap safe to make.
//
// OPTIONAL, like everything else here: without SDL3_ttf `ttf_available()` is
// false, `make_ttf_backend()` returns null, and text renders with font8x8,
// which is what keeps the goldens reproducible on a machine with no font
// libraries.
//
// THREAD SAFETY. Tiles raster in parallel and a TTF_Font is not reentrant, so
// the implementation rasterizes each glyph ONCE under a mutex into an
// immutable cache and draws from it afterwards.

namespace ctbrowser::raster {

[[nodiscard]] constexpr bool ttf_available() noexcept {
#if CTBROWSER_WITH_TTF
    return true;
#else
    return false;
#endif
}

class ttf_backend : public font_backend {
public:
    // Register one face. The BYTES ARE COPIED and kept: SDL3_ttf reads from the
    // stream for the lifetime of the font, so handing it a span of somebody
    // else's vector is a use-after-free waiting for the first page that gets
    // collected. False when the bytes are not a font.
    virtual bool add_face(std::string family, bool bold, bool italic,
                          std::span<const std::byte> bytes) = 0;
    [[nodiscard]] virtual std::size_t face_count() const = 0;
    // An unknown family resolves to this one rather than drawing nothing.
    virtual void set_default_family(std::string family) = 0;
};

// Null when this build has no SDL3_ttf or TTF_Init failed.
[[nodiscard]] std::unique_ptr<ttf_backend> make_ttf_backend();

} // namespace ctbrowser::raster
