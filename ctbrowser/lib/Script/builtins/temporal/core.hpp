//===- temporal/core.hpp - Temporal records and arithmetic ------*- C++ -*-===//
//
// Temporal's abstract operations on plain records, with no VM in them: every
// operation here takes and returns records of integers, so the ISO calendar,
// the time arithmetic, rounding, the RFC 9557 grammar and the duration
// algorithms are testable without a context. Reading JavaScript values
// (property bags, options objects, and the order test262 checks each Get
// happens in) belongs to the class layer in the other files of this
// directory, which calls down into this one.
//
// Names follow the abstract operations of the TC39 Temporal proposal
// (tc39.es/proposal-temporal), snake-cased: RoundRelativeDuration is
// round_relative_duration. Section numbers are that specification's. A
// RangeError the specification throws is an empty optional here; the caller
// throws.
//
// One calendar and one named zone: this engine has no ECMA-402, so the only
// calendar is "iso8601" and the only named time zone is "UTC" (what the
// specification requires of an implementation without it), beside the offset
// zones ("+05:30"), which need no database.
//
// Exact arithmetic is on __int128: an epoch-nanosecond value reaches 8.64e21
// and a time duration 9.007e24, both past int64 and past a double's 53 bits,
// and the specification's results are exact.
//
//===----------------------------------------------------------------------===//
#pragma once

#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ctbrowser::script::temporal {

/// A signed 128-bit integer, wide enough for exact nanosecond arithmetic.
__extension__ typedef __int128 i128;

inline constexpr i128 ns_per_day = 86'400'000'000'000;
/// 2**53 x 10**9 - 1 ns (about 9.007e24): the largest time duration (7.5.3).
inline constexpr i128 max_time_duration = (i128{1} << 53) * 1'000'000'000 - 1;
/// nsMaxInstant = 10**8 days, 8.64e21 ns (8.5).
inline constexpr i128 ns_max_instant = ns_per_day * 100'000'000;
inline constexpr i128 ns_min_instant = -ns_max_instant;

// --- units and rounding (13.x, Table 21) -------------------------------------

/// A Temporal unit, in Table 21's order, largest first:
/// LargerOfTwoTemporalUnits compares indices.
enum class unit : std::uint8_t {
    year,
    month,
    week,
    day,
    hour,
    minute,
    second,
    millisecond,
    microsecond,
    nanosecond
};
[[nodiscard]] constexpr unit larger_unit(unit a, unit b) noexcept {
    return a < b ? a : b;
}
[[nodiscard]] constexpr bool is_calendar_unit(unit u) noexcept {
    return u <= unit::week;
}
/// TemporalUnitCategory: date for year..day, time for the rest.
[[nodiscard]] constexpr bool is_date_unit(unit u) noexcept {
    return u <= unit::day;
}
/// Table 21's "Length in Nanoseconds", defined for day and the time units only.
[[nodiscard]] i128 unit_length_ns(unit u) noexcept;
/// MaximumTemporalDurationRoundingIncrement: 24, 60, 1000, or none (0).
[[nodiscard]] std::int64_t maximum_rounding_increment(unit u) noexcept;
/// Singular or plural name to unit; "auto" is left to the caller.
[[nodiscard]] std::optional<unit> unit_from_name(std::string_view name) noexcept;
[[nodiscard]] std::string_view unit_name(unit u) noexcept; // singular

enum class rounding_mode : std::uint8_t {
    ceil,
    floor,
    expand,
    trunc,
    half_ceil,
    half_floor,
    half_expand,
    half_trunc,
    half_even
};
[[nodiscard]] std::optional<rounding_mode> rounding_mode_from_name(std::string_view name) noexcept;
[[nodiscard]] rounding_mode negate_rounding_mode(rounding_mode mode) noexcept;
/// RoundNumberToIncrement on an exact integer.
[[nodiscard]] i128 round_number_to_increment(i128 x, i128 increment, rounding_mode mode) noexcept;
[[nodiscard]] i128 round_number_to_increment_as_if_positive(i128 x, i128 increment,
                                                            rounding_mode mode) noexcept;
/// ValidateTemporalRoundingIncrement.
[[nodiscard]] bool valid_rounding_increment(std::int64_t increment, std::int64_t dividend,
                                            bool inclusive) noexcept;

