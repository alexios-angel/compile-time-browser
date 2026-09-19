#pragma once

// `<color>`, CSS Color 4 and 5: the grammar, the colour spaces, and the two
// serialisations - the SPECIFIED one `el.style.color` reads back and the
// COMPUTED one `getComputedStyle` reports.
//
// THE SHAPE. A colour is parsed into a small tree (`parsed`): a keyword, a hex,
// an absolute function with its channels, a relative colour over an origin,
// a `color-mix()` over items, and the handful of CSS Color 5 wrappers. The
// tree is what the specified serialisation walks - `rgb(from rebeccapurple r
// calc(0.5 * g) b)` keeps its keywords and its calc() - and what `resolve()`
// evaluates into a `resolved` colour: one colour space, three channels each
// of which may be `none`, and an alpha. The computed serialisation is over
// that.
//
// LEGACY IS A FLAG, NOT A SPACE. `red`, `#f00`, `rgb(255 0 0)` and `hsl(0 100%
// 50%)` all serialise as `rgb(255, 0, 0)` (CSS Color 4 §15.2) while `color(srgb
// 1 0 0)`, `rgb(from red r g b)` and `color-mix(in srgb, red, red)` serialise
// as `color(srgb 1 0 0)`: the same sRGB triple, told apart only by how it was
// written. `resolved::legacy` is that fact and nothing else.
//
// THE CONVERSIONS are the specification's own sample code (§17 and the
// conversions.js it publishes), with the rational matrices it gives so the
// out-of-gamut tests, which compare to 1e-4, agree. Everything passes through
// XYZ D65; Lab and ProPhoto are D50 and adapt through Bradford.

#include "../internal.hpp"

#include "../../calc/internal.hpp"

#include <memory>
#include <numbers>
#include <optional>

