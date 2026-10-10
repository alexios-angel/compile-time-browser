//===- ctbrowser/lib/Shell/browser/styles.cpp - Author styles -------------===//
//
// The author's style sheets: <style> and <link rel=stylesheet> collected from
// the document (with @import expanded in place), re-collected when a script
// changes them, container-query settling, and the <title>.
//
//===----------------------------------------------------------------------===//

#include "internal.hpp"

#include <ctbrowser/style/css/token.hpp>

namespace ctbrowser::shell {

namespace {

/// Limits on @import expansion per top-level sheet. The depth cap alone is not
/// enough: a hundred imports of a file that imports the next a hundred times
/// is 100^16 expansions inside it. Past max_imports imports, or max_bytes
/// (16 MiB) of imported text, a further import is skipped as one that failed
/// to load.
struct import_budget {
    static constexpr std::size_t max_imports = 256;
    static constexpr std::size_t max_bytes = 16 * 1024 * 1024;
    std::size_t imports = 0;
    std::size_t bytes = 0;
};

/// Expands `@import` in place. The sheet parser records the statement because
/// it cannot fetch; this can, through the same registry the `<link>` came
/// from. The imported text goes where the statement stood, so it precedes the
/// sheet's own rules as the cascade requires, wrapped in the import's
/// conditions, and is expanded itself first so a chain of imports nests
/// correctly. `chain` is the hrefs being expanded, which refuses a cycle and
/// caps the depth at 16.
///
/// ponytail: an import's environment encoding is the document's, where CSS
/// Syntax 3 §3.2 says the importing sheet's; thread the decoded one down if a
/// page mixes encodings.
std::string expand_imports(const asset_registry & assets, std::string css, std::string_view base,
                           std::vector<std::string> & chain, std::string_view environment,
                           import_budget & budget) {
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
        if (++budget.imports > import_budget::max_imports) { continue; }
        const std::vector<std::byte> bytes = assets.load(href);
        budget.bytes += bytes.size();
        if (budget.bytes > import_budget::max_bytes) { continue; }
        std::string text = ctbrowser::style::css::decode_stylesheet(
            {reinterpret_cast<const char *>(bytes.data()), bytes.size()}, {}, environment);
        chain.push_back(href);
        text = expand_imports(assets, std::move(text), href, chain, environment, budget);
        chain.pop_back();
        // The import's conditions and layer, as the blocks they stand for (CSS
        // Cascade 5 §5.1): `layer(a)` files the sheet's rules in layer `a` and
        // declares it here, where the statement stood; `supports()` and the
        // media list gate them. Innermost first, so the layer is what the
        // conditions wrap.
        if (each.layered) { text = "@layer " + each.layer + " {\n" + text + "\n}"; }
        // `scope(...)` scopes the rules as the `@scope` block it stands for
        // (CSS Cascade 6 §2.2); an empty one is the implicit scope, rooted
        // where the importing sheet's is (collect_author_styles' `scope_root`).
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

std::string expand_imports(const asset_registry & assets, std::string css, std::string_view base,
                           std::vector<std::string> & chain, std::string_view environment) {
    import_budget budget;
    return expand_imports(assets, std::move(css), base, chain, environment, budget);
}

} // namespace

void browser::load_author_styles() {
    if (author_sheet_loaded_) { return; }
    // A control's live value, for `dir=auto` (HTML §3.2.6.4): the store's
    // once it was edited, and the `value` attribute (the engine's own
    // reading) until then.
    styles_->set_control_value([this](node_id id) -> std::optional<std::string> {
        const ctbrowser::shell::control_state * state = forms_.find(id);
        if (state == nullptr || !state->value_edited) { return std::nullopt; }
        return state->value;
    });
    // What :valid and :invalid match (HTML 4.16.3): the bindings' answer, the
    // same flags `validity` reports, so a sheet and a script cannot disagree.
    styles_->set_validity([this](node_id id) -> std::optional<bool> {
        return bindings_ ? bindings_->control_validity(id) : std::nullopt;
    });
    // A query container's size is its content box as last laid out (CSS
    // Conditional 5 §3.1): asked by the cascade, answered from the fragments,
    // and nothing before the first layout, when every size query is unknown.
    styles_->set_container_size(
        [this](node_id id) -> std::optional<ctbrowser::style::engine::container_size> {
            const ctbrowser::layout::fragment * f = fragments_.find(id);
            // Size containment does not apply to an inline box, a table or
            // an internal table box, and `display: contents` has no box at
            // all, so none of them has a size to answer with (CSS
            // Containment 2 §3.2; WPT ineligible-containment,
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
            // Which directions a scroll container can still scroll, for
            // scroll-state(): its scrolling area against its padding box at
            // its scroll offset. A 0.5px tolerance absorbs rounding.
            // ponytail: the offset is taken as the origin (the bindings keep
            // a scripted one privately), so only a box nothing has scrolled
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

/// Re-collects the author styles after load: a script may append a <style>,
/// rewrite one, or insert a <link>.
///
/// Re-collected rather than diffed, and only when the cascade is being re-run
/// anyway: a restyle already walks the whole document and re-resolves every
/// element, so one more walk to rebuild the text is proportionate. The rules
/// are replaced only when the text differs, so the ordinary case (a script
/// that changed a class) pays the walk and nothing else.
///
/// It reads the DOM's own text, not the CSSOM, for a page that never touches
/// the object model. The CSSOM's serialisation is canonical
/// (`author_style_text()` rebuilds every rule from the compiled form rather
/// than repeating the author's bytes), so routing every page through it would
/// re-spell every sheet for no gain. A page that has used the object model is
/// handled below and by the `set_author_styles_hook` run_scripts installs.
void browser::refresh_author_styles() {
    if (!author_sheet_loaded_) { return; }
    std::string css = collect_author_styles();
    // A sheet a script inserted is owed its `load`, queued for the next tick;
    // the text below is applied before this call returns, so the listener
    // sees it applied. This comes before the early return: an inserted <link>
    // whose href resolved to nothing changes no text and still owes an
    // `error`.
    announce_resource_loads();
    // Once the page has edited through the CSSOM, the CSSOM is the sheet. The
    // DOM's text lacks the rule `insertRule` added and the sheet
    // `adoptedStyleSheets` adopted, so using it here would undo both the
    // moment anything else restyled (WPT CSSStyleSheet-constructable,
    // css-style-reparse). The walk above still runs for the `load` events it
    // records.
    if (bindings_ && bindings_->style_stamp() != 0) { css = bindings_->author_style_text(); }
    if (css == author_css_) { return; }
    author_css_ = std::move(css);
    styles_->clear_origin(ctbrowser::style::author_origin);
    if (!author_css_.empty()) { styles_->add_sheet(author_css_, ctbrowser::style::author_origin); }
}

/// Closes the style -> layout -> style loop of container queries (CSS
/// Conditional 5 §3). The cascade asked each container's size of the previous
/// layout, so a layout that sized a container differently runs the cascade
/// and layout again until the sizes hold still, at most three extra passes,
/// because a query that flips the size it asks about never settles (size
/// containment, which the spec relies on to rule that out, is not applied
/// yet). Called at the end of every layout; a no-op for a page no size query
/// has asked about.
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
    if (!bindings_) { return; } // Before run_scripts: kept for the drain after it.
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
    // HTML 4.2.6: the first titled sheet names the preferred style sheet set,
    // and a sheet with any other title is an alternative set that does not
    // apply, unless its link was explicitly enabled. The set is the bindings'
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
    // The `media` attribute gates the sheet (HTML §4.2.6, CSSOM §5.1): the
    // sheet's rules apply only while its media list matches, which is what
    // wrapping them in `@media` says to the cascade, evaluated and re-evaluated
    // against the window exactly as a rule of the sheet's own would be.
    const atom media_attribute = atoms_.intern_lower("media");
    //
    // EOF closes what the attribute left open (CSS Syntax 3 §5.4.9): a list
    // written `(orientation` is `(orientation)`. Wrapped as written, the `{`
    // of the @media rule would land inside the parenthesis and swallow the
    // sheet (test_media_queries' "parsing tests").
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
    // One sheet, concatenated in document order, rather than one add_sheet per
    // <style> and <link>: the parser numbers a declaration's `order` from zero
    // in every sheet, so two author-origin add_sheet calls would tie on source
    // order and resolve by whichever the sort happened to see first.
    std::string css;
    // An implicit `@scope` is rooted at the owner node's parent (CSS Cascade 6
    // §2.1), and a concatenated sheet has lost its owner, so the root is
    // written in ahead of each sheet that says `@scope` at all, as the private
    // statement the sheet parser reads (css::scope_block). Every other sheet's
    // text is exactly the author's.
    const auto scope_root = [&](node_id owner, std::string_view text) -> std::string {
        if (ascii_lower_copy(text).find("@scope") == std::string::npos) { return {}; }
        return "@" + std::string{ctbrowser::style::css::scope_root_at_rule} + " " +
               std::to_string(ctbrowser::style::engine::key_of(txn.parent(owner))) + ";\n";
    };
    const auto walk = [&](auto && self, node_id at) -> void {
        // HTML elements only, for both. An SVG carries its own <style>, scoped
        // to the graphic, and it interns to the same atom, so without the
        // namespace check an `<svg><style>p { color: red }</style>` would
        // restyle every paragraph on the page.
        if (txn.element_ns(at) == ctbrowser::node_ns::html) {
            const atom tag = txn.tag(at).value_or(atom{});
            // Which links are stylesheets is the CSSOM's answer too
            // (dom_bindings::link_sheet_state), so the two cannot disagree. An
            // alternate sheet is fetched for its `load` and applies nothing.
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
                // Resolved by the asset registry exactly as <script src> is:
                // registry, then data:, then the filesystem from three roots.
                // A miss is recorded in style_error_, because a stylesheet that
                // silently does not load makes a page lay out as though it had
                // no author styles, which reads as an engine fault rather than
                // a bad path.
                const std::string href{txn.attribute_value(at, href_attribute)};
                const std::vector<std::byte> bytes = assets_.load(href);
                if (bytes.empty()) {
                    if (announce && style_error_.empty()) {
                        style_error_ = "<link rel=stylesheet href=\"" + href + "\"> not found";
                    }
                } else if (state == dom_bindings::link_sheet::active && applies(at, enabled)) {
                    // Decoded as CSS Syntax 3 §3.2 says, with the document's
                    // encoding as the fallback (WPT css-syntax/charset).
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
                // to apply; the page's LoadObserver handles both.
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
        // HTML <title> only. In SVG a <title> is the graphic's accessible name
        // (a tooltip) and it is very common; interning to the same atom, it
        // would otherwise become the window title of any page whose own
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