/// n / d, correctly rounded to the nearest double: TotalTimeDuration and the
/// `total` of NudgeToCalendarUnit, which the specification computes exactly.
[[nodiscard]] double exact_quotient(i128 n, i128 d) noexcept;

enum class overflow : std::uint8_t {
    constrain,
    reject
};
enum class disambiguation : std::uint8_t {
    compatible,
    earlier,
    later,
    reject
};
enum class offset_option : std::uint8_t {
    prefer,
    use,
    ignore,
    reject
};
enum class offset_behaviour : std::uint8_t {
    option,
    exact,
    wall
};
enum class match_behaviour : std::uint8_t {
    exactly,
    minutes
};
enum class show_calendar : std::uint8_t {
    auto_,
    always,
    never,
    critical
};

// --- records -----------------------------------------------------------------

/// An ISO calendar date. The year is 64-bit because an addition may leave the
/// representable range before ISODateWithinLimits refuses it (a year plus
/// 2**32 - 1 years).
struct iso_date {
    std::int64_t year = 1970;
    int month = 1;
    int day = 1;
    friend bool operator==(const iso_date &, const iso_date &) = default;
};

struct time_record {
    std::int64_t days = 0; // the carry out of BalanceTime; ignored elsewhere
    int hour = 0;
    int minute = 0;
    int second = 0;
    int millisecond = 0;
    int microsecond = 0;
    int nanosecond = 0;
};

struct iso_datetime {
    iso_date date;
    time_record time;
};

struct date_duration {
    double years = 0;
    double months = 0;
    double weeks = 0;
    double days = 0;
};

/// A Temporal.Duration's ten slots: float64-representable integers.
struct duration_record {
    double years = 0;
    double months = 0;
    double weeks = 0;
    double days = 0;
    double hours = 0;
    double minutes = 0;
    double seconds = 0;
    double milliseconds = 0;
    double microseconds = 0;
    double nanoseconds = 0;
};

/// An Internal Duration Record: a date duration plus a time duration.
struct internal_duration {
    date_duration date;
    i128 time = 0; // a time duration, in nanoseconds
};

// --- the ISO calendar (12.3, 13.1) -------------------------------------------

[[nodiscard]] bool is_leap_year(std::int64_t year) noexcept;
[[nodiscard]] int iso_days_in_month(std::int64_t year, int month) noexcept;
[[nodiscard]] int iso_days_in_year(std::int64_t year) noexcept;
/// ISODateToEpochDays: `month` is zero-based and may be any integer.
[[nodiscard]] std::int64_t iso_date_to_epoch_days(std::int64_t year, std::int64_t month,
                                                  std::int64_t day) noexcept;
[[nodiscard]] std::int64_t epoch_days(const iso_date & date) noexcept;
[[nodiscard]] iso_date epoch_days_to_iso_date(std::int64_t days) noexcept;
[[nodiscard]] bool is_valid_iso_date(std::int64_t year, std::int64_t month,
                                     std::int64_t day) noexcept;
/// RegulateISODate, from the doubles a property bag produced. A year too large
/// for any valid date is kept as out of range rather than overflowing.
[[nodiscard]] std::optional<iso_date> regulate_iso_date(double year, double month, double day,
                                                        overflow how) noexcept;
[[nodiscard]] iso_date add_days_to_iso_date(const iso_date & date, std::int64_t days) noexcept;
[[nodiscard]] int compare_iso_date(const iso_date & a, const iso_date & b) noexcept;
[[nodiscard]] bool iso_date_within_limits(const iso_date & date) noexcept;
[[nodiscard]] bool iso_year_month_within_limits(const iso_date & date) noexcept;
[[nodiscard]] bool check_iso_days_range(const iso_date & date) noexcept;
struct year_month {
    std::int64_t year;
    int month;
};
[[nodiscard]] year_month balance_iso_year_month(std::int64_t year, std::int64_t month) noexcept;
[[nodiscard]] int iso_day_of_week(const iso_date & date) noexcept; // 1 = Monday
[[nodiscard]] int iso_day_of_year(const iso_date & date) noexcept;
struct year_week {
    int week;
    std::int64_t year;
};
[[nodiscard]] year_week iso_week_of_year(const iso_date & date) noexcept;
/// CalendarDateAdd and CalendarDateUntil for "iso8601".
[[nodiscard]] std::optional<iso_date> calendar_date_add(const iso_date & date,
                                                        const date_duration & duration,
                                                        overflow how) noexcept;
