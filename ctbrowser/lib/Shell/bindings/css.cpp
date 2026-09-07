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

#include <cstdint>
#include <string>
#include <string_view>

namespace ctbrowser::shell {
namespace {

// CSSOM §2.1, "serialize an identifier". `CSS.escape` is how a page builds a
// selector out of an id or a class it did not choose - `querySelector('#' +
// CSS.escape(id))` - and it is exact rather than approximate work: escaping too
// little produces a selector that means something else, escaping too much
// produces one that matches nothing.
[[nodiscard]] std::string escape_identifier(std::string_view text) {
    std::string out;
    const auto hex_escape = [&out](unsigned char c) {
        static constexpr char digits[] = "0123456789abcdef";
        out += '\\';
        if (c >= 16) { out += digits[c >> 4]; }
        out += digits[c & 0xF];
        out += ' ';
    };
    for (std::size_t i = 0; i < text.size(); ++i) {
        const auto c = static_cast<unsigned char>(text[i]);
        // NULL is not escaped, it is REPLACED - §2.1 step 3, the same
        // U+FFFD substitution the CSS tokenizer does to its input.
        if (c == 0) {
            out += "\xEF\xBF\xBD";
            continue;
        }
        if (c <= 0x1F || c == 0x7F) {
            hex_escape(c);
            continue;
        }
        // A LEADING DIGIT, or a digit after a leading `-`, would make the
        // identifier a number: both are escaped numerically rather than with a
        // backslash, because `\1` is not a valid identifier start either.
        if (c >= '0' && c <= '9' && (i == 0 || (i == 1 && text[0] == '-'))) {
            hex_escape(c);
            continue;
        }
        if (c == '-' && text.size() == 1) {
            out += "\\-";
            continue;
        }
        if (c >= 0x80 || c == '-' || c == '_' || (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z')) {
            out += static_cast<char>(c);
            continue;
        }
        out += '\\';
        out += static_cast<char>(c);
    }
    return out;
}

} // namespace

void dom_bindings::install_css_interface(context & cx) {
    auto * css = static_cast<script::object_object *>(cx.make_object().as_heap());
    const auto method = [&](std::string name, script::native_fn fn) {
        css->define(name, value::object(cx.allocate<script::native_object>(name, std::move(fn))),
                    script::attr_builtin);
    };

    // BOTH ARITIES, because they are different questions. One argument is a
    // `<supports-condition>` - `(display: flex) and (color: red)`, parentheses
    // and all - and two are a property and a value with no parentheses. A page
    // that passes one string containing a colon is asking the first question,
    // and answering it as though it were the second is how `CSS.supports('(a:
    // b)')` comes back true for the property `(a`.
    method("supports", [](context & c, std::span<value> args) {
        if (args.empty()) { return value::boolean(false); }
        if (args.size() == 1) {
            return value::boolean(style::css::supports_condition(c.to_string(args[0])));
        }
        return value::boolean(style::css::supports_declaration(
            ascii_lower_copy(c.to_string(args[0])), c.to_string(args[1])));
    });
    method("escape", [](context & c, std::span<value> args) {
        if (args.empty()) { return c.string(std::string{"undefined"}); }
        return c.string(escape_identifier(c.to_string(args[0])));
    });

    css_interface_ = value::object(css);
    cx.define_global("CSS", css_interface_);
}

} // namespace ctbrowser::shell
