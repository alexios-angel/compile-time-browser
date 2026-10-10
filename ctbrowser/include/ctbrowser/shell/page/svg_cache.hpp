//===- svg_cache.hpp - SVG sources and rasters ------------------*- C++ -*-===//
//
// Where an element's SVG source lives, and the rasters made from it.
//
// This is a sibling of image_store, not a decoder plugged into it.
// image_store::load caches by name and decodes once, which is right for a PNG
// and wrong for a vector graphic; and its `decoder` hook is a single slot that
// set_decoder replaces rather than chains, so an SVG decoder installed there
// would also have to handle PNG and JPEG. Instead browser::load_images sniffs
// the bytes and sends SVG here before image_store sees them. As a result:
//
//   - SDL3_image's own SVG loader is never used, so Linux and Windows rasterise
//     through the same code at the same version and one golden image serves
//     both. (SDL3_image is not in the mingw sysroot at all.)
//   - the cache can be keyed on size, so a graphic is rasterised at the size
//     its box got rather than scaled.
//
// The source is verbatim: for an <img> it is the file; for an inline <svg> it
// is the exact byte range the tokenizer saw, not a re-serialisation of the
// parsed tree, so the rasteriser gets the author's bytes (`viewBox` and all).
// The cost: once script mutates a child of an inline <svg>, this source is
// stale and the picture does not follow.
//
//===----------------------------------------------------------------------===//

#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <ctbrowser/core/core.hpp>
#include <ctbrowser/paint/paint.hpp>

namespace ctbrowser::shell {

/// The natural size an <svg> or an .svg file declares, before any box has
/// been laid out. Scanned in-engine rather than by asking plutosvg, so a build
/// with no plutosvg lays a page out identically to one with it and simply
/// draws nothing where the graphic would be.
struct svg_natural {
    float width = 0;  // 0 = the document did not say
    float height = 0; // 0 = the document did not say
    [[nodiscard]] bool known() const noexcept { return width > 0 && height > 0; }
};

/// `width`/`height` on the root <svg> when they are absolute lengths;
/// otherwise the `viewBox`'s extent, which gives the aspect ratio even when
/// the size is a percentage. Neither present gives {0,0}, and the caller falls
/// back to CSS's 300x150 default for a replaced element.
///
/// Percentages deliberately resolve to nothing: `width="100%"` means "as wide
/// as the box allows", which is not a natural size, and treating the 100 as
/// pixels would turn a full-width banner into a 100px stamp.
[[nodiscard]] svg_natural scan_svg_natural(std::string_view source);

/// The SVG source of each element and a size-keyed cache of its rasters.
class svg_store {
public:
    /// Sets the source for an element. Copied rather than referenced: an
    /// <img>'s bytes belong to the asset registry and an inline <svg>'s to the
    /// parse input, and neither reliably outlives a navigation.
    void set_source(node_id id, std::string source);

    [[nodiscard]] bool has(node_id id) const noexcept;

    /// The document's own idea of its size, for layout. Cached: the scan
    /// happens once per distinct source, not once per call.
    [[nodiscard]] svg_natural natural_of(node_id id) const;

    /// Pixels at exactly this size, rasterised on the first request and
    /// cached after. Null when there is no source, no plutosvg, or the render
    /// failed.
    ///
    /// `width`/`height` are device pixels (the caller has already snapped the
    /// laid-out box), because a cache keyed on floats would miss on every
    /// sub-pixel wobble and re-rasterise a graphic that has not moved.
    [[nodiscard]] std::shared_ptr<const paint::bitmap> pixels_for(node_id id, int width,
                                                                  int height);

    /// Brackets a frame to sweep unused rasters: dragging a window edge asks
    /// for a new size every frame, and without a sweep the cache grows by one
    /// full raster per pixel dragged. begin_frame clears the marks, pixels_for
    /// sets them, and end_frame drops whatever was not requested.
    ///
    /// Call these only around a frame that records a display list. A frame
    /// that re-rasters a canvas without re-recording touches nothing here, and
    /// sweeping then would throw away every graphic on the page.
    void begin_frame() noexcept;
    void end_frame();

    void clear() noexcept;

private:
    // Two levels, so two <img> pointing at one file share both the scan and the
    // pixels. `content` is a hash of the source; sources_ maps it back to the
    // text.
    struct entry {
        std::uint64_t content = 0;
        svg_natural natural;
    };
    struct raster_key {
        std::uint64_t content;
        int width;
        int height;
        [[nodiscard]] bool operator==(const raster_key &) const noexcept = default;
    };
    struct raster_key_hash {
        [[nodiscard]] std::size_t operator()(const raster_key & k) const noexcept;
    };
    struct raster_entry {
        std::shared_ptr<const paint::bitmap> pixels;
        bool touched = false;
    };

    std::vector<std::pair<node_id, entry>> by_node_;
    std::unordered_map<std::uint64_t, std::string> sources_;
    std::unordered_map<raster_key, raster_entry, raster_key_hash> rasters_;

    [[nodiscard]] const entry * find(node_id id) const noexcept;
};

} // namespace ctbrowser::shell
