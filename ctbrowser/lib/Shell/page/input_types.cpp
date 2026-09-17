// The input types' value model - see the header. The calendar arithmetic is
// the proleptic Gregorian days-from-civil pair, which is what makes a week
// number or a month index a plain integer.

#include <ctbrowser/core/algorithms.hpp>
#include <ctbrowser/shell/page/input_types.hpp>
#include <ctbrowser/style/css/properties.hpp>

#include <charconv>
#include <cmath>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ctbrowser::shell::input_types {

using ctbrowser::html_whitespace;
using ctbrowser::trim;

// A "valid floating-point number" (HTML 2.3.5.2) when `lenient` is false:
// an optional minus, digits, an optional fraction, an optional exponent, and
// nothing else. The "rules for parsing floating-point number values" when
// true: leading whitespace and a plus sign allowed, trailing garbage ignored.
[[nodiscard]] bool parse_float(std::string_view text, double & out, bool lenient) {
    std::size_t at = 0;
    if (lenient) {
        while (at < text.size() && html_whitespace.find(text[at]) != std::string_view::npos) {
            ++at;
        }
    }
    std::string canonical;
    if (at < text.size() && (text[at] == '-' || (lenient && text[at] == '+'))) {
        if (text[at] == '-') { canonical += '-'; }
        ++at;
    }
    const auto digit = [&](std::size_t i) {
        return i < text.size() && text[i] >= '0' && text[i] <= '9';
    };
    if (!digit(at)) {
        if (!lenient || !(at < text.size() && text[at] == '.' && digit(at + 1))) { return false; }
        canonical += '0';
    }
    while (digit(at)) { canonical += text[at++]; }
    if (at < text.size() && text[at] == '.') {
        if (!lenient && !digit(at + 1)) { return false; }
        ++at;
        canonical += '.';
        if (!digit(at)) { canonical += '0'; }
        while (digit(at)) { canonical += text[at++]; }
    }
    if (at < text.size() && (text[at] == 'e' || text[at] == 'E')) {
        std::size_t look = at + 1;
        std::string exponent = "e";
        if (look < text.size() && (text[look] == '-' || text[look] == '+')) {
            if (text[look] == '-') { exponent += '-'; }
            ++look;
        }
        if (digit(look)) {
            while (digit(look)) { exponent += text[look++]; }
            canonical += exponent;
            at = look;
        } else if (!lenient) {
            return false;
        }
    }
    if (!lenient && at != text.size()) { return false; }
    const auto result = std::from_chars(canonical.data(), canonical.data() + canonical.size(), out);
    return result.ec == std::errc{} && std::isfinite(out);
}

