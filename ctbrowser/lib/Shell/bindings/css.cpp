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

#include <ctbrowser/shell/bindings.hpp>

#include <ctbrowser/core/algorithms.hpp>
#include <ctbrowser/style/css/properties.hpp>

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
    method("escape", 1, [](context & c, std::span<value> args) {
        if (args.empty()) {
            c.throw_error("TypeError", "CSS.escape requires an argument");
            return value::undefined();
        }
        // CSSOM §2.1's "serialize an identifier", which is exactly what a
        // selector's type, id, class and attribute names are serialised with
        // too - so it lives on dom_bindings and bindings/stylesheets/rules.cpp
        // owns it. Two copies of an escape are two answers to "what is a valid
        // identifier", and a page building `'#' + CSS.escape(id)` and this
        // engine printing that same rule back must agree.
        return c.string(dom_bindings::serialize_css_identifier(c.to_string(args[0])));
    });

    css_interface_ = value::object(css);
    cx.define_global("CSS", css_interface_);
}

} // namespace ctbrowser::shell