[[nodiscard]] date_duration calendar_date_until(const iso_date & one, const iso_date & two,
                                                unit largest) noexcept;
/// "M01".."M12" <-> 1..12. A leap month ("M05L") is not an ISO month.
[[nodiscard]] std::string month_code(int month);
[[nodiscard]] std::optional<int> parse_month_code(std::string_view code) noexcept;

// --- time (4.5) --------------------------------------------------------------

[[nodiscard]] bool is_valid_time(double hour, double minute, double second, double millisecond,
                                 double microsecond, double nanosecond) noexcept;
[[nodiscard]] std::optional<time_record> regulate_time(double hour, double minute, double second,
                                                       double millisecond, double microsecond,
                                                       double nanosecond, overflow how) noexcept;
[[nodiscard]] time_record balance_time(i128 hour, i128 minute, i128 second, i128 millisecond,
                                       i128 microsecond, i128 nanosecond) noexcept;
[[nodiscard]] i128 difference_time(const time_record & one, const time_record & two) noexcept;
[[nodiscard]] time_record add_time(const time_record & time, i128 duration) noexcept;
[[nodiscard]] time_record round_time(const time_record & time, std::int64_t increment, unit u,
                                     rounding_mode mode) noexcept;
[[nodiscard]] int compare_time(const time_record & a, const time_record & b) noexcept;
[[nodiscard]] i128 time_nanoseconds(const time_record & time) noexcept; // since midnight
[[nodiscard]] inline time_record noon() noexcept {
    return time_record{0, 12, 0, 0, 0, 0, 0};
}

// --- date-times and exact times (5.5, 8.5, 14.6) -----------------------------

[[nodiscard]] i128 get_utc_epoch_nanoseconds(const iso_datetime & dt) noexcept;
[[nodiscard]] iso_datetime balance_iso_datetime(std::int64_t year, int month, int day, i128 hour,
                                                i128 minute, i128 second, i128 millisecond,
                                                i128 microsecond, i128 nanosecond) noexcept;
[[nodiscard]] bool iso_datetime_within_limits(const iso_datetime & dt) noexcept;
[[nodiscard]] int compare_iso_datetime(const iso_datetime & a, const iso_datetime & b) noexcept;
[[nodiscard]] iso_datetime round_iso_datetime(const iso_datetime & dt, std::int64_t increment,
                                              unit u, rounding_mode mode) noexcept;
[[nodiscard]] iso_datetime iso_parts_from_epoch(i128 epoch_ns) noexcept;
[[nodiscard]] bool is_valid_epoch_nanoseconds(i128 epoch_ns) noexcept;
[[nodiscard]] std::optional<i128> add_instant(i128 epoch_ns, i128 duration) noexcept;
[[nodiscard]] internal_duration difference_instant(i128 ns1, i128 ns2, std::int64_t increment,
                                                   unit smallest, rounding_mode mode) noexcept;
[[nodiscard]] i128 round_temporal_instant(i128 epoch_ns, std::int64_t increment, unit u,
                                          rounding_mode mode) noexcept;
[[nodiscard]] internal_duration difference_iso_datetime(const iso_datetime & one,
                                                        const iso_datetime & two,
                                                        unit largest) noexcept;
[[nodiscard]] std::optional<internal_duration> difference_plain_datetime_with_rounding(
    const iso_datetime & one, const iso_datetime & two, unit largest, std::int64_t increment,
    unit smallest, rounding_mode mode);
[[nodiscard]] std::optional<double> difference_plain_datetime_with_total(const iso_datetime & one,
                                                                         const iso_datetime & two,
                                                                         unit u);

// --- time zones (11.1): "UTC" and offset identifiers -------------------------

