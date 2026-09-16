// dom_bindings - click, form control value and focus, the canvas methods
// getContext, toDataURL and toBlob, the table model (rows, cells, sections),
// and the forms: the select and option model, form.elements, labels,
// constraint validation, the selection API, the entry list and FormData.

#include "internal.hpp"

namespace ctbrowser::shell {

using namespace detail;

// ============================================================================
// THE FORMS' NUMBERS AND TEXT, HTML 4.10.5.1 and 2.3.5 - file-local because
// only the form bindings below read them.
// ============================================================================
namespace {

// --- UTF-16 code units over the store's UTF-8 value ---------------------------
//
// The selection API and `textLength` count code units; the store keeps byte
// offsets. A lone surrogate the page wrote is three WTF-8 bytes and one unit.
[[nodiscard]] std::size_t units_before(std::string_view text, std::size_t bytes) {
    std::size_t units = 0;
    const std::size_t stop = std::min(bytes, text.size());
    for (std::size_t at = 0; at < stop;) { units += decode_utf8(text, at) >= 0x10000 ? 2 : 1; }
    return units;
}
[[nodiscard]] std::size_t units_length_of(std::string_view text) {
    return units_before(text, text.size());
}
[[nodiscard]] std::size_t bytes_before(std::string_view text, std::size_t units) {
    std::size_t at = 0;
    std::size_t seen = 0;
    while (at < text.size() && seen < units) {
        const std::size_t here = at;
        const std::size_t width = decode_utf8(text, at) >= 0x10000 ? 2 : 1;
        if (seen + width > units) { return here; } // inside a pair: the pair's start
        seen += width;
    }
    return at;
}

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
[[nodiscard]] long long days_from_civil(long long y, unsigned m, unsigned d) {
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
[[nodiscard]] civil civil_from_days(long long z) {
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
[[nodiscard]] unsigned days_in_month(long long y, unsigned m) {
    static constexpr unsigned lengths[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    const bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
    return m == 2 && leap ? 29 : lengths[m - 1];
}
// Monday-based day of week, 0..6, of a day count (1970-01-01 was a Thursday).
[[nodiscard]] unsigned weekday_of(long long days) {
    return static_cast<unsigned>(((days + 3) % 7 + 7) % 7);
}
[[nodiscard]] unsigned weeks_in_year(long long y) {
    const unsigned jan1 = weekday_of(days_from_civil(y, 1, 1));
    const bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
    return jan1 == 3 || (leap && jan1 == 2) ? 53 : 52;
}
constexpr double ms_per_day = 86400000.0;

// A run of exactly `n` digits at `at`, advanced past.
[[nodiscard]] bool digits(std::string_view text, std::size_t & at, std::size_t n, long long & out) {
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
[[nodiscard]] bool year_digits(std::string_view text, std::size_t & at, long long & out) {
    std::size_t n = 0;
    while (at + n < text.size() && text[at + n] >= '0' && text[at + n] <= '9') { ++n; }
    if (n < 4) { return false; }
    return digits(text, at, n, out) && out > 0;
}
[[nodiscard]] std::optional<double> parse_date_ms(std::string_view text, std::size_t & at) {
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
[[nodiscard]] std::optional<double> parse_time_ms(std::string_view text, std::size_t & at) {
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
[[nodiscard]] std::string two(long long n) {
    return (n < 10 ? "0" : "") + std::to_string(n);
}
[[nodiscard]] std::string date_text(double ms) {
    const civil when = civil_from_days(static_cast<long long>(std::floor(ms / ms_per_day)));
    std::string year = std::to_string(when.year);
    while (year.size() < 4) { year.insert(0, "0"); }
    return year + "-" + two(when.month) + "-" + two(when.day);
}
[[nodiscard]] std::string time_text(double ms_in_day) {
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

// The hidden slots on a wrapper the forms keep their non-attribute state in:
// an option's selectedness and its dirtiness, a control's custom validity
// message, a FormData's entries.
constexpr std::string_view selected_slot = "__selected";
constexpr std::string_view dirty_slot = "__selectedDirty";
constexpr std::string_view custom_slot = "__customValidity";
constexpr std::string_view entries_slot = "__entries";

} // namespace

// `click`, `focus` and `blur` are HTMLElement's (focus and blur SVGElement's
// too); the canvas three are HTMLCanvasElement's. The EventTarget trio -
// addEventListener, removeEventListener, dispatchEvent - is NOT here: it lives
// on EventTarget.prototype (bindings/events/interfaces.cpp), which every node
// chains to, and `step_of` there already resolves a wrapper to its node.
void dom_bindings::install_control_methods(context & cx) {
    const std::initializer_list<const char *> html = {"HTMLElement"};
    const std::initializer_list<const char *> focusable = {"HTMLElement", "SVGElement"};
    const std::initializer_list<const char *> canvas = {"HTMLCanvasElement"};
    const auto method = [&](std::initializer_list<const char *> on, const char * name,
                            unsigned length, script::native_fn fn) {
        define_operation(cx, on, name, length, std::move(fn));
    };

    // `element.click()` - the whole of it is in dom_bindings::click, beside the
    // engine's own mouse events, because it IS one of those.
    method(html, "click", 0, [this](context & c, std::span<value>) {
        (void)click(receiver(c));
        return value::undefined();
    });

    // --- form controls -------------------------------------------------
    method(html, "getValue", 0, [this](context & c, std::span<value>) {
        const node_id id = receiver(c);
        if (!id) { return c.string(std::string{}); }
        const auto txn = doc_->read();
        return c.string(forms_->state_of(txn, *atoms_, id).value);
    });
    method(html, "setValue", 1, [this](context & c, std::span<value> args) {
        const node_id id = receiver(c);
        if (!id) { return value::undefined(); }
        const auto txn = doc_->read();
        control_state & control = forms_->state_of(txn, *atoms_, id);
        control.value = arg_string(c, args, 0);
        control.caret = control.value.size();
        control.selection = control.caret;
        control.value_edited = true;
        mutated();
        return value::undefined();
    });
    method(html, "isChecked", 0, [this](context & c, std::span<value>) {
        const node_id id = receiver(c);
        if (!id) { return value::boolean(false); }
        const auto txn = doc_->read();
        return value::boolean(forms_->state_of(txn, *atoms_, id).checked);
    });
    method(html, "setChecked", 1, [this](context & c, std::span<value> args) {
        const node_id id = receiver(c);
        if (!id) { return value::undefined(); }
        const auto txn = doc_->read();
        forms_->state_of(txn, *atoms_, id).checked = context::truthy(arg(args, 0));
        mutated();
        return value::undefined();
    });
    method(focusable, "focus", 0, [this](context & c, std::span<value>) {
        if (const node_id id = receiver(c); id && on_focus_) { on_focus_(id); }
        return value::undefined();
    });
    method(focusable, "blur", 0, [this](context &, std::span<value>) {
        if (on_focus_) { on_focus_(node_id{}); }
        return value::undefined();
    });

    // --- canvas --------------------------------------------------------
    method(canvas, "getContext", 1, [this](context & c, std::span<value> args) {
        const node_id id = receiver(c);
        const std::string kind = arg_string(c, args, 0);
        // "2d" and "webgl". `webgl2` IS NOT IMPLEMENTED AND RETURNS NULL, and
        // getting that right took two wrong answers.
        //
        // It first threw for the whole WebGL family, on the grounds that a null
        // was the silent-wrong-answer shape: p5 would take it, fall back to its
        // 2D renderer, and a WEBGL sketch would draw nothing 3D while reporting
        // nothing. A blank canvas and a clear conscience.
        //
        // When a real context arrived, `webgl2` kept throwing - and the comment
        // here claimed the throw was what made p5 fall back to `webgl`. That was
        // backwards, and measurably so. p5's RendererGL asks for `webgl2` first
        // and relies on `getContext(...) || getContext('webgl')`, so it needs a
        // FALSY VALUE to fall through. It catches nothing, so the throw escaped
        // the constructor, escaped createCanvas, and left the sketch on the
        // Renderer2D it already had - which is precisely the outcome the throw
        // was supposed to prevent.
        //
        // Null is also simply what the specification says: an unsupported
        // context id returns null, and feature detection is BUILT on that. It is
        // a documented "not supported" signal rather than a plausible wrong
        // answer, which is the distinction the loud-failure rule turns on.
        if (kind == "webgl" || kind == "experimental-webgl") {
            if (!id) { return value::null(); }
            return webgl_context_object(c, id, 1);
        }
        // `webgl2` RETURNS A CONTEXT NOW (2026-08-02). Everything above is the
        // history of it returning null, and every word of it was right at the
        // time; what changed is that the language and the two capabilities
        // behind it exist - see docs/history/webgl2.md stages 1 to 3.
        //
        // p5's RendererGL asks for this FIRST, so from here on every p5 WEBGL
        // sketch takes a path it has never taken in this engine.
        // examples/pages/p5-webgl.html's golden is the tripwire: the same
        // sketch must produce the same pixels through either context, and if
        // that image moves, this path is wrong rather than new.
        if (kind == "webgl2") {
            if (!id) { return value::null(); }
            return webgl_context_object(c, id, 2);
        }
        if (!id || kind != "2d") { return value::null(); }
        return canvas_context_object(c, id);
    });

    // `canvas.toDataURL()` and `canvas.toBlob()` - READING A CANVAS BACK OUT.
    //
    // Both mean PNG: that is what p5's save() asks for, and encode_png writes one
    // with no compression library (see shell/image/images.hpp). A `type` argument
    // naming anything else still gets PNG rather than a lie about the format -
    // the data URL says image/png, so a page that reads it back is not misled.
    const auto canvas_bytes = [this](context & c) -> std::vector<std::byte> {
        const node_id id = receiver(c);
        if (!id || canvases_ == nullptr) { return {}; }
        // context_for, not pixels_of: a canvas nobody asked getContext of has no
        // surface yet, and a browser still gives you a transparent PNG of the
        // right size rather than nothing. An empty answer here would look like a
        // broken encoder.
        const auto txn = doc_->read();
        (void)canvases_->context_for(id, static_cast<int>(size_attribute(txn, id, "width", 300)),
                                     static_cast<int>(size_attribute(txn, id, "height", 150)));
        const std::shared_ptr<const paint::bitmap> pixels = canvases_->pixels_of(id);
        return pixels ? encode_png(*pixels) : std::vector<std::byte>{};
    };
    method(canvas, "toDataURL", 0, [canvas_bytes](context & c, std::span<value>) {
        const std::vector<std::byte> png = canvas_bytes(c);
        std::string binary;
        binary.reserve(png.size());
        for (const std::byte b : png) { binary += static_cast<char>(b); }
        // Through the standard library's own btoa, so ONE base64 encoder decides
        // what this means here.
        const value encoder = c.global("btoa");
        if (!encoder.is_callable()) { return c.string("data:image/png;base64,"); }
        const value text = c.string(binary);
        const value args[1] = {text};
        return c.string("data:image/png;base64," + c.to_string(c.call(encoder, args)));
    });
    method(canvas, "toBlob", 1, [this, canvas_bytes](context & c, std::span<value> args) {
        const value callback = arg(args, 0);
        if (!callback.is_callable()) { return value::undefined(); }
        const std::vector<std::byte> png = canvas_bytes(c);
        // QUEUED, not called: toBlob is asynchronous, and a page that wraps it in
        // a promise - which is what p5's p5.Image.toBlob does - depends on the
        // callback landing after the call returns.
        const value blob_value = make_blob(c, make_u8_array(c, png), "image/png");
        c.queue_microtask(callback, std::vector<value>{blob_value});
        return value::undefined();
    });

    // --- THE TABLE MODEL, HTML 4.9.1, 4.9.5, 4.9.8 and 4.9.9 ----------------
    //
    // `table.rows`, `tBodies`, `caption`/`tHead`/`tFoot` and the create/delete
    // pair for each, `insertRow`/`deleteRow` on the table and on a section,
    // `tr.cells`/`rowIndex`/`sectionRowIndex`/`insertCell`/`deleteCell` and
    // `cell.cellIndex`. The collections are LIVE - `table.rows` after a
    // `deleteRow` is the shorter list - and every index rule is the
    // specification's: -1 means "the end", one past the end is an
    // IndexSizeError for an insert and the end itself is one for a delete.
    // getElementsByClassName-20..25.htm reach their cells through it.
    const auto proto = [this](const char * which) {
        return prototype_object(interface_prototype(which));
    };
    const auto operation = [&](const char * which, const char * name, unsigned length,
                               script::native_fn fn) {
        define_operation(cx, {which}, name, length, std::move(fn));
    };
    const auto getter = [&](const char * which, const char * name, script::native_fn get,
                            script::native_fn set = nullptr) {
        auto * on = proto(which);
        if (on == nullptr) { return; }
        on->define_accessor(
            name, value::object(cx.allocate<script::native_object>(name, std::move(get))),
            set == nullptr
                ? value::undefined()
                : value::object(cx.allocate<script::native_object>(name, std::move(set))));
    };
    const auto is = [](const read_txn & txn, node_id id, std::string_view name) {
        return id && txn.element_ns(id) == node_ns::html && txn.local_name(id) == name;
    };
    // The first child called `name`, or none.
    const auto first_child = [this, is](node_id parent, std::string_view name) {
        const auto txn = doc_->read();
        for (const node_id child : txn.children(parent)) {
            if (is(txn, child, name)) { return child; }
        }
        return node_id{};
    };
    // A section's rows: its `tr` children. A table's: the `thead` children's
    // rows, then its own `tr` children and its `tbody` children's rows in
    // tree order, then the `tfoot` children's.
    const auto section_rows = [this, is](node_id section) {
        std::vector<node_id> rows;
        const auto txn = doc_->read();
        for (const node_id child : txn.children(section)) {
            if (is(txn, child, "tr")) { rows.push_back(child); }
        }
        return rows;
    };
    const auto table_rows = [this, is](node_id table) {
        std::vector<node_id> rows;
        const auto txn = doc_->read();
        const auto add_rows_of = [&](node_id section) {
            for (const node_id row : txn.children(section)) {
                if (is(txn, row, "tr")) { rows.push_back(row); }
            }
        };
        for (const node_id child : txn.children(table)) {
            if (is(txn, child, "thead")) { add_rows_of(child); }
        }
        for (const node_id child : txn.children(table)) {
            if (is(txn, child, "tr")) { rows.push_back(child); }
            if (is(txn, child, "tbody")) { add_rows_of(child); }
        }
        for (const node_id child : txn.children(table)) {
            if (is(txn, child, "tfoot")) { add_rows_of(child); }
        }
        return rows;
    };
    const auto row_cells = [this, is](node_id row) {
        std::vector<node_id> cells;
        const auto txn = doc_->read();
        for (const node_id child : txn.children(row)) {
            if (is(txn, child, "td") || is(txn, child, "th")) { cells.push_back(child); }
        }
        return cells;
    };
    const auto make = [this](std::string_view name) {
        return doc_->create_element(atoms_->intern(name));
    };
    // WebIDL `long`: ToInt32 of whatever was passed, -1 when absent.
    const auto index_arg = [](context & c, std::span<value> a) {
        if (a.empty()) { return -1LL; }
        const long long unsigned_value = to_uint32(c.to_number_value(a[0]));
        return unsigned_value >= 2147483648LL ? unsigned_value - 4294967296LL : unsigned_value;
    };
    // "insert a row/cell at index" over a list: -1 or the length appends,
    // anything else goes before the index-th member; out of range is an
    // IndexSizeError, thrown here.
    const auto insert_at = [this, index_arg](context & c, std::span<value> a, node_id parent,
                                             const std::vector<node_id> & members,
                                             std::string_view where, node_id made) -> value {
        const long long index = index_arg(c, a);
        const auto count = static_cast<long long>(members.size());
        if (index < -1 || index > count) {
            throw_dom_exception(c, "IndexSizeError",
                                std::string{where} + ": index " + std::to_string(index) +
                                    " is out of range");
            return value::undefined();
        }
        const node_id before =
            index == -1 || index == count ? node_id{} : members[static_cast<std::size_t>(index)];
        (void)insert_node(parent, made, before);
        return wrap(c, made);
    };
    const auto delete_at = [this, index_arg](context & c, std::span<value> a,
                                             const std::vector<node_id> & members,
                                             std::string_view where) {
        const long long index = index_arg(c, a);
        const auto count = static_cast<long long>(members.size());
        if (index < -1 || index >= count) {
            throw_dom_exception(c, "IndexSizeError",
                                std::string{where} + ": index " + std::to_string(index) +
                                    " is out of range");
            return value::undefined();
        }
        if (index == -1 && count == 0) { return value::undefined(); }
        (void)doc_->remove_child(
            members[static_cast<std::size_t>(index == -1 ? count - 1 : index)]);
        mutated();
        return value::undefined();
    };
    const auto remove_first = [this, first_child](node_id table, std::string_view name) {
        if (const node_id had = first_child(table, name)) {
            (void)doc_->remove_child(had);
            mutated();
        }
    };
    // Where a `thead` goes: before the first child that is neither a caption
    // nor a colgroup, else at the end.
    const auto insert_head = [this, is](node_id table, node_id head) {
        node_id before;
        {
            const auto txn = doc_->read();
            for (const node_id child : txn.children(table)) {
                if (txn.tag(child).has_value() && !is(txn, child, "caption") &&
                    !is(txn, child, "colgroup")) {
                    before = child;
                    break;
                }
            }
        }
        (void)insert_node(table, head, before);
    };

    // --- HTMLTableElement ----------------------------------------------------
    const char * table = "HTMLTableElement";
    getter(table, "rows", [this, table_rows](context & c, std::span<value>) {
        const node_id self = receiver(c);
        return make_live_collection(c, [table_rows, self] { return table_rows(self); });
    });
    getter(table, "tBodies", [this, is](context & c, std::span<value>) {
        const node_id self = receiver(c);
        return make_live_collection(c, [this, is, self] {
            std::vector<node_id> bodies;
            const auto txn = doc_->read();
            for (const node_id child : txn.children(self)) {
                if (is(txn, child, "tbody")) { bodies.push_back(child); }
            }
            return bodies;
        });
    });
    // caption, tHead and tFoot: the first such child, or null. Setting one
    // removes the old and puts the new where the specification says - a
    // caption first, a thead before the body, a tfoot last - and a value of
    // the wrong kind is a HierarchyRequestError (a TypeError for the caption,
    // whose IDL type is not nullable-of-anything-else).
    const auto part = [&](const char * idl, const char * name, auto place) {
        getter(
            table, idl,
            [this, first_child, name](context & c, std::span<value>) {
                const node_id had = first_child(receiver(c), name);
                return had ? wrap(c, had) : value::null();
            },
            [this, remove_first, name, idl, place](context & c, std::span<value> a) {
                const node_id self = receiver(c);
                if (!self) { return value::undefined(); }
                const value given = arg(a, 0);
                const node_id made = handle_of(given);
                if (!given.is_nullish()) {
                    const auto txn = doc_->read();
                    if (!made || txn.element_ns(made) != node_ns::html ||
                        txn.local_name(made) != name) {
                        if (std::string_view{idl} == "caption") {
                            c.throw_error("TypeError", "Failed to set the 'caption' property on "
                                                       "'HTMLTableElement': the value is not of "
                                                       "type 'HTMLTableCaptionElement'.");
                        } else {
                            throw_dom_exception(c, "HierarchyRequestError",
                                                std::string{idl} + " must be a <" + name + ">");
                        }
                        return value::undefined();
                    }
                }
                remove_first(self, name);
                if (made) { place(self, made); }
                return value::undefined();
            });
    };
    part("caption", "caption", [this](node_id self, node_id made) {
        const node_id first = [&] {
            const auto txn = doc_->read();
            const std::span<const node_id> kids = txn.children(self);
            return kids.empty() ? node_id{} : kids.front();
        }();
        (void)insert_node(self, made, first);
    });
    part("tHead", "thead", insert_head);
    part("tFoot", "tfoot",
         [this](node_id self, node_id made) { (void)insert_node(self, made, node_id{}); });
    // createX answers the existing one or makes it; deleteX removes it.
    operation(table, "createCaption", 0, [this, first_child, make](context & c, std::span<value>) {
        const node_id self = receiver(c);
        if (!self) { return value::null(); }
        if (const node_id had = first_child(self, "caption")) { return wrap(c, had); }
        const node_id made = make("caption");
        node_id first;
        {
            const auto txn = doc_->read();
            const std::span<const node_id> kids = txn.children(self);
            first = kids.empty() ? node_id{} : kids.front();
        }
        (void)insert_node(self, made, first);
        return wrap(c, made);
    });
    operation(table, "deleteCaption", 0, [this, remove_first](context & c, std::span<value>) {
        if (const node_id self = receiver(c)) { remove_first(self, "caption"); }
        return value::undefined();
    });
    operation(table, "createTHead", 0,
              [this, first_child, make, insert_head](context & c, std::span<value>) {
                  const node_id self = receiver(c);
                  if (!self) { return value::null(); }
                  if (const node_id had = first_child(self, "thead")) { return wrap(c, had); }
                  const node_id made = make("thead");
                  insert_head(self, made);
                  return wrap(c, made);
              });
    operation(table, "deleteTHead", 0, [this, remove_first](context & c, std::span<value>) {
        if (const node_id self = receiver(c)) { remove_first(self, "thead"); }
        return value::undefined();
    });
    operation(table, "createTFoot", 0, [this, first_child, make](context & c, std::span<value>) {
        const node_id self = receiver(c);
        if (!self) { return value::null(); }
        if (const node_id had = first_child(self, "tfoot")) { return wrap(c, had); }
        const node_id made = make("tfoot");
        (void)insert_node(self, made, node_id{});
        return wrap(c, made);
    });
    operation(table, "deleteTFoot", 0, [this, remove_first](context & c, std::span<value>) {
        if (const node_id self = receiver(c)) { remove_first(self, "tfoot"); }
        return value::undefined();
    });
    // A new tbody goes after the last tbody child, else at the end.
    operation(table, "createTBody", 0, [this, is, make](context & c, std::span<value>) {
        const node_id self = receiver(c);
        if (!self) { return value::null(); }
        const node_id made = make("tbody");
        node_id before;
        {
            const auto txn = doc_->read();
            const std::span<const node_id> kids = txn.children(self);
            for (std::size_t i = kids.size(); i-- > 0;) {
                if (is(txn, kids[i], "tbody")) {
                    before = i + 1 < kids.size() ? kids[i + 1] : node_id{};
                    break;
                }
            }
        }
        (void)insert_node(self, made, before);
        return wrap(c, made);
    });
    // insertRow on the TABLE: with no rows and no tbody a tbody is made to
    // hold the row; with no rows the last tbody takes it; an append goes to
    // the parent of the last row; anything else goes before the index-th row.
    operation(table, "insertRow", 0,
              [this, is, table_rows, make, insert_at, index_arg](context & c, std::span<value> a) {
                  const node_id self = receiver(c);
                  if (!self) { return value::null(); }
                  const std::vector<node_id> rows = table_rows(self);
                  const long long index = index_arg(c, a);
                  if (index < -1 || index > static_cast<long long>(rows.size())) {
                      throw_dom_exception(c, "IndexSizeError",
                                          "insertRow: index " + std::to_string(index) +
                                              " is out of range");
                      return value::undefined();
                  }
                  if (rows.empty()) {
                      node_id last_body;
                      {
                          const auto txn = doc_->read();
                          for (const node_id child : txn.children(self)) {
                              if (is(txn, child, "tbody")) { last_body = child; }
                          }
                      }
                      if (!last_body) {
                          last_body = make("tbody");
                          (void)insert_node(self, last_body, node_id{});
                      }
                      const node_id row = make("tr");
                      (void)insert_node(last_body, row, node_id{});
                      return wrap(c, row);
                  }
                  const node_id parent = doc_->read().parent(
                      index == -1 || index == static_cast<long long>(rows.size())
                          ? rows.back()
                          : rows[static_cast<std::size_t>(index)]);
                  return insert_at(c, a, parent, rows, "insertRow", make("tr"));
              });
    operation(table, "deleteRow", 1,
              [this, table_rows, delete_at](context & c, std::span<value> a) {
                  const node_id self = receiver(c);
                  if (!self) { return value::undefined(); }
                  return delete_at(c, a, table_rows(self), "deleteRow");
              });

    // --- HTMLTableSectionElement ---------------------------------------------
    const char * section = "HTMLTableSectionElement";
    getter(section, "rows", [this, section_rows](context & c, std::span<value>) {
        const node_id self = receiver(c);
        return make_live_collection(c, [section_rows, self] { return section_rows(self); });
    });
    operation(section, "insertRow", 0,
              [this, section_rows, make, insert_at](context & c, std::span<value> a) {
                  const node_id self = receiver(c);
                  if (!self) { return value::null(); }
                  return insert_at(c, a, self, section_rows(self), "insertRow", make("tr"));
              });
    operation(section, "deleteRow", 1,
              [this, section_rows, delete_at](context & c, std::span<value> a) {
                  const node_id self = receiver(c);
                  if (!self) { return value::undefined(); }
                  return delete_at(c, a, section_rows(self), "deleteRow");
              });

    // --- HTMLTableRowElement -------------------------------------------------
    const char * row = "HTMLTableRowElement";
    getter(row, "cells", [this, row_cells](context & c, std::span<value>) {
        const node_id self = receiver(c);
        return make_live_collection(c, [row_cells, self] { return row_cells(self); });
    });
    // rowIndex: this row's place in the rows of the table it is in - as a
    // child of the table or of a section child of it - else -1.
    // sectionRowIndex: its place among its parent's rows, the parent being a
    // table or a section, else -1.
    const auto position = [](const std::vector<node_id> & among, node_id self) {
        for (std::size_t i = 0; i < among.size(); ++i) {
            if (among[i] == self) { return value::number(static_cast<double>(i)); }
        }
        return value::number(-1);
    };
    getter(row, "rowIndex", [this, is, table_rows, position](context & c, std::span<value>) {
        const node_id self = receiver(c);
        if (!self) { return value::number(-1); }
        node_id table_node;
        {
            const auto txn = doc_->read();
            node_id parent = txn.parent(self);
            if (parent && (is(txn, parent, "thead") || is(txn, parent, "tbody") ||
                           is(txn, parent, "tfoot"))) {
                parent = txn.parent(parent);
            }
            if (is(txn, parent, "table")) { table_node = parent; }
        }
        return table_node ? position(table_rows(table_node), self) : value::number(-1);
    });
    getter(row, "sectionRowIndex",
           [this, is, table_rows, section_rows, position](context & c, std::span<value>) {
               const node_id self = receiver(c);
               if (!self) { return value::number(-1); }
               node_id parent;
               bool parent_is_table = false;
               {
                   const auto txn = doc_->read();
                   const node_id up = txn.parent(self);
                   parent_is_table = is(txn, up, "table");
                   if (parent_is_table || is(txn, up, "thead") || is(txn, up, "tbody") ||
                       is(txn, up, "tfoot")) {
                       parent = up;
                   }
               }
               if (!parent) { return value::number(-1); }
               return position(parent_is_table ? table_rows(parent) : section_rows(parent), self);
           });
    operation(row, "insertCell", 0,
              [this, row_cells, make, insert_at](context & c, std::span<value> a) {
                  const node_id self = receiver(c);
                  if (!self) { return value::null(); }
                  return insert_at(c, a, self, row_cells(self), "insertCell", make("td"));
              });
    operation(row, "deleteCell", 1, [this, row_cells, delete_at](context & c, std::span<value> a) {
        const node_id self = receiver(c);
        if (!self) { return value::undefined(); }
        return delete_at(c, a, row_cells(self), "deleteCell");
    });

    // --- HTMLTableCellElement ------------------------------------------------
    getter("HTMLTableCellElement", "cellIndex",
           [this, is, row_cells, position](context & c, std::span<value>) {
               const node_id self = receiver(c);
               if (!self) { return value::number(-1); }
               node_id parent;
               {
                   const auto txn = doc_->read();
                   if (const node_id up = txn.parent(self); is(txn, up, "tr")) { parent = up; }
               }
               return parent ? position(row_cells(parent), self) : value::number(-1);
           });

    // =========================================================================
    // THE FORMS, HTML 4.10: the form owner, `form.elements`, the select and
    // option model, the labels, constraint validation, the selection API,
    // the entry list and FormData. Everything here reads the receiver's OWN
    // document - the prototypes are shared by every document of the realm -
    // and the per-element state that is not a content attribute (an option's
    // selectedness, a custom validity message, the selection direction) is a
    // hidden slot on the element's wrapper, which is what the collector traces.
    //
    // WHAT STOPS SHORT, and where: `value` and `checked` on input, select
    // and textarea are OWN accessors the wrapper installs (element/views.cpp),
    // which shadow anything a prototype could say - so the input type states'
    // value sanitisation and the select's value-to-selectedness rule are not
    // here. Submission stops where the network would begin: the entry list
    // is built, `formdata` and `submit` fire, and nothing navigates.
    // =========================================================================

    // The receiver as (owning bindings, node): a prototype's native is called
    // for any document's element.
    const auto at = [this](context & c) {
        dom_bindings * owner = owner_of(c.current_this());
        if (owner == nullptr) { owner = this; }
        return std::pair{owner, owner->receiver(c)};
    };
    const auto accessor = [&](const char * which, const char * name, script::native_fn get,
                              script::native_fn set = nullptr) {
        if (secondary_) { return; }
        auto * on = proto(which);
        if (on == nullptr) { return; }
        on->define_accessor(name, native(cx, name, std::move(get)),
                            set ? native(cx, name, std::move(set)) : value::undefined());
    };
    // A hidden slot on an element's wrapper.
    const auto slot_of = [](context & c, dom_bindings * b, node_id id, std::string_view key) {
        const value w = b->wrap(c, id);
        if (!w.is_object()) { return value::undefined(); }
        const value * held = static_cast<script::object_object *>(w.as_heap())->find(key);
        return held == nullptr ? value::undefined() : *held;
    };
    const auto set_slot = [](context & c, dom_bindings * b, node_id id, std::string_view key,
                             value v) {
        const value w = b->wrap(c, id);
        if (w.is_object()) {
            static_cast<script::object_object *>(w.as_heap())
                ->define(std::string{key}, v, script::attr_none);
        }
    };
    const auto erase_slot = [](context & c, dom_bindings * b, node_id id, std::string_view key) {
        const value w = b->wrap(c, id);
        if (w.is_object()) { (void)static_cast<script::object_object *>(w.as_heap())->erase(key); }
    };
    const auto attribute_of = [](const read_txn & txn, dom_bindings * b, node_id id,
                                 std::string_view name) {
        return std::string{txn.attribute_value(id, b->atoms_->intern(name))};
    };
    const auto has_attribute = [](const read_txn & txn, dom_bindings * b, node_id id,
                                  std::string_view name) {
        return txn.has_attribute(id, b->atoms_->intern(name));
    };
    // An input's type STATE, HTML 4.10.5: the attribute's keyword, else text.
    const auto input_type = [](const read_txn & txn, dom_bindings * b, node_id id) {
        std::string type = ascii_lower_copy(txn.attribute_value(id, b->atoms_->intern("type")));
        constexpr std::string_view known =
            "hidden text search tel url email password date month week time datetime-local "
            "number range color checkbox radio file submit image reset button";
        return lists_token(known, type) ? type : std::string{"text"};
    };
    // A form control's value as the store holds it.
    const auto value_of = [](const read_txn & txn, dom_bindings * b, node_id id) {
        return b->forms_->state_of(txn, *b->atoms_, id).value;
    };
    const auto walk_tree = [](const read_txn & txn, node_id from, auto && visit) {
        const auto walk = [&](auto && self, node_id at2) -> void {
            visit(at2);
            for (const node_id child : txn.children(at2)) { self(self, child); }
        };
        walk(walk, from);
    };
    // The tree a node is in, from its top.
    const auto tree_top = [](const read_txn & txn, dom_bindings * b, node_id id) {
        return b->root_of_tree(txn, id, false);
    };

    // --- the form owner, HTML 4.10.17.3 ---------------------------------------
    //
    // The `form` attribute names a form by id in the element's tree; without
    // one the nearest form ancestor owns the element. (The `form` IDL
    // attribute itself is installed by install_form_owner, reflection.cpp.)
    const auto form_owner = [is, attribute_of, tree_top](const read_txn & txn, dom_bindings * b,
                                                         node_id id) -> node_id {
        const std::string named = attribute_of(txn, b, id, "form");
        if (txn.has_attribute(id, b->atoms_->intern("form"))) {
            if (named.empty()) { return node_id{}; }
            node_id found;
            const auto walk = [&](auto && self, node_id at2) -> void {
                if (found) { return; }
                if (txn.attribute_value(at2, b->atoms_->intern("id")) == named) {
                    found = at2;
                    return;
                }
                for (const node_id child : txn.children(at2)) { self(self, child); }
            };
            walk(walk, tree_top(txn, b, id));
            return found && is(txn, found, "form") ? found : node_id{};
        }
        for (node_id up = txn.parent(id); up; up = txn.parent(up)) {
            if (is(txn, up, "form")) { return up; }
        }
        return node_id{};
    };
    // The categories, HTML 4.10.2.
    const auto is_listed = [input_type](const read_txn & txn, dom_bindings * b, node_id id) {
        if (txn.element_ns(id) != node_ns::html) { return false; }
        const std::string_view local = txn.local_name(id);
        if (local == "input") { return input_type(txn, b, id) != "image"; }
        return local == "button" || local == "fieldset" || local == "object" || local == "output" ||
               local == "select" || local == "textarea";
    };
    const auto is_submittable = [](const read_txn & txn, node_id id) {
        if (txn.element_ns(id) != node_ns::html) { return false; }
        const std::string_view local = txn.local_name(id);
        return local == "button" || local == "input" || local == "object" || local == "select" ||
               local == "textarea";
    };
    const auto is_labelable = [input_type](const read_txn & txn, dom_bindings * b, node_id id) {
        if (!id || txn.element_ns(id) != node_ns::html) { return false; }
        const std::string_view local = txn.local_name(id);
        if (local == "input") { return input_type(txn, b, id) != "hidden"; }
        return local == "button" || local == "meter" || local == "output" || local == "progress" ||
               local == "select" || local == "textarea";
    };
    // The listed elements whose form owner is `form`, in tree order, over the
    // form's tree (a control with `form=` may be outside the form).
    const auto elements_of_form = [is_listed, form_owner, tree_top, walk_tree](dom_bindings * b,
                                                                               node_id form) {
        std::vector<node_id> out;
        const auto txn = b->doc_->read();
        if (!txn.contains(form)) { return out; }
        walk_tree(txn, tree_top(txn, b, form), [&](node_id at2) {
            if (is_listed(txn, b, at2) && form_owner(txn, b, at2) == form) { out.push_back(at2); }
        });
        return out;
    };
    // "Disabled", HTML 4.10.18.5: its own attribute, or a disabled fieldset
    // ancestor it is not inside the first legend of.
    const auto is_disabled = [is](const read_txn & txn, dom_bindings * b, node_id id) {
        const atom disabled = b->atoms_->intern("disabled");
        if (txn.element_ns(id) != node_ns::html) { return false; }
        const std::string_view local = txn.local_name(id);
        if (local != "button" && local != "input" && local != "select" && local != "textarea" &&
            local != "fieldset" && local != "optgroup" && local != "option") {
            return false;
        }
        if (txn.has_attribute(id, disabled)) { return true; }
        node_id child = id;
        for (node_id up = txn.parent(id); up; child = up, up = txn.parent(up)) {
            if (!is(txn, up, "fieldset") || !txn.has_attribute(up, disabled)) { continue; }
            // Inside the fieldset's FIRST legend child: not disabled by it.
            node_id first_legend;
            for (const node_id kid : txn.children(up)) {
                if (is(txn, kid, "legend")) {
                    first_legend = kid;
                    break;
                }
            }
            if (child != first_legend) { return true; }
        }
        return false;
    };
    // An event fired at a node with the flags given; whether it was cancelled.
    const auto fire = [](context & c, dom_bindings * b, node_id target, std::string_view type,
                         bool bubbles, bool cancelable, auto && decorate) {
        const value event = b->make_event_object(c, type, bubbles, cancelable);
        auto * object = static_cast<script::object_object *>(event.as_heap());
        object->set("__isTrusted", value::boolean(true));
        object->set("__initialised", value::boolean(true));
        object->set("target", b->wrap(c, target));
        object->set("srcElement", b->wrap(c, target));
        decorate(*object);
        return b->dispatch_event(type, target, event);
    };
    const auto plain = [](script::object_object &) {};

    // --- the select and option model, HTML 4.10.7 and 4.10.10 -----------------
    //
    // An option's SELECTEDNESS is the `__selected` slot on its wrapper when
    // a script or the select has set it, else its `selected` attribute; its
    // DIRTINESS is `__selectedDirty`. The select's list of options is its
    // option descendants (customizable select), skipping a nested select's.
    // The engine paints and submits the select from its store value, so every
    // change of selectedness here is written through to that value - and a
    // store value the chrome or the shadowing `value` setter changed is read
    // back into selectedness when the two disagree (`reconcile`).
    const auto options_of = [is](const read_txn & txn, dom_bindings * b, node_id select) {
        std::vector<node_id> out;
        const auto walk = [&](auto && self, node_id at2) -> void {
            for (const node_id child : txn.children(at2)) {
                if (is(txn, child, "option")) { out.push_back(child); }
                if (is(txn, child, "select") || is(txn, child, "datalist")) { continue; }
                self(self, child);
            }
        };
        (void)b;
        walk(walk, select);
        return out;
    };
    const auto option_value = [](const read_txn & txn, dom_bindings * b, node_id option) {
        return form_store::option_value(txn, *b->atoms_, option);
    };
    const auto option_selected = [slot_of](context & c, const read_txn & txn, dom_bindings * b,
                                           node_id option) {
        const value held = slot_of(c, b, option, selected_slot);
        if (held.is_boolean()) { return context::truthy(held); }
        return txn.has_attribute(option, b->atoms_->intern("selected"));
    };
    const auto select_of = [is](const read_txn & txn, node_id option) {
        for (node_id up = txn.parent(option); up; up = txn.parent(up)) {
            if (is(txn, up, "select")) { return up; }
        }
        return node_id{};
    };
    const auto is_multiple = [](const read_txn & txn, dom_bindings * b, node_id select) {
        return txn.has_attribute(select, b->atoms_->intern("multiple"));
    };
    // The display size: the `size` attribute, else 4 for multiple and 1
    // otherwise.
    const auto display_size = [is_multiple](const read_txn & txn, dom_bindings * b,
                                            node_id select) {
        const std::string_view text = txn.attribute_value(select, b->atoms_->intern("size"));
        long long parsed = 0;
        if (parse_html_integer(text, parsed) && parsed > 0) { return parsed; }
        return is_multiple(txn, b, select) ? 4LL : 1LL;
    };
    // Every option's selectedness, with the store reconciled - see above.
    // `__syncedValue` on the select is the store value this model last
    // wrote; a store value that differs from it was written by the chrome or
    // the shadowing `value` setter since, and is read back into the slots.
    const auto selected_flags = [options_of, option_selected, option_value, is_multiple, slot_of,
                                 set_slot, display_size,
                                 is_disabled](context & c, const read_txn & txn, dom_bindings * b,
                                              node_id select, std::vector<node_id> & options) {
        options = options_of(txn, b, select);
        std::vector<bool> flags;
        flags.reserve(options.size());
        bool any_slot = false;
        for (const node_id option : options) {
            flags.push_back(option_selected(c, txn, b, option));
            if (slot_of(c, b, option, selected_slot).is_boolean()) { any_slot = true; }
        }
        if (is_multiple(txn, b, select)) { return flags; }
        // A select nothing has touched: the parser's insertions ran "ask for
        // a reset" - the last of several selected wins, and a display-size-1
        // select with none selects its first enabled option.
        if (!any_slot) {
            std::size_t count = 0;
            std::size_t last = options.size();
            for (std::size_t i = 0; i < options.size(); ++i) {
                if (flags[i]) {
                    ++count;
                    last = i;
                }
            }
            if (count > 1) {
                for (std::size_t i = 0; i < options.size(); ++i) { flags[i] = i == last; }
            } else if (count == 0 && display_size(txn, b, select) == 1) {
                for (std::size_t i = 0; i < options.size(); ++i) {
                    if (!is_disabled(txn, b, options[i])) {
                        flags[i] = true;
                        break;
                    }
                }
            }
        }
        const control_state * held = b->forms_->find(select);
        if (held == nullptr) { return flags; }
        const value synced = slot_of(c, b, select, "__syncedValue");
        const std::string reference =
            synced.is_string() ? c.to_string(synced)
                               : form_store::selected_option_value(txn, *b->atoms_, select);
        if (held->value == reference) { return flags; }
        set_slot(c, b, select, "__syncedValue", c.string(held->value));
        std::size_t match = options.size();
        for (std::size_t i = 0; i < options.size(); ++i) {
            if (option_value(txn, b, options[i]) == held->value) {
                match = i;
                break;
            }
        }
        for (std::size_t j = 0; j < options.size(); ++j) {
            flags[j] = j == match;
            set_slot(c, b, options[j], selected_slot, value::boolean(j == match));
        }
        return flags;
    };
    // Write the selected option's value to the store, so the paint and the
    // submission agree with the model.
    const auto sync_store = [option_value, set_slot](context & c, const read_txn & txn,
                                                     dom_bindings * b, node_id select,
                                                     const std::vector<node_id> & options,
                                                     const std::vector<bool> & flags) {
        std::string first;
        for (std::size_t i = 0; i < options.size(); ++i) {
            if (flags[i]) {
                first = option_value(txn, b, options[i]);
                break;
            }
        }
        control_state & held = b->forms_->state_of(txn, *b->atoms_, select);
        held.value = first;
        set_slot(c, b, select, "__syncedValue", c.string(first));
        b->wrote_to_control_ = true;
    };
    // "Ask for a reset", HTML 4.10.7: a single select with several selected
    // keeps the last; a display-size-1 single select with none selects the
    // first option that is not disabled.
    const auto ask_for_reset = [is_multiple, display_size, selected_flags, set_slot, sync_store,
                                is_disabled](context & c, dom_bindings * b, node_id select) {
        const auto txn = b->doc_->read();
        if (!txn.contains(select)) { return; }
        std::vector<node_id> options;
        std::vector<bool> flags = selected_flags(c, txn, b, select, options);
        if (is_multiple(txn, b, select)) {
            sync_store(c, txn, b, select, options, flags);
            return;
        }
        std::size_t count = 0;
        std::size_t last = options.size();
        for (std::size_t i = 0; i < options.size(); ++i) {
            if (flags[i]) {
                ++count;
                last = i;
            }
        }
        if (count > 1) {
            for (std::size_t i = 0; i < options.size(); ++i) {
                flags[i] = i == last;
                set_slot(c, b, options[i], selected_slot, value::boolean(flags[i]));
            }
        } else if (count == 0 && display_size(txn, b, select) == 1) {
            for (std::size_t i = 0; i < options.size(); ++i) {
                if (!is_disabled(txn, b, options[i])) {
                    flags[i] = true;
                    set_slot(c, b, options[i], selected_slot, value::boolean(true));
                    break;
                }
            }
        }
        sync_store(c, txn, b, select, options, flags);
    };
    const auto set_selected = [set_slot, options_of, option_selected, option_value, is_multiple,
                               select_of, ask_for_reset](context & c, dom_bindings * b,
                                                         node_id option, bool on, bool dirty) {
        set_slot(c, b, option, selected_slot, value::boolean(on));
        if (dirty) { set_slot(c, b, option, dirty_slot, value::boolean(true)); }
        const node_id select = [&] {
            const auto txn = b->doc_->read();
            return select_of(txn, option);
        }();
        if (!select) { return; }
        {
            const auto txn = b->doc_->read();
            if (on && !is_multiple(txn, b, select)) {
                // Every other option off - through the slots, not the flags,
                // so the store is not consulted against a half-made state.
                for (const node_id other : options_of(txn, b, select)) {
                    if (other != option && option_selected(c, txn, b, other)) {
                        set_slot(c, b, other, selected_slot, value::boolean(false));
                    }
                }
                control_state & held = b->forms_->state_of(txn, *b->atoms_, select);
                held.value = option_value(txn, b, option);
                set_slot(c, b, select, "__syncedValue", c.string(held.value));
            }
        }
        ask_for_reset(c, b, select);
        b->mutated();
    };
    const auto selected_index = [selected_flags](context & c, dom_bindings * b, node_id select) {
        const auto txn = b->doc_->read();
        std::vector<node_id> options;
        const std::vector<bool> flags = selected_flags(c, txn, b, select, options);
        for (std::size_t i = 0; i < flags.size(); ++i) {
            if (flags[i]) { return static_cast<long long>(i); }
        }
        return -1LL;
    };
    const auto set_selected_index = [selected_flags, set_slot,
                                     sync_store](context & c, dom_bindings * b, node_id select,
                                                 long long index) {
        const auto txn = b->doc_->read();
        std::vector<node_id> options;
        std::vector<bool> flags = selected_flags(c, txn, b, select, options);
        for (std::size_t i = 0; i < options.size(); ++i) {
            flags[i] = static_cast<long long>(i) == index;
            set_slot(c, b, options[i], selected_slot, value::boolean(flags[i]));
            if (flags[i]) { set_slot(c, b, options[i], dirty_slot, value::boolean(true)); }
        }
        sync_store(c, txn, b, select, options, flags);
        b->mutated();
    };

    // `select.add(element, before)` and HTMLOptionsCollection.add: an option
    // or optgroup, before an element or an index, or at the end.
    const auto select_add = [this, is, ask_for_reset](context & c, dom_bindings * b, node_id select,
                                                      std::span<value> a) {
        const node_id element = b->handle_of(arg(a, 0));
        {
            const auto txn = b->doc_->read();
            if (!element || !(is(txn, element, "option") || is(txn, element, "optgroup"))) {
                c.throw_error("TypeError", "add: the element is not an option or optgroup");
                return;
            }
            for (node_id up = select; up; up = txn.parent(up)) {
                if (up == element) {
                    throw_dom_exception(c, "HierarchyRequestError",
                                        "add: the element is an ancestor of the select");
                    return;
                }
            }
        }
        const value before_value = arg(a, 1);
        node_id parent = select;
        node_id before;
        if (before_value.is_object_like()) {
            before = b->handle_of(before_value);
            const auto txn = b->doc_->read();
            bool inside = false;
            for (node_id up = before; up; up = txn.parent(up)) {
                if (up == select) { inside = true; }
            }
            if (!before || !inside) {
                throw_dom_exception(c, "NotFoundError", "add: `before` is not in the select");
                return;
            }
            parent = txn.parent(before);
        } else if (!before_value.is_nullish()) {
            const long long index = static_cast<long long>(context::to_int32(before_value));
            const auto txn = b->doc_->read();
            std::vector<node_id> options;
            const auto walk = [&](auto && self, node_id at2) -> void {
                for (const node_id child : txn.children(at2)) {
                    if (is(txn, child, "option")) { options.push_back(child); }
                    if (is(txn, child, "select")) { continue; }
                    self(self, child);
                }
            };
            walk(walk, select);
            if (index >= 0 && static_cast<std::size_t>(index) < options.size()) {
                before = options[static_cast<std::size_t>(index)];
                parent = txn.parent(before);
            }
        }
        if (element == before) { return; }
        (void)b->insert_node(parent, element, before);
        // The option insertion steps run the selectedness setting algorithm:
        // a selected option arriving beside a selected one keeps the LAST.
        ask_for_reset(c, b, select);
    };

    // --- `Option`, the legacy factory --------------------------------------------
    if (!secondary_) {
        auto * option_ctor = cx.allocate<script::native_object>(
            "Option", [this, set_slot](context & c, std::span<value> a) -> value {
                const value made = create_html_element(c, "option");
                const node_id id = handle_of(made);
                if (!id) { return made; }
                if (const value text = arg(a, 0); !text.is_undefined()) {
                    const std::string data = c.to_string(text);
                    if (!data.empty()) {
                        (void)insert_node(id, doc_->create_text(data), node_id{});
                    }
                }
                if (const value given = arg(a, 1); !given.is_undefined()) {
                    (void)doc_->set_attribute(id, atoms_->intern("value"), c.to_string(given));
                }
                if (context::truthy(arg(a, 2))) {
                    (void)doc_->set_attribute(id, atoms_->intern("selected"), "");
                }
                if (a.size() > 3) {
                    set_slot(c, this, id, selected_slot, value::boolean(context::truthy(a[3])));
                    set_slot(c, this, id, dirty_slot, value::boolean(true));
                }
                mutated();
                return made;
            });
        if (const value proto_value = interface_prototype("HTMLOptionElement");
            proto_value.is_object()) {
            option_ctor->define("prototype", proto_value, script::attr_none);
        }
        option_ctor->define("length", value::number(0), script::attr_configurable);
        cx.define_global("Option", value::object(option_ctor));
    }

    // --- HTMLOptionElement --------------------------------------------------------
    accessor(
        "HTMLOptionElement", "selected",
        [at, option_selected](context & c, std::span<value>) {
            const auto where = at(c);
            dom_bindings * b = where.first;
            const node_id id = where.second;
            if (!id) { return value::boolean(false); }
            const auto txn = b->doc_->read();
            return value::boolean(option_selected(c, txn, b, id));
        },
        [at, set_selected](context & c, std::span<value> a) {
            const auto where = at(c);
            dom_bindings * b = where.first;
            const node_id id = where.second;
            if (id) { set_selected(c, b, id, context::truthy(arg(a, 0)), true); }
            return value::undefined();
        });
    accessor("HTMLOptionElement", "index",
             [at, select_of, options_of](context & c, std::span<value>) {
                 const auto where = at(c);
                 dom_bindings * b = where.first;
                 const node_id id = where.second;
                 if (!id) { return value::number(0); }
                 const auto txn = b->doc_->read();
                 const node_id select = select_of(txn, id);
                 if (!select) { return value::number(0); }
                 const std::vector<node_id> options = options_of(txn, b, select);
                 const auto here = std::ranges::find(options, id);
                 return value::number(
                     here == options.end() ? 0 : static_cast<double>(here - options.begin()));
             });
    accessor("HTMLOptionElement", "form",
             [at, select_of, form_owner](context & c, std::span<value>) {
                 const auto where = at(c);
                 dom_bindings * b = where.first;
                 const node_id id = where.second;
                 if (!id) { return value::null(); }
                 node_id form;
                 {
                     const auto txn = b->doc_->read();
                     if (const node_id select = select_of(txn, id)) {
                         form = form_owner(txn, b, select);
                     }
                 }
                 return form ? b->wrap(c, form) : value::null();
             });
    // `text`: the descendant text with any <script> and <svg:script> left
    // out, stripped and collapsed; set replaces the children with one Text.
    accessor(
        "HTMLOptionElement", "text",
        [at](context & c, std::span<value>) {
            const auto where = at(c);
            dom_bindings * b = where.first;
            const node_id id = where.second;
            if (!id) { return c.string(""); }
            std::string text;
            {
                const auto txn = b->doc_->read();
                const auto walk = [&](auto && self, node_id at2) -> void {
                    if (txn.local_name(at2) == "script" && (txn.element_ns(at2) == node_ns::html ||
                                                            txn.element_ns(at2) == node_ns::svg)) {
                        return;
                    }
                    if (txn.kind(at2).value_or(node_kind::element) == node_kind::text ||
                        txn.kind(at2).value_or(node_kind::element) == node_kind::cdata_section) {
                        text += txn.text(at2);
                    }
                    for (const node_id child : txn.children(at2)) { self(self, child); }
                };
                walk(walk, id);
            }
            return c.string(collapse_whitespace(text, html_whitespace));
        },
        [at](context & c, std::span<value> a) {
            const auto where = at(c);
            dom_bindings * b = where.first;
            const node_id id = where.second;
            if (id) { b->set_text(id, arg_string(c, a, 0)); }
            return value::undefined();
        });

    // --- HTMLSelectElement --------------------------------------------------------
    accessor("HTMLSelectElement", "type", [at, is_multiple](context & c, std::span<value>) {
        const auto where = at(c);
        dom_bindings * b = where.first;
        const node_id id = where.second;
        if (!id) { return c.string("select-one"); }
        const auto txn = b->doc_->read();
        return c.string(is_multiple(txn, b, id) ? "select-multiple" : "select-one");
    });
    accessor(
        "HTMLSelectElement", "selectedIndex",
        [at, selected_index](context & c, std::span<value>) {
            const auto where = at(c);
            dom_bindings * b = where.first;
            const node_id id = where.second;
            return value::number(id ? static_cast<double>(selected_index(c, b, id)) : -1);
        },
        [at, set_selected_index](context & c, std::span<value> a) {
            const auto where = at(c);
            dom_bindings * b = where.first;
            const node_id id = where.second;
            if (id) { set_selected_index(c, b, id, context::to_int32(arg(a, 0))); }
            return value::undefined();
        });
    // `options`, one HTMLOptionsCollection per select, live over its options,
    // with `selectedIndex`, `add`, `remove` and a settable `length` of its own.
    const auto options_collection = [slot_of, set_slot, options_of](context & c, dom_bindings * b,
                                                                    node_id select) {
        const value held = slot_of(c, b, select, "__options");
        if (held.is_object_like()) { return held; }
        const value list = b->make_live_collection(
            c,
            [b, options_of, select] {
                const auto txn = b->doc_->read();
                return txn.contains(select) ? options_of(txn, b, select) : std::vector<node_id>{};
            },
            "HTMLOptionsCollection");
        c.store_property(list, "@@sym:ctbrowser:options-owner", b->wrap(c, select));
        set_slot(c, b, select, "__options", list);
        return list;
    };
    accessor("HTMLSelectElement", "options",
             [at, options_collection](context & c, std::span<value>) {
                 const auto where = at(c);
                 dom_bindings * b = where.first;
                 const node_id id = where.second;
                 return id ? options_collection(c, b, id) : value::null();
             });
    accessor("HTMLSelectElement", "selectedOptions",
             [at, selected_flags](context & c, std::span<value>) {
                 const auto where = at(c);
                 dom_bindings * b = where.first;
                 const node_id id = where.second;
                 if (!id) { return value::null(); }
                 return b->make_live_collection(c, [b, id, selected_flags] {
                     const auto txn = b->doc_->read();
                     std::vector<node_id> options;
                     std::vector<node_id> out;
                     if (!txn.contains(id) || b->cx_ == nullptr) { return out; }
                     const std::vector<bool> flags = selected_flags(*b->cx_, txn, b, id, options);
                     for (std::size_t i = 0; i < options.size(); ++i) {
                         if (flags[i]) { out.push_back(options[i]); }
                     }
                     return out;
                 });
             });
    // `length`: the number of options; setting it appends blank options or
    // removes the surplus from the end.
    const auto set_option_count = [options_of](context & c, dom_bindings * b, node_id select,
                                               double wanted) {
        if (!(wanted >= 0)) { return; }
        std::vector<node_id> options;
        {
            const auto txn = b->doc_->read();
            options = options_of(txn, b, select);
        }
        const auto count = static_cast<double>(options.size());
        if (wanted > count) {
            if (wanted - count > 100000) { return; }
            for (double i = count; i < wanted; ++i) {
                (void)b->insert_node(select, b->doc_->create_element(b->atoms_->intern("option")),
                                     node_id{});
            }
        } else {
            for (std::size_t i = static_cast<std::size_t>(wanted); i < options.size(); ++i) {
                (void)b->doc_->remove_child(options[i]);
            }
            b->mutated();
        }
        (void)c;
    };
    accessor(
        "HTMLSelectElement", "length",
        [at, options_of](context & c, std::span<value>) {
            const auto where = at(c);
            dom_bindings * b = where.first;
            const node_id id = where.second;
            if (!id) { return value::number(0); }
            const auto txn = b->doc_->read();
            return value::number(static_cast<double>(options_of(txn, b, id).size()));
        },
        [at, set_option_count](context & c, std::span<value> a) {
            const auto where = at(c);
            dom_bindings * b = where.first;
            const node_id id = where.second;
            if (id) {
                set_option_count(c, b, id, static_cast<double>(context::to_uint32(arg(a, 0))));
            }
            return value::undefined();
        });
    operation("HTMLSelectElement", "item", 1, [this, options_of](context & c, std::span<value> a) {
        const node_id id = receiver(c);
        if (!id) { return value::null(); }
        std::vector<node_id> options;
        {
            const auto txn = doc_->read();
            options = options_of(txn, this, id);
        }
        const double index = static_cast<double>(context::to_uint32(arg(a, 0)));
        if (index >= static_cast<double>(options.size())) { return value::null(); }
        return wrap(c, options[static_cast<std::size_t>(index)]);
    });
    operation("HTMLSelectElement", "namedItem", 1,
              [this, options_of](context & c, std::span<value> a) {
                  const node_id id = receiver(c);
                  if (!id) { return value::null(); }
                  const std::string want = arg_string(c, a, 0);
                  node_id found;
                  {
                      const auto txn = doc_->read();
                      const atom id_attr = atoms_->intern("id");
                      const atom name_attr = atoms_->intern("name");
                      for (const node_id option : options_of(txn, this, id)) {
                          if (!want.empty() && (txn.attribute_value(option, id_attr) == want ||
                                                txn.attribute_value(option, name_attr) == want)) {
                              found = option;
                              break;
                          }
                      }
                  }
                  return found ? wrap(c, found) : value::null();
              });
    operation("HTMLSelectElement", "add", 1, [this, select_add](context & c, std::span<value> a) {
        if (const node_id id = receiver(c)) { select_add(c, this, id, a); }
        return value::undefined();
    });
    // `remove()` with no argument is ChildNode's; with an index it removes
    // that option.
    operation("HTMLSelectElement", "remove", 0,
              [this, options_of, ask_for_reset](context & c, std::span<value> a) {
                  const node_id id = receiver(c);
                  if (!id) { return value::undefined(); }
                  if (a.empty()) {
                      if (id == doc_->root() && secondary_) {
                          doc_->remove_document_element();
                      } else {
                          (void)doc_->remove_child(id);
                      }
                      mutated();
                      return value::undefined();
                  }
                  const long long index = context::to_int32(a[0]);
                  std::vector<node_id> options;
                  {
                      const auto txn = doc_->read();
                      options = options_of(txn, this, id);
                  }
                  if (index >= 0 && static_cast<std::size_t>(index) < options.size()) {
                      (void)doc_->remove_child(options[static_cast<std::size_t>(index)]);
                      ask_for_reset(c, this, id); // the removal steps run it too
                      mutated();
                  }
                  return value::undefined();
              });

    // --- HTMLOptionsCollection's own members ----------------------------------------
    if (!secondary_) {
        if (auto * on = proto("HTMLOptionsCollection")) {
            const auto owner_select = [this](context & c) -> std::pair<dom_bindings *, node_id> {
                const value select =
                    c.lookup_property(c.current_this(), "@@sym:ctbrowser:options-owner");
                dom_bindings * b = owner_of(select);
                if (b == nullptr) { return {nullptr, node_id{}}; }
                return {b, b->handle_of(select)};
            };
            on->define_accessor(
                "selectedIndex",
                native(cx, "selectedIndex",
                       [owner_select, selected_index](context & c, std::span<value>) {
                           const auto where = owner_select(c);
                           dom_bindings * b = where.first;
                           const node_id id = where.second;
                           return value::number(id ? static_cast<double>(selected_index(c, b, id))
                                                   : -1);
                       }),
                native(cx, "selectedIndex",
                       [owner_select, set_selected_index](context & c, std::span<value> a) {
                           const auto where = owner_select(c);
                           dom_bindings * b = where.first;
                           const node_id id = where.second;
                           if (id) { set_selected_index(c, b, id, context::to_int32(arg(a, 0))); }
                           return value::undefined();
                       }));
            // `length` is the HTMLCollection's getter and this setter.
            on->define_accessor(
                "length",
                native(cx, "length",
                       [owner_select, options_of](context & c, std::span<value>) {
                           const auto where = owner_select(c);
                           dom_bindings * b = where.first;
                           const node_id id = where.second;
                           if (!id) { return value::number(0); }
                           const auto txn = b->doc_->read();
                           return value::number(static_cast<double>(options_of(txn, b, id).size()));
                       }),
                native(cx, "length",
                       [owner_select, set_option_count](context & c, std::span<value> a) {
                           const auto where = owner_select(c);
                           dom_bindings * b = where.first;
                           const node_id id = where.second;
                           if (id) {
                               set_option_count(c, b, id,
                                                static_cast<double>(context::to_uint32(arg(a, 0))));
                           }
                           return value::undefined();
                       }));
            set_method(
                cx, *on, "add",
                [owner_select, select_add](context & c, std::span<value> a) {
                    const auto where = owner_select(c);
                    dom_bindings * b = where.first;
                    const node_id id = where.second;
                    if (id) { select_add(c, b, id, a); }
                    return value::undefined();
                },
                script::attr_builtin);
            set_method(
                cx, *on, "remove",
                [owner_select, options_of, ask_for_reset](context & c, std::span<value> a) {
                    const auto where = owner_select(c);
                    dom_bindings * b = where.first;
                    const node_id id = where.second;
                    if (!id) { return value::undefined(); }
                    const long long index = context::to_int32(arg(a, 0));
                    std::vector<node_id> options;
                    {
                        const auto txn = b->doc_->read();
                        options = options_of(txn, b, id);
                    }
                    if (index >= 0 && static_cast<std::size_t>(index) < options.size()) {
                        (void)b->doc_->remove_child(options[static_cast<std::size_t>(index)]);
                        ask_for_reset(c, b, id);
                        b->mutated();
                    }
                    return value::undefined();
                },
                script::attr_builtin);
        }
    }

    // --- RadioNodeList.value, HTML 4.10.20.1 --------------------------------------------
    //
    // The value of the first checked radio in the list ("on" without a value
    // attribute), and setting it checks the first radio whose value matches.
    if (!secondary_) {
        if (auto * on = proto("RadioNodeList")) {
            const auto radios = [this, is, input_type](context & c) {
                std::vector<std::pair<dom_bindings *, node_id>> out;
                const value self = c.current_this();
                const double length = context::to_number(c.lookup_property(self, "length"));
                for (double i = 0; i < length; ++i) {
                    const value item =
                        c.lookup_property(self, std::to_string(static_cast<long long>(i)));
                    dom_bindings * b = owner_of(item);
                    const node_id id = b == nullptr ? node_id{} : b->handle_of(item);
                    if (!id) { continue; }
                    const auto txn = b->doc_->read();
                    if (is(txn, id, "input") && input_type(txn, b, id) == "radio") {
                        out.emplace_back(b, id);
                    }
                }
                return out;
            };
            on->define_accessor(
                "value",
                native(cx, "value",
                       [radios, has_attribute, value_of](context & c, std::span<value>) {
                           for (const auto & radio : radios(c)) {
                               dom_bindings * b = radio.first;
                               const node_id id = radio.second;
                               const auto txn = b->doc_->read();
                               if (!b->forms_->state_of(txn, *b->atoms_, id).checked) { continue; }
                               return c.string(has_attribute(txn, b, id, "value")
                                                   ? value_of(txn, b, id)
                                                   : std::string{"on"});
                           }
                           return c.string("");
                       }),
                native(cx, "value",
                       [radios, has_attribute, value_of](context & c, std::span<value> a) {
                           const std::string want = arg_string(c, a, 0);
                           for (const auto & radio : radios(c)) {
                               dom_bindings * b = radio.first;
                               const node_id id = radio.second;
                               const auto txn = b->doc_->read();
                               const std::string held = has_attribute(txn, b, id, "value")
                                                            ? value_of(txn, b, id)
                                                            : std::string{"on"};
                               if (held != want) { continue; }
                               b->forms_->toggle(txn, *b->atoms_, id, control_kind::radio);
                               b->wrote_to_control_ = true;
                               b->mutated();
                               break;
                           }
                           return value::undefined();
                       }));
        }
    }

    // --- HTMLDataListElement.options -------------------------------------------------
    accessor("HTMLDataListElement", "options", [at, is](context & c, std::span<value>) {
        const auto where = at(c);
        dom_bindings * b = where.first;
        const node_id id = where.second;
        if (!id) { return value::null(); }
        return b->make_live_collection(c, [b, id, is] {
            std::vector<node_id> out;
            const auto txn = b->doc_->read();
            if (!txn.contains(id)) { return out; }
            const auto walk = [&](auto && self, node_id at2) -> void {
                for (const node_id child : txn.children(at2)) {
                    if (is(txn, child, "option")) { out.push_back(child); }
                    self(self, child);
                }
            };
            walk(walk, id);
            return out;
        });
    });

    // --- labels, HTML 4.10.4 -----------------------------------------------------------
    //
    // A label's labeled control: the element its `for` names when that is
    // labelable, else its first labelable descendant. `labels` on a control
    // is every label in its tree whose control it is.
    const auto labeled_control = [is_labelable, walk_tree, tree_top](const read_txn & txn,
                                                                     dom_bindings * b,
                                                                     node_id label) -> node_id {
        const atom for_attr = b->atoms_->intern("for");
        if (txn.has_attribute(label, for_attr)) {
            const std::string_view want = txn.attribute_value(label, for_attr);
            if (want.empty()) { return node_id{}; }
            node_id found;
            const atom id_attr = b->atoms_->intern("id");
            walk_tree(txn, tree_top(txn, b, label), [&](node_id at2) {
                if (!found && txn.attribute_value(at2, id_attr) == want) { found = at2; }
            });
            return is_labelable(txn, b, found) ? found : node_id{};
        }
        node_id found;
        const auto walk = [&](auto && self, node_id at2) -> void {
            for (const node_id child : txn.children(at2)) {
                if (found) { return; }
                if (is_labelable(txn, b, child)) {
                    found = child;
                    return;
                }
                self(self, child);
            }
        };
        walk(walk, label);
        return found;
    };
    accessor("HTMLLabelElement", "control", [at, labeled_control](context & c, std::span<value>) {
        const auto where = at(c);
        dom_bindings * b = where.first;
        const node_id id = where.second;
        if (!id) { return value::null(); }
        node_id control;
        {
            const auto txn = b->doc_->read();
            control = labeled_control(txn, b, id);
        }
        return control ? b->wrap(c, control) : value::null();
    });
    accessor("HTMLLabelElement", "form",
             [at, labeled_control, form_owner](context & c, std::span<value>) {
                 const auto where = at(c);
                 dom_bindings * b = where.first;
                 const node_id id = where.second;
                 if (!id) { return value::null(); }
                 node_id form;
                 {
                     const auto txn = b->doc_->read();
                     if (const node_id control = labeled_control(txn, b, id)) {
                         form = form_owner(txn, b, control);
                     }
                 }
                 return form ? b->wrap(c, form) : value::null();
             });
    for (const char * which :
         {"HTMLButtonElement", "HTMLInputElement", "HTMLMeterElement", "HTMLOutputElement",
          "HTMLProgressElement", "HTMLSelectElement", "HTMLTextAreaElement"}) {
        accessor(which, "labels",
                 [at, is_labelable, labeled_control, is, walk_tree, tree_top](context & c,
                                                                              std::span<value>) {
                     const auto where = at(c);
                     dom_bindings * b = where.first;
                     const node_id id = where.second;
                     if (!id) { return value::null(); }
                     {
                         const auto txn = b->doc_->read();
                         if (!is_labelable(txn, b, id)) { return value::null(); }
                     }
                     return b->make_live_collection(
                         c,
                         [b, id, labeled_control, is, walk_tree, tree_top] {
                             std::vector<node_id> out;
                             const auto txn = b->doc_->read();
                             if (!txn.contains(id)) { return out; }
                             walk_tree(txn, tree_top(txn, b, id), [&](node_id at2) {
                                 if (is(txn, at2, "label") && labeled_control(txn, b, at2) == id) {
                                     out.push_back(at2);
                                 }
                             });
                             return out;
                         },
                         "NodeList");
                 });
    }

    // --- HTMLFieldSetElement, HTMLOutputElement, HTMLTextAreaElement bits -------------
    accessor("HTMLFieldSetElement", "type",
             [](context & c, std::span<value>) { return c.string("fieldset"); });
    accessor("HTMLFieldSetElement", "elements",
             [at, is_listed, walk_tree](context & c, std::span<value>) {
                 const auto where = at(c);
                 dom_bindings * b = where.first;
                 const node_id id = where.second;
                 if (!id) { return value::null(); }
                 return b->make_live_collection(c, [b, id, is_listed, walk_tree] {
                     std::vector<node_id> out;
                     const auto txn = b->doc_->read();
                     if (!txn.contains(id)) { return out; }
                     walk_tree(txn, id, [&](node_id at2) {
                         if (at2 != id && is_listed(txn, b, at2)) { out.push_back(at2); }
                     });
                     return out;
                 });
             });
    accessor("HTMLOutputElement", "type",
             [](context & c, std::span<value>) { return c.string("output"); });
    // An output's `value` and `defaultValue`, HTML 4.10.12: default mode
    // reads the text; setting `value` switches to value mode and keeps the
    // text that was the default.
    accessor(
        "HTMLOutputElement", "defaultValue",
        [at, slot_of](context & c, std::span<value>) {
            const auto where = at(c);
            dom_bindings * b = where.first;
            const node_id id = where.second;
            if (!id) { return c.string(""); }
            const value held = slot_of(c, b, id, "__defaultValue");
            return held.is_undefined() ? c.string(b->text_content(id)) : held;
        },
        [at, slot_of, set_slot](context & c, std::span<value> a) {
            const auto where = at(c);
            dom_bindings * b = where.first;
            const node_id id = where.second;
            if (!id) { return value::undefined(); }
            const value text = c.string(arg_string(c, a, 0));
            if (slot_of(c, b, id, "__defaultValue").is_undefined()) {
                b->set_text(id, c.to_string(text));
            } else {
                set_slot(c, b, id, "__defaultValue", text);
            }
            return value::undefined();
        });
    accessor(
        "HTMLOutputElement", "value",
        [at](context & c, std::span<value>) {
            const auto where = at(c);
            dom_bindings * b = where.first;
            const node_id id = where.second;
            return c.string(id ? b->text_content(id) : std::string{});
        },
        [at, slot_of, set_slot](context & c, std::span<value> a) {
            const auto where = at(c);
            dom_bindings * b = where.first;
            const node_id id = where.second;
            if (!id) { return value::undefined(); }
            if (slot_of(c, b, id, "__defaultValue").is_undefined()) {
                set_slot(c, b, id, "__defaultValue", c.string(b->text_content(id)));
            }
            b->set_text(id, arg_string(c, a, 0));
            return value::undefined();
        });
    accessor("HTMLTextAreaElement", "type",
             [](context & c, std::span<value>) { return c.string("textarea"); });
    accessor("HTMLTextAreaElement", "textLength", [at, value_of](context & c, std::span<value>) {
        const auto where = at(c);
        dom_bindings * b = where.first;
        const node_id id = where.second;
        if (!id) { return value::number(0); }
        const auto txn = b->doc_->read();
        return value::number(static_cast<double>(units_length_of(value_of(txn, b, id))));
    });
    accessor(
        "HTMLTextAreaElement", "defaultValue",
        [at](context & c, std::span<value>) {
            const auto where = at(c);
            dom_bindings * b = where.first;
            const node_id id = where.second;
            return c.string(id ? b->text_content(id) : std::string{});
        },
        [at](context & c, std::span<value> a) {
            const auto where = at(c);
            dom_bindings * b = where.first;
            const node_id id = where.second;
            if (id) { b->set_text(id, arg_string(c, a, 0)); }
            return value::undefined();
        });
    accessor("HTMLInputElement", "list",
             [at, input_type, walk_tree, tree_top, is](context & c, std::span<value>) {
                 const auto where = at(c);
                 dom_bindings * b = where.first;
                 const node_id id = where.second;
                 if (!id) { return value::null(); }
                 node_id found;
                 {
                     const auto txn = b->doc_->read();
                     const std::string type = input_type(txn, b, id);
                     if (type == "hidden" || type == "password" || type == "checkbox" ||
                         type == "radio" || type == "file" || type == "submit" || type == "image" ||
                         type == "reset" || type == "button") {
                         return value::null();
                     }
                     const std::string_view want =
                         txn.attribute_value(id, b->atoms_->intern("list"));
                     if (want.empty()) { return value::null(); }
                     const atom id_attr = b->atoms_->intern("id");
                     walk_tree(txn, tree_top(txn, b, id), [&](node_id at2) {
                         if (!found && txn.attribute_value(at2, id_attr) == want) { found = at2; }
                     });
                     if (!is(txn, found, "datalist")) { found = node_id{}; }
                 }
                 return found ? b->wrap(c, found) : value::null();
             });

    // --- HTMLFormElement: elements, length, submit, requestSubmit, reset ----------------
    const auto form_elements = [slot_of, set_slot, elements_of_form](context & c, dom_bindings * b,
                                                                     node_id form) {
        const value held = slot_of(c, b, form, "__elements");
        if (held.is_object_like()) { return held; }
        const value list = b->make_live_collection(
            c, [b, form, elements_of_form] { return elements_of_form(b, form); },
            "HTMLFormControlsCollection");
        set_slot(c, b, form, "__elements", list);
        return list;
    };
    accessor("HTMLFormElement", "elements", [at, form_elements](context & c, std::span<value>) {
        const auto where = at(c);
        dom_bindings * b = where.first;
        const node_id id = where.second;
        return id ? form_elements(c, b, id) : value::null();
    });
    accessor("HTMLFormElement", "length", [at, elements_of_form](context & c, std::span<value>) {
        const auto where = at(c);
        dom_bindings * b = where.first;
        const node_id id = where.second;
        return value::number(id ? static_cast<double>(elements_of_form(b, id).size()) : 0);
    });

    // --- the input types' numbers, HTML 4.10.5.1 ----------------------------------
    //
    // number and range parse a floating-point number; date, month, week, time
    // and datetime-local parse their strings to a number of milliseconds (or
    // months, for month). `to_number` answers nullopt where the value is not
    // one, and `to_text` writes a number back in the type's own format.
    const auto to_number = [input_type](const read_txn & txn, dom_bindings * b, node_id id,
                                        std::string_view text) -> std::optional<double> {
        return type_value_to_number(input_type(txn, b, id), text);
    };
    const auto text_of_number = [input_type](const read_txn & txn, dom_bindings * b, node_id id,
                                             double number) -> std::string {
        return type_number_to_text(input_type(txn, b, id), number);
    };
    // The allowed value step in the type's units, or nullopt for `any`; the
    // step base; and the min/max, each when parseable.
    const auto step_of = [input_type, attribute_of](const read_txn & txn, dom_bindings * b,
                                                    node_id id) -> std::optional<double> {
        const std::string type = input_type(txn, b, id);
        const double scale = step_scale_of(type);
        const std::string text = attribute_of(txn, b, id, "step");
        if (!text.empty()) {
            if (ascii_lower_copy(text) == "any") { return std::nullopt; }
            double parsed = 0;
            if (parse_float(text, parsed, true) && parsed > 0) { return parsed * scale; }
        }
        return default_step_of(type) * scale;
    };
    const auto step_base_of = [to_number, attribute_of](const read_txn & txn, dom_bindings * b,
                                                        node_id id) -> double {
        for (const char * name : {"min", "value"}) {
            if (const auto held = to_number(txn, b, id, attribute_of(txn, b, id, name))) {
                return *held;
            }
        }
        return 0;
    };
    const auto bound_of = [to_number, attribute_of](const read_txn & txn, dom_bindings * b,
                                                    node_id id, const char * which) {
        return to_number(txn, b, id, attribute_of(txn, b, id, which));
    };
    const auto is_step_aligned = [](double number, double base, double step) {
        const double quotient = (number - base) / step;
        return std::fabs(quotient - std::round(quotient)) <
               1e-9 * std::max(1.0, std::fabs(quotient));
    };

    // valueAsNumber / valueAsDate, HTML 4.10.5.3.
    const auto number_types = [](std::string_view type) {
        return type == "number" || type == "range" || type == "date" || type == "month" ||
               type == "week" || type == "time" || type == "datetime-local";
    };
    const auto date_types = [](std::string_view type) {
        return type == "date" || type == "month" || type == "week" || type == "time";
    };
    // Write a control's value into the store, as the `value` setter does.
    const auto store_value = [](const read_txn & txn, dom_bindings * b, node_id id,
                                std::string text) {
        control_state & held = b->forms_->state_of(txn, *b->atoms_, id);
        held.value = std::move(text);
        held.caret = held.value.size();
        held.selection = held.caret;
        held.value_edited = true;
        b->wrote_to_control_ = true;
    };
    accessor(
        "HTMLInputElement", "valueAsNumber",
        [at, input_type, number_types, to_number, value_of](context & c, std::span<value>) {
            const auto where = at(c);
            dom_bindings * b = where.first;
            const node_id id = where.second;
            if (!id) { return value::number(std::nan("")); }
            const auto txn = b->doc_->read();
            const std::string type = input_type(txn, b, id);
            if (!number_types(type)) { return value::number(std::nan("")); }
            const std::optional<double> held = to_number(txn, b, id, value_of(txn, b, id));
            (void)c;
            return value::number(held.value_or(std::nan("")));
        },
        [this, at, input_type, number_types, text_of_number, store_value](context & c,
                                                                          std::span<value> a) {
            const auto where = at(c);
            dom_bindings * b = where.first;
            const node_id id = where.second;
            if (!id) { return value::undefined(); }
            const double number = context::to_number(arg(a, 0));
            if (std::isinf(number)) {
                c.throw_error("TypeError", "valueAsNumber: the value is not finite");
                return value::undefined();
            }
            const auto txn = b->doc_->read();
            if (!number_types(input_type(txn, b, id))) {
                throw_dom_exception(c, "InvalidStateError",
                                    "valueAsNumber does not apply to this input type");
                return value::undefined();
            }
            store_value(txn, b, id,
                        std::isnan(number) ? std::string{} : text_of_number(txn, b, id, number));
            b->mutated();
            return value::undefined();
        });
    accessor(
        "HTMLInputElement", "valueAsDate",
        [at, input_type, date_types, to_number, value_of](context & c, std::span<value>) {
            const auto where = at(c);
            dom_bindings * b = where.first;
            const node_id id = where.second;
            if (!id) { return value::null(); }
            std::optional<double> ms;
            {
                const auto txn = b->doc_->read();
                const std::string type = input_type(txn, b, id);
                if (!date_types(type)) { return value::null(); }
                ms = to_number(txn, b, id, value_of(txn, b, id));
                if (ms && type == "month") { ms = month_index_to_ms(*ms); }
            }
            if (!ms) { return value::null(); }
            const value when = value::number(*ms);
            return c.construct(c.global("Date"), std::span<const value>{&when, 1});
        },
        [this, at, input_type, date_types, text_of_number, store_value](context & c,
                                                                        std::span<value> a) {
            const auto where = at(c);
            dom_bindings * b = where.first;
            const node_id id = where.second;
            if (!id) { return value::undefined(); }
            const value given = arg(a, 0);
            const auto txn = b->doc_->read();
            const std::string type = input_type(txn, b, id);
            if (!date_types(type)) {
                throw_dom_exception(c, "InvalidStateError",
                                    "valueAsDate does not apply to this input type");
                return value::undefined();
            }
            double ms = std::nan("");
            if (!given.is_null()) {
                const value get_time = given.is_object_like() ? c.lookup_property(given, "getTime")
                                                              : value::undefined();
                if (!get_time.is_callable()) {
                    c.throw_error("TypeError", "valueAsDate: the value is not a Date");
                    return value::undefined();
                }
                ms = context::to_number(c.call(get_time, {}, given));
            }
            if (!std::isnan(ms) && type == "month") { ms = ms_to_month_index(ms); }
            store_value(txn, b, id,
                        std::isnan(ms) ? std::string{} : text_of_number(txn, b, id, ms));
            b->mutated();
            return value::undefined();
        });
    // `value`, `checked` and `files` ON THE PROTOTYPE, for the inputs the
    // wrapper installs no own accessor on - a hidden input has none, so its
    // value read `undefined` - and `indeterminate`, a slot on the wrapper.
    // Where the wrapper's own accessor exists it shadows these, as it must.
    accessor(
        "HTMLInputElement", "value",
        [at, value_of](context & c, std::span<value>) {
            const auto where = at(c);
            dom_bindings * b = where.first;
            const node_id id = where.second;
            if (!id) { return c.string(""); }
            const auto txn = b->doc_->read();
            return c.string(value_of(txn, b, id));
        },
        [at, store_value](context & c, std::span<value> a) {
            const auto where = at(c);
            dom_bindings * b = where.first;
            const node_id id = where.second;
            if (!id) { return value::undefined(); }
            {
                const auto txn = b->doc_->read();
                const value given = arg(a, 0);
                store_value(txn, b, id, given.is_null() ? std::string{} : c.to_string(given));
            }
            b->mutated();
            return value::undefined();
        });
    accessor(
        "HTMLInputElement", "checked",
        [at](context & c, std::span<value>) {
            const auto where = at(c);
            dom_bindings * b = where.first;
            const node_id id = where.second;
            if (!id) { return value::boolean(false); }
            const auto txn = b->doc_->read();
            return value::boolean(b->forms_->state_of(txn, *b->atoms_, id).checked);
        },
        [at](context & c, std::span<value> a) {
            const auto where = at(c);
            dom_bindings * b = where.first;
            const node_id id = where.second;
            if (!id) { return value::undefined(); }
            {
                const auto txn = b->doc_->read();
                b->forms_->state_of(txn, *b->atoms_, id).checked = context::truthy(arg(a, 0));
            }
            b->wrote_to_control_ = true;
            b->mutated();
            return value::undefined();
        });
    accessor("HTMLInputElement", "files",
             [](context &, std::span<value>) { return value::null(); });
    accessor(
        "HTMLInputElement", "indeterminate",
        [at, slot_of](context & c, std::span<value>) {
            const auto where = at(c);
            dom_bindings * b = where.first;
            const node_id id = where.second;
            if (!id) { return value::boolean(false); }
            return value::boolean(context::truthy(slot_of(c, b, id, "__indeterminate")));
        },
        [at, set_slot](context & c, std::span<value> a) {
            const auto where = at(c);
            dom_bindings * b = where.first;
            const node_id id = where.second;
            if (id) {
                set_slot(c, b, id, "__indeterminate", value::boolean(context::truthy(arg(a, 0))));
            }
            return value::undefined();
        });

    // stepUp / stepDown, HTML 4.10.5.3.
    const auto step_by = [this, at, input_type, number_types, step_of, step_base_of, bound_of,
                          to_number, value_of, is_step_aligned, text_of_number,
                          store_value](context & c, std::span<value> a, bool up) {
        const auto where = at(c);
        dom_bindings * b = where.first;
        const node_id id = where.second;
        if (!id) { return; }
        const auto txn = b->doc_->read();
        const std::string type = input_type(txn, b, id);
        if (!number_types(type)) {
            throw_dom_exception(c, "InvalidStateError", "stepUp/stepDown does not apply here");
            return;
        }
        const std::optional<double> step = step_of(txn, b, id);
        if (!step) {
            throw_dom_exception(c, "InvalidStateError", "the step is `any`");
            return;
        }
        const double base = step_base_of(txn, b, id);
        const std::optional<double> min = bound_of(txn, b, id, "min");
        const std::optional<double> max = bound_of(txn, b, id, "max");
        if (min && max && *min > *max) { return; }
        double number = to_number(txn, b, id, value_of(txn, b, id)).value_or(0);
        const double before = number;
        double n = a.empty() ? 1 : context::to_number(a[0]);
        if (std::isnan(n)) { n = 0; }
        if (!is_step_aligned(number, base, *step)) {
            const double quotient = (number - base) / *step;
            number = base + (up ? std::ceil(quotient) : std::floor(quotient)) * *step;
        } else {
            number += (up ? 1 : -1) * n * *step;
        }
        if (min && number < *min) { number = base + std::ceil((*min - base) / *step) * *step; }
        if (max && number > *max) { number = base + std::floor((*max - base) / *step) * *step; }
        if ((up && number < before) || (!up && number > before)) { return; }
        if (min && number < *min) { return; }
        if (max && number > *max) { return; }
        store_value(txn, b, id, text_of_number(txn, b, id, number));
        b->mutated();
    };
    operation("HTMLInputElement", "stepUp", 0, [step_by](context & c, std::span<value> a) {
        step_by(c, a, true);
        return value::undefined();
    });
    operation("HTMLInputElement", "stepDown", 0, [step_by](context & c, std::span<value> a) {
        step_by(c, a, false);
        return value::undefined();
    });

    // --- constraint validation, HTML 4.10.20 ------------------------------------------
    //
    // `willValidate` is "candidate for constraint validation": a submittable
    // element that is not barred - disabled, readonly (an input or textarea),
    // an input of type hidden/reset/button, inside a datalist - and not an
    // output, fieldset or object, which are never candidates. The validity
    // flags are computed on every read from the store's value and the
    // attributes; the custom message is a slot on the wrapper.
    //
    // ponytail: tooLong and tooShort are always false. HTML raises them only
    // for a value the USER edited, and the store cannot tell a user's edit
    // from a script's `value =` (both set value_edited); the manual tests
    // are the ones that would measure them.
    const auto will_validate = [is_submittable, is_disabled, input_type, has_attribute,
                                is](const read_txn & txn, dom_bindings * b, node_id id) {
        if (!is_submittable(txn, id) || txn.local_name(id) == "object") { return false; }
        if (is_disabled(txn, b, id)) { return false; }
        const std::string_view local = txn.local_name(id);
        if (local == "input") {
            const std::string type = input_type(txn, b, id);
            if (type == "hidden" || type == "reset" || type == "button") { return false; }
            if (has_attribute(txn, b, id, "readonly")) { return false; }
        }
        if (local == "textarea" && has_attribute(txn, b, id, "readonly")) { return false; }
        for (node_id up = txn.parent(id); up; up = txn.parent(up)) {
            if (is(txn, up, "datalist")) { return false; }
        }
        return true;
    };
    enum flag : unsigned {
        value_missing = 1u << 0,
        type_mismatch = 1u << 1,
        pattern_mismatch = 1u << 2,
        too_long = 1u << 3,
        too_short = 1u << 4,
        range_underflow = 1u << 5,
        range_overflow = 1u << 6,
        step_mismatch = 1u << 7,
        bad_input = 1u << 8,
        custom_error = 1u << 9,
    };
    // The pattern attribute compiled as the specification says - anchored,
    // with the `v` flag - through a JS function so a bad pattern is caught
    // there and means "no constraint" rather than a throw.
    const auto compile_pattern = [proto](context & c, const std::string & pattern) -> value {
        auto * on = proto("ValidityState");
        if (on == nullptr) { return value::null(); }
        value compiler = value::undefined();
        if (const value * held = on->find("__compile"); held != nullptr) { compiler = *held; }
        if (!compiler.is_callable()) {
            script::program compiled = script::compiler::compile(
                "return (function (p) {\n"
                "    try { return new RegExp('^(?:' + p + ')$', 'v'); } catch (e) {}\n"
                "    try { return new RegExp('^(?:' + p + ')$', 'u'); } catch (e) {}\n"
                "    return null;\n"
                "});\n");
            if (!compiled.ok) { return value::null(); }
            compiler = c.run_nested(c.own_program(std::move(compiled)));
            if (!compiler.is_callable()) { return value::null(); }
            on->define("__compile", compiler, script::attr_none);
        }
        const value text = c.string(pattern);
        return c.call(compiler, std::span<const value>{&text, 1});
    };
    const auto matches_pattern = [compile_pattern](context & c, const std::string & pattern,
                                                   const std::string & text) {
        const value re = compile_pattern(c, pattern);
        if (!re.is_object_like()) { return true; }
        const value test = c.lookup_property(re, "test");
        if (!test.is_callable()) { return true; }
        const value subject = c.string(text);
        return context::truthy(c.call(test, std::span<const value>{&subject, 1}, re));
    };
    // Every flag of a control, as a bit set.
    const auto validity_flags = [input_type, attribute_of, has_attribute, value_of, to_number,
                                 bound_of, step_of, step_base_of, is_step_aligned, matches_pattern,
                                 slot_of, selected_flags, option_value, display_size, form_owner,
                                 tree_top, walk_tree,
                                 is](context & c, dom_bindings * b, node_id id) -> unsigned {
        unsigned flags = 0;
        const auto txn = b->doc_->read();
        if (!txn.contains(id)) { return flags; }
        const value custom = slot_of(c, b, id, custom_slot);
        if (custom.is_string() && !c.to_string(custom).empty()) { flags |= custom_error; }
        const std::string_view local = txn.local_name(id);
        const bool required = has_attribute(txn, b, id, "required");
        if (local == "select") {
            if (required) {
                std::vector<node_id> options;
                const std::vector<bool> selected = selected_flags(c, txn, b, id, options);
                std::size_t first = options.size();
                for (std::size_t i = 0; i < options.size(); ++i) {
                    if (selected[i]) {
                        first = i;
                        break;
                    }
                }
                bool missing = first == options.size();
                // The placeholder label option: a display-size-1 single
                // select's first option child with an empty value.
                if (!missing && display_size(txn, b, id) == 1 &&
                    !has_attribute(txn, b, id, "multiple")) {
                    node_id first_child;
                    for (const node_id child : txn.children(id)) {
                        if (is(txn, child, "option")) {
                            first_child = child;
                            break;
                        }
                    }
                    if (first_child && options[first] == first_child &&
                        option_value(txn, b, first_child).empty()) {
                        missing = true;
                    }
                }
                if (missing) { flags |= value_missing; }
            }
            return flags;
        }
        if (local == "textarea") {
            if (required && value_of(txn, b, id).empty()) { flags |= value_missing; }
            return flags;
        }
        if (local != "input") { return flags; }
        const std::string type = input_type(txn, b, id);
        const std::string text = value_of(txn, b, id);
        const bool checked = b->forms_->state_of(txn, *b->atoms_, id).checked;
        if (type == "checkbox") {
            if (required && !checked) { flags |= value_missing; }
            return flags;
        }
        if (type == "radio") {
            // The radio button group: same name, same form owner, same tree.
            const std::string name = attribute_of(txn, b, id, "name");
            bool any_required = required;
            bool any_checked = checked;
            if (!name.empty()) {
                const node_id owner = form_owner(txn, b, id);
                walk_tree(txn, tree_top(txn, b, id), [&](node_id other) {
                    if (other == id || !is(txn, other, "input") ||
                        input_type(txn, b, other) != "radio" ||
                        attribute_of(txn, b, other, "name") != name ||
                        form_owner(txn, b, other) != owner) {
                        return;
                    }
                    if (has_attribute(txn, b, other, "required")) { any_required = true; }
                    if (b->forms_->state_of(txn, *b->atoms_, other).checked) { any_checked = true; }
                });
            }
            if (any_required && !any_checked) { flags |= value_missing; }
            return flags;
        }
        if (type == "hidden" || type == "submit" || type == "image" || type == "reset" ||
            type == "button") {
            return flags;
        }
        if (type == "file") {
            if (required) { flags |= value_missing; }
            return flags;
        }
        if (required && text.empty()) { flags |= value_missing; }
        if (type == "email" && !text.empty()) {
            bool ok = true;
            if (has_attribute(txn, b, id, "multiple")) {
                std::size_t start = 0;
                while (ok) {
                    const std::size_t comma = text.find(',', start);
                    const std::string_view one = std::string_view{text}.substr(
                        start, comma == std::string::npos ? std::string::npos : comma - start);
                    if (!is_valid_email(trim(one, html_whitespace))) { ok = false; }
                    if (comma == std::string::npos) { break; }
                    start = comma + 1;
                }
            } else {
                ok = is_valid_email(text);
            }
            if (!ok) { flags |= type_mismatch; }
        }
        if (type == "url" && !text.empty() && !is_valid_url(text)) { flags |= type_mismatch; }
        const std::string pattern = attribute_of(txn, b, id, "pattern");
        if (!pattern.empty() && !text.empty() &&
            (type == "text" || type == "search" || type == "url" || type == "tel" ||
             type == "email" || type == "password")) {
            bool ok = true;
            if (type == "email" && has_attribute(txn, b, id, "multiple")) {
                std::size_t start = 0;
                while (ok) {
                    const std::size_t comma = text.find(',', start);
                    ok = matches_pattern(c, pattern,
                                         text.substr(start, comma == std::string::npos
                                                                ? std::string::npos
                                                                : comma - start));
                    if (comma == std::string::npos) { break; }
                    start = comma + 1;
                }
            } else {
                ok = matches_pattern(c, pattern, text);
            }
            if (!ok) { flags |= pattern_mismatch; }
        }
        if (type == "number" || type == "date" || type == "month" || type == "week" ||
            type == "time" || type == "datetime-local" || type == "range") {
            const std::optional<double> number = to_number(txn, b, id, text);
            if (!text.empty() && !number) { flags |= bad_input; }
            if (number && type != "range") {
                if (const auto min = bound_of(txn, b, id, "min"); min && *number < *min) {
                    flags |= range_underflow;
                }
                if (const auto max = bound_of(txn, b, id, "max"); max && *number > *max) {
                    flags |= range_overflow;
                }
            }
            if (number) {
                if (const auto step = step_of(txn, b, id);
                    step && !is_step_aligned(*number, step_base_of(txn, b, id), *step)) {
                    flags |= step_mismatch;
                }
            }
        }
        return flags;
    };
    // A ValidityState object: the flags frozen into a hidden slot, read by
    // the accessors on the prototype.
    if (!secondary_) {
        if (auto * on = proto("ValidityState")) {
            const auto flag_getter = [&](const char * name, unsigned bit) {
                on->define_accessor(
                    name,
                    native(cx, name,
                           [bit](context & c, std::span<value>) {
                               const value held = c.lookup_property(c.current_this(), "__flags");
                               const unsigned flags = context::to_uint32(held);
                               return value::boolean(bit == 0 ? flags == 0 : (flags & bit) != 0);
                           }),
                    value::undefined());
            };
            flag_getter("valueMissing", value_missing);
            flag_getter("typeMismatch", type_mismatch);
            flag_getter("patternMismatch", pattern_mismatch);
            flag_getter("tooLong", too_long);
            flag_getter("tooShort", too_short);
            flag_getter("rangeUnderflow", range_underflow);
            flag_getter("rangeOverflow", range_overflow);
            flag_getter("stepMismatch", step_mismatch);
            flag_getter("badInput", bad_input);
            flag_getter("customError", custom_error);
            flag_getter("valid", 0);
        }
    }
    const auto validity_object = [this](context & c, unsigned flags) {
        auto * made = c.allocate<script::object_object>();
        if (const value proto_value = interface_prototype("ValidityState");
            proto_value.is_object()) {
            made->prototype = proto_value;
        }
        made->define("__flags", value::number(static_cast<double>(flags)), script::attr_none);
        return value::object(made);
    };
    // The message for the first flag set, as a browser words it.
    const auto validation_message = [slot_of](context & c, dom_bindings * b, node_id id,
                                              unsigned flags) -> std::string {
        if (flags == 0) { return {}; }
        if ((flags & custom_error) != 0) { return c.to_string(slot_of(c, b, id, custom_slot)); }
        if ((flags & value_missing) != 0) { return "Please fill out this field."; }
        if ((flags & type_mismatch) != 0) { return "Please enter a valid value."; }
        if ((flags & pattern_mismatch) != 0) { return "Please match the requested format."; }
        if ((flags & range_underflow) != 0) {
            return "Value must be greater than or equal to the minimum.";
        }
        if ((flags & range_overflow) != 0) {
            return "Value must be less than or equal to the maximum.";
        }
        if ((flags & step_mismatch) != 0) { return "Please enter a valid value."; }
        if ((flags & bad_input) != 0) { return "Please enter a valid value."; }
        return "Please lengthen this text.";
    };
    // "Check validity" / "report validity" for one element: fires `invalid`
    // (cancelable, not bubbling) when a candidate is invalid.
    const auto check_one = [will_validate, validity_flags, fire,
                            plain](context & c, dom_bindings * b, node_id id) {
        bool candidate = false;
        {
            const auto txn = b->doc_->read();
            candidate = txn.contains(id) && will_validate(txn, b, id);
        }
        if (!candidate || validity_flags(c, b, id) == 0) { return true; }
        (void)fire(c, b, id, "invalid", false, true, plain);
        return false;
    };
    for (const char * which :
         {"HTMLButtonElement", "HTMLFieldSetElement", "HTMLInputElement", "HTMLObjectElement",
          "HTMLOutputElement", "HTMLSelectElement", "HTMLTextAreaElement"}) {
        accessor(which, "willValidate", [at, will_validate](context & c, std::span<value>) {
            const auto where = at(c);
            dom_bindings * b = where.first;
            const node_id id = where.second;
            if (!id) { return value::boolean(false); }
            const auto txn = b->doc_->read();
            return value::boolean(will_validate(txn, b, id));
        });
        accessor(which, "validity",
                 [at, validity_flags, validity_object](context & c, std::span<value>) {
                     const auto where = at(c);
                     dom_bindings * b = where.first;
                     const node_id id = where.second;
                     return validity_object(c, id ? validity_flags(c, b, id) : 0);
                 });
        accessor(
            which, "validationMessage",
            [at, will_validate, validity_flags, validation_message](context & c, std::span<value>) {
                const auto where = at(c);
                dom_bindings * b = where.first;
                const node_id id = where.second;
                if (!id) { return c.string(""); }
                {
                    const auto txn = b->doc_->read();
                    if (!will_validate(txn, b, id)) { return c.string(""); }
                }
                return c.string(validation_message(c, b, id, validity_flags(c, b, id)));
            });
        operation(which, "setCustomValidity", 1, [this, set_slot](context & c, std::span<value> a) {
            if (const node_id id = receiver(c)) {
                set_slot(c, this, id, custom_slot, c.string(arg_string(c, a, 0)));
            }
            return value::undefined();
        });
        operation(which, "checkValidity", 0, [this, check_one](context & c, std::span<value>) {
            const node_id id = receiver(c);
            return value::boolean(!id || check_one(c, this, id));
        });
        operation(which, "reportValidity", 0, [this, check_one](context & c, std::span<value>) {
            const node_id id = receiver(c);
            return value::boolean(!id || check_one(c, this, id));
        });
    }
    // The form's: "statically validate the constraints" over its submittable
    // elements, every invalid one told.
    const auto validate_form = [elements_of_form, is_submittable,
                                check_one](context & c, dom_bindings * b, node_id form) {
        bool ok = true;
        for (const node_id one : elements_of_form(b, form)) {
            bool submittable = false;
            {
                const auto txn = b->doc_->read();
                submittable = txn.contains(one) && is_submittable(txn, one);
            }
            if (submittable && !check_one(c, b, one)) { ok = false; }
        }
        return ok;
    };
    operation("HTMLFormElement", "checkValidity", 0,
              [this, validate_form](context & c, std::span<value>) {
                  const node_id id = receiver(c);
                  return value::boolean(!id || validate_form(c, this, id));
              });
    operation("HTMLFormElement", "reportValidity", 0,
              [this, validate_form](context & c, std::span<value>) {
                  const node_id id = receiver(c);
                  return value::boolean(!id || validate_form(c, this, id));
              });

    // --- the entry list and FormData, HTML 4.10.21.4 and XHR's FormData -----------------
    //
    // A FormData is a list of (name, value) pairs held as a hidden JS array of
    // two-element arrays on the object - traced by the collector - with the
    // prototype's methods over it. A value is a string, or the object it was
    // given when that is a Blob or a File.
    const auto entries_of = [](context & c) -> script::array_object * {
        const value self = c.current_this();
        if (!self.is_object()) { return nullptr; }
        const value * held =
            static_cast<script::object_object *>(self.as_heap())->find(entries_slot);
        if (held == nullptr || !held->is_array()) { return nullptr; }
        return static_cast<script::array_object *>(held->as_heap());
    };
    const auto make_entry = [](context & c, const std::string & name, value held) {
        const value pair = c.make_array();
        auto & items = static_cast<script::array_object *>(pair.as_heap())->items;
        items.push_back(c.string(name));
        items.push_back(held.is_object_like() ? held : c.string(c.to_string(held)));
        return pair;
    };
    const auto entry_name = [](context & c, const value & pair) {
        return c.to_string(static_cast<script::array_object *>(pair.as_heap())->items.at(0));
    };
    const auto entry_value = [](const value & pair) {
        return static_cast<script::array_object *>(pair.as_heap())->items.at(1);
    };
    // "Construct the entry list" for a form: the submittable elements'
    // successful controls, in tree order, then the `formdata` event.
    const auto entry_list = [elements_of_form, is_submittable, is_disabled, input_type, is,
                             attribute_of, value_of, selected_flags, option_value, has_attribute,
                             make_entry, fire](context & c, dom_bindings * b, node_id form,
                                               node_id submitter, value form_data) {
        auto * list = static_cast<script::array_object *>(
            static_cast<script::object_object *>(form_data.as_heap())
                ->find(entries_slot)
                ->as_heap());
        const auto add = [&](const std::string & name, value held) {
            list->items.push_back(make_entry(c, name, held));
        };
        for (const node_id one : elements_of_form(b, form)) {
            const auto txn = b->doc_->read();
            if (!txn.contains(one) || !is_submittable(txn, one) || is_disabled(txn, b, one)) {
                continue;
            }
            bool in_datalist = false;
            for (node_id up = txn.parent(one); up; up = txn.parent(up)) {
                if (is(txn, up, "datalist")) { in_datalist = true; }
            }
            if (in_datalist) { continue; }
            const std::string_view local = txn.local_name(one);
            const std::string type = local == "input" ? input_type(txn, b, one) : std::string{};
            if (local == "object") { continue; }
            if ((local == "button" || type == "submit" || type == "reset" || type == "button") &&
                one != submitter) {
                continue;
            }
            if (local == "button" && one == submitter) {
                const std::string_view button_type =
                    txn.attribute_value(one, b->atoms_->intern("type"));
                if (ascii_lower_copy(button_type) == "reset" ||
                    ascii_lower_copy(button_type) == "button") {
                    continue;
                }
            }
            if ((type == "checkbox" || type == "radio") &&
                !b->forms_->state_of(txn, *b->atoms_, one).checked) {
                continue;
            }
            if (type == "image" && one != submitter) { continue; }
            const std::string name = attribute_of(txn, b, one, "name");
            if (type == "image") {
                const std::string prefix = name.empty() ? "" : name + ".";
                add(prefix + "x", value::number(0));
                add(prefix + "y", value::number(0));
                continue;
            }
            if (name.empty()) { continue; }
            if (local == "select") {
                std::vector<node_id> options;
                const std::vector<bool> flags = selected_flags(c, txn, b, one, options);
                for (std::size_t i = 0; i < options.size(); ++i) {
                    if (flags[i] && !is_disabled(txn, b, options[i])) {
                        add(name, c.string(option_value(txn, b, options[i])));
                    }
                }
                continue;
            }
            if (type == "checkbox" || type == "radio") {
                add(name, c.string(has_attribute(txn, b, one, "value") ? value_of(txn, b, one)
                                                                       : std::string{"on"}));
                continue;
            }
            if (type == "file") { continue; }
            if (type == "hidden" && ascii_lower_copy(name) == "_charset_" &&
                !has_attribute(txn, b, one, "value")) {
                add(name, c.string("UTF-8"));
                continue;
            }
            add(name, c.string(value_of(txn, b, one)));
            if ((local == "textarea" || type == "text" || type == "search") &&
                !attribute_of(txn, b, one, "dirname").empty()) {
                add(attribute_of(txn, b, one, "dirname"), c.string("ltr"));
            }
        }
        (void)fire(c, b, form, "formdata", true, false, [form_data](script::object_object & event) {
            event.set("formData", form_data);
        });
    };
    if (!secondary_) {
        auto * form_data_proto = cx.allocate<script::object_object>();
        const value form_data_proto_value = value::object(form_data_proto);
        form_data_proto->define("@@toStringTag", cx.string("FormData"), script::attr_configurable);
        const auto fd_method = [&](const char * name, unsigned length, script::native_fn fn) {
            auto * made = cx.allocate<script::native_object>(name, std::move(fn));
            made->define("length", value::number(length), script::attr_configurable);
            form_data_proto->define(name, value::object(made), script::attr_builtin);
        };
        fd_method("append", 2, [entries_of, make_entry](context & c, std::span<value> a) {
            if (a.size() < 2) {
                c.throw_error("TypeError", "FormData.append needs a name and a value");
                return value::undefined();
            }
            if (auto * list = entries_of(c)) {
                list->items.push_back(make_entry(c, c.to_string(a[0]), a[1]));
            }
            return value::undefined();
        });
        fd_method("delete", 1, [entries_of, entry_name](context & c, std::span<value> a) {
            if (auto * list = entries_of(c)) {
                const std::string name = arg_string(c, a, 0);
                std::erase_if(list->items,
                              [&](const value & pair) { return entry_name(c, pair) == name; });
            }
            return value::undefined();
        });
        fd_method("get", 1, [entries_of, entry_name, entry_value](context & c, std::span<value> a) {
            if (auto * list = entries_of(c)) {
                const std::string name = arg_string(c, a, 0);
                for (const value & pair : list->items) {
                    if (entry_name(c, pair) == name) { return entry_value(pair); }
                }
            }
            return value::null();
        });
        fd_method("getAll", 1,
                  [entries_of, entry_name, entry_value](context & c, std::span<value> a) {
                      const value out = c.make_array();
                      if (auto * list = entries_of(c)) {
                          const std::string name = arg_string(c, a, 0);
                          for (const value & pair : list->items) {
                              if (entry_name(c, pair) == name) {
                                  static_cast<script::array_object *>(out.as_heap())
                                      ->items.push_back(entry_value(pair));
                              }
                          }
                      }
                      return out;
                  });
        fd_method("has", 1, [entries_of, entry_name](context & c, std::span<value> a) {
            if (auto * list = entries_of(c)) {
                const std::string name = arg_string(c, a, 0);
                for (const value & pair : list->items) {
                    if (entry_name(c, pair) == name) { return value::boolean(true); }
                }
            }
            return value::boolean(false);
        });
        fd_method("set", 2, [entries_of, entry_name, make_entry](context & c, std::span<value> a) {
            if (a.size() < 2) {
                c.throw_error("TypeError", "FormData.set needs a name and a value");
                return value::undefined();
            }
            auto * list = entries_of(c);
            if (list == nullptr) { return value::undefined(); }
            const std::string name = c.to_string(a[0]);
            const value fresh = make_entry(c, name, a[1]);
            bool replaced = false;
            std::vector<value> kept;
            for (const value & pair : list->items) {
                if (entry_name(c, pair) != name) {
                    kept.push_back(pair);
                } else if (!replaced) {
                    kept.push_back(fresh);
                    replaced = true;
                }
            }
            if (!replaced) { kept.push_back(fresh); }
            list->items = std::move(kept);
            return value::undefined();
        });
        // The iterable declaration: a snapshot of the pairs as an array's
        // iterator - keys, values, entries, @@iterator and forEach.
        const auto iterate = [entries_of, entry_name, entry_value](context & c, int kind) {
            const value out = c.make_array();
            auto & items = static_cast<script::array_object *>(out.as_heap())->items;
            if (auto * list = entries_of(c)) {
                for (const value & pair : list->items) {
                    if (kind == 0) {
                        items.push_back(c.string(entry_name(c, pair)));
                    } else if (kind == 1) {
                        items.push_back(entry_value(pair));
                    } else {
                        const value copy = c.make_array();
                        auto & two = static_cast<script::array_object *>(copy.as_heap())->items;
                        two.push_back(c.string(entry_name(c, pair)));
                        two.push_back(entry_value(pair));
                        items.push_back(copy);
                    }
                }
            }
            const value fn = c.lookup_property(out, "values");
            return fn.is_callable() ? c.call(fn, {}, out) : out;
        };
        fd_method("keys", 0, [iterate](context & c, std::span<value>) { return iterate(c, 0); });
        fd_method("values", 0, [iterate](context & c, std::span<value>) { return iterate(c, 1); });
        fd_method("entries", 0, [iterate](context & c, std::span<value>) { return iterate(c, 2); });
        form_data_proto->define("@@iterator", *form_data_proto->find("entries"),
                                script::attr_builtin);
        fd_method(
            "forEach", 1, [entries_of, entry_name, entry_value](context & c, std::span<value> a) {
                const value fn = arg(a, 0);
                if (!fn.is_callable()) {
                    c.throw_error("TypeError", "FormData.forEach: the callback is not a function");
                    return value::undefined();
                }
                auto * list = entries_of(c);
                if (list == nullptr) { return value::undefined(); }
                const value self = c.current_this();
                for (std::size_t i = 0; i < list->items.size(); ++i) {
                    const value pair = list->items[i];
                    const value args[3] = {entry_value(pair), c.string(entry_name(c, pair)), self};
                    (void)c.call(fn, args, arg(a, 1));
                    if (c.throw_pending()) { break; }
                }
                return value::undefined();
            });
        auto * form_data_ctor = cx.allocate<script::native_object>(
            "FormData",
            [this, form_data_proto_value, entry_list, is, input_type](context & c,
                                                                      std::span<value> a) -> value {
                const value self = c.current_this();
                if (!self.is_object()) {
                    c.throw_error("TypeError", "Failed to construct 'FormData': please use the "
                                               "'new' operator.");
                    return value::undefined();
                }
                auto * made = static_cast<script::object_object *>(self.as_heap());
                if (!made->prototype.is_object()) { made->prototype = form_data_proto_value; }
                made->define(std::string{entries_slot}, c.make_array(), script::attr_none);
                const value form_value = arg(a, 0);
                if (form_value.is_undefined()) { return self; }
                dom_bindings * b = owner_of(form_value);
                const node_id form = b == nullptr ? node_id{} : b->handle_of(form_value);
                bool is_form = false;
                if (form) {
                    const auto txn = b->doc_->read();
                    is_form = is(txn, form, "form");
                }
                if (!is_form) {
                    c.throw_error("TypeError", "Failed to construct 'FormData': parameter 1 is "
                                               "not of type 'HTMLFormElement'.");
                    return value::undefined();
                }
                node_id submitter;
                if (const value given = arg(a, 1); !given.is_nullish()) {
                    submitter = b->handle_of(given);
                    bool is_button = false;
                    bool owned = false;
                    if (submitter) {
                        const auto txn = b->doc_->read();
                        is_button = is(txn, submitter, "button") ||
                                    (is(txn, submitter, "input") &&
                                     (input_type(txn, b, submitter) == "submit" ||
                                      input_type(txn, b, submitter) == "image"));
                        for (node_id up = txn.parent(submitter); up; up = txn.parent(up)) {
                            if (up == form) { owned = true; }
                        }
                    }
                    if (!is_button) {
                        c.throw_error("TypeError", "Failed to construct 'FormData': the submitter "
                                                   "is not a submit button");
                        return value::undefined();
                    }
                    if (!owned) {
                        throw_dom_exception(c, "NotFoundError",
                                            "the submitter is not owned by the form");
                        return value::undefined();
                    }
                }
                entry_list(c, b, form, submitter, self);
                return self;
            });
        form_data_ctor->define("prototype", form_data_proto_value, script::attr_none);
        form_data_ctor->define("length", value::number(0), script::attr_configurable);
        form_data_proto->define("constructor", value::object(form_data_ctor), script::attr_builtin);
        cx.define_global("FormData", value::object(form_data_ctor));
    }
    // A fresh FormData for a submission, made through the constructor so the
    // page's `FormData.prototype` is the one on it.
    const auto new_form_data = [](context & c) -> value {
        const value ctor = c.global("FormData");
        return ctor.is_callable() ? c.construct(ctor, {}) : value::undefined();
    };

    // --- submit, requestSubmit, reset, HTML 4.10.21 ------------------------------------
    //
    // Submission stops where the network would begin: the entry list is
    // built (which fires `formdata`), and nothing navigates. `submit()`
    // fires no `submit` event; `requestSubmit()` validates, fires it, and
    // builds the list unless it was cancelled.
    const auto build_submission = [new_form_data, entry_list](context & c, dom_bindings * b,
                                                              node_id form, node_id submitter) {
        const value data = new_form_data(c);
        if (data.is_object()) { entry_list(c, b, form, submitter, data); }
    };
    operation("HTMLFormElement", "submit", 0,
              [this, build_submission](context & c, std::span<value>) {
                  const node_id form = receiver(c);
                  if (!form || !is_connected(form)) { return value::undefined(); }
                  build_submission(c, this, form, node_id{});
                  return value::undefined();
              });
    operation("HTMLFormElement", "requestSubmit", 0,
              [this, is, input_type, has_attribute, validate_form, fire,
               build_submission](context & c, std::span<value> a) {
                  const node_id form = receiver(c);
                  if (!form) { return value::undefined(); }
                  node_id submitter;
                  if (const value given = arg(a, 0); !given.is_nullish()) {
                      submitter = handle_of(given);
                      bool is_button = false;
                      bool owned = false;
                      {
                          const auto txn = doc_->read();
                          if (submitter) {
                              is_button = (is(txn, submitter, "button") &&
                                           ascii_lower_copy(txn.attribute_value(
                                               submitter, atoms_->intern("type"))) != "reset" &&
                                           ascii_lower_copy(txn.attribute_value(
                                               submitter, atoms_->intern("type"))) != "button") ||
                                          (is(txn, submitter, "input") &&
                                           (input_type(txn, this, submitter) == "submit" ||
                                            input_type(txn, this, submitter) == "image"));
                              for (node_id up = txn.parent(submitter); up; up = txn.parent(up)) {
                                  if (up == form) { owned = true; }
                              }
                              if (!owned && submitter) {
                                  const std::string_view named =
                                      txn.attribute_value(submitter, atoms_->intern("form"));
                                  owned = !named.empty() &&
                                          txn.attribute_value(form, atoms_->intern("id")) == named;
                              }
                          }
                      }
                      if (!is_button) {
                          c.throw_error("TypeError", "Failed to execute 'requestSubmit': the "
                                                     "submitter is not a submit button");
                          return value::undefined();
                      }
                      if (!owned) {
                          throw_dom_exception(
                              c, "NotFoundError",
                              "requestSubmit: the submitter is not owned by the form");
                          return value::undefined();
                      }
                  }
                  if (!is_connected(form)) { return value::undefined(); }
                  bool validate = true;
                  {
                      const auto txn = doc_->read();
                      if (has_attribute(txn, this, form, "novalidate") ||
                          (submitter && has_attribute(txn, this, submitter, "formnovalidate"))) {
                          validate = false;
                      }
                  }
                  if (validate && !validate_form(c, this, form)) { return value::undefined(); }
                  const value submitter_value = submitter ? wrap(c, submitter) : value::null();
                  const bool cancelled = fire(c, this, form, "submit", true, true,
                                              [submitter_value](script::object_object & event) {
                                                  event.set("submitter", submitter_value);
                                              });
                  if (cancelled || c.throw_pending()) { return value::undefined(); }
                  build_submission(c, this, form, submitter);
                  return value::undefined();
              });
    operation("HTMLFormElement", "reset", 0,
              [this, fire, plain, elements_of_form, is, walk_tree, erase_slot](context & c,
                                                                               std::span<value>) {
                  const node_id form = receiver(c);
                  if (!form) { return value::undefined(); }
                  if (fire(c, this, form, "reset", true, true, plain)) {
                      return value::undefined();
                  }
                  {
                      const auto txn = doc_->read();
                      forms_->reset_form(txn, form);
                      for (const node_id one : elements_of_form(this, form)) {
                          forms_->reset_form(txn, one);
                          if (is(txn, one, "select")) {
                              walk_tree(txn, one, [&](node_id at2) {
                                  if (is(txn, at2, "option")) {
                                      erase_slot(c, this, at2, selected_slot);
                                      erase_slot(c, this, at2, dirty_slot);
                                  }
                              });
                          }
                          if (is(txn, one, "output")) {
                              erase_slot(c, this, one, "__defaultValue");
                          }
                      }
                  }
                  wrote_to_control_ = true;
                  mutated();
                  return value::undefined();
              });

    // --- the selection API, HTML 4.10.19.5 --------------------------------------------
    //
    // On a textarea and on an input of a text-like type. The store keeps the
    // caret and the anchor as byte offsets into the UTF-8 value; the API
    // speaks UTF-16 code units, and `direction` is a slot on the wrapper.
    const auto selection_applies = [input_type, is](const read_txn & txn, dom_bindings * b,
                                                    node_id id) {
        if (is(txn, id, "textarea")) { return true; }
        if (!is(txn, id, "input")) { return false; }
        const std::string type = input_type(txn, b, id);
        return type == "text" || type == "search" || type == "url" || type == "tel" ||
               type == "password";
    };
    struct text_selection {
        std::size_t start = 0; // code units
        std::size_t end = 0;
        std::string direction = "none";
    };
    const auto selection_of = [slot_of](context & c, const read_txn & txn, dom_bindings * b,
                                        node_id id) {
        const control_state & held = b->forms_->state_of(txn, *b->atoms_, id);
        text_selection out;
        const std::size_t lo = std::min(held.caret, held.selection);
        const std::size_t hi = std::max(held.caret, held.selection);
        out.start = units_before(held.value, lo);
        out.end = units_before(held.value, hi);
        const value direction = slot_of(c, b, id, "__selectionDirection");
        if (direction.is_string()) { out.direction = c.to_string(direction); }
        if (out.start == out.end) {
            out.direction = out.direction == "backward"
                                ? "backward"
                                : (out.direction == "forward" ? "forward" : "none");
        }
        return out;
    };
    // "Set the selection range", with the `select` event queued when it moved.
    const auto set_selection = [set_slot, selection_of, fire,
                                plain](context & c, dom_bindings * b, node_id id, double start,
                                       double end, std::string direction) {
        bool changed = false;
        {
            const auto txn = b->doc_->read();
            control_state & held = b->forms_->state_of(txn, *b->atoms_, id);
            const std::size_t length = units_length_of(held.value);
            const auto clamp = [&](double x) {
                if (std::isnan(x) || x < 0) { return std::size_t{0}; }
                return std::min(static_cast<std::size_t>(x), length);
            };
            std::size_t s = clamp(start);
            std::size_t e = clamp(end);
            if (e < s) { s = e; }
            const text_selection before = selection_of(c, txn, b, id);
            if (direction != "forward" && direction != "backward") { direction = "none"; }
            const std::size_t s_bytes = bytes_before(held.value, s);
            const std::size_t e_bytes = bytes_before(held.value, e);
            if (direction == "backward") {
                held.caret = s_bytes;
                held.selection = e_bytes;
            } else {
                held.selection = s_bytes;
                held.caret = e_bytes;
            }
            set_slot(c, b, id, "__selectionDirection", c.string(direction));
            changed = before.start != s || before.end != e || before.direction != direction;
        }
        if (changed) {
            // Queued, as the specification says: a task, not a synchronous call.
            auto * task = c.allocate<script::native_object>(
                "select", [b, id, fire, plain](context & inner, std::span<value>) {
                    (void)fire(inner, b, id, "select", true, false, plain);
                    return value::undefined();
                });
            (void)b->add_timer(value::object(task), 0, false);
        }
    };
    for (const char * which : {"HTMLInputElement", "HTMLTextAreaElement"}) {
        const auto selection_accessor = [&](const char * name, auto read, auto write) {
            accessor(
                which, name,
                [at, selection_applies, selection_of, read](context & c, std::span<value>) {
                    const auto where = at(c);
                    dom_bindings * b = where.first;
                    const node_id id = where.second;
                    if (!id) { return value::null(); }
                    const auto txn = b->doc_->read();
                    if (!selection_applies(txn, b, id)) { return value::null(); }
                    return read(c, selection_of(c, txn, b, id));
                },
                [this, at, selection_applies, selection_of, set_selection,
                 write](context & c, std::span<value> a) {
                    const auto where = at(c);
                    dom_bindings * b = where.first;
                    const node_id id = where.second;
                    if (!id) { return value::undefined(); }
                    text_selection current;
                    {
                        const auto txn = b->doc_->read();
                        if (!selection_applies(txn, b, id)) {
                            throw_dom_exception(c, "InvalidStateError",
                                                "the selection does not apply to this control");
                            return value::undefined();
                        }
                        current = selection_of(c, txn, b, id);
                    }
                    write(c, current, arg(a, 0));
                    set_selection(c, b, id, static_cast<double>(current.start),
                                  static_cast<double>(current.end), current.direction);
                    return value::undefined();
                });
        };
        selection_accessor(
            "selectionStart",
            [](context &, const text_selection & s) {
                return value::number(static_cast<double>(s.start));
            },
            [](context &, text_selection & s, value given) {
                if (given.is_null()) { return; }
                const double start = static_cast<double>(context::to_uint32(given));
                s.start = static_cast<std::size_t>(start);
                if (s.end < s.start) { s.end = s.start; }
            });
        selection_accessor(
            "selectionEnd",
            [](context &, const text_selection & s) {
                return value::number(static_cast<double>(s.end));
            },
            [](context &, text_selection & s, value given) {
                if (given.is_null()) { return; }
                s.end = static_cast<std::size_t>(context::to_uint32(given));
            });
        selection_accessor(
            "selectionDirection",
            [](context & c, const text_selection & s) { return c.string(s.direction); },
            [](context & c, text_selection & s, value given) {
                if (given.is_null()) { return; }
                s.direction = c.to_string(given);
            });
        operation(which, "setSelectionRange", 2,
                  [this, at, selection_applies, set_selection](context & c, std::span<value> a) {
                      const auto where = at(c);
                      dom_bindings * b = where.first;
                      const node_id id = where.second;
                      if (!id) { return value::undefined(); }
                      {
                          const auto txn = b->doc_->read();
                          if (!selection_applies(txn, b, id)) {
                              throw_dom_exception(
                                  c, "InvalidStateError",
                                  "setSelectionRange does not apply to this control");
                              return value::undefined();
                          }
                      }
                      const value direction = arg(a, 2);
                      set_selection(c, b, id, static_cast<double>(context::to_uint32(arg(a, 0))),
                                    static_cast<double>(context::to_uint32(arg(a, 1))),
                                    direction.is_undefined() ? std::string{"none"}
                                                             : c.to_string(direction));
                      return value::undefined();
                  });
        operation(which, "select", 0,
                  [at, selection_applies, set_selection](context & c, std::span<value>) {
                      const auto where = at(c);
                      dom_bindings * b = where.first;
                      const node_id id = where.second;
                      if (!id) { return value::undefined(); }
                      std::size_t length = 0;
                      {
                          const auto txn = b->doc_->read();
                          if (!selection_applies(txn, b, id)) { return value::undefined(); }
                          length = units_length_of(b->forms_->state_of(txn, *b->atoms_, id).value);
                      }
                      set_selection(c, b, id, 0, static_cast<double>(length), "none");
                      return value::undefined();
                  });
        // setRangeText(replacement, start?, end?, selectMode): replace the
        // code units [start, end) of the value and move the selection as the
        // mode says.
        operation(
            which, "setRangeText", 1,
            [this, at, selection_applies, selection_of, set_selection,
             store_value](context & c, std::span<value> a) {
                const auto where = at(c);
                dom_bindings * b = where.first;
                const node_id id = where.second;
                if (!id) { return value::undefined(); }
                if (a.empty()) {
                    c.throw_error("TypeError", "setRangeText needs a replacement string");
                    return value::undefined();
                }
                const std::string replacement = c.to_string(a[0]);
                text_selection current;
                std::string text;
                {
                    const auto txn = b->doc_->read();
                    if (!selection_applies(txn, b, id)) {
                        throw_dom_exception(c, "InvalidStateError",
                                            "setRangeText does not apply to this control");
                        return value::undefined();
                    }
                    current = selection_of(c, txn, b, id);
                    text = b->forms_->state_of(txn, *b->atoms_, id).value;
                }
                const std::size_t length = units_length_of(text);
                std::size_t start = current.start;
                std::size_t end = current.end;
                std::string mode = "preserve";
                if (a.size() >= 3) {
                    start = std::min(static_cast<std::size_t>(context::to_uint32(a[1])), length);
                    end = std::min(static_cast<std::size_t>(context::to_uint32(a[2])), length);
                    if (start > end) {
                        throw_dom_exception(c, "IndexSizeError", "setRangeText: start is past end");
                        return value::undefined();
                    }
                    if (a.size() >= 4 && !a[3].is_undefined()) { mode = c.to_string(a[3]); }
                    if (mode != "select" && mode != "start" && mode != "end" &&
                        mode != "preserve") {
                        c.throw_error("TypeError", "setRangeText: unknown selectMode");
                        return value::undefined();
                    }
                } else if (a.size() == 2) {
                    c.throw_error("TypeError", "setRangeText: `end` is required with `start`");
                    return value::undefined();
                }
                const std::size_t start_bytes = bytes_before(text, start);
                const std::size_t end_bytes = bytes_before(text, end);
                text.replace(start_bytes, end_bytes - start_bytes, replacement);
                const std::size_t new_length = units_length_of(replacement);
                const std::size_t new_end = start + new_length;
                std::size_t s = current.start;
                std::size_t e = current.end;
                if (mode == "select") {
                    s = start;
                    e = new_end;
                } else if (mode == "start") {
                    s = e = start;
                } else if (mode == "end") {
                    s = e = new_end;
                } else {
                    const auto shift = [&](std::size_t x) {
                        if (x > end) { return x + new_length - (end - start); }
                        if (x > start) { return new_end; }
                        return x;
                    };
                    const std::size_t old_start = s;
                    const std::size_t old_end = e;
                    s = shift(old_start);
                    e = shift(old_end);
                    if (old_start > start && old_start < end) { s = start; }
                    if (old_end > start && old_end < end) { e = new_end; }
                }
                {
                    const auto txn = b->doc_->read();
                    store_value(txn, b, id, text);
                }
                set_selection(c, b, id, static_cast<double>(s), static_cast<double>(e),
                              current.direction);
                b->mutated();
                return value::undefined();
            });
    }
}

} // namespace ctbrowser::shell