// --- the calendar, proleptic Gregorian, days from 1970-01-01 ----------------------
[[nodiscard]] static long long days_from_civil(long long y, unsigned m, unsigned d) {
    y -= m <= 2;
    const long long era = (y >= 0 ? y : y - 399) / 400;
    const auto yoe = static_cast<unsigned long long>(y - era * 400);
    const unsigned mp = m > 2 ? m - 3 : m + 9;
    const unsigned long long doy = (153ull * mp + 2) / 5 + d - 1;
    const unsigned long long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<long long>(doe) - 719468;
}
struct civil {
    long long year;
    unsigned month;
    unsigned day;
};
[[nodiscard]] static civil civil_from_days(long long z) {
    z += 719468;
    const long long era = (z >= 0 ? z : z - 146096) / 146097;
    const auto doe = static_cast<unsigned long long>(z - era * 146097);
    const unsigned long long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const long long y = static_cast<long long>(yoe) + era * 400;
    const unsigned long long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned long long mp = (5 * doy + 2) / 153;
    const auto d = static_cast<unsigned>(doy - (153 * mp + 2) / 5 + 1);
    const auto m = static_cast<unsigned>(mp < 10 ? mp + 3 : mp - 9);
    return {y + (m <= 2), m, d};
}
[[nodiscard]] static unsigned days_in_month(long long y, unsigned m) {
    static constexpr unsigned lengths[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    const bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
    return m == 2 && leap ? 29 : lengths[m - 1];
}
// Monday-based day of week, 0..6, of a day count (1970-01-01 was a Thursday).
[[nodiscard]] static unsigned weekday_of(long long days) {
    return static_cast<unsigned>(((days + 3) % 7 + 7) % 7);
}
[[nodiscard]] static unsigned weeks_in_year(long long y) {
    const unsigned jan1 = weekday_of(days_from_civil(y, 1, 1));
    const bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
    return jan1 == 3 || (leap && jan1 == 2) ? 53 : 52;
}
constexpr double ms_per_day = 86400000.0;

// A run of exactly `n` digits at `at`, advanced past.
[[nodiscard]] static bool digits(std::string_view text, std::size_t & at, std::size_t n,
                                 long long & out) {
    if (at + n > text.size()) { return false; }
    out = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const char ch = text[at + i];
        if (ch < '0' || ch > '9') { return false; }
        out = out * 10 + (ch - '0');
    }
    at += n;
    return true;
}
// At least four digits: a year.
[[nodiscard]] static bool year_digits(std::string_view text, std::size_t & at, long long & out) {
    std::size_t n = 0;
    while (at + n < text.size() && text[at + n] >= '0' && text[at + n] <= '9') { ++n; }
    if (n < 4) { return false; }
    return digits(text, at, n, out) && out > 0;
}
[[nodiscard]] static std::optional<double> parse_date_ms(std::string_view text, std::size_t & at) {
    long long y = 0;
    long long m = 0;
    long long d = 0;
    if (!year_digits(text, at, y) || at >= text.size() || text[at] != '-') { return std::nullopt; }
    ++at;
    if (!digits(text, at, 2, m) || m < 1 || m > 12) { return std::nullopt; }
    if (at >= text.size() || text[at] != '-') { return std::nullopt; }
    ++at;
    if (!digits(text, at, 2, d) || d < 1 || d > days_in_month(y, static_cast<unsigned>(m))) {
        return std::nullopt;
    }
    return static_cast<double>(
               days_from_civil(y, static_cast<unsigned>(m), static_cast<unsigned>(d))) *
           ms_per_day;
}
[[nodiscard]] static std::optional<double> parse_time_ms(std::string_view text, std::size_t & at) {
    long long h = 0;
    long long mi = 0;
    if (!digits(text, at, 2, h) || h > 23 || at >= text.size() || text[at] != ':') {
        return std::nullopt;
    }
    ++at;
    if (!digits(text, at, 2, mi) || mi > 59) { return std::nullopt; }
    double ms = static_cast<double>(h * 3600000 + mi * 60000);
    if (at < text.size() && text[at] == ':') {
        ++at;
        long long s = 0;
        if (!digits(text, at, 2, s) || s > 59) { return std::nullopt; }
        ms += static_cast<double>(s) * 1000;
        if (at < text.size() && text[at] == '.') {
            ++at;
            std::size_t n = 0;
            while (n < 3 && at + n < text.size() && text[at + n] >= '0' && text[at + n] <= '9') {
                ++n;
            }
            if (n == 0) { return std::nullopt; }
            long long fraction = 0;
            (void)digits(text, at, n, fraction);
            for (std::size_t i = n; i < 3; ++i) { fraction *= 10; }
            ms += static_cast<double>(fraction);
        }
    }
    return ms;
}
// The value of a typed input as its number: ms for the date and time types,
// months since 1970-01 for month, the number itself for number and range.
[[nodiscard]] std::optional<double> type_value_to_number(std::string_view type,
                                                         std::string_view text) {
    std::size_t at = 0;
    if (type == "number" || type == "range") {
        double out = 0;
        if (!parse_float(text, out, false)) { return std::nullopt; }
        return out;
    }
    if (type == "date") {
        const std::optional<double> ms = parse_date_ms(text, at);
        return at == text.size() ? ms : std::nullopt;
    }
    if (type == "month") {
        long long y = 0;
        long long m = 0;
        if (!year_digits(text, at, y) || at >= text.size() || text[at] != '-') {
            return std::nullopt;
        }
        ++at;
        if (!digits(text, at, 2, m) || m < 1 || m > 12 || at != text.size()) {
            return std::nullopt;
        }
        return static_cast<double>((y - 1970) * 12 + (m - 1));
    }
    if (type == "week") {
        long long y = 0;
        long long w = 0;
        if (!year_digits(text, at, y) || at + 1 >= text.size() || text[at] != '-' ||
            text[at + 1] != 'W') {
            return std::nullopt;
        }
        at += 2;
        if (!digits(text, at, 2, w) || w < 1 || w > weeks_in_year(y) || at != text.size()) {
            return std::nullopt;
        }
        const long long jan4 = days_from_civil(y, 1, 4);
        const long long monday = jan4 - weekday_of(jan4) + (w - 1) * 7;
        return static_cast<double>(monday) * ms_per_day;
    }
    if (type == "time") {
        const std::optional<double> ms = parse_time_ms(text, at);
        return at == text.size() ? ms : std::nullopt;
    }
    if (type == "datetime-local") {
        const std::optional<double> day = parse_date_ms(text, at);
        if (!day || at >= text.size() || (text[at] != 'T' && text[at] != ' ')) {
            return std::nullopt;
        }
        ++at;
        const std::optional<double> time = parse_time_ms(text, at);
        if (!time || at != text.size()) { return std::nullopt; }
        return *day + *time;
    }
    return std::nullopt;
}
[[nodiscard]] static std::string two(long long n) {
    return (n < 10 ? "0" : "") + std::to_string(n);
}
[[nodiscard]] static std::string date_text(double ms) {
    const civil when = civil_from_days(static_cast<long long>(std::floor(ms / ms_per_day)));
    std::string year = std::to_string(when.year);
    while (year.size() < 4) { year.insert(0, "0"); }
    return year + "-" + two(when.month) + "-" + two(when.day);
}
[[nodiscard]] static std::string time_text(double ms_in_day) {
    const auto total = static_cast<long long>(ms_in_day);
    const long long h = total / 3600000;
    const long long mi = (total / 60000) % 60;
    const long long s = (total / 1000) % 60;
    const long long fraction = total % 1000;
    std::string out = two(h) + ":" + two(mi);
    if (s != 0 || fraction != 0) {
        out += ":" + two(s);
        if (fraction != 0) {
            std::string f = std::to_string(fraction);
            while (f.size() < 3) { f.insert(0, "0"); }
            while (f.back() == '0') { f.pop_back(); }
            out += "." + f;
        }
    }
    return out;
}
// The number of a typed input written back as its value; "" when it does
// not fit the type.
[[nodiscard]] std::string type_number_to_text(std::string_view type, double number) {
    if (!std::isfinite(number)) { return {}; }
    if (type == "number" || type == "range") {
        // JavaScript's Number-to-string, close enough for a finite value:
        // an integer prints without a fraction, the rest shortest-round-trip.
        char buffer[64];
        const auto result = std::to_chars(buffer, buffer + sizeof buffer, number);
        return std::string{buffer, result.ptr};
    }
    if (type == "date") { return date_text(number); }
    if (type == "month") {
        const auto months = static_cast<long long>(std::floor(number));
        long long y = 1970 + months / 12;
        long long m = months % 12;
        if (m < 0) {
            m += 12;
            --y;
        }
        if (y < 1) { return {}; }
        std::string year = std::to_string(y);
        while (year.size() < 4) { year.insert(0, "0"); }
        return year + "-" + two(m + 1);
    }
    if (type == "week") {
        const auto days = static_cast<long long>(std::floor(number / ms_per_day));
        const long long monday = days - weekday_of(days);
        const long long thursday = monday + 3;
        const long long y = civil_from_days(thursday).year;
        const long long jan4 = days_from_civil(y, 1, 4);
        const long long week1 = jan4 - weekday_of(jan4);
        const long long w = (monday - week1) / 7 + 1;
        if (y < 1) { return {}; }
        std::string year = std::to_string(y);
        while (year.size() < 4) { year.insert(0, "0"); }
        return year + "-W" + two(w);
    }
    if (type == "time") {
        const double in_day = std::fmod(std::fmod(number, ms_per_day) + ms_per_day, ms_per_day);
        return time_text(std::floor(in_day));
    }
    if (type == "datetime-local") {
        const double day = std::floor(number / ms_per_day) * ms_per_day;
        return date_text(number) + "T" + time_text(std::floor(number - day));
    }
    return {};
}
[[nodiscard]] double month_index_to_ms(double months) {
    const auto m = static_cast<long long>(std::floor(months));
    long long y = 1970 + m / 12;
    long long mo = m % 12;
    if (mo < 0) {
        mo += 12;
        --y;
    }
    return static_cast<double>(days_from_civil(y, static_cast<unsigned>(mo + 1), 1)) * ms_per_day;
}
[[nodiscard]] double ms_to_month_index(double ms) {
    const civil when = civil_from_days(static_cast<long long>(std::floor(ms / ms_per_day)));
    return static_cast<double>((when.year - 1970) * 12 + (when.month - 1));
}
// The step scale factor and the default step, HTML 4.10.5.1, in the type's
// unit (ms, months, or the number).
[[nodiscard]] double step_scale_of(std::string_view type) {
    if (type == "date") { return ms_per_day; }
    if (type == "week") { return 7 * ms_per_day; }
    if (type == "time" || type == "datetime-local") { return 1000; }
    return 1;
}
[[nodiscard]] double default_step_of(std::string_view type) {
    if (type == "time" || type == "datetime-local") { return 60; }
    return 1;
}

