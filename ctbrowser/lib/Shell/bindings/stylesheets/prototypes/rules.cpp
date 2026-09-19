#include "helpers.hpp"

#include <ctbrowser/shell/net/url.hpp>

namespace ctbrowser::shell {

using namespace detail;

void dom_bindings::install_stylesheet_rules(context & cx, script::object_object * internals) {
    // --- CSSRule and its subclasses
    script::object_object * rule_proto =
        cssom_interface(cx, internals, "CSSRule", nullptr, nullptr);
    struct rule_constant {
        const char * name;
        std::uint32_t value;
    };
    static constexpr rule_constant rule_constants[] = {
        {"STYLE_RULE", style_rule},         {"CHARSET_RULE", 2},
        {"IMPORT_RULE", import_rule},       {"MEDIA_RULE", media_rule},
        {"FONT_FACE_RULE", font_face_rule}, {"PAGE_RULE", page_rule},
        {"KEYFRAMES_RULE", keyframes_rule}, {"KEYFRAME_RULE", 8},
        {"NAMESPACE_RULE", namespace_rule}, {"COUNTER_STYLE_RULE", counter_style_rule},
        {"SUPPORTS_RULE", supports_rule},   {"FONT_FEATURE_VALUES_RULE", 14}};
    for (const rule_constant & each : rule_constants) {
        const value held = value::number(static_cast<double>(each.value));
        rule_proto->define(each.name, held, script::attr_enumerable);
        if (const value * ctor = internals->find("CSSRule")) {
            if (auto * fn = static_cast<script::native_object *>(ctor->as_heap())) {
                fn->define(each.name, held, script::attr_enumerable);
            }
        }
    }
    define_getter(
        cx, *rule_proto, "type",
        [this](context & c, std::span<value>) {
            const css_rule_record * rule = receiver_rule(c);
            return value::number(rule == nullptr ? 0 : static_cast<double>(rule->type));
        },
        {}, script::attr_configurable);
    define_getter(
        cx, *rule_proto, "cssText",
        [this](context & c, std::span<value>) {
            const css_rule_record * rule = receiver_rule(c);
            return c.string(rule == nullptr ? std::string{} : rule_css_text(*rule));
        },
        {}, script::attr_configurable);
    define_getter(
        cx, *rule_proto, "parentRule",
        [this](context & c, std::span<value>) {
            const css_rule_record * rule = receiver_rule(c);
            if (rule == nullptr || rule->parent >= css_rule_store_.size()) { return value::null(); }
            return rule_object_for(c, rule->parent);
        },
        {}, script::attr_configurable);
    define_getter(
        cx, *rule_proto, "parentStyleSheet",
        [this](context & c, std::span<value>) {
            const css_rule_record * rule = receiver_rule(c);
            if (rule == nullptr || rule->sheet >= css_sheets_.size()) { return value::null(); }
            return sheet_object_for(c, rule->sheet);
        },
        {}, script::attr_configurable);

    // A STYLE RULE IS A GROUPING RULE, CSS Nesting 1 §5: its nested rules are
    // `cssRules`, with insertRule and deleteRule.
    script::object_object * grouping_proto =
        cssom_interface(cx, internals, "CSSGroupingRule", "CSSRule", nullptr);
    script::object_object * style_rule_proto =
        cssom_interface(cx, internals, "CSSStyleRule", "CSSGroupingRule", nullptr);
    define_getter(
        cx, *style_rule_proto, "selectorText",
        [this](context & c, std::span<value>) {
            const css_rule_record * rule = receiver_rule(c);
            return c.string(rule == nullptr ? std::string{} : rule->selector);
        },
        [this](context & c, std::span<value> a) {
            css_rule_record * rule = receiver_rule(c);
            if (rule == nullptr) { return value::undefined(); }
            // "Parse the given value; if it returns failure, do nothing" -
            // CSSOM 6.4.2, and the whole of that test is the difference between
            // a selector that is WRONG and one this engine merely cannot match.
            // `parse_selector_text` is the entry point that tells them apart,
            // and it is the same one `querySelector` uses: its out parameter is
            // a SYNTAX error, so `::gibberish` leaves the rule alone while
            // `::before` replaces it and simply matches nothing. It replaced a
            // probe that appended `{--ctbrowser-probe:1}` and ran the SHEET
            // parser, which could not tell the two apart at all and which a `{`
            // inside an attribute value could derail.
            const std::string text = arg_string(c, a, 0);
            bool bad = false;
            const std::vector<style::css::namespace_declaration> namespaces =
                sheet_namespaces(rule->sheet);
            // A NESTED rule's text is parsed as one - `&` and an implicit `&`.
            const std::size_t self = slot_index(as_object(c.current_this()), rule_key);
            const std::vector<style::compiled_selector> placeholder(1);
            const style::css::nesting_context nesting{placeholder, false};
            const bool nested = nested_in_style(css_rule_store_, self);
            const style::css::stylesheet parsed = style::css::parse_selector_text(
                text, *atoms_, bad, &namespaces, nested ? &nesting : nullptr);
            if (bad || parsed.selectors.empty()) { return value::undefined(); }
            rule->prelude = collapse_whitespace(text, html_whitespace);
            rule->selector = representable(parsed.selectors)
                                 ? serialize_selector_list(parsed.selectors, *atoms_, namespaces)
                                 : rule->prelude;
            style_sheets_changed();
            return value::undefined();
        },
        script::attr_configurable);
    // `.style`, ON EVERY RULE WHOSE BLOCK IS DECLARATIONS - which is five of
    // them and was one. CSSOM gives a CSSStyleDeclaration to CSSStyleRule,
    // CSSFontFaceRule, CSSPageRule, CSSKeyframeRule and CSSCounterStyleRule
    // alike; `css/cssom/property-accessors.html` reaches for
    // `document.styleSheets[0].cssRules[0].style` where rule zero is a
    // `@font-face`, and nine of its nine subtests died on `getPropertyValue is
    // undefined` rather than on anything it set out to test.

    declaration_accessor(cx, style_rule_proto);

    // (CSSGroupingRule was made above CSSStyleRule, which inherits it.)
    define_getter(
        cx, *grouping_proto, "cssRules",
        [this](context & c, std::span<value>) {
            script::object_object * self = as_object(c.current_this());
            const css_rule_record * rule = receiver_rule(c);
            if (self == nullptr || rule == nullptr) { return value::undefined(); }
            if (const value * held = self->find(rules_key)) {
                refresh_rule_list(c, *held, rule->children);
                return *held;
            }
            const value list = make_rule_list(c, rule->children);
            self->define(rules_key, list, script::attr_none);
            return list;
        },
        {}, script::attr_configurable);
    // insertRule/deleteRule ON THE GROUP, which is the same pair of methods
    // CSSStyleSheet has and NOT the same list: a rule inserted here becomes a
    // child of the group and never a sibling of it. `@media print {}` followed
    // by `rule.insertRule(...)` is how `css/cssom/serialize-media-rule.html`
    // builds every one of its fixtures.
    set_method(
        cx, *grouping_proto, "insertRule",
        [this](context & c, std::span<value> args) {
            css_rule_record * rule = receiver_rule(c);
            if (rule == nullptr) { return value::undefined(); }
            if (args.empty()) {
                c.throw_error("TypeError", "insertRule requires a rule");
                return value::undefined();
            }
            const std::string text = c.to_string(args[0]);
            // `undefined` for an optional argument is the DEFAULT, not
            // `ToNumber(undefined)` - see the sheet's `insertRule`.
            const double asked =
                args.size() > 1 && !args[1].is_undefined() ? context::to_number(args[1]) : 0;
            // THE INDEX IS CHECKED BEFORE THE TEXT IS PARSED, which is the order
            // CSSOM 6.4.3 gives and which `CSSGroupingRule-insertRule.html` asserts
            // by passing a deliberate syntax error at an out-of-range index and
            // demanding the IndexSizeError.
            if (!(asked >= 0) || asked > static_cast<double>(rule->children.size())) {
                throw_dom_exception(c, "IndexSizeError", "the index is past the end of the rule");
                return value::undefined();
            }
            const std::size_t self = slot_index(as_object(c.current_this()), rule_key);
            std::string error;
            const std::size_t made = parse_one_rule(rule->sheet, text, error);
            if (made == no_index) {
                throw_dom_exception(c, error.empty() ? std::string{"SyntaxError"} : error,
                                    "the text is not a single CSS rule");
                return value::undefined();
            }
            // "If new rule cannot be inserted at index because the rule is not
            // allowed there, throw a HierarchyRequestError." `@import` and
            // `@namespace` are top-level rules and a grouping rule is not the top
            // level. The record is dropped rather than orphaned - it was appended a
            // moment ago and nothing else has seen it.
            const std::uint32_t kind = css_rule_store_[made]->type;
            if (kind == import_rule || kind == namespace_rule) {
                if (made + 1 == css_rule_store_.size()) { css_rule_store_.pop_back(); }
                throw_dom_exception(c, "HierarchyRequestError",
                                    "that rule is not allowed inside a grouping rule");
                return value::undefined();
            }
            css_rule_store_[made]->parent = self;
            rule->children.insert(rule->children.begin() + static_cast<std::ptrdiff_t>(asked),
                                  made);
            // Inside a style rule the new rule is nested - its selector is
            // spelled with `&`; outside one its bare declarations are dropped.
            if (nested_in_style(css_rule_store_, made)) {
                const std::vector<style::css::namespace_declaration> namespaces =
                    sheet_namespaces(rule->sheet);
                nest_rule_selector(css_rule_store_, *atoms_, namespaces, made);
            } else {
                prune_bare_declarations(css_rule_store_, made);
            }
            refresh_cached_rules(c, rule->children);
            style_sheets_changed();
            return value::number(asked);
        },
        script::attr_builtin);
    set_method(
        cx, *grouping_proto, "deleteRule",
        [this](context & c, std::span<value> args) {
            css_rule_record * rule = receiver_rule(c);
            if (rule == nullptr) { return value::undefined(); }
            if (args.empty()) {
                c.throw_error("TypeError", "deleteRule requires an index");
                return value::undefined();
            }
            const double asked = context::to_number(args[0]);
            if (!(asked >= 0) || asked >= static_cast<double>(rule->children.size())) {
                throw_dom_exception(c, "IndexSizeError", "there is no rule at that index");
                return value::undefined();
            }
            detach_rule(css_rule_store_, rule->children[static_cast<std::size_t>(asked)]);
            rule->children.erase(rule->children.begin() + static_cast<std::ptrdiff_t>(asked));
            refresh_cached_rules(c, rule->children);
            style_sheets_changed();
            return value::undefined();
        },
        script::attr_builtin);
    script::object_object * condition_proto =
        cssom_interface(cx, internals, "CSSConditionRule", "CSSGroupingRule", nullptr);
    define_getter(
        cx, *condition_proto, "conditionText",
        [this](context & c, std::span<value>) {
            const css_rule_record * rule = receiver_rule(c);
            if (rule == nullptr) { return c.string(""); }
            // "The conditionText of a CSSMediaRule is its media's mediaText" -
            // CSSOM 6.4.4. Only `@supports` answers from `prelude`, its condition
            // being a `<supports-condition>` and not a media query list.
            if (rule->type == media_rule) {
                return c.string(serialize_media_query_list(rule->media_queries));
            }
            return c.string(rule->prelude);
        },
        {}, script::attr_configurable);
    script::object_object * media_rule_proto =
        cssom_interface(cx, internals, "CSSMediaRule", "CSSConditionRule", nullptr);
    define_getter(
        cx, *media_rule_proto, "media",
        [this](context & c, std::span<value>) {
            script::object_object * self = as_object(c.current_this());
            if (self == nullptr || receiver_rule(c) == nullptr) { return value::undefined(); }
            return media_list_object(c, *self);
        },
        [](context & c, std::span<value> a) {
            const value list = c.lookup_property(c.current_this(), "media");
            c.store_property(list, "mediaText", a.empty() ? c.string("") : a[0]);
            return value::undefined();
        },
        script::attr_configurable);
    (void)cssom_interface(cx, internals, "CSSSupportsRule", "CSSConditionRule", nullptr);
    // --- CSSContainerRule, css-conditional-5. The prelude is `<container-name>?
    // <container-query>`: a name is an identifier and a query begins with `(`
    // (or `not`/`and`/`or` of one), so the first component decides which.
    script::object_object * container_proto =
        cssom_interface(cx, internals, "CSSContainerRule", "CSSConditionRule", nullptr);
    const auto container_part = [this](context & c, bool want_name) {
        const css_rule_record * rule = receiver_rule(c);
        if (rule == nullptr) { return c.string(""); }
        std::size_t at = 0;
        const std::string_view first = next_component(rule->prelude, at);
        const bool named = !first.empty() && first.front() != '(' && !ascii_iequals(first, "not") &&
                           !ascii_iequals(first, "and") && !ascii_iequals(first, "or");
        if (want_name) { return c.string(named ? std::string{first} : std::string{}); }
        const std::string_view whole = rule->prelude;
        std::string query = collapse_whitespace(named ? whole.substr(at) : whole, html_whitespace);
        // The one function a container condition has is spelled lowercase,
        // as cq-testcommon.js's feature probe reads it back.
        for (std::size_t k = 0; k + 6 <= query.size(); ++k) {
            if (ascii_iequals(std::string_view{query}.substr(k, 6), "style(")) {
                query.replace(k, 6, "style(");
            }
        }
        return c.string(query);
    };
    define_getter(
        cx, *container_proto, "containerName",
        [container_part](context & c, std::span<value>) { return container_part(c, true); }, {},
        script::attr_configurable);
    define_getter(
        cx, *container_proto, "containerQuery",
        [container_part](context & c, std::span<value>) { return container_part(c, false); }, {},
        script::attr_configurable);
    // --- CSSLayerBlockRule / CSSLayerStatementRule, CSS Cascade 5 §6.4.4:
    // the block's one name (empty for an anonymous layer) and the statement's
    // list, each name as written.
    script::object_object * layer_block_proto =
        cssom_interface(cx, internals, "CSSLayerBlockRule", "CSSGroupingRule", nullptr);
    define_getter(
        cx, *layer_block_proto, "name",
        [this](context & c, std::span<value>) {
            const css_rule_record * rule = receiver_rule(c);
            return c.string(rule == nullptr ? std::string{} : rule->prelude);
        },
        {}, script::attr_configurable);
    script::object_object * layer_statement_proto =
        cssom_interface(cx, internals, "CSSLayerStatementRule", "CSSRule", nullptr);
    define_getter(
        cx, *layer_statement_proto, "nameList",
        [this](context & c, std::span<value>) {
            const css_rule_record * rule = receiver_rule(c);
            const value names = c.make_array();
            if (rule != nullptr) {
                auto * items = static_cast<script::array_object *>(names.as_heap());
                for (const std::string_view part : split_on_commas(rule->prelude)) {
                    items->items.push_back(c.string(std::string{trim(part, html_whitespace)}));
                }
            }
            return names;
        },
        {}, script::attr_configurable);
    // --- CSSScopeRule, CSS Cascade 6 §3.5: `start` and `end` are the two
    // parenthesised selector lists of the prelude, or null when absent.
    script::object_object * scope_proto =
        cssom_interface(cx, internals, "CSSScopeRule", "CSSGroupingRule", nullptr);
    const auto scope_part = [this](context & c, bool want_end) {
        const css_rule_record * rule = receiver_rule(c);
        if (rule == nullptr) { return value::null(); }
        const std::string_view text = rule->prelude;
        std::size_t at = 0;
        std::string_view start;
        std::string_view end;
        if (!text.empty() && text.front() == '(') {
            const std::size_t close = scan_to(text, 1, ")");
            start = text.substr(1, close - 1);
            at = std::min(close + 1, text.size());
        }
        while (at < text.size() && html_whitespace.find(text[at]) != std::string_view::npos) {
            ++at;
        }
        if (text.compare(at, 2, "to") == 0) {
            const std::size_t open = text.find('(', at);
            if (open != std::string_view::npos) {
                const std::size_t close = scan_to(text, open + 1, ")");
                end = text.substr(open + 1, close - open - 1);
            }
        }
        const std::string_view part = want_end ? end : start;
        if (part.empty()) { return value::null(); }
        return c.string(collapse_whitespace(part, html_whitespace));
    };
    define_getter(
        cx, *scope_proto, "start",
        [scope_part](context & c, std::span<value>) { return scope_part(c, false); }, {},
        script::attr_configurable);
    define_getter(
        cx, *scope_proto, "end",
        [scope_part](context & c, std::span<value>) { return scope_part(c, true); }, {},
        script::attr_configurable);
    (void)cssom_interface(cx, internals, "CSSStartingStyleRule", "CSSGroupingRule", nullptr);
    // --- CSSNestedDeclarations, CSS Nesting 1 §4.1: a declaration block and
    // nothing else, reached through `style`.
    declaration_accessor(
        cx, cssom_interface(cx, internals, "CSSNestedDeclarations", "CSSRule", nullptr));
    declaration_accessor(cx, cssom_interface(cx, internals, "CSSFontFaceRule", "CSSRule", nullptr));

    // --- CSSImportRule, CSSOM 6.4.7
    //
    // `styleSheet` is the sheet load_imported_sheet fetched when the rule was
    // parsed - a CSSStyleSheet of its own with its own rules, as every engine
    // answers, and null only for an import a constructed sheet was given.
    script::object_object * import_proto =
        cssom_interface(cx, internals, "CSSImportRule", "CSSRule", nullptr);
    define_getter(
        cx, *import_proto, "href",
        [this](context & c, std::span<value>) {
            const css_rule_record * rule = receiver_rule(c);
            return c.string(rule == nullptr ? std::string{} : rule->selector);
        },
        {}, script::attr_configurable);
    define_getter(
        cx, *import_proto, "styleSheet",
        [this](context & c, std::span<value>) {
            const css_rule_record * rule = receiver_rule(c);
            if (rule == nullptr || rule->imported_sheet >= css_sheets_.size()) {
                return value::null();
            }
            return sheet_object_for(c, rule->imported_sheet);
        },
        {}, script::attr_configurable);
    define_getter(
        cx, *import_proto, "media",
        [this](context & c, std::span<value>) {
            script::object_object * self = as_object(c.current_this());
            if (self == nullptr || receiver_rule(c) == nullptr) { return value::undefined(); }
            return media_list_object(c, *self);
        },
        [](context & c, std::span<value> a) {
            const value list = c.lookup_property(c.current_this(), "media");
            c.store_property(list, "mediaText", a.empty() ? c.string("") : a[0]);
            return value::undefined();
        },
        script::attr_configurable);
    // `layerName` and `supportsText` are NULL when the import has neither,
    // which is the difference CSSOM draws between "no layer" and "an anonymous
    // one" - `@import url(a) layer` has a layer whose name is the empty string.
    const auto import_part = [this](context & c, std::string_view keyword, bool bare) {
        const css_rule_record * rule = receiver_rule(c);
        if (rule == nullptr) { return value::null(); }
        std::size_t at = 0;
        while (true) {
            const std::string_view part = next_component(rule->prelude, at);
            if (part.empty()) { return value::null(); }
            if (bare && ascii_iequals(part, keyword)) { return c.string(""); }
            if (part.size() > keyword.size() + 1 &&
                ascii_iequals(part.substr(0, keyword.size() + 1), std::string{keyword} + "(") &&
                part.back() == ')') {
                return c.string(
                    std::string{part.substr(keyword.size() + 1, part.size() - keyword.size() - 2)});
            }
        }
    };
    define_getter(
        cx, *import_proto, "layerName",
        [import_part](context & c, std::span<value>) { return import_part(c, "layer", true); }, {},
        script::attr_configurable);
    define_getter(
        cx, *import_proto, "supportsText",
        [import_part](context & c, std::span<value>) { return import_part(c, "supports", false); },
        {}, script::attr_configurable);

    // --- CSSNamespaceRule, CSSOM 6.4.9. A DEFAULT namespace has no prefix, and
    // the empty string is how CSSOM reports that - not `null`.
    script::object_object * namespace_proto =
        cssom_interface(cx, internals, "CSSNamespaceRule", "CSSRule", nullptr);
    define_getter(
        cx, *namespace_proto, "prefix",
        [this](context & c, std::span<value>) {
            const css_rule_record * rule = receiver_rule(c);
            return c.string(rule == nullptr ? std::string{} : rule->selector);
        },
        {}, script::attr_configurable);
    define_getter(
        cx, *namespace_proto, "namespaceURI",
        [this](context & c, std::span<value>) {
            const css_rule_record * rule = receiver_rule(c);
            return c.string(rule == nullptr ? std::string{} : rule->prelude);
        },
        {}, script::attr_configurable);

    // --- CSSPageRule. A grouping rule in CSSOM's current draft and a plain
    // CSSRule in the 2011 one; the draft is what Chrome exposes.
    script::object_object * page_proto =
        cssom_interface(cx, internals, "CSSPageRule", "CSSGroupingRule", nullptr);
    declaration_accessor(cx, page_proto);
    define_getter(
        cx, *page_proto, "selectorText",
        [this](context & c, std::span<value>) {
            const css_rule_record * rule = receiver_rule(c);
            return c.string(rule == nullptr ? std::string{} : rule->prelude);
        },
        [this](context & c, std::span<value> a) {
            css_rule_record * rule = receiver_rule(c);
            if (rule == nullptr) { return value::undefined(); }
            // "If the parse failed, do nothing" - and `named :first` is a parse
            // failure rather than two selectors, because a `<page-selector>`
            // has no whitespace in it anywhere.
            bool ok = false;
            const std::string parsed = serialize_page_selector(arg_string(c, a, 0), ok);
            if (!ok) { return value::undefined(); }
            rule->prelude = parsed;
            style_sheets_changed();
            return value::undefined();
        },
        script::attr_configurable);
}

} // namespace ctbrowser::shell
