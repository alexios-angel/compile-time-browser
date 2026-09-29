// <basic-shape>, CSS Shapes 1 §3 - for clip-path and shape-outside: parsed,
// written in canonical form, computed, and blended for an animation:
//
//   circle( <radial-size>? [ at <position> ]? )
//   ellipse( [ <radial-size>{2} ]? [ at <position> ]? )
//   inset( <length-percentage>{1,4} [ round <'border-radius'> ]? )
//   xywh( <length-percentage>{2} <length-percentage [0,∞]>{2} [ round <'border-radius'> ]? )
//   rect( [ <length-percentage> | auto ]{4} [ round <'border-radius'> ]? )
//   polygon( [ <fill-rule> || round <length> ]? , [ <length-percentage>{2} ]# )
//   path( <fill-rule>? , <string> )
//
// beside the box it is drawn in: clip-path's <geometry-box> (`border-box`
// by default) or shape-outside's <shape-box> (`margin-box`) - and CSS Shapes
// 2's shape(), whose grammar is set out above parse_shape_command. Nothing
// here draws.
//
// The COMPUTED shape has its lengths absolute, its position in percentages
// and calc()s from the top left, and xywh() and rect() as the inset() they
// mean (CSS Shapes 1 §3.2). Two computed shapes of one function blend
// component by component - the interpolation of §3.3 and the addition of
// Web Animations - and anything else is discrete.

#include "internal.hpp"

#include <cctype>
#include <numbers>
#include <utility>

