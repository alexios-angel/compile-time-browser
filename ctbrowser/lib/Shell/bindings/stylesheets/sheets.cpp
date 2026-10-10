//===- sheets.cpp - CSSStyleSheet objects -----------------------*- C++ -*-===//
//
// The CSSOM's sheet lists: the document's own sheets, collected lazily from
// the DOM and synced to `document.styleSheets`; a shadow root's sheets synced
// the same way onto its own list; `adoptedStyleSheets`; the author style text
// handed to the cascade; and the test, shared with the cascade, for which
// `<link>`s are stylesheets.
//
//===----------------------------------------------------------------------===//

#include "internal.hpp"

#include <ctbrowser/shell/net/url.hpp>

namespace ctbrowser::shell {

using namespace detail;

/// Classifies a <link> as an active, alternate or non-stylesheet. Shared with
/// browser::collect_author_styles so the cascade and the CSSOM agree on which
/// links are stylesheets. `rel` is a space-separated token list compared ASCII
/// case-insensitively.
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
    return std::ranges::find(enabled_links_, id.key()) != enabled_links_.end();
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
    // Document sheets are collected lazily (on a read of `document.styleSheets`),
    // so sync first; otherwise a page that never read that list would serialise
    // its adopted sheets but not its <style> elements.
    if (cx_ != nullptr) { sync_style_sheets(*cx_); }
    std::string out;
    // An `@import` is emitted as its imported sheet's rules, wrapped in the
    // import's media query when it has one, because the cascade's sheet parser
    // drops the `@import` line itself. The depth cap of 16 guards against the
    // import cycles load_imported_sheet already refuses.
    const auto emit = [&](auto && self, std::size_t at, std::size_t depth) -> void {
        if (at >= css_sheets_.size() || depth > 16) { return; }
        const std::unique_ptr<css_sheet_record> & sheet = css_sheets_[at];
        if (sheet->disabled) { return; }
        // An alternative style sheet set (titled, and not the preferred one)
        // does not apply unless its link was explicitly enabled.
        if (depth == 0 && !sheet->title.empty() && sheet->title != css_preferred_title_ &&
            !link_explicitly_enabled(sheet->owner)) {
            return;
        }
        // The sheet's own media list (the `media` attribute, as the CSSOM
        // holds it) gates every rule in it (CSSOM §5.1), just as
        // browser::collect_author_styles wraps the attribute.
        const std::string sheet_media = serialize_media_query_list(sheet->media_queries);
        if (!sheet_media.empty()) { out += "@media " + sheet_media + " {\n"; }
        for (const std::size_t rule : sheet->rules) {
            if (rule >= css_rule_store_.size()) { continue; }
            const css_rule_record & record = *css_rule_store_[rule];
            if (record.type == import_rule && record.imported_sheet != no_index) {
                // The import's `layer(x)`, `supports()` and `scope()` conditions
                // also wrap its rules (CSS Cascade 5 §5.1); `prelude` keeps them
                // as written.
                const std::string media = serialize_media_query_list(record.media_queries);
                std::string closers;
                if (!media.empty()) {
                    out += "@media " + media + " {\n";
                    closers += "}\n";
                }
                std::size_t at = 0;
                for (;;) {
                    const std::string_view part = next_component(record.prelude, at);
                    if (part.empty()) { break; }
                    if (ascii_iequals(part.substr(0, 9), "supports(") && part.back() == ')') {
                        out +=
                            "@supports (" + std::string{part.substr(9, part.size() - 10)} + ") {\n";
                        closers += "}\n";
                    } else if (ascii_iequals(part.substr(0, 6), "layer(") && part.back() == ')') {
                        out += "@layer " + std::string{part.substr(6, part.size() - 7)} + " {\n";
                        closers += "}\n";
                    } else if (ascii_iequals(part, "layer")) {
                        out += "@layer {\n";
                        closers += "}\n";
                    } else if (ascii_iequals(part.substr(0, 6), "scope(") && part.back() == ')') {
                        // The `@scope` block `scope()` stands for, as
                        // browser::collect_author_styles splices it.
                        out += "@scope " +
                               style::css::import_scope_prelude(part.substr(6, part.size() - 7)) +
                               " {\n";
                        closers += "}\n";
                    }
                }
                self(self, record.imported_sheet, depth + 1);
                out += closers;
                continue;
            }
            out += rule_css_text(record);
            out += '\n';
        }
        if (!sheet_media.empty()) { out += "}\n"; }
    };
    // Emits one sheet, preceded by the root of any implicit `@scope` in it (its
    // owner node's parent), written as browser::collect_author_styles writes it
    // (css::scope_block) and only ahead of a sheet that contains `@scope`. A
    // sheet with no owner, such as an adopted one, names root 0 so it does not
    // inherit the root the previous sheet named.
    const auto emit_rooted = [&](std::size_t at) {
        const std::size_t from = out.size();
        emit(emit, at, 0);
        if (ascii_lower_copy(std::string_view{out}.substr(from)).find("@scope") ==
            std::string::npos) {
            return;
        }
        const node_id owner = at < css_sheets_.size() ? css_sheets_[at]->owner : node_id{};
        const std::uint64_t root = owner ? style::engine::key_of(doc_->read().parent(owner)) : 0;
        out.insert(from, "@" + std::string{style::css::scope_root_at_rule} + " " +
                             std::to_string(root) + ";\n");
    };
    for (const std::size_t at : css_document_sheets_) { emit_rooted(at); }
    // Adopted sheets come last, in array order, as CSSOM's final list of style
    // sheets requires (WPT adoptedstylesheets-cascade-order.html checks the
    // order). They are read from the page-owned array itself rather than a
    // mirror, so in-place mutations are seen.
    if (script::object_object * internals = object_of(cssom_internals_)) {
        if (const value * held = internals->find("adopted"); held != nullptr && held->is_array()) {
            for (const value each : static_cast<script::array_object *>(held->as_heap())->items) {
                emit_rooted(slot_index(object_of(each), sheet_key));
            }
        }
    }
    return out;
}

