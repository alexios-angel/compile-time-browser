// dom_bindings' CSSOM - install_stylesheet_prototypes: the CSSStyleSheet,
// CSSRuleList, CSSRule, MediaList and CSSStyleDeclaration interfaces.

#include "internal.hpp"

#include <ctbrowser/shell/net/url.hpp>

namespace ctbrowser::shell {

using namespace detail;

// --- the interfaces ---------------------------------------------------------

void dom_bindings::install_stylesheet_prototypes(context & cx) {
    script::object_object * internals = cssom_internals(cx);
    if (internals == nullptr) { return; }

    // One interface object plus its prototype, wired the way
    // `bindings/exceptions.cpp` wires DOMException: the constructor carries a
    // non-writable `prototype`, the prototype carries `constructor`, and the
    // prototype is reachable from the internals object so a page deleting the
    // global cannot collect it.
    const auto interface = [&](const char * name, const char * inherits,
                               script::native_fn construct) -> script::object_object * {
        auto * proto = static_cast<script::object_object *>(cx.make_object().as_heap());
        if (inherits != nullptr) {
            if (const value * parent = internals->find(std::string{inherits} + ".prototype")) {
                proto->prototype = *parent;
            }
        }
        auto * ctor = cx.allocate<script::native_object>(
            name, construct
                      ? std::move(construct)
                      : script::native_fn{[name](context & c, std::span<value>) {
                            c.throw_error("TypeError", std::string{"Illegal constructor: "} + name);
                            return value::undefined();
                        }});
        ctor->define("prototype", value::object(proto), script::attr_none);
        proto->define("constructor", value::object(ctor), script::attr_builtin);
        // `@@toStringTag`, which is what `Object.prototype.toString` - and so
        // `rule.toString()` - reads: `[object CSSFontFaceRule]`, not
        // `[object Object]`. The same key element/interfaces.cpp stamps on the
        // element prototypes; Web IDL puts one on every interface prototype.
        proto->define("@@toStringTag", cx.string(name), script::attr_configurable);
        internals->set(std::string{name} + ".prototype", value::object(proto));
        internals->set(std::string{name}, value::object(ctor));
        cx.define_global(name, value::object(ctor));
        return proto;
    };
    const auto method = [&](script::object_object * on, const char * name, script::native_fn fn) {
        on->define(name, value::object(cx.allocate<script::native_object>(name, std::move(fn))),
                   script::attr_builtin);
    };
    const auto getter = [&](script::object_object * on, const char * name, script::native_fn read) {
        on->define_accessor(name,
                            value::object(cx.allocate<script::native_object>(
                                std::string{"get "} + name, std::move(read))),
                            value::undefined(), script::attr_configurable);
    };
    const auto accessor = [&](script::object_object * on, const char * name, script::native_fn read,
                              script::native_fn write) {
        on->define_accessor(name,
                            value::object(cx.allocate<script::native_object>(
                                std::string{"get "} + name, std::move(read))),
                            value::object(cx.allocate<script::native_object>(
                                std::string{"set "} + name, std::move(write))),
                            script::attr_configurable);
    };
    // `@@iterator` on every collection here. `for (const x of list)` already
    // worked - context::iterable_values reads `length` and the indices - but
    // `Symbol.iterator in CSSStyleDeclaration.prototype` is asked by name, and
    // a page driving the iterator by hand needs a real one. Web IDL gives an
    // indexed-getter interface exactly the Array iterator, so it IS that one,
    // over the snapshot iterable_values already takes.
    const auto iterable = [&](script::object_object * on) {
        method(on, "@@iterator", [](context & c, std::span<value>) {
            const value items = c.iterable_values(c.current_this());
            return c.call(c.lookup_property(items, "values"), std::span<const value>{}, items);
        });
    };

    // --- MediaList
    //
    // A VIEW OF A RECORD'S QUERY LIST, not a list of its own. `mediaText`,
    // `appendMedium` and `deleteMedium` all write through to the sheet or the
    // media rule the object came from, which is what makes
    // `rule.media.appendMedium('print')` change `rule.cssText` - the two are one
    // list read two ways rather than two lists that have to be kept in step.
    script::object_object * media_proto = interface("MediaList", nullptr, nullptr);
    iterable(media_proto);
    method(media_proto, "item",
           [](context & c, std::span<value> a) { return collection_item(c, a); });
    accessor(
        media_proto, "mediaText",
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
        });
    // The stringifier. `media.toString()` and `'' + media` are both `mediaText`.
    method(media_proto, "toString", [](context & c, std::span<value>) {
        return c.lookup_property(c.current_this(), "mediaText");
    });
    method(media_proto, "appendMedium", [this](context & c, std::span<value> a) {
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
    });
    method(media_proto, "deleteMedium", [this](context & c, std::span<value> a) {
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
    });

