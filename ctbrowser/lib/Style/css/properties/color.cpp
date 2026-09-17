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

#include "internal.hpp"

#include "../calc/internal.hpp"

#include <memory>
#include <numbers>
#include <optional>

namespace ctbrowser::style::css {

using namespace detail;

namespace {

// --- the vocabulary -------------------------------------------------------

// CSS Color 4 §6.1's named colours, with their sRGB values. `transparent` and
// `currentcolor` are keywords of their own below.
struct named {
    std::string_view name;
    std::uint32_t rgb;
};
constexpr named named_colors[] = {
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
// (getComputedStyle-resolved-colors). Nothing here is themed.
constexpr named system_colors[] = {
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

[[nodiscard]] const named * find_named(std::span<const named> table, std::string_view word) {
    for (const named & one : table) {
        if (ascii_iequals(one.name, word)) { return &one; }
    }
    return nullptr;
}

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

[[nodiscard]] std::string_view space_name(space s) noexcept {
    switch (s) {
    case space::srgb: return "srgb";
    case space::hsl: return "hsl";
    case space::hwb: return "hwb";
    case space::lab: return "lab";
    case space::lch: return "lch";
    case space::oklab: return "oklab";
    case space::oklch: return "oklch";
    case space::srgb_linear: return "srgb-linear";
    case space::display_p3: return "display-p3";
    case space::display_p3_linear: return "display-p3-linear";
    case space::a98_rgb: return "a98-rgb";
    case space::prophoto_rgb: return "prophoto-rgb";
    case space::rec2020: return "rec2020";
    case space::xyz_d50: return "xyz-d50";
    case space::xyz_d65: return "xyz-d65";
    }
    return "srgb";
}

// The spaces `color()` and `color-mix(in ...)` name. `xyz` is `xyz-d65`.
[[nodiscard]] std::optional<space> predefined_space(std::string_view word) {
    static constexpr std::pair<std::string_view, space> table[] = {
        {"srgb", space::srgb},
        {"srgb-linear", space::srgb_linear},
        {"display-p3", space::display_p3},
        {"display-p3-linear", space::display_p3_linear},
        {"a98-rgb", space::a98_rgb},
        {"prophoto-rgb", space::prophoto_rgb},
        {"rec2020", space::rec2020},
        {"xyz", space::xyz_d65},
        {"xyz-d50", space::xyz_d50},
        {"xyz-d65", space::xyz_d65},
    };
    for (const auto & [name, s] : table) {
        if (ascii_iequals(name, word)) { return s; }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<space> interpolation_space(std::string_view word) {
    if (const std::optional<space> s = predefined_space(word)) { return s; }
    static constexpr std::pair<std::string_view, space> table[] = {
        {"hsl", space::hsl}, {"hwb", space::hwb},     {"lab", space::lab},
        {"lch", space::lch}, {"oklab", space::oklab}, {"oklch", space::oklch},
    };
    for (const auto & [name, s] : table) {
        if (ascii_iequals(name, word)) { return s; }
    }
    return std::nullopt;
}

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

[[nodiscard]] std::array<part, 3> parts_of(space s) noexcept {
    switch (s) {
    case space::hsl: return {part::hue, part::colorfulness, part::lightness};
    case space::hwb: return {part::hue, part::whiteness, part::blackness};
    case space::lab:
    case space::oklab: return {part::lightness, part::opponent_a, part::opponent_b};
    case space::lch:
    case space::oklch: return {part::lightness, part::colorfulness, part::hue};
    default: return {part::red, part::green, part::blue};
    }
}

// The slot holding `p` in `s`, or -1.
[[nodiscard]] int slot_of(space s, part p) noexcept {
    const std::array<part, 3> parts = parts_of(s);
    for (int i = 0; i < 3; ++i) {
        if (parts[static_cast<std::size_t>(i)] == p) { return i; }
    }
    return -1;
}
[[nodiscard]] int hue_slot(space s) noexcept {
    return slot_of(s, part::hue);
}
[[nodiscard]] int colorfulness_slot(space s) noexcept {
    return slot_of(s, part::colorfulness);
}

// The channel keywords a relative colour may use, per space, in slot order;
// `alpha` is always the fourth.
[[nodiscard]] std::array<std::string_view, 3> channel_keywords(space s) noexcept {
    switch (s) {
    case space::hsl: return {"h", "s", "l"};
    case space::hwb: return {"h", "w", "b"};
    case space::lab:
    case space::oklab: return {"l", "a", "b"};
    case space::lch:
    case space::oklch: return {"l", "c", "h"};
    case space::xyz_d50:
    case space::xyz_d65: return {"x", "y", "z"};
    default: return {"r", "g", "b"};
    }
}

// What 100% means in each slot (CSS Color 4 §4.1's reference ranges), and
// how a plain number in the channel maps to the space's own unit.
[[nodiscard]] double percent_reference(space s, int slot, bool bytes) noexcept {
    switch (s) {
    case space::srgb: return slot == 3 || !bytes ? 1.0 : 255.0; // rgb(): numbers are 0..255
    case space::hsl:
    case space::hwb: return slot == 3 ? 1.0 : 100.0;
    case space::lab: return slot == 0 ? 100.0 : (slot == 3 ? 1.0 : 125.0);
    case space::lch: return slot == 0 ? 100.0 : (slot == 3 ? 1.0 : 150.0);
    case space::oklab: return slot == 0 || slot == 3 ? 1.0 : 0.4;
    case space::oklch: return slot == 0 || slot == 3 ? 1.0 : 0.4;
    default: return 1.0;
    }
}

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

[[nodiscard]] vec3 mul(const mat3 & m, const vec3 & v) noexcept {
    return {m[0][0] * v[0] + m[0][1] * v[1] + m[0][2] * v[2],
            m[1][0] * v[0] + m[1][1] * v[1] + m[1][2] * v[2],
            m[2][0] * v[0] + m[2][1] * v[1] + m[2][2] * v[2]};
}

// The matrices, as CSS Color 4 §17 publishes them.
constexpr mat3 lin_srgb_to_xyz{{{506752.0 / 1228815, 87881.0 / 245763, 12673.0 / 70218},
                                {87098.0 / 409605, 175762.0 / 245763, 12673.0 / 175545},
                                {7918.0 / 409605, 87881.0 / 737289, 1001167.0 / 1053270}}};
constexpr mat3 xyz_to_lin_srgb{{{12831.0 / 3959, -329.0 / 214, -1974.0 / 3959},
                                {-851781.0 / 878810, 1648619.0 / 878810, 36519.0 / 878810},
                                {705.0 / 12673, -2585.0 / 12673, 705.0 / 667}}};
constexpr mat3 lin_p3_to_xyz{{{608311.0 / 1250200, 189793.0 / 714400, 198249.0 / 1000160},
                              {35783.0 / 156275, 247089.0 / 357200, 198249.0 / 2500400},
                              {0.0, 32229.0 / 714400, 5220557.0 / 5000800}}};
constexpr mat3 xyz_to_lin_p3{{{446124.0 / 178915, -333277.0 / 357830, -72051.0 / 178915},
                              {-14852.0 / 17905, 63121.0 / 35810, 423.0 / 17905},
                              {11844.0 / 330415, -50337.0 / 660830, 316169.0 / 330415}}};
constexpr mat3 lin_a98_to_xyz{{{573536.0 / 994567, 263643.0 / 1420810, 187206.0 / 994567},
                               {591459.0 / 1989134, 6239551.0 / 9945670, 374412.0 / 4972835},
                               {53769.0 / 1989134, 351524.0 / 4972835, 4929758.0 / 4972835}}};
constexpr mat3 xyz_to_lin_a98{{{1829569.0 / 896150, -506331.0 / 896150, -308931.0 / 896150},
                               {-851781.0 / 878810, 1648619.0 / 878810, 36519.0 / 878810},
                               {16779.0 / 1248040, -147721.0 / 1248040, 1266979.0 / 1248040}}};
constexpr mat3 lin_2020_to_xyz{
    {{63426534.0 / 99577255, 20160776.0 / 139408157, 47086771.0 / 278816314},
     {26158966.0 / 99577255, 472592308.0 / 697040785, 8267143.0 / 139408157},
     {0.0, 19567812.0 / 697040785, 295819943.0 / 278816314}}};
constexpr mat3 xyz_to_lin_2020{
    {{30757411.0 / 17917100, -6372589.0 / 17917100, -4539589.0 / 17917100},
     {-19765991.0 / 29648200, 47925759.0 / 29648200, 467509.0 / 29648200},
     {792561.0 / 44930125, -1921689.0 / 44930125, 42328811.0 / 44930125}}};
// ProPhoto is D50; these take it to and from XYZ D50.
constexpr mat3 lin_prophoto_to_xyz{{{0.79776664490064230, 0.13518129740053308, 0.03134773412839220},
                                    {0.28807482881940, 0.71183523424187, 0.00008993693872},
                                    {0.0, 0.0, 0.82510460251046}}};
constexpr mat3 xyz_to_lin_prophoto{
    {{1.34578688164715830, -0.25557208737979464, -0.05110186497554526},
     {-0.54463070512490190, 1.50824774284514680, 0.02052744743642139},
     {0.0, 0.0, 1.21196754563894520}}};
constexpr mat3 d65_to_d50{{{1.0479297925449969, 0.022946870601609652, -0.05019226628920524},
                           {0.02962780877005599, 0.9904344267538799, -0.017073799063418826},
                           {-0.009243040646204504, 0.015055191490298152, 0.7518742814281371}}};
constexpr mat3 d50_to_d65{{{0.955473421488075, -0.02309845494876471, 0.06325924320057072},
                           {-0.0283697093338637, 1.0099953980813041, 0.021041441191917323},
                           {0.012314014864481998, -0.020507649298898964, 1.330365926242124}}};
constexpr mat3 xyz_to_lms{{{0.8190224379967030, 0.3619062600528904, -0.1288737815209879},
                           {0.0329836539323885, 0.9292868615863434, 0.0361446663506424},
                           {0.0481771893596242, 0.2642395317527308, 0.6335478284694309}}};
constexpr mat3 lms_to_oklab{{{0.2104542683093140, 0.7936177747023054, -0.0040720430116193},
                             {1.9779985324311684, -2.4285922420485799, 0.4505937096174110},
                             {0.0259040424655478, 0.7827717124575296, -0.8086757549230774}}};
constexpr mat3 oklab_to_lms{{{1.0, 0.3963377773761749, 0.2158037573099136},
                             {1.0, -0.1055613458156586, -0.0638541728258133},
                             {1.0, -0.0894841775298119, -1.2914855480194092}}};
constexpr mat3 lms_to_xyz{{{1.2268798758459243, -0.5578149944602171, 0.2813910456659647},
                           {-0.0405757452148008, 1.1122868032803170, -0.0717110580655164},
                           {-0.0763729366746601, -0.4214933324022432, 1.5869240198367816}}};
constexpr vec3 d50_white{0.3457 / 0.3585, 1.0, (1.0 - 0.3457 - 0.3585) / 0.3585};

// The transfer functions, sign-preserving as the specification writes them.
[[nodiscard]] double lin_srgb(double v) noexcept {
    const double a = std::fabs(v);
    const double sign = v < 0 ? -1.0 : 1.0;
    return a <= 0.04045 ? v / 12.92 : sign * std::pow((a + 0.055) / 1.055, 2.4);
}
[[nodiscard]] double gam_srgb(double v) noexcept {
    const double a = std::fabs(v);
    const double sign = v < 0 ? -1.0 : 1.0;
    return a > 0.0031308 ? sign * (1.055 * std::pow(a, 1.0 / 2.4) - 0.055) : 12.92 * v;
}
[[nodiscard]] double lin_prophoto(double v) noexcept {
    const double a = std::fabs(v);
    const double sign = v < 0 ? -1.0 : 1.0;
    return a <= 16.0 / 512 ? v / 16.0 : sign * std::pow(a, 1.8);
}
[[nodiscard]] double gam_prophoto(double v) noexcept {
    const double a = std::fabs(v);
    const double sign = v < 0 ? -1.0 : 1.0;
    return a >= 1.0 / 512 ? sign * std::pow(a, 1.0 / 1.8) : 16.0 * v;
}
[[nodiscard]] double lin_a98(double v) noexcept {
    const double sign = v < 0 ? -1.0 : 1.0;
    return sign * std::pow(std::fabs(v), 563.0 / 256);
}
[[nodiscard]] double gam_a98(double v) noexcept {
    const double sign = v < 0 ? -1.0 : 1.0;
    return sign * std::pow(std::fabs(v), 256.0 / 563);
}
// rec2020 is display-referred: BT.1886's pure 2.4 gamma, not BT.2020's
// camera curve (CSS Color 4 §10.8 since 2024; the corpus's rec2020 answers
// are computed with it).
[[nodiscard]] double lin_2020(double v) noexcept {
    const double sign = v < 0 ? -1.0 : 1.0;
    return sign * std::pow(std::fabs(v), 2.4);
}
[[nodiscard]] double gam_2020(double v) noexcept {
    const double sign = v < 0 ? -1.0 : 1.0;
    return sign * std::pow(std::fabs(v), 1.0 / 2.4);
}
[[nodiscard]] vec3 map3(const vec3 & v, double (*f)(double) noexcept) noexcept {
    return {f(v[0]), f(v[1]), f(v[2])};
}

[[nodiscard]] double normalize_hue(double h) noexcept {
    if (!std::isfinite(h)) { return 0.0; }
    h = std::fmod(h, 360.0);
    if (h < 0) { h += 360.0; }
    return h;
}

// §7.1 and §8: hsl and hwb to and from sRGB, the specification's own code.
[[nodiscard]] vec3 hsl_to_rgb(double hue, double sat, double light) noexcept {
    hue = normalize_hue(hue);
    sat /= 100.0;
    light /= 100.0;
    const auto f = [&](double n) {
        const double k = std::fmod(n + hue / 30.0, 12.0);
        const double a = sat * std::min(light, 1.0 - light);
        return light - a * std::max(-1.0, std::min({k - 3.0, 9.0 - k, 1.0}));
    };
    return {f(0), f(8), f(4)};
}
// The hue of an sRGB triple, the specification's rgbToHue: nought when the
// channels are equal, and no rotation for a negative saturation - that is
// hsl's own step below, and hwb does not take it.
[[nodiscard]] double rgb_to_hue(const vec3 & rgb) noexcept {
    const double red = rgb[0], green = rgb[1], blue = rgb[2];
    const double max = std::max({red, green, blue});
    const double min = std::min({red, green, blue});
    const double d = max - min;
    if (d == 0.0) { return 0.0; }
    double hue = 0.0;
    if (max == red) {
        hue = (green - blue) / d + (green < blue ? 6.0 : 0.0);
    } else if (max == green) {
        hue = (blue - red) / d + 2.0;
    } else {
        hue = (red - green) / d + 4.0;
    }
    hue *= 60.0;
    return hue >= 360.0 ? hue - 360.0 : hue;
}
[[nodiscard]] vec3 rgb_to_hsl(const vec3 & rgb) noexcept {
    const double max = std::max({rgb[0], rgb[1], rgb[2]});
    const double min = std::min({rgb[0], rgb[1], rgb[2]});
    double hue = rgb_to_hue(rgb), sat = 0.0;
    const double light = (min + max) / 2.0;
    if (max != min) {
        sat = (light == 0.0 || light == 1.0) ? 0.0 : (max - light) / std::min(light, 1.0 - light);
    }
    // A very out-of-gamut colour has a negative saturation: rotate the hue by
    // 180 and use the positive one (csswg-drafts/9222).
    if (sat < 0) {
        hue += 180.0;
        sat = std::fabs(sat);
    }
    if (hue >= 360.0) { hue -= 360.0; }
    return {hue, sat * 100.0, light * 100.0};
}
[[nodiscard]] vec3 hwb_to_rgb(double hue, double white, double black) noexcept {
    white /= 100.0;
    black /= 100.0;
    if (white + black >= 1.0) {
        const double gray = white / (white + black);
        return {gray, gray, gray};
    }
    vec3 rgb = hsl_to_rgb(hue, 100.0, 50.0);
    for (double & v : rgb) { v = v * (1.0 - white - black) + white; }
    return rgb;
}
[[nodiscard]] vec3 rgb_to_hwb(const vec3 & rgb) noexcept {
    const double white = std::min({rgb[0], rgb[1], rgb[2]});
    const double black = 1.0 - std::max({rgb[0], rgb[1], rgb[2]});
    return {rgb_to_hue(rgb), white * 100.0, black * 100.0};
}

// §9 and §10: Lab (D50) and OKLab, to and from XYZ D65.
[[nodiscard]] vec3 xyz_to_lab(const vec3 & xyz65) noexcept {
    constexpr double epsilon = 216.0 / 24389;
    constexpr double kappa = 24389.0 / 27;
    const vec3 xyz = mul(d65_to_d50, xyz65);
    vec3 f{};
    for (std::size_t i = 0; i < 3; ++i) {
        const double v = xyz[i] / d50_white[i];
        f[i] = v > epsilon ? std::cbrt(v) : (kappa * v + 16.0) / 116.0;
    }
    return {116.0 * f[1] - 16.0, 500.0 * (f[0] - f[1]), 200.0 * (f[1] - f[2])};
}
[[nodiscard]] vec3 lab_to_xyz(const vec3 & lab) noexcept {
    constexpr double epsilon = 216.0 / 24389;
    constexpr double kappa = 24389.0 / 27;
    vec3 f{};
    f[1] = (lab[0] + 16.0) / 116.0;
    f[0] = lab[1] / 500.0 + f[1];
    f[2] = f[1] - lab[2] / 200.0;
    const double f0 = f[0] * f[0] * f[0];
    const double f2 = f[2] * f[2] * f[2];
    vec3 xyz{f0 > epsilon ? f0 : (116.0 * f[0] - 16.0) / kappa,
             lab[0] > kappa * epsilon ? std::pow((lab[0] + 16.0) / 116.0, 3.0) : lab[0] / kappa,
             f2 > epsilon ? f2 : (116.0 * f[2] - 16.0) / kappa};
    for (std::size_t i = 0; i < 3; ++i) { xyz[i] *= d50_white[i]; }
    return mul(d50_to_d65, xyz);
}
[[nodiscard]] vec3 xyz_to_oklab(const vec3 & xyz) noexcept {
    const vec3 lms = mul(xyz_to_lms, xyz);
    return mul(lms_to_oklab, {std::cbrt(lms[0]), std::cbrt(lms[1]), std::cbrt(lms[2])});
}
[[nodiscard]] vec3 oklab_to_xyz(const vec3 & lab) noexcept {
    const vec3 lms = mul(oklab_to_lms, lab);
    return mul(lms_to_xyz,
               {lms[0] * lms[0] * lms[0], lms[1] * lms[1] * lms[1], lms[2] * lms[2] * lms[2]});
}
[[nodiscard]] vec3 lab_to_lch(const vec3 & lab) noexcept {
    const double hue = std::atan2(lab[2], lab[1]) * 180.0 / std::numbers::pi;
    return {lab[0], std::sqrt(lab[1] * lab[1] + lab[2] * lab[2]), normalize_hue(hue)};
}
[[nodiscard]] vec3 lch_to_lab(const vec3 & lch) noexcept {
    const double h = lch[2] * std::numbers::pi / 180.0;
    return {lch[0], lch[1] * std::cos(h), lch[1] * std::sin(h)};
}

// Every channel to XYZ D65, `none` as zero.
[[nodiscard]] vec3 to_xyz(space s, vec3 c) noexcept {
    switch (s) {
    case space::hsl: c = hsl_to_rgb(c[0], c[1], c[2]); [[fallthrough]];
    case space::srgb: return mul(lin_srgb_to_xyz, map3(c, lin_srgb));
    case space::hwb: return mul(lin_srgb_to_xyz, map3(hwb_to_rgb(c[0], c[1], c[2]), lin_srgb));
    case space::srgb_linear: return mul(lin_srgb_to_xyz, c);
    case space::display_p3: return mul(lin_p3_to_xyz, map3(c, lin_srgb));
    case space::display_p3_linear: return mul(lin_p3_to_xyz, c);
    case space::a98_rgb: return mul(lin_a98_to_xyz, map3(c, lin_a98));
    case space::prophoto_rgb:
        return mul(d50_to_d65, mul(lin_prophoto_to_xyz, map3(c, lin_prophoto)));
    case space::rec2020: return mul(lin_2020_to_xyz, map3(c, lin_2020));
    case space::xyz_d50: return mul(d50_to_d65, c);
    case space::xyz_d65: return c;
    case space::lab: return lab_to_xyz(c);
    case space::lch: return lab_to_xyz(lch_to_lab(c));
    case space::oklab: return oklab_to_xyz(c);
    case space::oklch: return oklab_to_xyz(lch_to_lab(c));
    }
    return c;
}
[[nodiscard]] vec3 from_xyz(space s, const vec3 & xyz) noexcept {
    switch (s) {
    case space::srgb: return map3(mul(xyz_to_lin_srgb, xyz), gam_srgb);
    case space::hsl: return rgb_to_hsl(map3(mul(xyz_to_lin_srgb, xyz), gam_srgb));
    case space::hwb: return rgb_to_hwb(map3(mul(xyz_to_lin_srgb, xyz), gam_srgb));
    case space::srgb_linear: return mul(xyz_to_lin_srgb, xyz);
    case space::display_p3: return map3(mul(xyz_to_lin_p3, xyz), gam_srgb);
    case space::display_p3_linear: return mul(xyz_to_lin_p3, xyz);
    case space::a98_rgb: return map3(mul(xyz_to_lin_a98, xyz), gam_a98);
    case space::prophoto_rgb:
        return map3(mul(xyz_to_lin_prophoto, mul(d65_to_d50, xyz)), gam_prophoto);
    case space::rec2020: return map3(mul(xyz_to_lin_2020, xyz), gam_2020);
    case space::xyz_d50: return mul(d65_to_d50, xyz);
    case space::xyz_d65: return xyz;
    case space::lab: return xyz_to_lab(xyz);
    case space::lch: return lab_to_lch(xyz_to_lab(xyz));
    case space::oklab: return xyz_to_oklab(xyz);
    case space::oklch: return lab_to_lch(xyz_to_oklab(xyz));
    }
    return xyz;
}

// §4.4.1's POWERLESS HUE: a colourfulness at or under the space's own epsilon
// - `hsl(180 0.001% 50%)` and `lch(20 0.0015 180)` are the corpus's
// boundaries - or an hwb whose white and black fill the whole colour. A
// missing colourfulness is nought here, as it is for every other purpose.
[[nodiscard]] double hue_epsilon(space s) noexcept {
    switch (s) {
    case space::hsl: return 0.001;
    case space::lch: return 0.0015;
    case space::oklch: return 0.000004;
    default: return 0.0;
    }
}

// A colour about to be converted, its powerless hue made missing (§4.4.1):
// the colourfulness that made it powerless goes to nought so the conversion
// does not amplify floating-point noise, and an hwb fills its white and
// black to 100. A colour written by hand never has this done to it -
// `hsl(180 0% 50%)` keeps its hue for as long as it stays hsl.
void settle_powerless(resolved & r) noexcept {
    const int h = hue_slot(r.cs);
    if (h < 0) { return; }
    if (r.cs == space::hwb) {
        const double w = r.none[1] ? 0.0 : r.c[1];
        const double b = r.none[2] ? 0.0 : r.c[2];
        if (w + b < 99.999) { return; }
        r.none[0] = true;
        r.c[0] = 0.0;
        if (w + b < 100.0) {
            if (!r.none[1] && !r.none[2]) {
                r.c[2] = 100.0 - w;
            } else if (!r.none[1]) {
                r.c[1] = 100.0;
            } else if (!r.none[2]) {
                r.c[2] = 100.0;
            }
        }
        return;
    }
    const auto c = static_cast<std::size_t>(colorfulness_slot(r.cs));
    const double chroma = r.none[c] ? 0.0 : r.c[c];
    if (chroma > hue_epsilon(r.cs)) { return; }
    r.none[static_cast<std::size_t>(h)] = true;
    r.c[static_cast<std::size_t>(h)] = 0.0;
    if (chroma > 0.0) { r.c[c] = 0.0; }
}

[[nodiscard]] bool srgb_family(space s) noexcept {
    return s == space::srgb || s == space::hsl || s == space::hwb;
}

// ONE COLOUR IN ANOTHER SPACE: §11.2's algorithm with §12.2's carrying
// forward of missing components. The source's powerless hue is made missing
// first; a missing component lands in the analogous slot of the target; and
// when every source component WITHOUT an analogue is missing, every target
// component without one is missing too - so `lab(50 none none)` is
// `lch(50 none none)`, `hwb(180 none none)` is `hsl(180 none none)`, and
// `rgb(none none none)` is missing everything wherever it goes. A hue the
// conversion produces powerless is missing as well. A colour is never
// converted to its own space, so a hand-written powerless hue stays.
[[nodiscard]] resolved convert(const resolved & from, space to) {
    if (from.cs == to) { return from; }
    resolved src = from;
    settle_powerless(src);
    vec3 c{};
    for (std::size_t i = 0; i < 3; ++i) { c[i] = src.none[i] ? 0.0 : src.c[i]; }
    resolved out;
    out.cs = to;
    out.alpha = from.alpha;
    out.alpha_none = from.alpha_none;
    // Within the sRGB family, and between a Lab and its own LCH, the
    // conversion is the specification's own arithmetic and not a trip through
    // XYZ, so `hsl(120 0% 50%)` comes back exactly 0.5 and rounds to 128
    // rather than 127, and `lab(50 10 0)` is `lch(50 10 0)` and not 360.
    if (srgb_family(src.cs) && srgb_family(to)) {
        vec3 rgb = c;
        if (src.cs == space::hsl) { rgb = hsl_to_rgb(c[0], c[1], c[2]); }
        if (src.cs == space::hwb) { rgb = hwb_to_rgb(c[0], c[1], c[2]); }
        out.c = to == space::srgb ? rgb : (to == space::hsl ? rgb_to_hsl(rgb) : rgb_to_hwb(rgb));
    } else if ((src.cs == space::lab && to == space::lch) ||
               (src.cs == space::oklab && to == space::oklch)) {
        out.c = lab_to_lch(c);
    } else if ((src.cs == space::lch && to == space::lab) ||
               (src.cs == space::oklch && to == space::oklab)) {
        out.c = lch_to_lab(c);
    } else {
        out.c = from_xyz(to, to_xyz(src.cs, c));
    }
    // The conversion's own powerless hue is judged on what it computed - a
    // carried-forward missing saturation does not make `hwb(180 none none)`
    // lose the hue it converts with - and the carried components are
    // re-inserted after (§12.2's order).
    settle_powerless(out);
    const std::array<part, 3> from_parts = parts_of(src.cs);
    const std::array<part, 3> to_parts = parts_of(to);
    bool rest_any = false, rest_all_missing = true;
    for (std::size_t i = 0; i < 3; ++i) {
        const int j = slot_of(to, from_parts[i]);
        if (j >= 0) {
            if (src.none[i]) { out.none[static_cast<std::size_t>(j)] = true; }
        } else {
            rest_any = true;
            rest_all_missing = rest_all_missing && src.none[i];
        }
    }
    if (rest_any && rest_all_missing) {
        for (std::size_t j = 0; j < 3; ++j) {
            if (slot_of(src.cs, to_parts[j]) < 0) { out.none[j] = true; }
        }
    }
    if (const int h = hue_slot(to); h >= 0) {
        out.c[static_cast<std::size_t>(h)] = normalize_hue(out.c[static_cast<std::size_t>(h)]);
    }
    return out;
}

// --- serialising numbers ----------------------------------------------------

// A channel value as every engine prints one: six significant digits, no
// exponent, trailing zeros dropped. `0.501961` for 128/255 and `73.3386` for
// 1.28rad.
[[nodiscard]] std::string channel_text(double v) {
    if (std::isnan(v)) { return "calc(NaN)"; }
    if (std::isinf(v)) { return v > 0 ? "calc(infinity)" : "calc(-infinity)"; }
    if (v == 0.0) { return "0"; }
    const double magnitude = std::fabs(v);
    int decimals = 0;
    if (magnitude < 1e6) {
        const int exponent = static_cast<int>(std::floor(std::log10(magnitude)));
        decimals = std::max(0, 5 - exponent);
    }
    const double scale = std::pow(10.0, decimals);
    const double rounded = std::round(v * scale) / scale;
    if (rounded == 0.0) { return "0"; }
    std::array<char, 64> buffer{};
    const std::to_chars_result written = std::to_chars(buffer.data(), buffer.data() + buffer.size(),
                                                       rounded, std::chars_format::fixed, decimals);
    if (written.ec != std::errc{}) { return "0"; }
    std::string out{buffer.data(), static_cast<std::size_t>(written.ptr - buffer.data())};
    if (out.find('.') != std::string::npos) {
        while (out.back() == '0') { out.pop_back(); }
        if (out.back() == '.') { out.pop_back(); }
    }
    if (out == "-0") { return "0"; }
    return out;
}

// A legacy sRGB channel: clamped to the byte and rounded, half up.
[[nodiscard]] std::string byte_text(double v) {
    if (std::isnan(v)) { v = 0; }
    // Snapped to a millionth first: `hwb(120 30% 50%)` is 127.49999999999997
    // of green in doubles and 128 in every browser.
    const double clamped = std::round(std::min(255.0, std::max(0.0, v)) * 1e6) / 1e6;
    return std::to_string(static_cast<int>(std::floor(clamped + 0.5)));
}

// CSSOM §6.7.2's <alphavalue> for an 8-bit alpha: the two-decimal fraction
// that rounds to the same byte if there is one, else three decimals.
[[nodiscard]] std::string legacy_alpha_text(double alpha) {
    if (std::isnan(alpha)) { alpha = 0; }
    alpha = std::min(1.0, std::max(0.0, alpha));
    const int byte = static_cast<int>(std::floor(alpha * 255.0 + 0.5));
    for (int i = 0; i <= 100; ++i) {
        if (static_cast<int>(std::floor(i * 255.0 / 100.0 + 0.5)) == byte) {
            return channel_text(i / 100.0);
        }
    }
    return channel_text(std::round(byte / 255.0 * 1000.0) / 1000.0);
}

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
constexpr std::string_view context_free_units[] = {"px",  "cm",   "mm",  "q",    "in", "pt", "pc",
                                                   "deg", "grad", "rad", "turn", "s",  "ms"};

[[nodiscard]] bool parse_time_answerable(const token_stream & ts, std::size_t begin,
                                         std::size_t end) {
    for (std::size_t i = begin; i < end; ++i) {
        const css_token & t = ts.tokens[i];
        if (t.type == token_type::dimension &&
            !ascii_iequals_any(ts.unit_of(t), context_free_units)) {
            return false;
        }
        if (t.type == token_type::function) {
            const std::string_view raw = ts.text_of(t);
            const std::string_view name = raw.substr(0, raw.size() - 1);
            if (ascii_iequals_any(name, {"sibling-index", "sibling-count", "random", "var", "env",
                                         "attr", "random-item", "inherit"})) {
                return false;
            }
        }
    }
    return true;
}

constexpr std::string_view math_names[] = {
    "calc",     "min",      "max",           "clamp",         "round", "mod",  "rem",
    "sin",      "cos",      "tan",           "asin",          "acos",  "atan", "atan2",
    "pow",      "sqrt",     "hypot",         "log",           "exp",   "abs",  "sign",
    "calc-mix", "progress", "sibling-index", "sibling-count", "random"};

class reader {
public:
    reader(const token_stream & ts, std::size_t begin, std::size_t end)
        : ts_(ts), at_(begin), end_(end) {}

    [[nodiscard]] std::unique_ptr<parsed> read_whole() {
        skip_ws();
        std::unique_ptr<parsed> out = color();
        if (!out) { return nullptr; }
        skip_ws();
        if (at_ != end_) { return nullptr; }
        return out;
    }

private:
    [[nodiscard]] const css_token & peek() const noexcept {
        return at_ < end_ ? ts_.tokens[at_] : ts_.tokens.back(); // the eof
    }
    void skip_ws() noexcept {
        while (at_ < end_ && ts_.tokens[at_].type == token_type::whitespace) { ++at_; }
    }
    [[nodiscard]] bool at_close() const noexcept {
        return at_ >= end_ || ts_.tokens[at_].type == token_type::close_paren;
    }
    // Consumes the `)` that closes the function being read; EOF closes it too.
    [[nodiscard]] bool close() noexcept {
        skip_ws();
        if (at_ >= end_) { return true; }
        if (ts_.tokens[at_].type != token_type::close_paren) { return false; }
        ++at_;
        return true;
    }
    [[nodiscard]] bool take_comma() noexcept {
        skip_ws();
        if (peek().type != token_type::comma) { return false; }
        ++at_;
        skip_ws();
        return true;
    }
    [[nodiscard]] std::string_view function_name_at(std::size_t i) const noexcept {
        const std::string_view raw = ts_.text_of(ts_.tokens[i]);
        return raw.empty() ? raw : raw.substr(0, raw.size() - 1);
    }
    // One past the `)` matching the function at `open`, or `end_`.
    [[nodiscard]] std::size_t matching_close(std::size_t open) const noexcept {
        int depth = 0;
        for (std::size_t i = open; i < end_; ++i) {
            const token_type type = ts_.tokens[i].type;
            if (type == token_type::function || type == token_type::open_paren ||
                type == token_type::open_square || type == token_type::open_curly) {
                ++depth;
            } else if (type == token_type::close_paren || type == token_type::close_square ||
                       type == token_type::close_curly) {
                if (--depth == 0) { return i + 1; }
            }
        }
        return end_;
    }
    [[nodiscard]] std::string text_between(std::size_t begin, std::size_t end) const {
        std::string out;
        for (std::size_t i = begin; i < end; ++i) { out += ts_.text_of(ts_.tokens[i]); }
        return out;
    }

    // --- <color> ---
    [[nodiscard]] std::unique_ptr<parsed> color() {
        const css_token & t = peek();
        if (t.type == token_type::ident) {
            const std::string word = ascii_lower_copy(ts_.text_of(t));
            auto out = std::make_unique<parsed>();
            out->k = parsed::kind::keyword;
            out->keyword = word;
            if (word == "currentcolor" || word == "transparent" || word.starts_with("-webkit-") ||
                find_named(named_colors, word) != nullptr ||
                find_named(system_colors, word) != nullptr) {
                ++at_;
                return out;
            }
            return nullptr;
        }
        if (t.type == token_type::hash) {
            const std::string_view digits = ts_.text_of(t).substr(1);
            if (digits.size() != 3 && digits.size() != 4 && digits.size() != 6 &&
                digits.size() != 8) {
                return nullptr;
            }
            std::array<int, 8> v{};
            for (std::size_t i = 0; i < digits.size(); ++i) {
                v[i] = hex_value(digits[i]);
                if (v[i] < 0) { return nullptr; }
            }
            auto out = std::make_unique<parsed>();
            out->k = parsed::kind::hex;
            out->fixed.legacy = true;
            if (digits.size() <= 4) {
                for (std::size_t i = 0; i < 3; ++i) { out->fixed.c[i] = v[i] * 17 / 255.0; }
                if (digits.size() == 4) { out->fixed.alpha = v[3] * 17 / 255.0; }
            } else {
                for (std::size_t i = 0; i < 3; ++i) {
                    out->fixed.c[i] = (v[2 * i] * 16 + v[2 * i + 1]) / 255.0;
                }
                if (digits.size() == 8) { out->fixed.alpha = (v[6] * 16 + v[7]) / 255.0; }
            }
            ++at_;
            return out;
        }
        if (t.type != token_type::function) { return nullptr; }
        const std::string name = ascii_lower_copy(function_name_at(at_));
        ++at_;
        skip_ws();
        std::unique_ptr<parsed> out;
        if (name == "rgb" || name == "rgba") {
            out = color_function("rgb", space::srgb);
        } else if (name == "hsl" || name == "hsla") {
            out = color_function("hsl", space::hsl);
        } else if (name == "hwb") {
            out = color_function("hwb", space::hwb);
        } else if (name == "lab" || name == "lch" || name == "oklab" || name == "oklch") {
            out = color_function(name, name == "lab"     ? space::lab
                                       : name == "lch"   ? space::lch
                                       : name == "oklab" ? space::oklab
                                                         : space::oklch);
        } else if (name == "color") {
            out = color_function("color", space::srgb);
        } else if (name == "color-mix") {
            out = color_mix();
        } else if (name == "light-dark") {
            out = two_colors(parsed::kind::light_dark);
        } else if (name == "alpha") {
            out = alpha_function();
        } else if (name == "contrast-color") {
            out = std::make_unique<parsed>();
            out->k = parsed::kind::contrast;
            out->origin = color();
            if (!out->origin) { return nullptr; }
        } else if (name == "color-layers") {
            out = color_layers();
        } else {
            return nullptr;
        }
        if (!out || !close()) { return nullptr; }
        return out;
    }

    // rgb() hsl() hwb() lab() lch() oklab() oklch() color(), absolute or
    // relative. `at_` is past the `(` and any whitespace.
    [[nodiscard]] std::unique_ptr<parsed> color_function(std::string fn, space cs) {
        auto out = std::make_unique<parsed>();
        out->k = parsed::kind::absolute;
        out->fn = std::move(fn);
        out->cs = cs;
        out->bytes = out->fn == "rgb";
        if (peek().type == token_type::ident && ascii_iequals(ts_.text_of(peek()), "from")) {
            ++at_;
            skip_ws();
            out->k = parsed::kind::relative;
            out->origin = color();
            if (!out->origin) { return nullptr; }
            skip_ws();
        }
        if (out->fn == "color") {
            if (peek().type != token_type::ident) { return nullptr; }
            const std::optional<space> named_space = predefined_space(ts_.text_of(peek()));
            if (!named_space) { return nullptr; }
            out->cs = *named_space;
            ++at_;
            skip_ws();
        }
        const bool relative = out->k == parsed::kind::relative;
        const std::array<std::string_view, 3> keywords = channel_keywords(out->cs);
        // The first channel decides the syntax: a comma after it is the legacy
        // form, which only rgb() and hsl() have and a relative colour never does.
        for (std::size_t slot = 0; slot < 3; ++slot) {
            std::optional<channel> one = read_channel(out->cs, static_cast<int>(slot), relative,
                                                      out->legacy_syntax, keywords);
            if (!one) { return nullptr; }
            out->ch[slot] = std::move(*one);
            skip_ws();
            if (slot == 0) {
                if (peek().type == token_type::comma) {
                    if (relative || (out->fn != "rgb" && out->fn != "hsl")) { return nullptr; }
                    out->legacy_syntax = true;
                    // `none` is not part of the legacy grammar.
                    if (out->ch[0].k == channel::kind::none) { return nullptr; }
                }
            }
            if (slot < 2) {
                if (out->legacy_syntax) {
                    if (!take_comma()) { return nullptr; }
                } else if (peek().type == token_type::comma) {
                    return nullptr;
                }
            }
        }
        skip_ws();
        if (out->legacy_syntax) {
            if (peek().type == token_type::comma) {
                ++at_;
                skip_ws();
                out->alpha = read_alpha(relative, true, keywords);
                if (!out->alpha) { return nullptr; }
            }
        } else if (peek().type == token_type::delim && ts_.text_of(peek()) == "/") {
            ++at_;
            skip_ws();
            out->alpha = read_alpha(relative, false, keywords);
            if (!out->alpha) { return nullptr; }
        }
        skip_ws();
        if (!at_close()) { return nullptr; }
        // The legacy forms: rgb() takes numbers or percentages but not both,
        // hsl() a number-or-angle hue and two percentages.
        if (out->legacy_syntax) {
            if (out->fn == "rgb") {
                const auto kind_of = [](const channel & c) {
                    return c.k == channel::kind::calc ? c.resolved_kind : c.k;
                };
                std::optional<bool> percent; // decided by the first channel with an answer
                for (const channel & c : out->ch) {
                    if (c.k == channel::kind::none) { return nullptr; }
                    if (c.k == channel::kind::calc && !c.resolvable) { continue; }
                    const bool this_percent = kind_of(c) == channel::kind::percent;
                    if (percent && *percent != this_percent) { return nullptr; }
                    percent = this_percent;
                }
            } else {
                for (std::size_t slot = 1; slot < 3; ++slot) {
                    const channel & c = out->ch[slot];
                    if (c.k == channel::kind::number) { return nullptr; }
                    if (c.k == channel::kind::calc && c.resolvable &&
                        c.resolved_kind == channel::kind::number) {
                        return nullptr;
                    }
                }
            }
            if (out->alpha && out->alpha->k == channel::kind::none) { return nullptr; }
        }
        return out;
    }

    // One channel of a colour function. `slot` 0..2, or 3 for the alpha.
    [[nodiscard]] std::optional<channel> read_channel(space cs, int slot, bool relative,
                                                      bool legacy,
                                                      std::span<const std::string_view> keywords) {
        const css_token & t = peek();
        channel out;
        const bool hue = slot == hue_slot(cs);
        const bool takes_percent = cs != space::hsl && cs != space::hwb ? true : slot != 0;
        switch (t.type) {
        case token_type::number:
            out.k = channel::kind::number;
            out.value = t.number;
            ++at_;
            return out;
        case token_type::percentage:
            if (!takes_percent || hue) { return std::nullopt; }
            out.k = channel::kind::percent;
            out.value = t.number;
            ++at_;
            return out;
        case token_type::dimension: {
            if (!hue) { return std::nullopt; }
            const std::optional<double> degrees = angle_degrees(t.number, ts_.unit_of(t));
            if (!degrees) { return std::nullopt; }
            out.k = channel::kind::angle;
            out.value = *degrees;
            ++at_;
            return out;
        }
        case token_type::ident: {
            const std::string_view word = ts_.text_of(t);
            if (ascii_iequals(word, "none")) {
                if (legacy) { return std::nullopt; }
                out.k = channel::kind::none;
                ++at_;
                return out;
            }
            if (!relative) { return std::nullopt; }
            if (!ascii_iequals(word, "alpha") && !ascii_iequals_any(word, keywords)) {
                return std::nullopt;
            }
            out.k = channel::kind::keyword;
            out.text = ascii_lower_copy(word);
            ++at_;
            return out;
        }
        case token_type::function: {
            if (!ascii_iequals_any(function_name_at(at_), math_names)) { return std::nullopt; }
            const std::size_t open = at_;
            const std::size_t after = matching_close(open);
            out.k = channel::kind::calc;
            out.raw_calc = text_between(open, after);
            if (after == end_ || ts_.tokens[after - 1].type != token_type::close_paren) {
                out.raw_calc += ')';
            }
            at_ = after;
            // The relative colour's keywords are <number> terms of the
            // expression; the specified form is simplified over them.
            std::vector<std::string_view> symbols;
            if (relative) {
                symbols.assign(keywords.begin(), keywords.end());
                symbols.emplace_back("alpha");
            }
            const number_symbols_scope scope{symbols};
            // OF A TYPE THE SLOT TAKES: a number, a percentage, or in the hue
            // slot an angle. `calc(h + 1deg)` adds a number to an angle and
            // `calc(0.56turn * -0.43turn)` is an angle squared; both are
            // syntax errors wherever they sit.
            const auto [outcome, sum] = evaluate_symbolic(out.raw_calc);
            if (outcome == math_outcome::invalid) { return std::nullopt; }
            if (outcome == math_outcome::resolved) {
                if (!sum.simple()) { return std::nullopt; }
                const numeric_type type = sum.type();
                // A percentage travels through the evaluator as a length with
                // an unresolved part; one with no pixels is a <percentage>.
                const bool percentage =
                    type == numeric_type::length && sum.has_percent && sum.value == 0.0;
                if (type == numeric_type::angle) {
                    if (!hue) { return std::nullopt; }
                } else if (type != numeric_type::number && !percentage) {
                    return std::nullopt;
                }
                if (sum.has_percent && (!takes_percent || hue)) { return std::nullopt; }
            }
            out.text = simplify_math(out.raw_calc);
            if (!relative && parse_time_answerable(ts_, open, after)) {
                const math_answer answer = evaluate_math(out.raw_calc, length_context{});
                if (answer.outcome == math_outcome::resolved) {
                    out.resolvable = true;
                    if (answer.value.has_percent && answer.value.px == 0.0) {
                        out.resolved_kind = channel::kind::percent;
                        out.resolved_value = answer.value.percent;
                    } else if (answer.value.type == numeric_type::angle) {
                        out.resolved_kind = channel::kind::angle;
                        out.resolved_value = answer.value.px;
                    } else {
                        out.resolved_kind = channel::kind::number;
                        out.resolved_value = answer.value.px;
                    }
                }
            }
            return out;
        }
        default: return std::nullopt;
        }
    }

    [[nodiscard]] std::optional<channel> read_alpha(bool relative, bool legacy,
                                                    std::span<const std::string_view> keywords) {
        // The alpha slot takes a number or a percentage, never an angle; the
        // hsl/hwb "hue slot" logic is sidestepped by asking as sRGB's slot 3.
        return read_channel(space::srgb, 3, relative, legacy, keywords);
    }

    [[nodiscard]] static std::optional<double> angle_degrees(double v, std::string_view unit) {
        if (ascii_iequals(unit, "deg")) { return v; }
        if (ascii_iequals(unit, "grad")) { return v * 0.9; }
        if (ascii_iequals(unit, "rad")) { return v * 180.0 / std::numbers::pi; }
        if (ascii_iequals(unit, "turn")) { return v * 360.0; }
        return std::nullopt;
    }

    // color-mix( <color-interpolation-method>? , [ <color> && <percentage>? ]#{1,} )
    [[nodiscard]] std::unique_ptr<parsed> color_mix() {
        auto out = std::make_unique<parsed>();
        out->k = parsed::kind::mix;
        if (peek().type == token_type::ident && ascii_iequals(ts_.text_of(peek()), "in")) {
            ++at_;
            skip_ws();
            if (peek().type != token_type::ident) { return nullptr; }
            const std::optional<space> s = interpolation_space(ts_.text_of(peek()));
            if (!s) { return nullptr; }
            out->mix_space = *s;
            ++at_;
            skip_ws();
            if (peek().type == token_type::ident) {
                const std::string method = ascii_lower_copy(ts_.text_of(peek()));
                if (!ascii_iequals_any(method, {"shorter", "longer", "increasing", "decreasing"})) {
                    return nullptr;
                }
                if (hue_slot(*s) < 0) { return nullptr; }
                ++at_;
                skip_ws();
                if (peek().type != token_type::ident ||
                    !ascii_iequals(ts_.text_of(peek()), "hue")) {
                    return nullptr;
                }
                ++at_;
                skip_ws();
                if (method != "shorter") { out->hue_method = method; }
            }
            if (!take_comma()) { return nullptr; }
        }
        for (;;) {
            mix_item item;
            // The percentage may come before or after the colour.
            if (!read_weight(item)) { return nullptr; }
            skip_ws();
            item.color = color();
            if (!item.color) { return nullptr; }
            skip_ws();
            if (!item.weight && !read_weight(item)) { return nullptr; }
            out->items.push_back(std::move(item));
            skip_ws();
            if (peek().type == token_type::comma) {
                ++at_;
                skip_ws();
                continue;
            }
            break;
        }
        if (out->items.empty()) { return nullptr; }
        return out;
    }

    // A weight for color-mix: `<percentage [0,100]>`, or a math function.
    [[nodiscard]] bool read_weight(mix_item & item) {
        const css_token & t = peek();
        if (t.type == token_type::percentage) {
            if (t.number < 0 || t.number > 100) { return false; }
            channel w;
            w.k = channel::kind::percent;
            w.value = t.number;
            item.weight = w;
            ++at_;
            return true;
        }
        if (t.type == token_type::function &&
            ascii_iequals_any(function_name_at(at_), math_names)) {
            static constexpr std::array<std::string_view, 3> none_keywords{"alpha", "alpha",
                                                                           "alpha"};
            std::optional<channel> w = read_channel(space::srgb, 3, false, false, none_keywords);
            if (!w) { return false; }
            if (w->resolvable) {
                if (w->resolved_kind != channel::kind::percent) { return false; }
                if (w->resolved_value < 0 || w->resolved_value > 100) { return false; }
            }
            item.weight = std::move(*w);
            return true;
        }
        return true; // no weight here
    }

    [[nodiscard]] std::unique_ptr<parsed> two_colors(parsed::kind k) {
        auto out = std::make_unique<parsed>();
        out->k = k;
        for (int i = 0; i < 2; ++i) {
            mix_item item;
            item.color = color();
            if (!item.color) { return nullptr; }
            out->items.push_back(std::move(item));
            if (i == 0 && !take_comma()) { return nullptr; }
        }
        return out;
    }

    // alpha( from <color> / <alpha-value> ), CSS Color 5.
    [[nodiscard]] std::unique_ptr<parsed> alpha_function() {
        auto out = std::make_unique<parsed>();
        out->k = parsed::kind::alpha_fn;
        if (peek().type != token_type::ident || !ascii_iequals(ts_.text_of(peek()), "from")) {
            return nullptr;
        }
        ++at_;
        skip_ws();
        out->origin = color();
        if (!out->origin) { return nullptr; }
        skip_ws();
        if (peek().type != token_type::delim || ts_.text_of(peek()) != "/") { return nullptr; }
        ++at_;
        skip_ws();
        static constexpr std::array<std::string_view, 3> none_keywords{"alpha", "alpha", "alpha"};
        out->alpha = read_alpha(true, false, none_keywords);
        if (!out->alpha) { return nullptr; }
        return out;
    }

    // color-layers( [ <blend-mode> , ]? <color># )
    [[nodiscard]] std::unique_ptr<parsed> color_layers() {
        auto out = std::make_unique<parsed>();
        out->k = parsed::kind::layers;
        if (peek().type == token_type::ident) {
            const std::string word = ascii_lower_copy(ts_.text_of(peek()));
            if (ascii_iequals_any(word, {"normal", "multiply", "screen", "overlay", "darken",
                                         "lighten", "color-dodge", "color-burn", "hard-light",
                                         "soft-light", "difference", "exclusion", "hue",
                                         "saturation", "color", "luminosity"})) {
                ++at_;
                if (!take_comma()) { return nullptr; }
                if (word != "normal") { out->blend_mode = word; }
            }
        }
        for (;;) {
            mix_item item;
            item.color = color();
            if (!item.color) { return nullptr; }
            out->items.push_back(std::move(item));
            skip_ws();
            if (peek().type == token_type::comma) {
                ++at_;
                skip_ws();
                continue;
            }
            break;
        }
        return out;
    }

    const token_stream & ts_;
    std::size_t at_;
    std::size_t end_;
};

// --- the specified serialisation ---------------------------------------------

// A literal channel in the function's own unit: a percentage becomes the
// number it stands for, an angle its degrees.
[[nodiscard]] double literal_value(const channel & c, space cs, int slot, bool bytes) noexcept {
    switch (c.k) {
    case channel::kind::percent: return c.value * percent_reference(cs, slot, bytes) / 100.0;
    case channel::kind::calc:
        if (c.resolved_kind == channel::kind::percent) {
            return c.resolved_value * percent_reference(cs, slot, bytes) / 100.0;
        }
        return c.resolved_value;
    default: return c.value;
    }
}

// The specification's parse-time clamps: rgb() channels to the byte, hsl()
// saturation and every chroma to nought and above, lightness to its range,
// alpha to [0, 1].
[[nodiscard]] double clamp_literal(double v, space cs, int slot, bool absolute,
                                   bool bytes) noexcept {
    if (std::isnan(v)) { v = 0; }
    if (slot == 3) { return std::min(1.0, std::max(0.0, v)); }
    switch (cs) {
    case space::srgb: return absolute && bytes ? std::min(255.0, std::max(0.0, v)) : v;
    case space::hsl:
        if (slot == 1 && absolute) { return std::max(0.0, v); }
        return slot == 0 ? normalize_hue(v) : v;
    case space::hwb: return slot == 0 ? normalize_hue(v) : v;
    case space::lab:
    case space::lch:
        if (slot == 0) { return std::min(100.0, std::max(0.0, v)); }
        if (cs == space::lch && slot == 1) { return std::max(0.0, v); }
        if (cs == space::lch && slot == 2) { return normalize_hue(v); }
        return v;
    case space::oklab:
    case space::oklch:
        if (slot == 0) { return std::min(1.0, std::max(0.0, v)); }
        if (cs == space::oklch && slot == 1) { return std::max(0.0, v); }
        if (cs == space::oklch && slot == 2) { return normalize_hue(v); }
        return v;
    default: return v;
    }
}

[[nodiscard]] bool channel_settled(const channel & c) noexcept {
    return c.k == channel::kind::number || c.k == channel::kind::percent ||
           c.k == channel::kind::angle || (c.k == channel::kind::calc && c.resolvable);
}

// Every channel known at parse time, `none` counted as known only when
// `none_ok`: the legacy serialisation resolves `rgb(none none none)` as
// black but keeps `hsl(120 none 50%)` in the modern form.
[[nodiscard]] bool all_settled(const parsed & p, bool none_ok) noexcept {
    for (const channel & c : p.ch) {
        if (c.k == channel::kind::none ? !none_ok : !channel_settled(c)) { return false; }
    }
    if (p.alpha && (p.alpha->k == channel::kind::none ? !none_ok : !channel_settled(*p.alpha))) {
        return false;
    }
    return true;
}

// An absolute rgb()/hsl()/hwb() with every channel settled, as sRGB.
[[nodiscard]] resolved settled_legacy(const parsed & p) {
    vec3 c{};
    for (std::size_t slot = 0; slot < 3; ++slot) {
        const channel & ch = p.ch[slot];
        c[slot] = ch.k == channel::kind::none
                      ? 0.0
                      : clamp_literal(literal_value(ch, p.cs, static_cast<int>(slot), true), p.cs,
                                      static_cast<int>(slot), true, true);
    }
    resolved out;
    out.legacy = true;
    if (p.cs == space::srgb) {
        for (std::size_t i = 0; i < 3; ++i) { out.c[i] = c[i] / 255.0; }
    } else if (p.cs == space::hsl) {
        out.c = hsl_to_rgb(c[0], c[1], c[2]);
    } else {
        out.c = hwb_to_rgb(c[0], c[1], c[2]);
    }
    if (p.alpha) {
        out.alpha =
            p.alpha->k == channel::kind::none
                ? 0.0
                : clamp_literal(literal_value(*p.alpha, p.cs, 3, true), p.cs, 3, true, true);
    }
    return out;
}

// An absolute hsl()/hwb() an `rgb()` serialisation cannot stand in for:
// its hue is powerless as written (§4.4.1), or its lightness is at either
// end, so that converting the rgb() back makes the hue MISSING where the
// author's colour kept it.
[[nodiscard]] bool origin_loses_in_rgb(const parsed & p) {
    if (p.cs != space::hsl && p.cs != space::hwb) { return false; }
    vec3 c{};
    for (std::size_t slot = 0; slot < 3; ++slot) {
        const channel & ch = p.ch[slot];
        c[slot] = ch.k == channel::kind::none
                      ? 0.0
                      : clamp_literal(literal_value(ch, p.cs, static_cast<int>(slot), true), p.cs,
                                      static_cast<int>(slot), true, true);
    }
    if (p.cs == space::hwb) { return c[1] + c[2] >= 99.999; }
    return c[1] <= hue_epsilon(space::hsl) || c[2] <= 0.0 || c[2] >= 100.0;
}

[[nodiscard]] std::string legacy_text(const resolved & r) {
    std::string out = byte_text(r.c[0] * 255.0) + ", " + byte_text(r.c[1] * 255.0) + ", " +
                      byte_text(r.c[2] * 255.0);
    const double alpha = r.alpha_none ? 0.0 : r.alpha;
    if (alpha >= 1.0) { return "rgb(" + out + ")"; }
    return "rgba(" + out + ", " + legacy_alpha_text(alpha) + ")";
}

std::string serialize_specified(const parsed & p, bool as_origin);

// One channel of an absolute colour in the modern syntax.
[[nodiscard]] std::string modern_channel_text(const channel & c, space cs, int slot,
                                              bool resolve_calc, bool bytes) {
    switch (c.k) {
    case channel::kind::none: return "none";
    case channel::kind::keyword: return c.text;
    case channel::kind::calc:
        if (resolve_calc && c.resolvable) {
            return channel_text(
                clamp_literal(literal_value(c, cs, slot, bytes), cs, slot, true, bytes));
        }
        return c.text;
    default:
        return channel_text(
            clamp_literal(literal_value(c, cs, slot, bytes), cs, slot, true, bytes));
    }
}

// A relative colour's channel: kept as written, numbers canonical.
[[nodiscard]] std::string relative_channel_text(const channel & c) {
    switch (c.k) {
    case channel::kind::none: return "none";
    case channel::kind::keyword: return c.text;
    case channel::kind::calc: return c.text;
    case channel::kind::percent: return channel_text(c.value) + "%";
    case channel::kind::angle: return channel_text(c.value) + "deg";
    default: return channel_text(c.value);
    }
}

[[nodiscard]] std::string absolute_specified(const parsed & p, bool as_origin) {
    const bool srgb_function = p.cs == space::srgb || p.cs == space::hsl || p.cs == space::hwb;
    const bool legacy_function = p.fn == "rgb" || p.fn == "hsl" || p.fn == "hwb";
    // §15.2: a legacy-syntax colour with every channel known is `rgb()`, and
    // a top-level rgb() with a missing channel is one with that channel at
    // nought.
    //
    // AN ORIGIN IS THE EXCEPTION, on purpose. The cascade is string-typed:
    // the computed value of `hsl(from hsl(120 none 50%) h s l)` is computed
    // from this serialisation, so an origin serialised as `rgb(128, 128,
    // 128)` has lost its missing saturation, its hsl space (a hue that is
    // powerless in hsl is missing after a conversion FROM rgb, and kept
    // when there is none), and its exact channels. Chromium prints the
    // rgb() here because it keeps the parsed tree beside the text; this
    // engine keeps the modern form when the rgb() would lose something -
    // a missing channel, a powerless hue, a lightness at either end - which
    // is what the computed side (color-computed-none, -powerless,
    // -relative-color, -color-mix) measures, at the cost of the seventy-odd
    // specified serialisations that expect the rgb() form.
    if (legacy_function && srgb_function && all_settled(p, p.fn == "rgb" && !as_origin) &&
        !(as_origin && origin_loses_in_rgb(p))) {
        return legacy_text(settled_legacy(p));
    }
    std::string out = p.fn + "(";
    if (p.fn == "color") { out += std::string{space_name(p.cs)} + " "; }
    for (std::size_t slot = 0; slot < 3; ++slot) {
        if (slot != 0) { out += ' '; }
        out +=
            modern_channel_text(p.ch[slot], p.cs, static_cast<int>(slot), legacy_function, p.bytes);
    }
    if (p.alpha) {
        const channel & a = *p.alpha;
        const bool one = channel_settled(a) && clamp_literal(literal_value(a, p.cs, 3, p.bytes),
                                                             p.cs, 3, true, p.bytes) >= 1.0;
        if (!(one && (a.k != channel::kind::calc || legacy_function))) {
            out += " / " + modern_channel_text(a, p.cs, 3, legacy_function, p.bytes);
        }
    }
    return out + ")";
}

[[nodiscard]] std::string relative_specified(const parsed & p) {
    std::string out = p.fn + "(from " + serialize_specified(*p.origin, true) + " ";
    if (p.fn == "color") { out += std::string{space_name(p.cs)} + " "; }
    for (std::size_t slot = 0; slot < 3; ++slot) {
        if (slot != 0) { out += ' '; }
        out += relative_channel_text(p.ch[slot]);
    }
    if (p.alpha) { out += " / " + relative_channel_text(*p.alpha); }
    return out + ")";
}

[[nodiscard]] std::string mix_specified(const parsed & p) {
    std::string out = "color-mix(";
    if (p.mix_space != space::oklab || !p.hue_method.empty()) {
        out += "in " + std::string{space_name(p.mix_space)};
        if (!p.hue_method.empty()) { out += " " + p.hue_method + " hue"; }
        out += ", ";
    }
    // The weights: the ones the author left out are derived from the ones
    // written (CSS Values 5's normalisation, the remainder shared equally),
    // and when every weight is the even share they are all omitted - so
    // `red 50%, blue 50%` is `red, blue` and `red 50%, green, blue` is
    // `red 50%, green 25%, blue 25%`. A calc() weight keeps every item as
    // written.
    std::vector<std::optional<double>> weights;
    bool unresolved = false;
    std::size_t omitted = 0;
    double specified = 0.0;
    for (const mix_item & item : p.items) {
        if (!item.weight) {
            weights.emplace_back(std::nullopt);
            ++omitted;
        } else if (item.weight->k == channel::kind::percent) {
            weights.emplace_back(item.weight->value);
        } else {
            unresolved = true;
            weights.emplace_back(std::nullopt);
        }
        if (weights.back()) { specified += *weights.back(); }
    }
    if (!unresolved && omitted < p.items.size()) {
        for (std::optional<double> & w : weights) {
            if (!w) { w = (100.0 - std::min(100.0, specified)) / static_cast<double>(omitted); }
        }
        const double share = 100.0 / static_cast<double>(p.items.size());
        if (std::ranges::all_of(weights, [&](const auto & w) { return *w == share; })) {
            for (std::optional<double> & w : weights) { w.reset(); }
        }
    }
    for (std::size_t i = 0; i < p.items.size(); ++i) {
        if (i != 0) { out += ", "; }
        out += serialize_specified(*p.items[i].color, true);
        if (unresolved) {
            if (p.items[i].weight) { out += " " + relative_channel_text(*p.items[i].weight); }
        } else if (weights[i]) {
            out += " " + channel_text(*weights[i]) + "%";
        }
    }
    return out + ")";
}

std::string serialize_specified(const parsed & p, bool as_origin) {
    switch (p.k) {
    case parsed::kind::keyword: return p.keyword;
    case parsed::kind::hex: return legacy_text(p.fixed);
    case parsed::kind::absolute: return absolute_specified(p, as_origin);
    case parsed::kind::relative: return relative_specified(p);
    case parsed::kind::mix: return mix_specified(p);
    case parsed::kind::light_dark:
        return "light-dark(" + serialize_specified(*p.items[0].color, false) + ", " +
               serialize_specified(*p.items[1].color, false) + ")";
    case parsed::kind::alpha_fn:
        return "alpha(from " + serialize_specified(*p.origin, true) + " / " +
               relative_channel_text(*p.alpha) + ")";
    case parsed::kind::contrast:
        return "contrast-color(" + serialize_specified(*p.origin, false) + ")";
    case parsed::kind::layers: {
        std::string out = "color-layers(";
        if (!p.blend_mode.empty()) { out += p.blend_mode + ", "; }
        for (std::size_t i = 0; i < p.items.size(); ++i) {
            if (i != 0) { out += ", "; }
            out += serialize_specified(*p.items[i].color, false);
        }
        return out + ")";
    }
    }
    return {};
}

// --- resolving --------------------------------------------------------------

struct resolve_context {
    std::string_view current_color; // computed text, or empty
    const length_context * lengths = nullptr;
};

std::optional<resolved> resolve(const parsed & p, const resolve_context & ctx, int depth);

// A channel calc() against the context, the relative colour's keywords
// substituted by their numbers first. NaN is nought (CSS Values 4 §10.9's
// censoring at the top level) and an infinity is left for the clamp.
[[nodiscard]] std::optional<double> evaluate_channel(const channel & c, space cs, int slot,
                                                     bool bytes, const resolved * origin,
                                                     const resolve_context & ctx) {
    std::string text = c.raw_calc;
    if (origin != nullptr) {
        const token_stream ts = tokenize(c.raw_calc);
        const std::array<std::string_view, 3> keywords = channel_keywords(origin->cs);
        text.clear();
        for (const css_token & t : ts.tokens) {
            if (t.type == token_type::eof) { break; }
            if (t.type == token_type::ident) {
                const std::string_view word = ts.text_of(t);
                bool replaced = false;
                for (std::size_t i = 0; i < 3; ++i) {
                    if (ascii_iequals(word, keywords[i])) {
                        text += channel_text(origin->none[i] ? 0.0 : origin->c[i]);
                        replaced = true;
                    }
                }
                if (!replaced && ascii_iequals(word, "alpha")) {
                    text += channel_text(origin->alpha_none ? 0.0 : origin->alpha);
                    replaced = true;
                }
                if (replaced) { continue; }
            }
            text += ts.text_of(t);
        }
    }
    const length_context fallback;
    const math_answer answer =
        evaluate_math(text, ctx.lengths != nullptr ? *ctx.lengths : fallback);
    if (answer.outcome != math_outcome::resolved) { return std::nullopt; }
    double v = answer.value.px;
    if (answer.value.has_percent && answer.value.px == 0.0) {
        v = answer.value.percent * percent_reference(cs, slot, bytes) / 100.0;
    }
    if (std::isnan(v)) { v = 0.0; }
    return v;
}

// The value of one channel of an absolute or relative colour, and whether it
// is missing. `origin` is the relative colour's origin in the target space.
struct channel_answer {
    double value = 0.0;
    bool none = false;
};

[[nodiscard]] std::optional<channel_answer> channel_value(const channel & c, space cs, int slot,
                                                          bool bytes, const resolved * origin,
                                                          const resolve_context & ctx) {
    channel_answer out;
    switch (c.k) {
    case channel::kind::none: out.none = true; return out;
    case channel::kind::keyword: {
        if (origin == nullptr) { return std::nullopt; }
        if (c.text == "alpha") {
            out.value = origin->alpha;
            out.none = origin->alpha_none;
            return out;
        }
        const std::array<std::string_view, 3> keywords = channel_keywords(origin->cs);
        for (std::size_t i = 0; i < 3; ++i) {
            if (c.text == keywords[i]) {
                out.value = origin->c[i];
                out.none = origin->none[i];
                return out;
            }
        }
        return std::nullopt;
    }
    case channel::kind::calc: {
        const std::optional<double> v = evaluate_channel(c, cs, slot, bytes, origin, ctx);
        if (!v) { return std::nullopt; }
        out.value = *v;
        return out;
    }
    default: out.value = literal_value(c, cs, slot, bytes); return out;
    }
}

// A resolved colour's channels in the units a relative colour's keywords
// read: rgb() in 0..255, everything else as the space keeps it.
[[nodiscard]] resolved as_keywords(resolved r, bool bytes) noexcept {
    if (r.cs == space::srgb && bytes) {
        for (double & v : r.c) { v *= 255.0; }
    }
    return r;
}

[[nodiscard]] std::optional<resolved> resolve_absolute(const parsed & p,
                                                       const resolve_context & ctx, int depth) {
    resolved origin;
    const bool relative = p.k == parsed::kind::relative;
    if (relative) {
        std::optional<resolved> from = resolve(*p.origin, ctx, depth + 1);
        if (!from) { return std::nullopt; }
        origin = as_keywords(convert(*from, p.cs), p.bytes);
    }
    resolved out;
    out.cs = p.cs;
    for (std::size_t slot = 0; slot < 3; ++slot) {
        const std::optional<channel_answer> a = channel_value(
            p.ch[slot], p.cs, static_cast<int>(slot), p.bytes, relative ? &origin : nullptr, ctx);
        if (!a) { return std::nullopt; }
        out.none[slot] = a->none;
        out.c[slot] =
            a->none ? 0.0
                    : clamp_literal(a->value, p.cs, static_cast<int>(slot), !relative, p.bytes);
    }
    if (p.alpha) {
        const std::optional<channel_answer> a =
            channel_value(*p.alpha, p.cs, 3, p.bytes, relative ? &origin : nullptr, ctx);
        if (!a) { return std::nullopt; }
        out.alpha_none = a->none;
        out.alpha = a->none ? 0.0 : clamp_literal(a->value, p.cs, 3, true, p.bytes);
    } else if (relative) {
        // CSS Color 5 §4.2: an omitted alpha is the origin's, not opaque.
        out.alpha_none = origin.alpha_none;
        out.alpha = origin.alpha_none ? 0.0 : clamp_literal(origin.alpha, p.cs, 3, true, p.bytes);
    }
    if (p.bytes) {
        for (double & v : out.c) { v /= 255.0; }
    }
    // An absolute rgb()/hsl()/hwb() with every channel present is a legacy
    // colour - serialised as `rgb()` - and STAYS IN ITS OWN SPACE, so that
    // `hsl(from hsl(180 0 50%) h s l)` converts nothing and keeps its hue.
    const bool legacy_function = p.fn == "rgb" || p.fn == "hsl" || p.fn == "hwb";
    out.legacy = legacy_function && !relative && !out.any_none();
    return out;
}

// §12: the interpolation of two colours in `space`, with their weights.
[[nodiscard]] resolved interpolate(resolved a, resolved b, space cs, std::string_view hue_method,
                                   double p1, double p2, double alpha_multiplier) {
    // A missing component takes the other colour's value; missing on both
    // sides stays missing.
    for (std::size_t i = 0; i < 3; ++i) {
        if (a.none[i] && !b.none[i]) {
            a.c[i] = b.c[i];
            a.none[i] = false;
        }
        if (b.none[i] && !a.none[i]) {
            b.c[i] = a.c[i];
            b.none[i] = false;
        }
    }
    if (a.alpha_none && !b.alpha_none) {
        a.alpha = b.alpha;
        a.alpha_none = false;
    }
    if (b.alpha_none && !a.alpha_none) {
        b.alpha = a.alpha;
        b.alpha_none = false;
    }
    const int hue = hue_slot(cs);
    // Premultiplied, hue excepted.
    const auto premultiply = [&](resolved & r) {
        if (r.alpha_none) { return; }
        for (std::size_t i = 0; i < 3; ++i) {
            if (static_cast<int>(i) != hue && !r.none[i]) { r.c[i] *= r.alpha; }
        }
    };
    premultiply(a);
    premultiply(b);
    if (hue >= 0 && !a.none[static_cast<std::size_t>(hue)]) {
        double & h1 = a.c[static_cast<std::size_t>(hue)];
        double & h2 = b.c[static_cast<std::size_t>(hue)];
        h1 = normalize_hue(h1);
        h2 = normalize_hue(h2);
        const double d = h2 - h1;
        if (hue_method == "longer") {
            if (0 < d && d < 180) {
                h1 += 360;
            } else if (-180 < d && d <= 0) {
                h2 += 360;
            }
        } else if (hue_method == "increasing") {
            if (h2 < h1) { h2 += 360; }
        } else if (hue_method == "decreasing") {
            if (h1 < h2) { h1 += 360; }
        } else {
            if (d > 180) {
                h1 += 360;
            } else if (d < -180) {
                h2 += 360;
            }
        }
    }
    resolved out;
    out.cs = cs;
    for (std::size_t i = 0; i < 3; ++i) {
        out.none[i] = a.none[i] && b.none[i];
        out.c[i] = out.none[i] ? 0.0 : a.c[i] * p1 + b.c[i] * p2;
    }
    out.alpha_none = a.alpha_none && b.alpha_none;
    out.alpha = out.alpha_none ? 0.0 : a.alpha * p1 + b.alpha * p2;
    if (!out.alpha_none && out.alpha != 0.0) {
        for (std::size_t i = 0; i < 3; ++i) {
            if (static_cast<int>(i) != hue && !out.none[i]) { out.c[i] /= out.alpha; }
        }
    }
    if (hue >= 0) {
        out.c[static_cast<std::size_t>(hue)] = normalize_hue(out.c[static_cast<std::size_t>(hue)]);
    }
    if (!out.alpha_none) { out.alpha *= alpha_multiplier; }
    return out;
}

[[nodiscard]] std::optional<resolved> resolve_mix(const parsed & p, const resolve_context & ctx,
                                                  int depth) {
    std::vector<resolved> colors;
    std::vector<std::optional<double>> weights;
    for (const mix_item & item : p.items) {
        std::optional<resolved> one = resolve(*item.color, ctx, depth + 1);
        if (!one) { return std::nullopt; }
        colors.push_back(convert(*one, p.mix_space));
        if (!item.weight) {
            weights.emplace_back(std::nullopt);
            continue;
        }
        const std::optional<channel_answer> w =
            channel_value(*item.weight, space::srgb, 3, false, nullptr, ctx);
        if (!w || w->none) { return std::nullopt; }
        weights.emplace_back(std::min(1.0, std::max(0.0, w->value)));
    }
    // CSS Values 5's normalisation of mix percentages, forced: the omitted
    // weights share what the written ones left, the total is scaled to one
    // when it is not nought, and what is short of one comes off the alpha.
    double specified = 0.0;
    std::size_t omitted = 0;
    for (const std::optional<double> & w : weights) {
        if (w) {
            specified += *w;
        } else {
            ++omitted;
        }
    }
    specified = std::min(1.0, specified);
    double total = 0.0;
    for (std::optional<double> & w : weights) {
        if (!w) { w = (1.0 - specified) / static_cast<double>(omitted); }
        total += *w;
    }
    const double alpha_multiplier = total < 1.0 ? total : 1.0;
    if (total > 0.0) {
        for (std::optional<double> & w : weights) { *w /= total; }
    }
    // CSS Color 5 §3.3: the items mixed pairwise in order, each result
    // carrying the combined weight, so a polar space's "shorter" is decided
    // one step at a time.
    resolved out = colors.front();
    double weight = *weights.front();
    for (std::size_t i = 1; i < colors.size(); ++i) {
        const double combined = weight + *weights[i];
        const double progress = combined > 0.0 ? *weights[i] / combined : 0.5;
        out = interpolate(out, colors[i], p.mix_space, p.hue_method, 1.0 - progress, progress, 1.0);
        weight = combined;
    }
    out.legacy = false;
    if (!out.alpha_none) { out.alpha *= alpha_multiplier; }
    return out;
}

std::optional<resolved> resolve(const parsed & p, const resolve_context & ctx, int depth) {
    if (depth > 32) { return std::nullopt; }
    switch (p.k) {
    case parsed::kind::keyword: {
        resolved out;
        out.legacy = true;
        if (p.keyword == "transparent") {
            out.alpha = 0.0;
            return out;
        }
        if (p.keyword == "currentcolor") {
            if (ctx.current_color.empty()) { return std::nullopt; }
            const token_stream ts = tokenize(ctx.current_color);
            reader inner{ts, 0, ts.tokens.size() - 1};
            const std::unique_ptr<parsed> current = inner.read_whole();
            if (!current || current->k == parsed::kind::keyword) { return std::nullopt; }
            resolve_context without = ctx;
            without.current_color = {};
            return resolve(*current, without, depth + 1);
        }
        const named * hit = find_named(named_colors, p.keyword);
        if (hit == nullptr) { hit = find_named(system_colors, p.keyword); }
        if (hit == nullptr) { return std::nullopt; }
        out.c = {((hit->rgb >> 16) & 0xFF) / 255.0, ((hit->rgb >> 8) & 0xFF) / 255.0,
                 (hit->rgb & 0xFF) / 255.0};
        return out;
    }
    case parsed::kind::hex: return p.fixed;
    case parsed::kind::absolute:
    case parsed::kind::relative: return resolve_absolute(p, ctx, depth);
    case parsed::kind::mix: return resolve_mix(p, ctx, depth);
    case parsed::kind::light_dark: return resolve(*p.items[0].color, ctx, depth + 1);
    case parsed::kind::alpha_fn: {
        std::optional<resolved> origin = resolve(*p.origin, ctx, depth + 1);
        if (!origin) { return std::nullopt; }
        const std::optional<channel_answer> a =
            channel_value(*p.alpha, origin->cs, 3, false, &*origin, ctx);
        if (!a) { return std::nullopt; }
        origin->alpha_none = a->none;
        origin->alpha = a->none ? 0.0 : clamp_literal(a->value, origin->cs, 3, true, false);
        origin->legacy = false;
        return origin;
    }
    case parsed::kind::contrast: {
        const std::optional<resolved> origin = resolve(*p.origin, ctx, depth + 1);
        if (!origin) { return std::nullopt; }
        // WCAG's relative luminance, against the sRGB the colour maps to.
        const vec3 lin = mul(xyz_to_lin_srgb, to_xyz(origin->cs, origin->c));
        const double luminance = 0.2126 * lin[0] + 0.7152 * lin[1] + 0.0722 * lin[2];
        resolved out;
        out.legacy = true;
        const double v = (luminance + 0.05) / 0.05 > 1.05 / (luminance + 0.05) ? 0.0 : 1.0;
        out.c = {v, v, v};
        return out;
    }
    case parsed::kind::layers: return std::nullopt;
    }
    return std::nullopt;
}

// --- the computed serialisation ----------------------------------------------

[[nodiscard]] std::string modern_text(const resolved & r) {
    std::string out;
    const bool functional = r.cs == space::hsl || r.cs == space::hwb || r.cs == space::lab ||
                            r.cs == space::lch || r.cs == space::oklab || r.cs == space::oklch;
    out = functional ? std::string{space_name(r.cs)} + "("
                     : "color(" + std::string{space_name(r.cs)} + " ";
    for (std::size_t i = 0; i < 3; ++i) {
        if (i != 0) { out += ' '; }
        if (r.none[i]) {
            out += "none";
            continue;
        }
        // A hue a hair under 360 prints as 360, which re-parses as 0: print 0.
        std::string text = channel_text(r.c[i]);
        if (static_cast<int>(i) == hue_slot(r.cs) && text == "360") { text = "0"; }
        out += text;
        // A computed hsl()/hwb() writes its percentages as percentages.
        if ((r.cs == space::hsl || r.cs == space::hwb) && i != 0) { out += '%'; }
    }
    if (r.alpha_none) {
        out += " / none";
    } else if (r.alpha < 1.0) {
        out += " / " + channel_text(r.alpha);
    }
    return out + ")";
}

[[nodiscard]] std::string serialize_computed(const resolved & r) {
    if (r.legacy && !r.any_none()) { return legacy_text(convert(r, space::srgb)); }
    // A computed hsl or hwb with nothing missing is the sRGB colour it names.
    if ((r.cs == space::hsl || r.cs == space::hwb) && !r.any_none()) {
        return modern_text(convert(r, space::srgb));
    }
    return modern_text(r);
}

[[nodiscard]] std::unique_ptr<parsed> parse_text(std::string_view text) {
    const token_stream ts = tokenize(text);
    reader in{ts, 0, ts.tokens.size() - 1};
    return in.read_whole();
}

} // namespace

namespace detail {

bool match_color(const token_stream & ts, const scan & found, std::string_view normalized,
                 std::string & out) {
    if (found.significant.empty()) { return false; }
    // `device-cmyk()` is accepted as written: CSS Color 5 defines it and
    // nothing here computes it.
    const css_token & first = ts.tokens[found.significant.front()];
    if (first.type == token_type::function && ascii_iequals(ts.text_of(first), "device-cmyk(") &&
        ts.tokens[found.significant.back()].type == token_type::close_paren) {
        out = std::string{normalized};
        return true;
    }
    reader in{ts, 0, ts.tokens.size() - 1};
    const std::unique_ptr<parsed> tree = in.read_whole();
    if (!tree) { return false; }
    out = serialize_specified(*tree, false);
    return true;
}

} // namespace detail

std::string serialize_color(std::string_view text) {
    const std::unique_ptr<parsed> tree = parse_text(trim(text, html_whitespace));
    return tree ? serialize_specified(*tree, false) : std::string{};
}

std::string computed_color(std::string_view specified, const color_context & ctx) {
    const std::unique_ptr<parsed> tree = parse_text(trim(specified, html_whitespace));
    if (!tree) { return {}; }
    const resolve_context rc{ctx.current_color, ctx.lengths};
    const std::optional<resolved> r = resolve(*tree, rc, 0);
    if (!r) { return {}; }
    return serialize_computed(*r);
}

std::optional<srgb_color> resolve_color(std::string_view specified, const color_context & ctx) {
    const std::unique_ptr<parsed> tree = parse_text(trim(specified, html_whitespace));
    if (!tree) { return std::nullopt; }
    const resolve_context rc{ctx.current_color, ctx.lengths};
    const std::optional<resolved> r = resolve(*tree, rc, 0);
    if (!r) { return std::nullopt; }
    const resolved srgb = convert(*r, space::srgb);
    return srgb_color{static_cast<float>(srgb.none[0] ? 0.0 : srgb.c[0]),
                      static_cast<float>(srgb.none[1] ? 0.0 : srgb.c[1]),
                      static_cast<float>(srgb.none[2] ? 0.0 : srgb.c[2]),
                      static_cast<float>(srgb.alpha_none ? 0.0 : srgb.alpha)};
}

std::string sanitize_color(std::string_view value, bool display_p3, bool alpha) {
    // "Parsing value": a CSS <color> with no context, so `currentcolor` and
    // `inherit` are failures and opaque black. A missing component is nought.
    resolved c;
    c.legacy = false;
    if (const std::unique_ptr<parsed> tree = parse_text(trim(value, html_whitespace))) {
        if (const std::optional<resolved> r = resolve(*tree, resolve_context{}, 0)) { c = *r; }
    }
    for (std::size_t i = 0; i < 3; ++i) {
        if (c.none[i]) { c.c[i] = 0.0; }
    }
    if (c.alpha_none) { c.alpha = 0.0; }
    if (!alpha) { c.alpha = 1.0; }
    c.none = {};
    c.alpha_none = false;
    if (display_p3) { return modern_text(convert(c, space::display_p3)); }
    // Limited sRGB: eight bits per component, the alpha included.
    resolved srgb = convert(c, space::srgb);
    const auto byte = [](double v) {
        return static_cast<int>(
            std::floor(std::round(std::min(255.0, std::max(0.0, v)) * 1e6) / 1e6 + 0.5));
    };
    std::array<int, 4> bytes{byte(srgb.c[0] * 255.0), byte(srgb.c[1] * 255.0),
                             byte(srgb.c[2] * 255.0), byte(srgb.alpha * 255.0)};
    if (!alpha) {
        std::string out = "#";
        for (std::size_t i = 0; i < 3; ++i) {
            out += "0123456789abcdef"[bytes[i] >> 4];
            out += "0123456789abcdef"[bytes[i] & 15];
        }
        return out;
    }
    for (std::size_t i = 0; i < 3; ++i) { srgb.c[i] = bytes[i] / 255.0; }
    srgb.alpha = bytes[3] / 255.0;
    return modern_text(srgb);
}

} // namespace ctbrowser::style::css
