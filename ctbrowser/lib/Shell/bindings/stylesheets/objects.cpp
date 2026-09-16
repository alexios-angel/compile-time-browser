// dom_bindings' CSSOM - the objects: a sheet, a rule, a rule list, a media list
// and a declaration block, made once and refreshed in place.

#include "internal.hpp"

namespace ctbrowser::shell {

using namespace detail;

// `shadowRoot.styleSheets` - the StyleSheetList of one shadow tree, held on
// the root's wrapper so it is [SameObject] for as long as the wrapper is, and
// re-derived from the tree on every read exactly as the document's is.
value dom_bindings::shadow_sheet_list(context & cx, node_id root) {
    if (!root || shadow_tree_of(root) == nullptr) { return value::undefined(); }
    script::object_object * wrapper = as_object(wrap(cx, root));
    if (wrapper == nullptr) { return value::undefined(); }
    value list = value::undefined();
    if (const value * held = wrapper->find(rules_key)) {
        list = *held;
    } else {
        list = cx.make_object();
        script::object_object * obj = as_object(list);
        if (obj == nullptr) { return value::undefined(); }
        if (script::object_object * internals = cssom_internals(cx)) {
            if (const value * proto = internals->find("StyleSheetList.prototype")) {
                obj->prototype = *proto;
            }
        }
        obj->define("length", value::number(0), script::attr_none);
        obj->define(tree_key, value::object(wrapper), script::attr_none);
        wrapper->define(rules_key, list, script::attr_none);
    }
    if (script::object_object * obj = as_object(list)) { sync_sheet_list(cx, root, *obj, nullptr); }
    return list;
}

value dom_bindings::sheet_object_of(context & cx, node_id owner) {
    sync_style_sheets(cx);
    // A `<style>` IN A SHADOW TREE is that tree's sheet: the document walk
    // never reaches the fragment, so the tree's own list is synced instead.
    {
        node_id root{};
        {
            const auto txn = doc_->read();
            root = root_of_tree(txn, owner, false);
        }
        if (root && root != owner && shadow_tree_of(root) != nullptr) {
            (void)shadow_sheet_list(cx, root);
        }
    }
    const auto it = css_sheet_by_owner_.find(owner.key());
    if (it == css_sheet_by_owner_.end() || it->second >= css_sheets_.size() ||
        !css_sheets_[it->second]->attached) {
        return value::null();
    }
    return sheet_object_for(cx, it->second);
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
    // ONE OBJECT PER RECORD, for as long as the record is. insertRule must not
    // rebuild the ones it did not touch: `css/cssom/CSSStyleSheet.html` puts an
    // expando on two rules, inserts a third between them, and asserts both
    // expandos survived.
    std::vector<value> ordered;
    for (const std::size_t rule : rules) { ordered.push_back(rule_object_for(cx, rule)); }
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
        } else if (record.type == font_feature_values_rule) {
            interface = "CSSFontFeatureValuesRule.prototype";
        } else if (record.at_name == "container") {
            interface = "CSSContainerRule.prototype";
        } else if (record.at_name == "layer") {
            interface = "CSSLayerBlockRule.prototype";
        } else if (record.at_name == "layer-statement") {
            interface = "CSSLayerStatementRule.prototype";
        } else if (record.at_name == "scope") {
            interface = "CSSScopeRule.prototype";
        } else if (record.at_name == "starting-style") {
            interface = "CSSStartingStyleRule.prototype";
        } else if (record.at_name == nested_declarations_name) {
            interface = "CSSNestedDeclarations.prototype";
        }
        if (const value * proto = internals->find(interface)) { obj->prototype = *proto; }
    }
    obj->define(rule_key, value::number(static_cast<double>(rule)), script::attr_none);
    obj->define(sheet_key, value::number(static_cast<double>(css_rule_store_[rule]->sheet)),
                script::attr_none);
    if (css_rule_store_[rule]->type == keyframes_rule) {
        // Indexed from the start - `keyframes[0]` is read without `cssRules`
        // ever having been - and the list is the cached one so the two agree.
        obj->define(rules_key, make_rule_list(cx, css_rule_store_[rule]->children),
                    script::attr_none);
        mirror_rule_list(*obj);
    }
    return object;
}

