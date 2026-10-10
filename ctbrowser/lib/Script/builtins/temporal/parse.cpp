//===- parse.cpp - Temporal string grammar ----------------------*- C++ -*-===//
//
// Temporal's string grammar (Temporal proposal sections 13.31-13.38): RFC 9557
// date-times with their annotations, UTC offsets, time zone identifiers and
// ISO 8601 durations. Bare section numbers below refer to the same
// specification.
//
// This is a recursive descent over one string: one function per nonterminal,
// each taking a cursor it advances only on success. The grammar is ambiguous
// only where the specification says so: a bare time against a month-day or
// year-month (13.31.3's early errors), which annotated_time resolves by trying
// the other two productions. Everywhere else the longest match is the only one
// that can reach the end of the string, since what may follow a time or an
// offset (a sign, a bracket, the end) never starts with a digit.
//
//===----------------------------------------------------------------------===//

#include "core.hpp"

#include <ctbrowser/core/algorithms.hpp>

#include <cmath>
#include <cstdlib>

namespace ctbrowser::script::temporal {

namespace {

/// A position in the string being parsed.
struct cursor {
    std::string_view text;
    std::size_t at = 0;

    [[nodiscard]] bool done() const noexcept { return at >= text.size(); }
    [[nodiscard]] char peek(std::size_t ahead = 0) const noexcept {
        return at + ahead < text.size() ? text[at + ahead] : '\0';
    }
    bool eat(char c) noexcept {
        if (peek() != c) { return false; }
        ++at;
        return true;
    }
    /// Consumes the designator letter \p upper in either case.
    bool eat_letter(char upper) noexcept {
        const char c = peek();
        if (c != upper && c != static_cast<char>(upper - 'A' + 'a')) { return false; }
        ++at;
        return true;
    }
};

[[nodiscard]] bool is_sign(char c) noexcept {
    return c == '+' || c == '-';
}

/// Reads exactly \p count digits as a number.
bool digits(cursor & c, int count, int & out) {
    int value = 0;
    for (int i = 0; i < count; ++i) {
        if (!is_ascii_digit(c.peek(static_cast<std::size_t>(i)))) { return false; }
        value = value * 10 + (c.peek(static_cast<std::size_t>(i)) - '0');
    }
    c.at += static_cast<std::size_t>(count);
    out = value;
    return true;
}

/// Hour: 00-23. MinuteSecond: 00-59; minute_second with \p highest 60 reads a
/// TimeSecond, which allows a leap second.
bool hour(cursor & c, int & out) {
    cursor probe = c;
    if (!digits(probe, 2, out) || out > 23) { return false; }
    c = probe;
    return true;
}
bool minute_second(cursor & c, int & out, int highest = 59) {
    cursor probe = c;
    if (!digits(probe, 2, out) || out > highest) { return false; }
    c = probe;
    return true;
}

/// TemporalDecimalFraction: '.' or ',' then one to nine digits, returned as text.
bool fraction(cursor & c, std::string & out) {
    if (c.peek() != '.' && c.peek() != ',') { return false; }
    std::size_t n = 0;
    while (is_ascii_digit(c.peek(1 + n))) { ++n; }
    if (n < 1 || n > 9) { return false; }
    out = std::string{c.text.substr(c.at + 1, n)};
    c.at += 1 + n;
    return true;
}

/// The digits of a fraction as nanoseconds (right-padded to nine).
[[nodiscard]] int fraction_nanoseconds(const std::string & digits_text) {
    std::string padded = digits_text + "000000000";
    padded.resize(9);
    return std::atoi(padded.c_str());
}

/// DateYear: four digits, or a sign and six (not -000000).
bool date_year(cursor & c, std::int64_t & out) {
    cursor probe = c;
    if (is_sign(probe.peek())) {
        const bool negative = probe.peek() == '-';
        ++probe.at;
        int value = 0;
        if (!digits(probe, 6, value)) { return false; }
        if (negative && value == 0) { return false; }
        out = negative ? -value : value;
    } else {
        int value = 0;
        if (!digits(probe, 4, value)) { return false; }
        out = value;
    }
    c = probe;
    return true;
}

bool date_month(cursor & c, int & out) {
    cursor probe = c;
    if (!digits(probe, 2, out) || out < 1 || out > 12) { return false; }
    c = probe;
    return true;
}
bool date_day(cursor & c, int & out) {
    cursor probe = c;
    if (!digits(probe, 2, out) || out < 1 || out > 31) { return false; }
    c = probe;
    return true;
}

/// IsValidMonthDay (13.31.1): rejects day 31 in a 30-day month and February
/// 30. February 29 in a non-leap year (IsValidDate, 13.31.2) is checked by date().
[[nodiscard]] bool valid_month_day(int month, int day) noexcept {
    if (day == 31 && (month == 2 || month == 4 || month == 6 || month == 9 || month == 11)) {
        return false;
    }
    return !(month == 2 && day == 30);
}

/// The date part of a parse; \c year is empty for a bare month-day.
struct date_fields {
    std::optional<std::int64_t> year;
    int month = 1;
    int day = 1;
    bool has_day = false;
};

/// Date: DateSpec[+Extended] | DateSpec[~Extended].
bool date(cursor & c, date_fields & out) {
    cursor probe = c;
    std::int64_t year = 0;
    int month = 0;
    int day = 0;
    if (!date_year(probe, year)) { return false; }
    const bool extended = probe.eat('-');
    if (!date_month(probe, month)) { return false; }
    if (extended && !probe.eat('-')) { return false; }
    if (!date_day(probe, day)) { return false; }
    if (!valid_month_day(month, day)) { return false; }
    if (month == 2 && day == 29 && !is_leap_year(year)) { return false; }
    out = date_fields{year, month, day, true};
    c = probe;
    return true;
}

/// DateSpecYearMonth: DateYear -? DateMonth.
bool date_spec_year_month(cursor & c, date_fields & out) {
    cursor probe = c;
    std::int64_t year = 0;
    int month = 0;
    if (!date_year(probe, year)) { return false; }
    (void)probe.eat('-');
    if (!date_month(probe, month)) { return false; }
    out = date_fields{year, month, 1, false};
    c = probe;
    return true;
}

/// DateSpecMonthDay: --? DateMonth -? DateDay.
bool date_spec_month_day(cursor & c, date_fields & out) {
    cursor probe = c;
    if (probe.peek() == '-' && probe.peek(1) == '-') { probe.at += 2; }
    int month = 0;
    int day = 0;
    if (!date_month(probe, month)) { return false; }
    (void)probe.eat('-');
    if (!date_day(probe, day)) { return false; }
    if (!valid_month_day(month, day)) { return false; }
    out = date_fields{std::nullopt, month, day, true};
    c = probe;
    return true;
}

/// Time: TimeSpec[+Extended] | TimeSpec[~Extended], longest match.
bool time_spec(cursor & c, time_record & out) {
    cursor probe = c;
    time_record t;
    if (!hour(probe, t.hour)) { return false; }
    const bool extended = probe.peek() == ':';
    cursor after_hour = probe;
    if (extended) { ++probe.at; }
    if (minute_second(probe, t.minute)) {
        cursor after_minute = probe;
        if (extended && !probe.eat(':')) {
            probe = after_minute;
        } else if (int second = 0; minute_second(probe, second, 60)) {
            // A leap second (:60) parses and is clamped to :59, as Temporal does.
            t.second = second == 60 ? 59 : second;
            std::string digits_text;
            if (fraction(probe, digits_text)) {
                const int ns = fraction_nanoseconds(digits_text);
                t.millisecond = ns / 1'000'000;
                t.microsecond = ns / 1000 % 1000;
                t.nanosecond = ns % 1000;
            }
        } else {
            probe = after_minute;
        }
    } else {
        probe = after_hour; // "12:" is not a time; "12" is.
    }
    out = t;
    c = probe;
    return true;
}

/// UTCOffset[SubMinutePrecision]: the sign, the hour, and optionally minutes
/// (and, with \p sub_minute, seconds and a fraction), either all separated by
/// colons or none of them. \p out is the offset in nanoseconds.
bool utc_offset(cursor & c, bool sub_minute, i128 & out) {
    cursor probe = c;
    if (!is_sign(probe.peek())) { return false; }
    const int sign = probe.peek() == '-' ? -1 : 1;
    ++probe.at;
    int h = 0;
    if (!hour(probe, h)) { return false; }
    int m = 0;
    int s = 0;
    int ns = 0;
    const bool extended = probe.peek() == ':';
    cursor after_hour = probe;
    if (extended) { ++probe.at; }
    if (minute_second(probe, m)) {
        cursor after_minute = probe;
        if (sub_minute && (!extended || probe.eat(':')) && minute_second(probe, s)) {
            std::string digits_text;
            if (fraction(probe, digits_text)) { ns = fraction_nanoseconds(digits_text); }
        } else {
            probe = after_minute;
            s = 0;
        }
    } else {
        probe = after_hour;
    }
    out = sign * ((((i128{h} * 60 + m) * 60 + s) * 1'000'000'000) + ns);
    c = probe;
    return true;
}

/// TimeZoneIANAName: components of TZLeadingChar TZChar*, joined by '/'.
bool iana_name(cursor & c) {
    cursor probe = c;
    for (;;) {
        const char lead = probe.peek();
        if (!is_ascii_alpha(lead) && lead != '.' && lead != '_') { return false; }
        ++probe.at;
        for (char ch = probe.peek(); is_ascii_alpha(ch) || is_ascii_digit(ch) || ch == '.' ||
                                     ch == '_' || ch == '-' || ch == '+';
             ch = probe.peek()) {
            ++probe.at;
        }
        if (!probe.eat('/')) { break; }
    }
    c = probe;
    return true;
}

/// TimeZoneIdentifier: UTCOffset[~SubMinutePrecision] | TimeZoneIANAName.
bool time_zone_identifier(cursor & c) {
    i128 ignored = 0;
    cursor probe = c;
    if (utc_offset(probe, false, ignored)) {
        c = probe;
        return true;
    }
    return iana_name(c);
}

/// One bracketed annotation, "[key=value]", critical when written "[!key=value]".
struct annotation {
    bool critical = false;
    std::string key;
    std::string value;
};

/// The annotations after a date-time.
struct annotated {
    std::string time_zone; ///< The TimeZoneAnnotation's identifier, or empty.
    bool has_time_zone = false;
    std::vector<annotation> annotations;
};

/// One Annotation. AnnotationKey: [a-z_][a-z_0-9-]*. AnnotationValue:
/// alnum+ (- alnum+)*.
bool annotation_item(cursor & c, annotation & out) {
    cursor probe = c;
    if (!probe.eat('[')) { return false; }
    out.critical = probe.eat('!');
    const std::size_t key_start = probe.at;
    const auto key_lead = [](char ch) { return (ch >= 'a' && ch <= 'z') || ch == '_'; };
    if (!key_lead(probe.peek())) { return false; }
    while (key_lead(probe.peek()) || is_ascii_digit(probe.peek()) || probe.peek() == '-') {
        ++probe.at;
    }
    out.key = std::string{probe.text.substr(key_start, probe.at - key_start)};
    if (!probe.eat('=')) { return false; }
    const std::size_t value_start = probe.at;
    for (;;) {
        std::size_t n = 0;
        while (is_ascii_alpha(probe.peek()) || is_ascii_digit(probe.peek())) {
            ++probe.at;
            ++n;
        }
        if (n == 0) { return false; }
        if (!probe.eat('-')) { break; }
    }
    out.value = std::string{probe.text.substr(value_start, probe.at - value_start)};
    if (!probe.eat(']')) { return false; }
    c = probe;
    return true;
}

/// TimeZoneAnnotation? Annotations?: the time zone first, if it is there.
bool annotations(cursor & c, annotated & out, bool zone_required) {
    cursor probe = c;
    if (probe.peek() == '[') {
        cursor zone = probe;
        ++zone.at;
        (void)zone.eat('!');
        const std::size_t start = zone.at;
        if (time_zone_identifier(zone) && zone.peek() == ']') {
            out.time_zone = std::string{zone.text.substr(start, zone.at - start)};
            out.has_time_zone = true;
            ++zone.at;
            probe = zone;
        }
    }
    if (zone_required && !out.has_time_zone) { return false; }
    while (probe.peek() == '[') {
        annotation item;
        if (!annotation_item(probe, item)) { return false; }
        out.annotations.push_back(std::move(item));
    }
    c = probe;
    return true;
}

/// DateTimeUTCOffset[Z]: with [+Z] a Z designator, or UTCOffset[+SubMinutePrecision].
struct offset_part {
    bool z = false;
    std::string text; ///< The offset's source text.
};
bool date_time_utc_offset(cursor & c, bool allow_z, offset_part & out) {
    if (allow_z && (c.peek() == 'Z' || c.peek() == 'z')) {
        ++c.at;
        out.z = true;
        return true;
    }
    const std::size_t start = c.at;
    i128 ignored = 0;
    if (!utc_offset(c, true, ignored)) { return false; }
    out.text = std::string{c.text.substr(start, c.at - start)};
    return true;
}

/// Everything one goal's parse found, before parse_iso_datetime checks it.
struct parsed {
    date_fields date;
    std::optional<time_record> time;
    offset_part offset;
    annotated notes;
};

/// AnnotatedDateTime[Zoned, TimeRequired], or TemporalInstantString when
/// \p instant is set (a time and an offset required, Z allowed).
bool annotated_date_time(cursor & c, bool zoned, bool time_required, bool instant, parsed & out) {
    cursor probe = c;
    if (!date(probe, out.date)) { return false; }
    const char separator = probe.peek();
    if (separator == 'T' || separator == 't' || separator == ' ') {
        cursor timed = probe;
        ++timed.at;
        time_record t;
        if (time_spec(timed, t)) {
            out.time = t;
            probe = timed;
            (void)date_time_utc_offset(probe, zoned || instant, out.offset);
        }
    }
    if (time_required && !out.time) { return false; }
    if (instant && !out.offset.z && out.offset.text.empty()) { return false; }
    if (!annotations(probe, out.notes, zoned)) { return false; }
    c = probe;
    return true;
}

/// Whether the whole of \p text is a DateSpecMonthDay or a DateSpecYearMonth:
/// the early errors that keep "1214" and "2021-12" from being times (13.31.3).
[[nodiscard]] bool is_month_day_or_year_month(std::string_view text) {
    date_fields ignored;
    cursor a{text};
    if (date_spec_month_day(a, ignored) && a.done()) { return true; }
    cursor b{text};
    return date_spec_year_month(b, ignored) && b.done();
}

/// AnnotatedTime: T? Time DateTimeUTCOffset[~Z]? annotations.
bool annotated_time(cursor & c, parsed & out) {
    cursor probe = c;
    const bool designated = probe.eat_letter('T');
    const std::size_t start = probe.at;
    time_record t;
    if (!time_spec(probe, t)) { return false; }
    (void)date_time_utc_offset(probe, false, out.offset);
    if (!designated && is_month_day_or_year_month(probe.text.substr(start, probe.at - start))) {
        return false;
    }
    out.time = t;
    if (!annotations(probe, out.notes, false)) { return false; }
    c = probe;
    return true;
}

/// Parses the whole of \p text as the production(s) goal \p g names.
bool parse_goal(std::string_view text, goal g, parsed & out) {
    const auto whole = [&](auto && production) {
        parsed attempt;
        cursor c{text};
        if (!production(c, attempt) || !c.done()) { return false; }
        out = std::move(attempt);
        return true;
    };
    const auto date_time = [](bool zoned, bool time_required) {
        return [zoned, time_required](cursor & c, parsed & p) {
            return annotated_date_time(c, zoned, time_required, false, p);
        };
    };
    switch (g) {
    case goal::date_time_zoned: return whole(date_time(true, false));
    case goal::date_time: return whole(date_time(false, false));
    case goal::instant:
        return whole(
            [](cursor & c, parsed & p) { return annotated_date_time(c, false, true, true, p); });
    case goal::time: return whole(annotated_time) || whole(date_time(false, true));
    case goal::month_day:
        return whole([](cursor & c, parsed & p) {
                   return date_spec_month_day(c, p.date) && annotations(c, p.notes, false);
               }) ||
               whole(date_time(false, false));
    case goal::year_month:
        return whole([](cursor & c, parsed & p) {
                   return date_spec_year_month(c, p.date) && annotations(c, p.notes, false);
               }) ||
               whole(date_time(false, false));
    }
    return false;
}

} // namespace

std::optional<parse_record> parse_iso_datetime(std::string_view text,
                                               std::initializer_list<goal> goals) {
    for (const goal g : goals) {
        parsed result;
        if (!parse_goal(text, g, result)) { continue; }
        // ParseISODateTime step 4.b: the first calendar annotation is used. A
        // second one is an error if either is marked critical (`[!...]`), and
        // an unknown annotation is an error if it is critical.
        std::string calendar;
        bool calendar_critical = false;
        bool have_calendar = false;
        for (const annotation & item : result.notes.annotations) {
            if (item.key == "u-ca") {
                if (!have_calendar) {
                    calendar = item.value;
                    have_calendar = true;
                    calendar_critical = item.critical;
                } else if (item.critical || calendar_critical) {
                    return std::nullopt;
                }
            } else if (item.critical) {
                return std::nullopt;
            }
        }
        const bool iso_calendar = !have_calendar || ascii_iequals(calendar, "iso8601");
        if (g == goal::year_month && !result.date.has_day && !iso_calendar) { return std::nullopt; }
        if (g == goal::month_day && !result.date.year && !iso_calendar) { return std::nullopt; }
        parse_record record;
        // A month-day string's year is absent only when the goal found none.
        record.year = result.date.year;
        record.month = result.date.month;
        record.day = result.date.day;
        record.time = result.time;
        record.z = result.offset.z;
        record.offset = result.offset.text;
        record.annotation = result.notes.time_zone;
        record.calendar = calendar;
        return record;
    }
    return std::nullopt;
}

std::optional<i128> parse_utc_offset(std::string_view text, bool sub_minute) {
    cursor c{text};
    i128 ns = 0;
    if (!utc_offset(c, sub_minute, ns) || !c.done()) { return std::nullopt; }
    return ns;
}

bool is_offset_time_zone_identifier(std::string_view id) {
    return parse_utc_offset(id, false).has_value();
}

std::optional<time_zone_parse> parse_time_zone_identifier(std::string_view id) {
    cursor offset{id};
    i128 ns = 0;
    if (utc_offset(offset, false, ns) && offset.done()) {
        return time_zone_parse{{}, static_cast<int>(ns / 60'000'000'000)};
    }
    cursor name{id};
    if (iana_name(name) && name.done()) { return time_zone_parse{std::string{id}, std::nullopt}; }
    return std::nullopt;
}

std::optional<std::string> parse_temporal_calendar_string(std::string_view text) {
    if (const auto record =
            parse_iso_datetime(text, {goal::date_time_zoned, goal::date_time, goal::instant,
                                      goal::time, goal::month_day, goal::year_month})) {
        return record->calendar.empty() ? std::string{"iso8601"} : record->calendar;
    }
    // A bare AnnotationValue: alnum+ (- alnum+)*.
    std::size_t n = 0;
    for (std::size_t i = 0; i <= text.size(); ++i) {
        if (i == text.size() || text[i] == '-') {
            if (n == 0) { return std::nullopt; }
            n = 0;
        } else if (is_ascii_alpha(text[i]) || is_ascii_digit(text[i])) {
            ++n;
        } else {
            return std::nullopt;
        }
    }
    return std::string{text};
}

std::optional<time_zone_parse> parse_temporal_time_zone_string(std::string_view text) {
    if (auto direct = parse_time_zone_identifier(text)) { return direct; }
    const auto record =
        parse_iso_datetime(text, {goal::date_time_zoned, goal::date_time, goal::instant, goal::time,
                                  goal::month_day, goal::year_month});
    if (!record) { return std::nullopt; }
    if (!record->annotation.empty()) { return parse_time_zone_identifier(record->annotation); }
    if (record->z) { return parse_time_zone_identifier("UTC"); }
    if (!record->offset.empty()) { return parse_time_zone_identifier(record->offset); }
    return std::nullopt;
}

/// ISO 8601 durations (13.37). Each value is the source's digits read as a
/// Number, as ToIntegerWithTruncation reads them; a fraction on the smallest
/// unit present is spread down the smaller ones exactly, as nanoseconds.
std::optional<duration_record> parse_temporal_duration_string(std::string_view text) {
    cursor c{text};
    int factor = 1;
    if (is_sign(c.peek())) {
        factor = c.peek() == '-' ? -1 : 1;
        ++c.at;
    }
    if (!c.eat_letter('P')) { return std::nullopt; }
    // Digits then a designator; `frac` receives a fraction when one is allowed.
    const auto number = [&](cursor & at, std::string & whole, std::string * frac) {
        const std::size_t start = at.at;
        while (is_ascii_digit(at.peek())) { ++at.at; }
        if (at.at == start) { return false; }
        whole = std::string{at.text.substr(start, at.at - start)};
        if (frac != nullptr) { (void)fraction(at, *frac); }
        return true;
    };
    const auto to_number = [](const std::string & d) -> std::optional<double> {
        if (d.empty()) { return 0.0; }
        const double v = std::strtod(d.c_str(), nullptr);
        if (!std::isfinite(v)) { return std::nullopt; }
        return v;
    };
    std::string years, months, weeks, days, hours, minutes, seconds;
    std::string f_hours, f_minutes, f_seconds;
    bool any_date = false;
    const auto date_part = [&](char designator, std::string & into) {
        cursor probe = c;
        std::string digits_text;
        if (!number(probe, digits_text, nullptr) || !probe.eat_letter(designator)) { return false; }
        into = digits_text;
        c = probe;
        any_date = true;
        return true;
    };
    (void)date_part('Y', years);
    (void)date_part('M', months);
    (void)date_part('W', weeks);
    (void)date_part('D', days);
    bool any_time = false;
    if (c.eat_letter('T')) {
        const auto time_part = [&](char designator, std::string & into, std::string & frac) {
            cursor probe = c;
            std::string digits_text;
            std::string fraction_text;
            if (!number(probe, digits_text, &fraction_text) || !probe.eat_letter(designator)) {
                return false;
            }
            into = digits_text;
            frac = fraction_text;
            c = probe;
            any_time = true;
            return true;
        };
        if (time_part('H', hours, f_hours) && !f_hours.empty()) {
            // A fractional hour ends the duration.
        } else {
            if (time_part('M', minutes, f_minutes) && !f_minutes.empty()) {
                // A fractional minute ends it.
            } else {
                (void)time_part('S', seconds, f_seconds);
            }
        }
        if (!any_time) { return std::nullopt; }
    }
    if (!c.done() || (!any_date && !any_time)) { return std::nullopt; }
    const auto y = to_number(years), mo = to_number(months), w = to_number(weeks),
               d = to_number(days), h = to_number(hours);
    if (!y || !mo || !w || !d || !h) { return std::nullopt; }
    double minutes_v = 0, seconds_v = 0, ms_v = 0, us_v = 0, ns_v = 0;
    // The fraction's nanoseconds of the unit it is on, split down the rest.
    const auto spread = [&](i128 fraction_ns, bool into_minutes, bool into_seconds) {
        if (into_minutes) { minutes_v = double(fraction_ns / 60'000'000'000); }
        if (into_seconds) { seconds_v = double(fraction_ns / 1'000'000'000 % 60); }
        ms_v = double(fraction_ns / 1'000'000 % 1000);
        us_v = double(fraction_ns / 1000 % 1000);
        ns_v = double(fraction_ns % 1000);
    };
    const auto fraction_of = [](const std::string & f, i128 unit_ns) {
        i128 scale = 1;
        for (std::size_t i = 0; i < f.size(); ++i) { scale *= 10; }
        return i128{std::atoi(f.c_str())} * unit_ns / scale;
    };
    if (!f_hours.empty()) {
        spread(fraction_of(f_hours, 3'600'000'000'000), true, true);
    } else {
        const auto m = to_number(minutes);
        if (!m) { return std::nullopt; }
        minutes_v = *m;
        if (!f_minutes.empty()) {
            spread(fraction_of(f_minutes, 60'000'000'000), false, true);
        } else {
            const auto s = to_number(seconds);
            if (!s) { return std::nullopt; }
            seconds_v = *s;
            if (!f_seconds.empty()) { spread(fraction_of(f_seconds, 1'000'000'000), false, false); }
        }
    }
    const auto signed_value = [factor](double v) { return v == 0 ? 0.0 : v * factor; };
    duration_record result{signed_value(*y),        signed_value(*mo),  signed_value(*w),
                           signed_value(*d),        signed_value(*h),   signed_value(minutes_v),
                           signed_value(seconds_v), signed_value(ms_v), signed_value(us_v),
                           signed_value(ns_v)};
    if (!is_valid_duration(result)) { return std::nullopt; }
    return result;
}

} // namespace ctbrowser::script::temporal