namespace ctbrowser::style::css::color_detail {

using namespace detail;

// --- the vocabulary -------------------------------------------------------

// CSS Color 4 §6.1's named colours, with their sRGB values. `transparent` and
// `currentcolor` are keywords of their own below.
struct named {
    std::string_view name;
    std::uint32_t rgb;
};

inline constexpr named named_colors[] = {
    {"aliceblue", 0xF0F8FF},
    {"antiquewhite", 0xFAEBD7},
    {"aqua", 0x00FFFF},
    {"aquamarine", 0x7FFFD4},
    {"azure", 0xF0FFFF},
    {"beige", 0xF5F5DC},
    {"bisque", 0xFFE4C4},
    {"black", 0x000000},
    {"blanchedalmond", 0xFFEBCD},
    {"blue", 0x0000FF},
    {"blueviolet", 0x8A2BE2},
    {"brown", 0xA52A2A},
    {"burlywood", 0xDEB887},
    {"cadetblue", 0x5F9EA0},
    {"chartreuse", 0x7FFF00},
    {"chocolate", 0xD2691E},
    {"coral", 0xFF7F50},
    {"cornflowerblue", 0x6495ED},
    {"cornsilk", 0xFFF8DC},
    {"crimson", 0xDC143C},
    {"cyan", 0x00FFFF},
    {"darkblue", 0x00008B},
    {"darkcyan", 0x008B8B},
    {"darkgoldenrod", 0xB8860B},
    {"darkgray", 0xA9A9A9},
    {"darkgreen", 0x006400},
    {"darkgrey", 0xA9A9A9},
    {"darkkhaki", 0xBDB76B},
    {"darkmagenta", 0x8B008B},
    {"darkolivegreen", 0x556B2F},
    {"darkorange", 0xFF8C00},
    {"darkorchid", 0x9932CC},
    {"darkred", 0x8B0000},
    {"darksalmon", 0xE9967A},
    {"darkseagreen", 0x8FBC8F},
    {"darkslateblue", 0x483D8B},
    {"darkslategray", 0x2F4F4F},
    {"darkslategrey", 0x2F4F4F},
    {"darkturquoise", 0x00CED1},
    {"darkviolet", 0x9400D3},
    {"deeppink", 0xFF1493},
    {"deepskyblue", 0x00BFFF},
    {"dimgray", 0x696969},
    {"dimgrey", 0x696969},
    {"dodgerblue", 0x1E90FF},
    {"firebrick", 0xB22222},
    {"floralwhite", 0xFFFAF0},
    {"forestgreen", 0x228B22},
    {"fuchsia", 0xFF00FF},
    {"gainsboro", 0xDCDCDC},
    {"ghostwhite", 0xF8F8FF},
    {"gold", 0xFFD700},
    {"goldenrod", 0xDAA520},
    {"gray", 0x808080},
    {"green", 0x008000},
    {"greenyellow", 0xADFF2F},
    {"grey", 0x808080},
    {"honeydew", 0xF0FFF0},
    {"hotpink", 0xFF69B4},
    {"indianred", 0xCD5C5C},
    {"indigo", 0x4B0082},
    {"ivory", 0xFFFFF0},
    {"khaki", 0xF0E68C},
    {"lavender", 0xE6E6FA},
    {"lavenderblush", 0xFFF0F5},
    {"lawngreen", 0x7CFC00},
    {"lemonchiffon", 0xFFFACD},
    {"lightblue", 0xADD8E6},
    {"lightcoral", 0xF08080},
    {"lightcyan", 0xE0FFFF},
    {"lightgoldenrodyellow", 0xFAFAD2},
    {"lightgray", 0xD3D3D3},
    {"lightgreen", 0x90EE90},
    {"lightgrey", 0xD3D3D3},
    {"lightpink", 0xFFB6C1},
    {"lightsalmon", 0xFFA07A},
    {"lightseagreen", 0x20B2AA},
    {"lightskyblue", 0x87CEFA},
    {"lightslategray", 0x778899},
    {"lightslategrey", 0x778899},
    {"lightsteelblue", 0xB0C4DE},
    {"lightyellow", 0xFFFFE0},
    {"lime", 0x00FF00},
    {"limegreen", 0x32CD32},
    {"linen", 0xFAF0E6},
    {"magenta", 0xFF00FF},
    {"maroon", 0x800000},
    {"mediumaquamarine", 0x66CDAA},
    {"mediumblue", 0x0000CD},
    {"mediumorchid", 0xBA55D3},
    {"mediumpurple", 0x9370DB},
    {"mediumseagreen", 0x3CB371},
    {"mediumslateblue", 0x7B68EE},
    {"mediumspringgreen", 0x00FA9A},
    {"mediumturquoise", 0x48D1CC},
    {"mediumvioletred", 0xC71585},
    {"midnightblue", 0x191970},
    {"mintcream", 0xF5FFFA},
    {"mistyrose", 0xFFE4E1},
    {"moccasin", 0xFFE4B5},
    {"navajowhite", 0xFFDEAD},
    {"navy", 0x000080},
    {"oldlace", 0xFDF5E6},
    {"olive", 0x808000},
    {"olivedrab", 0x6B8E23},
    {"orange", 0xFFA500},
    {"orangered", 0xFF4500},
    {"orchid", 0xDA70D6},
    {"palegoldenrod", 0xEEE8AA},
    {"palegreen", 0x98FB98},
    {"paleturquoise", 0xAFEEEE},
    {"palevioletred", 0xDB7093},
    {"papayawhip", 0xFFEFD5},
    {"peachpuff", 0xFFDAB9},
    {"peru", 0xCD853F},
    {"pink", 0xFFC0CB},
    {"plum", 0xDDA0DD},
    {"powderblue", 0xB0E0E6},
    {"purple", 0x800080},
    {"rebeccapurple", 0x663399},
    {"red", 0xFF0000},
    {"rosybrown", 0xBC8F8F},
    {"royalblue", 0x4169E1},
    {"saddlebrown", 0x8B4513},
    {"salmon", 0xFA8072},
    {"sandybrown", 0xF4A460},
    {"seagreen", 0x2E8B57},
    {"seashell", 0xFFF5EE},
    {"sienna", 0xA0522D},
    {"silver", 0xC0C0C0},
    {"skyblue", 0x87CEEB},
    {"slateblue", 0x6A5ACD},
    {"slategray", 0x708090},
    {"slategrey", 0x708090},
    {"snow", 0xFFFAFA},
    {"springgreen", 0x00FF7F},
    {"steelblue", 0x4682B4},
    {"tan", 0xD2B48C},
    {"teal", 0x008080},
    {"thistle", 0xD8BFD8},
    {"tomato", 0xFF6347},
    {"turquoise", 0x40E0D0},
    {"violet", 0xEE82EE},
    {"wheat", 0xF5DEB3},
    {"white", 0xFFFFFF},
    {"whitesmoke", 0xF5F5F5},
    {"yellow", 0xFFFF00},
    {"yellowgreen", 0x9ACD32},
};

// §6.2's system colours and the deprecated ones §6.3 keeps parsing, with the
// light-scheme values Chromium ships: CSSOM makes a colour's resolved value
// the used one, so `background-color: Menu` reads back as an `rgb()`
// (getComputedStyle-resolved-colors). The dark table below is what
// `color-scheme: dark` swaps in (CSS Color Adjust 1 §2); a name not in it
// is the same in both schemes.
inline constexpr named system_colors[] = {
    {"accentcolor", 0x0075FF},
    {"accentcolortext", 0xFFFFFF},
    {"activetext", 0xFF0000},
    {"buttonborder", 0x767676},
    {"buttonface", 0xEFEFEF},
    {"buttontext", 0x000000},
    {"canvas", 0xFFFFFF},
    {"canvastext", 0x000000},
    {"field", 0xFFFFFF},
    {"fieldtext", 0x000000},
    {"graytext", 0x808080},
    {"highlight", 0x1E90FF},
    {"highlighttext", 0xFFFFFF},
    {"linktext", 0x0000EE},
    {"mark", 0xFFFF00},
    {"marktext", 0x000000},
    {"selecteditem", 0x1E90FF},
    {"selecteditemtext", 0xFFFFFF},
    {"visitedtext", 0x551A8B},
    {"activeborder", 0xFFFFFF},
    {"activecaption", 0xCCCCCC},
    {"appworkspace", 0xFFFFFF},
    {"background", 0x6363CE},
    {"buttonhighlight", 0xFFFFFF},
    {"buttonshadow", 0x808080},
    {"captiontext", 0x000000},
    {"inactiveborder", 0xFFFFFF},
    {"inactivecaption", 0xFFFFFF},
    {"inactivecaptiontext", 0x7F7F7F},
    {"infobackground", 0xFBFCC5},
    {"infotext", 0x000000},
    {"menu", 0xF7F7F7},
    {"menutext", 0x000000},
    {"scrollbar", 0xFFFFFF},
    {"threeddarkshadow", 0x666666},
    {"threedface", 0xC0C0C0},
    {"threedhighlight", 0xDDDDDD},
    {"threedlightshadow", 0xC0C0C0},
    {"threedshadow", 0x888888},
    {"window", 0xFFFFFF},
    {"windowframe", 0xCCCCCC},
    {"windowtext", 0x000000},
};

inline constexpr named dark_system_colors[] = {
    {"activetext", 0xFF9E9E},   {"buttonborder", 0x6B6B6B},     {"buttonface", 0x6B6B6B},
    {"buttontext", 0xFFFFFF},   {"canvas", 0x121212},           {"canvastext", 0xFFFFFF},
    {"field", 0x3B3B3B},        {"fieldtext", 0xFFFFFF},        {"graytext", 0xA0A0A0},
    {"highlight", 0x99C8FF},    {"highlighttext", 0x000000},    {"linktext", 0x9E9EFF},
    {"selecteditem", 0x99C8FF}, {"selecteditemtext", 0x000000}, {"visitedtext", 0xD0ADF0},
    {"appworkspace", 0x121212}, {"scrollbar", 0x121212},        {"menu", 0x121212},
    {"menutext", 0xFFFFFF},     {"window", 0x121212},           {"windowtext", 0xFFFFFF},
    {"threedface", 0x6B6B6B},   {"captiontext", 0xFFFFFF},      {"infobackground", 0x121212},
    {"infotext", 0xFFFFFF},
};

[[nodiscard]] const named * find_named(std::span<const named> table, std::string_view word);

// The colour spaces this file computes in: the four polar/cylindrical ones,
// the two Lab-likes, and the predefined RGB and XYZ spaces of `color()`.
enum class space : std::uint8_t {
    srgb,
    hsl,
    hwb,
    lab,
    lch,
    oklab,
    oklch,
    srgb_linear,
    display_p3,
    display_p3_linear,
    a98_rgb,
    prophoto_rgb,
    rec2020,
    xyz_d50,
    xyz_d65,
};

[[nodiscard]] std::string_view space_name(space s) noexcept;

[[nodiscard]] std::optional<space> predefined_space(std::string_view word);

[[nodiscard]] std::optional<space> interpolation_space(std::string_view word);

// §12.2's ANALOGOUS COMPONENTS: which category each slot of each space is
// in. A missing component carries forward to the slot of the same category
// in another space, and the XYZ spaces count as super-saturated RGB. hwb's
// whiteness and blackness are analogous to nothing.
enum class part : std::uint8_t {
    red,
    green,
    blue,
    lightness,
    colorfulness, // chroma, or hsl's saturation
    hue,
    opponent_a,
    opponent_b,
    whiteness,
    blackness,
};

[[nodiscard]] std::array<part, 3> parts_of(space s) noexcept;

[[nodiscard]] int slot_of(space s, part p) noexcept;

[[nodiscard]] int hue_slot(space s) noexcept;

[[nodiscard]] int colorfulness_slot(space s) noexcept;

[[nodiscard]] std::array<std::string_view, 3> channel_keywords(space s) noexcept;

[[nodiscard]] double percent_reference(space s, int slot, bool bytes) noexcept;

// --- the resolved colour ----------------------------------------------------

struct resolved {
    space cs = space::srgb;
    std::array<double, 3> c{};
    std::array<bool, 3> none{};
    double alpha = 1.0;
    bool alpha_none = false;
    // See the file comment: written as rgb()/hex/named/hsl()/hwb() with every
    // channel present, and therefore serialised as `rgb()`.
    bool legacy = false;

