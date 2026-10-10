//===- properties.hpp - CSS property table ----------------------*- C++ -*-===//
//
// Which properties exist, what each accepts, and how a value serialises: what
// `el.style` validates and re-serialises against, what `getComputedStyle`
// lists, and what `CSS.supports` answers from. Also the computed-value
// serialisers for colours, images, shapes and other compound values, and the
// CSSOM declaration block.
//
// The table is deliberately conservative. The vendored test pages and every
// render golden write through `el.style`, so a grammar that is wrong about a
// value they use turns a render into a blank box. So:
//
//   * a property not in the table is accepted verbatim;
//   * a property in the table as `freeform` is known to exist (which is what
//     `CSS.supports(name)` and `name in getComputedStyle(e)` ask), but its
//     grammar is not modelled and its values are accepted verbatim;
//   * only a property with a real `value_kind` can refuse anything.
//
// Every shorthand is `freeform`: refusing `margin: 10px 20px` would need the
// expansion, and a half-modelled shorthand could break a working page.
//
// It lives in style/ rather than the shell because it is a fact about CSS with
// three consumers: `el.style` (the CSSOM's specified values),
// `getComputedStyle` (which needs the name list and the initial values), and
// `CSS.supports`. Two of the three are in `lib/Shell/`.
//
//===----------------------------------------------------------------------===//
#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ctbrowser::style::css {

/// The value types this table can express. Anything richer (a real
/// `<shadow>#`, a `<transform-list>`) is `freeform`, because a grammar that is
/// 80% right refuses 20% of the valid values a page writes.
enum class value_kind : std::uint8_t {
    freeform,          // Known to exist; grammar not modelled, values accepted.
    keyword_only,      // Nothing but the listed keywords.
    length,            // <length>, and 0 with no unit.
    length_percentage, // <length-percentage>
    percentage,
    number,
    integer,
    number_percentage, // opacity: <number> | <percentage>
    number_length,     // tab-size: <number> | <length>
    // line-height: `<number> | <length-percentage>`. Separate from
    // number_length because `line-height: 50%` is half the font size while
    // `tab-size: 50%` is invalid, so a percentage inside a math function is a
    // value for one and a syntax error for the other.
    number_length_percentage,
    angle,
    time,
    // `<position>`, CSS Values 5 §position. The only kind whose value has more
    // than one component, and whose canonical form reorders what the author
    // wrote: `bottom right` serialises as `right bottom`, because a position is
    // a horizontal half then a vertical half however they were spelled.
    position,
    // `<color>`, checked by syntax only: a named or system colour keyword, a hex
    // colour, or a colour function whose arguments are kept as written. It
    // exists to reject `color: undefined` and `color: unknown color`, which
    // CSSOM says leave the declaration unchanged.
    color,
};

/// One property's table entry. `keywords` is a space-separated set, matched
/// ASCII case-insensitively; a `keyword_only` property accepts only that set,
/// and a typed property accepts the set or the type.
struct property_syntax {
    std::string_view name;
    value_kind kind = value_kind::freeform;
    std::string_view keywords;
    /// The CSS initial value, already serialised. `getComputedStyle` returns it
    /// for a property nothing declared.
    std::string_view initial;
    bool inherited = false;
    /// Whether a `<length>` or `<number>` value must be non-negative. `width:
    /// -1px` is invalid and `margin-left: -1px` is not; the difference is per
    /// property, not per type.
    bool nonnegative = false;
    /// Whether this is a shorthand. CSSOM's indexed properties are the supported
    /// longhands (`'border' in getComputedStyle(e)` is true, but
    /// `Array.from(...)` does not contain it), so `getComputedStyle` has to tell
    /// the two apart, and this table is the only place that knows.
    bool shorthand = false;
};

/// The table entry for `name`, or nullptr for a property this engine has never
/// heard of. Unknown is not invalid: an unknown property still stores its
/// value (CSSOM lets a page set one), but `CSS.supports` says false and
/// `getComputedStyle` does not list it.
[[nodiscard]] const property_syntax * find_property(std::string_view name);

/// Every property in the table, in source order, which is the order
/// `getComputedStyle`'s indexed properties enumerate.
[[nodiscard]] std::span<const property_syntax> known_properties();