/// A Time Zone Identifier Parse Record: a name, or an offset in minutes.
struct time_zone_parse {
    std::string name;
    std::optional<int> offset_minutes;
};
[[nodiscard]] std::optional<time_zone_parse> parse_time_zone_identifier(std::string_view id);
[[nodiscard]] bool is_offset_time_zone_identifier(std::string_view id);
/// The canonical identifier for a parse record: "+05:30", or the available
/// named zone's own spelling; empty when the name is not available.
[[nodiscard]] std::optional<std::string> canonical_time_zone(const time_zone_parse & parsed);
[[nodiscard]] bool time_zone_equals(std::string_view one, std::string_view two);
[[nodiscard]] i128 get_offset_nanoseconds_for(std::string_view zone, i128 epoch_ns);
[[nodiscard]] iso_datetime get_iso_datetime_for(std::string_view zone, i128 epoch_ns);
[[nodiscard]] std::optional<std::vector<i128>> get_possible_epoch_nanoseconds(
    std::string_view zone, const iso_datetime & dt);
[[nodiscard]] std::optional<i128> get_epoch_nanoseconds_for(std::string_view zone,
                                                            const iso_datetime & dt,
                                                            disambiguation how);
[[nodiscard]] std::optional<i128> get_start_of_day(std::string_view zone, const iso_date & date);
/// InterpretISODateTimeOffset; an empty `time` means start-of-day.
[[nodiscard]] std::optional<i128> interpret_iso_datetime_offset(
    const iso_date & date, const std::optional<time_record> & time, offset_behaviour behaviour,
    i128 offset_ns, std::string_view zone, disambiguation how, offset_option option,
    match_behaviour match);
[[nodiscard]] std::string format_offset_time_zone_identifier(int offset_minutes,
                                                             bool separated = true);
[[nodiscard]] std::string format_utc_offset_nanoseconds(i128 offset_ns);
[[nodiscard]] std::string format_datetime_utc_offset_rounded(i128 offset_ns);

// --- durations (7.5) ---------------------------------------------------------

[[nodiscard]] bool is_valid_duration(const duration_record & d) noexcept;
[[nodiscard]] int duration_sign(const duration_record & d) noexcept;
[[nodiscard]] int date_duration_sign(const date_duration & d) noexcept;
[[nodiscard]] int internal_duration_sign(const internal_duration & d) noexcept;
[[nodiscard]] constexpr int time_duration_sign(i128 d) noexcept {
    return d < 0 ? -1 : (d > 0 ? 1 : 0);
}
[[nodiscard]] unit default_largest_unit(const duration_record & d) noexcept;
[[nodiscard]] duration_record negate(const duration_record & d) noexcept;
/// TimeDurationFromComponents, from fields IsValidDuration has accepted.
[[nodiscard]] i128 time_duration_from_components(double hours, double minutes, double seconds,
                                                 double milliseconds, double microseconds,
                                                 double nanoseconds) noexcept;
[[nodiscard]] internal_duration to_internal_duration(const duration_record & d) noexcept;
[[nodiscard]] internal_duration to_internal_duration_24h_days(const duration_record & d) noexcept;
[[nodiscard]] std::optional<date_duration> to_date_duration_without_time(
    const duration_record & d) noexcept;
[[nodiscard]] std::optional<date_duration> create_date_duration(double years, double months,
                                                                double weeks, double days) noexcept;
[[nodiscard]] std::optional<duration_record> temporal_duration_from_internal(
    const internal_duration & d, unit largest) noexcept;
[[nodiscard]] std::optional<i128> add_time_duration(i128 one, i128 two) noexcept;
[[nodiscard]] std::optional<i128> add_24_hour_days(i128 d, double days) noexcept;
[[nodiscard]] std::optional<i128> round_time_duration(i128 d, std::int64_t increment, unit u,
                                                      rounding_mode mode) noexcept;
[[nodiscard]] double total_time_duration(i128 d, unit u) noexcept;
/// DateDurationDays against a plain relativeTo date.
[[nodiscard]] std::optional<double> date_duration_days(const date_duration & d,
                                                       const iso_date & relative_to) noexcept;
/// RoundRelativeDuration and TotalRelativeDuration. An empty `zone` means
/// relativeTo is a plain date-time with no time zone.
[[nodiscard]] std::optional<internal_duration> round_relative_duration(
    const internal_duration & d, i128 origin_ns, i128 dest_ns, const iso_datetime & dt,
    std::string_view zone, unit largest, std::int64_t increment, unit smallest, rounding_mode mode);
[[nodiscard]] std::optional<double> total_relative_duration(const internal_duration & d,
                                                            i128 origin_ns, i128 dest_ns,
                                                            const iso_datetime & dt,
                                                            std::string_view zone, unit u);
