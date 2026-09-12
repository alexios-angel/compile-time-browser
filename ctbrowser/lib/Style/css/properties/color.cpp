// `<color>`, CSS Color 4 - the SYNTAX only, so `el.style.color = "undefined"`
// is refused and `color: rgb(...)` is kept exactly as the author wrote it. What
// a colour IS is paint's business (paint/values.hpp); this file only knows
// which spellings are colours.

#include "internal.hpp"

namespace ctbrowser::style::css {

using namespace detail;

namespace {

// CSS Color 4 §6.1's named colours, plus `transparent` and `currentcolor`.
constexpr std::string_view named_colors =
    "aliceblue antiquewhite aqua aquamarine azure beige bisque black blanchedalmond blue "
    "blueviolet brown burlywood cadetblue chartreuse chocolate coral cornflowerblue cornsilk "
    "crimson cyan darkblue darkcyan darkgoldenrod darkgray darkgreen darkgrey darkkhaki "
    "darkmagenta darkolivegreen darkorange darkorchid darkred darksalmon darkseagreen "
    "darkslateblue darkslategray darkslategrey darkturquoise darkviolet deeppink deepskyblue "
    "dimgray dimgrey dodgerblue firebrick floralwhite forestgreen fuchsia gainsboro ghostwhite "
    "gold goldenrod gray green greenyellow grey honeydew hotpink indianred indigo ivory khaki "
    "lavender lavenderblush lawngreen lemonchiffon lightblue lightcoral lightcyan "
    "lightgoldenrodyellow lightgray lightgreen lightgrey lightpink lightsalmon lightseagreen "
    "lightskyblue lightslategray lightslategrey lightsteelblue lightyellow lime limegreen linen "
    "magenta maroon mediumaquamarine mediumblue mediumorchid mediumpurple mediumseagreen "
    "mediumslateblue mediumspringgreen mediumturquoise mediumvioletred midnightblue mintcream "
    "mistyrose moccasin navajowhite navy oldlace olive olivedrab orange orangered orchid "
    "palegoldenrod palegreen paleturquoise palevioletred papayawhip peachpuff peru pink plum "
    "powderblue purple rebeccapurple red rosybrown royalblue saddlebrown salmon sandybrown "
    "seagreen seashell sienna silver skyblue slateblue slategray slategrey snow springgreen "
    "steelblue tan teal thistle tomato turquoise violet wheat white whitesmoke yellow "
    "yellowgreen transparent currentcolor";

// §6.2's system colours, and the deprecated ones §6.3 keeps parsing.
constexpr std::string_view system_colors =
    "accentcolor accentcolortext activetext buttonborder buttonface buttontext canvas "
    "canvastext field fieldtext graytext highlight highlighttext linktext mark marktext "
    "selecteditem selecteditemtext visitedtext activeborder activecaption appworkspace "
    "background buttonhighlight buttonshadow captiontext inactiveborder inactivecaption "
    "inactivecaptiontext infobackground infotext menu menutext scrollbar threeddarkshadow "
    "threedface threedhighlight threedlightshadow threedshadow window windowframe windowtext";

constexpr std::array<std::string_view, 14> color_functions{"rgb",
                                                           "rgba",
                                                           "hsl",
                                                           "hsla",
                                                           "hwb",
                                                           "lab",
                                                           "lch",
                                                           "oklab",
                                                           "oklch",
                                                           "color",
                                                           "color-mix",
                                                           "light-dark",
                                                           "contrast-color",
                                                           "device-cmyk"};

} // namespace

namespace detail {

// ONE COLOUR: a keyword, a `#` with 3, 4, 6 or 8 hex digits, or a colour
// function - whose arguments are not checked, on the same reasoning every
// freeform value here follows: a grammar that is 80% right refuses 20% of the
// valid values. `out` is the keyword lowercased; anything else keeps its text.
bool match_color(const token_stream & ts, const scan & found, std::string_view normalized,
                 std::string & out) {
    if (found.significant.empty()) { return false; }
    const css_token & t = ts.tokens[found.significant.front()];
    const std::string_view body = ts.text_of(t);
    if (t.type == token_type::ident && found.significant.size() == 1) {
        const std::string word = ascii_lower_copy(body);
        if (!has_keyword(named_colors, word) && !has_keyword(system_colors, word) &&
            !word.starts_with("-webkit-")) {
            return false;
        }
        out = word;
        return true;
    }
    if (t.type == token_type::hash && found.significant.size() == 1) {
        const std::string_view digits = body.substr(1);
        if (digits.size() != 3 && digits.size() != 4 && digits.size() != 6 && digits.size() != 8) {
            return false;
        }
        for (const char c : digits) {
            if (hex_value(c) < 0) { return false; }
        }
        out = std::string{normalized};
        return true;
    }
    if (t.type == token_type::function && !body.empty() &&
        in_list(color_functions, body.substr(0, body.size() - 1)) &&
        ts.tokens[found.significant.back()].type == token_type::close_paren) {
        // THE ARITY OF THE THREE-CHANNEL FUNCTIONS, and only that: `rgb(0)` is
        // no colour (attr-all-types), `rgb(from red r g b)` is one and its
        // channels are the colour grammar's to judge. Three or four top-level
        // components, whether comma- or space-separated, a `/` counted as the
        // comma before the alpha.
        const std::string_view fn = body.substr(0, body.size() - 1);
        if (ascii_iequals(fn, "rgb") || ascii_iequals(fn, "rgba") || ascii_iequals(fn, "hsl") ||
            ascii_iequals(fn, "hsla") || ascii_iequals(fn, "hwb")) {
            std::size_t components = 0;
            int depth = 0;
            bool relative = false;
            for (std::size_t k = 1; k + 1 < found.significant.size(); ++k) {
                const css_token & a = ts.tokens[found.significant[k]];
                if (depth == 0) {
                    if (components == 0 && a.type == token_type::ident &&
                        ascii_iequals(ts.text_of(a), "from")) {
                        relative = true;
                        break;
                    }
                    const bool separator = a.type == token_type::comma ||
                                           (a.type == token_type::delim && ts.text_of(a) == "/");
                    if (separator) { continue; }
                    ++components; // a token at the top is a component; a function is one
                }
                if (a.type == token_type::function || a.type == token_type::open_paren) {
                    ++depth;
                } else if (a.type == token_type::close_paren) {
                    --depth;
                }
            }
            if (!relative && (components < 3 || components > 4)) { return false; }
        }
        out = std::string{normalized};
        return true;
    }
    return false;
}

} // namespace detail

} // namespace ctbrowser::style::css