/// Whether a property's animation type is anything but "not animatable". Not
/// animatable: the animation and transition properties, the container's name
/// and type (CSS Conditional 5 §4), the bidi pair, will-change and
/// interpolate-size (CSS Values 5 §interpolate-size). A keyframe ignores such a
/// property (CSS Animations 1 §4.1, Web Animations 1 §5.4.5) and no transition
/// starts on one, allow-discrete or not (CSS Transitions 2 §2.1). A custom
/// property is animatable.
[[nodiscard]] bool animatable(std::string_view name);

/// Whether the table says `name` inherits ("Inherited: yes"). A hash lookup,
/// because the cascade asks once per declaration per element. The name is the
/// lowercase CSS spelling the cascade interns.
[[nodiscard]] bool inherited_property(std::string_view name);

/// The answer `el.style[p] = v` and `CSS.supports(p, v)` both need.
///
/// `serialized` is the canonical form: `0` becomes `0px` for a length, a
/// keyword is lowercased, a number loses its trailing zeros, and whitespace is
/// normalised. It is empty exactly when `valid` is false, so reading back ""
/// means the declaration was rejected.
struct value_check {
    bool valid = false;
    std::string serialized;
    /// Whether the value carried a trailing `!important`. It is not part of
    /// `serialized`: importance belongs to the declaration, and CSSOM gives it
    /// its own argument (`setProperty(p, v, "important")`) and reader
    /// (`getPropertyPriority`).
    bool important = false;
    /// Whether the value calls a function this engine does not implement
    /// (`attr()`, `random-item()`, `type()`). That does not make the value
    /// invalid, since CSSOM lets a page set and read back values the engine
    /// does not know. But `CSS.supports` answers no, because CSS Conditional 3
    /// §5 asks whether the declaration would be dropped, and one calling a
    /// function nothing can evaluate would be.
    bool uses_unknown_function = false;
    /// Whether the value holds a `var()`/`env()`/`attr()`, so its meaning is
    /// not known until substitution. A shorthand carrying one cannot be split
    /// into its longhands here, and stays whole in the declaration block.
    bool substituted = false;
};

/// Whether `word` is a CSS-wide keyword, valid for every property including
/// unknown ones: `inherit`, `initial`, `unset`, `revert`, `revert-layer`, and
/// the draft `revert-rule`, which the cascade rolls back like the other two
/// (engine/cascade.cpp).
[[nodiscard]] bool is_wide_keyword(std::string_view word) noexcept;

/// Validates and canonicalises `value` for `property`; see value_check.
///
/// `allow_important` is false for the two paths CSSOM says must reject a
/// trailing `!important` (the IDL setter, `el.style.width = "1px !important"`,
/// and `setProperty`'s value argument), and true for a declaration parsed out
/// of a `style` attribute or a stylesheet.
[[nodiscard]] value_check check_declaration(std::string_view property, std::string_view value,
                                            bool allow_important = false);

/// The computed value of a `<position>` (CSS Values 5 §position). Keywords
/// compute to percentages (`center` is `50%`, `right 30%` is `70%`, `right
/// 20px` is `calc(100% - 20px)`), and a single component gets `50%` for the
/// missing half, so `10% center` and `10%` both compute to `10% 50%`. The four
/// flow-relative keywords resolve against the caller's writing mode and
/// direction: `x-start` is `left` in a left-to-right horizontal box and `right`
/// in a right-to-left one.
///
/// A math function is a component like any other and is kept as written
/// (`calc(0% + 320px)` is `calc(0% + 320px) 50%`). Text that is not a position
/// this reader can resolve comes back empty, so a caller keeps what it had.
[[nodiscard]] std::string computed_position(std::string_view specified,
                                            std::string_view writing_mode,
                                            std::string_view direction);

/// The physical longhand a logical one maps to under a writing mode and
/// direction (CSS Logical 1 §2-4): `margin-block-start` is `margin-top` in
/// horizontal-tb and `margin-right` in vertical-rl, `inline-size` is `width`
/// or `height`, `border-start-end-radius` is a corner. Empty for a property
/// that is not logical. The cascade maps every logical declaration through it
/// at computed-value time, and getComputedStyle reads a logical property back
/// as the physical one it mapped to.
[[nodiscard]] std::string physical_property_of(std::string_view logical,
                                               std::string_view writing_mode,
                                               std::string_view direction);

