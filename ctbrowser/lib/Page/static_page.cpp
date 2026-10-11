//===- ctbrowser/lib/Page/static_page.cpp - A page with no script engine ---===//
//
// static_page's load and frame: the steps shell::browser takes for a page
// with no script (lib/Shell/browser/lifecycle.cpp, styles.cpp and frame.cpp),
// in the same order and through the same shared functions.
//
//===----------------------------------------------------------------------===//

#include <ctbrowser/page/static_page.hpp>

#include <ctbrowser/dom/encoding.hpp>
#include <ctbrowser/dom/treebuilder.hpp>
#include <ctbrowser/page/author_styles.hpp>
#include <ctbrowser/page/client_rects.hpp>
#include <ctbrowser/page/computed_style.hpp>
#include <ctbrowser/page/pipeline.hpp>
#include <ctbrowser/raster/raster.hpp>
#include <ctbrowser/shell/metrics.hpp>

namespace ctbrowser::page {

namespace {

/// The style engine's measure for `ch`: the built-in 8x8 face, which is what
/// a browser that never loaded real fonts measures with.
[[nodiscard]] float font8x8_advance(std::string_view text, float size, std::string_view family,
                                    bool bold, bool italic) {
    return raster::font8x8_fonts().advance(text, size, family, bold, italic);
}

} // namespace

static_page::static_page(static_page_options options) : options_(options) {
    load_html({});
}

static_page::~static_page() = default;

void static_page::load_html(std::string_view html) {
    // A new document and a new cascade, as browser::reset_document makes.
    doc_ = std::make_unique<document>(atoms_);
    styles_ = std::make_unique<style::engine>(atoms_);
    styles_->set_text_measure(font8x8_advance);
    // The window before the first sheet, so its conditions see the real
    // viewport.
    style::css::media_environment env = styles_->environment();
    env.viewport_width = static_cast<float>(options_.width);
    env.viewport_height = static_cast<float>(options_.height);
    (void)styles_->set_environment(env);
    styles_->add_sheet(style::ua_css, style::ua_origin);
    resolved_.clear();
    boxes_ = {};
    fragments_ = {};
    layout_width_ = 0;
    style_error_.clear();
    if (html.empty()) { return; }

    // What the page declares it is encoded as, then the parse as the bindings'
    // parse_document runs it for a navigation's document, with no script hook.
    if (const std::string declared = prescan_encoding(html); !declared.empty()) {
        doc_->set_encoding(declared);
    }
    doc_->set_allow_declarative_shadow_roots(true);
    html::tree_builder parser{*doc_, atoms_};
    parser.begin(decode_document_bytes(html, doc_->encoding()), false);

    // The author sheets, as browser::load_author_styles collects them.
    styles_->set_container_size(
        [this](node_id id) { return container_size_of(fragments_, atoms_, id); });
    author_style_hooks hooks;
    hooks.missing = [this](const std::string & href) {
        if (style_error_.empty()) {
            style_error_ = "<link rel=stylesheet href=\"" + href + "\"> not found";
        }
    };
    const std::string css = collect_author_styles(*doc_, atoms_, assets_, hooks);
    if (!css.empty()) { styles_->add_sheet(css, style::author_origin); }
}

void static_page::frame() {
    ++frames_;
    resolve();
    lay_out();
    // Container queries asked the previous layout's sizes; re-run until they
    // hold still, at most three extra passes (browser::settle_container_queries).
    for (int pass = 0; pass < 3 && styles_->container_sizes_changed(); ++pass) {
        resolve();
        lay_out();
    }
}

void static_page::resolve() {
    resolved_ =
        resolve_page_styles(*styles_, doc_->read(), atoms_, static_cast<float>(options_.width),
                            static_cast<float>(options_.height), options_.scrollbar_width);
}

void static_page::lay_out() {
    const auto txn = doc_->read();
    const layout::measure_text_fn measure = shell::font8x8_metrics();
    layout::box_builder builder{atoms_, resolved_, measure};
    boxes_ = builder.build(txn, txn.root());
    page_layout laid =
        lay_out_page(boxes_, measure, atoms_, resolved_, txn, static_cast<float>(options_.width),
                     static_cast<float>(options_.height), options_.scrollbar_width);
    fragments_ = std::move(laid.fragments);
    layout_width_ = laid.layout_width;
}

int static_page::viewport_client_width() const noexcept {
    return layout_width_ > 0 ? static_cast<int>(layout_width_) : options_.width;
}

page_view static_page::view() {
    page_view out;
    out.doc = doc_.get();
    out.atoms = &atoms_;
    out.styles = &resolved_;
    out.selector_engine = styles_.get();
    out.boxes = &boxes_;
    out.fragments = &fragments_;
    out.viewport_width = viewport_client_width();
    out.viewport_height = options_.height;
    return out;
}

rect static_page::bounding_client_rect(node_id id) {
    std::vector<rect> rects;
    for (const box_geometry & box : client_boxes(view(), id, "border")) {
        rects.push_back(box.bounding_rect());
    }
    return page::bounding_client_rect(rects);
}

std::vector<std::pair<std::string, std::string>> static_page::computed_style(node_id id) {
    return computed_style_entries(view(), id);
}

} // namespace ctbrowser::page
