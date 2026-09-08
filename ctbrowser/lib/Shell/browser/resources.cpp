// browser - what a page loads besides scripts: the font backend and the
// page's @font-face files, and the <img> set - bitmaps through image_store,
// SVG intercepted before it.
//
// One of ten files carved out of a 2,871-line Shell/browser.cpp on 2026-09-08.
// All are member functions of one class declared in
// include/ctbrowser/shell/browser.hpp; internal.hpp beside this carries the
// includes browser.cpp had, so every file sees exactly what it saw. Nothing
// about the public header changed.

#include "internal.hpp"

namespace ctbrowser::shell {

const ctbrowser::raster::font_backend & browser::fonts() const {
    return fonts_ != nullptr ? *fonts_ : ctbrowser::raster::font8x8_fonts();
}

ctbrowser::layout::measure_text_fn browser::measure() const {
    return metrics_for(fonts());
}

void browser::load_page_fonts() {
#if CTBROWSER_WITH_TTF
    if (!ttf_) { return; }
    for (const auto & face : styles_->page_fonts()) {
        const std::vector<std::byte> bytes = assets_.load(face.source);
        if (!bytes.empty()) { (void)ttf_->add_face(face.family, face.bold, face.italic, bytes); }
    }
#endif
}

// Whether these bytes are SVG. Content first, name second, because a file
// served as `chart` is still an SVG and a `.svg` that turns out to be a PNG is
// not - and because this decides which of two rasterisers sees the bytes, which
// is not a decision to make on a file extension alone.
namespace {

[[nodiscard]] bool looks_like_svg(std::string_view bytes) {
    std::size_t at = 0;
    // A BOM, then whitespace. An SVG written by a tool that emits UTF-8 BOMs is
    // otherwise not recognised, and the failure - a blank box - says nothing.
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
    // HERE, not beside canvases_.clear() in run_scripts. A canvas is created BY
    // a script, so clearing before scripts run is right for one; an SVG source
    // is found by this walk, which happens BEFORE scripts, so clearing there
    // deletes what was just loaded. Both are per-document - they just have
    // different producers, and this is the one that resets with its own writer.
    //
    // The SVG clear is the ONLY thing this adds over refresh_images, and it is
    // per-document: an inline <svg>'s source comes from the parse and is re-set
    // by the caller straight after, so a refresh must not wipe it.
    svg_.clear();
    refresh_images();
}

// AN <img> WHOSE src A SCRIPT SET has a bitmap too.
//
// load_images ran once per document, before scripts, so `img.src = url` and
// `document.body.appendChild(new Image())` laid out at 0x0 forever - the
// attribute was right, the decode was cached, and layout had nothing to measure.
// Silently: a zero-sized box is what a missing image looks like.
//
// Called before every layout, which is cheap for the reason that matters:
// image_store caches by name, so this is a tree walk and an attribute read per
// <img>, not a decode.
void browser::refresh_images() {
    images_by_node_.clear();
    const auto txn = doc_->read();
    const atom img_tag = atoms_.intern_lower("img");
    const atom src_attribute = atoms_.intern("src");
    const auto walk = [&](auto && self, node_id at) -> void {
        if (txn.tag(at).value_or(atom{}) == img_tag) {
            const std::string_view src = txn.attribute_value(at, src_attribute);
            if (!src.empty()) {
                // SVG IS INTERCEPTED BEFORE image_store EVER SEES IT. Not a
                // decoder plugged into image_store: its cache is keyed by name
                // and decodes once, which cannot produce a raster at the size
                // the box turns out to be. It also means SDL3_image's own SVG
                // loader is never reached, so both platforms rasterise through
                // plutosvg and one golden serves both.
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
