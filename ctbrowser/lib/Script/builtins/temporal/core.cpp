//===- temporal/core.cpp - Temporal date and time arithmetic ----*- C++ -*-===//
//
// Temporal's abstract operations: the ISO calendar, time arithmetic,
// rounding, the two kinds of time zone (UTC and fixed offsets), durations and
// their relative rounding, and the string forms. CamelCase names in comments
// are abstract operations from the TC39 Temporal proposal's specification,
// called "the Temporal spec" below. See core.hpp.
//
//===----------------------------------------------------------------------===//

#include "core.hpp"

#include <ctbrowser/core/algorithms.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

namespace ctbrowser::script::temporal {

namespace {

template <class T> [[nodiscard]] constexpr T floor_div(T a, T b) noexcept {
    T q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) { --q; }
    return q;
}
template <class T> [[nodiscard]] constexpr T floor_mod(T a, T b) noexcept {
    return a - floor_div(a, b) * b;
}
template <class T> [[nodiscard]] constexpr T abs_of(T v) noexcept {
    return v < 0 ? -v : v;
}

/// An integral double to i128; the caller has bounded it.
[[nodiscard]] i128 to_i128(double v) noexcept {
    return static_cast<i128>(v);
}
[[nodiscard]] double to_double(i128 v) noexcept {
    return static_cast<double>(v);
}

enum class unsigned_mode : std::uint8_t {
    zero,
    infinity,
    half_zero,
    half_infinity,
    half_even
};

[[nodiscard]] unsigned_mode get_unsigned_rounding_mode(rounding_mode mode, bool negative) noexcept {
    switch (mode) {
    case rounding_mode::ceil: return negative ? unsigned_mode::zero : unsigned_mode::infinity;
    case rounding_mode::floor: return negative ? unsigned_mode::infinity : unsigned_mode::zero;
    case rounding_mode::expand: return unsigned_mode::infinity;
    case rounding_mode::trunc: return unsigned_mode::zero;
    case rounding_mode::half_ceil:
        return negative ? unsigned_mode::half_zero : unsigned_mode::half_infinity;
    case rounding_mode::half_floor:
        return negative ? unsigned_mode::half_infinity : unsigned_mode::half_zero;
    case rounding_mode::half_expand: return unsigned_mode::half_infinity;
    case rounding_mode::half_trunc: return unsigned_mode::half_zero;
    case rounding_mode::half_even: return unsigned_mode::half_even;
    }
    return unsigned_mode::zero;
}

/// ApplyUnsignedRoundingMode for x = r1 + d1/(d1 + d2) between r1 and r1 + 1,
/// told apart by the two distances rather than by a fraction: d1 is how far x
/// is above r1 and d2 how far below r1 + 1, both in the same (exact) unit.
/// True when x rounds to r1 + 1.
[[nodiscard]] bool rounds_up(unsigned_mode mode, i128 d1, i128 d2, i128 r1) noexcept {
    if (d1 == 0) { return false; }
    switch (mode) {
    case unsigned_mode::zero: return false;
    case unsigned_mode::infinity: return true;
    default: break;
    }
    if (d1 < d2) { return false; }
    if (d2 < d1) { return true; }
    if (mode == unsigned_mode::half_zero) { return false; }
    if (mode == unsigned_mode::half_infinity) { return true; }
    return floor_mod<i128>(r1, 2) != 0; // half-even: to the even one
}

} // namespace

// --- units and rounding ------------------------------------------------------