// A "valid e-mail address", HTML 4.10.5.1.5's regular expression by hand.
[[nodiscard]] bool is_valid_email(std::string_view text) {
    const std::size_t at = text.find('@');
    if (at == std::string_view::npos || at == 0 || at + 1 >= text.size()) { return false; }
    constexpr std::string_view local_extra = ".!#$%&'*+/=?^_`{|}~-";
    const auto alnum = [](char ch) {
        return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9');
    };
    for (const char ch : text.substr(0, at)) {
        if (!alnum(ch) && local_extra.find(ch) == std::string_view::npos) { return false; }
    }
    const std::string_view domain = text.substr(at + 1);
    std::size_t start = 0;
    while (true) {
        const std::size_t dot = domain.find('.', start);
        const std::string_view label = domain.substr(
            start, dot == std::string_view::npos ? std::string_view::npos : dot - start);
        if (label.empty() || label.size() > 63 || !alnum(label.front()) || !alnum(label.back())) {
            return false;
        }
        for (const char ch : label) {
            if (!alnum(ch) && ch != '-') { return false; }
        }
        if (dot == std::string_view::npos) { return true; }
        start = dot + 1;
    }
}
// A "valid URL potentially surrounded by spaces" that is absolute: a scheme,
// a colon, and no whitespace inside.
[[nodiscard]] bool is_valid_url(std::string_view text) {
    const std::string_view trimmed = trim(text, html_whitespace);
    const std::size_t colon = trimmed.find(':');
    if (colon == std::string_view::npos || colon == 0) { return false; }
    const char first = trimmed.front();
    if (!((first >= 'a' && first <= 'z') || (first >= 'A' && first <= 'Z'))) { return false; }
    for (const char ch : trimmed.substr(0, colon)) {
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
              ch == '+' || ch == '-' || ch == '.')) {
            return false;
        }
    }
    for (const char ch : trimmed) {
        if (html_whitespace.find(ch) != std::string_view::npos) { return false; }
    }
    return true;
}