    // --- StyleSheetList
    script::object_object * list_proto = interface("StyleSheetList", nullptr, nullptr);
    iterable(list_proto);
    // LIVE: both re-derive the list from the tree first - see set_sheet_list.
    getter(list_proto, "length", [this](context & c, std::span<value>) {
        script::object_object * self = as_object(c.current_this());
        if (self == nullptr) { return value::number(0); }
        resync_sheet_list(c, *self);
        const std::size_t count = slot_index(self, count_key);
        return value::number(count == no_index ? 0 : static_cast<double>(count));
    });
    method(list_proto, "item", [this](context & c, std::span<value> a) {
        if (script::object_object * self = as_object(c.current_this())) {
            resync_sheet_list(c, *self);
        }
        return collection_item(c, a);
    });

    // --- CSSRuleList
    script::object_object * rules_proto = interface("CSSRuleList", nullptr, nullptr);
    iterable(rules_proto);
    method(rules_proto, "item",
           [](context & c, std::span<value> a) { return collection_item(c, a); });

    // --- StyleSheet / CSSStyleSheet
    script::object_object * base_proto = interface("StyleSheet", nullptr, nullptr);
    getter(base_proto, "type", [](context & c, std::span<value>) { return c.string("text/css"); });
    getter(base_proto, "href", [this](context & c, std::span<value>) {
        const css_sheet_record * sheet = receiver_sheet(c);
        if (sheet == nullptr || sheet->href.empty()) { return value::null(); }
        return c.string(sheet->href);
    });
    getter(base_proto, "ownerNode", [this](context & c, std::span<value>) {
        const css_sheet_record * sheet = receiver_sheet(c);
        // NULL ONCE THE OWNER NO LONGER CARRIES IT: a `<link>` that was
        // disabled or removed keeps this record for the page that holds it,
        // and the record answers that it belongs to nothing.
        if (sheet == nullptr || !sheet->owner || !sheet->attached) { return value::null(); }
        return wrap(c, sheet->owner);
    });
    // An `@import`'s sheet knows its rule, and through it its parent - and
    // deleteRule detaches the rule from its sheet, which is what makes
    // `parentStyleSheet` null afterwards (cssimportrule-parent.html).
    const auto owner_rule = [this](context & c) -> const css_rule_record * {
        const css_sheet_record * sheet = receiver_sheet(c);
        if (sheet == nullptr || sheet->owner_rule >= css_rule_store_.size()) { return nullptr; }
        const css_rule_record & rule = *css_rule_store_[sheet->owner_rule];
        return rule.sheet < css_sheets_.size() ? &rule : nullptr;
    };
    getter(base_proto, "ownerRule", [this, owner_rule](context & c, std::span<value>) {
        const css_rule_record * rule = owner_rule(c);
        return rule == nullptr ? value::null() : rule_object_for(c, receiver_sheet(c)->owner_rule);
    });
    getter(base_proto, "parentStyleSheet", [this, owner_rule](context & c, std::span<value>) {
        const css_rule_record * rule = owner_rule(c);
        return rule == nullptr ? value::null() : sheet_object_for(c, rule->sheet);
    });
    getter(base_proto, "title", [this](context & c, std::span<value>) {
        const css_sheet_record * sheet = receiver_sheet(c);
        // "The title attribute must return the title or null if the title is
        // the empty string" - and a CONSTRUCTED sheet has no title at all,
        // whatever was passed to the constructor.
        if (sheet == nullptr || sheet->constructed || sheet->title.empty()) {
            return value::null();
        }
        return c.string(sheet->title);
    });
    accessor(
        base_proto, "media",
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
        });
    accessor(
        base_proto, "disabled",
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
        });

    script::object_object * sheet_proto =
        interface("CSSStyleSheet", "StyleSheet", [this](context & c, std::span<value> args) {
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
    getter(sheet_proto, "cssRules", [this, origin_dirty](context & c, std::span<value>) {
        // [SameObject]: `sheet.cssRules === sheet.cssRules` and
        // `sheet.cssRules === sheet.rules` are both asserted, so the list is
        // built once and REFRESHED rather than rebuilt.
        script::object_object * self = as_object(c.current_this());
        const css_sheet_record * sheet = receiver_sheet(c);
        if (self == nullptr || sheet == nullptr || origin_dirty(c)) { return value::undefined(); }
        if (const value * held = self->find(rules_key)) {
            refresh_rule_list(c, *held, sheet->rules);
            return *held;
        }
        const value list = make_rule_list(c, sheet->rules);
        self->define(rules_key, list, script::attr_none);
        return list;
    });
    // `rules` is the legacy alias and must be the SAME object.
    sheet_proto->define_accessor("rules",
                                 value::object(cx.allocate<script::native_object>(
                                     "get rules",
                                     [](context & c, std::span<value>) {
                                         const value self = c.current_this();
                                         return c.lookup_property(self, "cssRules");
                                     })),
                                 value::undefined(), script::attr_configurable);
    method(sheet_proto, "insertRule", [this, origin_dirty](context & c, std::span<value> args) {
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
        style_sheets_changed();
        return value::number(asked);
    });
    method(sheet_proto, "deleteRule", [this, origin_dirty](context & c, std::span<value> args) {
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
        style_sheets_changed();
        return value::undefined();
    });
    // The two legacy IE spellings CSSOM keeps: `removeRule` is `deleteRule` with
    // a default index, and `addRule` builds a rule out of two strings and always
    // answers -1.
    method(sheet_proto, "removeRule", [](context & c, std::span<value> args) {
        const value self = c.current_this();
        const value method_value = c.lookup_property(self, "deleteRule");
        const value index = args.empty() ? value::number(0) : args[0];
        const value forwarded[1] = {index};
        return c.call(method_value, forwarded, self);
    });
    method(sheet_proto, "addRule", [](context & c, std::span<value> args) {
        const value self = c.current_this();
        const std::string selector = args.empty() ? std::string{"undefined"} : c.to_string(args[0]);
        const std::string block = args.size() > 1 ? c.to_string(args[1]) : std::string{"undefined"};
        const value length = c.lookup_property(c.lookup_property(self, "cssRules"), "length");
        const value index = args.size() > 2 ? args[2] : length;
        const value method_value = c.lookup_property(self, "insertRule");
        const value forwarded[2] = {c.string(selector + " { " + block + " }"), index};
        (void)c.call(method_value, forwarded, self);
        return value::number(-1);
    });
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
        style_sheets_changed();
        return true;
    };
    method(sheet_proto, "replaceSync", [this, replace_rules](context & c, std::span<value> args) {
        if (!replace_rules(c, args)) {
            throw_dom_exception(c, "NotAllowedError",
                                "replace is only allowed on a constructed stylesheet");
        }
        return value::undefined();
    });
    method(sheet_proto, "replace", [this, replace_rules](context & c, std::span<value> args) {
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
    });

    // --- CSSRule and its subclasses
    script::object_object * rule_proto = interface("CSSRule", nullptr, nullptr);
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
    getter(rule_proto, "type", [this](context & c, std::span<value>) {
        const css_rule_record * rule = receiver_rule(c);
        return value::number(rule == nullptr ? 0 : static_cast<double>(rule->type));
    });
    getter(rule_proto, "cssText", [this](context & c, std::span<value>) {
        const css_rule_record * rule = receiver_rule(c);
        return c.string(rule == nullptr ? std::string{} : rule_css_text(*rule));
    });
    getter(rule_proto, "parentRule", [this](context & c, std::span<value>) {
        const css_rule_record * rule = receiver_rule(c);
        if (rule == nullptr || rule->parent >= css_rule_store_.size()) { return value::null(); }
        return rule_object_for(c, rule->parent);
    });
    getter(rule_proto, "parentStyleSheet", [this](context & c, std::span<value>) {
        const css_rule_record * rule = receiver_rule(c);
        if (rule == nullptr || rule->sheet >= css_sheets_.size()) { return value::null(); }
        return sheet_object_for(c, rule->sheet);
    });

    script::object_object * style_rule_proto = interface("CSSStyleRule", "CSSRule", nullptr);
    accessor(
        style_rule_proto, "selectorText",
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
            const style::css::stylesheet parsed =
                style::css::parse_selector_text(text, *atoms_, bad, &namespaces);
            if (bad || parsed.selectors.empty()) { return value::undefined(); }
            rule->selector = representable(parsed.selectors)
                                 ? serialize_selector_list(parsed.selectors, *atoms_)
                                 : collapse_whitespace(text);
            style_sheets_changed();
            return value::undefined();
        });
    // `.style`, ON EVERY RULE WHOSE BLOCK IS DECLARATIONS - which is five of
    // them and was one. CSSOM gives a CSSStyleDeclaration to CSSStyleRule,
    // CSSFontFaceRule, CSSPageRule, CSSKeyframeRule and CSSCounterStyleRule
    // alike; `css/cssom/property-accessors.html` reaches for
    // `document.styleSheets[0].cssRules[0].style` where rule zero is a
    // `@font-face`, and nine of its nine subtests died on `getPropertyValue is
    // undefined` rather than on anything it set out to test.
    const auto declaration_accessor = [&](script::object_object * on) {
        accessor(
            on, "style",
            [this](context & c, std::span<value>) {
                // [SameObject], and LAZY. The declaration object is where the
                // ~290 property accessors are reachable from, and a sheet with
                // three thousand rules in it would otherwise build three
                // thousand of them for a page that never reads one.
                script::object_object * self = as_object(c.current_this());
                if (self == nullptr) { return value::undefined(); }
                if (const value * held = self->find(style_key)) {
                    refresh_declaration_object(c, *held);
                    return *held;
                }
                const std::size_t at = slot_index(self, rule_key);
                const value made = declaration_object(c, at);
                self->define(style_key, made, script::attr_none);
                return made;
            },
            [](context & c, std::span<value> a) {
                // [PutForwards=cssText]: `rule.style = "margin: 42px"` assigns
                // to `rule.style.cssText` and the object itself never changes.
                const value self = c.current_this();
                const value declarations = c.lookup_property(self, "style");
                c.store_property(declarations, "cssText", a.empty() ? c.string("") : a[0]);
                return value::undefined();
            });
    };
    declaration_accessor(style_rule_proto);

    script::object_object * grouping_proto = interface("CSSGroupingRule", "CSSRule", nullptr);
    getter(grouping_proto, "cssRules", [this](context & c, std::span<value>) {
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
    });
    // insertRule/deleteRule ON THE GROUP, which is the same pair of methods
    // CSSStyleSheet has and NOT the same list: a rule inserted here becomes a
    // child of the group and never a sibling of it. `@media print {}` followed
    // by `rule.insertRule(...)` is how `css/cssom/serialize-media-rule.html`
    // builds every one of its fixtures.
    method(grouping_proto, "insertRule", [this](context & c, std::span<value> args) {
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
        rule->children.insert(rule->children.begin() + static_cast<std::ptrdiff_t>(asked), made);
        style_sheets_changed();
        return value::number(asked);
    });
    method(grouping_proto, "deleteRule", [this](context & c, std::span<value> args) {
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
        style_sheets_changed();
        return value::undefined();
    });
    script::object_object * condition_proto =
        interface("CSSConditionRule", "CSSGroupingRule", nullptr);
    getter(condition_proto, "conditionText", [this](context & c, std::span<value>) {
        const css_rule_record * rule = receiver_rule(c);
        if (rule == nullptr) { return c.string(""); }
        // "The conditionText of a CSSMediaRule is its media's mediaText" -
        // CSSOM 6.4.4. Only `@supports` answers from `prelude`, its condition
        // being a `<supports-condition>` and not a media query list.
        if (rule->type == media_rule) {
            return c.string(serialize_media_query_list(rule->media_queries));
        }
        return c.string(rule->prelude);
    });
    script::object_object * media_rule_proto =
        interface("CSSMediaRule", "CSSConditionRule", nullptr);
    accessor(
        media_rule_proto, "media",
        [this](context & c, std::span<value>) {
            script::object_object * self = as_object(c.current_this());
            if (self == nullptr || receiver_rule(c) == nullptr) { return value::undefined(); }
            return media_list_object(c, *self);
        },
        [](context & c, std::span<value> a) {
            const value list = c.lookup_property(c.current_this(), "media");
            c.store_property(list, "mediaText", a.empty() ? c.string("") : a[0]);
            return value::undefined();
        });
    (void)interface("CSSSupportsRule", "CSSConditionRule", nullptr);
    // --- CSSContainerRule, css-conditional-5. The prelude is `<container-name>?
    // <container-query>`: a name is an identifier and a query begins with `(`
    // (or `not`/`and`/`or` of one), so the first component decides which.
    script::object_object * container_proto =
        interface("CSSContainerRule", "CSSConditionRule", nullptr);
    const auto container_part = [this](context & c, bool want_name) {
        const css_rule_record * rule = receiver_rule(c);
        if (rule == nullptr) { return c.string(""); }
        std::size_t at = 0;
        const std::string_view first = next_component(rule->prelude, at);
        const bool named = !first.empty() && first.front() != '(' && !ascii_iequals(first, "not") &&
                           !ascii_iequals(first, "and") && !ascii_iequals(first, "or");
        if (want_name) { return c.string(named ? std::string{first} : std::string{}); }
        const std::string_view whole = rule->prelude;
        return c.string(collapse_whitespace(named ? whole.substr(at) : whole));
    };
    getter(container_proto, "containerName",
           [container_part](context & c, std::span<value>) { return container_part(c, true); });
    getter(container_proto, "containerQuery",
           [container_part](context & c, std::span<value>) { return container_part(c, false); });
    declaration_accessor(interface("CSSFontFaceRule", "CSSRule", nullptr));

    // --- CSSImportRule, CSSOM 6.4.7
    //
    // `styleSheet` is the sheet load_imported_sheet fetched when the rule was
    // parsed - a CSSStyleSheet of its own with its own rules, as every engine
    // answers, and null only for an import a constructed sheet was given.
    script::object_object * import_proto = interface("CSSImportRule", "CSSRule", nullptr);
    getter(import_proto, "href", [this](context & c, std::span<value>) {
        const css_rule_record * rule = receiver_rule(c);
        return c.string(rule == nullptr ? std::string{} : rule->selector);
    });
    getter(import_proto, "styleSheet", [this](context & c, std::span<value>) {
        const css_rule_record * rule = receiver_rule(c);
        if (rule == nullptr || rule->imported_sheet >= css_sheets_.size()) { return value::null(); }
        return sheet_object_for(c, rule->imported_sheet);
    });
    accessor(
        import_proto, "media",
        [this](context & c, std::span<value>) {
            script::object_object * self = as_object(c.current_this());
            if (self == nullptr || receiver_rule(c) == nullptr) { return value::undefined(); }
            return media_list_object(c, *self);
        },
        [](context & c, std::span<value> a) {
            const value list = c.lookup_property(c.current_this(), "media");
            c.store_property(list, "mediaText", a.empty() ? c.string("") : a[0]);
            return value::undefined();
        });
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
    getter(import_proto, "layerName",
           [import_part](context & c, std::span<value>) { return import_part(c, "layer", true); });
    getter(import_proto, "supportsText", [import_part](context & c, std::span<value>) {
        return import_part(c, "supports", false);
    });

    // --- CSSNamespaceRule, CSSOM 6.4.9. A DEFAULT namespace has no prefix, and
    // the empty string is how CSSOM reports that - not `null`.
    script::object_object * namespace_proto = interface("CSSNamespaceRule", "CSSRule", nullptr);
    getter(namespace_proto, "prefix", [this](context & c, std::span<value>) {
        const css_rule_record * rule = receiver_rule(c);
        return c.string(rule == nullptr ? std::string{} : rule->selector);
    });
    getter(namespace_proto, "namespaceURI", [this](context & c, std::span<value>) {
        const css_rule_record * rule = receiver_rule(c);
        return c.string(rule == nullptr ? std::string{} : rule->prelude);
    });

    // --- CSSPageRule. A grouping rule in CSSOM's current draft and a plain
    // CSSRule in the 2011 one; the draft is what Chrome exposes.
    script::object_object * page_proto = interface("CSSPageRule", "CSSGroupingRule", nullptr);
    declaration_accessor(page_proto);
    accessor(
        page_proto, "selectorText",
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
        });

    // --- CSSKeyframesRule and CSSKeyframeRule
    script::object_object * keyframes_proto = interface("CSSKeyframesRule", "CSSRule", nullptr);
    accessor(
        keyframes_proto, "name",
        [this](context & c, std::span<value>) {
            const css_rule_record * rule = receiver_rule(c);
            return c.string(rule == nullptr ? std::string{} : rule->prelude);
        },
        [this](context & c, std::span<value> a) {
            css_rule_record * rule = receiver_rule(c);
            if (rule == nullptr) { return value::undefined(); }
            rule->prelude = collapse_whitespace(arg_string(c, a, 0));
            style_sheets_changed();
            return value::undefined();
        });
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
    getter(keyframes_proto, "cssRules",
           [keyframes_list](context & c, std::span<value>) { return keyframes_list(c); });
    getter(keyframes_proto, "length", [this](context & c, std::span<value>) {
        const css_rule_record * rule = receiver_rule(c);
        return value::number(rule == nullptr ? 0 : static_cast<double>(rule->children.size()));
    });
    // `appendRule` takes a whole keyframe and `deleteRule`/`findRule` take a
    // keyText - NOT an index, which is what makes this trio different from
    // every other insert/delete pair in the CSSOM.
    method(keyframes_proto, "appendRule", [this, keyframes_list](context & c, std::span<value> a) {
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
    });
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
    method(keyframes_proto, "findRule", [this, keyframe_at](context & c, std::span<value> a) {
        css_rule_record * rule = receiver_rule(c);
        // "Return the LAST rule that matches", which is why the search above
        // walks backwards: a keyframes rule may name the same key twice.
        const std::size_t found = keyframe_at(c, collapse_whitespace(arg_string(c, a, 0)));
        if (rule == nullptr || found == no_index) { return value::null(); }
        return rule_object_for(c, rule->children[found]);
    });
    method(keyframes_proto, "deleteRule",
           [this, keyframe_at, keyframes_list](context & c, std::span<value> a) {
               css_rule_record * rule = receiver_rule(c);
               const std::size_t found = keyframe_at(c, collapse_whitespace(arg_string(c, a, 0)));
               if (rule == nullptr || found == no_index) { return value::undefined(); }
               detach_rule(css_rule_store_, rule->children[found]);
               rule->children.erase(rule->children.begin() + static_cast<std::ptrdiff_t>(found));
               (void)keyframes_list(c);
               style_sheets_changed();
               return value::undefined();
           });
    script::object_object * keyframe_proto = interface("CSSKeyframeRule", "CSSRule", nullptr);
    declaration_accessor(keyframe_proto);
    accessor(
        keyframe_proto, "keyText",
        [this](context & c, std::span<value>) {
            const css_rule_record * rule = receiver_rule(c);
            return c.string(rule == nullptr ? std::string{} : rule->selector);
        },
        [this](context & c, std::span<value> a) {
            css_rule_record * rule = receiver_rule(c);
            if (rule == nullptr) { return value::undefined(); }
            rule->selector = collapse_whitespace(arg_string(c, a, 0));
            style_sheets_changed();
            return value::undefined();
        });
    declaration_accessor(interface("CSSCounterStyleRule", "CSSRule", nullptr));

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
    script::object_object * declaration_proto = interface("CSSStyleDeclaration", nullptr, nullptr);
    iterable(declaration_proto);
    const auto property_accessor = [&](script::object_object * on, const std::string & idl,
                                       const std::string & css) {
        on->define_accessor(
            idl,
            value::object(cx.allocate<script::native_object>(
                "get " + idl,
                [this, css](context & c, std::span<value>) {
                    const css_rule_record * rule = receiver_rule(c);
                    if (rule == nullptr) { return c.string(""); }
                    for (const css_declaration & declared : rule->declarations) {
                        if (declared.name == css) { return c.string(declared.value); }
                    }
                    return c.string("");
                })),
            value::object(cx.allocate<script::native_object>(
                "set " + idl,
                [this, css](context & c, std::span<value> a) {
                    css_rule_record * rule = receiver_rule(c);
                    if (rule == nullptr) { return value::undefined(); }
                    if (store_declaration(*rule, css, arg_string(c, a, 0), false, false)) {
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
        interface("CSSStyleProperties", "CSSStyleDeclaration", nullptr);
    for (const style::css::property_syntax & property : known_properties()) {
        both_spellings(properties_proto, property.name);
    }
    // CSS Fonts 4 §11.1 and CSS Paged Media 3 §7.1: the descriptor sets, by
    // name - the property table knows none of them, so a value is kept as the
    // author wrote it, which is what a descriptor's grammar this engine does
    // not model amounts to.
    script::object_object * font_face_proto =
        interface("CSSFontFaceDescriptors", "CSSStyleDeclaration", nullptr);
    for (const std::string_view name :
         {"ascent-override", "descent-override", "font-display", "font-family",
          "font-feature-settings", "font-language-override", "font-named-instance", "font-stretch",
          "font-style", "font-weight", "font-width", "font-variation-settings", "line-gap-override",
          "size-adjust", "src", "unicode-range"}) {
        both_spellings(font_face_proto, name);
    }
    script::object_object * page_descriptors_proto =
        interface("CSSPageDescriptors", "CSSStyleDeclaration", nullptr);
    for (const std::string_view name :
         {"margin", "margin-top", "margin-right", "margin-bottom", "margin-left", "size",
          "page-orientation", "marks", "bleed"}) {
        both_spellings(page_descriptors_proto, name);
    }
    getter(declaration_proto, "length", [this](context & c, std::span<value>) {
        const css_rule_record * rule = receiver_rule(c);
        return value::number(rule == nullptr ? 0 : static_cast<double>(rule->declarations.size()));
    });
    getter(declaration_proto, "parentRule", [this](context & c, std::span<value>) {
        script::object_object * self = as_object(c.current_this());
        const std::size_t at = slot_index(self, rule_key);
        if (at >= css_rule_store_.size()) { return value::null(); }
        return rule_object_for(c, at);
    });
    accessor(
        declaration_proto, "cssText",
        [this](context & c, std::span<value>) {
            const css_rule_record * rule = receiver_rule(c);
            return c.string(rule == nullptr ? std::string{} : serialize_block(rule->declarations));
        },
        [this](context & c, std::span<value> a) {
            css_rule_record * rule = receiver_rule(c);
            if (rule == nullptr) { return value::undefined(); }
            // A WHOLE-BLOCK REPLACEMENT, not a merge, and through the same
            // declaration-list parser a `style` attribute goes through - so a
            // `;` inside a string cannot end a declaration here either.
            rule->declarations.clear();
            parse_declarations_into(*rule, arg_string(c, a, 0), *atoms_);
            refresh_declaration_object(c, c.current_this());
            style_sheets_changed();
            return value::undefined();
        });
    method(declaration_proto, "item", [](context & c, std::span<value> a) {
        const value held = collection_item(c, a);
        // CSSOM's `item` answers the EMPTY STRING past the end here, unlike the
        // collections above whose `item` answers null.
        return held.is_null() ? c.string("") : held;
    });
    method(declaration_proto, "getPropertyValue", [this](context & c, std::span<value> a) {
        const css_rule_record * rule = receiver_rule(c);
        if (rule == nullptr) { return c.string(""); }
        const std::string name = asked_name(c, a);
        for (const css_declaration & declared : rule->declarations) {
            if (declared.name == name) { return c.string(declared.value); }
        }
        return c.string("");
    });
    method(declaration_proto, "getPropertyPriority", [this](context & c, std::span<value> a) {
        const css_rule_record * rule = receiver_rule(c);
        if (rule == nullptr) { return c.string(""); }
        const std::string name = asked_name(c, a);
        for (const css_declaration & declared : rule->declarations) {
            if (declared.name == name) { return c.string(declared.important ? "important" : ""); }
        }
        return c.string("");
    });
    method(declaration_proto, "setProperty", [this](context & c, std::span<value> a) {
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
        const std::string priority = a.size() > 2 && a[2].is_undefined() ? std::string{} : text(2);
        // "If priority is not the empty string and is not an ASCII
        // case-insensitive match for 'important', return" - CSSOM 6.7.2.
        if (!priority.empty() && !ascii_iequals(priority, "important")) {
            return value::undefined();
        }
        if (store_declaration(*rule, asked_name(c, a), text(1), false, !priority.empty())) {
            refresh_declaration_object(c, c.current_this());
            style_sheets_changed();
        }
        return value::undefined();
    });
    method(declaration_proto, "removeProperty", [this](context & c, std::span<value> a) {
        css_rule_record * rule = receiver_rule(c);
        if (rule == nullptr) { return c.string(""); }
        const std::string name = asked_name(c, a);
        std::string was;
        for (std::size_t i = 0; i < rule->declarations.size(); ++i) {
            if (rule->declarations[i].name != name) { continue; }
            was = rule->declarations[i].value;
            rule->declarations.erase(rule->declarations.begin() + static_cast<std::ptrdiff_t>(i));
            break;
        }
        refresh_declaration_object(c, c.current_this());
        style_sheets_changed();
        return c.string(was);
    });
}

} // namespace ctbrowser::shell