i128 unit_length_ns(unit u) noexcept {
    switch (u) {
    case unit::day: return ns_per_day;
    case unit::hour: return i128{3'600'000'000'000};
    case unit::minute: return i128{60'000'000'000};
    case unit::second: return i128{1'000'000'000};
    case unit::millisecond: return i128{1'000'000};
    case unit::microsecond: return i128{1'000};
    case unit::nanosecond: return i128{1};
    default: return 0;
    }
}

std::int64_t maximum_rounding_increment(unit u) noexcept {
    switch (u) {
    case unit::hour: return 24;
    case unit::minute:
    case unit::second: return 60;
    case unit::millisecond:
    case unit::microsecond:
    case unit::nanosecond: return 1000;
    default: return 0;
    }
}

std::optional<unit> unit_from_name(std::string_view name) noexcept {
    struct row {
        std::string_view singular;
        std::string_view plural;
        unit value;
    };
    static constexpr row table[] = {
        {"year", "years", unit::year},
        {"month", "months", unit::month},
        {"week", "weeks", unit::week},
        {"day", "days", unit::day},
        {"hour", "hours", unit::hour},
        {"minute", "minutes", unit::minute},
        {"second", "seconds", unit::second},
        {"millisecond", "milliseconds", unit::millisecond},
        {"microsecond", "microseconds", unit::microsecond},
        {"nanosecond", "nanoseconds", unit::nanosecond},
    };
    for (const row & r : table) {
        if (name == r.singular || name == r.plural) { return r.value; }
    }
    return std::nullopt;
}

std::string_view unit_name(unit u) noexcept {
    static constexpr std::string_view names[] = {
        "year",   "month",  "week",        "day",         "hour",
        "minute", "second", "millisecond", "microsecond", "nanosecond"};
    return names[static_cast<std::size_t>(u)];
}

std::optional<rounding_mode> rounding_mode_from_name(std::string_view name) noexcept {
    static constexpr std::pair<std::string_view, rounding_mode> table[] = {
        {"ceil", rounding_mode::ceil},
        {"floor", rounding_mode::floor},
        {"expand", rounding_mode::expand},
        {"trunc", rounding_mode::trunc},
        {"halfCeil", rounding_mode::half_ceil},
        {"halfFloor", rounding_mode::half_floor},
        {"halfExpand", rounding_mode::half_expand},
        {"halfTrunc", rounding_mode::half_trunc},
        {"halfEven", rounding_mode::half_even},
    };
    for (const auto & [text, mode] : table) {
        if (name == text) { return mode; }
    }
    return std::nullopt;
}

rounding_mode negate_rounding_mode(rounding_mode mode) noexcept {
    switch (mode) {
    case rounding_mode::ceil: return rounding_mode::floor;
    case rounding_mode::floor: return rounding_mode::ceil;
    case rounding_mode::half_ceil: return rounding_mode::half_floor;
    case rounding_mode::half_floor: return rounding_mode::half_ceil;
    default: return mode;
    }
}

i128 round_number_to_increment(i128 x, i128 increment, rounding_mode mode) noexcept {
    const i128 r = x % increment;
    if (r == 0) { return x; }
    const bool negative = x < 0;
    const i128 r1 = abs_of(x / increment); // floor of |x| / increment
    const i128 d1 = abs_of(r);
    const i128 d2 = increment - d1;
    const i128 rounded =
        rounds_up(get_unsigned_rounding_mode(mode, negative), d1, d2, r1) ? r1 + 1 : r1;
    return (negative ? -rounded : rounded) * increment;
}

i128 round_number_to_increment_as_if_positive(i128 x, i128 increment, rounding_mode mode) noexcept {
    const i128 r1 = floor_div(x, increment);
    const i128 d1 = x - r1 * increment;
    const i128 d2 = increment - d1;
    const i128 rounded =
        rounds_up(get_unsigned_rounding_mode(mode, false), d1, d2, r1) ? r1 + 1 : r1;
    return rounded * increment;
}

bool valid_rounding_increment(std::int64_t increment, std::int64_t dividend,
                              bool inclusive) noexcept {
    const std::int64_t maximum = inclusive ? dividend : dividend - 1;
    if (increment > maximum) { return false; }
    return dividend % increment == 0;
}

// Scale |n| up to ~125 bits before dividing, so the quotient carries at least
// 78 significant bits, fold the remainder into a sticky bit below them, and
// let the (correctly rounding) i128 -> double conversion do the rest.
double exact_quotient(i128 n, i128 d) noexcept {
    if (d < 0) {
        n = -n;
        d = -d;
    }
    if (n == 0) { return 0.0; }
    const bool negative = n < 0;
    i128 a = negative ? -n : n;
    int bits = 0;
    for (i128 t = a; t != 0; t >>= 1) { ++bits; }
    const int shift = 125 - bits;
    if (shift > 0) { a <<= shift; }
    i128 q = a / d;
    if (a % d != 0) { q |= 1; }
    double result = std::ldexp(static_cast<double>(q), -std::max(shift, 0));
    if (shift < 0) { result = std::ldexp(result, -shift); }
    return negative ? -result : result;
}

// --- the ISO calendar --------------------------------------------------------

bool is_leap_year(std::int64_t year) noexcept {
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

int iso_days_in_month(std::int64_t year, int month) noexcept {
    switch (month) {
    case 1:
    case 3:
    case 5:
    case 7:
    case 8:
    case 10:
    case 12: return 31;
    case 4:
    case 6:
    case 9:
    case 11: return 30;
    default: return is_leap_year(year) ? 29 : 28;
    }
}

int iso_days_in_year(std::int64_t year) noexcept {
    return is_leap_year(year) ? 366 : 365;
}

std::int64_t iso_date_to_epoch_days(std::int64_t year, std::int64_t month,
                                    std::int64_t day) noexcept {
    const std::int64_t y = year + floor_div<std::int64_t>(month, 12);
    const int m = static_cast<int>(floor_mod<std::int64_t>(month, 12)) + 1;
    return days_from_civil(y, m, 1) + day - 1;
}

std::int64_t epoch_days(const iso_date & date) noexcept {
    return iso_date_to_epoch_days(date.year, date.month - 1, date.day);
}

iso_date epoch_days_to_iso_date(std::int64_t z) noexcept {
    const civil_date civil = civil_from_days(z);
    return iso_date{civil.year, civil.month, civil.day};
}

bool is_valid_iso_date(std::int64_t year, std::int64_t month, std::int64_t day) noexcept {
    if (month < 1 || month > 12) { return false; }
    return day >= 1 && day <= iso_days_in_month(year, static_cast<int>(month));
}

std::optional<iso_date> regulate_iso_date(double year, double month, double day,
                                          overflow how) noexcept {
    // Far past any representable year either way: the value is only ever
    // refused by ISODateWithinLimits, so a stand-in that is also out of range
    // gives the same answer without overflowing.
    constexpr double far = 1e12;
    const std::int64_t y = static_cast<std::int64_t>(std::clamp(year, -far, far));
    if (how == overflow::constrain) {
        const int m = static_cast<int>(std::clamp(month, 1.0, 12.0));
        const int d = static_cast<int>(std::clamp(day, 1.0, double(iso_days_in_month(y, m))));
        return iso_date{y, m, d};
    }
    if (month < 1 || month > 12) { return std::nullopt; }
    const int m = static_cast<int>(month);
    if (day < 1 || day > iso_days_in_month(y, m)) { return std::nullopt; }
    return iso_date{y, m, static_cast<int>(day)};
}

iso_date add_days_to_iso_date(const iso_date & date, std::int64_t days) noexcept {
    return epoch_days_to_iso_date(epoch_days(date) + days);
}

int compare_iso_date(const iso_date & a, const iso_date & b) noexcept {
    if (a.year != b.year) { return a.year > b.year ? 1 : -1; }
    if (a.month != b.month) { return a.month > b.month ? 1 : -1; }
    if (a.day != b.day) { return a.day > b.day ? 1 : -1; }
    return 0;
}

bool iso_date_within_limits(const iso_date & date) noexcept {
    return iso_datetime_within_limits(iso_datetime{date, noon()});
}

bool iso_year_month_within_limits(const iso_date & date) noexcept {
    if (date.year < -271821 || date.year > 275760) { return false; }
    if (date.year == -271821 && date.month < 4) { return false; }
    if (date.year == 275760 && date.month > 9) { return false; }
    return true;
}

bool check_iso_days_range(const iso_date & date) noexcept {
    return abs_of(epoch_days(date)) <= 100'000'000;
}

year_month balance_iso_year_month(std::int64_t year, std::int64_t month) noexcept {
    return year_month{year + floor_div<std::int64_t>(month - 1, 12),
                      static_cast<int>(floor_mod<std::int64_t>(month - 1, 12)) + 1};
}

int iso_day_of_week(const iso_date & date) noexcept {
    // 1970-01-01 was a Thursday.
    const int d = static_cast<int>(floor_mod<std::int64_t>(epoch_days(date) + 4, 7));
    return d == 0 ? 7 : d;
}

int iso_day_of_year(const iso_date & date) noexcept {
    return static_cast<int>(epoch_days(date) - iso_date_to_epoch_days(date.year, 0, 1)) + 1;
}

year_week iso_week_of_year(const iso_date & date) noexcept {
    const std::int64_t year = date.year;
    const int day_of_year = iso_day_of_year(date);
    const int day_of_week = iso_day_of_week(date);
    const int week = (day_of_year + 7 - day_of_week + 3) / 7;
    if (week < 1) {
        const int jan1 = iso_day_of_week(iso_date{year, 1, 1});
        if (jan1 == 5) { return year_week{53, year - 1}; }
        if (jan1 == 6 && is_leap_year(year - 1)) { return year_week{53, year - 1}; }
        return year_week{52, year - 1};
    }
    if (week == 53) {
        const int later = iso_days_in_year(year) - day_of_year;
        if (later < 4 - day_of_week) { return year_week{1, year + 1}; }
    }
    return year_week{week, year};
}

std::optional<iso_date> calendar_date_add(const iso_date & date, const date_duration & duration,
                                          overflow how) noexcept {
    const year_month ym =
        balance_iso_year_month(date.year + static_cast<std::int64_t>(duration.years),
                               date.month + static_cast<std::int64_t>(duration.months));
    const auto regulated =
        regulate_iso_date(double(ym.year), double(ym.month), double(date.day), how);
    if (!regulated) { return std::nullopt; }
    const auto days =
        static_cast<std::int64_t>(duration.days) + 7 * static_cast<std::int64_t>(duration.weeks);
    const iso_date result = add_days_to_iso_date(*regulated, days);
    if (!iso_date_within_limits(result)) { return std::nullopt; }
    return result;
}

namespace {

/// CompareSurpasses for the ISO calendar, where a month code orders as its month.
[[nodiscard]] bool surpasses(int sign, std::int64_t year, int month, int day,
                             const iso_date & target) noexcept {
    if (year != target.year) { return sign * (year - target.year) > 0; }
    if (month != target.month) { return sign * (month - target.month) > 0; }
    if (day != target.day) { return sign * (day - target.day) > 0; }
    return false;
}

} // namespace

// CalendarDateUntil for "iso8601". The specification finds each unit by
// counting up from one while ISODateSurpasses says the candidate has not
// passed `two`, which is the largest count that does not pass it; that is
// computed here directly: the count that lands in the target's year (or year
// and month), less one if the base's month or day then overshoots. (In the
// Temporal spec, ISODateSurpasses returns false outright when `months` is 0,
// so its day loop, read literally, never ends; this computes what that loop is
// meant to compute.)
date_duration calendar_date_until(const iso_date & one, const iso_date & two,
                                  unit largest) noexcept {
    const int compared = compare_iso_date(one, two);
    if (compared == 0) { return {}; }
    const int sign = -compared;
    std::int64_t years = 0;
    std::int64_t months = 0;
    if (largest == unit::year) {
        years = two.year - one.year;
        if (years != 0 && surpasses(sign, one.year + years, one.month, one.day, two)) {
            years -= sign;
        }
    }
    if (largest == unit::year || largest == unit::month) {
        const std::int64_t y0 = one.year + years;
        months = (two.year - y0) * 12 + (two.month - one.month);
        if (months != 0) {
            const year_month added = balance_iso_year_month(y0, one.month + months);
            if (surpasses(sign, added.year, added.month, one.day, two)) { months -= sign; }
        }
    }
    const year_month added = balance_iso_year_month(one.year + years, one.month + months);
    const int day = std::min(one.day, iso_days_in_month(added.year, added.month));
    const std::int64_t remaining =
        epoch_days(two) - epoch_days(iso_date{added.year, added.month, day});
    std::int64_t weeks = 0;
    std::int64_t days = remaining;
    if (largest == unit::week) {
        weeks = remaining / 7;
        days = remaining % 7;
    }
    return date_duration{double(years), double(months), double(weeks), double(days)};
}

std::string month_code(int month) {
    std::string code = "M";
    code += static_cast<char>('0' + month / 10);
    code += static_cast<char>('0' + month % 10);
    return code;
}

std::optional<int> parse_month_code(std::string_view code) noexcept {
    if (code.size() != 3 || code[0] != 'M' || code[1] < '0' || code[1] > '9' || code[2] < '0' ||
        code[2] > '9') {
        return std::nullopt;
    }
    const int month = (code[1] - '0') * 10 + (code[2] - '0');
    if (month < 1 || month > 12) { return std::nullopt; }
    return month;
}

// --- time --------------------------------------------------------------------

bool is_valid_time(double hour, double minute, double second, double millisecond,
                   double microsecond, double nanosecond) noexcept {
    return hour >= 0 && hour <= 23 && minute >= 0 && minute <= 59 && second >= 0 && second <= 59 &&
           millisecond >= 0 && millisecond <= 999 && microsecond >= 0 && microsecond <= 999 &&
           nanosecond >= 0 && nanosecond <= 999;
}

std::optional<time_record> regulate_time(double hour, double minute, double second,
                                         double millisecond, double microsecond, double nanosecond,
                                         overflow how) noexcept {
    if (how == overflow::constrain) {
        const auto clamp = [](double v, double high) {
            return static_cast<int>(std::clamp(v, 0.0, high));
        };
        return time_record{0,
                           clamp(hour, 23),
                           clamp(minute, 59),
                           clamp(second, 59),
                           clamp(millisecond, 999),
                           clamp(microsecond, 999),
                           clamp(nanosecond, 999)};
    }
    if (!is_valid_time(hour, minute, second, millisecond, microsecond, nanosecond)) {
        return std::nullopt;
    }
    return time_record{0,
                       static_cast<int>(hour),
                       static_cast<int>(minute),
                       static_cast<int>(second),
                       static_cast<int>(millisecond),
                       static_cast<int>(microsecond),
                       static_cast<int>(nanosecond)};
}

time_record balance_time(i128 hour, i128 minute, i128 second, i128 millisecond, i128 microsecond,
                         i128 nanosecond) noexcept {
    microsecond += floor_div<i128>(nanosecond, 1000);
    nanosecond = floor_mod<i128>(nanosecond, 1000);
    millisecond += floor_div<i128>(microsecond, 1000);
    microsecond = floor_mod<i128>(microsecond, 1000);
    second += floor_div<i128>(millisecond, 1000);
    millisecond = floor_mod<i128>(millisecond, 1000);
    minute += floor_div<i128>(second, 60);
    second = floor_mod<i128>(second, 60);
    hour += floor_div<i128>(minute, 60);
    minute = floor_mod<i128>(minute, 60);
    const i128 days = floor_div<i128>(hour, 24);
    hour = floor_mod<i128>(hour, 24);
    return time_record{static_cast<std::int64_t>(days), static_cast<int>(hour),
                       static_cast<int>(minute),        static_cast<int>(second),
                       static_cast<int>(millisecond),   static_cast<int>(microsecond),
                       static_cast<int>(nanosecond)};
}

i128 time_nanoseconds(const time_record & t) noexcept {
    return ((((i128{t.hour} * 60 + t.minute) * 60 + t.second) * 1000 + t.millisecond) * 1000 +
            t.microsecond) *
               1000 +
           t.nanosecond;
}

i128 difference_time(const time_record & one, const time_record & two) noexcept {
    return time_nanoseconds(two) - time_nanoseconds(one);
}

time_record add_time(const time_record & t, i128 duration) noexcept {
    return balance_time(t.hour, t.minute, t.second, t.millisecond, t.microsecond,
                        i128{t.nanosecond} + duration);
}

time_record round_time(const time_record & t, std::int64_t increment, unit u,
                       rounding_mode mode) noexcept {
    i128 quantity = 0;
    switch (u) {
    case unit::day:
    case unit::hour: quantity = time_nanoseconds(t); break;
    case unit::minute: quantity = time_nanoseconds(t) - i128{t.hour} * 3'600'000'000'000; break;
    case unit::second:
        quantity =
            ((i128{t.second} * 1000 + t.millisecond) * 1000 + t.microsecond) * 1000 + t.nanosecond;
        break;
    case unit::millisecond:
        quantity = (i128{t.millisecond} * 1000 + t.microsecond) * 1000 + t.nanosecond;
        break;
    case unit::microsecond: quantity = i128{t.microsecond} * 1000 + t.nanosecond; break;
    default: quantity = t.nanosecond; break;
    }
    const i128 length = unit_length_ns(u);
    const i128 result = round_number_to_increment(quantity, increment * length, mode) / length;
    switch (u) {
    case unit::day: return time_record{static_cast<std::int64_t>(result), 0, 0, 0, 0, 0, 0};
    case unit::hour: return balance_time(result, 0, 0, 0, 0, 0);
    case unit::minute: return balance_time(t.hour, result, 0, 0, 0, 0);
    case unit::second: return balance_time(t.hour, t.minute, result, 0, 0, 0);
    case unit::millisecond: return balance_time(t.hour, t.minute, t.second, result, 0, 0);
    case unit::microsecond:
        return balance_time(t.hour, t.minute, t.second, t.millisecond, result, 0);
    default: return balance_time(t.hour, t.minute, t.second, t.millisecond, t.microsecond, result);
    }
}

int compare_time(const time_record & a, const time_record & b) noexcept {
    const i128 x = time_nanoseconds(a);
    const i128 y = time_nanoseconds(b);
    return x > y ? 1 : (x < y ? -1 : 0);
}

// --- date-times and exact times ----------------------------------------------

i128 get_utc_epoch_nanoseconds(const iso_datetime & dt) noexcept {
    return i128{epoch_days(dt.date)} * ns_per_day + time_nanoseconds(dt.time);
}

iso_datetime balance_iso_datetime(std::int64_t year, int month, int day, i128 hour, i128 minute,
                                  i128 second, i128 millisecond, i128 microsecond,
                                  i128 nanosecond) noexcept {
    time_record time = balance_time(hour, minute, second, millisecond, microsecond, nanosecond);
    const iso_date date = add_days_to_iso_date(iso_date{year, month, day}, time.days);
    time.days = 0;
    return iso_datetime{date, time};
}

bool iso_datetime_within_limits(const iso_datetime & dt) noexcept {
    if (abs_of(epoch_days(dt.date)) > 100'000'001) { return false; }
    const i128 ns = get_utc_epoch_nanoseconds(dt);
    return ns > ns_min_instant - ns_per_day && ns < ns_max_instant + ns_per_day;
}

int compare_iso_datetime(const iso_datetime & a, const iso_datetime & b) noexcept {
    const int date = compare_iso_date(a.date, b.date);
    return date != 0 ? date : compare_time(a.time, b.time);
}

iso_datetime round_iso_datetime(const iso_datetime & dt, std::int64_t increment, unit u,
                                rounding_mode mode) noexcept {
    time_record time = round_time(dt.time, increment, u, mode);
    const iso_date date = add_days_to_iso_date(dt.date, time.days);
    time.days = 0;
    return iso_datetime{date, time};
}

iso_datetime iso_parts_from_epoch(i128 epoch_ns) noexcept {
    const i128 days = floor_div(epoch_ns, ns_per_day);
    const i128 rest = epoch_ns - days * ns_per_day;
    time_record time = balance_time(0, 0, 0, 0, 0, rest);
    time.days = 0;
    return iso_datetime{epoch_days_to_iso_date(static_cast<std::int64_t>(days)), time};
}

bool is_valid_epoch_nanoseconds(i128 epoch_ns) noexcept {
    return epoch_ns >= ns_min_instant && epoch_ns <= ns_max_instant;
}

std::optional<i128> add_instant(i128 epoch_ns, i128 duration) noexcept {
    const i128 result = epoch_ns + duration;
    if (!is_valid_epoch_nanoseconds(result)) { return std::nullopt; }
    return result;
}

internal_duration difference_instant(i128 ns1, i128 ns2, std::int64_t increment, unit smallest,
                                     rounding_mode mode) noexcept {
    const i128 d = round_number_to_increment(ns2 - ns1, unit_length_ns(smallest) * increment, mode);
    return internal_duration{{}, d};
}

i128 round_temporal_instant(i128 epoch_ns, std::int64_t increment, unit u,
                            rounding_mode mode) noexcept {
    return round_number_to_increment_as_if_positive(epoch_ns, unit_length_ns(u) * increment, mode);
}

internal_duration difference_iso_datetime(const iso_datetime & one, const iso_datetime & two,
                                          unit largest) noexcept {
    i128 time = difference_time(one.time, two.time);
    const int time_sign = time_duration_sign(time);
    const int date_sign = compare_iso_date(one.date, two.date);
    iso_date adjusted = two.date;
    if (time_sign == date_sign) {
        adjusted = add_days_to_iso_date(adjusted, time_sign);
        time -= time_sign * ns_per_day;
    }
    const unit date_largest = larger_unit(unit::day, largest);
    date_duration date = calendar_date_until(one.date, adjusted, date_largest);
    if (largest != date_largest) {
        time += to_i128(date.days) * ns_per_day;
        date.days = 0;
    }
    return internal_duration{date, time};
}

std::optional<internal_duration> difference_plain_datetime_with_rounding(
    const iso_datetime & one, const iso_datetime & two, unit largest, std::int64_t increment,
    unit smallest, rounding_mode mode) {
    if (compare_iso_datetime(one, two) == 0) { return internal_duration{}; }
    if (!iso_datetime_within_limits(one) || !iso_datetime_within_limits(two)) {
        return std::nullopt;
    }
    const internal_duration diff = difference_iso_datetime(one, two, largest);
    if (smallest == unit::nanosecond && increment == 1) { return diff; }
    return round_relative_duration(diff, get_utc_epoch_nanoseconds(one),
                                   get_utc_epoch_nanoseconds(two), one, {}, largest, increment,
                                   smallest, mode);
}

std::optional<double> difference_plain_datetime_with_total(const iso_datetime & one,
                                                           const iso_datetime & two, unit u) {
    if (compare_iso_datetime(one, two) == 0) { return 0.0; }
    if (!iso_datetime_within_limits(one) || !iso_datetime_within_limits(two)) {
        return std::nullopt;
    }
    const internal_duration diff = difference_iso_datetime(one, two, u);
    if (u == unit::nanosecond) { return to_double(diff.time); }
    return total_relative_duration(diff, get_utc_epoch_nanoseconds(one),
                                   get_utc_epoch_nanoseconds(two), one, {}, u);
}

// --- time zones --------------------------------------------------------------

namespace {

/// The zone's fixed offset in minutes. Only UTC and fixed-offset zones are
/// supported (no IANA database), so every zone has one; UTC is 0.
[[nodiscard]] std::int64_t offset_minutes_of(std::string_view zone) {
    const auto parsed = parse_time_zone_identifier(zone);
    return parsed && parsed->offset_minutes ? *parsed->offset_minutes : 0;
}

} // namespace

std::optional<std::string> canonical_time_zone(const time_zone_parse & parsed) {
    if (parsed.offset_minutes) {
        return format_offset_time_zone_identifier(*parsed.offset_minutes);
    }
    // AvailableNamedTimeZoneIdentifiers: "UTC" alone, without ECMA-402.
    if (ascii_iequals(parsed.name, "UTC")) { return std::string{"UTC"}; }
    return std::nullopt;
}

bool time_zone_equals(std::string_view one, std::string_view two) {
    if (one == two) { return true; }
    if (!is_offset_time_zone_identifier(one) && !is_offset_time_zone_identifier(two)) {
        return ascii_iequals(one, two);
    }
    return false;
}

i128 get_offset_nanoseconds_for(std::string_view zone, i128) {
    return i128{offset_minutes_of(zone)} * 60'000'000'000;
}

iso_datetime get_iso_datetime_for(std::string_view zone, i128 epoch_ns) {
    const iso_datetime parts = iso_parts_from_epoch(epoch_ns);
    const time_record & t = parts.time;
    return balance_iso_datetime(parts.date.year, parts.date.month, parts.date.day, t.hour, t.minute,
                                t.second, t.millisecond, t.microsecond,
                                i128{t.nanosecond} + get_offset_nanoseconds_for(zone, epoch_ns));
}

std::optional<std::vector<i128>> get_possible_epoch_nanoseconds(std::string_view zone,
                                                                const iso_datetime & dt) {
    const std::int64_t minutes = offset_minutes_of(zone);
    const time_record & t = dt.time;
    const iso_datetime balanced = balance_iso_datetime(dt.date.year, dt.date.month, dt.date.day,
                                                       t.hour, i128{t.minute} - minutes, t.second,
                                                       t.millisecond, t.microsecond, t.nanosecond);
    if (!check_iso_days_range(balanced.date)) { return std::nullopt; }
    const i128 epoch_ns = get_utc_epoch_nanoseconds(balanced);
    if (!is_valid_epoch_nanoseconds(epoch_ns)) { return std::nullopt; }
    return std::vector<i128>{epoch_ns};
}

// A fixed-offset zone has exactly one instant for every wall-clock time, so
// DisambiguatePossibleEpochNanoseconds always takes its first step.
std::optional<i128> get_epoch_nanoseconds_for(std::string_view zone, const iso_datetime & dt,
                                              disambiguation) {
    const auto possible = get_possible_epoch_nanoseconds(zone, dt);
    if (!possible || possible->empty()) { return std::nullopt; }
    return possible->front();
}

std::optional<i128> get_start_of_day(std::string_view zone, const iso_date & date) {
    return get_epoch_nanoseconds_for(zone, iso_datetime{date, time_record{}},
                                     disambiguation::compatible);
}

std::optional<i128> interpret_iso_datetime_offset(const iso_date & date,
                                                  const std::optional<time_record> & time,
                                                  offset_behaviour behaviour, i128 offset_ns,
                                                  std::string_view zone, disambiguation how,
                                                  offset_option option, match_behaviour match) {
    if (!time) { return get_start_of_day(zone, date); }
    const iso_datetime dt{date, *time};
    if (behaviour == offset_behaviour::wall ||
        (behaviour == offset_behaviour::option && option == offset_option::ignore)) {
        return get_epoch_nanoseconds_for(zone, dt, how);
    }
    if (behaviour == offset_behaviour::exact ||
        (behaviour == offset_behaviour::option && option == offset_option::use)) {
        const time_record & t = *time;
        const iso_datetime balanced =
            balance_iso_datetime(date.year, date.month, date.day, t.hour, t.minute, t.second,
                                 t.millisecond, t.microsecond, i128{t.nanosecond} - offset_ns);
        if (!check_iso_days_range(balanced.date)) { return std::nullopt; }
        const i128 epoch_ns = get_utc_epoch_nanoseconds(balanced);
        if (!is_valid_epoch_nanoseconds(epoch_ns)) { return std::nullopt; }
        return epoch_ns;
    }
    if (!check_iso_days_range(date)) { return std::nullopt; }
    const i128 utc = get_utc_epoch_nanoseconds(dt);
    const auto possible = get_possible_epoch_nanoseconds(zone, dt);
    if (!possible) { return std::nullopt; }
    for (const i128 candidate : *possible) {
        const i128 candidate_offset = utc - candidate;
        if (candidate_offset == offset_ns) { return candidate; }
        if (match == match_behaviour::minutes &&
            round_number_to_increment(candidate_offset, 60'000'000'000,
                                      rounding_mode::half_expand) == offset_ns) {
            return candidate;
        }
    }
    if (option == offset_option::reject) { return std::nullopt; }
    return get_epoch_nanoseconds_for(zone, dt, how);
}

std::string format_offset_time_zone_identifier(int offset_minutes, bool separated) {
    const char sign = offset_minutes >= 0 ? '+' : '-';
    const int absolute = offset_minutes < 0 ? -offset_minutes : offset_minutes;
    return sign +
           format_time_string(absolute / 60, absolute % 60, 0, 0, precision_minute, separated);
}

std::string format_utc_offset_nanoseconds(i128 offset_ns) {
    const char sign = offset_ns >= 0 ? '+' : '-';
    const i128 absolute = abs_of(offset_ns);
    const auto hour = static_cast<int>(absolute / 3'600'000'000'000);
    const auto minute = static_cast<int>((absolute / 60'000'000'000) % 60);
    const auto second = static_cast<int>((absolute / 1'000'000'000) % 60);
    const auto sub = static_cast<std::int64_t>(absolute % 1'000'000'000);
    const int precision = second == 0 && sub == 0 ? precision_minute : precision_auto;
    return sign + format_time_string(hour, minute, second, sub, precision);
}

std::string format_datetime_utc_offset_rounded(i128 offset_ns) {
    const i128 rounded =
        round_number_to_increment(offset_ns, 60'000'000'000, rounding_mode::half_expand);
    return format_offset_time_zone_identifier(static_cast<int>(rounded / 60'000'000'000));
}

// --- durations ---------------------------------------------------------------

namespace {

constexpr double two_32 = 4294967296.0;

// The largest magnitude each time field can have in a valid duration: past
// it the total alone reaches 2**53 seconds (the fields must share a sign, so
// none can cancel another). Checked before any conversion, so no value that
// would overflow i128 is ever converted.
constexpr double time_field_limits[] = {1.05e11, 2.6e12, 1.6e14, 9.1e15, 9.1e18, 9.1e21, 9.1e24};

} // namespace

bool is_valid_duration(const duration_record & d) noexcept {
    const double fields[] = {d.years,   d.months,  d.weeks,        d.days,         d.hours,
                             d.minutes, d.seconds, d.milliseconds, d.microseconds, d.nanoseconds};
    int sign = 0;
    for (const double v : fields) {
        if (!std::isfinite(v)) { return false; }
        if (v < 0) {
            if (sign > 0) { return false; }
            sign = -1;
        } else if (v > 0) {
            if (sign < 0) { return false; }
            sign = 1;
        }
    }
    if (std::fabs(d.years) >= two_32 || std::fabs(d.months) >= two_32 ||
        std::fabs(d.weeks) >= two_32) {
        return false;
    }
    for (std::size_t i = 0; i < 7; ++i) {
        if (std::fabs(fields[3 + i]) > time_field_limits[i]) { return false; }
    }
    const i128 total = to_i128(d.days) * ns_per_day +
                       time_duration_from_components(d.hours, d.minutes, d.seconds, d.milliseconds,
                                                     d.microseconds, d.nanoseconds);
    return abs_of(total) <= max_time_duration;
}

int duration_sign(const duration_record & d) noexcept {
    for (const double v : {d.years, d.months, d.weeks, d.days, d.hours, d.minutes, d.seconds,
                           d.milliseconds, d.microseconds, d.nanoseconds}) {
        if (v < 0) { return -1; }
        if (v > 0) { return 1; }
    }
    return 0;
}

int date_duration_sign(const date_duration & d) noexcept {
    for (const double v : {d.years, d.months, d.weeks, d.days}) {
        if (v < 0) { return -1; }
        if (v > 0) { return 1; }
    }
    return 0;
}

int internal_duration_sign(const internal_duration & d) noexcept {
    const int date = date_duration_sign(d.date);
    return date != 0 ? date : time_duration_sign(d.time);
}

unit default_largest_unit(const duration_record & d) noexcept {
    if (d.years != 0) { return unit::year; }
    if (d.months != 0) { return unit::month; }
    if (d.weeks != 0) { return unit::week; }
    if (d.days != 0) { return unit::day; }
    if (d.hours != 0) { return unit::hour; }
    if (d.minutes != 0) { return unit::minute; }
    if (d.seconds != 0) { return unit::second; }
    if (d.milliseconds != 0) { return unit::millisecond; }
    if (d.microseconds != 0) { return unit::microsecond; }
    return unit::nanosecond;
}

duration_record negate(const duration_record & d) noexcept {
    // 0 - v rather than -v: a negated zero is +0, as CreateTemporalDuration's
    // ℝ(𝔽(-0)) makes it.
    return duration_record{0 - d.years,        0 - d.months,     0 - d.weeks,   0 - d.days,
                           0 - d.hours,        0 - d.minutes,    0 - d.seconds, 0 - d.milliseconds,
                           0 - d.microseconds, 0 - d.nanoseconds};
}

i128 time_duration_from_components(double hours, double minutes, double seconds,
                                   double milliseconds, double microseconds,
                                   double nanoseconds) noexcept {
    return to_i128(hours) * 3'600'000'000'000 + to_i128(minutes) * 60'000'000'000 +
           to_i128(seconds) * 1'000'000'000 + to_i128(milliseconds) * 1'000'000 +
           to_i128(microseconds) * 1'000 + to_i128(nanoseconds);
}

internal_duration to_internal_duration(const duration_record & d) noexcept {
    return internal_duration{date_duration{d.years, d.months, d.weeks, d.days},
                             time_duration_from_components(d.hours, d.minutes, d.seconds,
                                                           d.milliseconds, d.microseconds,
                                                           d.nanoseconds)};
}

internal_duration to_internal_duration_24h_days(const duration_record & d) noexcept {
    return internal_duration{date_duration{d.years, d.months, d.weeks, 0},
                             time_duration_from_components(d.hours, d.minutes, d.seconds,
                                                           d.milliseconds, d.microseconds,
                                                           d.nanoseconds) +
                                 to_i128(d.days) * ns_per_day};
}

std::optional<date_duration> to_date_duration_without_time(const duration_record & d) noexcept {
    const internal_duration internal = to_internal_duration_24h_days(d);
    return create_date_duration(d.years, d.months, d.weeks, to_double(internal.time / ns_per_day));
}

std::optional<date_duration> create_date_duration(double years, double months, double weeks,
                                                  double days) noexcept {
    duration_record check;
    check.years = years;
    check.months = months;
    check.weeks = weeks;
    check.days = days;
    if (!is_valid_duration(check)) { return std::nullopt; }
    return date_duration{years + 0.0, months + 0.0, weeks + 0.0, days + 0.0};
}

std::optional<duration_record> temporal_duration_from_internal(const internal_duration & d,
                                                               unit largest) noexcept {
    i128 days = 0, hours = 0, minutes = 0, seconds = 0, milliseconds = 0, microseconds = 0;
    const int sign = time_duration_sign(d.time);
    i128 nanoseconds = abs_of(d.time);
    if (largest <= unit::microsecond) {
        microseconds = nanoseconds / 1000;
        nanoseconds %= 1000;
    }
    if (largest <= unit::millisecond) {
        milliseconds = microseconds / 1000;
        microseconds %= 1000;
    }
    if (largest <= unit::second) {
        seconds = milliseconds / 1000;
        milliseconds %= 1000;
    }
    if (largest <= unit::minute) {
        minutes = seconds / 60;
        seconds %= 60;
    }
    if (largest <= unit::hour) {
        hours = minutes / 60;
        minutes %= 60;
    }
    if (largest <= unit::day) {
        days = hours / 24;
        hours %= 24;
    }
    const auto field = [sign](i128 v) { return v == 0 ? 0.0 : to_double(sign * v); };
    duration_record result{d.date.years,        d.date.months,
                           d.date.weeks,        to_double(to_i128(d.date.days) + sign * days) + 0.0,
                           field(hours),        field(minutes),
                           field(seconds),      field(milliseconds),
                           field(microseconds), field(nanoseconds)};
    if (!is_valid_duration(result)) { return std::nullopt; }
    return result;
}

std::optional<i128> add_time_duration(i128 one, i128 two) noexcept {
    const i128 result = one + two;
    if (abs_of(result) > max_time_duration) { return std::nullopt; }
    return result;
}

std::optional<i128> add_24_hour_days(i128 d, double days) noexcept {
    if (std::fabs(days) > 1e12) { return std::nullopt; }
    return add_time_duration(d, to_i128(days) * ns_per_day);
}

std::optional<i128> round_time_duration(i128 d, std::int64_t increment, unit u,
                                        rounding_mode mode) noexcept {
    const i128 rounded = round_number_to_increment(d, unit_length_ns(u) * increment, mode);
    if (abs_of(rounded) > max_time_duration) { return std::nullopt; }
    return rounded;
}

double total_time_duration(i128 d, unit u) noexcept {
    return exact_quotient(d, unit_length_ns(u));
}

std::optional<double> date_duration_days(const date_duration & d,
                                         const iso_date & relative_to) noexcept {
    const date_duration ymw{d.years, d.months, d.weeks, 0};
    if (date_duration_sign(ymw) == 0) { return d.days; }
    const auto later = calendar_date_add(relative_to, ymw, overflow::constrain);
    if (!later) { return std::nullopt; }
    return d.days + double(epoch_days(*later) - epoch_days(relative_to));
}

namespace {

/// A Duration Nudge Result Record: the rounded duration, the epoch
/// nanoseconds it reaches, and whether rounding crossed into the next unit.
struct nudge_result {
    internal_duration duration;
    i128 nudged_epoch_ns = 0;
    bool did_expand_calendar_unit = false;
};

/// The rounding window of NudgeToCalendarUnit: the two candidate counts
/// r1 and r2 of the rounding unit, and the instants and durations at each.
struct nudge_window {
    double r1 = 0;
    double r2 = 0;
    i128 start_epoch_ns = 0;
    i128 end_epoch_ns = 0;
    internal_duration start_duration;
    internal_duration end_duration;
};

/// The epoch nanoseconds of a date and wall-clock time: in UTC when `zone` is
/// empty (a plain relativeTo), else through the zone with compatible
/// disambiguation.
[[nodiscard]] std::optional<i128> epoch_ns_at(std::string_view zone, const iso_date & date,
                                              const time_record & time) {
    const iso_datetime dt{date, time};
    if (zone.empty()) { return get_utc_epoch_nanoseconds(dt); }
    return get_epoch_nanoseconds_for(zone, dt, disambiguation::compatible);
}

[[nodiscard]] bool tc_zero_date(const date_duration & d) {
    return date_duration_sign(d) == 0;
}

[[nodiscard]] double trunc_to_increment(double v, std::int64_t increment) {
    return to_double(round_number_to_increment(to_i128(v), increment, rounding_mode::trunc)) + 0.0;
}

std::optional<nudge_window> compute_nudge_window(int sign, const internal_duration & d,
                                                 i128 origin_ns, const iso_datetime & dt,
                                                 std::string_view zone, std::int64_t increment,
                                                 unit u, bool additional_shift) {
    nudge_window w;
    std::optional<date_duration> start;
    std::optional<date_duration> end;
    const double step = double(increment) * sign;
    if (u == unit::year) {
        const double years = trunc_to_increment(d.date.years, increment);
        w.r1 = additional_shift ? years + step : years;
        w.r2 = w.r1 + step;
        start = create_date_duration(w.r1, 0, 0, 0);
        end = create_date_duration(w.r2, 0, 0, 0);
    } else if (u == unit::month) {
        const double months = trunc_to_increment(d.date.months, increment);
        w.r1 = additional_shift ? months + step : months;
        w.r2 = w.r1 + step;
        start = create_date_duration(d.date.years, w.r1, 0, 0);
        end = create_date_duration(d.date.years, w.r2, 0, 0);
    } else if (u == unit::week) {
        const auto weeks_start = calendar_date_add(
            dt.date, date_duration{d.date.years, d.date.months, 0, 0}, overflow::constrain);
        if (!weeks_start) { return std::nullopt; }
        const iso_date weeks_end =
            add_days_to_iso_date(*weeks_start, static_cast<std::int64_t>(d.date.days));
        const date_duration until = calendar_date_until(*weeks_start, weeks_end, unit::week);
        const double weeks = trunc_to_increment(d.date.weeks + until.weeks, increment);
        w.r1 = weeks;
        w.r2 = weeks + step;
        start = create_date_duration(d.date.years, d.date.months, w.r1, 0);
        end = create_date_duration(d.date.years, d.date.months, w.r2, 0);
    } else {
        const double days = trunc_to_increment(d.date.days, increment);
        w.r1 = days;
        w.r2 = days + step;
        start = create_date_duration(d.date.years, d.date.months, d.date.weeks, w.r1);
        end = create_date_duration(d.date.years, d.date.months, d.date.weeks, w.r2);
    }
    if (!start || !end) { return std::nullopt; }
    // The Temporal spec uses the origin whenever r1 is 0. This code uses it
    // only when the whole start duration is 0: rounding weeks under a month
    // (r1 = 0 weeks after -1 month) would otherwise measure the window from
    // the wrong date, and test262's exact-multiple-of-larger-unit tests expect
    // the true start.
    if (tc_zero_date(*start)) {
        w.start_epoch_ns = origin_ns;
    } else {
        const auto start_date = calendar_date_add(dt.date, *start, overflow::constrain);
        if (!start_date) { return std::nullopt; }
        const auto ns = epoch_ns_at(zone, *start_date, dt.time);
        if (!ns) { return std::nullopt; }
        w.start_epoch_ns = *ns;
    }
    const auto end_date = calendar_date_add(dt.date, *end, overflow::constrain);
    if (!end_date) { return std::nullopt; }
    const auto end_ns = epoch_ns_at(zone, *end_date, dt.time);
    if (!end_ns) { return std::nullopt; }
    w.end_epoch_ns = *end_ns;
    w.start_duration = internal_duration{*start, 0};
    w.end_duration = internal_duration{*end, 0};
    return w;
}

struct calendar_nudge {
    nudge_result nudge;
    double total = 0;
};

std::optional<calendar_nudge> nudge_to_calendar_unit(int sign, const internal_duration & d,
                                                     i128 origin_ns, i128 dest_ns,
                                                     const iso_datetime & dt, std::string_view zone,
                                                     std::int64_t increment, unit u,
                                                     rounding_mode mode) {
    bool did_expand = false;
    auto window = compute_nudge_window(sign, d, origin_ns, dt, zone, increment, u, false);
    if (!window) { return std::nullopt; }
    const bool inside =
        sign == 1 ? (window->start_epoch_ns <= dest_ns && dest_ns <= window->end_epoch_ns)
                  : (window->end_epoch_ns <= dest_ns && dest_ns <= window->start_epoch_ns);
    if (!inside) {
        window = compute_nudge_window(sign, d, origin_ns, dt, zone, increment, u, true);
        if (!window) { return std::nullopt; }
        did_expand = true;
    }
    const i128 start = window->start_epoch_ns;
    const i128 end = window->end_epoch_ns;
    if (start == end) { return std::nullopt; }
    // total = r1 + progress x increment x sign, progress = (dest - start) /
    // (end - start): one exact fraction, divided once.
    const i128 span = end - start;
    const i128 moved = dest_ns - start;
    const double total =
        exact_quotient(to_i128(window->r1) * span + moved * increment * sign, span);
    // |total| against |r1| and |r2| exactly: |total| - |r1| is
    // increment x |moved| / |span|, and |r2| - |total| the rest of increment.
    const i128 d1 = abs_of(moved) * increment;
    const i128 d2 = abs_of(span) * increment - d1;
    bool to_r2 = false;
    if (moved == span) {
        to_r2 = true;
    } else {
        to_r2 = rounds_up(get_unsigned_rounding_mode(mode, sign < 0), d1, d2,
                          abs_of(to_i128(window->r1)) / increment);
    }
    calendar_nudge result;
    result.total = total;
    if (to_r2) {
        did_expand = true;
        result.nudge = nudge_result{window->end_duration, end, did_expand};
    } else {
        result.nudge = nudge_result{window->start_duration, start, did_expand};
    }
    return result;
}

std::optional<nudge_result> nudge_to_zoned_time(int sign, const internal_duration & d,
                                                const iso_datetime & dt, std::string_view zone,
                                                std::int64_t increment, unit u,
                                                rounding_mode mode) {
    const auto start = calendar_date_add(dt.date, d.date, overflow::constrain);
    if (!start) { return std::nullopt; }
    const iso_date end_date = add_days_to_iso_date(*start, sign);
    const auto start_ns =
        get_epoch_nanoseconds_for(zone, iso_datetime{*start, dt.time}, disambiguation::compatible);
    const auto end_ns = get_epoch_nanoseconds_for(zone, iso_datetime{end_date, dt.time},
                                                  disambiguation::compatible);
    if (!start_ns || !end_ns) { return std::nullopt; }
    const i128 day_span = *end_ns - *start_ns;
    const i128 length = unit_length_ns(u) * increment;
    auto rounded = round_time_duration(d.time, increment, u, mode);
    if (!rounded) { return std::nullopt; }
    const i128 beyond = *rounded - day_span;
    bool did_round_beyond_day = false;
    std::int64_t day_delta = 0;
    i128 nudged = 0;
    if (time_duration_sign(beyond) != -sign) {
        did_round_beyond_day = true;
        day_delta = sign;
        const i128 again = round_number_to_increment(beyond, length, mode);
        if (abs_of(again) > max_time_duration) { return std::nullopt; }
        rounded = again;
        nudged = again + *end_ns;
    } else {
        nudged = *rounded + *start_ns;
    }
    const auto date = create_date_duration(d.date.years, d.date.months, d.date.weeks,
                                           d.date.days + double(day_delta));
    if (!date) { return std::nullopt; }
    return nudge_result{internal_duration{*date, *rounded}, nudged, did_round_beyond_day};
}

std::optional<nudge_result> nudge_to_day_or_time(const internal_duration & d, i128 dest_ns,
                                                 unit largest, std::int64_t increment,
                                                 unit smallest, rounding_mode mode) {
    const i128 time = d.time + to_i128(d.date.days) * ns_per_day;
    const i128 length = unit_length_ns(smallest);
    const i128 rounded_time = round_number_to_increment(time, length * increment, mode);
    if (abs_of(rounded_time) > max_time_duration) { return std::nullopt; }
    const i128 diff = rounded_time - time;
    const i128 whole_days = time / ns_per_day;
    const i128 rounded_whole_days = rounded_time / ns_per_day;
    const i128 day_delta = rounded_whole_days - whole_days;
    const int day_delta_sign = time_duration_sign(day_delta);
    const bool did_expand_days = day_delta_sign == time_duration_sign(time);
    const i128 nudged = diff + dest_ns;
    i128 days = 0;
    i128 remainder = rounded_time;
    if (is_date_unit(largest)) {
        days = rounded_whole_days;
        remainder = rounded_time - rounded_whole_days * ns_per_day;
    }
    const auto date =
        create_date_duration(d.date.years, d.date.months, d.date.weeks, to_double(days));
    if (!date) { return std::nullopt; }
    return nudge_result{internal_duration{*date, remainder}, nudged, did_expand_days};
}

std::optional<internal_duration> bubble_relative_duration(int sign, internal_duration d,
                                                          i128 nudged_ns, const iso_datetime & dt,
                                                          std::string_view zone, unit largest,
                                                          unit smallest) {
    if (smallest == largest) { return d; }
    for (int index = static_cast<int>(smallest) - 1; index >= static_cast<int>(largest); --index) {
        const auto u = static_cast<unit>(index);
        if (u == unit::week && largest != unit::week) { continue; }
        std::optional<date_duration> end;
        if (u == unit::year) {
            end = create_date_duration(d.date.years + sign, 0, 0, 0);
        } else if (u == unit::month) {
            end = create_date_duration(d.date.years, d.date.months + sign, 0, 0);
        } else {
            end = create_date_duration(d.date.years, d.date.months, d.date.weeks + sign, 0);
        }
        if (!end) { return std::nullopt; }
        const auto end_date = calendar_date_add(dt.date, *end, overflow::constrain);
        if (!end_date) { return std::nullopt; }
        const auto end_ns = epoch_ns_at(zone, *end_date, dt.time);
        if (!end_ns) { return std::nullopt; }
        const int beyond_sign = time_duration_sign(nudged_ns - *end_ns);
        if (beyond_sign == -sign) { break; }
        d = internal_duration{*end, 0};
    }
    return d;
}

} // namespace

std::optional<internal_duration> round_relative_duration(const internal_duration & d,
                                                         i128 origin_ns, i128 dest_ns,
                                                         const iso_datetime & dt,
                                                         std::string_view zone, unit largest,
                                                         std::int64_t increment, unit smallest,
                                                         rounding_mode mode) {
    const bool irregular = is_calendar_unit(smallest) || (!zone.empty() && smallest == unit::day);
    const int sign = internal_duration_sign(d) < 0 ? -1 : 1;
    std::optional<nudge_result> nudge;
    if (irregular) {
        const auto record = nudge_to_calendar_unit(sign, d, origin_ns, dest_ns, dt, zone, increment,
                                                   smallest, mode);
        if (!record) { return std::nullopt; }
        nudge = record->nudge;
    } else if (!zone.empty()) {
        nudge = nudge_to_zoned_time(sign, d, dt, zone, increment, smallest, mode);
    } else {
        nudge = nudge_to_day_or_time(d, dest_ns, largest, increment, smallest, mode);
    }
    if (!nudge) { return std::nullopt; }
    if (nudge->did_expand_calendar_unit && smallest != unit::week) {
        return bubble_relative_duration(sign, nudge->duration, nudge->nudged_epoch_ns, dt, zone,
                                        largest, larger_unit(smallest, unit::day));
    }
    return nudge->duration;
}

std::optional<double> total_relative_duration(const internal_duration & d, i128 origin_ns,
                                              i128 dest_ns, const iso_datetime & dt,
                                              std::string_view zone, unit u) {
    if (is_calendar_unit(u) || (!zone.empty() && u == unit::day)) {
        const int sign = internal_duration_sign(d) < 0 ? -1 : 1;
        const auto record = nudge_to_calendar_unit(sign, d, origin_ns, dest_ns, dt, zone, 1, u,
                                                   rounding_mode::trunc);
        if (!record) { return std::nullopt; }
        return record->total;
    }
    const auto time = add_24_hour_days(d.time, d.date.days);
    if (!time) { return std::nullopt; }
    return total_time_duration(*time, u);
}

std::optional<internal_duration> difference_zoned_datetime(i128 ns1, i128 ns2,
                                                           std::string_view zone, unit largest) {
    if (ns1 == ns2) { return internal_duration{}; }
    const iso_datetime start = get_iso_datetime_for(zone, ns1);
    const iso_datetime end = get_iso_datetime_for(zone, ns2);
    if (compare_iso_date(start.date, end.date) == 0) { return internal_duration{{}, ns2 - ns1}; }
    const int sign = ns2 - ns1 < 0 ? 1 : -1;
    const int max_day_correction = sign == -1 ? 2 : 1;
    int day_correction = 0;
    i128 time = difference_time(start.time, end.time);
    if (time_duration_sign(time) == sign) { ++day_correction; }
    bool success = false;
    iso_datetime intermediate;
    while (day_correction <= max_day_correction && !success) {
        intermediate =
            iso_datetime{add_days_to_iso_date(end.date, day_correction * sign), start.time};
        const auto intermediate_ns =
            get_epoch_nanoseconds_for(zone, intermediate, disambiguation::compatible);
        if (!intermediate_ns) { return std::nullopt; }
        time = ns2 - *intermediate_ns;
        if (sign != time_duration_sign(time)) { success = true; }
        ++day_correction;
    }
    if (!success) { return std::nullopt; }
    const date_duration date =
        calendar_date_until(start.date, intermediate.date, larger_unit(largest, unit::day));
    return internal_duration{date, time};
}

std::optional<internal_duration> difference_zoned_datetime_with_rounding(
    i128 ns1, i128 ns2, std::string_view zone, unit largest, std::int64_t increment, unit smallest,
    rounding_mode mode) {
    if (!is_date_unit(largest)) { return difference_instant(ns1, ns2, increment, smallest, mode); }
    const auto difference = difference_zoned_datetime(ns1, ns2, zone, largest);
    if (!difference) { return std::nullopt; }
    if (smallest == unit::nanosecond && increment == 1) { return difference; }
    return round_relative_duration(*difference, ns1, ns2, get_iso_datetime_for(zone, ns1), zone,
                                   largest, increment, smallest, mode);
}

std::optional<double> difference_zoned_datetime_with_total(i128 ns1, i128 ns2,
                                                           std::string_view zone, unit u) {
    if (!is_date_unit(u)) { return total_time_duration(ns2 - ns1, u); }
    const auto difference = difference_zoned_datetime(ns1, ns2, zone, u);
    if (!difference) { return std::nullopt; }
    return total_relative_duration(*difference, ns1, ns2, get_iso_datetime_for(zone, ns1), zone, u);
}

std::optional<i128> add_zoned_datetime(i128 epoch_ns, std::string_view zone,
                                       const internal_duration & d, overflow how) {
    if (date_duration_sign(d.date) == 0) { return add_instant(epoch_ns, d.time); }
    const iso_datetime dt = get_iso_datetime_for(zone, epoch_ns);
    const auto added = calendar_date_add(dt.date, d.date, how);
    if (!added) { return std::nullopt; }
    const iso_datetime intermediate{*added, dt.time};
    if (!iso_datetime_within_limits(intermediate)) { return std::nullopt; }
    const auto intermediate_ns =
        get_epoch_nanoseconds_for(zone, intermediate, disambiguation::compatible);
    if (!intermediate_ns) { return std::nullopt; }
    return add_instant(*intermediate_ns, d.time);
}

// --- strings -----------------------------------------------------------------

namespace {

[[nodiscard]] std::string zero_padded(std::int64_t v, int width) {
    std::string digits = std::to_string(v);
    if (digits.size() < static_cast<std::size_t>(width)) {
        digits.insert(0, static_cast<std::size_t>(width) - digits.size(), '0');
    }
    return digits;
}

} // namespace

std::string pad_iso_year(std::int64_t year) {
    if (year >= 0 && year <= 9999) { return zero_padded(year, 4); }
    return (year > 0 ? "+" : "-") + zero_padded(year < 0 ? -year : year, 6);
}

std::string format_fractional_seconds(std::int64_t sub_second_ns, int precision) {
    if (precision == precision_auto) {
        if (sub_second_ns == 0) { return {}; }
        std::string fraction = zero_padded(sub_second_ns, 9);
        while (!fraction.empty() && fraction.back() == '0') { fraction.pop_back(); }
        return "." + fraction;
    }
    if (precision <= 0) { return {}; }
    return "." + zero_padded(sub_second_ns, 9).substr(0, static_cast<std::size_t>(precision));
}

std::string format_time_string(int hour, int minute, int second, std::int64_t sub_second_ns,
                               int precision, bool separated) {
    const std::string separator = separated ? ":" : "";
    std::string result = zero_padded(hour, 2) + separator + zero_padded(minute, 2);
    if (precision == precision_minute) { return result; }
    return result + separator + zero_padded(second, 2) +
           format_fractional_seconds(sub_second_ns, precision);
}

std::string format_calendar_annotation(std::string_view id, show_calendar show) {
    if (show == show_calendar::never) { return {}; }
    if (show == show_calendar::auto_ && id == "iso8601") { return {}; }
    return std::string{"["} + (show == show_calendar::critical ? "!" : "") +
           "u-ca=" + std::string{id} + "]";
}

std::string time_record_to_string(const time_record & t, int precision) {
    const std::int64_t sub =
        std::int64_t{t.millisecond} * 1'000'000 + t.microsecond * 1000 + t.nanosecond;
    return format_time_string(t.hour, t.minute, t.second, sub, precision);
}

std::string temporal_date_to_string(const iso_date & date, std::string_view calendar,
                                    show_calendar show) {
    return pad_iso_year(date.year) + "-" + zero_padded(date.month, 2) + "-" +
           zero_padded(date.day, 2) + format_calendar_annotation(calendar, show);
}

std::string iso_datetime_to_string(const iso_datetime & dt, std::string_view calendar,
                                   int precision, show_calendar show) {
    return pad_iso_year(dt.date.year) + "-" + zero_padded(dt.date.month, 2) + "-" +
           zero_padded(dt.date.day, 2) + "T" + time_record_to_string(dt.time, precision) +
           format_calendar_annotation(calendar, show);
}

std::string year_month_to_string(const iso_date & date, std::string_view calendar,
                                 show_calendar show) {
    std::string result = pad_iso_year(date.year) + "-" + zero_padded(date.month, 2);
    if (show == show_calendar::always || show == show_calendar::critical || calendar != "iso8601") {
        result += "-" + zero_padded(date.day, 2);
    }
    return result + format_calendar_annotation(calendar, show);
}

std::string month_day_to_string(const iso_date & date, std::string_view calendar,
                                show_calendar show) {
    std::string result = zero_padded(date.month, 2) + "-" + zero_padded(date.day, 2);
    if (show == show_calendar::always || show == show_calendar::critical || calendar != "iso8601") {
        result = pad_iso_year(date.year) + "-" + result;
    }
    return result + format_calendar_annotation(calendar, show);
}

std::string instant_to_string(i128 epoch_ns, std::string_view zone, int precision) {
    const std::string_view output = zone.empty() ? std::string_view{"UTC"} : zone;
    const iso_datetime dt = get_iso_datetime_for(output, epoch_ns);
    std::string result = iso_datetime_to_string(dt, "iso8601", precision, show_calendar::never);
    if (zone.empty()) { return result + "Z"; }
    return result +
           format_datetime_utc_offset_rounded(get_offset_nanoseconds_for(output, epoch_ns));
}

std::string integer_to_string(double v) {
    if (std::fabs(v) < 1e38) { return i128_to_string(to_i128(v)); }
    char buffer[400];
    std::snprintf(buffer, sizeof buffer, "%.0f", v);
    return buffer;
}

std::string i128_to_string(i128 v) {
    if (v == 0) { return "0"; }
    const bool negative = v < 0;
    std::string digits;
    while (v != 0) {
        const i128 digit = v % 10;
        digits += static_cast<char>('0' + static_cast<int>(negative ? -digit : digit));
        v /= 10;
    }
    if (negative) { digits += '-'; }
    return {digits.rbegin(), digits.rend()};
}

std::string temporal_duration_to_string(const duration_record & d, int precision) {
    const int sign = duration_sign(d);
    const auto part = [](double v, char designator) {
        return v == 0 ? std::string{} : integer_to_string(std::fabs(v)) + designator;
    };
    const std::string date_part =
        part(d.years, 'Y') + part(d.months, 'M') + part(d.weeks, 'W') + part(d.days, 'D');
    std::string time_part = part(d.hours, 'H') + part(d.minutes, 'M');
    const unit largest = default_largest_unit(d);
    const bool zero_minutes_and_higher = largest >= unit::second;
    const i128 seconds = time_duration_from_components(0, 0, d.seconds, d.milliseconds,
                                                       d.microseconds, d.nanoseconds);
    if (seconds != 0 || zero_minutes_and_higher || precision != precision_auto) {
        time_part += i128_to_string(abs_of(seconds / 1'000'000'000)) +
                     format_fractional_seconds(
                         static_cast<std::int64_t>(abs_of(seconds % 1'000'000'000)), precision) +
                     "S";
    }
    std::string result = std::string{sign < 0 ? "-" : ""} + "P" + date_part;
    if (!time_part.empty()) { result += "T" + time_part; }
    return result;
}

std::optional<std::string> canonicalize_calendar(std::string_view id) {
    if (ascii_iequals(id, "iso8601")) { return std::string{"iso8601"}; }
    return std::nullopt;
}

} // namespace ctbrowser::script::temporal
