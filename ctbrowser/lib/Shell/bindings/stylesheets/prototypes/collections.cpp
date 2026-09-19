#include "helpers.hpp"

#include <ctbrowser/shell/net/url.hpp>

namespace ctbrowser::shell {

using namespace detail;

void dom_bindings::install_stylesheet_collections(context & cx, script::object_object * internals) {
    // --- MediaList
    //
    // A VIEW OF A RECORD'S QUERY LIST, not a list of its own. `mediaText`,
    // `appendMedium` and `deleteMedium` all write through to the sheet or the
    // media rule the object came from, which is what makes
    // `rule.media.appendMedium('print')` change `rule.cssText` - the two are one
    // list read two ways rather than two lists that have to be kept in step.
    script::object_object * media_proto =
        cssom_interface(cx, internals, "MediaList", nullptr, nullptr);
    cssom_iterable(cx, media_proto);
    set_method(
        cx, *media_proto, "item",
        [](context & c, std::span<value> a) { return collection_item(c, a); },
        script::attr_builtin);
    define_getter(
        cx, *media_proto, "mediaText",
        [this](context & c, std::span<value>) {
            const std::vector<std::string> * queries = receiver_media(c);
            return c.string(queries == nullptr ? std::string{}
                                               : serialize_media_query_list(*queries));
        },
        [this](context & c, std::span<value> a) {
            std::vector<std::string> * queries = receiver_media(c);
            if (queries == nullptr) { return value::undefined(); }
            // [LegacyNullToEmptyString]: `media.mediaText = null` EMPTIES the
            // list rather than parsing the string "null", which
            // `css/cssom/MediaList.html` asserts by name.
            const std::string text = a.empty() || a[0].is_null() || a[0].is_undefined()
                                         ? std::string{}
                                         : c.to_string(a[0]);
            *queries = parse_media_query_list(text);
            refresh_media_list(c, c.current_this());
            style_sheets_changed();
            return value::undefined();
        },
        script::attr_configurable);
    // The stringifier. `media.toString()` and `'' + media` are both `mediaText`.
    set_method(
        cx, *media_proto, "toString",
        [](context & c, std::span<value>) {
            return c.lookup_property(c.current_this(), "mediaText");
        },
        script::attr_builtin);
    set_method(
        cx, *media_proto, "appendMedium",
        [this](context & c, std::span<value> a) {
            std::vector<std::string> * queries = receiver_media(c);
            if (queries == nullptr) { return value::undefined(); }
            // "Parse A MEDIA QUERY" - singular. `appendMedium("screen, print")` is
            // not two appends and it is not one query called `screen, print`
            // either: the parse returns null, and step 1 says return. A top-level
            // comma is the whole test for it.
            const std::string one = arg_string(c, a, 0);
            if (split_on_commas(one).size() != 1) { return value::undefined(); }
            const std::string added = serialize_media_query_text(one);
            if (added.empty()) { return value::undefined(); }
            // "If comparing medium with any of the media queries in the collection
            // returns true, then return" - appending a medium twice is a no-op.
            if (std::find(queries->begin(), queries->end(), added) != queries->end()) {
                return value::undefined();
            }
            queries->push_back(added);
            refresh_media_list(c, c.current_this());
            style_sheets_changed();
            return value::undefined();
        },
        script::attr_builtin);
    set_method(
        cx, *media_proto, "deleteMedium",
        [this](context & c, std::span<value> a) {
            std::vector<std::string> * queries = receiver_media(c);
            if (queries == nullptr) { return value::undefined(); }
            // A REQUIRED ARGUMENT, so calling it with none is a TypeError and not a
            // NotFoundError about the empty string - `medialist-interfaces-002.html`
            // asserts which of the two by name.
            if (a.empty()) {
                c.throw_error("TypeError", "deleteMedium requires a medium");
                return value::undefined();
            }
            const std::string wanted = serialize_media_query_text(c.to_string(a[0]));
            // "Remove ALL media queries in the collection that match" - a list may
            // hold the same query twice (`screen, print, screen`) and removing only
            // the first leaves one behind that the page has just asked to be rid of.
            const auto gone = std::remove(queries->begin(), queries->end(), wanted);
            if (wanted.empty() || gone == queries->end()) {
                // "If nothing was removed, then throw a NotFoundError" - the one
                // place in the CSSOM where deleting something absent is an error.
                throw_dom_exception(c, "NotFoundError", "that medium is not in the list");
                return value::undefined();
            }
            queries->erase(gone, queries->end());
            refresh_media_list(c, c.current_this());
            style_sheets_changed();
            return value::undefined();
        },
        script::attr_builtin);

    // --- StyleSheetList
    script::object_object * list_proto =
        cssom_interface(cx, internals, "StyleSheetList", nullptr, nullptr);
    cssom_iterable(cx, list_proto);
    // `item()` re-derives the list from the tree first - see resync_sheet_list.
    set_method(
        cx, *list_proto, "item",
        [this](context & c, std::span<value> a) {
            if (script::object_object * self = as_object(c.current_this())) {
                resync_sheet_list(c, *self);
            }
            return collection_item(c, a);
        },
        script::attr_builtin);

    // --- CSSRuleList
    script::object_object * rules_proto =
        cssom_interface(cx, internals, "CSSRuleList", nullptr, nullptr);
    cssom_iterable(cx, rules_proto);
    set_method(
        cx, *rules_proto, "item",
        [](context & c, std::span<value> a) { return collection_item(c, a); },
        script::attr_builtin);

    // --- StyleSheet / CSSStyleSheet
    script::object_object * base_proto =
        cssom_interface(cx, internals, "StyleSheet", nullptr, nullptr);
    define_getter(
        cx, *base_proto, "type", [](context & c, std::span<value>) { return c.string("text/css"); },
        {}, script::attr_configurable);
    define_getter(
        cx, *base_proto, "href",
        [this](context & c, std::span<value>) {
            const css_sheet_record * sheet = receiver_sheet(c);
            if (sheet == nullptr || sheet->href.empty()) { return value::null(); }
            return c.string(sheet->href);
        },
        {}, script::attr_configurable);
    define_getter(
        cx, *base_proto, "ownerNode",
        [this](context & c, std::span<value>) {
            const css_sheet_record * sheet = receiver_sheet(c);
            // NULL ONCE THE OWNER NO LONGER CARRIES IT: a `<link>` that was
            // disabled or removed keeps this record for the page that holds it,
            // and the record answers that it belongs to nothing. The owner's tree
            // is re-walked first, so the answer follows a `link.disabled = true`
            // made a statement ago.
            if (sheet == nullptr || !sheet->owner) { return value::null(); }
            if (shadow_tree_of(sheet->tree) != nullptr) {
                (void)shadow_sheet_list(c, sheet->tree);
            } else {
                sync_style_sheets(c);
            }
            if (!sheet->attached) { return value::null(); }
            return wrap(c, sheet->owner);
        },
        {}, script::attr_configurable);
    // An `@import`'s sheet knows its rule, and through it its parent - and
    // deleteRule detaches the rule from its sheet, which is what makes
    // `parentStyleSheet` null afterwards (cssimportrule-parent.html).
    const auto owner_rule = [this](context & c) -> const css_rule_record * {
        const css_sheet_record * sheet = receiver_sheet(c);
        if (sheet == nullptr || sheet->owner_rule >= css_rule_store_.size()) { return nullptr; }
        const css_rule_record & rule = *css_rule_store_[sheet->owner_rule];
        return rule.sheet < css_sheets_.size() ? &rule : nullptr;
    };
    define_getter(
        cx, *base_proto, "ownerRule",
        [this, owner_rule](context & c, std::span<value>) {
            const css_rule_record * rule = owner_rule(c);
            return rule == nullptr ? value::null()
                                   : rule_object_for(c, receiver_sheet(c)->owner_rule);
        },
        {}, script::attr_configurable);
    define_getter(
        cx, *base_proto, "parentStyleSheet",
        [this, owner_rule](context & c, std::span<value>) {
            const css_rule_record * rule = owner_rule(c);
            return rule == nullptr ? value::null() : sheet_object_for(c, rule->sheet);
        },
        {}, script::attr_configurable);
    define_getter(
        cx, *base_proto, "title",
        [this](context & c, std::span<value>) {
            const css_sheet_record * sheet = receiver_sheet(c);
            // "The title attribute must return the title or null if the title is
            // the empty string" - and a CONSTRUCTED sheet has no title at all,
            // whatever was passed to the constructor.
            if (sheet == nullptr || sheet->constructed || sheet->title.empty()) {
                return value::null();
            }
            return c.string(sheet->title);
        },
        {}, script::attr_configurable);
    define_getter(
        cx, *base_proto, "media",
        [this](context & c, std::span<value>) {
            script::object_object * self = as_object(c.current_this());
            if (self == nullptr || receiver_sheet(c) == nullptr) { return value::undefined(); }
            return media_list_object(c, *self);
        },
        [](context & c, std::span<value> a) {
            // [PutForwards=mediaText]: `sheet.media = 'print'` assigns to the
            // MediaList's mediaText and the list object itself never changes.
            const value list = c.lookup_property(c.current_this(), "media");
            c.store_property(list, "mediaText", a.empty() ? c.string("") : a[0]);
            return value::undefined();
        },
        script::attr_configurable);
    define_getter(
        cx, *base_proto, "disabled",
        [this](context & c, std::span<value>) {
            const css_sheet_record * sheet = receiver_sheet(c);
            return value::boolean(sheet != nullptr && sheet->disabled);
        },
        [this](context & c, std::span<value> a) {
            if (css_sheet_record * sheet = receiver_sheet(c)) {
                const bool wanted = !a.empty() && context::truthy(a[0]);
                if (sheet->disabled != wanted) {
                    sheet->disabled = wanted;
                    style_sheets_changed();
                }
            }
            return value::undefined();
        },
        script::attr_configurable);

    script::object_object * sheet_proto = cssom_interface(
        cx, internals, "CSSStyleSheet", "StyleSheet", [this](context & c, std::span<value> args) {
            // `new CSSStyleSheet(options)`. `media` and `disabled` are honoured;
            // `title` is NOT, which is not an omission - the specification says
            // a constructed sheet has no title however it was constructed, and
            // `CSSStyleSheet-constructable.html` asserts exactly that.
            // `baseURL` is parsed against the document's; one that cannot be
            // parsed is a NotAllowedError (CSSStyleSheet-constructable-baseURL).
            // Nothing here resolves a `url()` against it, so it is only checked.
            if (!args.empty() && args[0].is_object()) {
                auto * options = static_cast<script::object_object *>(args[0].as_heap());
                if (const value * base = options->find("baseURL");
                    base != nullptr && !base->is_undefined()) {
                    const std::string given = c.to_string(*base);
                    if (given.find("://") != std::string::npos && !parse_absolute(given).valid) {
                        throw_dom_exception(c, "NotAllowedError", "baseURL is not a valid URL");
                        return value::undefined();
                    }
                }
            }
            css_sheets_.push_back(std::make_unique<css_sheet_record>());
            const std::size_t at = css_sheets_.size() - 1;
            css_sheets_[at]->constructed = true;
            if (!args.empty() && args[0].is_object()) {
                auto * options = static_cast<script::object_object *>(args[0].as_heap());
                if (const value * media = options->find("media")) {
                    css_sheets_[at]->media = c.to_string(*media);
                    css_sheets_[at]->media_queries = parse_media_query_list(css_sheets_[at]->media);
                }
                if (const value * disabled = options->find("disabled")) {
                    css_sheets_[at]->disabled = context::truthy(*disabled);
                }
            }
            return sheet_object_for(c, at);
        });
    // "If the origin-clean flag is unset, throw a SecurityError" - the first
    // step of cssRules, insertRule and deleteRule alike, CSSOM 6.3.
    const auto origin_dirty = [this](context & c) {
        const css_sheet_record * sheet = receiver_sheet(c);
        if (sheet == nullptr || sheet->origin_clean) { return false; }
        throw_dom_exception(c, "SecurityError", "the stylesheet is not origin-clean");
        return true;
    };
    define_getter(
        cx, *sheet_proto, "cssRules",
        [this, origin_dirty](context & c, std::span<value>) {
            // [SameObject]: `sheet.cssRules === sheet.cssRules` and
            // `sheet.cssRules === sheet.rules` are both asserted, so the list is
            // built once and REFRESHED rather than rebuilt.
            script::object_object * self = as_object(c.current_this());
            const css_sheet_record * sheet = receiver_sheet(c);
            if (self == nullptr || sheet == nullptr || origin_dirty(c)) {
                return value::undefined();
            }
            if (const value * held = self->find(rules_key)) {
                refresh_rule_list(c, *held, sheet->rules);
                return *held;
            }
            const value list = make_rule_list(c, sheet->rules);
            self->define(rules_key, list, script::attr_none);
            return list;
        },
        {}, script::attr_configurable);
    // `rules` is the legacy alias and must be the SAME object.
    sheet_proto->define_accessor("rules",
                                 value::object(cx.allocate<script::native_object>(
                                     "get rules",
                                     [](context & c, std::span<value>) {
                                         const value self = c.current_this();
                                         return c.lookup_property(self, "cssRules");
                                     })),
                                 value::undefined(), script::attr_configurable);
    set_method(
        cx, *sheet_proto, "insertRule",
        [this, origin_dirty](context & c, std::span<value> args) {
            css_sheet_record * sheet = receiver_sheet(c);
            if (sheet == nullptr || origin_dirty(c)) { return value::undefined(); }
            if (args.empty()) {
                c.throw_error("TypeError", "insertRule requires a rule");
                return value::undefined();
            }
            const std::string text = c.to_string(args[0]);
            // `insertRule(text)` and `insertRule(text, undefined)` ARE THE SAME
            // CALL. `index` is an optional unsigned long defaulting to 0, and Web
            // IDL says an `undefined` passed for an optional argument means the
            // default was not supplied - it is not `ToNumber(undefined)`, which is
            // NaN and was answering IndexSizeError for a perfectly ordinary
            // insertion. `insertRule-no-index.html` and its four siblings are one
            // subtest each of exactly that.
            const double asked =
                args.size() > 1 && !args[1].is_undefined() ? context::to_number(args[1]) : 0;
            if (!(asked >= 0) || asked > static_cast<double>(sheet->rules.size())) {
                throw_dom_exception(c, "IndexSizeError", "the index is past the end of the sheet");
                return value::undefined();
            }
            const auto at = static_cast<std::size_t>(asked);
            std::string error;
            const std::size_t made = parse_one_rule(
                static_cast<std::size_t>(slot_index(as_object(c.current_this()), sheet_key)), text,
                error);
            if (made == no_index) {
                throw_dom_exception(c, error.empty() ? std::string{"SyntaxError"} : error,
                                    "the text is not a single CSS rule");
                return value::undefined();
            }
            prune_bare_declarations(css_rule_store_, made);
            const std::uint32_t kind = css_rule_store_[made]->type;
            if (kind == import_rule && sheet->constructed) {
                // "@import rules are not allowed in a constructed stylesheet."
                throw_dom_exception(c, "SyntaxError", "@import is not allowed here");
                return value::undefined();
            }
            // WHAT MAY PRECEDE WHAT - CSSOM 6.3.3 steps 4 and 5, and the two steps
            // answer different questions. Step 4 is about the POSITION: `@import`
            // may only go where everything before it is `@charset`, `@layer` or
            // another `@import`, `@namespace` may additionally follow those, and
            // everything else may only go after all of them. Step 5 is about the
            // WHOLE LIST: a `@namespace` may not be added to a sheet that already
            // has a style rule in it at all, wherever the insertion point is,
            // because the namespace would change what the existing selectors mean.
            // `at-namespace.html` is one assertion of exactly that and says so in
            // its title.
            const auto rule_type = [this, sheet](std::size_t i) {
                const std::size_t which = sheet->rules[i];
                return which < css_rule_store_.size() ? css_rule_store_[which]->type : 0;
            };
            const auto before_ok = [&](std::uint32_t allowed_a, std::uint32_t allowed_b) {
                for (std::size_t i = 0; i < at; ++i) {
                    const std::uint32_t each = rule_type(i);
                    if (each != allowed_a && each != allowed_b) { return false; }
                }
                return true;
            };
            bool allowed = true;
            if (kind == import_rule) {
                allowed = before_ok(import_rule, import_rule);
            } else if (kind == namespace_rule) {
                allowed = before_ok(import_rule, namespace_rule);
            } else {
                // Nothing else may be inserted BEFORE an `@import` or a
                // `@namespace`, so every rule from the insertion point on must be
                // neither.
                for (std::size_t i = at; i < sheet->rules.size(); ++i) {
                    const std::uint32_t each = rule_type(i);
                    if (each == import_rule || each == namespace_rule) { allowed = false; }
                }
            }
            if (!allowed) {
                throw_dom_exception(c, "HierarchyRequestError", "that rule may not go there");
                return value::undefined();
            }
            if (kind == namespace_rule) {
                for (std::size_t i = 0; i < sheet->rules.size(); ++i) {
                    const std::uint32_t each = rule_type(i);
                    if (each != import_rule && each != namespace_rule) {
                        throw_dom_exception(c, "InvalidStateError",
                                            "the sheet already has rules a namespace would change");
                        return value::undefined();
                    }
                }
            }
            sheet->rules.insert(sheet->rules.begin() + static_cast<std::ptrdiff_t>(at), made);
            refresh_cached_rules(c, sheet->rules);
            style_sheets_changed();
            return value::number(asked);
        },
        script::attr_builtin);
    set_method(
        cx, *sheet_proto, "deleteRule",
        [this, origin_dirty](context & c, std::span<value> args) {
            css_sheet_record * sheet = receiver_sheet(c);
            if (sheet == nullptr || origin_dirty(c)) { return value::undefined(); }
            if (args.empty()) {
                c.throw_error("TypeError", "deleteRule requires an index");
                return value::undefined();
            }
            const double asked = context::to_number(args[0]);
            if (!(asked >= 0) || asked >= static_cast<double>(sheet->rules.size())) {
                throw_dom_exception(c, "IndexSizeError", "there is no rule at that index");
                return value::undefined();
            }
            // THE MIRROR OF THE INSERT RULE, CSSOM 6.3.4: removing a `@namespace`
            // from a sheet that has anything but `@import` and `@namespace` in it
            // would change what the remaining selectors mean, so it is refused.
            const std::size_t going = sheet->rules[static_cast<std::size_t>(asked)];
            if (going < css_rule_store_.size() && css_rule_store_[going]->type == namespace_rule) {
                for (const std::size_t each : sheet->rules) {
                    const std::uint32_t kind =
                        each < css_rule_store_.size() ? css_rule_store_[each]->type : 0;
                    if (kind != import_rule && kind != namespace_rule) {
                        throw_dom_exception(c, "InvalidStateError",
                                            "the sheet has rules that namespace would change");
                        return value::undefined();
                    }
                }
            }
            detach_rule(css_rule_store_, going);
            sheet->rules.erase(sheet->rules.begin() + static_cast<std::ptrdiff_t>(asked));
            refresh_cached_rules(c, sheet->rules);
            style_sheets_changed();
            return value::undefined();
        },
        script::attr_builtin);
    // The two legacy IE spellings CSSOM keeps: `removeRule` is `deleteRule` with
    // a default index, and `addRule` builds a rule out of two strings and always
    // answers -1.
    set_method(
        cx, *sheet_proto, "removeRule",
        [](context & c, std::span<value> args) {
            const value self = c.current_this();
            const value method_value = c.lookup_property(self, "deleteRule");
            const value index = args.empty() ? value::number(0) : args[0];
            const value forwarded[1] = {index};
            return c.call(method_value, forwarded, self);
        },
        script::attr_builtin);
    set_method(
        cx, *sheet_proto, "addRule",
        [](context & c, std::span<value> args) {
            const value self = c.current_this();
            const std::string selector =
                args.empty() ? std::string{"undefined"} : c.to_string(args[0]);
            const std::string block =
                args.size() > 1 ? c.to_string(args[1]) : std::string{"undefined"};
            const value length = c.lookup_property(c.lookup_property(self, "cssRules"), "length");
            const value index = args.size() > 2 ? args[2] : length;
            const value method_value = c.lookup_property(self, "insertRule");
            const value forwarded[2] = {c.string(selector + " { " + block + " }"), index};
            (void)c.call(method_value, forwarded, self);
            return value::number(-1);
        },
        script::attr_builtin);
    // The two differ in HOW they refuse, not in what they refuse: `replaceSync`
    // THROWS a NotAllowedError and `replace` returns a promise REJECTED with
    // one. `CSSStyleSheet-constructable-replace-on-regular-sheet.html` asserts
    // both halves separately, and a throw out of `replace` fails that test with
    // an uncaught exception rather than the rejection it is waiting for.
    const auto replace_rules = [this](context & c, std::span<value> args) -> bool {
        css_sheet_record * sheet = receiver_sheet(c);
        if (sheet == nullptr || !sheet->constructed) { return false; }
        const std::size_t at = slot_index(as_object(c.current_this()), sheet_key);
        parse_sheet_rules(at, args.empty() ? std::string{} : c.to_string(args[0]));
        refresh_cached_rules(c, sheet->rules);
        style_sheets_changed();
        return true;
    };
    set_method(
        cx, *sheet_proto, "replaceSync",
        [this, replace_rules](context & c, std::span<value> args) {
            if (!replace_rules(c, args)) {
                throw_dom_exception(c, "NotAllowedError",
                                    "replace is only allowed on a constructed stylesheet");
            }
            return value::undefined();
        },
        script::attr_builtin);
    set_method(
        cx, *sheet_proto, "replace",
        [this, replace_rules](context & c, std::span<value> args) {
            // The work is synchronous - there is no subresource to fetch, `@import`
            // being ignored - so the promise is already settled. What matters to a
            // page is that it IS a promise, that it resolves with the sheet, and
            // that a refusal arrives as a rejection.
            const value self = c.current_this();
            if (!replace_rules(c, args)) {
                return c.make_promise(make_dom_exception(c, "NotAllowedError",
                                                         "replace is only allowed on a "
                                                         "constructed stylesheet"),
                                      true);
            }
            return c.make_promise(self, false);
        },
        script::attr_builtin);
}

} // namespace ctbrowser::shell