/// A `font-family` list serialised as CSSOM does: a family name that is a valid
/// identifier sequence loses its quotes (`'Times New Roman'` is `Times New
/// Roman`), one that is not gets double quotes (`'34J'` is `"34J"`, and so is
/// a quoted `"serif"`, which is not the generic family), the author's case
/// survives, and the separator is `, `. The same string is both the specified
/// value `el.style.fontFamily` reads back and the computed value
/// `getComputedStyle` reports.
[[nodiscard]] std::string serialize_font_family(std::string_view text);

// --- Colours and other computed values (CSS Color 4 and 5) ------------------
//
// check_declaration() already serialises a `<color>` the way `el.style` reads
// it back; the functions below give the computed and painted answers. They
// understand the whole grammar: legacy and modern rgb()/hsl()/hwb(),
// lab()/lch()/oklab()/oklch(), color() over the predefined spaces,
// color-mix(), the relative colour syntax, light-dark(), alpha() and
// contrast-color().
struct length_context;
/// What resolving a colour (or a value containing one) needs to know.
struct color_context {
    /// What `currentcolor` means here, as computed colour text (`rgb(255, 0,
    /// 0)`); empty leaves a colour that needs it unresolved.
    std::string_view current_color;
    /// The bases for a `calc()` in a channel (`sign(1em - 10px)`); null means
    /// the defaults.
    const length_context * lengths = nullptr;
    /// True when the element's used colour scheme is dark: `light-dark()` takes
    /// its second colour and the system colours their dark values.
    bool dark = false;
};
/// The specified serialisation of a `<color>` on its own (what a colour inside
/// a gradient or a shadow reads back as), or empty for text that is not one.
[[nodiscard]] std::string serialize_color(std::string_view text);
/// The computed value of a colour (CSS Color 4 §15): a legacy colour as
/// `rgb()`/`rgba()`, every other as its own function or `color()`, `none`
/// kept, calc() folded. Empty when `specified` is not a colour or cannot be
/// resolved here.
[[nodiscard]] std::string computed_color(std::string_view specified, const color_context & ctx);
/// The computed value of an `<image>` list (CSS Images 3 and 4): each
/// gradient's colours computed, its lengths in pixels and its angles in
/// degrees, a `url()` as written. Empty for text with no image this models.
[[nodiscard]] std::string computed_image(std::string_view specified, const color_context & ctx);
/// The percentage a font-width keyword names (CSS Fonts 4 §2.3), such as
/// `normal` 100% or `condensed` 75%, or nullopt for any other text.
[[nodiscard]] std::optional<std::string_view> font_width_percentage(std::string_view keyword);
/// offset-rotate's computed value (Motion Path 1 §4.4): `auto` plus an angle in
/// degrees (`reverse` is `auto 180deg`), or an angle alone in degrees. With
/// `keep_auto`, a bare `auto` (`auto 0deg`) stays `auto`, as getComputedStyle
/// writes it; without, it is written out in full, as interpolation needs.
/// Empty for text that is not a valid value.
[[nodiscard]] std::string computed_offset_rotate(std::string_view specified, bool keep_auto);
/// zoom's computed value (CSS Viewport 1 §5): a number as itself, a percentage
/// as its number, `normal` as 1, and the legacy `0` as 1 (also where an
/// interpolation clamped at zero lands). Empty for text that is not a valid
/// value.
[[nodiscard]] std::string computed_zoom(std::string_view specified, const length_context & ctx);
/// The computed value of a <basic-shape> in clip-path or shape-outside (CSS
/// Shapes 1 §3.2): lengths absolute, the position from the top left in
/// percentages and calc()s, xywh() and rect() as their inset(). Empty for a
/// value that is not one.
[[nodiscard]] std::string computed_basic_shape(std::string_view property, std::string_view text,
                                               const length_context & ctx);
/// Blends two computed basic shapes of the same function component by
/// component as `wa * a + wb * b`: an interpolation at `p` passes (1 - p, p),
/// an addition (1, 1). nullopt when the two do not blend (CSS Shapes 1 §3.3).
[[nodiscard]] std::optional<std::string> blend_basic_shapes(std::string_view property,
                                                            std::string_view a, std::string_view b,
                                                            double wa, double wb);
