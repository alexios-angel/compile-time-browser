//===- lib/Shell/bindings/css.cpp - The CSS namespace object ----*- C++ -*-===//
//
// dom_bindings: the `CSS` namespace object, with CSS.supports,
// CSS.registerProperty and CSS.escape. CSS.supports matters most: every
// `css/css-values` WPT test that uses `test_computed_value` first asserts
//
//     assert_true(CSS.supports(property, specified), "'…' is a supported value")
//
// so without it those tests fail on their first line.
//
// The answers come from `style/css/properties.hpp`, which owns the property
// table and the value grammar; this file is only the binding, so
// `CSS.supports` and `el.style` cannot disagree about whether a value is valid.
//
//===----------------------------------------------------------------------===//

#include "stylesheets/internal.hpp"

#include <ctbrowser/core/algorithms.hpp>
#include <ctbrowser/style/css/properties.hpp>
#include <ctbrowser/style/css/substitute.hpp>
#include <ctbrowser/style/engine.hpp>

#include <string>

namespace ctbrowser::shell {

void dom_bindings::install_css_interface(context & cx) {
    auto * css = cx.allocate<script::object_object>();
    // `length` is the number of required arguments: 1 for every method here
    // (`supports` is an overload pair whose shorter form takes one argument).
    // `escape.html` asserts it.
    const auto method = [&](std::string name, double arity, script::native_fn fn) {
        auto * made = cx.allocate<script::native_object>(name, std::move(fn));
        made->define("length", value::number(arity), script::attr_configurable);
        css->define(name, value::object(made), script::attr_builtin);
    };
    // A namespace object carries its own `@@toStringTag` (Web IDL §3.13), so
    // `CSS.toString()` is `[object CSS]`. Own, non-writable, configurable: a
    // page may redefine or delete it, and `CSS-namespace-object-class-string`
    // does both.
    css->define(CTBROWSER_SYMBOL_KEY("toStringTag"), cx.string("CSS"), script::attr_configurable);

    // The two arities ask different questions. One argument is a
    // `<supports-condition>` (`(display: flex) and (color: red)`, parentheses
    // and all); two are a property and a value with no parentheses. Treating a
    // one-argument call as the second form would make `CSS.supports('(a: b)')`
    // true for a property named `(a`.
    //
    // The one-argument form has a second chance `@supports` does not: CSS
    // Conditional 4 §6.1 step 2 tries the text again wrapped in parentheses,
    // so `CSS.supports('color: red')` is true while `@supports color: red`
    // stays invalid. That is why the retry is here and not in the grammar.
    method("supports", 1, [](context & c, std::span<value> args) {
        if (args.empty()) { return value::boolean(false); }
        if (args.size() == 1) {
            const std::string text = c.to_string(args[0]);
            return value::boolean(style::css::supports_condition(text) ||
                                  style::css::supports_condition("(" + text + ")"));
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
            // The initial value must also be computationally independent (CSS
            // Properties and Values 1 §1.2): a unit measuring a font or a query
            // container needs an element, and an initial value has none.
            const style::css::token_stream tokens = style::css::tokenize(made.initial);
            for (const style::css::css_token & t : tokens.tokens) {
                if (t.type != style::css::token_type::dimension) { continue; }
                const std::string unit = ascii_lower_copy(tokens.unit_of(t));
                if (unit.starts_with("cq") ||
                    ascii_iequals_any(unit, {"em", "ex", "cap", "ch", "ic", "lh", "rem", "rex",
                                             "rcap", "rch", "ric", "rlh"})) {
                    throw_dom_exception(c, "SyntaxError",
                                        "the initialValue is not computationally independent");
                    return value::undefined();
                }
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
        // CSSOM §2.1's "serialize an identifier": style/'s one copy, the same
        // one serialised selectors use for type, id, class and attribute names,
        // so a page building `'#' + CSS.escape(id)` and this engine printing
        // that rule back agree.
        return c.string(style::css::serialize_identifier(c.to_string(args[0])));
    });

    css_interface_ = value::object(css);
    cx.define_builtin_global("CSS", css_interface_);
}

} // namespace ctbrowser::shell
