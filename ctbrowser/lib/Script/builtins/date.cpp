// Split from values.cpp: date.
#include "internal.hpp"
#include "text/internal.hpp"
#include <chrono>
#include <format>
#include <numbers>

namespace ctbrowser::script::builtins_detail {

// Date
// `Date` - CONSTRUCTIBLE, and reading a calendar out of a millisecond count.
//
// It was a namespace with `now()` on it and nothing else, so `new Date()` was
// "Date is not a function". p5 exposes day()/month()/year()/hour() and every
// one of them builds a Date, so a sketch showing a clock - which is most
// beginners' second sketch - failed on its first line.
//
// UTC only, and no parsing: `new Date(string)` is a calendar and a timezone
// database, which is a different project. What is here is the civil date
// arithmetic that turns a millisecond count into fields and back, which is what
// a page reading the clock actually needs.
// --- Date, 21.4 -------------------------------------------------------------
//
// NO TIME ZONE: this engine has none, so local time IS UTC and every local
// method answers what its UTC twin does; getTimezoneOffset says 0 and the
// string forms say "GMT+0000 (Coordinated Universal Time)". The time value is
// the `__ms` slot, a non-enumerable own property, NaN for an invalid date.

namespace {

constexpr double ms_per_day = 86400000.0;
[[nodiscard]] constexpr bool is_digit(char c) noexcept {
    return c >= '0' && c <= '9';
}
[[nodiscard]] constexpr bool is_alpha(char c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}
constexpr std::string_view invalid_date_slot = "__ms";

// 21.4.1.31 TimeClip.
[[nodiscard]] double time_clip(double t) {
    if (!std::isfinite(t) || std::fabs(t) > 8.64e15) { return std::nan(""); }
    return std::trunc(t) + 0.0; // -0 becomes +0
}

// Days since the epoch <-> y/m/d, proleptic Gregorian, on 64-bit integers
// (Hinnant's algorithms). NOT <chrono>: std::chrono::year stops at +-32767,
// and a time value reaches year +-273790.
[[nodiscard]] long long days_from_civil(long long y, int m, int d) {
    y -= m <= 2;
    const long long era = (y >= 0 ? y : y - 399) / 400;
    const long long yoe = y - era * 400;
    const long long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const long long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}
void civil_from_days(long long z, long long & y, int & m, int & d) {
    z += 719468;
    const long long era = (z >= 0 ? z : z - 146096) / 146097;
    const long long doe = z - era * 146097;
    const long long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    y = yoe + era * 400;
    const long long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const long long mp = (5 * doy + 2) / 153;
    d = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
    m = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
    y += m <= 2;
}

// 21.4.1.28 MakeDay / 21.4.1.29 MakeTime / 21.4.1.30 MakeDate, on doubles: an
// out-of-range month or day normalises (`new Date(2024, 12, 1)` is 1 January
// 2025), a non-finite part is NaN.
[[nodiscard]] double make_date(double year, double month, double day, double h, double m, double s,
                               double ms) {
    for (const double v : {year, month, day, h, m, s, ms}) {
        if (!std::isfinite(v)) { return std::nan(""); }
    }
    // MakeTime FIRST and in ITS order - ((h + m) + s) + milli, each product
    // rounded as IEEE double - because test262's fp-evaluation-order.js
    // asserts the exact rounding of `Date.UTC(1970, 0, 1, 80063993375, 29, 1,
    // -288230376151711740)`, and MakeDate is then day * msPerDay + time.
    const double time =
        ((std::trunc(h) * 3600000.0 + std::trunc(m) * 60000.0) + std::trunc(s) * 1000.0) +
        std::trunc(ms);
    const double y = std::trunc(year), mo = std::trunc(month);
    const double ym = y + std::floor(mo / 12.0);
    const int mn = static_cast<int>(mo - std::floor(mo / 12.0) * 12.0);
    // A year past 2^40 has no time value TimeClip could keep, however the
    // day count is offset; days_from_civil's arithmetic stays in range there.
    if (std::fabs(ym) > 1099511627776.0) { return std::nan(""); }
    const double days =
        static_cast<double>(days_from_civil(static_cast<long long>(ym), mn + 1, 1)) +
        std::trunc(day) - 1.0;
    if (!std::isfinite(days)) { return std::nan(""); }
    const double tv = days * ms_per_day + time;
    return std::isfinite(tv) ? tv : std::nan("");
}

struct fields {
    double year, month /* 0-11 */, day, hour, minute, second, ms, weekday /* 0 = Sunday */;
};
[[nodiscard]] fields split(double t) {
    fields out{};
    const double days = std::floor(t / ms_per_day);
    const double rest = t - days * ms_per_day;
    long long y = 0;
    int m = 0, d = 0;
    civil_from_days(static_cast<long long>(days), y, m, d);
    out.year = static_cast<double>(y);
    out.month = m - 1;
    out.day = d;
    out.hour = std::floor(rest / 3600000.0);
    out.minute = std::fmod(std::floor(rest / 60000.0), 60.0);
    out.second = std::fmod(std::floor(rest / 1000.0), 60.0);
    out.ms = std::fmod(rest, 1000.0);
    out.weekday = std::fmod(std::fmod(days + 4.0, 7.0) + 7.0, 7.0); // 1970-01-01 was a Thursday
    return out;
}

constexpr const char * day_names[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
constexpr const char * month_names[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

// 21.4.4.41.2 DateString / 21.4.4.43 step 8: a sign for a negative year and
// AT LEAST four digits either way - "-0001", not the ISO form's "-000001".
[[nodiscard]] std::string year_text(double year) {
    return year < 0 ? std::format("-{:04}", -static_cast<int>(year))
                    : std::format("{:04}", static_cast<int>(year));
}
// 21.4.4.41.2 DateString: "Fri Feb 13 2009".
[[nodiscard]] std::string date_string(const fields & f) {
    return std::format("{} {} {:02} {}", day_names[static_cast<int>(f.weekday)],
                       month_names[static_cast<int>(f.month)], static_cast<int>(f.day),
                       year_text(f.year));
}
// 21.4.4.41.1 TimeString + 21.4.4.41.3 TimeZoneString.
[[nodiscard]] std::string time_string(const fields & f) {
    return std::format("{:02}:{:02}:{:02} GMT+0000 (Coordinated Universal Time)",
                       static_cast<int>(f.hour), static_cast<int>(f.minute),
                       static_cast<int>(f.second));
}
// 21.4.4.43 toUTCString: "Fri, 13 Feb 2009 23:31:30 GMT".
[[nodiscard]] std::string utc_string(const fields & f) {
    return std::format(
        "{}, {:02} {} {} {:02}:{:02}:{:02} GMT", day_names[static_cast<int>(f.weekday)],
        static_cast<int>(f.day), month_names[static_cast<int>(f.month)], year_text(f.year),
        static_cast<int>(f.hour), static_cast<int>(f.minute), static_cast<int>(f.second));
}
[[nodiscard]] std::string iso_string(const fields & f) {
    const int y = static_cast<int>(f.year);
    const std::string year = y >= 0 && y <= 9999 ? std::format("{:04}", y)
                             : y < 0             ? std::format("-{:06}", -y)
                                                 : std::format("+{:06}", y);
    return std::format("{}-{:02}-{:02}T{:02}:{:02}:{:02}.{:03}Z", year,
                       static_cast<int>(f.month) + 1, static_cast<int>(f.day),
                       static_cast<int>(f.hour), static_cast<int>(f.minute),
                       static_cast<int>(f.second), static_cast<int>(f.ms));
}

// 21.4.3.2 Date.parse: the Date Time String Format (21.4.1.32) - `YYYY`,
// `YYYY-MM`, `YYYY-MM-DD`, each with an optional `THH:mm[:ss[.sss]]` and `Z`
// or `+HH:mm` - and the two forms toString and toUTCString produce. Anything
// else is NaN; a browser's tolerant fallbacks are not specified.
[[nodiscard]] double parse_date(std::string_view s) {
    std::size_t i = 0;
    const auto digits = [&](std::size_t n, double & out) {
        if (i + n > s.size()) { return false; }
        double v = 0;
        for (std::size_t k = 0; k < n; ++k) {
            if (!is_digit(s[i + k])) { return false; }
            v = v * 10 + (s[i + k] - '0');
        }
        i += n;
        out = v;
        return true;
    };
    const auto eat = [&](char ch) {
        if (i < s.size() && s[i] == ch) {
            ++i;
            return true;
        }
        return false;
    };
    // The ISO form.
    double year = 0, month = 1, day = 1, hour = 0, minute = 0, second = 0, ms = 0, offset = 0;
    bool iso = false;
    if (eat('+') || eat('-')) {
        const bool negative = s[i - 1] == '-';
        iso = digits(6, year);
        if (iso && negative) {
            if (year == 0) { return std::nan(""); }
            year = -year;
        }
    } else {
        iso = digits(4, year);
    }
    if (iso) {
        bool has_time = false;
        if (eat('-')) {
            if (!digits(2, month)) { return std::nan(""); }
            if (eat('-') && !digits(2, day)) { return std::nan(""); }
        }
        if (eat('T') || eat('t') || (i < s.size() && s[i] == ' ' && ++i)) {
            has_time = true;
            if (!digits(2, hour) || !eat(':') || !digits(2, minute)) { return std::nan(""); }
            if (eat(':')) {
                if (!digits(2, second)) { return std::nan(""); }
                if (eat('.')) {
                    std::size_t n = 0;
                    double frac = 0;
                    while (i < s.size() && is_digit(s[i])) {
                        if (n < 3) { frac = frac * 10 + (s[i] - '0'); }
                        ++n;
                        ++i;
                    }
                    if (n == 0) { return std::nan(""); }
                    for (; n < 3; ++n) { frac *= 10; }
                    ms = frac;
                }
            }
        }
        if (eat('Z') || eat('z')) {
            if (!has_time) { return std::nan(""); }
        } else if (i < s.size() && (s[i] == '+' || s[i] == '-')) {
            const double sign = s[i] == '-' ? -1 : 1;
            ++i;
            double oh = 0, om = 0;
            if (!digits(2, oh) || !eat(':') || !digits(2, om)) { return std::nan(""); }
            offset = sign * (oh * 60 + om) * 60000.0;
        }
        if (i != s.size() || month < 1 || month > 12 || day < 1 || day > 31 || hour > 24 ||
            minute > 59 || second > 59 || (hour == 24 && (minute != 0 || second != 0 || ms != 0))) {
            return std::nan("");
        }
        return time_clip(make_date(year, month - 1, day, hour, minute, second, ms) - offset);
    }
    // "Fri Feb 13 2009 23:31:30 GMT+0000 (...)" and "Fri, 13 Feb 2009 23:31:30 GMT".
    const auto word = [&] {
        std::string w;
        while (i < s.size() && is_alpha(s[i])) { w += s[i++]; }
        return w;
    };
    const auto number = [&](double & out) {
        double v = 0;
        std::size_t n = 0;
        while (i < s.size() && is_digit(s[i])) {
            v = v * 10 + (s[i++] - '0');
            ++n;
        }
        out = v;
        return n > 0;
    };
    const auto month_of = [&](const std::string & w) {
        for (int m = 0; m < 12; ++m) {
            if (w == month_names[m]) { return m; }
        }
        return -1;
    };
    const auto spaces = [&] {
        while (i < s.size() && s[i] == ' ') { ++i; }
    };
    std::string first = word();
    (void)eat(',');
    spaces();
    int mon = -1;
    if (i < s.size() && is_digit(s[i])) {
        // "13 Feb 2009"
        if (!number(day)) { return std::nan(""); }
        spaces();
        mon = month_of(word());
    } else {
        // "Feb 13 2009" - `first` was the weekday, or the month itself.
        mon = month_of(first);
        if (mon < 0) { mon = month_of(word()); }
        spaces();
        if (!number(day)) { return std::nan(""); }
    }
    spaces();
    if (mon < 0 || !number(year)) { return std::nan(""); }
    spaces();
    if (i < s.size() && is_digit(s[i])) {
        if (!number(hour) || !eat(':') || !number(minute)) { return std::nan(""); }
        if (eat(':') && !number(second)) { return std::nan(""); }
        spaces();
        if (s.substr(i, 3) == "GMT" || s.substr(i, 3) == "UTC") {
            i += 3;
            if (i < s.size() && (s[i] == '+' || s[i] == '-')) {
                const double sign = s[i] == '-' ? -1 : 1;
                ++i;
                double hhmm = 0;
                if (!number(hhmm)) { return std::nan(""); }
                offset = sign * (std::floor(hhmm / 100) * 60 + std::fmod(hhmm, 100)) * 60000.0;
            }
        }
    }
    return time_clip(make_date(year, mon, day, hour, minute, second, 0) - offset);
}

} // namespace

void install_date(context & cx) {
    using detail::method;
    using detail::new_table;

    object_object * date_proto = new_table(cx);

    // thisTimeValue (21.4.4): the slot, or a TypeError for anything that is
    // not a Date. NaN is a valid answer - an invalid date is still a Date.
    const auto this_time = [](context & c, const char * name, double & out) {
        const value self = c.current_this();
        if (self.is_object()) {
            if (const value * held =
                    static_cast<object_object *>(self.as_heap())->find(invalid_date_slot)) {
                out = held->as_number();
                return true;
            }
        }
        c.throw_error("TypeError", std::string{name} + " called on a non-Date");
        return false;
    };
    const auto set_time = [](context & c, double t) {
        static_cast<object_object *>(c.current_this().as_heap())
            ->set(invalid_date_slot, value::number(t));
        return value::number(t);
    };

    // The getters: a field of the split time, NaN for an invalid date. Local
    // and UTC are the same function body under two names.
    const auto getter = [&](std::string name, double fields::* which, double adjust) {
        method(cx, date_proto, name, 0,
               [this_time, which, adjust, name](context & c, std::span<value>) {
                   double t = 0;
                   if (!this_time(c, name.c_str(), t)) { return value::undefined(); }
                   if (std::isnan(t)) { return value::number(t); }
                   return value::number(split(t).*which + adjust);
               });
    };
    for (const char * prefix : {"get", "getUTC"}) {
        const std::string p = prefix;
        getter(p + "FullYear", &fields::year, 0);
        getter(p + "Month", &fields::month, 0);
        getter(p + "Date", &fields::day, 0);
        getter(p + "Day", &fields::weekday, 0);
        getter(p + "Hours", &fields::hour, 0);
        getter(p + "Minutes", &fields::minute, 0);
        getter(p + "Seconds", &fields::second, 0);
        getter(p + "Milliseconds", &fields::ms, 0);
    }
    getter("getYear", &fields::year, -1900); // B.2.3.1
    method(cx, date_proto, "getTime", 0, [this_time](context & c, std::span<value>) {
        double t = 0;
        return this_time(c, "Date.prototype.getTime", t) ? value::number(t) : value::undefined();
    });
    method(cx, date_proto, "valueOf", 0, [this_time](context & c, std::span<value>) {
        double t = 0;
        return this_time(c, "Date.prototype.valueOf", t) ? value::number(t) : value::undefined();
    });
    // No timezone here, so the local getters ARE the UTC ones and say so rather
    // than pretending to a zone this engine does not have.
    method(cx, date_proto, "getTimezoneOffset", 0, [this_time](context & c, std::span<value>) {
        double t = 0;
        if (!this_time(c, "Date.prototype.getTimezoneOffset", t)) { return value::undefined(); }
        return value::number(std::isnan(t) ? t : 0.0);
    });

    // The setters (21.4.4.20-21.4.4.34): the arguments replace `count` fields
    // from `first` on - only as many as were supplied - and the date is
    // rebuilt through MakeDate and TimeClip. Every argument is ToNumber'd
    // BEFORE the time is examined (a valueOf that changes the date runs first).
    const auto setter = [&](std::string name, int first, int count, double arity) {
        method(cx, date_proto, name, arity,
               [this_time, set_time, first, count, name](context & c, std::span<value> a) {
                   double t = 0;
                   if (!this_time(c, name.c_str(), t)) { return value::undefined(); }
                   double parts[7] = {};
                   const int supplied = static_cast<int>(
                       std::min<std::size_t>(a.size(), static_cast<std::size_t>(count)));
                   for (int k = 0; k < std::max(supplied, 1); ++k) {
                       if (!numeric_arg(c, arg_at(a, static_cast<std::size_t>(k)))) {
                           return value::undefined();
                       }
                       parts[k] = c.to_number_value(arg_at(a, static_cast<std::size_t>(k)));
                       if (c.throw_pending()) { return value::undefined(); }
                   }
                   // 21.4.4.21 step 4: setFullYear on an invalid date starts from +0;
                   // every other setter of an invalid date stays NaN.
                   if (std::isnan(t)) {
                       if (first != 0) { return value::number(t); }
                       t = 0;
                   }
                   const fields f = split(t);
                   double all[7] = {f.year, f.month, f.day, f.hour, f.minute, f.second, f.ms};
                   for (int k = 0; k < std::max(supplied, 1); ++k) { all[first + k] = parts[k]; }
                   return set_time(c, time_clip(make_date(all[0], all[1], all[2], all[3], all[4],
                                                          all[5], all[6])));
               });
    };
    for (const char * prefix : {"set", "setUTC"}) {
        const std::string p = prefix;
        setter(p + "FullYear", 0, 3, 3);
        setter(p + "Month", 1, 2, 2);
        setter(p + "Date", 2, 1, 1);
        setter(p + "Hours", 3, 4, 4);
        setter(p + "Minutes", 4, 3, 3);
        setter(p + "Seconds", 5, 2, 2);
        setter(p + "Milliseconds", 6, 1, 1);
    }
    // B.2.3.2 setYear: MakeFullYear of the argument (0-99 is 1900-1999, NaN
    // stays NaN) over an invalid date's +0.
    method(cx, date_proto, "setYear", 1, [this_time, set_time](context & c, std::span<value> a) {
        double t = 0;
        if (!this_time(c, "Date.prototype.setYear", t)) { return value::undefined(); }
        if (!numeric_arg(c, arg_at(a, 0))) { return value::undefined(); }
        const double y = c.to_number_value(arg_at(a, 0));
        if (c.throw_pending()) { return value::undefined(); }
        if (std::isnan(y)) { return set_time(c, std::nan("")); }
        const double whole = std::trunc(y);
        const double yyyy = whole >= 0 && whole <= 99 ? 1900 + whole : y;
        const fields f = split(std::isnan(t) ? 0.0 : t);
        return set_time(
            c, time_clip(make_date(yyyy, f.month, f.day, f.hour, f.minute, f.second, f.ms)));
    });
    method(cx, date_proto, "setTime", 1, [this_time, set_time](context & c, std::span<value> a) {
        double t = 0;
        if (!this_time(c, "Date.prototype.setTime", t)) { return value::undefined(); }
        if (!numeric_arg(c, arg_at(a, 0))) { return value::undefined(); }
        const double wanted = c.to_number_value(arg_at(a, 0));
        if (c.throw_pending()) { return value::undefined(); }
        return set_time(c, time_clip(wanted));
    });

    // The string forms. toISOString is the one that REFUSES an invalid date
    // (21.4.4.36 step 3); the rest say "Invalid Date".
    const auto stringer = [&](std::string name, std::string (*render)(const fields &)) {
        method(cx, date_proto, name, 0, [this_time, render, name](context & c, std::span<value>) {
            double t = 0;
            if (!this_time(c, name.c_str(), t)) { return value::undefined(); }
            if (std::isnan(t)) { return c.string("Invalid Date"); }
            return c.string(render(split(t)));
        });
    };
    stringer("toString", [](const fields & f) { return date_string(f) + " " + time_string(f); });
    stringer("toDateString", date_string);
    stringer("toTimeString", time_string);
    stringer("toUTCString", utc_string);
    // B.2.3.3 toGMTString IS toUTCString - the same function object.
    if (const value * utc = date_proto->find("toUTCString")) {
        date_proto->define("toGMTString", *utc, attr_builtin);
    }
    stringer("toLocaleString",
             [](const fields & f) { return date_string(f) + " " + time_string(f); });
    stringer("toLocaleDateString", date_string);
    stringer("toLocaleTimeString", time_string);
    method(cx, date_proto, "toISOString", 0, [this_time](context & c, std::span<value>) {
        double t = 0;
        if (!this_time(c, "Date.prototype.toISOString", t)) { return value::undefined(); }
        if (std::isnan(t)) {
            c.throw_error("RangeError", "Invalid time value");
            return value::undefined();
        }
        return c.string(iso_string(split(t)));
    });
    // 21.4.4.37 toJSON: generic - ToPrimitive(this, number), null for a
    // non-finite one, else Invoke(O, "toISOString").
    method(cx, date_proto, "toJSON", 1, [](context & c, std::span<value>) {
        const value self = c.current_this();
        const value boxed = detail::box_primitive(c, self);
        if (boxed.is_nullish()) {
            c.throw_error("TypeError", "Date.prototype.toJSON called on null or undefined");
            return value::undefined();
        }
        value primitive = value::undefined();
        if (!c.to_primitive_hint(boxed, "number", primitive)) { return value::undefined(); }
        if (primitive.is_number() && !std::isfinite(primitive.as_number())) {
            return value::null();
        }
        const value fn = c.lookup_property(boxed, "toISOString");
        if (!fn.is_callable()) {
            c.throw_error("TypeError", "toISOString is not a function");
            return value::undefined();
        }
        return c.call(fn, std::span<const value>{}, boxed);
    });
    // 21.4.4.45 Date.prototype[@@toPrimitive]: "number" tries valueOf first,
    // "string" and "default" try toString first - which is why `date + ''`
    // is the date string and `date - 0` is the time value.
    {
        auto * exotic =
            detail::method_native(cx, "[Symbol.toPrimitive]", [](context & c, std::span<value> a) {
                const value self = c.current_this();
                if (!self.is_object_like()) {
                    c.throw_error("TypeError",
                                  "Date.prototype[Symbol.toPrimitive] called on non-object");
                    return value::undefined();
                }
                const std::string hint = arg_at(a, 0).is_string() ? c.to_string(a[0]) : "";
                if (hint != "number" && hint != "string" && hint != "default") {
                    c.throw_error("TypeError", "Invalid hint");
                    return value::undefined();
                }
                const bool number_first = hint == "number";
                for (const char * name : {number_first ? "valueOf" : "toString",
                                          number_first ? "toString" : "valueOf"}) {
                    const value fn = c.lookup_property(self, name);
                    if (c.throw_pending()) { return value::undefined(); }
                    if (!fn.is_callable()) { continue; }
                    const value out = c.call(fn, std::span<const value>{}, self);
                    if (c.throw_pending()) { return value::undefined(); }
                    if (!out.is_object_like()) { return out; }
                }
                c.throw_error("TypeError", "Cannot convert object to primitive value");
                return value::undefined();
            });
        detail::install_arity(cx, exotic, 1);
        date_proto->define("@@toPrimitive", value::object(exotic), attr_configurable);
    }

    auto * ctor = cx.allocate<native_object>("Date", [date_proto](context & c, std::span<value> a) {
        value self = c.current_this();
        // 21.4.2.1 step 1: `Date()` without `new` is the current time as a string.
        if (!detail::constructing_this(self)) {
            const fields f = split(c.clock_ms());
            return c.string(date_string(f) + " " + time_string(f));
        }
        auto * made = static_cast<object_object *>(self.as_heap());
        if (!made->prototype.is_object()) { made->prototype = value::object(date_proto); }
        double ms = 0;
        if (a.empty()) {
            // NOW, from the context's clock - see context::set_clock. It used
            // to be the literal epoch, so every page here believed it was 1970.
            ms = c.clock_ms();
        } else if (a.size() == 1) {
            // Step 4: another Date's time value; else ToPrimitive - a string
            // parses, anything else is a Number.
            value v = a[0];
            const value * held =
                v.is_object() ? static_cast<object_object *>(v.as_heap())->find(invalid_date_slot)
                              : nullptr;
            if (held != nullptr) {
                ms = held->as_number();
            } else {
                if (v.is_object_like() && !c.to_primitive_hint(v, "default", v)) {
                    return value::undefined();
                }
                if (v.is_string()) {
                    ms = parse_date(static_cast<string_object *>(v.as_heap())->text);
                } else {
                    if (!numeric_arg(c, v)) { return value::undefined(); }
                    ms = time_clip(c.to_number_value(v));
                }
            }
        } else {
            // (year, monthIndex[, day, hours, minutes, seconds, ms]), each
            // ToNumber'd in order; a year 0-99 is 1900-1999 (step 5.e).
            double parts[7] = {0, 0, 1, 0, 0, 0, 0};
            for (std::size_t i = 0; i < std::min<std::size_t>(a.size(), 7); ++i) {
                if (!numeric_arg(c, a[i])) { return value::undefined(); }
                parts[i] = c.to_number_value(a[i]);
                if (c.throw_pending()) { return value::undefined(); }
            }
            if (std::isfinite(parts[0]) && std::trunc(parts[0]) >= 0 &&
                std::trunc(parts[0]) <= 99) {
                parts[0] = 1900 + std::trunc(parts[0]);
            }
            ms = time_clip(
                make_date(parts[0], parts[1], parts[2], parts[3], parts[4], parts[5], parts[6]));
        }
        // NON-ENUMERABLE, like every other internal slot this engine spells as
        // a property: a Date has no own enumerable property in the
        // specification, and `Object.defineProperties(obj, new Date)` handed
        // this Number over as a descriptor and threw.
        made->define(invalid_date_slot, value::number(ms), attr_builtin);
        return self;
    });
    detail::constant(ctor, "prototype", value::object(date_proto));
    link_constructor(cx, date_proto, "Date", 7, value::object(ctor));
    method(cx, ctor, "now", 0,
           [](context & c, std::span<value>) { return value::number(c.clock_ms()); });
    method(cx, ctor, "parse", 1, [](context & c, std::span<value> a) {
        const std::string text = str_at(c, a, 0);
        if (c.throw_pending()) { return value::undefined(); }
        return value::number(parse_date(text));
    });
    method(cx, ctor, "UTC", 7, [](context & c, std::span<value> a) {
        double parts[7] = {std::nan(""), 0, 1, 0, 0, 0, 0};
        for (std::size_t i = 0; i < std::min<std::size_t>(a.size(), 7); ++i) {
            if (!numeric_arg(c, a[i])) { return value::undefined(); }
            parts[i] = c.to_number_value(a[i]);
            if (c.throw_pending()) { return value::undefined(); }
        }
        if (std::isfinite(parts[0]) && std::trunc(parts[0]) >= 0 && std::trunc(parts[0]) <= 99) {
            parts[0] = 1900 + std::trunc(parts[0]);
        }
        return value::number(time_clip(
            make_date(parts[0], parts[1], parts[2], parts[3], parts[4], parts[5], parts[6])));
    });
    cx.define_global("Date", value::object(ctor));
}

} // namespace ctbrowser::script::builtins_detail