/// The computed value of a gap rule list (CSS Gap Decorations 1): a width
/// list's lengths in pixels, a colour list's colours computed, a repeat count
/// as its integer. Empty for another property or text that is not one.
[[nodiscard]] std::string computed_gap_list(std::string_view property, std::string_view text,
                                            const color_context & ctx);
/// The computed value of a `<filter-value-list>` (Filter Effects 1 §11):
/// percentages as numbers, lengths in pixels, a shadow's colour first.
[[nodiscard]] std::string computed_filter(std::string_view specified, const color_context & ctx);
/// The computed value of a grid track list or grid line for an element that is
/// not a grid container (CSS Grid 2): lengths in pixels, a calc() folded.
/// Empty for a property or value this does not model.
[[nodiscard]] std::string computed_grid(std::string_view property, std::string_view specified,
                                        const color_context & ctx);
/// The computed value of rotate, scale, translate, transform-origin or
/// perspective-origin (CSS Transforms 2): angles in degrees, a scale's
/// percentages as numbers, an origin resolved against the box's border width
/// and height. Empty for a property or value this does not model.
[[nodiscard]] std::string computed_transform_property(std::string_view property,
                                                      std::string_view specified,
                                                      const color_context & ctx, float box_width,
                                                      float box_height);
/// The computed value of a background-* or mask-* layer list (CSS Backgrounds
/// 4, Masking 1): a size's lengths in pixels with its second `auto` written
/// out, a position's keywords as percentages. Empty for a property or value
/// this does not model.
[[nodiscard]] std::string computed_background_list(std::string_view property,
                                                   std::string_view specified,
                                                   const color_context & ctx,
                                                   std::string_view writing_mode = "horizontal-tb",
                                                   std::string_view direction = "ltr");
/// A colour as unclamped sRGB channels in [0, 1] nominally, for painting.
struct srgb_color {
    float r = 0, g = 0, b = 0, a = 1;
};
/// Resolves a colour to sRGB, or nullopt when it is not a colour or cannot be
/// resolved in `ctx`.
[[nodiscard]] std::optional<srgb_color> resolve_color(std::string_view specified,
                                                      const color_context & ctx);
/// A system colour by name, ASCII case-insensitive (CSS Color 4 §6.2's, plus
/// the deprecated ones §6.3 keeps parsing), as the light-scheme 0xRRGGBB value
/// Chromium ships, which is what the cascade resolves one to. nullopt for any
/// other word.
[[nodiscard]] std::optional<std::uint32_t> system_color(std::string_view name);
/// A colour in an interpolation space (CSS Color 4 §12), for transitions,
/// animations and gradients: the three channels in the space's own units (a
/// hue in degrees), which of them are missing after §12.2's carrying forward
/// and §4.4.1's powerless rule, and the alpha.
///
/// color_in_space() converts to one; `space` is one of `srgb`, `srgb-linear`,
/// `display-p3`, `a98-rgb`, `prophoto-rgb`, `rec2020`, `xyz`, `xyz-d50`,
/// `xyz-d65`, `hsl`, `hwb`, `lab`, `lch`, `oklab`, `oklch`. It returns nullopt
/// when the text is not a colour or the space is not one of these.
struct space_color {
    std::array<double, 3> c{};
    std::array<bool, 3> none{};
    double alpha = 1.0;
    bool alpha_none = false;
};
[[nodiscard]] std::optional<space_color> color_in_space(std::string_view specified,
                                                        std::string_view space,
                                                        const color_context & ctx);
/// Converts back: the computed-value serialisation of a space_color, such as
/// `color(srgb r g b)` or `oklab(l a b)`, `none` kept. Empty when `space` is
/// not a known space.
[[nodiscard]] std::string color_from_space(const space_color & c, std::string_view space);
/// HTML's "serialize a color well control color" over a parsed `value`, for
/// `<input type=color>`. Text that is not a `<color>` (or needs a context,
/// like `currentcolor`) is opaque black; without `alpha` the colour is made
/// opaque; `display_p3` gives `color(display-p3 r g b / a)`; otherwise the
/// colour is sRGB at eight bits per channel: `#rrggbb` without `alpha`,
/// `color(srgb r g b / a)` with it.
[[nodiscard]] std::string sanitize_color(std::string_view value, bool display_p3, bool alpha);

