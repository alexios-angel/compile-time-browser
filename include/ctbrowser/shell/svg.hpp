#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <ctbrowser/core/core.hpp>
#include <ctbrowser/paint/paint.hpp>

// Where an element's SVG source lives, and the rasters made from it.
//
// A SIBLING OF image_store, NOT A DECODER PLUGGED INTO IT, and that is the
// design decision this file exists to record. image_store::load caches by name
// and decodes ONCE, which is right for a PNG and wrong for a vector graphic;
// and its `decoder` hook is a single slot that set_decoder REPLACES rather than
// chains, so an SVG decoder installed there would have to also handle PNG and
// JPEG. Instead browser::load_images sniffs the bytes and sends SVG here before
// image_store ever sees them. Two consequences worth having:
//
//   - SDL3_image's own SVG loader is never used, so Linux and Windows rasterise
//     through the SAME code at the same version and one golden serves both.
//     (SDL3_image is not in the mingw sysroot at all, so on Windows the
//     alternative was nothing.)
//   - the cache can be keyed on SIZE, which is the whole point.
//
// THE SOURCE IS VERBATIM. For an <img> it is the file; for an inline <svg> it
// is the exact byte range the tokenizer saw, not a re-serialisation of the
// parsed tree. That matters because the HTML tokenizer lowercases tag and
// attribute names, and `viewBox` lowercased is a `viewbox` that plutosvg
// ignores. THE PRICE: once script mutates a child of an inline <svg>, this
// source is stale and the picture does not follow. Nothing in the engine
// currently reaches into an SVG subtree, so nothing is wrong today - but if
// that changes, this is the thing that has to change with it.

namespace ctbrowser::shell {

// What an <svg> or an .svg file says it is, before any box has been laid out.
// Scanned IN-ENGINE rather than by asking plutosvg, deliberately: it means a
// build with no plutosvg lays a page out IDENTICALLY to one with it, and simply
// draws nothing where the graphic would be. Layout does not acquire a decoder.
struct svg_natural {
    float width = 0;  // 0 = the document did not say
    float height = 0; // 0 = the document did not say
    [[nodiscard]] bool known() const noexcept { return width > 0 && height > 0; }
};

// `width`/`height` on the root <svg> when they are absolute lengths; otherwise
// the `viewBox`'s extent, which gives the aspect ratio even when the size is a
// percentage. Neither present -> {0,0}, and the caller falls back to CSS's
// 300x150 default for a replaced element.
//
// Percentages resolve to nothing ON PURPOSE: `width="100%"` means "as wide as
// the box gives me", which is not a natural size, and treating the 100 as
// pixels is how a full-width banner becomes a 100px stamp.
[[nodiscard]] svg_natural scan_svg_natural(std::string_view source);

class svg_store {
public:
    // The source for an element. Copied rather than referenced: an <img>'s
    // bytes belong to the asset registry and an inline <svg>'s to the parse
    // input, and neither outlives a navigation reliably.
    void set_source(node_id id, std::string source);

    [[nodiscard]] bool has(node_id id) const noexcept;

    // The document's own idea of its size, for layout. Cheap and cached - the
    // scan happens once per distinct source, not once per call.
    [[nodiscard]] svg_natural natural_of(node_id id) const;

    // Pixels at EXACTLY this size, rasterised on the first ask and cached
    // after. Null when there is no source, no plutosvg, or the render failed.
    //
    // `width`/`height` are device pixels - the caller has already snapped the
    // laid-out box - because a cache keyed on floats would miss on every
    // sub-pixel wobble and re-rasterise a graphic that has not moved.
    [[nodiscard]] std::shared_ptr<const paint::bitmap> pixels_for(node_id id, int width,
                                                                  int height);

    // The frame bracket, and the reason it exists: dragging a window edge asks
    // for a new size every frame, and without a sweep the cache grows by one
    // full raster per pixel dragged. begin_frame clears the marks, pixels_for
    // sets them, end_frame drops whatever went unasked-for.
    //
    // ONLY call these around a frame that actually records. A frame that
    // re-rasters a canvas without re-recording touches nothing here, and
    // sweeping then would throw away every graphic on the page.
    void begin_frame() noexcept;
    void end_frame();

    void clear() noexcept;

    // Cached rasters, for tests - a number nothing in the engine branches on.
    [[nodiscard]] std::size_t cached_rasters() const noexcept { return rasters_.size(); }

private:
    // Two levels, so two <img> pointing at one file share both the scan and the
    // pixels. `content` is a hash; the full source is compared on a hit, which
    // costs a string compare only when the hash already matched.
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