std::string type_state_of(std::string_view type_attribute) {
    const std::string type = ascii_lower_copy(type_attribute);
    constexpr std::string_view known =
        "hidden text search tel url email password date month week time datetime-local "
        "number range color checkbox radio file submit image reset button";
    for (const std::string_view word : split_top_level(known, " ")) {
        if (word == type) { return type; }
    }
    return "text";
}

std::string_view value_mode_of(std::string_view type) {
    if (type == "hidden" || type == "submit" || type == "image" || type == "reset" ||
        type == "button") {
        return "default";
    }
    if (type == "checkbox" || type == "radio") { return "default/on"; }
    if (type == "file") { return "filename"; }
    return "value";
}

std::string sanitize_value(std::string_view type, std::string value, std::string_view min,
                           std::string_view max, std::string_view step) {
    // "Strip newlines from the value" - the text, search, tel and password
    // states; url and email also "strip leading and trailing ASCII whitespace",
    // and a multiple email is each address so stripped, comma-joined.
    const auto strip_newlines = [](std::string & text) {
        std::erase_if(text, [](char ch) { return ch == '\n' || ch == '\r'; });
    };
    if (type == "text" || type == "search" || type == "tel" || type == "password") {
        strip_newlines(value);
        return value;
    }
    if (type == "url") {
        strip_newlines(value);
        return std::string{trim(value, html_whitespace)};
    }
    if (type == "email") {
        strip_newlines(value);
        // Without the `multiple` attribute's word here, one address; the
        // caller that knows `multiple` splits the same way per address.
        return std::string{trim(value, html_whitespace)};
    }
    if (type == "number") {
        double parsed = 0;
        return parse_float(value, parsed, false) ? value : std::string{};
    }
    if (type == "range") {
        // A range's default is the middle of its bounds (or the minimum when
        // the maximum is below it), and a value outside them is clamped.
        double low = 0;
        double high = 100;
        double parsed = 0;
        if (!min.empty() && parse_float(min, parsed, false)) { low = parsed; }
        if (!max.empty() && parse_float(max, parsed, false)) { high = parsed; }
        double number = 0;
        if (!parse_float(value, number, false)) {
            number = high < low ? low : low + (high - low) / 2;
        }
        if (number < low) { number = low; }
        if (high >= low && number > high) { number = high; }
        (void)step;
        return type_number_to_text(type, number);
    }
    if (type == "color") {
        // HTML's "update a color well control color": the value parsed as a
        // CSS <color> (opaque black when it is not one) and serialised for
        // the control's colour space. The `colorspace` and `alpha` attributes
        // are not threaded through here yet, so this is the limited-sRGB,
        // opaque form: `#rrggbb`.
        return style::css::sanitize_color(value, false, false);
    }
    if (type == "date" || type == "month" || type == "week" || type == "time" ||
        type == "datetime-local") {
        // A string that is not a valid one for the type is the empty string;
        // datetime-local is also normalised (no seconds when they are zero,
        // `T` rather than a space), which is what writing the parsed value
        // back does.
        const std::optional<double> number = type_value_to_number(type, value);
        if (!number) { return {}; }
        return type == "datetime-local" ? type_number_to_text(type, *number) : value;
    }
    return value;
}

