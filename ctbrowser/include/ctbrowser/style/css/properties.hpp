#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

// WHICH PROPERTIES EXIST, WHAT EACH ACCEPTS, AND HOW A VALUE SERIALISES.
//
// This is the piece the CSSOM has never had. `el.style` is a string store: it
// records whatever it is given and hands it back unchanged, so a value is never
// validated and never re-serialised, and `getComputedStyle` publishes a handful
// of names picked by what happened to be declared. Measured in
// `docs/css-conformance.md`, that is the whole of `test_invalid_value`
// (`expected "" but got "round()"`) and the canonical-serialisation half of
// `test_valid_value` - and it is why `CSS.supports` cannot exist at all.
//
// THE TABLE IS DELIBERATELY CONSERVATIVE, and that is the design rather than an
// apology. Three of the vendored corpora and every render golden write through
// `el.style`, so a syntax that is WRONG about a value they use turns a render
// into a blank box. So:
//
//   * a property NOT in the table is accepted verbatim, exactly as today;
//   * a property in the table as `freeform` is known to exist - which is what
//     `CSS.supports(name)` and `name in getComputedStyle(e)` ask - but its
//     grammar is not modelled and its values are accepted verbatim;
//   * only a property with a real `value_kind` can refuse anything.
//
// Every shorthand is `freeform`: refusing `margin: 10px 20px` would need the
// expansion, and a half-modelled shorthand is the one way this file could
// break a page that works today.
//
// IT LIVES IN style/ RATHER THAN IN THE SHELL because it is a fact about CSS
// and there are three consumers: `el.style` (the CSSOM's specified values),
// `getComputedStyle` (which needs the name list and the initial values), and
// `CSS.supports`. Two of the three are in `lib/Shell/`, which is what made the
// spelling conversion below get written twice before this existed.

namespace ctbrowser::style::css {

// The value types this table can express. Anything more expressive than this -
// a real `<shadow>#`, a `<transform-list>` - is `freeform` and says so, because
// a grammar that is 80% right refuses 20% of the valid values a page writes.
enum class value_kind : std::uint8_t {
    freeform,          // known to exist; grammar not modelled, values accepted
    keyword_only,      // nothing but the listed keywords
    length,            // <length>, and 0 with no unit
    length_percentage, // <length-percentage>
    percentage,
    number,
    integer,
    number_percentage, // opacity: <number> | <percentage>
    number_length,     // line-height, tab-size: <number> | <length-percentage>
    angle,
    time,
};

// One longhand. `keywords` is a space-separated set, matched ASCII
// case-insensitively; a `keyword_only` property is nothing but that set, and a
// typed property accepts the set OR the type.
struct property_syntax {
    std::string_view name;
    value_kind kind = value_kind::freeform;
    std::string_view keywords;
    // The CSS initial value, ALREADY SERIALISED. `getComputedStyle` answers it
    // for a property nothing declared, which is most of what `undefined` meant
    // before this table existed.
    std::string_view initial;
    bool inherited = false;
    // A `<length>` or `<number>` this property may not take negative. `width:
    // -1px` is invalid; `margin-left: -1px` is not, and the difference is per
    // property rather than per type.
    bool nonnegative = false;
    // A SHORTHAND. CSSOM's indexed properties are the supported LONGHANDS -
    // `'border' in getComputedStyle(e)` is true and `Array.from(...)` does not
    // contain it - so `getComputedStyle` has to tell the two apart, and this
    // table is the only place that knows. It was a duplicate list in
    // `computed_style.cpp` until it was a field here, which meant a shorthand
    // added to one and not the other got enumerated and then failed
    // `serialize-all-longhands`.
    bool shorthand = false;
};

// nullptr for a property this engine has never heard of - which is NOT the same
// as an invalid one, and the callers treat it differently: an unknown property
// still stores its value (CSSOM says a page may set one), but `CSS.supports`
// says false and `getComputedStyle` does not publish it.
[[nodiscard]] const property_syntax * find_property(std::string_view name);

// Every property in the table, in the order it is written - which is the order
// `getComputedStyle`'s indexed properties enumerate.
[[nodiscard]] std::span<const property_syntax> known_properties();

// The answer `el.style[p] = v` and `CSS.supports(p, v)` both need.
//
// `serialized` is the CANONICAL form: `0` becomes `0px` for a length, a keyword
// is lowercased, a number loses its trailing zeros, and whitespace is
// normalised. It is empty exactly when `valid` is false, which is what makes
// `test_invalid_value`'s `assert_equals(div.style.getPropertyValue(p), "")`
// the same test as "the declaration was refused".
struct value_check {
    bool valid = false;
    std::string serialized;
    // Whether the value carried a trailing `!important`. It is NOT part of
    // `serialized`: importance is a property of the DECLARATION and CSSOM gives
    // it its own argument (`setProperty(p, v, "important")`) and its own reader
    // (`getPropertyPriority`).
    bool important = false;
    // Whether the value calls a function this engine does not implement -
    // `attr()`, `random-item()`, `type()`. It does NOT make the value invalid:
    // CSSOM says a page may set a property or a value the engine has never heard
    // of and read it back. It is what `CSS.supports` answers no to, because §5
    // of CSS Conditional 3 asks whether the declaration would be DROPPED, and
    // one calling a function nothing can evaluate would be.
    bool uses_unknown_function = false;
};

// `allow_important` is false for the two paths CSSOM says must refuse one - the
// IDL setter (`el.style.width = "1px !important"`) and `setProperty`'s value
// argument - and true for the one path CSS syntax allows it on, which is a
// declaration parsed out of a `style` attribute or a stylesheet.
[[nodiscard]] value_check check_declaration(std::string_view property, std::string_view value,
                                            bool allow_important = false);

// `CSS.supports(property, value)` - §5 of CSS Conditional 3, which is
// `check_declaration` with the answer thrown away.
[[nodiscard]] bool supports_declaration(std::string_view property, std::string_view value);

// `CSS.supports(conditionText)` - the one-argument form, which takes a
// `<supports-condition>`: `(color: red)`, `not (x: y)`, `(a: b) and (c: d)`.
[[nodiscard]] bool supports_condition(std::string_view text);

// THE TWO SPELLINGS OF ONE PROPERTY, in one place. `background-color` is the
// CSS name and `backgroundColor` the IDL one; a page writes both and the CSSOM
// has to answer to both. This conversion had two copies - `element.cpp`'s
// `css_property_name` and `computed_style.cpp`'s inline loop in
// `getPropertyValue` - which is exactly the shape `core/algorithms.hpp` was
// created for, one layer down.
//
// The `-webkit-` prefix is the case a naive loop gets wrong in both directions:
// the IDL name is `webkitTransform`, capital W, with no leading dash.
[[nodiscard]] std::string css_name_of(std::string_view idl);
[[nodiscard]] std::string idl_name_of(std::string_view css);

} // namespace ctbrowser::style::css
