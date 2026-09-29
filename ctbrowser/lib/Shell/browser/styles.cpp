// browser - the author's stylesheets: <style> and <link rel=stylesheet>
// collected from the document, re-collected when a script changes them, and
// the <title>.

#include "internal.hpp"

#include <ctbrowser/style/css/token.hpp>

namespace ctbrowser::shell {

namespace {

// `@import`, EXPANDED IN PLACE. The sheet parser records the statement because it
// cannot fetch; this can, through the same registry the `<link>` came from. The
// imported text goes where the statement stood - so it precedes the sheet's own
// rules, as the cascade requires - wrapped in the import's media query when it
// has one, and expanded itself first so a chain of imports nests correctly.
// `chain` is the hrefs being expanded, which refuses a cycle and caps the depth.
// ponytail: an import's environment encoding is the document's, where CSS Syntax
// 3 §3.2 says the importing sheet's; thread the decoded one down if a page mixes.
std::string expand_imports(const asset_registry & assets, std::string css, std::string_view base,
                           std::vector<std::string> & chain, std::string_view environment) {
    if (chain.size() >= 16) { return css; }
    ctbrowser::atom_table atoms;
    const std::vector<ctbrowser::style::css::import_statement> imports =
        ctbrowser::style::css::parse_stylesheet(css, atoms).imports;
    if (imports.empty()) { return css; }
    std::string out;
    std::size_t at = 0;
    for (const ctbrowser::style::css::import_statement & each : imports) {
        out += css.substr(at, each.begin - at);
        at = each.end;
        const std::string href = dom_bindings::resolve_sheet_href(base, each.href);
        if (std::ranges::find(chain, href) != chain.end()) { continue; }
        const std::vector<std::byte> bytes = assets.load(href);
        std::string text = ctbrowser::style::css::decode_stylesheet(
            {reinterpret_cast<const char *>(bytes.data()), bytes.size()}, {}, environment);
        chain.push_back(href);
        text = expand_imports(assets, std::move(text), href, chain, environment);
        chain.pop_back();
        // THE IMPORT'S CONDITIONS AND LAYER, as the blocks they stand for (CSS
        // Cascade 5 §5.1): `layer(a)` files the sheet's rules in layer `a` and
        // declares it here, where the statement stood; `supports()` and the
        // media list gate them. Innermost first, so the layer is what the
        // conditions wrap.
        if (each.layered) { text = "@layer " + each.layer + " {\n" + text + "\n}"; }
        // `scope(...)` scopes the rules as the `@scope` block it stands for
        // (CSS Cascade 6 §2.2) - an empty one the implicit scope, rooted where
        // the importing sheet's is (collect_author_styles' `scope_root`).
        if (each.scoped) { text = "@scope " + each.scope + " {\n" + text + "\n}"; }
        if (!each.supports.empty()) {
            // Parenthesised whichever form was written: `supports(display:
            // flex)` takes a bare declaration where `@supports` wants a group.
            text = "@supports (" + each.supports + ") {\n" + text + "\n}";
        }
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
    // A QUERY CONTAINER'S SIZE is its content box as last laid out (CSS
    // Conditional 5 §3.1) - asked by the cascade, answered from the fragments,
    // and nothing before the first layout, when every size query is unknown.
    // A CONTROL'S LIVE VALUE, for `dir=auto` (HTML §3.2.6.4): the store's
    // once it was edited, and the `value` attribute - the engine's own
    // reading - until then.
    styles_->set_control_value([this](node_id id) -> std::optional<std::string> {
        const ctbrowser::shell::control_state * state = forms_.find(id);
        if (state == nullptr || !state->value_edited) { return std::nullopt; }
        return state->value;
    });
    // WHAT :valid AND :invalid MATCH (HTML 4.16.3): the bindings' answer, the
    // same flags `validity` reports, so a sheet and a script cannot disagree.
    styles_->set_validity([this](node_id id) -> std::optional<bool> {
        return bindings_ ? bindings_->control_validity(id) : std::nullopt;
    });
    styles_->set_container_size(
        [this](node_id id) -> std::optional<ctbrowser::style::engine::container_size> {
            const ctbrowser::layout::fragment * f = fragments_.find(id);
            // Size containment does not apply to an inline box, a table or
            // an internal table box, and `display: contents` has no box at
            // all - so none of them has a size to answer with (CSS
            // Containment 2 §3.2, ineligible-containment,
            // size-container-no-principal-box).
            if (f == nullptr || f->box == nullptr ||
                f->box->kind == ctbrowser::layout::box_kind::inline_ ||
                f->box->kind == ctbrowser::layout::box_kind::text ||
                f->box->kind == ctbrowser::layout::box_kind::table) {
                return std::nullopt;
            }
            if (f->box->style) {
                const std::string_view display = f->box->style->get(atoms_.intern_lower("display"));
                if (display == "contents" || display.starts_with("table")) { return std::nullopt; }
            }
            const ctbrowser::layout::resolved_edges e = ctbrowser::layout::edges_of(*f);
            ctbrowser::style::engine::container_size size{
                std::max(0.0f, f->bounds.width - e.horizontal_inner()),
                std::max(0.0f, f->bounds.height - e.vertical_inner())};
            // WHERE A SCROLL CONTAINER CAN STILL GO, for scroll-state(): its
            // scrolling area against its padding box at its scroll offset.
            // ponytail: the offset is taken as the origin - the bindings keep
            // a scripted one privately - so only a box nothing has scrolled
            // answers right; read dom_bindings::scroll_offset_of once it is
            // public, and restyle on a scroll.
            if (f->box->scroll_container) {
                const ctbrowser::rect area = ctbrowser::layout::scrolling_area_of(*f);
                const ctbrowser::rect port = ctbrowser::layout::padding_box_of(*f);
                const ctbrowser::point at{};
                namespace css = ctbrowser::style::css;
                const float right = area.width - port.width - at.x;
                const float down = area.height - port.height - at.y;
                size.scrollable = static_cast<std::uint8_t>(
                    (at.y > 0.5f ? css::scroll_top : 0) | (down > 0.5f ? css::scroll_bottom : 0) |
                    (at.x > 0.5f ? css::scroll_left : 0) | (right > 0.5f ? css::scroll_right : 0));
            }
            return size;
        });
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
    // ONCE THE PAGE HAS EDITED THROUGH THE CSSOM, THE CSSOM IS THE SHEET. The
    // DOM's text never had the rule `insertRule` added or the sheet
    // `adoptedStyleSheets` adopted, so re-collecting it here undid both the
    // moment anything else restyled - a `<style>` appended after an adoption
    // dropped the adopted sheet (CSSStyleSheet-constructable), and a `<style>`
    // whose text was rewritten kept the inserted rule the object model had
    // already re-parsed away (css-style-reparse). The walk above still runs
    // for the `load` events it records.
    if (bindings_ && bindings_->style_stamp() != 0) { css = bindings_->author_style_text(); }
    if (css == author_css_) { return; }
    author_css_ = std::move(css);
    styles_->clear_origin(ctbrowser::style::author_origin);
    if (!author_css_.empty()) { styles_->add_sheet(author_css_, ctbrowser::style::author_origin); }
}

// THE STYLE -> LAYOUT -> STYLE LOOP a container query closes (CSS
// Conditional 5 §3): the cascade asked each container's size of the layout
// before it, so a layout that sized a container differently has the cascade
// and the layout run again - and again, until the sizes hold still. Bounded,
// because a query that flips the size it asks about never settles; size
// containment is what the spec relies on to rule that out, and this layout
// does not apply it yet. Called at the end of every layout, and a no-op for
// a page no size query has asked about.
void browser::settle_container_queries() {
    // ponytail: one guard for every browser on the thread, since the loop
    // re-enters run_layout; a member when two pages ever lay out nested.
    static bool settling = false;
    if (settling) { return; }
    settling = true;
    for (int pass = 0; pass < 3 && styles_->container_sizes_changed(); ++pass) {
        resolve_styles();
        run_layout();
    }
    settling = false;
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
    return collect_author_styles(*doc_, bindings_.get(), true);
}

std::string browser::collect_author_styles(document & doc, dom_bindings * bindings, bool announce) {
    const auto txn = doc.read();
    const atom style_tag = atoms_.intern_lower("style");
    const atom link_tag = atoms_.intern_lower("link");
    const atom rel_attribute = atoms_.intern_lower("rel");
    const atom href_attribute = atoms_.intern_lower("href");
    const atom disabled_attribute = atoms_.intern_lower("disabled");
    const atom title_attribute = atoms_.intern_lower("title");
    // HTML 4.2.6: the first TITLED sheet names the preferred style sheet set,
    // and a sheet with any other title is an alternative set that does not
    // apply - unless its link was explicitly enabled. The set is the bindings'
    // sticky answer once they exist (dom_bindings::preferred_sheet_title);
    // before scripts run it is the first titled sheet of this walk, which is
    // the same answer for a parsed document.
    std::string preferred;
    bool have_preferred = false;
    const auto applies = [&](node_id at, bool enabled) {
        const std::string_view title = txn.attribute_value(at, title_attribute);
        if (title.empty()) { return true; }
        if (bindings != nullptr) {
            const std::string_view preferred = bindings->preferred_sheet_title();
            return enabled || preferred.empty() || title == preferred;
        }
        if (!have_preferred) {
            preferred = std::string{title};
            have_preferred = true;
        }
        return enabled || title == preferred;
    };
    // THE `media` ATTRIBUTE GATES THE SHEET (HTML §4.2.6, CSSOM §5.1): the
    // sheet's rules apply only while its media list matches, which is what
    // wrapping them in `@media` says to the cascade, evaluated and re-evaluated
    // against the window exactly as a rule of the sheet's own would be.
    const atom media_attribute = atoms_.intern_lower("media");
    //
    // EOF CLOSES WHAT THE ATTRIBUTE LEFT OPEN (CSS Syntax 3 §5.4.9): a list
    // written `(orientation` is `(orientation)`, and wrapping it as written
    // put the `{` of the @media rule inside its parenthesis, which swallowed
    // the sheet (test_media_queries' "parsing tests").
    const auto gated = [&](node_id at, std::string text) {
        const std::string_view media =
            trim(txn.attribute_value(at, media_attribute), ctbrowser::html_whitespace);
        if (media.empty()) { return text; }
        std::string list{media};
        std::string closers;
        const ctbrowser::style::css::token_stream ts = ctbrowser::style::css::tokenize(media);
        for (const ctbrowser::style::css::css_token & t : ts.tokens) {
            using ctbrowser::style::css::token_type;
            if (t.type == token_type::function || t.type == token_type::open_paren) {
                closers.push_back(')');
            } else if (t.type == token_type::open_square) {
                closers.push_back(']');
            } else if (t.type == token_type::open_curly) {
                closers.push_back('}');
            } else if ((t.type == token_type::close_paren || t.type == token_type::close_square ||
                        t.type == token_type::close_curly) &&
                       !closers.empty()) {
                closers.pop_back();
            }
        }
        list.append(closers.rbegin(), closers.rend());
        return "@media " + list + " {\n" + text + "\n}";
    };
    // ONE sheet, concatenated in document order, rather than one add_sheet per
    // <style> and <link>. Both preserve source order; this one also cannot get
    // it wrong, because ctcss numbers a declaration's `order` from zero on every
    // parse_value call - so two author-origin add_sheet calls would tie on
    // source order and resolve by whichever the sort happened to see first.
    std::string css;
    // AN IMPLICIT `@scope` IS ROOTED AT THE OWNER NODE'S PARENT (CSS Cascade 6
    // §2.1), and a concatenated sheet has lost its owner - so the root is
    // written in ahead of each sheet that says `@scope` at all, as the private
    // statement the sheet parser reads (css::scope_block). Every other sheet's
    // text is exactly the author's.
    const auto scope_root = [&](node_id owner, std::string_view text) -> std::string {
        if (ascii_lower_copy(text).find("@scope") == std::string::npos) { return {}; }
        return "@" + std::string{ctbrowser::style::css::scope_root_at_rule} + " " +
               std::to_string(ctbrowser::style::engine::key_of(txn.parent(owner))) + ";\n";
    };
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
            const bool enabled = bindings != nullptr && bindings->link_explicitly_enabled(at);
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
                    std::string sheet =
                        expand_imports(assets_, std::move(text), {}, chain, doc.encoding());
                    css += scope_root(at, sheet);
                    css += gated(at, std::move(sheet));
                    css += '\n';
                }
                // HTML "update a style block" ends by firing `load` at the
                // element; the sheet is applied by the caller straight after
                // this walk, and the event is queued for the tick after that.
                if (announce) { note_resource_load(at, true); }
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
                    if (announce && style_error_.empty()) {
                        style_error_ = "<link rel=stylesheet href=\"" + href + "\"> not found";
                    }
                } else if (state == dom_bindings::link_sheet::active && applies(at, enabled)) {
                    // Decoded as CSS Syntax 3 §3.2 says, the document's
                    // encoding the fallback (css-syntax/charset).
                    std::string text = ctbrowser::style::css::decode_stylesheet(
                        {reinterpret_cast<const char *>(bytes.data()), bytes.size()}, {},
                        doc.encoding());
                    std::vector<std::string> chain{href};
                    std::string sheet =
                        expand_imports(assets_, std::move(text), href, chain, doc.encoding());
                    css += scope_root(at, sheet);
                    css += gated(at, std::move(sheet));
                    css += '\n';
                }
                // `load` once the sheet applies, `error` when there is nothing
                // to apply - the page's LoadObserver is written for both.
                if (announce) { note_resource_load(at, !bytes.empty()); }
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
