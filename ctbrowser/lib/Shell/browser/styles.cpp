//===- ctbrowser/lib/Shell/browser/styles.cpp - Author styles -------------===//
//
// The author's style sheets: collected from the document through
// page::collect_author_styles, re-collected when a script changes them,
// container-query settling, and the <title>.
//
//===----------------------------------------------------------------------===//

#include "internal.hpp"

namespace ctbrowser::shell {

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
        [this](node_id id) { return page::container_size_of(fragments_, atoms_, id); });
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
    page::author_style_hooks hooks;
    if (bindings != nullptr) {
        hooks.preferred_title = [bindings] { return bindings->preferred_sheet_title(); };
        hooks.link_enabled = [bindings](node_id id) {
            return bindings->link_explicitly_enabled(id);
        };
    }
    // Only the page's own load announces: a sheet's `load` and `error`, and
    // the first missing <link> as the page's style error.
    if (announce) {
        hooks.loaded = [this](node_id id, bool ok) { note_resource_load(id, ok); };
        hooks.missing = [this](const std::string & href) {
            if (style_error_.empty()) {
                style_error_ = "<link rel=stylesheet href=\"" + href + "\"> not found";
            }
        };
    }
    return page::collect_author_styles(doc, atoms_, assets_, hooks);
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
