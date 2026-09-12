// browser - the author's stylesheets: <style> and <link rel=stylesheet>
// collected from the document, re-collected when a script changes them, and
// the <title>.

#include "internal.hpp"

namespace ctbrowser::shell {

namespace {

// `@import`, EXPANDED IN PLACE. The sheet parser drops the statement because it
// cannot fetch; this can, through the same registry the `<link>` came from. The
// imported text goes where the statement stood - so it precedes the sheet's own
// rules, as the cascade requires - wrapped in the import's media query when it
// has one, and expanded itself first so a chain of imports nests correctly.
// `chain` is the hrefs being expanded, which refuses a cycle and caps the depth.
std::string expand_imports(const asset_registry & assets, std::string css, std::string_view base,
                           std::vector<std::string> & chain) {
    if (chain.size() >= 16) { return css; }
    const std::vector<ctbrowser::style::css::import_statement> imports =
        ctbrowser::style::css::leading_imports(css);
    if (imports.empty()) { return css; }
    std::string out;
    std::size_t at = 0;
    for (const ctbrowser::style::css::import_statement & each : imports) {
        out += css.substr(at, each.begin - at);
        at = each.end;
        const std::string href = dom_bindings::resolve_sheet_href(base, each.href);
        if (std::ranges::find(chain, href) != chain.end()) { continue; }
        const std::vector<std::byte> bytes = assets.load(href);
        std::string text{reinterpret_cast<const char *>(bytes.data()), bytes.size()};
        chain.push_back(href);
        text = expand_imports(assets, std::move(text), href, chain);
        chain.pop_back();
        if (each.media.empty()) {
            out += text + "\n";
        } else {
            out += "@media " + each.media + " {\n" + text + "\n}\n";
        }
    }
    out += css.substr(at);
    return out;
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
    const atom disabled_attribute = atoms_.intern_lower("disabled");
    const atom title_attribute = atoms_.intern_lower("title");
    // HTML 4.2.6: the first TITLED sheet in tree order names the preferred
    // style sheet set, and a sheet with any other title is an alternative set
    // that does not apply - unless its link was explicitly enabled.
    std::string preferred;
    bool have_preferred = false;
    const auto applies = [&](node_id at, bool enabled) {
        const std::string_view title = txn.attribute_value(at, title_attribute);
        if (title.empty()) { return true; }
        if (!have_preferred) {
            preferred = std::string{title};
            have_preferred = true;
        }
        return enabled || title == preferred;
    };
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
            // Which links are stylesheets is the CSSOM's answer too, so the two
            // cannot disagree - dom_bindings::link_sheet_state. An ALTERNATE
            // sheet is fetched for its `load` and applies nothing.
            const bool enabled = bindings_ && bindings_->link_explicitly_enabled(at);
            const dom_bindings::link_sheet state =
                tag == link_tag
                    ? dom_bindings::link_sheet_state(txn.attribute_value(at, rel_attribute),
                                                     txn.has_attribute(at, disabled_attribute),
                                                     enabled)
                    : dom_bindings::link_sheet::none;
            if (tag == style_tag) {
                if (applies(at, false)) {
                    std::string text;
                    for (const node_id child : txn.children(at)) { text += txn.text(child); }
                    std::vector<std::string> chain;
                    css += expand_imports(assets_, std::move(text), {}, chain);
                    css += '\n';
                }
                // HTML "update a style block" ends by firing `load` at the
                // element; the sheet is applied by the caller straight after
                // this walk, and the event is queued for the tick after that.
                note_resource_load(at, true);
            } else if (state != dom_bindings::link_sheet::none) {
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
                } else if (state == dom_bindings::link_sheet::active && applies(at, enabled)) {
                    std::string text{reinterpret_cast<const char *>(bytes.data()), bytes.size()};
                    std::vector<std::string> chain{href};
                    css += expand_imports(assets_, std::move(text), href, chain);
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
