// ctbrowser.script builtins - the Temporal CLASS LAYER's shared vocabulary:
// the eight kinds of Temporal object, their internal slots, the options
// readers, and the ToTemporalX conversions every class calls.
//
// Everything here reads JavaScript values in the order the specification
// Gets them (test262 records the order with proxies and getters), and every
// `bool` function answers false with the exception already thrown - check it
// and return; nothing here throws twice or goes on after a throw.
//
// THE SLOTS are one hidden property, "@#Temporal", holding an array whose
// first element is the kind (see kind_of). "@#" keys are private names: no
// reflection reaches them, so a Temporal object has no own properties, as the
// specification's objects have none.
#pragma once

#include "../internal.hpp"
#include "core.hpp"

#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>

namespace ctbrowser::script::builtins_detail::temporal_js {

namespace tc = ::ctbrowser::script::temporal;
using tc::i128;

enum class kind : std::uint8_t {
    duration,
    plain_date,
    plain_time,
    plain_date_time,
    plain_year_month,
    plain_month_day,
    instant,
    zoned_date_time
};
inline constexpr std::size_t kind_count = 8;

struct plain_date {
    tc::iso_date date;
    std::string calendar = "iso8601";
};
struct plain_date_time {
    tc::iso_datetime dt;
    std::string calendar = "iso8601";
};
struct zoned {
    i128 epoch_ns = 0;
    std::string zone;
    std::string calendar = "iso8601";
};

// --- the intrinsic prototypes ------------------------------------------------

// Kept on %Function.prototype% under a private name, which the collector
// sees and a page cannot reach: a class's own methods must create instances
// of the intrinsic prototype whatever the page has done to the global. NOT
// %Object.prototype%: structuredClone refuses an object carrying a private
// slot, and `structuredClone(Object.prototype)` is an ordinary clone.
void set_prototype(context & cx, kind k, object_object * proto);
[[nodiscard]] object_object * prototype(context & cx, kind k);

// --- the slots ---------------------------------------------------------------

[[nodiscard]] std::optional<kind> kind_of(value v);
[[nodiscard]] std::optional<tc::duration_record> duration_slot(value v);
[[nodiscard]] std::optional<plain_date> date_slot(context & cx, value v);
[[nodiscard]] std::optional<tc::time_record> time_slot(value v);
[[nodiscard]] std::optional<plain_date_time> date_time_slot(context & cx, value v);
[[nodiscard]] std::optional<plain_date> year_month_slot(context & cx, value v);
[[nodiscard]] std::optional<plain_date> month_day_slot(context & cx, value v);
[[nodiscard]] std::optional<i128> instant_slot(value v);
[[nodiscard]] std::optional<zoned> zoned_slot(context & cx, value v);

// RequireInternalSlot on the running native's `this`: the record, or empty
// with the TypeError thrown. `method` names the caller in the message.
[[nodiscard]] std::optional<tc::duration_record> this_duration(context & cx,
                                                               std::string_view method);
[[nodiscard]] std::optional<plain_date> this_date(context & cx, std::string_view method);
[[nodiscard]] std::optional<tc::time_record> this_time(context & cx, std::string_view method);
[[nodiscard]] std::optional<plain_date_time> this_date_time(context & cx, std::string_view method);
[[nodiscard]] std::optional<plain_date> this_year_month(context & cx, std::string_view method);
[[nodiscard]] std::optional<plain_date> this_month_day(context & cx, std::string_view method);
[[nodiscard]] std::optional<i128> this_instant(context & cx, std::string_view method);
[[nodiscard]] std::optional<zoned> this_zoned(context & cx, std::string_view method);

// CreateTemporalX: the checks the specification's Create operation makes
// (a RangeError, and undefined returned), then the object. `self` is the
// object a constructor was handed - context::construct made it from
// NewTarget's prototype - and undefined for a fresh intrinsic instance.
[[nodiscard]] value make_duration(context & cx, const tc::duration_record & d,
                                  value self = value::undefined());
[[nodiscard]] value make_date(context & cx, const plain_date & d, value self = value::undefined());
[[nodiscard]] value make_time(context & cx, const tc::time_record & t,
                              value self = value::undefined());
[[nodiscard]] value make_date_time(context & cx, const plain_date_time & d,
                                   value self = value::undefined());
[[nodiscard]] value make_year_month(context & cx, const plain_date & d,
                                    value self = value::undefined());
[[nodiscard]] value make_month_day(context & cx, const plain_date & d,
                                   value self = value::undefined());
[[nodiscard]] value make_instant(context & cx, i128 epoch_ns, value self = value::undefined());
[[nodiscard]] value make_zoned(context & cx, const zoned & z, value self = value::undefined());

// A range error / type error, thrown; always false, for `return fail(...)`.
bool range_error(context & cx, std::string message);
bool type_error(context & cx, std::string message);

// --- BigInt ------------------------------------------------------------------

[[nodiscard]] value bigint_value(context & cx, i128 v);
// ToBigInt, then exact: false (thrown) for a non-BigInt-convertible value.
[[nodiscard]] bool to_i128_bigint(context & cx, value v, i128 & out);

// --- number conversions (13.39, 13.40, 14.5.1.1) -----------------------------

[[nodiscard]] bool to_integer_with_truncation(context & cx, value v, double & out);
[[nodiscard]] bool to_positive_integer_with_truncation(context & cx, value v, double & out);
[[nodiscard]] bool to_integer_if_integral(context & cx, value v, double & out);
// Get(object, name), with getters and proxies - the observable read.
[[nodiscard]] bool get(context & cx, value object, std::string_view name, value & out);

// --- options (13.x, 14.5.2) --------------------------------------------------

// GetOptionsObject: undefined is a fresh null-prototype object.
[[nodiscard]] bool get_options_object(context & cx, value options, value & out);
// GetOption(options, name, string, allowed, fallback). `fallback` empty with
// `required` false means "undefined": `out` stays empty when absent.
[[nodiscard]] bool get_string_option(context & cx, value options, std::string_view name,
                                     std::initializer_list<std::string_view> allowed,
                                     std::optional<std::string_view> fallback,
                                     std::optional<std::string> & out, bool required = false);
[[nodiscard]] bool get_overflow_option(context & cx, value options, tc::overflow & out);
[[nodiscard]] bool get_disambiguation_option(context & cx, value options, tc::disambiguation & out);
[[nodiscard]] bool get_offset_option(context & cx, value options, tc::offset_option fallback,
                                     tc::offset_option & out);
[[nodiscard]] bool get_show_calendar_option(context & cx, value options, tc::show_calendar & out);
// "timeZoneName": auto / never / critical, in show_calendar's three values.
[[nodiscard]] bool get_show_time_zone_option(context & cx, value options, tc::show_calendar & out);
// "offset" for toString: auto / never; `out` true is "auto".
[[nodiscard]] bool get_show_offset_option(context & cx, value options, bool & out);
[[nodiscard]] bool get_rounding_mode_option(context & cx, value options, tc::rounding_mode fallback,
                                            tc::rounding_mode & out);
[[nodiscard]] bool get_rounding_increment_option(context & cx, value options, std::int64_t & out);
// tc::precision_auto or 0..9.
[[nodiscard]] bool get_fractional_second_digits_option(context & cx, value options, int & out);

// A Temporal-unit-valued option: absent ("unset"), "auto", or a unit.
struct unit_value {
    bool present = false;
    bool is_auto = false;
    tc::unit u = tc::unit::nanosecond;
};
enum class unit_group : std::uint8_t {
    date,
    time,
    datetime
};
[[nodiscard]] bool get_unit_option(context & cx, value options, std::string_view name,
                                   unit_value & out);
// ValidateTemporalUnitValue; `allow_auto` is extraValues « auto ».
[[nodiscard]] bool validate_unit_value(context & cx, const unit_value & v, unit_group group,
                                       bool allow_auto = false,
                                       std::initializer_list<tc::unit> extra = {});

struct difference_settings {
    tc::unit smallest = tc::unit::nanosecond;
    tc::unit largest = tc::unit::nanosecond;
    tc::rounding_mode mode = tc::rounding_mode::trunc;
    std::int64_t increment = 1;
};
// GetDifferenceSettings. `since` negates the rounding mode.
[[nodiscard]] bool get_difference_settings(context & cx, bool since, value options,
                                           unit_group group,
                                           std::initializer_list<tc::unit> disallowed,
                                           tc::unit fallback_smallest,
                                           tc::unit smallest_largest_default,
                                           difference_settings & out);
// ToSecondsStringPrecisionRecord.
struct precision_record {
    int precision = tc::precision_auto;
    tc::unit u = tc::unit::nanosecond;
    std::int64_t increment = 1;
};
[[nodiscard]] precision_record seconds_string_precision(const unit_value & smallest, int digits);

// --- calendars and time zones ------------------------------------------------

[[nodiscard]] bool to_calendar_identifier(context & cx, value v, std::string & out);
[[nodiscard]] bool calendar_with_iso_default(context & cx, value item, std::string & out);
[[nodiscard]] bool to_time_zone_identifier(context & cx, value v, std::string & out);

// --- calendar fields (12.3) --------------------------------------------------

// Enumeration keys of Table 19, as bits.
enum field : std::uint32_t {
    f_year = 1u << 0,
    f_month = 1u << 1,
    f_month_code = 1u << 2,
    f_day = 1u << 3,
    f_hour = 1u << 4,
    f_minute = 1u << 5,
    f_second = 1u << 6,
    f_millisecond = 1u << 7,
    f_microsecond = 1u << 8,
    f_nanosecond = 1u << 9,
    f_offset = 1u << 10,
    f_time_zone = 1u << 11,
};
inline constexpr std::uint32_t date_fields = f_year | f_month | f_month_code | f_day;
inline constexpr std::uint32_t time_fields =
    f_hour | f_minute | f_second | f_millisecond | f_microsecond | f_nanosecond;

// A Calendar Fields Record: empty optionals are "unset".
struct calendar_fields {
    std::optional<double> year, month, day;
    std::optional<int> month_code; // the month number of a parsed "M01".."M12"
    std::optional<double> hour, minute, second, millisecond, microsecond, nanosecond;
    std::optional<std::string> offset;
    std::optional<std::string> time_zone;
};
// PrepareCalendarFields. `partial` is requiredFieldNames = partial; otherwise
// `required` lists the keys whose absence is a TypeError, and the absent time
// fields take their defaults (0).
[[nodiscard]] bool prepare_calendar_fields(context & cx, value fields, std::uint32_t names,
                                           std::uint32_t required, bool partial,
                                           calendar_fields & out);
enum class date_type : std::uint8_t {
    date,
    year_month,
    month_day
};
[[nodiscard]] bool calendar_resolve_fields(context & cx, calendar_fields & f, date_type type);
[[nodiscard]] bool calendar_date_from_fields(context & cx, calendar_fields f, tc::overflow how,
                                             tc::iso_date & out);
[[nodiscard]] bool calendar_year_month_from_fields(context & cx, calendar_fields f,
                                                   tc::overflow how, tc::iso_date & out);
[[nodiscard]] bool calendar_month_day_from_fields(context & cx, calendar_fields f, tc::overflow how,
                                                  tc::iso_date & out);
[[nodiscard]] calendar_fields iso_date_to_fields(const tc::iso_date & date, date_type type);
[[nodiscard]] calendar_fields calendar_merge_fields(const calendar_fields & fields,
                                                    const calendar_fields & additional);
[[nodiscard]] bool interpret_date_time_fields(context & cx, const calendar_fields & f,
                                              tc::overflow how, tc::iso_datetime & out);

// --- conversions -------------------------------------------------------------

[[nodiscard]] bool to_temporal_duration(context & cx, value item, tc::duration_record & out);
// ToTemporalPartialDurationRecord: fields left NaN are "undefined".
[[nodiscard]] bool to_partial_duration(context & cx, value item, tc::duration_record & out);
[[nodiscard]] bool to_temporal_date(context & cx, value item, value options, plain_date & out);
[[nodiscard]] bool to_temporal_time(context & cx, value item, value options, tc::time_record & out);
[[nodiscard]] bool to_time_record_or_midnight(context & cx, value item, tc::time_record & out);
// ToTemporalTimeRecord; `partial` leaves absent fields NaN.
[[nodiscard]] bool to_time_record(context & cx, value item, bool partial, double (&out)[6]);
[[nodiscard]] bool to_temporal_date_time(context & cx, value item, value options,
                                         plain_date_time & out);
[[nodiscard]] bool to_temporal_year_month(context & cx, value item, value options,
                                          plain_date & out);
[[nodiscard]] bool to_temporal_month_day(context & cx, value item, value options, plain_date & out);
[[nodiscard]] bool to_temporal_instant(context & cx, value item, i128 & out);
[[nodiscard]] bool to_temporal_zoned(context & cx, value item, value options, zoned & out);
// IsPartialTemporalObject.
[[nodiscard]] bool is_partial_temporal_object(context & cx, value v, bool & out);
// GetTemporalRelativeToOption: at most one of the two is set.
[[nodiscard]] bool get_relative_to_option(context & cx, value options,
                                          std::optional<plain_date> & plain,
                                          std::optional<zoned> & zoned_out);

// round(x) and total(x): a String argument stands for an options object
// with the one property `key` (round("day") is round({ smallestUnit: "day" })),
// undefined is a TypeError, and anything else is GetOptionsObject's.
[[nodiscard]] bool options_or_string(context & cx, value arg, std::string_view key, value & out);
// round(roundTo) on PlainTime, PlainDateTime, ZonedDateTime and Instant: the
// argument through options_or_string, then roundingIncrement, roundingMode and
// smallestUnit, read in THAT order because a getter can observe it; then a
// required smallestUnit, validated as a time unit or one of `extra`.
[[nodiscard]] bool get_round_options(context & cx, value arg, std::int64_t & increment,
                                     tc::rounding_mode & mode, unit_value & smallest,
                                     std::initializer_list<tc::unit> extra = {});

// A string from a C++ string, as a value.
[[nodiscard]] inline value str(context & cx, std::string s) {
    return cx.string(std::move(s));
}

// --- installation ------------------------------------------------------------

// A class: its constructor (a native under `new`) and prototype, linked, on
// the Temporal namespace object as `name`.
struct class_parts {
    native_object * ctor = nullptr;
    object_object * proto = nullptr;
};
[[nodiscard]] class_parts make_class(context & cx, object_object & ns, kind k, const char * name,
                                     double arity, native_fn construct);
// A getter on a prototype: `get <name>`, { enumerable: false, configurable: true }.
void getter(context & cx, object_object * proto, const std::string & name, native_fn fn);

// The calendar-date getters a date-bearing class shares (3.3.3-3.3.18),
// reading the ISO date out of `this` by kind - a ZonedDateTime's through its
// zone. `which` is a mask of the getters the class has.
enum calendar_getter : std::uint32_t {
    g_calendar_id = 1u << 0,
    g_era = 1u << 1,
    g_era_year = 1u << 2,
    g_year = 1u << 3,
    g_month = 1u << 4,
    g_month_code = 1u << 5,
    g_day = 1u << 6,
    g_day_of_week = 1u << 7,
    g_day_of_year = 1u << 8,
    g_week_of_year = 1u << 9,
    g_year_of_week = 1u << 10,
    g_days_in_week = 1u << 11,
    g_days_in_month = 1u << 12,
    g_days_in_year = 1u << 13,
    g_months_in_year = 1u << 14,
    g_in_leap_year = 1u << 15,
};
inline constexpr std::uint32_t all_calendar_getters = 0xFFFFu;
void install_calendar_getters(context & cx, object_object * proto, kind k, std::uint32_t which);

void install_duration(context & cx, object_object & ns);
void install_plain_date(context & cx, object_object & ns);
void install_plain_time(context & cx, object_object & ns);
void install_plain_date_time(context & cx, object_object & ns);
void install_plain_year_month(context & cx, object_object & ns);
void install_plain_month_day(context & cx, object_object & ns);
void install_instant(context & cx, object_object & ns);
void install_zoned_date_time(context & cx, object_object & ns);
void install_now(context & cx, object_object & ns);

} // namespace ctbrowser::script::builtins_detail::temporal_js
