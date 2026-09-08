// dom_bindings - the inline-style declaration store behind `element.style`:
// what counts as a declaration, how one is written through the value grammar,
// and how the store serialises back to the `style` attribute and to `cssText`.
//
// One of twelve files carved out of a 5,442-line bindings/element.cpp on
// 2026-09-08 - which was itself one of six carved out of bindings.cpp on
// 2026-08-09. All are member functions of one class declared in
// include/ctbrowser/shell/bindings.hpp; the helpers more than one of them
// needs are declared in internal.hpp beside this, with external linkage in
// ctbrowser::shell::detail. Nothing about the public header changed.

#include "internal.hpp"

namespace ctbrowser::shell {

using namespace detail;

namespace {

// `backgroundColor` -> `background-color`. The IDL name and the CSS name are
// different spellings of the same property, and the attribute the style engine
// parses wants the CSS one.
//
// THE CONVERSION MOVED to style/css/properties.hpp, where the property table
// is: it had a second copy inside `computed_style.cpp`'s `getPropertyValue`,
// and both of them got `-webkit-transform` wrong in the same way - the IDL name
// drops the prefix's leading dash, so a plain camel-to-hyphen loop produces
// `webkit-transform` and finds nothing.
using style::css::css_name_of;

// ONE WRITE THROUGH THE VALUE GRAMMAR. Every path into the declaration store -
// `el.style.color = x`, `setProperty`, `cssText`, and the seed from the
// element's own `style` attribute - goes through here, so there is exactly one
// answer to "is this valid" and exactly one canonical form.
//
// An INVALID value is a NO-OP, which is what CSSOM §6.7.2 says and what
// `test_invalid_value` measures: the test clears the property, sets the bad
// value, and asserts the read is `""`. Refusing the write is the whole test.
// Before this, `el.style` recorded whatever it was given and handed it back
// unchanged - `expected "" but got "round()"`, ~600 subtests of `css-values`.
// THE PRIORITY RIDES IN THE STORED STRING, and every read strips it. There is
// nowhere else for it to go: the store IS the declaration list, a JS object of
// name to value, and a parallel table keyed on the element would be a second
// thing to keep in step with the first. `important_suffix` is what the two ends
// agree on, `declared_value` takes it off and `declared_priority` reads it.
//
// It has to be kept at all. `style="width: 100px !important"` is ordinary CSS,
// and refusing the value would DROP the declaration - the inline width simply
// stops applying - which is a great deal worse than mis-reporting a priority.
constexpr std::string_view important_suffix = " !important";

} // namespace

namespace detail {

// Whether a key on the declaration store is a DECLARATION rather than one of
// the methods sharing the object with them. A method is a callable and is
// skipped on that ground alone; `length`, `cssText` and the indexed properties
// are answered by the proxy and never stored, which is what keeps this test to
// one condition.
[[nodiscard]] bool is_declaration(const value & v) {
    return !v.is_nullish() && !v.is_callable();
}

// The declarations an object holds, as a `style` attribute. Serialising the
// whole object on every write is what keeps the two representations from
// drifting: there is one source of truth, the object, and the attribute is
// derived from it.
std::string style_attribute(script::object_object & held, context & cx) {
    std::string out;
    for (const auto & [name, v] : held.props) {
        // `setProperty` and friends live on the same object, and a CSS value is
        // never a function - without this the methods serialise themselves into
        // the attribute as `set-property: function;`.
        if (!is_declaration(v)) { continue; }
        const std::string text = cx.to_string(v);
        // Assigning "" REMOVES a declaration, which is how a page turns one
        // off - emitting `display: ;` instead would leave the old value in
        // place as far as the parser is concerned.
        if (text.empty()) { continue; }
        out += css_name_of(name);
        out += ": ";
        out += text;
        out += "; ";
    }
    return out;
}

// `cssText`: the same declarations, without the trailing space CSSOM does not
// ask for. A separate function from the one above because the ATTRIBUTE is a
// derived artefact the style engine re-parses and `cssText` is an answer to
// script, and the two have drifted before.
std::string css_text_of(script::object_object & held, context & cx) {
    std::string out;
    for (const auto & [name, v] : held.props) {
        if (!is_declaration(v)) { continue; }
        const std::string text = cx.to_string(v);
        if (text.empty()) { continue; }
        if (!out.empty()) { out += ' '; }
        out += css_name_of(name);
        out += ": ";
        out += text;
        out += ';';
    }
    return out;
}

[[nodiscard]] std::string_view declared_value(std::string_view stored) {
    return stored.ends_with(important_suffix)
               ? stored.substr(0, stored.size() - important_suffix.size())
               : stored;
}

[[nodiscard]] std::string_view declared_priority(std::string_view stored) {
    return stored.ends_with(important_suffix) ? std::string_view{"important"} : std::string_view{};
}

bool store_declaration(script::object_object & held, context & cx, const std::string & css_name,
                       std::string_view text, bool allow_important, bool force_important) {
    const style::css::value_check checked =
        style::css::check_declaration(css_name, text, allow_important);
    if (!checked.valid) {
        // An empty value REMOVES the declaration; anything else that fails to
        // parse leaves the old one exactly where it was.
        if (trim(text, html_whitespace).empty()) {
            held.erase(css_name);
            return true;
        }
        return false;
    }
    std::string stored = checked.serialized;
    if (checked.important || force_important) { stored += important_suffix; }
    held.set(css_name, cx.string(stored));
    return true;
}

// The declarations already in a `style` attribute, so a write through
// `el.style` extends what the author wrote rather than replacing it. The whole
// object is re-serialised on every write, so anything not read back here is
// LOST on the first assignment - which silently deleted the width and height an
// element was sized by.
void seed_declarations(script::object_object & held, context & cx, std::string_view text) {
    std::size_t i = 0;
    while (i < text.size()) {
        const std::size_t colon = text.find(':', i);
        if (colon == std::string_view::npos) { break; }
        std::size_t end = text.find(';', colon);
        if (end == std::string_view::npos) { end = text.size(); }
        const std::string_view name = trim(text.substr(i, colon - i), html_whitespace);
        const std::string_view v = trim(text.substr(colon + 1, end - colon - 1), html_whitespace);
        // THROUGH THE SAME GRAMMAR as a write from script, so the object a page
        // reads back cannot disagree with the attribute it was built from - and
        // so an invalid declaration in the markup is dropped here rather than
        // surviving as a value no engine would compute.
        if (!name.empty()) { store_declaration(held, cx, ascii_lower_copy(name), v, true, false); }
        i = end + 1;
    }
}

} // namespace detail

} // namespace ctbrowser::shell