std::string autocomplete_idl_value(std::string_view attribute, bool has_attribute,
                                   bool anchor_mantle) {
    if (!has_attribute) { return {}; }
    std::vector<std::string> tokens;
    for (const std::string_view word : split_top_level(attribute, html_whitespace)) {
        if (!word.empty()) { tokens.push_back(ascii_lower_copy(word)); }
    }
    if (tokens.empty()) { return {}; }
    constexpr std::string_view normal_fields =
        "name honorific-prefix given-name additional-name family-name honorific-suffix nickname "
        "username new-password current-password one-time-code organization-title organization "
        "street-address address-line1 address-line2 address-line3 address-level4 address-level3 "
        "address-level2 address-level1 country country-name postal-code cc-name cc-given-name "
        "cc-additional-name cc-family-name cc-number cc-exp cc-exp-month cc-exp-year cc-csc "
        "cc-type transaction-currency transaction-amount language bday bday-day bday-month "
        "bday-year sex url photo";
    constexpr std::string_view contact_fields =
        "tel tel-country-code tel-national tel-area-code tel-local tel-local-prefix "
        "tel-local-suffix tel-extension email impp";
    const auto listed = [](std::string_view list, std::string_view word) {
        for (const std::string_view each : split_top_level(list, " ")) {
            if (each == word) { return true; }
        }
        return false;
    };
    // The field's category and the maximum number of tokens it allows.
    enum class category : std::uint8_t {
        none,
        off,
        automatic,
        normal,
        contact,
        credential
    };
    const auto category_of = [&](std::string_view field) {
        if (field == "off") { return std::pair{category::off, std::size_t{1}}; }
        if (field == "on") { return std::pair{category::automatic, std::size_t{1}}; }
        if (listed(normal_fields, field)) { return std::pair{category::normal, std::size_t{3}}; }
        if (listed(contact_fields, field)) { return std::pair{category::contact, std::size_t{4}}; }
        if (field == "webauthn") { return std::pair{category::credential, std::size_t{5}}; }
        return std::pair{category::none, std::size_t{0}};
    };
    std::size_t index = tokens.size() - 1;
    std::string field = tokens[index];
    auto [kind, maximum] = category_of(field);
    if (kind == category::none || tokens.size() > maximum) { return {}; }
    if ((kind == category::off || kind == category::automatic) && anchor_mantle) { return {}; }
    if (kind == category::off || kind == category::automatic) { return field; }
    std::string out = field;
    if (kind == category::credential) {
        // `webauthn` names a credential type after a field of the other two
        // categories, which then sets the remaining shape.
        if (index == 0) { return {}; }
        --index;
        const auto inner = category_of(tokens[index]);
        if (inner.first != category::normal && inner.first != category::contact) { return {}; }
        kind = inner.first;
        out = tokens[index] + " " + out;
    }
    if (kind == category::contact && index > 0 &&
        listed("home work mobile fax pager", tokens[index - 1])) {
        --index;
        out = tokens[index] + " " + out;
    }
    if (index > 0 && (tokens[index - 1] == "shipping" || tokens[index - 1] == "billing")) {
        --index;
        out = tokens[index] + " " + out;
    }
    if (index > 0 && tokens[index - 1].starts_with("section-")) {
        --index;
        out = tokens[index] + " " + out;
    }
    return index == 0 ? out : std::string{};
}

} // namespace ctbrowser::shell::input_types
