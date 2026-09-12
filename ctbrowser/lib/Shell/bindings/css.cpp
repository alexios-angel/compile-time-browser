// dom_bindings - the `CSS` namespace object.
//
// TWO STATIC METHODS, AND ONE OF THEM GATES A WHOLE SUITE. Every test in
// `css/css-values` that uses `test_computed_value` opens with
//
//     assert_true(CSS.supports(property, specified), "'…' is a supported value")
//
// before it looks at a computed value at all, so an absent `CSS` is a failure on
// line one of ~15 files and of every `@supports`-shaped assertion elsewhere. It
// is listed in `docs/css-conformance.md` §4 as "a third, smaller one" beside the
// two Shell-side gaps, and it is the only one of the three that is a missing
// object rather than a missing behaviour.
//
// The answers come from `style/css/properties.hpp`, which owns the property
// table and the value grammar: this file is the binding and nothing else, so
// `CSS.supports` and `el.style` can never disagree about whether a value is
// valid.

#include "stylesheets/internal.hpp"

#include <ctbrowser/core/algorithms.hpp>
#include <ctbrowser/style/css/media.hpp>
#include <ctbrowser/style/css/properties.hpp>
#include <ctbrowser/style/css/substitute.hpp>
#include <ctbrowser/style/engine.hpp>

#include <string>

namespace ctbrowser::shell {

void dom_bindings::install_css_interface(context & cx) {
    auto * css = static_cast<script::object_object *>(cx.make_object().as_heap());
    // `length` is the number of REQUIRED arguments - one for both, `supports`
    // being an overload pair whose shorter form takes one. `escape.html`
    // asserts it beside the TypeError a call with no argument throws.
    const auto method = [&](std::string name, double arity, script::native_fn fn) {
        auto * made = cx.allocate<script::native_object>(name, std::move(fn));
        made->define("length", value::number(arity), script::attr_configurable);
        css->define(name, value::object(made), script::attr_builtin);
    };
    // A NAMESPACE OBJECT carries its own `@@toStringTag` - Web IDL §3.13 - so
    // `CSS.toString()` is `[object CSS]`. Own, non-writable, configurable: a
    // page may redefine or delete it, and `CSS-namespace-object-class-string`
    // does both.
    css->define("@@toStringTag", cx.string("CSS"), script::attr_configurable);

    // BOTH ARITIES, because they are different questions. One argument is a
    // `<supports-condition>` - `(display: flex) and (color: red)`, parentheses
    // and all - and two are a property and a value with no parentheses. A page
    // that passes one string containing a colon is asking the first question,
    // and answering it as though it were the second is how `CSS.supports('(a:
    // b)')` comes back true for the property `(a`.
    method("supports", 1, [](context & c, std::span<value> args) {
        if (args.empty()) { return value::boolean(false); }
        if (args.size() == 1) {
            return value::boolean(style::css::supports_condition(c.to_string(args[0])));
        }
        return value::boolean(style::css::supports_declaration(
            ascii_lower_copy(c.to_string(args[0])), c.to_string(args[1])));
    });
    // `CSS.registerProperty({ name, syntax, inherits, initialValue })`, CSS
    // Properties and Values API 1 §3. The same registration a sheet's
    // `@property` makes, through the cascade's own engine, so the two cannot
    // disagree about a name; the errors are the specification's: a name that
    // is not a custom property or a syntax that does not parse is a
    // SyntaxError, a name already taken an InvalidStateError, and a missing
    // required member a TypeError.
    method("registerProperty", 1, [this](context & c, std::span<value> args) {
        if (args.empty() || !args[0].is_object()) {
            c.throw_error("TypeError", "CSS.registerProperty requires a PropertyDefinition");
            return value::undefined();
        }
        const value definition = args[0];
        const value name = c.lookup_property(definition, "name");
        const value inherits = c.lookup_property(definition, "inherits");
        if (name.is_undefined() || inherits.is_undefined()) {
            c.throw_error("TypeError", "PropertyDefinition needs a name and inherits");
            return value::undefined();
        }
        style::css::property_registration made;
        const std::string property = c.to_string(name);
        if (!property.starts_with("--")) {
            throw_dom_exception(c, "SyntaxError", "a custom property name starts with --");
            return value::undefined();
        }
        const value syntax = c.lookup_property(definition, "syntax");
        if (!syntax.is_undefined()) {
            made.syntax = std::string{trim(c.to_string(syntax), html_whitespace)};
        }
        made.inherits = context::truthy(inherits);
        const value initial = c.lookup_property(definition, "initialValue");
        if (!initial.is_undefined()) { made.initial = c.to_string(initial); }
        // The syntax must parse, and a value it takes must exist unless it takes
        // anything.
        if (made.syntax != "*") {
            if (initial.is_undefined()) {
                throw_dom_exception(c, "SyntaxError", "a typed property needs an initialValue");
                return value::undefined();
            }
            if (!style::css::compute_registered(made.initial, made.syntax,
                                                style::css::length_context{})) {
                throw_dom_exception(c, "SyntaxError",
                                    "the initialValue does not parse against the syntax");
                return value::undefined();
            }
        }
        if (!selector_engine().register_property(property, std::move(made))) {
            throw_dom_exception(c, "InvalidStateError", property + " is already registered");
            return value::undefined();
        }
        mutated();
        return value::undefined();
    });
    method("escape", 1, [](context & c, std::span<value> args) {
        if (args.empty()) {
            c.throw_error("TypeError", "CSS.escape requires an argument");
            return value::undefined();
        }
        // CSSOM §2.1's "serialize an identifier" - style/'s one copy, the same
        // one a serialised selector's type, id, class and attribute names go
        // through, so a page building `'#' + CSS.escape(id)` and this engine
        // printing that rule back agree.
        return c.string(style::css::serialize_identifier(c.to_string(args[0])));
    });

    css_interface_ = value::object(css);
    cx.define_global("CSS", css_interface_);

    // `matchMedia(query)`, CSSOM View §4.2: a MediaQueryList whose `media` is
    // the list SERIALISED - the same serialiser `MediaList.mediaText` uses, so
    // `(min-width: 10px) and (min-height: 10px)` is neither sorted nor
    // deduplicated (mediaquery-sort-dedup) - and whose `matches` is the same
    // evaluation the cascade gives an `@media` rule, against the same
    // environment. Phaser asks it `(orientation: portrait)` and Babylon
    // `(pointer: fine)`. A bare global, as `getComputedStyle` is: `window` is a
    // proxy that falls back to the globals.
    //
    // ponytail: `matches` is read once and no `change` event ever fires; the
    // listener methods accept and forget. Wire them to the engine's media
    // re-evaluation when a page needs the event.
    cx.define_native("matchMedia", [this](context & c, std::span<value> args) {
        const std::string text = args.empty() ? std::string{} : c.to_string(args[0]);
        auto * list = static_cast<script::object_object *>(c.make_object().as_heap());
        list->set("media", c.string(detail::serialize_media_query_list(
                               detail::parse_media_query_list(text))));
        list->set("matches",
                  value::boolean(style::css::evaluate(style::css::parse_media_query_list(text),
                                                      selector_engine().environment())));
        list->set("onchange", value::null());
        for (const char * name :
             {"addListener", "removeListener", "addEventListener", "removeEventListener"}) {
            list->set(name,
                      value::object(c.allocate<script::native_object>(
                          name, [](context &, std::span<value>) { return value::undefined(); })));
        }
        list->define("@@toStringTag", c.string("MediaQueryList"), script::attr_configurable);
        return value::object(list);
    });
}

} // namespace ctbrowser::shell
