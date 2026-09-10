// browser - the author's stylesheets: <style> and <link rel=stylesheet>
// collected from the document, re-collected when a script changes them, and
// the <title>.
//
// One of ten files carved out of a 2,871-line Shell/browser.cpp on 2026-09-08.
// All are member functions of one class declared in
// include/ctbrowser/shell/browser.hpp; internal.hpp beside this carries the
// includes browser.cpp had, so every file sees exactly what it saw. Nothing
// about the public header changed.

#include "internal.hpp"

namespace ctbrowser::shell {

namespace {

// Does this `rel` make the link a stylesheet? The attribute is a
// space-separated token list matched ASCII case-insensitively, so `stylesheet`,
// `StyleSheet` and `stylesheet preload` are all one.
//
// `alternate stylesheet` is NOT one: it is a user-selectable alternative and a
// browser leaves it disabled until something picks it. Applying it would make a
// page that offers a light and a dark sheet render both.
[[nodiscard]] bool rel_is_stylesheet(std::string_view rel) {
    bool stylesheet = false;
    std::size_t at = 0;
    while (at < rel.size()) {
        const std::size_t start = rel.find_first_not_of(ctbrowser::html_whitespace, at);
        if (start == std::string_view::npos) { break; }
        std::size_t end = rel.find_first_of(ctbrowser::html_whitespace, start);
        if (end == std::string_view::npos) { end = rel.size(); }
        const std::string_view token = rel.substr(start, end - start);
        if (ctbrowser::ascii_iequals(token, "alternate")) { return false; }
        if (ctbrowser::ascii_iequals(token, "stylesheet")) { stylesheet = true; }
        at = end;
    }
    return stylesheet;
}

} // namespace

void browser::load_author_styles() {
    if (author_sheet_loaded_) { return; }
    author_css_ = collect_author_styles();
    if (!author_css_.empty()) { styles_->add_sheet(author_css_, ctbrowser::style::author_origin); }
    author_sheet_loaded_ = true;
}

// AUTHOR STYLES CAN CHANGE AFTER THE PAGE HAS LOADED, and until this they could
// not: `load_author_styles` latched, so a `<style>` a script appended, a
// `<style>` whose text it rewrote and a `<link>` it inserted all did nothing at
// all. That is not a small gap - injecting a stylesheet and then reading
// `getComputedStyle` is how a great many tests and no few libraries work.
//
// Re-collected rather than diffed, and only when the cascade is being re-run
// anyway: a restyle already walks the whole document and re-resolves every
// element, so one more walk to rebuild the text is proportionate. The rules are
// replaced only when the TEXT differs, so the ordinary case - a script that
// changed a class - pays the walk and nothing else.
//
// It goes through the DOM's own text and NOT through the CSSOM, and that is the
// path a page which never touches the object model keeps taking, byte for byte.
// The CSSOM's serialisation is a CANONICAL one - `author_style_text()` rebuilds
// every rule from the compiled form rather than repeating the author's bytes -
// so routing every page through it would re-spell every sheet in the corpus for
// no gain. A page that HAS used the object model is the other case, and the
// `set_author_styles_hook` installed by run_scripts is where it is answered.
void browser::refresh_author_styles() {
    if (!author_sheet_loaded_) { return; }
    std::string css = collect_author_styles();
    // A sheet a script inserted is owed its `load` - queued for the next tick,
    // and the text below is applied before this call returns, so the listener
    // sees it applied. BEFORE the early return: an inserted <link> whose href
    // resolved to nothing changes no text and still owes an `error`.
    announce_resource_loads();
    if (css == author_css_) { return; }
    author_css_ = std::move(css);
    styles_->clear_origin(ctbrowser::style::author_origin);
    if (!author_css_.empty()) { styles_->add_sheet(author_css_, ctbrowser::style::author_origin); }
}

void browser::note_resource_load(node_id id, bool ok) {
    // ponytail: a linear scan. A page has a handful of sheets and scripts, and
    // the walk that asks is already linear in the document.
    if (std::ranges::find(announced_loads_, id) != announced_loads_.end()) { return; }
    announced_loads_.push_back(id);
    resource_loads_.emplace_back(id, ok);
}

void browser::announce_resource_loads() {
    if (!bindings_) { return; } // before run_scripts: kept for the drain after it
    for (const auto & [id, ok] : resource_loads_) { bindings_->announce_load(id, ok); }
    resource_loads_.clear();
}

std::string browser::collect_author_styles() {
    const auto txn = doc_->read();
    const atom style_tag = atoms_.intern_lower("style");
    const atom link_tag = atoms_.intern_lower("link");
    const atom rel_attribute = atoms_.intern_lower("rel");
    const atom href_attribute = atoms_.intern_lower("href");
    // ONE sheet, concatenated in document order, rather than one add_sheet per
    // <style> and <link>. Both preserve source order; this one also cannot get
    // it wrong, because ctcss numbers a declaration's `order` from zero on every
    // parse_value call - so two author-origin add_sheet calls would tie on
    // source order and resolve by whichever the sort happened to see first.
    std::string css;
    const auto walk = [&](auto && self, node_id at) -> void {
        // HTML ONLY, for both. An SVG carries its own <style>, scoped to the
        // graphic, and it interns to the same atom - so without the namespace
        // check an `<svg><style>p { color: red }</style>` restyles every
        // paragraph on the page.
        if (txn.element_ns(at) == ctbrowser::node_ns::html) {
            const atom tag = txn.tag(at).value_or(atom{});
            if (tag == style_tag) {
                for (const node_id child : txn.children(at)) { css += txn.text(child); }
                css += '\n';
                // HTML "update a style block" ends by firing `load` at the
                // element; the sheet is applied by the caller straight after
                // this walk, and the event is queued for the tick after that.
                note_resource_load(at, true);
            } else if (tag == link_tag &&
                       rel_is_stylesheet(txn.attribute_value(at, rel_attribute))) {
                // Resolved by the asset registry exactly as <script src> is
                // (see load_page_scripts): registry, then data:, then the
                // filesystem from three roots. A miss is RECORDED, not passed
                // over - a stylesheet that silently does not load is a page
                // that lays out as though it had no author styles at all, and
                // that reads as an engine fault rather than a bad path.
                const std::string href{txn.attribute_value(at, href_attribute)};
                const std::vector<std::byte> bytes = assets_.load(href);
                if (bytes.empty()) {
                    if (style_error_.empty()) {
                        style_error_ = "<link rel=stylesheet href=\"" + href + "\"> not found";
                    }
                } else {
                    css.append(reinterpret_cast<const char *>(bytes.data()), bytes.size());
                    css += '\n';
                }
                // `load` once the sheet applies, `error` when there is nothing
                // to apply - the page's LoadObserver is written for both.
                note_resource_load(at, !bytes.empty());
            }
        }
        for (const node_id child : txn.children(at)) { self(self, child); }
    };
    walk(walk, txn.root());
    return css;
}

std::string browser::extract_title() {
    const auto txn = doc_->read();
    const atom title_tag = atoms_.intern_lower("title");
    std::string found;
    const auto walk = [&](auto && self, node_id at) -> void {
        if (!found.empty()) { return; }
        // HTML <title> ONLY. In SVG a <title> is the graphic's accessible name
        // - a tooltip - and it is extremely common; interning to the same atom,
        // it would otherwise become the WINDOW title of any page whose own
        // <title> is missing or comes later.
        if (txn.tag(at).value_or(atom{}) == title_tag &&
            txn.element_ns(at) == ctbrowser::node_ns::html) {
            for (const node_id child : txn.children(at)) { found += txn.text(child); }
        }
        for (const node_id child : txn.children(at)) { self(self, child); }
    };
    walk(walk, txn.root());
    return found;
}

} // namespace ctbrowser::shell
