// dom_bindings' CSSOM - the sheet list: the document's own sheets collected
// lazily, synced to document.styleSheets, and installed on the document; a
// shadow root's the same way onto its own list; and the one rule for which
// `<link>`s are stylesheets that the cascade shares.

#include "internal.hpp"

#include <ctbrowser/shell/net/url.hpp>

namespace ctbrowser::shell {

using namespace detail;

// ONE TEST FOR BOTH HALVES. browser::collect_author_styles had a copy of the
// `rel` scan in its own anonymous namespace, and the CSSOM answering a
// different question from the cascade about which links are stylesheets is
// exactly the disagreement this file exists to avoid - so the cascade calls
// this. `rel` is a space-separated token list matched ASCII case-insensitively.
dom_bindings::link_sheet dom_bindings::link_sheet_state(std::string_view rel,
                                                        bool disabled_attribute,
                                                        bool explicitly_enabled) {
    bool stylesheet = false;
    bool alternate = false;
    std::size_t at = 0;
    while (at < rel.size()) {
        const std::size_t start = rel.find_first_not_of(html_whitespace, at);
        if (start == std::string_view::npos) { break; }
        std::size_t end = rel.find_first_of(html_whitespace, start);
        if (end == std::string_view::npos) { end = rel.size(); }
        const std::string_view token = rel.substr(start, end - start);
        if (ascii_iequals(token, "alternate")) { alternate = true; }
        if (ascii_iequals(token, "stylesheet")) { stylesheet = true; }
        at = end;
    }
    if (!stylesheet || disabled_attribute) { return link_sheet::none; }
    return alternate && !explicitly_enabled ? link_sheet::alternate : link_sheet::active;
}

bool dom_bindings::link_explicitly_enabled(node_id id) const {
    return std::ranges::find(enabled_links_, pack(id)) != enabled_links_.end();
}

std::string dom_bindings::resolve_sheet_href(std::string_view base, std::string_view reference) {
    if (base.empty() || reference.empty() || is_data_url(reference) ||
        parse_absolute(reference).valid || reference.front() == '/') {
        return std::string{reference};
    }
    if (parse_absolute(base).valid) { return resolve(base, reference); }
    // A relative base is a path the registry resolves from the page's
    // directory, so its imports live beside it. A data: base has no directory.
    if (is_data_url(base)) { return std::string{reference}; }
    const std::size_t slash = base.rfind('/');
    if (slash == std::string_view::npos) { return std::string{reference}; }
    return std::string{base.substr(0, slash + 1)} + std::string{reference};
}

std::string dom_bindings::author_style_text() {
    // THE DOCUMENT'S OWN SHEETS ARE COLLECTED LAZILY, on a read of
    // `document.styleSheets` - so a page that adopted a constructed sheet
    // without ever touching `document.styleSheets` had `css_document_sheets_`
    // empty here, and its `<style>` element vanished from the serialisation
    // while the adopted sheets survived. Syncing first is what makes this the
    // FINAL list of style sheets CSSOM defines rather than whatever the page
    // happened to have asked for.
    if (cx_ != nullptr) { sync_style_sheets(*cx_); }
    std::string out;
    // AN `@import` IS ITS SHEET'S RULES, wrapped in the import's media query
    // when it has one - which is what the cascade would have got from the
    // author's bytes, and the `@import` line itself is what the sheet parser
    // drops. Depth-capped for the cycle load_imported_sheet already refuses.
    const auto emit = [&](auto && self, std::size_t at, std::size_t depth) -> void {
        if (at >= css_sheets_.size() || depth > 16) { return; }
        const std::unique_ptr<css_sheet_record> & sheet = css_sheets_[at];
        if (sheet->disabled) { return; }
        // An alternative style sheet set - titled, and not the preferred one -
        // does not apply unless its link was explicitly enabled.
        if (depth == 0 && !sheet->title.empty() && sheet->title != css_preferred_title_ &&
            !link_explicitly_enabled(sheet->owner)) {
            return;
        }
        for (const std::size_t rule : sheet->rules) {
            if (rule >= css_rule_store_.size()) { continue; }
            const css_rule_record & record = *css_rule_store_[rule];
            if (record.type == import_rule && record.imported_sheet != no_index) {
                const std::string media = serialize_media_query_list(record.media_queries);
                if (!media.empty()) { out += "@media " + media + " {\n"; }
                self(self, record.imported_sheet, depth + 1);
                if (!media.empty()) { out += "}\n"; }
                continue;
            }
            out += rule_css_text(record);
            out += '\n';
        }
    };
    for (const std::size_t at : css_document_sheets_) { emit(emit, at, 0); }
    // AND THE ADOPTED SHEETS, AFTER THEM AND IN THEIR OWN ORDER.
    //
    // They were in the object model and in nothing else: `adoptedStyleSheets`
    // held an array, `document.styleSheets` correctly did not include it (a
    // constructed sheet is not a document sheet), and so a sheet a page adopted
    // reached the cascade through no route at all. CSSOM puts them LAST in the
    // final list of style sheets, which is what makes
    // `adoptedStyleSheets = [red, green]` green and `[green, red]` red -
    // `adoptedstylesheets-cascade-order.html` asserts exactly that pair, and
    // then asserts it again for a rotation, which only an ordered walk of the
    // array can answer.
    //
    // Read from the ARRAY rather than from a mirror kept beside it, because the
    // page owns that array and a mirror updated only on assignment would be a
    // second answer to what has been adopted.
    if (script::object_object * internals = as_object(cssom_internals_)) {
        if (const value * held = internals->find("adopted"); held != nullptr && held->is_array()) {
            for (const value each : static_cast<script::array_object *>(held->as_heap())->items) {
                emit(emit, slot_index(as_object(each), sheet_key), 0);
            }
        }
    }
    return out;
}

void dom_bindings::style_sheets_changed() {
    if (on_author_styles_) { on_author_styles_(author_style_text()); }
    // MARKED DIRTY EITHER WAY. With no hook the cascade does not observe the
    // change and the frame is identical, which costs one recomposite on an
    // operation a page performs a handful of times; with a hook the browser has
    // already replaced the author sheet and this is what schedules the restyle.
    mutated();
}

// --- the document's sheets, re-derived --------------------------------------

script::object_object * dom_bindings::cssom_internals(context & cx) {
    if (script::object_object * held = as_object(cssom_internals_)) { return held; }
    script::object_object * doc = document_object();
    if (doc == nullptr) { return nullptr; }
    if (const value * found = doc->find(internals_key)) {
        cssom_internals_ = *found;
        return as_object(cssom_internals_);
    }
    cssom_internals_ = cx.make_object();
    // ON THE DOCUMENT, non-configurable, and that is what roots it. Everything
    // the CSSOM holds hangs off this object, `register_roots` already marks
    // `document_`, and the collector traces an object's properties - so this
    // needs no line in a file this rung does not own. A page cannot delete it
    // either, the property being non-configurable.
    doc->define(internals_key, cssom_internals_, script::attr_none);
    return as_object(cssom_internals_);
}

value dom_bindings::style_sheet_list(context & cx) {
    script::object_object * internals = cssom_internals(cx);
    if (internals == nullptr) { return value::undefined(); }
    if (const value * found = internals->find("list")) { return *found; }
    const value list = cx.make_object();
    if (script::object_object * obj = as_object(list)) {
        if (const value * proto = internals->find("StyleSheetList.prototype")) {
            obj->prototype = *proto;
        }
    }
    internals->set("list", list);
    return list;
}

// A StyleSheetList IS LIVE, and its `length` is where that is tested:
// ttwf-cssom-doc-ext-load-count.html holds `document.styleSheets` in a
// variable, removes a `<link>`, and reads `.length` off the variable. So the
// indexed properties are refreshed here and `length` is an accessor on the
// prototype that re-derives the list from the DOM before it counts - the
// count lives in a private slot rather than an own data property, which no
// accessor could stand in front of.
void dom_bindings::set_sheet_list(script::object_object & list, std::span<const value> items) {
    const std::size_t was = slot_index(&list, count_key);
    for (std::size_t i = items.size(); i < was && was != no_index; ++i) {
        (void)list.erase(std::to_string(i));
    }
    for (std::size_t i = 0; i < items.size(); ++i) {
        list.define(std::to_string(i), items[i], script::attr_enumerable);
    }
    list.define(count_key, value::number(static_cast<double>(items.size())), script::attr_none);
}

void dom_bindings::resync_sheet_list(context & cx, script::object_object & list) {
    if (const value * tree = list.find(tree_key)) {
        if (const node_id root = handle_of(*tree)) { sync_sheet_list(cx, root, list, nullptr); }
        return;
    }
    sync_style_sheets(cx);
}

namespace {

// The per-record object caches on the internals object: one plain object each
// for sheets and rules, keyed by record index. Reachable from the document,
// so nothing here needs a root of its own.
[[nodiscard]] value cached_object(context & cx, script::object_object & internals,
                                  std::string_view table, std::size_t index,
                                  const std::function<value()> & make) {
    script::object_object * held = nullptr;
    if (const value * found = internals.find(table)) { held = as_object(*found); }
    if (held == nullptr) {
        const value made = cx.make_object();
        internals.set(std::string{table}, made);
        held = as_object(made);
        if (held == nullptr) { return value::undefined(); }
    }
    const std::string key = std::to_string(index);
    if (const value * found = held->find(key)) { return *found; }
    const value made = make();
    held->set(key, made);
    return made;
}

} // namespace

value dom_bindings::sheet_object_for(context & cx, std::size_t sheet) {
    script::object_object * internals = cssom_internals(cx);
    if (internals == nullptr || sheet >= css_sheets_.size()) { return value::null(); }
    return cached_object(cx, *internals, "sheets", sheet,
                         [&] { return make_sheet_object(cx, sheet); });
}

value dom_bindings::rule_object_for(context & cx, std::size_t rule) {
    script::object_object * internals = cssom_internals(cx);
    if (internals == nullptr || rule >= css_rule_store_.size()) { return value::null(); }
    return cached_object(cx, *internals, "rules", rule, [&] { return make_rule_object(cx, rule); });
}

void dom_bindings::sync_style_sheets(context & cx) {
    script::object_object * list_obj = as_object(style_sheet_list(cx));
    if (list_obj == nullptr) { return; }
    const node_id root = doc_->read().root();
    sync_sheet_list(cx, root, *list_obj, &css_document_sheets_);
}

void dom_bindings::sync_sheet_list(context & cx, node_id from, script::object_object & list_obj,
                                   std::vector<std::size_t> * order) {
    // WHAT THE DOM SAYS NOW - the same walk browser::collect_author_styles
    // makes, through the same link test.
    struct found_sheet {
        node_id owner;
        bool linked = false;
        std::string href;
        std::string title;
        std::string media;
        std::string text;
    };
    std::vector<found_sheet> found;
    {
        const auto txn = doc_->read();
        const atom style_tag = atoms_->intern_lower("style");
        const atom link_tag = atoms_->intern_lower("link");
        const atom rel_attribute = atoms_->intern_lower("rel");
        const atom href_attribute = atoms_->intern_lower("href");
        const atom title_attribute = atoms_->intern_lower("title");
        const atom media_attribute = atoms_->intern_lower("media");
        const atom disabled_attribute = atoms_->intern_lower("disabled");
        const auto walk = [&](auto && self, node_id at) -> void {
            // HTML ONLY, for both, and for the reason load_author_styles gives:
            // an SVG carries its own <style>, it interns to the same atom, and
            // it is not a document stylesheet.
            if (txn.element_ns(at) == ctbrowser::node_ns::html) {
                const atom tag = txn.tag(at).value_or(atom{});
                const bool is_style = tag == style_tag;
                const bool is_link =
                    tag == link_tag &&
                    link_sheet_state(txn.attribute_value(at, rel_attribute),
                                     txn.has_attribute(at, disabled_attribute),
                                     link_explicitly_enabled(at)) == link_sheet::active;
                if (is_style || is_link) {
                    found_sheet made;
                    made.owner = at;
                    made.linked = is_link;
                    made.href = std::string{txn.attribute_value(at, href_attribute)};
                    made.title = std::string{txn.attribute_value(at, title_attribute)};
                    made.media = std::string{txn.attribute_value(at, media_attribute)};
                    if (is_style) {
                        for (const node_id child : txn.children(at)) {
                            made.text += txn.text(child);
                        }
                    }
                    found.push_back(std::move(made));
                }
            }
            for (const node_id child : txn.children(at)) { self(self, child); }
        };
        walk(walk, from);
    }

    // A record this tree held that the walk no longer finds keeps its identity
    // - a rule object the page still holds must keep answering - and answers
    // null for `ownerNode` until the owner is found again.
    for (const std::unique_ptr<css_sheet_record> & record : css_sheets_) {
        if (record->tree == from) { record->attached = false; }
    }

    std::vector<value> ordered;
    if (order != nullptr) {
        order->clear();
        css_preferred_title_.clear();
        for (const found_sheet & each : found) {
            if (!each.title.empty()) {
                css_preferred_title_ = each.title;
                break;
            }
        }
    }
    for (const found_sheet & each : found) {
        const std::uint64_t key = pack(each.owner);
        const auto it = css_sheet_by_owner_.find(key);
        bool fresh = false;
        std::size_t at = no_index;
        if (it == css_sheet_by_owner_.end() || it->second >= css_sheets_.size()) {
            css_sheets_.push_back(std::make_unique<css_sheet_record>());
            at = css_sheets_.size() - 1;
            css_sheets_[at]->owner = each.owner;
            css_sheet_by_owner_[key] = at;
            fresh = true;
        } else {
            at = it->second;
        }
        css_sheet_record & record = *css_sheets_[at];
        record.tree = from;
        record.attached = true;
        record.title = each.title;
        // THE ATTRIBUTE IS RE-READ, THE LIST IS NOT. This walk runs on every
        // read of `document.styleSheets`, and a MediaList is mutable - a page
        // that has called `appendMedium` must not have it undone by the next
        // property access. So the query list is re-derived only when the
        // element's `media` attribute has actually changed under it.
        if (fresh || record.media != each.media) {
            record.media = each.media;
            record.media_queries = parse_media_query_list(record.media);
        }
        if (each.linked) {
            // A `<link>`'s bytes come from the asset registry, exactly as
            // load_author_styles resolves them, and only when the href changes -
            // this walk runs on every read of `document.styleSheets` and
            // re-reading a file each time would be a load per property access.
            if (fresh || record.href != each.href) {
                record.href = each.href;
                // ORIGIN-CLEAN, CSSOM 6.3: a sheet fetched from another origin
                // keeps its rules to itself. Only an absolute http(s) href can
                // be cross-origin - a relative one, a data: URL and a
                // constructed sheet are the document's own - and the origin
                // is the URL's tuple, compared the way `location.origin`
                // reports it.
                record.origin_clean =
                    !parse_absolute(record.href).valid ||
                    location_parts(record.href).origin == location_parts(location_href_).origin;
                std::string text;
                if (assets_ != nullptr) {
                    const std::vector<std::byte> bytes = assets_->load(record.href);
                    text.assign(reinterpret_cast<const char *>(bytes.data()), bytes.size());
                }
                record.source = std::move(text);
                parse_sheet_rules(at, record.source);
            }
        } else if (fresh || record.source != each.text) {
            // EDITING A `<style>` REPLACES ITS SHEET, which is what the source
            // comparison is for. An insertRule does NOT change the element's
            // text, so the CSSOM's own mutations survive this.
            record.source = each.text;
            parse_sheet_rules(at, record.source);
        }
        if (order != nullptr) { order->push_back(at); }
        ordered.push_back(sheet_object_for(cx, at));
    }
    set_sheet_list(list_obj, ordered);
}

void dom_bindings::install_style_sheets(context & cx) {
    install_stylesheet_prototypes(cx);
    script::object_object * doc = document_object();
    if (doc == nullptr) { return; }
    // `document.styleSheets` - [SameObject], and LIVE ENOUGH: the list object
    // keeps its identity forever and its contents are re-derived from the DOM on
    // every read, which is what makes a `<style>` a script appended a moment ago
    // show up without an invalidation hook.
    doc->define_accessor(
        "styleSheets",
        value::object(cx.allocate<script::native_object>("get styleSheets",
                                                         [this](context & c, std::span<value>) {
                                                             sync_style_sheets(c);
                                                             return style_sheet_list(c);
                                                         })),
        value::undefined(), script::attr_configurable);
    doc->define_accessor("adoptedStyleSheets",
                         value::object(cx.allocate<script::native_object>(
                             "get adoptedStyleSheets",
                             [this](context & c, std::span<value>) {
                                 script::object_object * internals = cssom_internals(c);
                                 if (internals == nullptr) { return value::undefined(); }
                                 if (const value * held = internals->find("adopted")) {
                                     return *held;
                                 }
                                 const value made = c.make_array();
                                 internals->set("adopted", made);
                                 return made;
                             })),
                         value::object(cx.allocate<script::native_object>(
                             "set adoptedStyleSheets",
                             [this](context & c, std::span<value> a) {
                                 script::object_object * internals = cssom_internals(c);
                                 if (internals == nullptr) { return value::undefined(); }
                                 const value made = adopted_sheets_array(c, a);
                                 if (made.is_undefined()) { return value::undefined(); }
                                 internals->set("adopted", made);
                                 style_sheets_changed();
                                 return value::undefined();
                             })),
                         script::attr_configurable);
}

// The array `adoptedStyleSheets = [...]` stores, on the document or on a
// shadow root: a fresh array of the given sheets, or undefined after throwing
// for one that was not constructed.
value dom_bindings::adopted_sheets_array(context & cx, std::span<value> args) {
    const value made = cx.make_array();
    auto * items = static_cast<script::array_object *>(made.as_heap());
    if (!args.empty() && args[0].is_array()) {
        auto * given = static_cast<script::array_object *>(args[0].as_heap());
        for (const value each : given->items) {
            const std::size_t at = slot_index(as_object(each), sheet_key);
            // "Only sheets constructed in this document may be adopted", which
            // is the one check this can make and the one `adoptedstylesheets-*`
            // asserts.
            if (at >= css_sheets_.size() || !css_sheets_[at]->constructed) {
                throw_dom_exception(cx, "NotAllowedError",
                                    "only a constructed CSSStyleSheet may be adopted");
                return value::undefined();
            }
            items->items.push_back(each);
        }
    }
    return made;
}

} // namespace ctbrowser::shell
