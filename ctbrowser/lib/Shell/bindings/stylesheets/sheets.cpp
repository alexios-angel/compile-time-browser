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

std::string_view dom_bindings::preferred_sheet_title() {
    if (css_preferred_title_.empty() && cx_ != nullptr) { sync_style_sheets(*cx_); }
    return css_preferred_title_;
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
    ++style_generation_;
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

// `document.styleSheets`: LIVE, THROUGH A PROXY. The target under
// `internals.list` holds the prototype, `length` and the indices that
// sync_style_sheets writes; what the page holds is a proxy over it whose every
// read re-derives the document's sheets first, so `length` on a list held in
// a variable across a DOM change is the count NOW
// (ttwf-cssom-doc-ext-load-count removes a <link> and reads the list it took
// before). `length` itself stays a data property on the target, because the
// VM's iteration reads it as one - and a proxy is array-like through its traps
// (context::iterable_values), which is how every other live DOM collection
// already iterates. [SameObject]: one proxy, kept beside the target.
value dom_bindings::style_sheet_list(context & cx) {
    script::object_object * internals = cssom_internals(cx);
    if (internals == nullptr) { return value::undefined(); }
    if (const value * found = internals->find("list_view")) { return *found; }
    const value target = cx.make_object();
    if (script::object_object * obj = as_object(target)) {
        if (const value * proto = internals->find("StyleSheetList.prototype")) {
            obj->prototype = *proto;
        }
        obj->define("length", value::number(0), script::attr_none);
    }
    internals->set("list", target);
    auto * handler = cx.allocate<script::object_object>();
    set_method(cx, *handler, "get", [this](context & c, std::span<value> args) {
        if (args.size() < 2) { return value::undefined(); }
        sync_style_sheets(c);
        return c.lookup_property(args[0], c.to_string(args[1]));
    });
    set_method(cx, *handler, "has", [this](context & c, std::span<value> args) {
        if (args.size() < 2) { return value::boolean(false); }
        sync_style_sheets(c);
        return value::boolean(c.has_property(args[0], args[1]));
    });
    set_method(cx, *handler, "getOwnPropertyDescriptor",
               [this](context & c, std::span<value> args) {
                   if (args.size() < 2) { return value::undefined(); }
                   sync_style_sheets(c);
                   script::context::property_descriptor found;
                   if (!c.own_property(args[0], c.to_string(args[1]), found)) {
                       return value::undefined();
                   }
                   return c.from_property_descriptor(found);
               });
    // An index and `length` are read-only; anything else is an expando.
    set_method(cx, *handler, "set", [](context & c, std::span<value> args) {
        if (args.size() < 3 || !args[0].is_object()) { return value::boolean(false); }
        const std::string key = c.to_string(args[1]);
        if (key == "length" ||
            (!key.empty() && key.find_first_not_of("0123456789") == std::string::npos)) {
            return value::boolean(false);
        }
        c.store_property(args[0], key, args[2]);
        return value::boolean(true);
    });
    const value view =
        value::object(cx.allocate<script::proxy_object>(target, value::object(handler)));
    internals->set("list_view", view);
    return view;
}

// `item()` re-derives the list from the tree first - the document's, or the
// shadow root's the list carries in `tree_key`.
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
    (void)style_sheet_list(cx);
    script::object_object * internals = cssom_internals(cx);
    const value * target = internals != nullptr ? internals->find("list") : nullptr;
    script::object_object * list_obj = target != nullptr ? as_object(*target) : nullptr;
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
        // The <style>'s child nodes, as ids: HTML §4.2.6 re-creates the sheet
        // on ANY child change, and css-style-reparse asks that an appended
        // empty text node drop the rules insertRule added, text or no text.
        std::string children;
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
                            made.children += std::to_string(pack(child)) + ',';
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
        if (css_preferred_title_.empty()) {
            std::uint64_t earliest = 0;
            for (const found_sheet & each : found) {
                if (each.title.empty()) { continue; }
                if (css_preferred_title_.empty() || pack(each.owner) < earliest) {
                    css_preferred_title_ = each.title;
                    earliest = pack(each.owner);
                }
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
                // An http(s) href that does not even parse is not this
                // document's origin either.
                const bool remote = ascii_istarts_with(record.href, "http://") ||
                                    ascii_istarts_with(record.href, "https://");
                record.origin_clean = !remote || (parse_absolute(record.href).valid &&
                                                  location_parts(record.href).origin ==
                                                      location_parts(location_href_).origin);
                std::string text;
                if (assets_ != nullptr) {
                    const std::vector<std::byte> bytes = assets_->load(record.href);
                    text.assign(reinterpret_cast<const char *>(bytes.data()), bytes.size());
                }
                record.source = std::move(text);
                parse_sheet_rules(at, record.source);
            }
        } else if (fresh || record.source != each.text || record.children != each.children) {
            // EDITING A `<style>` REPLACES ITS SHEET, which is what the source
            // and child-list comparisons are for. An insertRule does NOT change
            // the element's children, so the CSSOM's own mutations survive this.
            record.source = each.text;
            record.children = each.children;
            parse_sheet_rules(at, record.source);
        }
        if (order != nullptr) { order->push_back(at); }
        ordered.push_back(sheet_object_for(cx, at));
    }
    set_indexed(list_obj, ordered);
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
    // AN OBSERVABLE ARRAY, CSSOM §6.2 through Web IDL's ObservableArray: the
    // page sees a proxy over the array `author_style_text` walks, so an
    // in-place `push`, `pop`, `splice` or `reverse` is checked the way the
    // setter checks - only a constructed sheet may be adopted - and restyles.
    // The array keeps its identity across `= [...]`, which REPLACES its
    // contents rather than the object.
    doc->define_accessor(
        "adoptedStyleSheets",
        value::object(cx.allocate<script::native_object>(
            "get adoptedStyleSheets",
            [this](context & c, std::span<value>) {
                script::object_object * internals = cssom_internals(c);
                if (internals == nullptr) { return value::undefined(); }
                if (const value * held = internals->find("adopted_view")) { return *held; }
                const value target = c.make_array();
                internals->set("adopted", target);
                auto * handler = c.allocate<script::object_object>();
                set_method(c, *handler, "set", [this](context & cx2, std::span<value> args) {
                    if (args.size() < 3) { return value::boolean(false); }
                    const std::string key = cx2.to_string(args[1]);
                    const bool index =
                        !key.empty() && key.find_first_not_of("0123456789") == std::string::npos;
                    if (index) {
                        const std::size_t at = slot_index(as_object(args[2]), sheet_key);
                        if (!args[2].is_object() || at == no_index) {
                            cx2.throw_error("TypeError", "adoptedStyleSheets takes CSSStyleSheets");
                            return value::boolean(false);
                        }
                        if (at >= css_sheets_.size() || !css_sheets_[at]->constructed) {
                            throw_dom_exception(cx2, "NotAllowedError",
                                                "only a constructed CSSStyleSheet may be adopted");
                            return value::boolean(false);
                        }
                    }
                    cx2.store_property(args[0], key, args[2]);
                    style_sheets_changed();
                    return value::boolean(true);
                });
                set_method(
                    c, *handler, "deleteProperty", [this](context & cx2, std::span<value> args) {
                        if (args.size() < 2) { return value::boolean(false); }
                        const bool gone = cx2.delete_own_property(args[0], cx2.to_string(args[1]));
                        style_sheets_changed();
                        return value::boolean(gone);
                    });
                const value view =
                    value::object(c.allocate<script::proxy_object>(target, value::object(handler)));
                internals->set("adopted_view", view);
                return view;
            })),
        value::object(cx.allocate<script::native_object>(
            "set adoptedStyleSheets",
            [this](context & c, std::span<value> a) {
                script::object_object * internals = cssom_internals(c);
                if (internals == nullptr) { return value::undefined(); }
                const value made = adopted_sheets_array(c, a);
                if (made.is_undefined()) { return value::undefined(); }
                // Through the getter, so the array and its view exist, then the
                // contents are replaced in place.
                (void)c.lookup_property(value::object(document_object()), "adoptedStyleSheets");
                const value * held = internals->find("adopted");
                if (held == nullptr || !held->is_array()) { return value::undefined(); }
                static_cast<script::array_object *>(held->as_heap())->items =
                    static_cast<script::array_object *>(made.as_heap())->items;
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
    // `= document.adoptedStyleSheets` hands back the observable view itself.
    value from = args.empty() ? value::undefined() : args[0];
    if (from.is_kind(script::heap_kind::proxy)) {
        from = static_cast<script::proxy_object *>(from.as_heap())->target;
    }
    if (from.is_array()) {
        auto * given = static_cast<script::array_object *>(from.as_heap());
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