void dom_bindings::style_sheets_changed() {
    ++style_generation_;
    if (on_author_styles_) { on_author_styles_(author_style_text()); }
    // Marked dirty either way. With a hook, the browser has already replaced
    // the author sheet and this schedules the restyle. Without one, the frame
    // is unchanged and this costs one recomposite, which is cheap for an
    // operation a page performs a handful of times.
    mutated();
}

// --- The document's sheets, re-derived --------------------------------------

script::object_object * dom_bindings::cssom_internals(context & cx) {
    if (script::object_object * held = object_of(cssom_internals_)) { return held; }
    script::object_object * doc = document_object();
    if (doc == nullptr) { return nullptr; }
    if (const value * found = doc->find(internals_key)) {
        cssom_internals_ = *found;
        return object_of(cssom_internals_);
    }
    cssom_internals_ = cx.make_object();
    // Stored on the document as a non-configurable property: the collector
    // already marks the document (register_roots) and traces its properties, so
    // everything the CSSOM holds is rooted without registering a separate GC
    // root, and a page cannot delete it.
    doc->define(internals_key, cssom_internals_, script::attr_none);
    // A frame's CSSOM is its own, made the first time anything asks for it
    // (most often its `<style>`'s sheet). A frame is built with install_document
    // alone (frames.cpp), and every CSSOM method captures the bindings that
    // installed it, so a frame's sheet under the page's CSSStyleSheet.prototype
    // would read the page's record store at the frame's index. The frame
    // therefore installs the interfaces over its own records, in its own realm,
    // which also makes `sheet.rules[0].constructor` the frame's CSSStyleRule
    // (WPT insertRule-across-context).
    // ponytail: installed lazily, because nothing in this directory runs when a
    // frame is made; until a frame's CSSOM is first touched, its `CSSStyleSheet`
    // is still the page's and `document.styleSheets` is absent. Calling
    // install_style_sheets from install_frame_realm would fix that.
    if (realm_ != nullptr && primary_ != nullptr) {
        const auto in_frame = enter_realm(cx);
        install_style_sheets(cx);
    }
    return object_of(cssom_internals_);
}