    [[nodiscard]] bool any_none() const noexcept {
        return none[0] || none[1] || none[2] || alpha_none;
    }
};

using vec3 = std::array<double, 3>;

using mat3 = std::array<std::array<double, 3>, 3>;

[[nodiscard]] vec3 mul(const mat3 & m, const vec3 & v) noexcept;

// The matrices, as CSS Color 4 §17 publishes them.
inline constexpr mat3 lin_srgb_to_xyz{{{506752.0 / 1228815, 87881.0 / 245763, 12673.0 / 70218},
                                       {87098.0 / 409605, 175762.0 / 245763, 12673.0 / 175545},
                                       {7918.0 / 409605, 87881.0 / 737289, 1001167.0 / 1053270}}};

inline constexpr mat3 xyz_to_lin_srgb{{{12831.0 / 3959, -329.0 / 214, -1974.0 / 3959},
                                       {-851781.0 / 878810, 1648619.0 / 878810, 36519.0 / 878810},
                                       {705.0 / 12673, -2585.0 / 12673, 705.0 / 667}}};

inline constexpr mat3 lin_p3_to_xyz{{{608311.0 / 1250200, 189793.0 / 714400, 198249.0 / 1000160},
                                     {35783.0 / 156275, 247089.0 / 357200, 198249.0 / 2500400},
                                     {0.0, 32229.0 / 714400, 5220557.0 / 5000800}}};

inline constexpr mat3 xyz_to_lin_p3{{{446124.0 / 178915, -333277.0 / 357830, -72051.0 / 178915},
                                     {-14852.0 / 17905, 63121.0 / 35810, 423.0 / 17905},
                                     {11844.0 / 330415, -50337.0 / 660830, 316169.0 / 330415}}};

inline constexpr mat3 lin_a98_to_xyz{
    {{573536.0 / 994567, 263643.0 / 1420810, 187206.0 / 994567},
     {591459.0 / 1989134, 6239551.0 / 9945670, 374412.0 / 4972835},
     {53769.0 / 1989134, 351524.0 / 4972835, 4929758.0 / 4972835}}};

inline constexpr mat3 xyz_to_lin_a98{
    {{1829569.0 / 896150, -506331.0 / 896150, -308931.0 / 896150},
     {-851781.0 / 878810, 1648619.0 / 878810, 36519.0 / 878810},
     {16779.0 / 1248040, -147721.0 / 1248040, 1266979.0 / 1248040}}};

inline constexpr mat3 lin_2020_to_xyz{
    {{63426534.0 / 99577255, 20160776.0 / 139408157, 47086771.0 / 278816314},
     {26158966.0 / 99577255, 472592308.0 / 697040785, 8267143.0 / 139408157},
     {0.0, 19567812.0 / 697040785, 295819943.0 / 278816314}}};

inline constexpr mat3 xyz_to_lin_2020{
    {{30757411.0 / 17917100, -6372589.0 / 17917100, -4539589.0 / 17917100},
     {-19765991.0 / 29648200, 47925759.0 / 29648200, 467509.0 / 29648200},
     {792561.0 / 44930125, -1921689.0 / 44930125, 42328811.0 / 44930125}}};

// ProPhoto is D50; these take it to and from XYZ D50.
inline constexpr mat3 lin_prophoto_to_xyz{
    {{0.79776664490064230, 0.13518129740053308, 0.03134773412839220},
     {0.28807482881940, 0.71183523424187, 0.00008993693872},
     {0.0, 0.0, 0.82510460251046}}};

inline constexpr mat3 xyz_to_lin_prophoto{
    {{1.34578688164715830, -0.25557208737979464, -0.05110186497554526},
     {-0.54463070512490190, 1.50824774284514680, 0.02052744743642139},
     {0.0, 0.0, 1.21196754563894520}}};

inline constexpr mat3 d65_to_d50{
    {{1.0479297925449969, 0.022946870601609652, -0.05019226628920524},
     {0.02962780877005599, 0.9904344267538799, -0.017073799063418826},
     {-0.009243040646204504, 0.015055191490298152, 0.7518742814281371}}};

inline constexpr mat3 d50_to_d65{
    {{0.955473421488075, -0.02309845494876471, 0.06325924320057072},
     {-0.0283697093338637, 1.0099953980813041, 0.021041441191917323},
     {0.012314014864481998, -0.020507649298898964, 1.330365926242124}}};

inline constexpr mat3 xyz_to_lms{{{0.8190224379967030, 0.3619062600528904, -0.1288737815209879},
                                  {0.0329836539323885, 0.9292868615863434, 0.0361446663506424},
                                  {0.0481771893596242, 0.2642395317527308, 0.6335478284694309}}};

inline constexpr mat3 lms_to_oklab{{{0.2104542683093140, 0.7936177747023054, -0.0040720430116193},
                                    {1.9779985324311684, -2.4285922420485799, 0.4505937096174110},
                                    {0.0259040424655478, 0.7827717124575296, -0.8086757549230774}}};

inline constexpr mat3 oklab_to_lms{{{1.0, 0.3963377773761749, 0.2158037573099136},
                                    {1.0, -0.1055613458156586, -0.0638541728258133},
                                    {1.0, -0.0894841775298119, -1.2914855480194092}}};

inline constexpr mat3 lms_to_xyz{{{1.2268798758459243, -0.5578149944602171, 0.2813910456659647},
                                  {-0.0405757452148008, 1.1122868032803170, -0.0717110580655164},
                                  {-0.0763729366746601, -0.4214933324022432, 1.5869240198367816}}};

inline constexpr vec3 d50_white{0.3457 / 0.3585, 1.0, (1.0 - 0.3457 - 0.3585) / 0.3585};

[[nodiscard]] double lin_srgb(double v) noexcept;

[[nodiscard]] double gam_srgb(double v) noexcept;

[[nodiscard]] double lin_prophoto(double v) noexcept;

[[nodiscard]] double gam_prophoto(double v) noexcept;

[[nodiscard]] double lin_a98(double v) noexcept;

[[nodiscard]] double gam_a98(double v) noexcept;

[[nodiscard]] double lin_2020(double v) noexcept;

[[nodiscard]] double gam_2020(double v) noexcept;

[[nodiscard]] vec3 map3(const vec3 & v, double (*f)(double) noexcept) noexcept;

[[nodiscard]] double normalize_hue(double h) noexcept;

[[nodiscard]] vec3 hsl_to_rgb(double hue, double sat, double light) noexcept;

[[nodiscard]] double rgb_to_hue(const vec3 & rgb) noexcept;

[[nodiscard]] vec3 rgb_to_hsl(const vec3 & rgb) noexcept;

[[nodiscard]] vec3 hwb_to_rgb(double hue, double white, double black) noexcept;

[[nodiscard]] vec3 rgb_to_hwb(const vec3 & rgb) noexcept;

[[nodiscard]] vec3 xyz_to_lab(const vec3 & xyz65) noexcept;

[[nodiscard]] vec3 lab_to_xyz(const vec3 & lab) noexcept;

[[nodiscard]] vec3 xyz_to_oklab(const vec3 & xyz) noexcept;

[[nodiscard]] vec3 oklab_to_xyz(const vec3 & lab) noexcept;

[[nodiscard]] vec3 lab_to_lch(const vec3 & lab) noexcept;

[[nodiscard]] vec3 lch_to_lab(const vec3 & lch) noexcept;

[[nodiscard]] vec3 to_xyz(space s, vec3 c) noexcept;

[[nodiscard]] vec3 from_xyz(space s, const vec3 & xyz) noexcept;

[[nodiscard]] double hue_epsilon(space s) noexcept;

void settle_powerless(resolved & r) noexcept;

[[nodiscard]] bool srgb_family(space s) noexcept;

[[nodiscard]] resolved convert(const resolved & from, space to);

[[nodiscard]] std::string channel_text(double v);

[[nodiscard]] std::string byte_text(double v);

[[nodiscard]] std::string legacy_alpha_text(double alpha);

// --- the parsed tree --------------------------------------------------------

// One channel as written: a literal, `none`, a relative colour's keyword, or a
// math function kept as text (its simplified specified form) with what it
// evaluates to when that can be known before a font size exists.
struct channel {
    enum class kind : std::uint8_t {
        number,
        percent,
        angle,
        none,
        keyword,
        calc
    };
    kind k = kind::none;
    double value = 0.0;   // number; percent as 0..100; angle in degrees
    std::string text;     // keyword lowercased; calc as simplified specified text
    std::string raw_calc; // the calc as written, for evaluation against a context
    // A calc() with an answer at parse time (no relative units, no tree
    // counting), and which kind of answer.
    bool resolvable = false;
    double resolved_value = 0.0;
    kind resolved_kind = kind::number;
};

struct parsed;

struct mix_item {
    std::unique_ptr<parsed> color;
    std::optional<channel> weight; // a percentage, or a calc()
};

struct parsed {
    enum class kind : std::uint8_t {
        keyword, // named, system, currentcolor, transparent, -webkit-*
        hex,
        absolute, // rgb() hsl() hwb() lab() lch() oklab() oklch() color()
        relative, // the same, `from <color>`
        mix,      // color-mix()
        light_dark,
        alpha_fn, // alpha(from <color> / <alpha>)
        contrast, // contrast-color()
        layers,   // color-layers()
    };
    kind k = kind::keyword;
    std::string keyword;
    resolved fixed; // a hex colour, already resolved
    // absolute / relative
    std::string fn; // canonical function name: rgb hsl hwb lab lch oklab oklch color
    space cs = space::srgb;
    bool legacy_syntax = false; // the comma form of rgb()/hsl()
    // rgb()'s channels are 0..255 where color(srgb ...)'s are 0..1: the one
    // function whose numbers are not the space's own unit.
    bool bytes = false;
    std::array<channel, 3> ch{};
    std::optional<channel> alpha;
    std::unique_ptr<parsed> origin; // relative, alpha_fn, contrast
    // mix / light_dark / layers
    space mix_space = space::oklab;
    std::string hue_method; // shorter (default), longer, increasing, decreasing
    std::string blend_mode; // color-layers
    std::vector<mix_item> items;
};

// --- the reader -------------------------------------------------------------

// The units a channel calc() can be answered with before a font size or a
// viewport exists.
inline constexpr std::string_view context_free_units[] = {
    "px", "cm", "mm", "q", "in", "pt", "pc", "deg", "grad", "rad", "turn", "s", "ms"};

[[nodiscard]] bool parse_time_answerable(const token_stream & ts, std::size_t begin,
                                         std::size_t end);

inline constexpr std::string_view math_names[] = {
    "calc",     "min",      "max",           "clamp",         "round", "mod",  "rem",
    "sin",      "cos",      "tan",           "asin",          "acos",  "atan", "atan2",
    "pow",      "sqrt",     "hypot",         "log",           "exp",   "abs",  "sign",
    "calc-mix", "progress", "sibling-index", "sibling-count", "random"};

[[nodiscard]] double literal_value(const channel & c, space cs, int slot, bool bytes) noexcept;

[[nodiscard]] double clamp_literal(double v, space cs, int slot, bool absolute,
                                   bool bytes) noexcept;

[[nodiscard]] bool channel_settled(const channel & c) noexcept;

[[nodiscard]] bool all_settled(const parsed & p, bool none_ok) noexcept;

[[nodiscard]] resolved settled_legacy(const parsed & p);

[[nodiscard]] bool origin_loses_in_rgb(const parsed & p);

[[nodiscard]] std::string legacy_text(const resolved & r);

std::string serialize_specified(const parsed & p, bool as_origin);

[[nodiscard]] std::string modern_channel_text(const channel & c, space cs, int slot,
                                              bool resolve_calc, bool bytes);

[[nodiscard]] std::string relative_channel_text(const channel & c);

[[nodiscard]] std::string absolute_specified(const parsed & p, bool as_origin);

[[nodiscard]] std::string relative_specified(const parsed & p);

[[nodiscard]] std::string mix_specified(const parsed & p);

std::string serialize_specified(const parsed & p, bool as_origin);

// --- resolving --------------------------------------------------------------

struct resolve_context {
    std::string_view current_color; // computed text, or empty
    const length_context * lengths = nullptr;
    bool dark = false; // the used colour scheme
};

std::optional<resolved> resolve(const parsed & p, const resolve_context & ctx, int depth);

[[nodiscard]] std::optional<double> evaluate_channel(const channel & c, space cs, int slot,
                                                     bool bytes, const resolved * origin,
                                                     const resolve_context & ctx);

// The value of one channel of an absolute or relative colour, and whether it
// is missing. `origin` is the relative colour's origin in the target space.
struct channel_answer {
    double value = 0.0;
    bool none = false;
};

[[nodiscard]] std::optional<channel_answer> channel_value(const channel & c, space cs, int slot,
                                                          bool bytes, const resolved * origin,
                                                          const resolve_context & ctx);

[[nodiscard]] resolved as_keywords(resolved r, bool bytes) noexcept;

[[nodiscard]] std::optional<resolved> resolve_absolute(const parsed & p,
                                                       const resolve_context & ctx, int depth);

[[nodiscard]] resolved interpolate(resolved a, resolved b, space cs, std::string_view hue_method,
                                   double p1, double p2, double alpha_multiplier);

[[nodiscard]] std::optional<resolved> resolve_mix(const parsed & p, const resolve_context & ctx,
                                                  int depth);

std::optional<resolved> resolve(const parsed & p, const resolve_context & ctx, int depth);

[[nodiscard]] std::string modern_text(const resolved & r);

[[nodiscard]] std::string serialize_computed(const resolved & r);

[[nodiscard]] std::unique_ptr<parsed> parse_text(std::string_view text);

} // namespace ctbrowser::style::css::color_detail