/// `CSS.supports(property, value)` (CSS Conditional 3 §5): whether
/// check_declaration() would accept it.
[[nodiscard]] bool supports_declaration(std::string_view property, std::string_view value);

/// `CSS.supports(conditionText)`, the one-argument form, which takes a
/// `<supports-condition>`: `(color: red)`, `not (x: y)`, `(a: b) and (c: d)`.
[[nodiscard]] bool supports_condition(std::string_view text);

/// Converts between a property's CSS name (`background-color`) and its IDL
/// name (`backgroundColor`); the CSSOM accepts both. The `-webkit-` prefix is
/// the case a naive loop gets wrong: its IDL name is `webkitTransform`, with a
/// lowercase w and no leading dash.
[[nodiscard]] std::string css_name_of(std::string_view idl);
[[nodiscard]] std::string idl_name_of(std::string_view css);

/// CSSOM §2.1 "serialize an identifier": `CSS.escape`, also used to write a
/// selector's names and a custom property's name.
[[nodiscard]] std::string serialize_identifier(std::string_view text);

// --- The CSSOM declaration block --------------------------------------------
//
// CSSOM §6.6 over a list of declarations, shared by the two blocks the shell
// keeps (`el.style` and a rule's `.style`) so a shorthand is expanded, read
// back and folded one way. A shorthand this table can split (`margin`,
// `border`, `flex`, `overflow`, `all`, ...) is stored as its longhands and
// reconstructed on the way out per "serialize a CSS declaration block"; one it
// cannot (`background`, `font`, or any value holding `var()`) is stored whole.

/// One declaration in a CSSOM declaration block.
struct declaration {
    std::string name; // The CSS spelling; a custom property keeps its case.
    std::string value;
    bool important = false;
};
using declaration_block = std::vector<declaration>;

/// The longhands `shorthand` expands to, in canonical order; empty for a
/// longhand and for a shorthand this table does not split.
[[nodiscard]] std::span<const std::string_view> longhands_of(std::string_view shorthand);

/// Expands an already-substituted shorthand value for the cascade into
/// (longhand, value) pairs. Results borrow `property` and `value` or static
/// names and defaults. CSSOM canonicalisation and validation stay with the
/// caller. The order, and the uniform border aliases, are what the cascade
/// expects; unknown or unsupported values return an empty list.
[[nodiscard]] std::vector<std::pair<std::string_view, std::string_view>> expand_cascaded_shorthand(
    std::string_view property, std::string_view value);

/// `setProperty` and the IDL setter: "set a CSS declaration" for `name`, or for
/// each longhand of a shorthand. The value goes through check_declaration(); an
/// invalid one is a no-op and an empty one removes. Returns whether the
/// block's serialisation changed, which is when the style attribute is
/// rewritten and a mutation record queued.
bool set_declaration(declaration_block & block, std::string_view name, std::string_view text,
                     bool important);
/// Appends a declaration list to the block, as a `style` attribute, `cssText`
/// or a rule body arrives. A later declaration of the same longhand replaces
/// the earlier one and moves to the end, unless the earlier one was
/// `!important` and the later one is not. `allow` filters the declarations a
/// block may hold, by name and value (an `@page` block takes only its
/// descriptors, and no descriptor takes a tree-counting function); null allows
/// everything.
void parse_declaration_block(declaration_block & block, std::string_view text,
                             bool (*allow)(std::string_view name, std::string_view value,
                                           const void * ctx) = nullptr,
                             const void * ctx = nullptr);
/// `removeProperty`: returns the value it removed (as `getPropertyValue` read
/// it first), and sets `removed` to whether anything left the block.
[[nodiscard]] std::string remove_declaration(declaration_block & block, std::string_view name,
                                             bool & removed);
/// `getPropertyValue` and `getPropertyPriority`. A shorthand answers from its
/// longhands, and "" unless every one is present with the same priority.
[[nodiscard]] std::string declaration_value(const declaration_block & block, std::string_view name);
[[nodiscard]] std::string declaration_priority(const declaration_block & block,
                                               std::string_view name);
/// `cssText`: CSSOM §6.6 "serialize a CSS declaration block".
[[nodiscard]] std::string serialize_declaration_block(const declaration_block & block);

} // namespace ctbrowser::style::css