namespace ctbrowser::style::css {

using namespace detail;

namespace {

// One command of a shape(), CSS Shapes 2 §3.2.
struct shape_command {
    std::string verb; // move line hline vline curve smooth arc close, lowercased
    bool by = false;  // `by` - relative to the command's start - rather than `to`
    std::string end;  // `to`'s <position>, `by`'s pair, or an hline's or vline's one value
    // curve's and smooth's control points, each with its anchor - `start`,
    // `end` or `origin` - empty for the command's default: `origin` after
    // `to`, `start` after `by`.
    std::vector<std::pair<std::string, std::string>> controls;
    std::vector<std::string> radii; // arc's one or two
    bool cw = false;                // arc's sweep; `ccw` is the default
    bool large = false;             // arc's size; `small` is the default
    std::string rotate;             // arc's angle, empty for none
};

struct shape_parts {
    std::string function;                // lowercased name; empty when not a shape
    std::vector<std::string> args;       // radial sizes, inset sides, xywh / rect components
    std::string position;                // after `at`; empty when omitted
    std::vector<std::string> radii;      // `round`'s eight corners, or empty
    std::string fill_rule;               // `evenodd`, or empty for the default
    std::string round;                   // polygon's `round <length>`, or empty
    std::vector<std::string> points;     // polygon: x, y, x, y ...
    std::string path;                    // path's string, serialised
    std::string box;                     // the box beside it, or empty for the default
    std::string size;                    // ray()'s size, or empty for `closest-side`
    bool contain = false;                // ray()'s `contain`
    std::vector<shape_command> commands; // shape(), whose `from` is `position`
};

// --- SVG path data (SVG 2 §9.3.9), for path() -------------------------------

// One command and its numbers; an implicit repetition is its own segment,
// written with the letter it stands for (`M 1 2 3 4` is `M 1 2 L 3 4`).
struct path_segment {
    char command = 'M';
    std::vector<double> numbers;
};

[[nodiscard]] std::size_t path_arity(char command) {
    switch (command | 0x20) {
    case 'm':
    case 'l':
    case 't': return 2;
    case 'h':
    case 'v': return 1;
    case 'c': return 6;
    case 's':
    case 'q': return 4;
    case 'a': return 7;
    case 'z': return 0;
    default: return static_cast<std::size_t>(-1);
    }
}

// The whole data, or nullopt for anything SVG's grammar refuses - an empty
// path, a first command that is not a moveto, a command short of numbers.
[[nodiscard]] std::optional<std::vector<path_segment>> parse_path_data(std::string_view data) {
    std::vector<path_segment> out;
    std::size_t i = 0;
    const auto skip = [&](bool comma) {
        while (i < data.size() &&
               (data[i] == ' ' || data[i] == '\t' || data[i] == '\n' || data[i] == '\r' ||
                data[i] == '\f' || (comma && data[i] == ','))) {
            ++i;
        }
    };
    const auto number = [&](bool flag) -> std::optional<double> {
        skip(true);
        if (i >= data.size()) { return std::nullopt; }
        if (flag) {
            // An arc flag is one digit, and may be run into what follows.
            if (data[i] != '0' && data[i] != '1') { return std::nullopt; }
            return data[i++] == '1' ? 1.0 : 0.0;
        }
        const std::size_t start = i;
        if (data[i] == '+' || data[i] == '-') { ++i; }
        bool digits = false, point = false;
        while (i < data.size() &&
               ((data[i] >= '0' && data[i] <= '9') || (data[i] == '.' && !point))) {
            point = point || data[i] == '.';
            digits = digits || data[i] != '.';
            ++i;
        }
        if (!digits) { return std::nullopt; }
        if (i < data.size() && (data[i] == 'e' || data[i] == 'E')) {
            std::size_t e = i + 1;
            if (e < data.size() && (data[e] == '+' || data[e] == '-')) { ++e; }
            if (e < data.size() && data[e] >= '0' && data[e] <= '9') {
                i = e;
                while (i < data.size() && data[i] >= '0' && data[i] <= '9') { ++i; }
            }
        }
        double value = 0;
        const auto [end, ec] = std::from_chars(data.data() + start, data.data() + i, value);
        if (ec != std::errc{} || end != data.data() + i) { return std::nullopt; }
        return value;
    };
    char command = 0;
    for (;;) {
        skip(false);
        if (i >= data.size()) { break; }
        const char c = data[i];
        if (path_arity(c) != static_cast<std::size_t>(-1)) {
            if (out.empty() && (c | 0x20) != 'm') { return std::nullopt; }
            command = c;
            ++i;
        } else if (command == 0 || (command | 0x20) == 'z') {
            return std::nullopt;
        } else if ((command | 0x20) == 'm') {
            command = command == 'M' ? 'L' : 'l';
        }
        path_segment segment{command, {}};
        const std::size_t arity = path_arity(command);
        for (std::size_t k = 0; k < arity; ++k) {
            const bool flag = (command | 0x20) == 'a' && (k == 3 || k == 4);
            const std::optional<double> n = number(flag);
            if (!n) { return std::nullopt; }
            segment.numbers.push_back(*n);
        }
        if ((command | 0x20) == 'z') { segment.command = 'Z'; }
        out.push_back(std::move(segment));
        skip(true);
    }
    if (out.empty()) { return std::nullopt; }
    return out;
}

// Every command as its absolute twin: a relative one's points are offsets
// from the current point, and a closepath returns the current point to the
// start of its subpath.
void absolutize(std::vector<path_segment> & data) {
    double x = 0, y = 0, start_x = 0, start_y = 0;
    for (path_segment & s : data) {
        const char verb = static_cast<char>(s.command | 0x20);
        std::vector<double> & n = s.numbers;
        if (s.command == verb && verb != 'z') {
            switch (verb) {
            case 'h': n[0] += x; break;
            case 'v': n[0] += y; break;
            case 'a':
                n[5] += x;
                n[6] += y;
                break;
            default:
                for (std::size_t k = 0; k + 1 < n.size(); k += 2) {
                    n[k] += x;
                    n[k + 1] += y;
                }
                break;
            }
            s.command = static_cast<char>(verb - 0x20);
        }
        switch (verb) {
        case 'z':
            x = start_x;
            y = start_y;
            break;
        case 'h': x = n[0]; break;
        case 'v': y = n[0]; break;
        default:
            x = n[n.size() - 2];
            y = n[n.size() - 1];
            break;
        }
        if (verb == 'm') {
            start_x = x;
            start_y = y;
        }
    }
}

[[nodiscard]] std::string path_text(std::span<const path_segment> data) {
    std::string out;
    for (const path_segment & s : data) {
        if (!out.empty()) { out += ' '; }
        out += s.command;
        for (const double n : s.numbers) { out += " " + serialize_number(n); }
    }
    return out;
}

constexpr std::string_view geometry_boxes =
    "border-box padding-box content-box margin-box fill-box stroke-box view-box";

[[nodiscard]] std::string joined(std::span<const std::string> parts, std::string_view by = " ") {
    std::string out;
    for (const std::string & part : parts) {
        if (!out.empty()) { out += by; }
        out += part;
    }
    return out;
}

[[nodiscard]] std::optional<std::string> length_percentage(std::string_view part,
                                                           bool nonnegative) {
    static const property_syntax signed_lp{"", value_kind::length_percentage, "", "", false, false};
    static const property_syntax unsigned_lp{"",  value_kind::length_percentage, "", "", false,
                                             true};
    std::string out;
    if (!typed_component(part, nonnegative ? unsigned_lp : signed_lp, out)) { return std::nullopt; }
    return out;
}

// `margin`'s four sides, from one to four values.
[[nodiscard]] std::vector<std::string> four(std::span<const std::string> v) {
    if (v.empty() || v.size() > 4) { return {}; }
    return {v[0], v.size() > 1 ? v[1] : v[0], v.size() > 2 ? v[2] : v[0],
            v.size() > 3 ? v[3] : (v.size() > 1 ? v[1] : v[0])};
}

[[nodiscard]] std::string fold_four(std::span<const std::string> v) {
    std::vector<std::string> parts(v.begin(), v.end());
    if (parts.size() == 4 && parts[3] == parts[1]) { parts.pop_back(); }
    if (parts.size() == 3 && parts[2] == parts[0]) { parts.pop_back(); }
    if (parts.size() == 2 && parts[1] == parts[0]) { parts.pop_back(); }
    return joined(parts);
}

// `round <'border-radius'>`: eight corners, the vertical four the
// horizontal when there is no slash.
[[nodiscard]] bool parse_radii(std::span<const std::string_view> words,
                               std::vector<std::string> & out) {
    std::vector<std::string> horizontal, vertical;
    bool slash = false;
    for (const std::string_view word : words) {
        if (word == "/") {
            if (slash) { return false; }
            slash = true;
            continue;
        }
        std::optional<std::string> one = length_percentage(word, true);
        if (!one) { return false; }
        (slash ? vertical : horizontal).push_back(std::move(*one));
    }
    if (slash && vertical.empty()) { return false; }
    std::vector<std::string> h = four(horizontal);
    std::vector<std::string> v = slash ? four(vertical) : h;
    if (h.empty() || v.empty()) { return false; }
    out = h;
    out.insert(out.end(), v.begin(), v.end());
    return true;
}

[[nodiscard]] std::string radii_text(std::span<const std::string> r) {
    if (r.size() != 8) { return {}; }
    const std::string h = fold_four(r.first(4));
    const std::string v = fold_four(r.subspan(4));
    return h == v ? h : h + " / " + v;
}

[[nodiscard]] bool zero(std::string_view text) {
    return text == "0px" || text == "0%" || text == "0";
}

[[nodiscard]] std::optional<std::string> canonical_position(std::string_view text) {
    // A MATH FUNCTION in a position - `from calc(10% - 5px) 2px`, which is how
    // an interpolated one computes. match_position reads single tokens, so one
    // or two <length-percentage>s with one among them are written here.
    if (text.find('(') != std::string_view::npos) {
        const std::vector<std::string_view> words = words_of(text);
        if (words.empty() || words.size() > 2) { return std::nullopt; }
        std::string out;
        for (const std::string_view word : words) {
            const std::optional<std::string> one = length_percentage(word, false);
            if (!one) { return std::nullopt; }
            out += (out.empty() ? "" : " ") + *one;
        }
        return words.size() == 1 ? out + " center" : out;
    }
    const token_stream ts = tokenize(text);
    const scan found = scan_tokens(ts);
    std::string out;
    if (found.malformed || found.significant.empty() || !match_position(ts, found, out)) {
        return std::nullopt;
    }
    return out;
}

// The words of a circle() or an ellipse(): the radii, then `at` and a position.
[[nodiscard]] bool parse_radial(std::span<const std::string_view> words, std::size_t most,
                                shape_parts & out) {
    std::size_t at = 0;
    for (; at < words.size() && !ascii_iequals(words[at], "at"); ++at) {
        if (at == most) { return false; }
        if (ascii_iequals_any(words[at], {"closest-side", "farthest-side"})) {
            out.args.push_back(ascii_lower_copy(words[at]));
        } else if (std::optional<std::string> r = length_percentage(words[at], true)) {
            out.args.push_back(std::move(*r));
        } else {
            return false;
        }
    }
    if (!out.args.empty() && out.args.size() != most) { return false; }
    if (at < words.size()) {
        if (at + 1 == words.size()) { return false; }
        std::string rest;
        for (std::size_t i = at + 1; i < words.size(); ++i) {
            rest += (rest.empty() ? "" : " ") + std::string{words[i]};
        }
        std::optional<std::string> position = canonical_position(rest);
        if (!position) { return false; }
        out.position = std::move(*position);
    }
    // The default radii are left out (CSS Shapes 1 §3.1's serialisation).
    bool all_closest = true;
    for (const std::string & r : out.args) { all_closest = all_closest && r == "closest-side"; }
    if (all_closest) { out.args.clear(); }
    return true;
}

// Everything after `round`, into the radii; empty `words` is no `round`.
[[nodiscard]] bool parse_round(std::span<const std::string_view> words, shape_parts & out) {
    if (words.empty()) { return true; }
    if (!parse_radii(words, out.radii)) { return false; }
    bool all_zero = true;
    for (const std::string & r : out.radii) { all_zero = all_zero && zero(r); }
    if (all_zero) { out.radii.clear(); }
    return true;
}

// --- shape(), CSS Shapes 2 §3.2 ----------------------------------------------
//
//   shape( <fill-rule>? from <position> , <shape-command># )
//   <shape-command> = move <end> | line <end> | close
//     | hline [ to [ <lp> | left | center | right | x-start | x-end ] | by <lp> ]
//     | vline [ to [ <lp> | top | center | bottom | y-start | y-end ] | by <lp> ]
//     | curve [ to <position> with <control> [ / <control> ]?
//             | by <pair> with <relative-control> [ / <relative-control> ]? ]
//     | smooth [ to <position> [ with <control> ]? | by <pair> [ with <relative-control> ]? ]
//     | arc <end> [ of <lp [0,∞]>{1,2} && [ cw | ccw ]? && [ large | small ]?
//                 && [ rotate <angle> ]? ]
//   <end> = to <position> | by <pair>        <pair> = <lp>{2}
//   <control> = <position> | <relative-control>
//   <relative-control> = <pair> [ from [ start | end | origin ] ]?

[[nodiscard]] std::string joined_words(std::span<const std::string_view> words) {
    std::string out;
    for (const std::string_view w : words) { out += (out.empty() ? "" : " ") + std::string{w}; }
    return out;
}

// Exactly two <length-percentage>s.
[[nodiscard]] bool coordinate_pair(std::span<const std::string_view> words, std::string & out) {
    if (words.size() != 2) { return false; }
    const std::optional<std::string> x = length_percentage(words[0], false);
    const std::optional<std::string> y = length_percentage(words[1], false);
    if (!x || !y) { return false; }
    out = *x + " " + *y;
    return true;
}

// `to <position>` or `by <pair>`, the words after the `to` or `by`.
[[nodiscard]] bool end_point(std::span<const std::string_view> words, bool by, std::string & out) {
    if (words.empty()) { return false; }
    if (by) { return coordinate_pair(words, out); }
    std::optional<std::string> position = canonical_position(joined_words(words));
    if (!position) { return false; }
    out = std::move(*position);
    return true;
}

// One control point. A <position> is allowed after `to` only, and anchors at
// the origin; the anchor a command defaults to is left unwritten, which is
// how `with 1px 2px from origin` after `to` serialises as `with 1px 2px`.
[[nodiscard]] bool control_point(std::span<const std::string_view> words, bool by,
                                 std::pair<std::string, std::string> & out) {
    std::string anchor;
    if (words.size() >= 2 && ascii_iequals(words[words.size() - 2], "from")) {
        anchor = ascii_lower_copy(words.back());
        if (!has_keyword("start end origin", anchor)) { return false; }
        if (!coordinate_pair(words.first(words.size() - 2), out.first)) { return false; }
    } else if (!coordinate_pair(words, out.first)) {
        if (by || words.empty()) { return false; }
        std::optional<std::string> position = canonical_position(joined_words(words));
        if (!position) { return false; }
        out.first = std::move(*position);
    }
    if (anchor == (by ? "start" : "origin")) { anchor.clear(); }
    out.second = std::move(anchor);
    return true;
}

// An angle in degrees: the computed form of arc's `rotate`.
[[nodiscard]] std::optional<double> degrees_of(std::string_view text) {
    std::size_t unit = 0;
    while (unit < text.size() &&
           (std::isdigit(static_cast<unsigned char>(text[unit])) || text[unit] == '.' ||
            text[unit] == '-' || text[unit] == '+' || text[unit] == 'e' || text[unit] == 'E')) {
        ++unit;
    }
    // `1e3deg`'s `e` is the exponent; `1deg`'s is not reached. A trailing `e`
    // before the unit belongs to the unit, which only `deg` would begin with.
    if (unit > 0 && (text[unit - 1] == 'e' || text[unit - 1] == 'E')) { --unit; }
    double value = 0;
    const auto [end, ec] = std::from_chars(text.data(), text.data() + unit, value);
    if (ec != std::errc{} || end != text.data() + unit) { return std::nullopt; }
    const std::string_view name = text.substr(unit);
    if (ascii_iequals(name, "deg")) { return value; }
    if (ascii_iequals(name, "rad")) { return value * 180 / std::numbers::pi; }
    if (ascii_iequals(name, "grad")) { return value * 0.9; }
    if (ascii_iequals(name, "turn")) { return value * 360; }
    return std::nullopt;
}

[[nodiscard]] std::optional<shape_command> parse_shape_command(std::string_view item) {
    // A `/` is a word of its own however it is spaced: `53px /34% 61px`.
    std::string spaced;
    int depth = 0;
    for (const char ch : item) {
        depth += ch == '(' ? 1 : ch == ')' ? -1 : 0;
        spaced += depth == 0 && ch == '/' ? std::string{" / "} : std::string{ch};
    }
    const std::vector<std::string_view> words = words_of(spaced);
    if (words.empty()) { return std::nullopt; }
    shape_command c;
    c.verb = ascii_lower_copy(words[0]);
    if (c.verb == "close") {
        if (words.size() != 1) { return std::nullopt; }
        return c;
    }
    if (words.size() < 3) { return std::nullopt; }
    if (ascii_iequals(words[1], "by")) {
        c.by = true;
    } else if (!ascii_iequals(words[1], "to")) {
        return std::nullopt;
    }
    const std::span<const std::string_view> rest = std::span{words}.subspan(2);
    if (c.verb == "move" || c.verb == "line") {
        if (!end_point(rest, c.by, c.end)) { return std::nullopt; }
        return c;
    }
    if (c.verb == "hline" || c.verb == "vline") {
        if (rest.size() != 1) { return std::nullopt; }
        const std::string_view sides = c.verb == "hline" ? "left center right x-start x-end"
                                                         : "top center bottom y-start y-end";
        if (!c.by && has_keyword(sides, rest[0])) {
            c.end = ascii_lower_copy(rest[0]);
            return c;
        }
        std::optional<std::string> one = length_percentage(rest[0], false);
        if (!one) { return std::nullopt; }
        c.end = std::move(*one);
        return c;
    }
    if (c.verb == "curve" || c.verb == "smooth") {
        std::size_t with = 0;
        while (with < rest.size() && !ascii_iequals(rest[with], "with")) { ++with; }
        if (!end_point(rest.first(with), c.by, c.end)) { return std::nullopt; }
        if (with == rest.size()) {
            if (c.verb == "curve") { return std::nullopt; }
            return c;
        }
        std::size_t from = with + 1;
        for (std::size_t i = from; i <= rest.size(); ++i) {
            if (i < rest.size() && rest[i] != "/") { continue; }
            std::pair<std::string, std::string> point;
            if (!control_point(rest.subspan(from, i - from), c.by, point)) { return std::nullopt; }
            c.controls.push_back(std::move(point));
            from = i + 1;
        }
        if (c.controls.size() > (c.verb == "curve" ? 2u : 1u)) { return std::nullopt; }
        return c;
    }
    if (c.verb == "arc") {
        const auto keyword = [](std::string_view w) {
            return has_keyword("of cw ccw large small rotate", w);
        };
        std::size_t at = 0;
        while (at < rest.size() && !keyword(rest[at])) { ++at; }
        if (!end_point(rest.first(at), c.by, c.end)) { return std::nullopt; }
        bool of = false, sweep = false, size = false, rotate = false;
        while (at < rest.size()) {
            const std::string word = ascii_lower_copy(rest[at++]);
            if (word == "of" && !of) {
                of = true;
                while (at < rest.size() && c.radii.size() < 2 && !keyword(rest[at])) {
                    std::optional<std::string> radius = length_percentage(rest[at++], true);
                    if (!radius) { return std::nullopt; }
                    c.radii.push_back(std::move(*radius));
                }
                if (c.radii.empty()) { return std::nullopt; }
            } else if ((word == "cw" || word == "ccw") && !sweep) {
                sweep = true;
                c.cw = word == "cw";
            } else if ((word == "large" || word == "small") && !size) {
                size = true;
                c.large = word == "large";
            } else if (word == "rotate" && !rotate && at < rest.size()) {
                rotate = true;
                static const property_syntax angle{"", value_kind::angle, "", "", false, false};
                if (!typed_component(rest[at++], angle, c.rotate)) { return std::nullopt; }
                if (const std::optional<double> d = degrees_of(c.rotate); d && *d == 0) {
                    c.rotate.clear();
                }
            } else {
                return std::nullopt;
            }
        }
        if (!of) { return std::nullopt; }
        return c;
    }
    return std::nullopt;
}

// `<fill-rule>? from <position>`, then one command per comma - at least one.
[[nodiscard]] bool parse_shape_body(std::string_view body, shape_parts & out) {
    const std::vector<std::string_view> items = split_top_level(body, ",");
    if (items.size() < 2) { return false; }
    // split_top_level drops an empty item; a stray comma leaves one too few.
    std::size_t commas = 0;
    int depth = 0;
    for (const char c : body) {
        depth += c == '(' ? 1 : c == ')' ? -1 : 0;
        commas += depth == 0 && c == ',' ? 1U : 0U;
    }
    if (commas + 1 != items.size()) { return false; }
    const std::vector<std::string_view> head = words_of(items.front());
    std::size_t k = 0;
    if (!head.empty() && ascii_iequals_any(head[0], {"nonzero", "evenodd"})) {
        out.fill_rule = ascii_lower_copy(head[0]);
        k = 1;
    }
    if (k + 1 >= head.size() || !ascii_iequals(head[k], "from")) { return false; }
    std::optional<std::string> from =
        canonical_position(joined_words(std::span{head}.subspan(k + 1)));
    if (!from) { return false; }
    out.position = std::move(*from);
    if (out.fill_rule == "nonzero") { out.fill_rule.clear(); }
    for (std::size_t i = 1; i < items.size(); ++i) {
        std::optional<shape_command> c = parse_shape_command(trim(items[i], html_whitespace));
        if (!c) { return false; }
        out.commands.push_back(std::move(*c));
    }
    return true;
}

[[nodiscard]] std::string command_text(const shape_command & c) {
    if (c.verb == "close") { return "close"; }
    std::string out = c.verb + (c.by ? " by " : " to ") + c.end;
    for (std::size_t i = 0; i < c.controls.size(); ++i) {
        out += (i == 0 ? " with " : " / ") + c.controls[i].first;
        if (!c.controls[i].second.empty()) { out += " from " + c.controls[i].second; }
    }
    if (c.verb == "arc") {
        out += " of " + joined(c.radii);
        if (c.cw) { out += " cw"; }
        if (c.large) { out += " large"; }
        if (!c.rotate.empty()) { out += " rotate " + c.rotate; }
    }
    return out;
}

[[nodiscard]] std::optional<shape_parts> parse_function(std::string_view text) {
    shape_parts out;
    const std::size_t open = text.find('(');
    if (open == std::string_view::npos || !text.ends_with(')')) { return std::nullopt; }
    out.function = ascii_lower_copy(text.substr(0, open));
    const std::string_view body = text.substr(open + 1, text.size() - open - 2);
    const std::vector<std::string_view> words = words_of(body);
    const auto round_at = [&words]() {
        std::size_t i = 0;
        while (i < words.size() && !ascii_iequals(words[i], "round")) { ++i; }
        return i;
    };
    if (out.function == "circle" || out.function == "ellipse") {
        if (!parse_radial(words, out.function == "circle" ? 1 : 2, out)) { return std::nullopt; }
        return out;
    }
    if (out.function == "inset" || out.function == "xywh" || out.function == "rect") {
        const std::size_t r = round_at();
        if (r == words.size() - 1 && !words.empty()) { return std::nullopt; }
        const std::size_t count = out.function == "inset" ? r : 4;
        if (r == 0 || r > 4 || (out.function != "inset" && r != 4)) { return std::nullopt; }
        for (std::size_t i = 0; i < count; ++i) {
            if (out.function == "rect" && ascii_iequals(words[i], "auto")) {
                out.args.emplace_back("auto");
                continue;
            }
            std::optional<std::string> one =
                length_percentage(words[i], out.function == "xywh" && i >= 2);
            if (!one) { return std::nullopt; }
            out.args.push_back(std::move(*one));
        }
        if (!parse_round(std::span{words}.subspan(r < words.size() ? r + 1 : r), out)) {
            return std::nullopt;
        }
        return out;
    }
    if (out.function == "ray") {
        // `ray( <angle> && <ray-size>? && contain? && [ at <position> ]? )`,
        // Motion Path 1 §3.1: any order, written angle, size, contain,
        // position; `closest-side` is the default size.
        static const property_syntax angle{"", value_kind::angle, "", "", false, false};
        static constexpr std::string_view sizes =
            "closest-side closest-corner farthest-side farthest-corner sides";
        for (std::size_t i = 0; i < words.size(); ++i) {
            const std::string_view w = words[i];
            std::string one;
            if (ascii_iequals(w, "at") && out.position.empty()) {
                // The position is every word up to the next that is not one.
                std::size_t end = i + 1;
                std::string rest;
                while (end < words.size() && !ascii_iequals(words[end], "contain") &&
                       !has_keyword(sizes, words[end]) &&
                       !(out.args.empty() && typed_component(words[end], angle, one))) {
                    rest += (rest.empty() ? "" : " ") + std::string{words[end]};
                    ++end;
                }
                std::optional<std::string> position = canonical_position(rest);
                if (!position) { return std::nullopt; }
                out.position = std::move(*position);
                i = end - 1;
            } else if (ascii_iequals(w, "contain") && !out.contain) {
                out.contain = true;
            } else if (has_keyword(sizes, w) && out.size.empty()) {
                out.size = ascii_lower_copy(w);
            } else if (out.args.empty() && typed_component(w, angle, one)) {
                out.args.push_back(std::move(one));
            } else {
                return std::nullopt;
            }
        }
        if (out.args.empty()) { return std::nullopt; }
        if (out.size == "closest-side") { out.size.clear(); }
        return out;
    }
    if (out.function == "shape") {
        if (!parse_shape_body(body, out)) { return std::nullopt; }
        return out;
    }
    if (out.function == "polygon" || out.function == "path") {
        std::vector<std::string_view> items = split_top_level(body, ",");
        for (std::string_view & item : items) { item = trim(item, html_whitespace); }
        if (items.empty()) { return std::nullopt; }
        // The leading item may be a fill rule and, in a polygon, a `round`.
        const std::vector<std::string_view> first = words_of(items.front());
        std::size_t k = 0;
        bool leading = false;
        while (k < first.size()) {
            if (ascii_iequals_any(first[k], {"nonzero", "evenodd"}) && out.fill_rule.empty()) {
                out.fill_rule = ascii_lower_copy(first[k]);
                leading = true;
                ++k;
            } else if (out.function == "polygon" && ascii_iequals(first[k], "round") &&
                       out.round.empty() && k + 1 < first.size()) {
                static const property_syntax nonnegative{"",  value_kind::length, "", "", false,
                                                         true};
                if (!typed_component(first[k + 1], nonnegative, out.round)) { return std::nullopt; }
                leading = true;
                k += 2;
            } else {
                break;
            }
        }
        if (leading) {
            if (k != first.size()) { return std::nullopt; }
            items.erase(items.begin());
        }
        if (out.fill_rule == "nonzero") { out.fill_rule.clear(); }
        if (zero(out.round)) { out.round.clear(); }
        if (out.function == "path") {
            if (items.size() != 1) { return std::nullopt; }
            const token_stream ts = tokenize(items.front());
            const scan found = scan_tokens(ts);
            if (found.significant.size() != 1 ||
                ts.tokens[found.significant.front()].type != token_type::string) {
                return std::nullopt;
            }
            std::optional<std::vector<path_segment>> data =
                parse_path_data(ts.value_of(ts.tokens[found.significant.front()]));
            if (!data) { return std::nullopt; }
            out.path = path_text(*data);
            return out;
        }
        if (items.empty()) { return std::nullopt; }
        for (const std::string_view item : items) {
            const std::vector<std::string_view> xy = words_of(item);
            if (xy.size() != 2) { return std::nullopt; }
            for (const std::string_view c : xy) {
                std::optional<std::string> one = length_percentage(c, false);
                if (!one) { return std::nullopt; }
                out.points.push_back(std::move(*one));
            }
        }
        return out;
    }
    return std::nullopt;
}

[[nodiscard]] std::string serialize(const shape_parts & s) {
    std::string out = s.function + "(";
    const auto round = [&s]() -> std::string {
        return s.radii.empty() ? std::string{} : " round " + radii_text(s.radii);
    };
    if (s.function == "circle" || s.function == "ellipse") {
        out += joined(s.args);
        if (!s.position.empty()) { out += (s.args.empty() ? "at " : " at ") + s.position; }
    } else if (s.function == "inset") {
        out += fold_four(s.args) + round();
    } else if (s.function == "xywh" || s.function == "rect") {
        out += joined(s.args) + round();
    } else if (s.function == "polygon") {
        std::vector<std::string> items;
        std::string lead = s.fill_rule;
        if (!s.round.empty()) { lead += (lead.empty() ? "" : " ") + ("round " + s.round); }
        if (!lead.empty()) { items.push_back(lead); }
        for (std::size_t i = 0; i + 1 < s.points.size(); i += 2) {
            items.push_back(s.points[i] + " " + s.points[i + 1]);
        }
        out += joined(items, ", ");
    } else if (s.function == "ray") {
        out += s.args.empty() ? std::string{} : s.args.front();
        if (!s.size.empty()) { out += " " + s.size; }
        if (s.contain) { out += " contain"; }
        if (!s.position.empty()) { out += " at " + s.position; }
    } else if (s.function == "path") {
        out += (s.fill_rule.empty() ? "" : s.fill_rule + ", ") + string_text(s.path);
    } else if (s.function == "shape") {
        out += (s.fill_rule.empty() ? "" : s.fill_rule + " ") + "from " + s.position;
        for (const shape_command & c : s.commands) { out += ", " + command_text(c); }
    }
    out += ")";
    if (!s.box.empty()) { out += " " + s.box; }
    return out;
}

// A shape and its box, in either order; nullopt when `text` is not one of
// the shapes above. `default_box` is the one left unwritten.
[[nodiscard]] std::optional<shape_parts> parse_shape_value(std::string_view text,
                                                           std::string_view default_box) {
    const std::vector<std::string_view> parts = words_of(text);
    if (parts.empty() || parts.size() > 2) { return std::nullopt; }
    std::optional<shape_parts> shape;
    std::string box;
    for (const std::string_view part : parts) {
        if (has_keyword(geometry_boxes, part) && box.empty() && part.find('(') == part.npos) {
            box = ascii_lower_copy(part);
        } else if (!shape) {
            shape = parse_function(part);
            if (!shape) { return std::nullopt; }
        } else {
            return std::nullopt;
        }
    }
    if (!shape) { return std::nullopt; }
    shape->box = box == default_box ? std::string{} : box;
    return shape;
}

[[nodiscard]] std::string_view default_box_of(std::string_view property) {
    return property == "shape-outside" ? "margin-box" : "border-box";
}

// One component's computed text: its math folded against `ctx`, a length
// in pixels, a percentage and a mix of the two in canonical form.
[[nodiscard]] std::string computed_component(std::string_view text, const length_context & ctx,
                                             bool nonnegative) {
    if (ascii_iequals_any(text, {"auto", "closest-side", "farthest-side"})) {
        return std::string{text};
    }
    const math_answer answer = evaluate_math(text, ctx);
    if (answer.outcome != math_outcome::resolved) { return std::string{text}; }
    std::string out = serialize_calc(answer.value);
    return nonnegative ? non_negative(out) : out;
}

[[nodiscard]] std::optional<calc_result> amount(std::string_view text) {
    const math_answer answer = evaluate_math(text, length_context{});
    if (answer.outcome != math_outcome::resolved) { return std::nullopt; }
    return answer.value;
}

// `100% - a - b` for xywh() and rect(), in canonical form.
[[nodiscard]] std::optional<std::string> remainder(std::string_view a, std::string_view b) {
    calc_result out;
    out.has_percent = true;
    out.percent = 100;
    for (const std::string_view term : {a, b}) {
        if (term.empty()) { continue; }
        const std::optional<calc_result> v = amount(term);
        if (!v) { return std::nullopt; }
        out.px -= v->px;
        out.percent -= v->percent;
    }
    return serialize_calc(out);
}

// A path() AS THE shape() IT DRAWS, which is how the two interpolate (CSS
// Shapes 2 §3.3): its moveto the `from`, each later command the shape
// command of the same meaning - an absolute one `to`, a relative one `by` -
// and every number a length in pixels. A quadratic is a one-control curve,
// `T` a smooth with none, and an arc's flags its `large` and `cw`.
[[nodiscard]] std::optional<shape_parts> shape_of_path(const shape_parts & path) {
    const std::optional<std::vector<path_segment>> data = parse_path_data(path.path);
    if (!data) { return std::nullopt; }
    shape_parts out;
    out.function = "shape";
    out.fill_rule = path.fill_rule;
    out.box = path.box;
    const auto px = [](double v) { return serialize_number(v) + "px"; };
    const auto pair = [&px](double x, double y) { return px(x) + " " + px(y); };
    for (const path_segment & s : *data) {
        const std::vector<double> & n = s.numbers;
        const char verb = static_cast<char>(s.command | 0x20);
        if (out.position.empty()) {
            out.position = pair(n[0], n[1]); // parse_path_data refuses any other first command
            continue;
        }
        shape_command c;
        c.by = s.command == verb && verb != 'z';
        switch (verb) {
        case 'm':
            c.verb = "move";
            c.end = pair(n[0], n[1]);
            break;
        case 'l':
            c.verb = "line";
            c.end = pair(n[0], n[1]);
            break;
        case 'h':
            c.verb = "hline";
            c.end = px(n[0]);
            break;
        case 'v':
            c.verb = "vline";
            c.end = px(n[0]);
            break;
        case 'c':
            c.verb = "curve";
            c.end = pair(n[4], n[5]);
            c.controls = {{pair(n[0], n[1]), ""}, {pair(n[2], n[3]), ""}};
            break;
        case 'q':
            c.verb = "curve";
            c.end = pair(n[2], n[3]);
            c.controls = {{pair(n[0], n[1]), ""}};
            break;
        case 's':
            c.verb = "smooth";
            c.end = pair(n[2], n[3]);
            c.controls = {{pair(n[0], n[1]), ""}};
            break;
        case 't':
            c.verb = "smooth";
            c.end = pair(n[0], n[1]);
            break;
        case 'a':
            c.verb = "arc";
            c.end = pair(n[5], n[6]);
            c.radii = {px(n[0]), px(n[1])};
            if (n[2] != 0) { c.rotate = serialize_number(n[2]) + "deg"; }
            c.large = n[3] != 0;
            c.cw = n[4] != 0;
            break;
        default: c.verb = "close"; break;
        }
        out.commands.push_back(std::move(c));
    }
    return out;
}

} // namespace

namespace detail {

bool match_basic_shape(std::string_view property, std::string_view text, std::string & out) {
    const std::vector<std::string_view> parts = words_of(text);
    // `object-view-box: none | <basic-shape-rect>` (CSS Images 4 §5.7): an
    // inset(), rect() or xywh() and nothing beside it.
    if (ascii_iequals(property, "object-view-box")) {
        std::optional<shape_parts> shape;
        if (parts.size() == 1 && ascii_iequals(parts[0], "none")) {
            out = "none";
        } else if (parts.size() == 1 && (shape = parse_shape_value(text, "border-box")) &&
                   ascii_iequals_any(shape->function, {"inset", "rect", "xywh"})) {
            out = serialize(*shape);
        } else {
            out.clear();
        }
        return true;
    }
    if (!ascii_iequals_any(property, {"clip-path", "shape-outside", "offset-path"})) {
        return false;
    }
    // A box alone is a value too, written as itself.
    bool any_function = false;
    for (const std::string_view part : parts) {
        any_function = any_function || part.find('(') != std::string_view::npos;
    }
    if (!any_function) { return false; }
    std::optional<shape_parts> shape = parse_shape_value(text, default_box_of(property));
    if (!shape) {
        // A url() or an image is not this file's.
        for (const std::string_view part : parts) {
            if (ascii_istarts_with(part, "url(") ||
                part.find("gradient(") != std::string_view::npos ||
                ascii_istarts_with(part, "image(") || ascii_istarts_with(part, "image-set(") ||
                ascii_istarts_with(part, "cross-fade(")) {
                return false;
            }
        }
        out.clear();
        return true;
    }
    out = serialize(*shape);
    return true;
}

} // namespace detail

std::string computed_basic_shape(std::string_view property, std::string_view text,
                                 const length_context & ctx) {
    std::optional<shape_parts> s = parse_shape_value(text, default_box_of(property));
    if (!s) { return {}; }
    for (std::string & a : s->args) {
        a = computed_component(a, ctx, s->function == "circle" || s->function == "ellipse");
    }
    for (std::string & r : s->radii) { r = computed_component(r, ctx, true); }
    for (std::string & p : s->points) { p = computed_component(p, ctx, false); }
    if (!s->round.empty()) { s->round = computed_component(s->round, ctx, true); }
    if (!s->position.empty()) {
        const std::string physical = computed_position(s->position, "", "");
        if (!physical.empty()) {
            std::string resolved;
            for (const std::string_view half : words_of(physical)) {
                resolved += (resolved.empty() ? "" : " ") + computed_component(half, ctx, false);
            }
            s->position = std::move(resolved);
        }
    }
    // A shape()'s points compute as the positions they name - `center` is 50%
    // - and its lengths as pixels (CSS Shapes 2 §3.2.2).
    const auto computed_point = [&ctx](std::string & text) {
        const std::string physical = computed_position(text, "", "");
        std::string resolved;
        for (const std::string_view half : words_of(physical.empty() ? text : physical)) {
            resolved += (resolved.empty() ? "" : " ") + computed_component(half, ctx, false);
        }
        text = std::move(resolved);
    };
    for (shape_command & c : s->commands) {
        if (c.verb == "hline" || c.verb == "vline") {
            if (has_keyword("left top x-start y-start", c.end)) {
                c.end = "0%";
            } else if (c.end == "center") {
                c.end = "50%";
            } else if (has_keyword("right bottom x-end y-end", c.end)) {
                c.end = "100%";
            } else {
                c.end = computed_component(c.end, ctx, false);
            }
        } else if (c.verb != "close") {
            computed_point(c.end);
        }
        for (auto & control : c.controls) { computed_point(control.first); }
        for (std::string & r : c.radii) { r = computed_component(r, ctx, true); }
        if (const std::optional<double> d = degrees_of(c.rotate)) {
            c.rotate = serialize_number(*d) + "deg";
        }
    }
    // xywh() and rect() compute to the inset() they describe (§3.2).
    if (s->function == "xywh" && s->args.size() == 4) {
        const std::optional<std::string> right = remainder(s->args[0], s->args[2]);
        const std::optional<std::string> bottom = remainder(s->args[1], s->args[3]);
        if (right && bottom) {
            s->args = {s->args[1], *right, *bottom, s->args[0]};
            s->function = "inset";
        }
    } else if (s->function == "rect" && s->args.size() == 4) {
        const auto side = [&](std::size_t i, bool from_end) -> std::optional<std::string> {
            if (s->args[i] == "auto") { return std::string{"0%"}; }
            return from_end ? remainder(s->args[i], "") : std::optional{s->args[i]};
        };
        const std::optional<std::string> top = side(0, false), right = side(1, true),
                                         bottom = side(2, true), left = side(3, false);
        if (top && right && bottom && left) {
            s->args = {*top, *right, *bottom, *left};
            s->function = "inset";
        }
    }
    return serialize(*s);
}

std::optional<std::string> blend_basic_shapes(std::string_view property, std::string_view a,
                                              std::string_view b, double wa, double wb) {
    std::optional<shape_parts> x = parse_shape_value(a, default_box_of(property));
    std::optional<shape_parts> y = parse_shape_value(b, default_box_of(property));
    if (x && y && x->function == "path" && y->function == "shape") { x = shape_of_path(*x); }
    if (x && y && y->function == "path" && x->function == "shape") { y = shape_of_path(*y); }
    if (!x || !y || x->function != y->function || x->box != y->box ||
        x->fill_rule != y->fill_rule || x->size != y->size || x->contain != y->contain) {
        return std::nullopt;
    }
    bool ok = true;
    const auto mix = [&](std::string_view p, std::string_view q, bool nonnegative) {
        if (p == q && ascii_iequals(p, "auto")) { return std::string{p}; }
        // A radius keyword makes a circle or an ellipse discrete, the same
        // keyword on both sides included (CSS Shapes 1 §3.3's "none of the
        // radii use the closest-side or farthest-side keywords").
        if (ascii_iequals_any(p, {"closest-side", "farthest-side"}) ||
            ascii_iequals_any(q, {"closest-side", "farthest-side"})) {
            ok = false;
            return std::string{};
        }
        const std::optional<calc_result> u = amount(p.empty() ? "0px" : p);
        const std::optional<calc_result> v = amount(q.empty() ? "0px" : q);
        if (!u || !v) {
            ok = false;
            return std::string{};
        }
        calc_result out;
        out.px = wa * u->px + wb * v->px;
        out.percent = wa * u->percent + wb * v->percent;
        out.has_percent = u->has_percent || v->has_percent;
        out.type = u->has_percent && u->px == 0 ? v->type : u->type;
        out.is_number = u->is_number && v->is_number;
        std::string text = serialize_calc(out);
        return nonnegative ? non_negative(text) : text;
    };
    const auto mix_all = [&](std::vector<std::string> & p, const std::vector<std::string> & q,
                             bool nonnegative) {
        if (p.size() != q.size()) {
            ok = false;
            return;
        }
        for (std::size_t i = 0; i < p.size(); ++i) { p[i] = mix(p[i], q[i], nonnegative); }
    };
    const bool radial = x->function == "circle" || x->function == "ellipse";
    if (x->function == "inset") {
        x->args = four(x->args);
        y->args = four(y->args);
    }
    if (radial) {
        // An omitted radius is `closest-side` and blends with nothing else.
        if (x->args.size() != y->args.size()) { return std::nullopt; }
    }
    // An omitted position is the centre, written once either side has one.
    if (radial || x->function == "ray") {
        if (x->position.empty() != y->position.empty()) {
            if (x->position.empty()) { x->position = "50% 50%"; }
            if (y->position.empty()) { y->position = "50% 50%"; }
        }
    }
    mix_all(x->args, y->args, radial);
    if (!x->position.empty() || !y->position.empty()) {
        const std::vector<std::string_view> p = words_of(x->position), q = words_of(y->position);
        if (p.size() != 2 || q.size() != 2) { return std::nullopt; }
        x->position = mix(p[0], q[0], false) + " " + mix(p[1], q[1], false);
    }
    if (!x->radii.empty() || !y->radii.empty()) {
        if (x->radii.empty()) { x->radii.assign(8, "0px"); }
        if (y->radii.empty()) { y->radii.assign(8, "0px"); }
        mix_all(x->radii, y->radii, true);
    }
    if (x->function == "path") {
        // SVG 2 §9.3.9's path interpolation: both paths in ABSOLUTE commands -
        // `m 20 70` and `M 100 30` are one command, and the result is written
        // absolute as every engine writes it (offset-path-interpolation-001 to
        // -004) - the same commands in the same order, their numbers blended.
        // An arc's two flags are booleans: the nearer end's outside the
        // interval, and set in between wherever either end sets one.
        std::optional<std::vector<path_segment>> p = parse_path_data(x->path);
        std::optional<std::vector<path_segment>> q = parse_path_data(y->path);
        if (!p || !q || p->size() != q->size()) { return std::nullopt; }
        absolutize(*p);
        absolutize(*q);
        const bool interpolating = std::abs(wa + wb - 1) < 1e-9; // not an addition
        for (std::size_t i = 0; i < p->size(); ++i) {
            path_segment & s = (*p)[i];
            const path_segment & t = (*q)[i];
            if (s.command != t.command || s.numbers.size() != t.numbers.size()) {
                return std::nullopt;
            }
            for (std::size_t k = 0; k < s.numbers.size(); ++k) {
                const bool flag = s.command == 'A' && (k == 3 || k == 4);
                if (!flag) {
                    s.numbers[k] = wa * s.numbers[k] + wb * t.numbers[k];
                } else if (interpolating && wb <= 0) {
                    continue;
                } else if (interpolating && wb >= 1) {
                    s.numbers[k] = t.numbers[k];
                } else {
                    s.numbers[k] = s.numbers[k] != 0 || t.numbers[k] != 0 ? 1 : 0;
                }
            }
        }
        x->path = path_text(*p);
    }
    if (x->function == "shape") {
        // CSS Shapes 2 §3.3: the same commands, `to` or `by` alike, and each
        // control point anchored alike - then every value blends. An arc's
        // flags are booleans, set in between wherever either end sets them.
        if (x->commands.size() != y->commands.size()) { return std::nullopt; }
        const auto point = [&](std::string_view p, std::string_view q) {
            const std::vector<std::string_view> u = words_of(p), v = words_of(q);
            if (u.size() != 2 || v.size() != 2) {
                ok = false;
                return std::string{};
            }
            return mix(u[0], v[0], false) + " " + mix(u[1], v[1], false);
        };
        const bool interpolating = std::abs(wa + wb - 1) < 1e-9; // not an addition
        const auto flag = [&](bool from, bool to) {
            if (interpolating && wb <= 0) { return from; }
            if (interpolating && wb >= 1) { return to; }
            return from || to;
        };
        for (std::size_t i = 0; i < x->commands.size(); ++i) {
            shape_command & c = x->commands[i];
            const shape_command & d = y->commands[i];
            if (c.verb != d.verb || c.by != d.by || c.controls.size() != d.controls.size()) {
                return std::nullopt;
            }
            if (c.verb == "close") { continue; }
            c.end = c.verb == "hline" || c.verb == "vline" ? mix(c.end, d.end, false)
                                                           : point(c.end, d.end);
            for (std::size_t k = 0; k < c.controls.size(); ++k) {
                if (c.controls[k].second != d.controls[k].second) { return std::nullopt; }
                c.controls[k].first = point(c.controls[k].first, d.controls[k].first);
            }
            if (c.verb != "arc") { continue; }
            std::vector<std::string> radii = d.radii;
            if (c.radii.size() != radii.size()) {
                if (c.radii.size() == 1) { c.radii.push_back(c.radii.front()); }
                if (radii.size() == 1) { radii.push_back(radii.front()); }
            }
            mix_all(c.radii, radii, true);
            const double turn =
                wa * degrees_of(c.rotate).value_or(0) + wb * degrees_of(d.rotate).value_or(0);
            c.rotate = turn == 0 ? std::string{} : serialize_number(turn) + "deg";
            c.cw = flag(c.cw, d.cw);
            c.large = flag(c.large, d.large);
        }
    }
    if (x->function == "polygon") {
        mix_all(x->points, y->points, false);
        if (!x->round.empty() || !y->round.empty()) {
            x->round = mix(x->round, y->round, true);
            if (zero(x->round)) { x->round.clear(); }
        }
    }
    if (!ok) { return std::nullopt; }
    return serialize(*x);
}

} // namespace ctbrowser::style::css
