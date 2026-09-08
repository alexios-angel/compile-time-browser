// dom_bindings' CSSOM - the objects: a sheet, a rule, a rule list, a media list
// and a declaration block, made once and refreshed in place.
//
// One of six files carved out of a 2,814-line bindings/stylesheets.cpp on
// 2026-09-08. The member functions belong to one class declared in
// include/ctbrowser/shell/bindings.hpp; the helpers more than one of these
// files needs are declared in internal.hpp beside this and defined in
// serialize.cpp and source.cpp. Nothing about the public header changed.

#include "internal.hpp"

namespace ctbrowser::shell {

using namespace detail;

value dom_bindings::sheet_object_of(context & cx, node_id owner) {
    sync_style_sheets(cx);
    const auto it = css_sheet_by_owner_.find(pack(owner));
    if (it == css_sheet_by_owner_.end()) { return value::null(); }
    script::object_object * list_obj = as_object(style_sheet_list(cx));
    if (list_obj == nullptr) { return value::null(); }
    std::size_t count = 0;
    if (const value * held = list_obj->find("length"); held != nullptr && held->is_number()) {
        const double n = held->as_number();
        if (n > 0) { count = static_cast<std::size_t>(n); }
    }
    for (std::size_t i = 0; i < count; ++i) {
        const value * held = list_obj->find(std::to_string(i));
        if (held == nullptr) { continue; }
        if (slot_index(as_object(*held), sheet_key) == it->second) { return *held; }
    }
    return value::null();
}

// --- the objects ------------------------------------------------------------

dom_bindings::css_sheet_record * dom_bindings::receiver_sheet(context & cx) {
    const std::size_t at = slot_index(as_object(cx.current_this()), sheet_key);
    return at < css_sheets_.size() ? css_sheets_[at].get() : nullptr;
}

dom_bindings::css_rule_record * dom_bindings::receiver_rule(context & cx) {
    const std::size_t at = slot_index(as_object(cx.current_this()), rule_key);
    return at < css_rule_store_.size() ? css_rule_store_[at].get() : nullptr;
}

std::vector<std::string> * dom_bindings::receiver_media(context & cx) {
    // A RULE FIRST, because a media rule's own object carries both private slots
    // - `rule_key` and the `sheet_key` that says which sheet it came from - and
    // the sheet's list is not the rule's. A MediaList itself carries exactly one.
    if (css_rule_record * rule = receiver_rule(cx)) { return &rule->media_queries; }
    if (css_sheet_record * sheet = receiver_sheet(cx)) { return &sheet->media_queries; }
    return nullptr;
}

value dom_bindings::media_list_object(context & cx, script::object_object & owner) {
    // [SameObject]: `sheet.media === sheet.media`, and a page's expando on one
    // survives - so the list is cached on its owner under a private key and
    // REFRESHED rather than rebuilt.
    if (const value * held = owner.find(media_key)) {
        refresh_media_list(cx, *held);
        return *held;
    }
    const value list = cx.make_object();
    script::object_object * obj = as_object(list);
    if (obj == nullptr) { return list; }
    if (script::object_object * internals = cssom_internals(cx)) {
        if (const value * proto = internals->find("MediaList.prototype")) {
            obj->prototype = *proto;
        }
    }
    // THE OWNER'S SLOT, COPIED ONTO THE LIST. It is how every method below finds
    // the vector it is a view of: a MediaList has no owner pointer of its own,
    // and re-deriving one from the JS object graph would need a back-reference
    // the collector would then have to know about.
    if (const value * rule = owner.find(rule_key)) {
        obj->define(rule_key, *rule, script::attr_none);
    } else if (const value * sheet = owner.find(sheet_key)) {
        obj->define(sheet_key, *sheet, script::attr_none);
    }
    obj->define("length", value::number(0), script::attr_none);
    owner.define(media_key, list, script::attr_none);
    refresh_media_list(cx, list);
    return list;
}

void dom_bindings::refresh_media_list(context & cx, value list) {
    script::object_object * obj = as_object(list);
    if (obj == nullptr) { return; }
    const std::vector<std::string> * queries = nullptr;
    {
        const std::size_t rule = slot_index(obj, rule_key);
        const std::size_t sheet = slot_index(obj, sheet_key);
        if (rule < css_rule_store_.size()) {
            queries = &css_rule_store_[rule]->media_queries;
        } else if (sheet < css_sheets_.size()) {
            queries = &css_sheets_[sheet]->media_queries;
        }
    }
    if (queries == nullptr) { return; }
    std::vector<value> items;
    items.reserve(queries->size());
    for (const std::string & query : *queries) { items.push_back(cx.string(query)); }
    set_indexed(*obj, items);
}

value dom_bindings::make_rule_list(context & cx, std::span<const std::size_t> rules) {
    const value list = cx.make_object();
    script::object_object * obj = as_object(list);
    if (obj == nullptr) { return list; }
    if (script::object_object * internals = cssom_internals(cx)) {
        if (const value * proto = internals->find("CSSRuleList.prototype")) {
            obj->prototype = *proto;
        }
    }
    obj->define("length", value::number(0), script::attr_none);
    refresh_rule_list(cx, list, rules);
    return list;
}

void dom_bindings::refresh_rule_list(context & cx, value list, std::span<const std::size_t> rules) {
    script::object_object * obj = as_object(list);
    if (obj == nullptr) { return; }
    // The rule OBJECTS already in the list, by record index. insertRule must not
    // rebuild the ones it did not touch: `css/cssom/CSSStyleSheet.html` puts an
    // expando on two rules, inserts a third between them, and asserts both
    // expandos survived.
    std::vector<std::pair<std::size_t, value>> existing;
    std::size_t was = 0;
    if (const value * held = obj->find("length"); held != nullptr && held->is_number()) {
        const double count = held->as_number();
        if (count > 0) { was = static_cast<std::size_t>(count); }
    }
    for (std::size_t i = 0; i < was; ++i) {
        const value * held = obj->find(std::to_string(i));
        if (held == nullptr) { continue; }
        existing.emplace_back(slot_index(as_object(*held), rule_key), *held);
    }
    std::vector<value> ordered;
    for (const std::size_t rule : rules) {
        value object = value::undefined();
        for (const auto & [index, held] : existing) {
            if (index == rule) {
                object = held;
                break;
            }
        }
        if (object.is_undefined()) { object = make_rule_object(cx, rule); }
        ordered.push_back(object);
    }
    set_indexed(*obj, ordered);
}

value dom_bindings::make_rule_object(context & cx, std::size_t rule) {
    const value object = cx.make_object();
    script::object_object * obj = as_object(object);
    if (obj == nullptr || rule >= css_rule_store_.size()) { return object; }
    script::object_object * internals = cssom_internals(cx);
    if (internals != nullptr) {
        const css_rule_record & record = *css_rule_store_[rule];
        std::string_view interface = "CSSRule.prototype";
        if (record.type == style_rule) {
            interface = "CSSStyleRule.prototype";
        } else if (record.type == media_rule) {
            interface = "CSSMediaRule.prototype";
        } else if (record.type == font_face_rule) {
            interface = "CSSFontFaceRule.prototype";
        } else if (record.type == import_rule) {
            interface = "CSSImportRule.prototype";
        } else if (record.type == supports_rule) {
            interface = "CSSSupportsRule.prototype";
        } else if (record.type == page_rule) {
            interface = "CSSPageRule.prototype";
        } else if (record.type == keyframes_rule) {
            interface = "CSSKeyframesRule.prototype";
        } else if (record.type == keyframe_rule) {
            interface = "CSSKeyframeRule.prototype";
        } else if (record.type == namespace_rule) {
            interface = "CSSNamespaceRule.prototype";
        } else if (record.type == counter_style_rule) {
            interface = "CSSCounterStyleRule.prototype";
        }
        if (const value * proto = internals->find(interface)) { obj->prototype = *proto; }
    }
    obj->define(rule_key, value::number(static_cast<double>(rule)), script::attr_none);
    obj->define(sheet_key, value::number(static_cast<double>(css_rule_store_[rule]->sheet)),
                script::attr_none);
    return object;
}

value dom_bindings::declaration_object(context & cx, std::size_t rule) {
    const value object = cx.make_object();
    script::object_object * obj = as_object(object);
    if (obj == nullptr) { return object; }
    if (script::object_object * internals = cssom_internals(cx)) {
        if (const value * proto = internals->find("CSSStyleDeclaration.prototype")) {
            obj->prototype = *proto;
        }
    }
    obj->define(rule_key, value::number(static_cast<double>(rule)), script::attr_none);
    refresh_declaration_object(cx, object);
    return object;
}

void dom_bindings::refresh_declaration_object(context & cx, value declarations) {
    (void)cx;
    script::object_object * obj = as_object(declarations);
    if (obj == nullptr) { return; }
    const std::size_t at = slot_index(obj, rule_key);
    if (at >= css_rule_store_.size()) { return; }
    // AN INDEX NAMES A PROPERTY, not a value - CSSOM 6.7.1 - which is what
    // `[...style]` and every `for (const p of style)` iterates. These are data
    // properties refreshed on every write rather than accessors, because there
    // is no bound on how many a block may have and a prototype cannot carry an
    // accessor per index.
    std::vector<value> names;
    for (const css_declaration & declared : css_rule_store_[at]->declarations) {
        names.push_back(cx.string(declared.name));
    }
    set_indexed(*obj, names);
}

value dom_bindings::make_sheet_object(context & cx, std::size_t sheet) {
    const value object = cx.make_object();
    script::object_object * obj = as_object(object);
    if (obj == nullptr) { return object; }
    if (script::object_object * internals = cssom_internals(cx)) {
        if (const value * proto = internals->find("CSSStyleSheet.prototype")) {
            obj->prototype = *proto;
        }
    }
    obj->define(sheet_key, value::number(static_cast<double>(sheet)), script::attr_none);
    return object;
}

void dom_bindings::install_sheet_property(context & cx, script::object_object & obj, node_id id) {
    // `sheet`, the LinkStyle interface. An ACCESSOR rather than a property: a
    // `<style>` a script has just created and appended has no sheet until the
    // document is walked again, and `dom/events`' four animation tests do
    // exactly that - `document.head.appendChild(style); style.sheet.insertRule(...)`.
    obj.define_accessor(
        "sheet",
        value::object(cx.allocate<script::native_object>(
            "get sheet",
            [this, id](context & c, std::span<value>) { return sheet_object_of(c, id); })),
        value::undefined(), script::attr_configurable);
}

} // namespace ctbrowser::shell