value dom_bindings::declaration_object(context & cx, std::size_t rule) {
    const value object = cx.make_object();
    script::object_object * obj = as_object(object);
    if (obj == nullptr) { return object; }
    if (script::object_object * internals = cssom_internals(cx)) {
        // WHICH CSSStyleDeclaration. CSSOM splits the property accessors off
        // onto CSSStyleProperties, and a descriptor block carries its
        // descriptors instead: `@font-face`'s `src`, `@page`'s `size`. So a
        // page rule's block has no `cssFloat` and a style rule's has no
        // `unicodeRange`, which page-descriptors.html and
        // cssstyledeclaration-cssfontrule.html assert from the two sides.
        std::string_view interface = "CSSStyleProperties.prototype";
        if (rule < css_rule_store_.size()) {
            if (css_rule_store_[rule]->type == font_face_rule) {
                interface = "CSSFontFaceDescriptors.prototype";
            } else if (css_rule_store_[rule]->type == page_rule) {
                interface = "CSSPageDescriptors.prototype";
            }
        }
        if (const value * proto = internals->find(interface)) { obj->prototype = *proto; }
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
    //
    // ON THE PROTOTYPE, once, and not on each wrapper: it is an IDL attribute
    // of HTMLStyleElement and HTMLLinkElement, and `assert_idl_attribute`
    // asks for it in the prototype chain and not as an own property. The
    // receiver names the node; a wrapper is only made after the interfaces
    // are, so the fallback below is for an embedder that built none.
    bool installed = false;
    for (const std::string_view name : {"HTMLStyleElement", "HTMLLinkElement"}) {
        script::object_object * proto = as_object(interface_prototype(name));
        if (proto == nullptr) { continue; }
        installed = true;
        if (proto->find_accessor("sheet") != nullptr) { continue; }
        define_getter(
            cx, *proto, "sheet",
            [this](context & c, std::span<value>) {
                return sheet_object_of(c, handle_of(c.current_this()));
            },
            {}, script::attr_configurable);
        if (name == "HTMLStyleElement") {
            // `HTMLStyleElement.disabled` is the SHEET's flag, not an attribute:
            // false while the element has no sheet, and a write then does
            // nothing (style-sheet-interfaces-001, "disabled attribute
            // getter/setter").
            const auto owned = [this](context & c) -> css_sheet_record * {
                sync_style_sheets(c);
                const auto it = css_sheet_by_owner_.find(handle_of(c.current_this()).key());
                if (it == css_sheet_by_owner_.end() || it->second >= css_sheets_.size()) {
                    return nullptr;
                }
                css_sheet_record * sheet = css_sheets_[it->second].get();
                return sheet->attached ? sheet : nullptr;
            };
            proto->define_accessor("disabled",
                                   value::object(cx.allocate<script::native_object>(
                                       "get disabled",
                                       [owned](context & c, std::span<value>) {
                                           const css_sheet_record * sheet = owned(c);
                                           return value::boolean(sheet != nullptr &&
                                                                 sheet->disabled);
                                       })),
                                   value::object(cx.allocate<script::native_object>(
                                       "set disabled",
                                       [this, owned](context & c, std::span<value> a) {
                                           css_sheet_record * sheet = owned(c);
                                           const bool wanted = !a.empty() && context::truthy(a[0]);
                                           if (sheet != nullptr && sheet->disabled != wanted) {
                                               sheet->disabled = wanted;
                                               style_sheets_changed();
                                           }
                                           return value::undefined();
                                       })),
                                   script::attr_configurable);
            continue;
        }
        // `HTMLLinkElement.disabled`, HTML 4.2.4: it reflects the attribute,
        // and REMOVING the attribute is what sets the element's "explicitly
        // enabled" flag - the one thing that makes an `alternate stylesheet`
        // apply. Setting false on a link that has no attribute is therefore
        // a no-op, which HTMLLinkElement-disabled-006 asserts by name.
        // ponytail: only this setter sets the flag; `removeAttribute('disabled')`
        // does not. Hook the attribute mutation funnel if a page needs it.
        proto->define_accessor("disabled",
                               value::object(cx.allocate<script::native_object>(
                                   "get disabled",
                                   [this](context & c, std::span<value>) {
                                       const node_id id = handle_of(c.current_this());
                                       if (!id) { return value::boolean(false); }
                                       const auto txn = doc_->read();
                                       return value::boolean(
                                           txn.has_attribute(id, atoms_->intern_lower("disabled")));
                                   })),
                               value::object(cx.allocate<script::native_object>(
                                   "set disabled",
                                   [this](context & c, std::span<value> a) {
                                       const node_id id = handle_of(c.current_this());
                                       if (!id) { return value::undefined(); }
                                       const bool wanted = !a.empty() && context::truthy(a[0]);
                                       const atom name = atoms_->intern_lower("disabled");
                                       const bool present = doc_->read().has_attribute(id, name);
                                       if (wanted == present) { return value::undefined(); }
                                       if (wanted) {
                                           (void)doc_->set_attribute(id, name, "");
                                       } else {
                                           (void)doc_->remove_attribute(id, name);
                                           if (!link_explicitly_enabled(id)) {
                                               enabled_links_.push_back(id.key());
                                           }
                                       }
                                       mutated();
                                       return value::undefined();
                                   })),
                               script::attr_configurable);
    }
    // A ShadowRoot's `styleSheets` and `adoptedStyleSheets` - DocumentOrShadowRoot,
    // the same two members the document has, over the shadow tree alone.
    if (script::object_object * proto = as_object(interface_prototype("ShadowRoot"));
        proto != nullptr && proto->find_accessor("styleSheets") == nullptr) {
        define_getter(
            cx, *proto, "styleSheets",
            [this](context & c, std::span<value>) {
                return shadow_sheet_list(c, handle_of(c.current_this()));
            },
            {}, script::attr_configurable);
        proto->define_accessor("adoptedStyleSheets",
                               value::object(cx.allocate<script::native_object>(
                                   "get adoptedStyleSheets",
                                   [](context & c, std::span<value>) {
                                       script::object_object * self = as_object(c.current_this());
                                       if (self == nullptr) { return value::undefined(); }
                                       if (const value * held = self->find("__ctbrowser_adopted")) {
                                           return *held;
                                       }
                                       const value made = c.make_array();
                                       self->define("__ctbrowser_adopted", made, script::attr_none);
                                       return made;
                                   })),
                               value::object(cx.allocate<script::native_object>(
                                   "set adoptedStyleSheets",
                                   [this](context & c, std::span<value> a) {
                                       script::object_object * self = as_object(c.current_this());
                                       if (self == nullptr) { return value::undefined(); }
                                       const value made = adopted_sheets_array(c, a);
                                       if (!made.is_undefined()) {
                                           self->define("__ctbrowser_adopted", made,
                                                        script::attr_none);
                                       }
                                       return value::undefined();
                                   })),
                               script::attr_configurable);
    }
    if (installed) { return; }
    obj.define_accessor(
        "sheet",
        value::object(cx.allocate<script::native_object>(
            "get sheet",
            [this, id](context & c, std::span<value>) { return sheet_object_of(c, id); })),
        value::undefined(), script::attr_configurable);
}

} // namespace ctbrowser::shell
