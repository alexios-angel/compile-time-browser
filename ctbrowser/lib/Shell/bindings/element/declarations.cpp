// dom_bindings - the inline-style declaration store behind `element.style`:
// what counts as a declaration, how one is written, and how the store
// serialises back to the `style` attribute and to `cssText`.
//
// THE STORE IS A JS OBJECT of name to value and THE ALGORITHMS ARE
// style/css/properties.hpp's: every operation loads the object into a
// `declaration_block`, runs the CSSOM §6.6 step there - which is where a
// shorthand becomes its longhands and folds back - and writes the object out
// again in the block's order. `rule.style` runs the same functions over its
// own vector, so the two blocks cannot disagree about one property.

#include "internal.hpp"

namespace ctbrowser::shell {

using namespace detail;

namespace {

using style::css::declaration_block;

// THE PRIORITY RIDES IN THE STORED STRING, and every read strips it. There is
// nowhere else for it to go: the store IS the declaration list, and a parallel
// table keyed on the element would be a second thing to keep in step.
constexpr std::string_view important_suffix = " !important";

[[nodiscard]] declaration_block load(script::object_object & held, context & cx) {
    declaration_block block;
    for (const auto & [name, v] : held.props) {
        if (!is_declaration(v)) { continue; }
        const std::string stored = cx.to_string(v);
        block.push_back(style::css::declaration{name, std::string{declared_value(stored)},
                                                !declared_priority(stored).empty()});
    }
    return block;
}

// The METHODS stay: `setProperty` and its siblings live on the same object,
// so only the declarations are replaced, in the block's order.
void save(script::object_object & held, context & cx, const declaration_block & block) {
    std::vector<std::string> declared;
    for (const auto & [key, v] : held.props) {
        if (is_declaration(v)) { declared.push_back(key); }
    }
    for (const std::string & key : declared) { held.erase(key); }
    for (const style::css::declaration & d : block) {
        std::string stored = d.value;
        if (d.important) { stored += important_suffix; }
        held.set(d.name, cx.string(stored));
    }
}

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

// The declarations an object holds, as a `style` attribute. CSSOM's "update
// style attribute for" (6.7.1) writes the SERIALISED block, which is the same
// string `cssText` answers.
std::string style_attribute(script::object_object & held, context & cx) {
    return css_text_of(held, cx);
}

// `cssText`: 6.6 "serialize a CSS declaration block".
std::string css_text_of(script::object_object & held, context & cx) {
    return style::css::serialize_declaration_block(load(held, cx));
}

[[nodiscard]] std::string_view declared_value(std::string_view stored) {
    return stored.ends_with(important_suffix)
               ? stored.substr(0, stored.size() - important_suffix.size())
               : stored;
}

[[nodiscard]] std::string_view declared_priority(std::string_view stored) {
    return stored.ends_with(important_suffix) ? std::string_view{"important"} : std::string_view{};
}

// `getPropertyValue` and `getPropertyPriority`: a shorthand answers from its
// longhands.
std::string read_declaration(script::object_object & held, context & cx, std::string_view name) {
    return style::css::declaration_value(load(held, cx), name);
}

std::string read_priority(script::object_object & held, context & cx, std::string_view name) {
    return style::css::declaration_priority(load(held, cx), name);
}

// `setProperty` and the IDL setter: "set a CSS declaration", with a shorthand
// landing as its longhands. An invalid value is a NO-OP, which is what CSSOM
// §6.7.2 says and what `test_invalid_value` measures; an empty one removes.
// Answers whether the block changed, which is when the attribute is rewritten.
bool store_declaration(script::object_object & held, context & cx, const std::string & css_name,
                       std::string_view text, bool important) {
    declaration_block block = load(held, cx);
    if (!style::css::set_declaration(block, css_name, text, important)) { return false; }
    save(held, cx, block);
    return true;
}

// `removeProperty`: the value it removed, and whether anything left.
std::string remove_stored_declaration(script::object_object & held, context & cx,
                                      std::string_view name, bool & removed) {
    declaration_block block = load(held, cx);
    std::string was = style::css::remove_declaration(block, name, removed);
    if (removed) { save(held, cx, block); }
    return was;
}

// The declarations in a `style` attribute or a `cssText` write, through the
// same declaration-list parser the cascade uses - so a `;` inside a string
// cannot end a declaration here either, and an invalid declaration in the
// markup is dropped here rather than surviving as a value nothing computes.
void seed_declarations(script::object_object & held, context & cx, std::string_view text) {
    declaration_block block = load(held, cx);
    style::css::parse_declaration_block(block, text);
    save(held, cx, block);
}

} // namespace detail

} // namespace ctbrowser::shell
