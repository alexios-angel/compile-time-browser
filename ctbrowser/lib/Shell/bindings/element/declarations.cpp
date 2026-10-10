//===- declarations.cpp - Inline style declaration store ------------------===//
//
// The inline-style declaration store behind `element.style`: what counts as a
// declaration, how one is written, and how the store serialises back to the
// `style` attribute and to `cssText`.
//
// The store is a JS object mapping property name to value, and the algorithms
// are style/css/properties.hpp's: every operation loads the object into a
// `declaration_block`, runs the CSSOM 6.6 step there (where a shorthand
// becomes its longhands and folds back), and writes the object out again in
// the block's order. `rule.style` runs the same functions over its own vector,
// so the two blocks cannot disagree about one property.
//
//===----------------------------------------------------------------------===//

#include "internal.hpp"

namespace ctbrowser::shell {

using namespace detail;

namespace {

using style::css::declaration_block;

/// The priority is appended to the stored string, and every read strips it.
/// The store is the declaration list, so a parallel priority table keyed on the
/// element would be a second thing to keep in step.
constexpr std::string_view important_suffix = " !important";

/// Builds a declaration_block from the string-valued (declaration) keys of
/// the store, in the store's order.
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

/// Writes `block` back into the store. The methods stay: `setProperty` and its
/// siblings live on the same object, so only the declarations are replaced,
/// in the block's order.
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

/// Whether a key on the declaration store is a declaration rather than one of
/// the methods sharing the object, or the expando table. A declaration is
/// stored as a string (see save()); a method is a callable and the expandos
/// live under one object key, so both fail the one test. `length`, `cssText`
/// and the indexed properties are answered by the proxy and never stored.
[[nodiscard]] bool is_declaration(const value & v) {
    return v.is_string();
}

/// Returns the object holding the store's expandos, creating it on first use.
/// CSSOM 6.7.2 gives `el.style` an IDL attribute for every supported property
/// and nothing else, so `el.style.unknown = x` and `el.style.COLOR = 'red'`
/// (`-c-o-l-o-r`, an unsupported name) create ordinary properties that never
/// reach cssText (WPT cssstyledeclaration-csstext.html). They are kept on one
/// object beside the declarations rather than among them, because among them a
/// string could not be told from a declaration.
script::object_object & expandos_of(script::object_object & held, context & cx) {
    if (const value * found = held.find(std::string{expando_key}); found && found->is_object()) {
        return *static_cast<script::object_object *>(found->as_heap());
    }
    auto * made = cx.allocate<script::object_object>();
    held.set(std::string{expando_key}, value::object(made));
    return *made;
}

/// The declarations an object holds, as a `style` attribute. CSSOM's "update
/// style attribute for" (6.7.1) writes the serialised block, which is the same
/// string `cssText` answers.
std::string style_attribute(script::object_object & held, context & cx) {
    return css_text_of(held, cx);
}

/// `cssText`: CSSOM 6.6 "serialize a CSS declaration block".
std::string css_text_of(script::object_object & held, context & cx) {
    return style::css::serialize_declaration_block(load(held, cx));
}

/// The value part of a stored declaration, without the priority suffix.
[[nodiscard]] std::string_view declared_value(std::string_view stored) {
    return stored.ends_with(important_suffix)
               ? stored.substr(0, stored.size() - important_suffix.size())
               : stored;
}

/// "important" when the stored declaration carries the priority, else "".
[[nodiscard]] std::string_view declared_priority(std::string_view stored) {
    return stored.ends_with(important_suffix) ? std::string_view{"important"} : std::string_view{};
}

/// `getPropertyValue`; with read_priority, `getPropertyPriority`. A shorthand
/// answers from its longhands.
std::string read_declaration(script::object_object & held, context & cx, std::string_view name) {
    return style::css::declaration_value(load(held, cx), name);
}

std::string read_priority(script::object_object & held, context & cx, std::string_view name) {
    return style::css::declaration_priority(load(held, cx), name);
}

/// `setProperty` and the IDL setter: "set a CSS declaration", with a shorthand
/// stored as its longhands. An invalid value is a no-op (CSSOM 6.7.2); an
/// empty one removes. Returns whether the block changed, which is when the
/// caller rewrites the attribute.
bool store_declaration(script::object_object & held, context & cx, const std::string & css_name,
                       std::string_view text, bool important) {
    declaration_block block = load(held, cx);
    if (!style::css::set_declaration(block, css_name, text, important)) { return false; }
    save(held, cx, block);
    return true;
}

/// `removeProperty`: returns the value it removed and sets `removed` when a
/// declaration was removed.
std::string remove_stored_declaration(script::object_object & held, context & cx,
                                      std::string_view name, bool & removed) {
    declaration_block block = load(held, cx);
    std::string was = style::css::remove_declaration(block, name, removed);
    if (removed) { save(held, cx, block); }
    return was;
}

/// Parses the declarations in a `style` attribute or a `cssText` write into the
/// store, through the same declaration-list parser the cascade uses: a `;`
/// inside a string cannot end a declaration here either, and an invalid
/// declaration in the markup is dropped rather than kept as a value nothing
/// computes.
void seed_declarations(script::object_object & held, context & cx, std::string_view text) {
    declaration_block block = load(held, cx);
    style::css::parse_declaration_block(block, text);
    save(held, cx, block);
}

} // namespace detail

} // namespace ctbrowser::shell
