#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// WHICH PROPERTIES EXIST, WHAT EACH ACCEPTS, AND HOW A VALUE SERIALISES - what
// `el.style` validates and re-serialises against, what `getComputedStyle` lists,
// and what `CSS.supports` answers from.
//
// THE TABLE IS DELIBERATELY CONSERVATIVE, and that is the design rather than an
// apology. Three of the vendored corpora and every render golden write through
// `el.style`, so a syntax that is WRONG about a value they use turns a render
// into a blank box. So:
//
//   * a property NOT in the table is accepted verbatim;
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
// `CSS.supports`. Two of the three are in `lib/Shell/`.

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
    number_length,     // tab-size: <number> | <length>
    // line-height, which is `<number> | <length-percentage>` and is the reason
    // the two are not one kind: `line-height: 50%` is half the font size and
    // `tab-size: 50%` is nothing at all, so a percentage inside a math function
    // is a syntax error for one of them and a value for the other.
    number_length_percentage,
    angle,
    time,
    // `<position>`, CSS Values 5 §position. The only kind here whose value is
    // more than one component, and the only one whose canonical form REORDERS
    // what the author wrote: `bottom right` serialises as `right bottom`,
    // because a position is a horizontal half and a vertical half in that order
    // however they were spelled.
    position,
};

// One longhand. `keywords` is a space-separated set, matched ASCII
// case-insensitively; a `keyword_only` property is nothing but that set, and a
// typed property accepts the set OR the type.
struct property_syntax {
    std::string_view name;
    value_kind kind = value_kind::freeform;
    std::string_view keywords;
    // The CSS initial value, ALREADY SERIALISED. `getComputedStyle` answers it
    // for a property nothing declared.
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
    // Whether the value holds a `var()`/`env()`/`attr()`, so what it means is
    // not known until substitution. A SHORTHAND carrying one cannot be split
    // into its longhands here, and stays whole in the declaration block.
    bool substituted = false;
};

// `allow_important` is false for the two paths CSSOM says must refuse one - the
// IDL setter (`el.style.width = "1px !important"`) and `setProperty`'s value
// argument - and true for the one path CSS syntax allows it on, which is a
// declaration parsed out of a `style` attribute or a stylesheet.
[[nodiscard]] value_check check_declaration(std::string_view property, std::string_view value,
                                            bool allow_important = false);

// THE COMPUTED VALUE OF A `<position>`. CSS Values 5 §position: the keywords
// compute to percentages - `center` is `50%`, `right 30%` is `70%`, `right 20px`
// is `calc(100% - 20px)` - and a single component gets `50%` for the half it
// left unsaid, so `10% center` and `10%` both compute to `10% 50%`. The four
// flow-relative keywords resolve against the WRITING MODE and DIRECTION the
// caller supplies, which is why they are parameters: `x-start` is `left` in a
// horizontal left-to-right box and `right` in a right-to-left one.
//
// A math function is a component like any other and is kept as written -
// `calc(0% + 320px)` is `calc(0% + 320px) 50%` - and text that is not a
// position this reader can resolve comes back EMPTY, so a caller keeps what it
// had rather than guessing.
[[nodiscard]] std::string computed_position(std::string_view specified,
                                            std::string_view writing_mode,
                                            std::string_view direction);

// A `font-family` LIST WRITTEN AS CSSOM WRITES ONE: a family name that is a
// valid identifier sequence loses its quotes (`'Times New Roman'` is `Times
// New Roman`), one that is not keeps them as DOUBLE quotes (`'34J'` is
// `"34J"`, and so is a quoted `"serif"`, which is not the generic family), the
// author's case survives, and the separator is `, `. The same string is the
// specified value `el.style.fontFamily` reads back and the computed value
// `getComputedStyle` reports - css/cssom's serialize-values and
// font-family-serialization-001 ask for it from each side.
[[nodiscard]] std::string serialize_font_family(std::string_view text);

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

// --- THE CSSOM DECLARATION BLOCK -------------------------------------------
//
// CSSOM §6.6 over a list of declarations, in one place for the two blocks the
// shell keeps - `el.style` and a rule's `.style` - so a shorthand is expanded,
// read back and folded one way. A shorthand this table can split (`margin`,
// `border`, `flex`, `overflow`, `all`, ...) is stored as its LONGHANDS and
// reconstructed on the way out per "serialize a CSS declaration block"; one it
// cannot (`background`, `font`, or any value holding `var()`) is stored whole,
// exactly as every property was before this existed.
struct declaration {
    std::string name; // the CSS spelling; a custom property keeps its case
    std::string value;
    bool important = false;
};
using declaration_block = std::vector<declaration>;

// The longhands `shorthand` expands to, in canonical order; empty for a
// longhand and for a shorthand this table does not split.
[[nodiscard]] std::span<const std::string_view> longhands_of(std::string_view shorthand);

// `setProperty` / the IDL setter: "set a CSS declaration" for `name`, or for
// each longhand of a shorthand. The value goes through `check_declaration`;
// an invalid one is a no-op and an empty one removes. Returns whether the
// serialisation of the block changed - which is when the style attribute is
// rewritten and a mutation record queued.
bool set_declaration(declaration_block & block, std::string_view name, std::string_view text,
                     bool important);
// A declaration LIST appended to the block, as a `style` attribute, `cssText`
// or a rule body arrives: a later declaration of the same longhand replaces
// the earlier one and takes its place at the END, unless the earlier one was
// `!important` and it is not. `allow` filters the property names a block may
// hold (an `@page` block takes its descriptors only); null allows everything.
void parse_declaration_block(declaration_block & block, std::string_view text,
                             bool (*allow)(std::string_view name, const void * ctx) = nullptr,
                             const void * ctx = nullptr);
// `removeProperty`: answers the value it removed (`getPropertyValue` first),
// and `removed` says whether anything left the block.
[[nodiscard]] std::string remove_declaration(declaration_block & block, std::string_view name,
                                             bool & removed);
// `getPropertyValue` and `getPropertyPriority`; a shorthand answers from its
// longhands and "" unless every one is present with one priority.
[[nodiscard]] std::string declaration_value(const declaration_block & block, std::string_view name);
[[nodiscard]] std::string declaration_priority(const declaration_block & block,
                                               std::string_view name);
// `cssText`: §6.6 "serialize a CSS declaration block".
[[nodiscard]] std::string serialize_declaration_block(const declaration_block & block);

} // namespace ctbrowser::style::css