[[nodiscard]] std::optional<internal_duration> difference_zoned_datetime(i128 ns1, i128 ns2,
                                                                         std::string_view zone,
                                                                         unit largest);
[[nodiscard]] std::optional<internal_duration> difference_zoned_datetime_with_rounding(
    i128 ns1, i128 ns2, std::string_view zone, unit largest, std::int64_t increment, unit smallest,
    rounding_mode mode);
[[nodiscard]] std::optional<double> difference_zoned_datetime_with_total(i128 ns1, i128 ns2,
                                                                         std::string_view zone,
                                                                         unit u);
[[nodiscard]] std::optional<i128> add_zoned_datetime(i128 epoch_ns, std::string_view zone,
                                                     const internal_duration & d, overflow how);

// --- strings -----------------------------------------------------------------

// A precision is 0..9 fractional-second digits, or one of these.
inline constexpr int precision_auto = -1;
inline constexpr int precision_minute = -2;

[[nodiscard]] std::string pad_iso_year(std::int64_t year);
[[nodiscard]] std::string format_fractional_seconds(std::int64_t sub_second_ns, int precision);
[[nodiscard]] std::string format_time_string(int hour, int minute, int second,
                                             std::int64_t sub_second_ns, int precision,
                                             bool separated = true);
[[nodiscard]] std::string format_calendar_annotation(std::string_view id, show_calendar show);
[[nodiscard]] std::string time_record_to_string(const time_record & time, int precision);
[[nodiscard]] std::string temporal_date_to_string(const iso_date & date, std::string_view calendar,
                                                  show_calendar show);
[[nodiscard]] std::string iso_datetime_to_string(const iso_datetime & dt, std::string_view calendar,
                                                 int precision, show_calendar show);
[[nodiscard]] std::string year_month_to_string(const iso_date & date, std::string_view calendar,
                                               show_calendar show);
[[nodiscard]] std::string month_day_to_string(const iso_date & date, std::string_view calendar,
                                              show_calendar show);
/// TemporalInstantToString: an empty `zone` is "undefined" (a Z designator).
[[nodiscard]] std::string instant_to_string(i128 epoch_ns, std::string_view zone, int precision);
[[nodiscard]] std::string temporal_duration_to_string(const duration_record & d, int precision);
/// A float64 integer as the decimal digits of its exact value.
[[nodiscard]] std::string integer_to_string(double v);
[[nodiscard]] std::string i128_to_string(i128 v);

// --- the RFC 9557 / ISO 8601 grammar (13.31-13.38) ---------------------------

/// The grammar goal symbol parse_iso_datetime tries.
enum class goal : std::uint8_t {
    date_time_zoned, // TemporalDateTimeString[+Zoned]
    date_time,       // TemporalDateTimeString[~Zoned]
    instant,         // TemporalInstantString
    time,            // TemporalTimeString
    month_day,       // TemporalMonthDayString
    year_month       // TemporalYearMonthString
};

/// An ISO Date-Time Parse Record.
struct parse_record {
    std::optional<std::int64_t> year; // empty: a month-day string without one
    int month = 1;
    int day = 1;
    std::optional<time_record> time; // empty: start-of-day
    bool z = false;
    std::string offset;     // the UTCOffset[+SubMinutePrecision] text, or empty
    std::string annotation; // the TimeZoneIdentifier text, or empty
    std::string calendar;   // or empty
};

[[nodiscard]] std::optional<parse_record> parse_iso_datetime(std::string_view text,
                                                             std::initializer_list<goal> goals);
/// ParseDateTimeUTCOffset: `sub_minute` false is UTCOffset[~SubMinutePrecision].
[[nodiscard]] std::optional<i128> parse_utc_offset(std::string_view text, bool sub_minute = true);
[[nodiscard]] std::optional<std::string> parse_temporal_calendar_string(std::string_view text);
[[nodiscard]] std::optional<duration_record> parse_temporal_duration_string(std::string_view text);
[[nodiscard]] std::optional<time_zone_parse> parse_temporal_time_zone_string(std::string_view text);
/// CanonicalizeCalendar: "iso8601" in any case is the only available calendar.
[[nodiscard]] std::optional<std::string> canonicalize_calendar(std::string_view id);

} // namespace ctbrowser::script::temporal