/// `document.styleSheets`, kept live through a proxy. The target under
/// `internals.list` holds the prototype, `length` and the indices that
/// sync_style_sheets writes; the page holds a proxy over it whose every read
/// re-derives the document's sheets first, so `length` on a list held across a
/// DOM change is the current count (WPT ttwf-cssom-doc-ext-load-count).
/// `length` stays a data property on the target because the VM's iteration
/// reads it as one, and a proxy is array-like through its traps
/// (context::iterable_values), as every other live DOM collection is.
/// [SameObject]: one proxy, cached beside the target as `list_view`.
value dom_bindings::style_sheet_list(context & cx) {
    script::object_object * internals = cssom_internals(cx);
    if (internals == nullptr) { return value::undefined(); }
    if (const value * found = internals->find("list_view")) { return *found; }
    const value target = cx.make_object();
    if (script::object_object * obj = object_of(target)) {
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

/// Re-derives a sheet list from its tree before `item()` reads it: the
/// document's, or the shadow root's that the list carries in `tree_key`.
void dom_bindings::resync_sheet_list(context & cx, script::object_object & list) {
    if (const value * tree = list.find(tree_key)) {
        if (const node_id root = handle_of(*tree)) { sync_sheet_list(cx, root, list, nullptr); }
        return;
    }
    sync_style_sheets(cx);
}

namespace {

/// Returns the cached JS object for record `index` in cache `table` on the
/// internals object (one plain object each for sheets and rules, keyed by
/// record index), creating it with `make` on first use. The caches are
/// reachable from the document, so they need no GC root of their own.
[[nodiscard]] value cached_object(context & cx, script::object_object & internals,
                                  std::string_view table, std::size_t index,
                                  const std::function<value()> & make) {
    script::object_object * held = nullptr;
    if (const value * found = internals.find(table)) { held = object_of(*found); }
    if (held == nullptr) {
        const value made = cx.make_object();
        internals.set(std::string{table}, made);
        held = object_of(made);
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
    script::object_object * list_obj = target != nullptr ? object_of(*target) : nullptr;
    if (list_obj == nullptr) { return; }
    const node_id root = doc_->read().root();
    sync_sheet_list(cx, root, *list_obj, &css_document_sheets_);
}

void dom_bindings::sync_sheet_list(context & cx, node_id from, script::object_object & list_obj,
                                   std::vector<std::size_t> * order) {
    // Collect the sheets the DOM holds now, with the same walk and link test
    // browser::collect_author_styles uses.
    struct found_sheet {
        node_id owner;
        bool linked = false;
        std::string href;
        std::string title;
        std::string media;
        std::string text;
        // The <style>'s child node ids: HTML §4.2.6 re-creates the sheet on any
        // child change, so an appended empty text node drops the rules
        // insertRule added even though the text is unchanged (WPT
        // css-style-reparse).
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
            // HTML elements only, for the reason load_author_styles gives: an
            // SVG <style> interns to the same atom but is not a document
            // stylesheet.
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
                            made.children += std::to_string(child.key()) + ',';
                        }
                    }
                    found.push_back(std::move(made));
                }
            }
            for (const node_id child : txn.children(at)) { self(self, child); }
        };
        walk(walk, from);
    }

    // A record this tree held that the walk no longer finds keeps its identity,
    // because a rule object the page still holds must keep answering; it
    // answers null for `ownerNode` until the owner is found again.
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
                if (css_preferred_title_.empty() || each.owner.key() < earliest) {
                    css_preferred_title_ = each.title;
                    earliest = each.owner.key();
                }
            }
        }
    }
    for (const found_sheet & each : found) {
        const std::uint64_t key = each.owner.key();
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
        // Re-derive the media query list only when the `media` attribute has
        // changed. This walk runs on every read of `document.styleSheets`, and a
        // MediaList is mutable: a page's `appendMedium` must not be undone by the
        // next property access.
        if (fresh || record.media != each.media) {
            record.media = each.media;
            record.media_queries = parse_media_query_list(record.media);
        }
        if (each.linked) {
            // A `<link>`'s bytes come from the asset registry, as
            // load_author_styles resolves them, and are reloaded only when the
            // href changes, since this walk runs on every read of
            // `document.styleSheets`.
            if (fresh || record.href != each.href) {
                record.href = each.href;
                // Origin-clean flag (CSSOM 6.3): a sheet fetched from another
                // origin hides its rules. Only an absolute http(s) href can be
                // cross-origin (a relative one, a data: URL and a constructed
                // sheet are the document's own); the origin is the URL's tuple,
                // compared the way `location.origin` reports it. An http(s)
                // href that does not parse is treated as cross-origin.
                const bool remote = ascii_istarts_with(record.href, "http://") ||
                                    ascii_istarts_with(record.href, "https://");
                record.origin_clean = !remote || (parse_absolute(record.href).valid &&
                                                  location_parts(record.href).origin ==
                                                      location_parts(location_href_).origin);
                std::string text;
                if (assets_ != nullptr) {
                    const std::vector<std::byte> bytes = assets_->load(record.href);
                    text = style::css::decode_stylesheet(
                        {reinterpret_cast<const char *>(bytes.data()), bytes.size()}, {},
                        doc_->encoding());
                }
                record.source = std::move(text);
                parse_sheet_rules(at, record.source);
            }
        } else if (fresh || record.source != each.text || record.children != each.children) {
            // Editing a `<style>` replaces its sheet, which is what the source
            // and child-list comparisons detect. insertRule does not change the
            // element's children, so the CSSOM's own mutations survive this.
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
    // `document.styleSheets` is [SameObject] and effectively live: the list
    // object keeps its identity and its contents are re-derived from the DOM on
    // every read, so a `<style>` a script just appended shows up without an
    // invalidation hook.
    doc->define_accessor(
        "styleSheets",
        value::object(cx.allocate<script::native_object>("get styleSheets",
                                                         [this](context & c, std::span<value>) {
                                                             sync_style_sheets(c);
                                                             return style_sheet_list(c);
                                                         })),
        value::undefined(), script::attr_configurable);
    // An observable array (CSSOM §6.2, Web IDL ObservableArray): the page sees
    // a proxy over the array author_style_text walks, so an in-place `push`,
    // `pop`, `splice` or `reverse` is checked as the setter checks (only a
    // constructed sheet may be adopted) and triggers a restyle. Assigning
    // `= [...]` replaces the array's contents, not the array object.
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
                        const std::size_t at = slot_index(object_of(args[2]), sheet_key);
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

/// Builds the array an `adoptedStyleSheets = [...]` assignment stores, on the
/// document or on a shadow root: a fresh array of the given sheets, or
/// undefined after throwing for one that was not constructed.
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
            const std::size_t at = slot_index(object_of(each), sheet_key);
            // Only constructed sheets may be adopted (checked by WPT
            // adoptedstylesheets-*). Which document constructed the sheet is
            // not checked.
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
