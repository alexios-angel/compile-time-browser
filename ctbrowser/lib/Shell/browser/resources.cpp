//===- ctbrowser/lib/Shell/browser/resources.cpp - Fonts and images -------===//
//
// What a page loads besides scripts: the font backend and the page's
// @font-face files, and the <img> set (bitmaps through image_store, SVG
// intercepted before it).
//
//===----------------------------------------------------------------------===//

#include "internal.hpp"

namespace ctbrowser::shell {

const ctbrowser::raster::font_backend & browser::fonts() const {
    return fonts_ != nullptr ? *fonts_ : ctbrowser::raster::font8x8_fonts();
}

ctbrowser::layout::measure_text_fn browser::measure() const {
    return metrics_for(fonts());
}

void browser::load_page_fonts() {
    if (!ttf_) { return; }
    for (const auto & face : styles_->page_fonts()) {
        const std::vector<std::byte> bytes = assets_.load(face.source);
        if (!bytes.empty()) { (void)ttf_->add_face(face.family, face.bold, face.italic, bytes); }
    }
    // The faces may have changed, so the `0` advances measured before (for
    // `ch`) are stale: hand the measurement over again, which drops the
    // engine's cache.
    install_text_measure();
}

/// Gives the style engine a text measurement callback. CSS Values 4 §6.1.1:
/// `ch` is the advance of `0` in the element's font, and only a font backend
/// knows it. Without this `ch` falls back to half an em, and `width: 3ch` in a
/// 10px monospace font becomes 15px instead of three 6px zeros.
void browser::install_text_measure() {
    styles_->set_text_measure(
        [this](std::string_view text, float size, std::string_view family, bool bold, bool italic) {
            return fonts().advance(text, size, family, bold, italic);
        });
}

namespace {

/// Whether these bytes are SVG, judged by content alone: a file served as
/// `chart` is still an SVG and a `.svg` that turns out to be a PNG is not, and
/// this decides which of two rasterisers sees the bytes.
[[nodiscard]] bool looks_like_svg(std::string_view bytes) {
    std::size_t at = 0;
    // Skip a UTF-8 BOM, then whitespace. Without this an SVG from a tool that
    // emits BOMs is not recognised, and the failure (a blank box) says
    // nothing.
    if (bytes.size() >= 3 && bytes.compare(0, 3, "\xEF\xBB\xBF") == 0) { at = 3; }
    while (at < bytes.size() &&
           (bytes[at] == ' ' || bytes[at] == '\t' || bytes[at] == '\n' || bytes[at] == '\r')) {
        ++at;
    }
    const std::string_view rest = bytes.substr(at);
    // `<?xml` counts: the root <svg> is then a line or two further in, past a
    // declaration and possibly a DOCTYPE, and plutosvg reads all of it.
    return rest.starts_with("<svg") || rest.starts_with("<?xml") ||
           rest.starts_with("<!DOCTYPE svg");
}

} // namespace

void browser::load_images() {
    // The SVG store is cleared here, not beside canvases_.clear() in
    // run_scripts. A canvas is created by a script, so clearing before
    // scripts run is right for one; an SVG source is found by this walk, which
    // happens before scripts, so clearing there would delete what was just
    // loaded.
    //
    // The SVG clear is the only thing this adds over refresh_images, and it
    // is per-document: an inline <svg>'s source comes from the parse and is
    // re-set by the caller straight after, so a refresh must not wipe it.
    svg_.clear();
    refresh_images();
}

/// Re-resolves every <img>'s bitmap or SVG source, so an image whose src a
/// script set (`img.src = url`, `appendChild(new Image())`) has a size at the
/// next layout.
///
/// Called before every layout. That is cheap because image_store caches by
/// name, so this is a tree walk and an attribute read per <img>, not a
/// decode.
void browser::refresh_images() {
    images_by_node_.clear();
    const auto txn = doc_->read();
    const atom img_tag = atoms_.intern_lower("img");
    const atom src_attribute = atoms_.intern("src");
    const auto walk = [&](auto && self, node_id at) -> void {
        if (txn.tag(at).value_or(atom{}) == img_tag) {
            const std::string_view src = txn.attribute_value(at, src_attribute);
            if (!src.empty()) {
                // SVG is intercepted before image_store sees it, not decoded
                // by a plugin: image_store's cache is keyed by name and decodes
                // once, which cannot produce a raster at the size the box turns
                // out to be. It also means SDL3_image's own SVG loader is never
                // reached, so both platforms rasterise through plutosvg and one
                // golden serves both.
                const std::vector<std::byte> bytes = assets_.load(src);
                const std::string_view text{reinterpret_cast<const char *>(bytes.data()),
                                            bytes.size()};
                if (!bytes.empty() && looks_like_svg(text)) {
                    svg_.set_source(at, std::string{text});
                } else if (auto pixels = images_.load(assets_, src)) {
                    images_by_node_.emplace_back(at, std::move(pixels));
                }
            }
        }
        for (const node_id child : txn.children(at)) { self(self, child); }
    };
    walk(walk, txn.root());
}

std::shared_ptr<const ctbrowser::paint::bitmap> browser::image_of(node_id id) const {
    for (const auto & [at, pixels] : images_by_node_) {
        if (at == id) { return pixels; }
    }
    return nullptr;
}

} // namespace ctbrowser::shell
