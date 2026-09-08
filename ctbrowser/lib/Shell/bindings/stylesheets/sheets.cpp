// dom_bindings' CSSOM - the sheet list: the document's own sheets collected
// lazily, synced to document.styleSheets, and installed on the document.
//
// One of six files carved out of a 2,814-line bindings/stylesheets.cpp on
// 2026-09-08. The member functions belong to one class declared in
// include/ctbrowser/shell/bindings.hpp; the helpers more than one of these
// files needs are declared in internal.hpp beside this and defined in
// serialize.cpp and source.cpp. Nothing about the public header changed.

#include "internal.hpp"

namespace ctbrowser::shell {

using namespace detail;

namespace {

// A SECOND COPY OF browser.cpp's rel test, and it has to be one: that function
// is in an anonymous namespace in a file this rung does not own, and the CSSOM
// answering a different question from the cascade about which links are
// stylesheets is exactly the disagreement this file exists to avoid. `rel` is a
// space-separated token list matched ASCII case-insensitively, and `alternate
// stylesheet` is NOT one - it is user-selectable and a browser leaves it
// disabled.
[[nodiscard]] bool rel_is_stylesheet(std::string_view rel) {
    bool stylesheet = false;
    std::size_t at = 0;
    while (at < rel.size()) {
        const std::size_t start = rel.find_first_not_of(html_whitespace, at);
        if (start == std::string_view::npos) { break; }
        std::size_t end = rel.find_first_of(html_whitespace, start);
        if (end == std::string_view::npos) { end = rel.size(); }
        const std::string_view token = rel.substr(start, end - start);
        if (ascii_iequals(token, "alternate")) { return false; }
        if (ascii_iequals(token, "stylesheet")) { stylesheet = true; }
        at = end;
    }
    return stylesheet;
}

} // namespace

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
    const auto emit = [&](std::size_t at) {
        if (at >= css_sheets_.size()) { return; }
        const std::unique_ptr<css_sheet_record> & sheet = css_sheets_[at];
        if (sheet->disabled) { return; }
        for (const std::size_t rule : sheet->rules) {
            if (rule >= css_rule_store_.size()) { continue; }
            out += rule_css_text(*css_rule_store_[rule]);
            out += '\n';
        }
    };
    for (const std::size_t at : css_document_sheets_) { emit(at); }
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
                emit(slot_index(as_object(each), sheet_key));
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
        obj->define("length", value::number(0), script::attr_none);
    }
    internals->set("list", list);
    return list;
}

void dom_bindings::sync_style_sheets(context & cx) {
    script::object_object * internals = cssom_internals(cx);
    if (internals == nullptr) { return; }
    const value list = style_sheet_list(cx);
    script::object_object * list_obj = as_object(list);
    if (list_obj == nullptr) { return; }

    // WHAT THE DOM SAYS NOW - the same walk browser::load_author_styles makes.
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
        const auto walk = [&](auto && self, node_id at) -> void {
            // HTML ONLY, for both, and for the reason load_author_styles gives:
            // an SVG carries its own <style>, it interns to the same atom, and
            // it is not a document stylesheet.
            if (txn.element_ns(at) == ctbrowser::node_ns::html) {
                const atom tag = txn.tag(at).value_or(atom{});
                const bool is_style = tag == style_tag;
                const bool is_link =
                    tag == link_tag && rel_is_stylesheet(txn.attribute_value(at, rel_attribute));
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
        walk(walk, txn.root());
    }

    // The sheet objects the list already holds, so a record that survives keeps
    // its JavaScript identity - and with it whatever the page put on it.
    std::vector<std::pair<std::size_t, value>> existing;
    {
        std::size_t was = 0;
        if (const value * held = list_obj->find("length"); held != nullptr && held->is_number()) {
            const double count = held->as_number();
            if (count > 0) { was = static_cast<std::size_t>(count); }
        }
        for (std::size_t i = 0; i < was; ++i) {
            const value * held = list_obj->find(std::to_string(i));
            if (held == nullptr) { continue; }
            existing.emplace_back(slot_index(as_object(*held), sheet_key), *held);
        }
    }

    flat_map<std::uint64_t, std::size_t> by_owner;
    std::vector<value> ordered;
    css_document_sheets_.clear();
    for (const found_sheet & each : found) {
        const std::uint64_t key = pack(each.owner);
        const auto it = css_sheet_by_owner_.find(key);
        bool fresh = false;
        std::size_t at = no_index;
        if (it == css_sheet_by_owner_.end() || it->second >= css_sheets_.size()) {
            css_sheets_.push_back(std::make_unique<css_sheet_record>());
            at = css_sheets_.size() - 1;
            css_sheets_[at]->owner = each.owner;
            fresh = true;
        } else {
            at = it->second;
        }
        css_sheet_record & record = *css_sheets_[at];
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
        by_owner.emplace(key, at);
        css_document_sheets_.push_back(at);
        value object = value::undefined();
        for (const auto & [index, held] : existing) {
            if (index == at) {
                object = held;
                break;
            }
        }
        if (object.is_undefined()) { object = make_sheet_object(cx, at); }
        ordered.push_back(object);
    }
    css_sheet_by_owner_ = std::move(by_owner);
    set_indexed(*list_obj, ordered);
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
    doc->define_accessor(
        "adoptedStyleSheets",
        value::object(cx.allocate<script::native_object>("get adoptedStyleSheets",
                                                         [this](context & c, std::span<value>) {
                                                             script::object_object * internals =
                                                                 cssom_internals(c);
                                                             if (internals == nullptr) {
                                                                 return value::undefined();
                                                             }
                                                             if (const value * held =
                                                                     internals->find("adopted")) {
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
                const value made = c.make_array();
                auto * items = static_cast<script::array_object *>(made.as_heap());
                if (!a.empty() && a[0].is_array()) {
                    auto * given = static_cast<script::array_object *>(a[0].as_heap());
                    for (const value each : given->items) {
                        const std::size_t at = slot_index(as_object(each), sheet_key);
                        // "Only sheets constructed in this document may be
                        // adopted", which is the one check this can make and the
                        // one `adoptedstylesheets-*` asserts.
                        if (at >= css_sheets_.size() || !css_sheets_[at]->constructed) {
                            throw_dom_exception(c, "NotAllowedError",
                                                "only a constructed CSSStyleSheet may be adopted");
                            return value::undefined();
                        }
                        items->items.push_back(each);
                    }
                }
                internals->set("adopted", made);
                style_sheets_changed();
                return value::undefined();
            })),
        script::attr_configurable);
}

} // namespace ctbrowser::shell
