//===- ttf.hpp - TrueType text ----------------------------------*- C++ -*-===//
//
// Real fonts, through SDL3_ttf.
//
// SDL3_ttf is confined to lib/Raster/text/ttf.cpp, as plutosvg is for
// raster/svg.hpp: this header declares an interface and a factory, so no
// consumer includes an SDL header. SDL3_ttf is the same library family the
// window and audio come from, and TTF_Init needs no video subsystem, so real
// text stays testable with no display.
//
// SDL3_ttf is optional: without it ttf_available() is false,
// make_ttf_backend() returns null, and text renders with font8x8, which keeps
// the goldens reproducible on a machine with no font libraries.
//
// Thread safety: tiles raster in parallel and a TTF_Font is not reentrant, so
// the implementation rasterizes each glyph once under a mutex into an
// immutable cache and draws from it afterwards.
//
//===----------------------------------------------------------------------===//
#pragma once
#include <cstddef>
#include <memory>
#include <span>
#include <string>

#include <ctbrowser/raster/draw.hpp>

namespace ctbrowser::raster {

/// Whether this build has SDL3_ttf (a build-time fact).
[[nodiscard]] constexpr bool ttf_available() noexcept {
#if CTBROWSER_WITH_TTF
    return true;
#else
    return false;
#endif
}

/// A font_backend that draws registered TrueType faces.
class ttf_backend : public font_backend {
public:
    /// Registers one face. The bytes are copied and kept, because SDL3_ttf reads
    /// from the stream for the font's whole lifetime and the caller's buffer
    /// may be freed first. Returns false when the bytes are not a font.
    virtual bool add_face(std::string family, bool bold, bool italic,
                          std::span<const std::byte> bytes) = 0;
    [[nodiscard]] virtual std::size_t face_count() const = 0;
    /// Sets the family an unknown family resolves to, rather than drawing
    /// nothing.
    virtual void set_default_family(std::string family) = 0;
};

/// Creates the backend. Null when this build has no SDL3_ttf or TTF_Init
/// failed.
[[nodiscard]] std::unique_ptr<ttf_backend> make_ttf_backend();

} // namespace ctbrowser::raster
