#include "helpers.hpp"

#include <ctbrowser/shell/net/url.hpp>

namespace ctbrowser::shell {

using namespace detail;

void dom_bindings::install_stylesheet_declarations(context & cx,
                                                   script::object_object * internals) {
    // --- CSSKeyframesRule and CSSKeyframeRule
    script::object_object * keyframes_proto =
        cssom_interface(cx, internals, "CSSKeyframesRule", "CSSRule", nullptr);
    define_getter(
        cx, *keyframes_proto, "name",
        [this](context & c, std::span<value>) {
            const css_rule_record * rule = receiver_rule(c);
            return c.string(rule == nullptr ? std::string{} : rule->prelude);
        },
        [this](context & c, std::span<value> a) {
            css_rule_record * rule = receiver_rule(c);
            if (rule == nullptr) { return value::undefined(); }
            rule->prelude = collapse_whitespace(arg_string(c, a, 0), html_whitespace);
            style_sheets_changed();
            return value::undefined();
        },
        script::attr_configurable);
    // The list is made with the rule object (make_rule_object), because the
    // rule is itself indexed - `keyframes[0]` - and MIRRORS it after each read
    // and each write.
    const auto keyframes_list = [this](context & c) {
        script::object_object * self = as_object(c.current_this());
        const css_rule_record * rule = receiver_rule(c);
        if (self == nullptr || rule == nullptr) { return value::undefined(); }
        if (const value * held = self->find(rules_key)) {
            refresh_rule_list(c, *held, rule->children);
        } else {
            self->define(rules_key, make_rule_list(c, rule->children), script::attr_none);
        }
        mirror_rule_list(*self);
        return *self->find(rules_key);
    };
    define_getter(
        cx, *keyframes_proto, "cssRules",
        [keyframes_list](context & c, std::span<value>) { return keyframes_list(c); }, {},
        script::attr_configurable);
    define_getter(
        cx, *keyframes_proto, "length",
        [this](context & c, std::span<value>) {
            const css_rule_record * rule = receiver_rule(c);
            return value::number(rule == nullptr ? 0 : static_cast<double>(rule->children.size()));
        },
        {}, script::attr_configurable);
    // `appendRule` takes a whole keyframe and `deleteRule`/`findRule` take a
    // keyText - NOT an index, which is what makes this trio different from
    // every other insert/delete pair in the CSSOM.
    set_method(
        cx, *keyframes_proto, "appendRule",
        [this, keyframes_list](context & c, std::span<value> a) {
            css_rule_record * rule = receiver_rule(c);
            if (rule == nullptr) { return value::undefined(); }
            const std::size_t self = slot_index(as_object(c.current_this()), rule_key);
            std::string error;
            const std::size_t made =
                parse_one_rule(rule->sheet, "@keyframes _ {" + arg_string(c, a, 0) + "}", error);
            if (made == no_index || css_rule_store_[made]->children.empty()) {
                return value::undefined();
            }
            const std::size_t frame = css_rule_store_[made]->children.front();
            css_rule_store_[frame]->parent = self;
            rule->children.push_back(frame);
            (void)keyframes_list(c);
            style_sheets_changed();
            return value::undefined();
        },
        script::attr_builtin);
    const auto keyframe_at = [this](context & c, const std::string & key) -> std::size_t {
        const css_rule_record * rule = receiver_rule(c);
        if (rule == nullptr) { return no_index; }
        for (std::size_t i = rule->children.size(); i > 0; --i) {
            const std::size_t child = rule->children[i - 1];
            if (child < css_rule_store_.size() && css_rule_store_[child]->selector == key) {
                return i - 1;
            }
        }
        return no_index;
    };
    set_method(
        cx, *keyframes_proto, "findRule",
        [this, keyframe_at](context & c, std::span<value> a) {
            css_rule_record * rule = receiver_rule(c);
            // "Return the LAST rule that matches", which is why the search above
            // walks backwards: a keyframes rule may name the same key twice.
            const std::size_t found =
                keyframe_at(c, collapse_whitespace(arg_string(c, a, 0), html_whitespace));
            if (rule == nullptr || found == no_index) { return value::null(); }
            return rule_object_for(c, rule->children[found]);
        },
        script::attr_builtin);
    set_method(
        cx, *keyframes_proto, "deleteRule",
        [this, keyframe_at, keyframes_list](context & c, std::span<value> a) {
            css_rule_record * rule = receiver_rule(c);
            const std::size_t found =
                keyframe_at(c, collapse_whitespace(arg_string(c, a, 0), html_whitespace));
            if (rule == nullptr || found == no_index) { return value::undefined(); }
            detach_rule(css_rule_store_, rule->children[found]);
            rule->children.erase(rule->children.begin() + static_cast<std::ptrdiff_t>(found));
            (void)keyframes_list(c);
            style_sheets_changed();
            return value::undefined();
        },
        script::attr_builtin);
    script::object_object * keyframe_proto =
        cssom_interface(cx, internals, "CSSKeyframeRule", "CSSRule", nullptr);
    declaration_accessor(cx, keyframe_proto);
    define_getter(
        cx, *keyframe_proto, "keyText",
        [this](context & c, std::span<value>) {
            const css_rule_record * rule = receiver_rule(c);
            return c.string(rule == nullptr ? std::string{} : rule->selector);
        },
        [this](context & c, std::span<value> a) {
            css_rule_record * rule = receiver_rule(c);
            if (rule == nullptr) { return value::undefined(); }
            rule->selector = collapse_whitespace(arg_string(c, a, 0), html_whitespace);
            style_sheets_changed();
            return value::undefined();
        },
        script::attr_configurable);
    declaration_accessor(cx,
                         cssom_interface(cx, internals, "CSSCounterStyleRule", "CSSRule", nullptr));

    // --- CSSFontFeatureValuesRule, CSS Fonts 4 §11.2
    //
    // Seven maplike views over one rule's feature list, each object carrying
    // the rule's slot and its feature type; `set` takes a number or a sequence
    // of numbers, as the IDL's `(unsigned long or sequence<unsigned long>)`.
    script::object_object * feature_map_proto =
        cssom_interface(cx, internals, "CSSFontFeatureValuesMap", nullptr, nullptr);
    static constexpr std::string_view feature_key = "__ctbrowser_feature";
    const auto feature_type = [](context & c) {
        script::object_object * self = as_object(c.current_this());
        const value * held = self == nullptr ? nullptr : self->find(feature_key);
        return held == nullptr ? std::string{} : c.to_string(*held);
    };
    const auto feature_entries =
        [this, feature_type](context & c) -> std::pair<css_rule_record *, std::string> {
        return {receiver_rule(c), feature_type(c)};
    };
    define_getter(
        cx, *feature_map_proto, "size",
        [feature_entries](context & c, std::span<value>) {
            const auto [rule, type] = feature_entries(c);
            double n = 0;
            if (rule != nullptr) {
                for (const css_rule_record::feature_value & f : rule->features) {
                    if (f.type == type) { n += 1; }
                }
            }
            return value::number(n);
        },
        {}, script::attr_configurable);
    const auto find_feature = [](css_rule_record & rule, const std::string & type,
                                 const std::string & name) {
        return std::find_if(rule.features.begin(), rule.features.end(),
                            [&](const css_rule_record::feature_value & f) {
                                return f.type == type && f.name == name;
                            });
    };
    set_method(
        cx, *feature_map_proto, "get",
        [feature_entries, find_feature](context & c, std::span<value> a) {
            const auto [rule, type] = feature_entries(c);
            if (rule == nullptr) { return value::undefined(); }
            const auto it = find_feature(*rule, type, arg_string(c, a, 0));
            if (it == rule->features.end()) { return value::undefined(); }
            const value made = c.make_array();
            auto * items = static_cast<script::array_object *>(made.as_heap());
            for (const double n : it->numbers) { items->items.push_back(value::number(n)); }
            return made;
        },
        script::attr_builtin);
    set_method(
        cx, *feature_map_proto, "has",
        [feature_entries, find_feature](context & c, std::span<value> a) {
            const auto [rule, type] = feature_entries(c);
            return value::boolean(rule != nullptr &&
                                  find_feature(*rule, type, arg_string(c, a, 0)) !=
                                      rule->features.end());
        },
        script::attr_builtin);
    set_method(
        cx, *feature_map_proto, "set",
        [this, feature_entries, find_feature](context & c, std::span<value> a) {
            const auto [rule, type] = feature_entries(c);
            if (rule == nullptr || a.size() < 2) { return c.current_this(); }
            css_rule_record::feature_value entry;
            entry.type = type;
            entry.name = arg_string(c, a, 0);
            if (a[1].is_array()) {
                for (const value each :
                     static_cast<script::array_object *>(a[1].as_heap())->items) {
                    entry.numbers.push_back(context::to_number(each));
                }
            } else {
                entry.numbers.push_back(context::to_number(a[1]));
            }
            const auto it = find_feature(*rule, type, entry.name);
            if (it == rule->features.end()) {
                rule->features.push_back(std::move(entry));
            } else {
                it->numbers = std::move(entry.numbers);
            }
            style_sheets_changed();
            return c.current_this();
        },
        script::attr_builtin);
    set_method(
        cx, *feature_map_proto, "delete",
        [this, feature_entries, find_feature](context & c, std::span<value> a) {
            const auto [rule, type] = feature_entries(c);
            if (rule == nullptr) { return value::boolean(false); }
            const auto it = find_feature(*rule, type, arg_string(c, a, 0));
            if (it == rule->features.end()) { return value::boolean(false); }
            rule->features.erase(it);
            style_sheets_changed();
            return value::boolean(true);
        },
        script::attr_builtin);
    set_method(
        cx, *feature_map_proto, "clear",
        [this, feature_entries](context & c, std::span<value>) {
            const auto [rule, type] = feature_entries(c);
            if (rule == nullptr) { return value::undefined(); }
            std::erase_if(rule->features,
                          [&](const css_rule_record::feature_value & f) { return f.type == type; });
            style_sheets_changed();
            return value::undefined();
        },
        script::attr_builtin);
    script::object_object * feature_values_proto =
        cssom_interface(cx, internals, "CSSFontFeatureValuesRule", "CSSRule", nullptr);
    define_getter(
        cx, *feature_values_proto, "fontFamily",
        [this](context & c, std::span<value>) {
            const css_rule_record * rule = receiver_rule(c);
            return c.string(rule == nullptr ? std::string{} : rule->prelude);
        },
        [this](context & c, std::span<value> a) {
            css_rule_record * rule = receiver_rule(c);
            if (rule == nullptr) { return value::undefined(); }
            rule->prelude = style::css::serialize_font_family(arg_string(c, a, 0));
            style_sheets_changed();
            return value::undefined();
        },
        script::attr_configurable);
    for (const auto & [idl, type] :
         {std::pair{"annotation", "annotation"}, std::pair{"characterVariant", "character-variant"},
          std::pair{"historicalForms", "historical-forms"}, std::pair{"ornaments", "ornaments"},
          std::pair{"styleset", "styleset"}, std::pair{"stylistic", "stylistic"},
          std::pair{"swash", "swash"}}) {
        const std::string type_name{type};
        define_getter(
            cx, *feature_values_proto, idl,
            [type_name, feature_map_proto](context & c, std::span<value>) {
                script::object_object * self = as_object(c.current_this());
                const std::size_t at = slot_index(self, rule_key);
                const value made = c.make_object();
                script::object_object * obj = as_object(made);
                if (obj == nullptr) { return made; }
                obj->prototype = value::object(feature_map_proto);
                obj->define(rule_key, value::number(static_cast<double>(at)), script::attr_none);
                obj->define(feature_key, c.string(type_name), script::attr_none);
                return made;
            },
            {}, script::attr_configurable);
    }

    // --- CSSStyleDeclaration, and the three blocks that inherit it
    //
    // ONE PROTOTYPE PER KIND OF BLOCK FOR THE WHOLE PAGE, carrying an accessor
    // per property under both spellings. `el.style` is a PROXY over a store,
    // which is the other way to answer an unbounded property set - it costs two
    // natives per element and makes `el.style instanceof CSSStyleDeclaration`
    // false, a proxy not being an object as far as the prototype walk is
    // concerned. Here the set is bounded (the property table IS the set of IDL
    // attributes a CSSStyleProperties has), so ~290 accessors on one shared
    // prototype answer every rule in the document and `instanceof` works.
    //
    // CSSStyleDeclaration itself carries the generic API - cssText, item,
    // getPropertyValue and the rest - and the property accessors sit on
    // CSSStyleProperties beneath it, exactly as CSSOM draws the split; the
    // descriptor blocks of `@font-face` and `@page` are its other two
    // subclasses and carry their descriptors instead. objects.cpp picks.
    script::object_object * declaration_proto =
        cssom_interface(cx, internals, "CSSStyleDeclaration", nullptr, nullptr);
    cssom_iterable(cx, declaration_proto);
    const auto property_accessor = [&](script::object_object * on, const std::string & idl,
                                       const std::string & css) {
        on->define_accessor(idl,
                            value::object(cx.allocate<script::native_object>(
                                "get " + idl,
                                [this, css](context & c, std::span<value>) {
                                    const css_rule_record * rule = receiver_rule(c);
                                    if (rule == nullptr) { return c.string(""); }
                                    return c.string(
                                        style::css::declaration_value(rule->declarations, css));
                                })),
                            value::object(cx.allocate<script::native_object>(
                                "set " + idl,
                                [this, css](context & c, std::span<value> a) {
                                    css_rule_record * rule = receiver_rule(c);
                                    if (rule == nullptr) { return value::undefined(); }
                                    if (store_declaration(*rule, css, arg_string(c, a, 0), false)) {
                                        refresh_declaration_object(c, c.current_this());
                                        style_sheets_changed();
                                    }
                                    return value::undefined();
                                })),
                            script::attr_enumerable | script::attr_configurable);
    };
    const auto both_spellings = [&](script::object_object * on, std::string_view name) {
        const std::string css{name};
        property_accessor(on, css, css);
        const std::string idl = idl_name_of(css);
        if (idl != css) { property_accessor(on, idl, css); }
    };
    script::object_object * properties_proto =
        cssom_interface(cx, internals, "CSSStyleProperties", "CSSStyleDeclaration", nullptr);
    for (const style::css::property_syntax & property : known_properties()) {
        both_spellings(properties_proto, property.name);
    }
    // CSS Fonts 4 §11.1 and CSS Paged Media 3 §7.1: the descriptor sets, by
    // name - the property table knows none of them, so a value is kept as the
    // author wrote it, which is what a descriptor's grammar this engine does
    // not model amounts to.
    script::object_object * font_face_proto =
        cssom_interface(cx, internals, "CSSFontFaceDescriptors", "CSSStyleDeclaration", nullptr);
    for (const std::string_view name :
         {"ascent-override", "descent-override", "font-display", "font-family",
          "font-feature-settings", "font-language-override", "font-named-instance", "font-stretch",
          "font-style", "font-weight", "font-width", "font-variation-settings", "line-gap-override",
          "size-adjust", "src", "unicode-range"}) {
        both_spellings(font_face_proto, name);
    }
    script::object_object * page_descriptors_proto =
        cssom_interface(cx, internals, "CSSPageDescriptors", "CSSStyleDeclaration", nullptr);
    for (const std::string_view name :
         {"margin", "margin-top", "margin-right", "margin-bottom", "margin-left", "size",
          "page-orientation", "marks", "bleed"}) {
        both_spellings(page_descriptors_proto, name);
    }
    define_getter(
        cx, *declaration_proto, "length",
        [this](context & c, std::span<value>) {
            const css_rule_record * rule = receiver_rule(c);
            return value::number(rule == nullptr ? 0
                                                 : static_cast<double>(rule->declarations.size()));
        },
        {}, script::attr_configurable);
    define_getter(
        cx, *declaration_proto, "parentRule",
        [this](context & c, std::span<value>) {
            script::object_object * self = as_object(c.current_this());
            const std::size_t at = slot_index(self, rule_key);
            if (at >= css_rule_store_.size()) { return value::null(); }
            return rule_object_for(c, at);
        },
        {}, script::attr_configurable);
    define_getter(
        cx, *declaration_proto, "cssText",
        [this](context & c, std::span<value>) {
            const css_rule_record * rule = receiver_rule(c);
            return c.string(rule == nullptr
                                ? std::string{}
                                : style::css::serialize_declaration_block(rule->declarations));
        },
        [this](context & c, std::span<value> a) {
            css_rule_record * rule = receiver_rule(c);
            if (rule == nullptr) { return value::undefined(); }
            // A WHOLE-BLOCK REPLACEMENT, not a merge, and through the same
            // declaration-list parser a `style` attribute goes through - so a
            // `;` inside a string cannot end a declaration here either.
            rule->declarations.clear();
            parse_declarations_into(*rule, arg_string(c, a, 0));
            refresh_declaration_object(c, c.current_this());
            style_sheets_changed();
            return value::undefined();
        },
        script::attr_configurable);
    set_method(
        cx, *declaration_proto, "item",
        [](context & c, std::span<value> a) {
            const value held = collection_item(c, a);
            // CSSOM's `item` answers the EMPTY STRING past the end here, unlike the
            // collections above whose `item` answers null.
            return held.is_null() ? c.string("") : held;
        },
        script::attr_builtin);
    set_method(
        cx, *declaration_proto, "getPropertyValue",
        [this](context & c, std::span<value> a) {
            const css_rule_record * rule = receiver_rule(c);
            if (rule == nullptr) { return c.string(""); }
            return c.string(style::css::declaration_value(rule->declarations, asked_name(c, a)));
        },
        script::attr_builtin);
    set_method(
        cx, *declaration_proto, "getPropertyPriority",
        [this](context & c, std::span<value> a) {
            const css_rule_record * rule = receiver_rule(c);
            if (rule == nullptr) { return c.string(""); }
            return c.string(style::css::declaration_priority(rule->declarations, asked_name(c, a)));
        },
        script::attr_builtin);
    set_method(
        cx, *declaration_proto, "setProperty",
        [this](context & c, std::span<value> a) {
            css_rule_record * rule = receiver_rule(c);
            if (rule == nullptr) { return value::undefined(); }
            // [LegacyNullToEmptyString] on both `value` and `priority`, and
            // `priority` is optional: null is "", and an undefined priority is the
            // default "" rather than the string "undefined" - which was refusing
            // the whole call. An undefined VALUE is the string "undefined", which
            // no grammar accepts, so it is a no-op by a different route.
            const auto text = [&](std::size_t i) {
                return i < a.size() && !a[i].is_null() ? c.to_string(a[i]) : std::string{};
            };
            const std::string priority =
                a.size() > 2 && a[2].is_undefined() ? std::string{} : text(2);
            // "If priority is not the empty string and is not an ASCII
            // case-insensitive match for 'important', return" - CSSOM 6.7.2.
            if (!priority.empty() && !ascii_iequals(priority, "important")) {
                return value::undefined();
            }
            if (store_declaration(*rule, asked_name(c, a), text(1), !priority.empty())) {
                refresh_declaration_object(c, c.current_this());
                style_sheets_changed();
            }
            return value::undefined();
        },
        script::attr_builtin);
    set_method(
        cx, *declaration_proto, "removeProperty",
        [this](context & c, std::span<value> a) {
            css_rule_record * rule = receiver_rule(c);
            if (rule == nullptr) { return c.string(""); }
            bool removed = false;
            const std::string was =
                style::css::remove_declaration(rule->declarations, asked_name(c, a), removed);
            if (removed) {
                refresh_declaration_object(c, c.current_this());
                style_sheets_changed();
            }
            return c.string(was);
        },
        script::attr_builtin);
}

} // namespace ctbrowser::shell
