//===- objects.cpp - Temporal class layer -----------------------*- C++ -*-===//
//
// The shared half of the Temporal JavaScript classes: slots, creation,
// options, calendar fields and the ToTemporalX conversions. See objects.hpp.
// Bare section numbers refer to the Temporal proposal specification.
//
//===----------------------------------------------------------------------===//

#include "objects.hpp"

namespace ctbrowser::script::builtins_detail::temporal_js {

namespace {

// Each Temporal object keeps its internal slots in a private array stored under
// slot_key. Element 0 is the kind tag; the slots for that kind follow. An epoch
// time takes two elements (see push_epoch).
constexpr std::string_view slot_key = CTBROWSER_PRIVATE_KEY("Temporal");
constexpr std::string_view prototypes_key = CTBROWSER_PRIVATE_KEY("TemporalPrototypes");

constexpr const char * kind_names[] = {"Temporal.Duration",       "Temporal.PlainDate",
                                       "Temporal.PlainTime",      "Temporal.PlainDateTime",
                                       "Temporal.PlainYearMonth", "Temporal.PlainMonthDay",
                                       "Temporal.Instant",        "Temporal.ZonedDateTime"};

/// The slot array of \p v when it is a Temporal object of kind \p k, else null.
[[nodiscard]] array_object * slots_of(value v, kind k) {
    if (!v.is_object()) { return nullptr; }
    auto * object = static_cast<object_object *>(v.as_heap());
    const value * held = object->find(slot_key);
    if (held == nullptr || !held->is_array()) { return nullptr; }
    auto * slots = static_cast<array_object *>(held->as_heap());
    if (slots->items.empty() || context::to_number(slots->items[0]) != double(k)) {
        return nullptr;
    }
    return slots;
}

[[nodiscard]] double num(const array_object * slots, std::size_t i) {
    return context::to_number(slots->items[i]);
}
[[nodiscard]] int inum(const array_object * slots, std::size_t i) {
    return static_cast<int>(num(slots, i));
}

[[nodiscard]] std::string text(context & cx, const array_object * slots, std::size_t i) {
    return cx.to_string(slots->items[i]);
}

/// Appends an epoch-nanosecond value as two exact doubles: whole seconds
/// (floored), and the nanoseconds within the second (0 to 999999999). One
/// double cannot hold the i128 exactly.
void push_epoch(std::vector<value> & items, i128 ns) {
    i128 seconds = ns / 1'000'000'000;
    i128 rest = ns % 1'000'000'000;
    if (rest < 0) {
        rest += 1'000'000'000;
        seconds -= 1;
    }
    items.push_back(value::number(static_cast<double>(seconds)));
    items.push_back(value::number(static_cast<double>(rest)));
}
[[nodiscard]] i128 read_epoch(const array_object * slots, std::size_t i) {
    return static_cast<i128>(num(slots, i)) * 1'000'000'000 + static_cast<i128>(num(slots, i + 1));
}

/// The object to fill: the constructor's `this`, or a new intrinsic instance.
[[nodiscard]] object_object * target_object(context & cx, kind k, value self) {
    if (self.is_object()) { return static_cast<object_object *>(self.as_heap()); }
    auto * made = static_cast<object_object *>(cx.make_object().as_heap());
    if (object_object * proto = prototype(cx, k)) { made->prototype = value::object(proto); }
    return made;
}

/// Stores the slot array [k, items...] on the target object and returns it.
[[nodiscard]] value fill(context & cx, kind k, value self, std::vector<value> items) {
    object_object * target = target_object(cx, k, self);
    const value held = value::object(target);
    const context::rooted keep{cx, held};
    const value slots = cx.make_array();
    auto * array = static_cast<array_object *>(slots.as_heap());
    array->items.reserve(items.size() + 1);
    array->items.push_back(value::number(double(k)));
    for (const value & v : items) { array->items.push_back(v); }
    target->define(slot_key, slots, attr_none);
    return held;
}

[[nodiscard]] std::string receiver_message(std::string_view method, kind k) {
    return std::string{method} + " called on an object that is not a " + kind_names[int(k)];
}

} // namespace

bool range_error(context & cx, std::string message) {
    cx.throw_error("RangeError", std::move(message));
    return false;
}
bool type_error(context & cx, std::string message) {
    cx.throw_error("TypeError", std::move(message));
    return false;
}

// --- prototypes --------------------------------------------------------------

void set_prototype(context & cx, kind k, object_object * proto) {
    object_object * anchor = cx.prototype(context::proto_kind::function);
    if (anchor == nullptr) { return; }
    value * held = anchor->find(prototypes_key);
    if (held == nullptr) {
        const value list = cx.make_array();
        static_cast<array_object *>(list.as_heap())->items.assign(kind_count, value::undefined());
        anchor->define(prototypes_key, list, attr_none);
        held = anchor->find(prototypes_key);
    }
    static_cast<array_object *>(held->as_heap())->items[std::size_t(k)] = value::object(proto);
}

object_object * prototype(context & cx, kind k) {
    object_object * anchor = cx.prototype(context::proto_kind::function);
    if (anchor == nullptr) { return nullptr; }
    const value * held = anchor->find(prototypes_key);
    if (held == nullptr || !held->is_array()) { return nullptr; }
    const value proto = static_cast<array_object *>(held->as_heap())->items[std::size_t(k)];
    return proto.is_object() ? static_cast<object_object *>(proto.as_heap()) : nullptr;
}

// --- slots -------------------------------------------------------------------

std::optional<kind> kind_of(value v) {
    if (!v.is_object()) { return std::nullopt; }
    const value * held = static_cast<object_object *>(v.as_heap())->find(slot_key);
    if (held == nullptr || !held->is_array()) { return std::nullopt; }
    const auto * slots = static_cast<array_object *>(held->as_heap());
    if (slots->items.empty()) { return std::nullopt; }
    return static_cast<kind>(static_cast<int>(context::to_number(slots->items[0])));
}

std::optional<tc::duration_record> duration_slot(value v) {
    const array_object * s = slots_of(v, kind::duration);
    if (s == nullptr) { return std::nullopt; }
    return tc::duration_record{num(s, 1), num(s, 2), num(s, 3), num(s, 4), num(s, 5),
                               num(s, 6), num(s, 7), num(s, 8), num(s, 9), num(s, 10)};
}

namespace {
[[nodiscard]] std::optional<plain_date> date_like_slot(context & cx, value v, kind k) {
    const array_object * s = slots_of(v, k);
    if (s == nullptr) { return std::nullopt; }
    return plain_date{tc::iso_date{static_cast<std::int64_t>(num(s, 1)), inum(s, 2), inum(s, 3)},
                      text(cx, s, 4)};
}
} // namespace

std::optional<plain_date> date_slot(context & cx, value v) {
    return date_like_slot(cx, v, kind::plain_date);
}
std::optional<plain_date> year_month_slot(context & cx, value v) {
    return date_like_slot(cx, v, kind::plain_year_month);
}
std::optional<plain_date> month_day_slot(context & cx, value v) {
    return date_like_slot(cx, v, kind::plain_month_day);
}

std::optional<tc::time_record> time_slot(value v) {
    const array_object * s = slots_of(v, kind::plain_time);
    if (s == nullptr) { return std::nullopt; }
    return tc::time_record{0,          inum(s, 1), inum(s, 2), inum(s, 3),
                           inum(s, 4), inum(s, 5), inum(s, 6)};
}

std::optional<plain_date_time> date_time_slot(context & cx, value v) {
    const array_object * s = slots_of(v, kind::plain_date_time);
    if (s == nullptr) { return std::nullopt; }
    plain_date_time d;
    d.dt.date = tc::iso_date{static_cast<std::int64_t>(num(s, 1)), inum(s, 2), inum(s, 3)};
    d.dt.time =
        tc::time_record{0, inum(s, 4), inum(s, 5), inum(s, 6), inum(s, 7), inum(s, 8), inum(s, 9)};
    d.calendar = text(cx, s, 10);
    return d;
}

std::optional<i128> instant_slot(value v) {
    const array_object * s = slots_of(v, kind::instant);
    if (s == nullptr) { return std::nullopt; }
    return read_epoch(s, 1);
}

std::optional<zoned> zoned_slot(context & cx, value v) {
    const array_object * s = slots_of(v, kind::zoned_date_time);
    if (s == nullptr) { return std::nullopt; }
    return zoned{read_epoch(s, 1), text(cx, s, 3), text(cx, s, 4)};
}

namespace {
template <class T, class F>
[[nodiscard]] std::optional<T> require(context & cx, std::string_view method, kind k, F read) {
    auto found = read(cx.current_this());
    if (!found) { (void)type_error(cx, receiver_message(method, k)); }
    return found;
}
} // namespace

std::optional<tc::duration_record> this_duration(context & cx, std::string_view method) {
    return require<tc::duration_record>(cx, method, kind::duration, duration_slot);
}
std::optional<plain_date> this_date(context & cx, std::string_view method) {
    return require<plain_date>(cx, method, kind::plain_date,
                               [&cx](value v) { return date_slot(cx, v); });
}
std::optional<tc::time_record> this_time(context & cx, std::string_view method) {
    return require<tc::time_record>(cx, method, kind::plain_time, time_slot);
}
std::optional<plain_date_time> this_date_time(context & cx, std::string_view method) {
    return require<plain_date_time>(cx, method, kind::plain_date_time,
                                    [&cx](value v) { return date_time_slot(cx, v); });
}
std::optional<plain_date> this_year_month(context & cx, std::string_view method) {
    return require<plain_date>(cx, method, kind::plain_year_month,
                               [&cx](value v) { return year_month_slot(cx, v); });
}
std::optional<plain_date> this_month_day(context & cx, std::string_view method) {
    return require<plain_date>(cx, method, kind::plain_month_day,
                               [&cx](value v) { return month_day_slot(cx, v); });
}
std::optional<i128> this_instant(context & cx, std::string_view method) {
    return require<i128>(cx, method, kind::instant, instant_slot);
}
std::optional<zoned> this_zoned(context & cx, std::string_view method) {
    return require<zoned>(cx, method, kind::zoned_date_time,
                          [&cx](value v) { return zoned_slot(cx, v); });
}

namespace {
[[nodiscard]] std::optional<std::string> calendar_of_object(context & cx, value v) {
    const auto k = kind_of(v);
    if (!k) { return std::nullopt; }
    switch (*k) {
    case kind::plain_date:
    case kind::plain_year_month:
    case kind::plain_month_day: return text(cx, slots_of(v, *k), 4);
    case kind::plain_date_time: return text(cx, slots_of(v, *k), 10);
    case kind::zoned_date_time: return text(cx, slots_of(v, *k), 4);
    default: return std::nullopt;
    }
}
} // namespace

// --- creation ----------------------------------------------------------------

value make_duration(context & cx, const tc::duration_record & d, value self) {
    if (!tc::is_valid_duration(d)) {
        (void)range_error(cx, "invalid duration");
        return value::undefined();
    }
    const auto n = [](double v) { return value::number(v + 0.0); };
    return fill(cx, kind::duration, self,
                {n(d.years), n(d.months), n(d.weeks), n(d.days), n(d.hours), n(d.minutes),
                 n(d.seconds), n(d.milliseconds), n(d.microseconds), n(d.nanoseconds)});
}

namespace {
[[nodiscard]] value date_like(context & cx, kind k, const plain_date & d, value self) {
    return fill(cx, k, self,
                {value::number(double(d.date.year)), value::number(d.date.month),
                 value::number(d.date.day), cx.string(d.calendar)});
}
} // namespace

value make_date(context & cx, const plain_date & d, value self) {
    if (!tc::iso_date_within_limits(d.date)) {
        (void)range_error(cx, "date outside the representable range");
        return value::undefined();
    }
    return date_like(cx, kind::plain_date, d, self);
}

value make_year_month(context & cx, const plain_date & d, value self) {
    if (!tc::iso_year_month_within_limits(d.date)) {
        (void)range_error(cx, "year-month outside the representable range");
        return value::undefined();
    }
    return date_like(cx, kind::plain_year_month, d, self);
}

value make_month_day(context & cx, const plain_date & d, value self) {
    if (!tc::iso_date_within_limits(d.date)) {
        (void)range_error(cx, "month-day outside the representable range");
        return value::undefined();
    }
    return date_like(cx, kind::plain_month_day, d, self);
}

value make_time(context & cx, const tc::time_record & t, value self) {
    return fill(cx, kind::plain_time, self,
                {value::number(t.hour), value::number(t.minute), value::number(t.second),
                 value::number(t.millisecond), value::number(t.microsecond),
                 value::number(t.nanosecond)});
}

value make_date_time(context & cx, const plain_date_time & d, value self) {
    if (!tc::iso_datetime_within_limits(d.dt)) {
        (void)range_error(cx, "date-time outside the representable range");
        return value::undefined();
    }
    const tc::time_record & t = d.dt.time;
    return fill(cx, kind::plain_date_time, self,
                {value::number(double(d.dt.date.year)), value::number(d.dt.date.month),
                 value::number(d.dt.date.day), value::number(t.hour), value::number(t.minute),
                 value::number(t.second), value::number(t.millisecond),
                 value::number(t.microsecond), value::number(t.nanosecond), cx.string(d.calendar)});
}

value make_instant(context & cx, i128 epoch_ns, value self) {
    if (!tc::is_valid_epoch_nanoseconds(epoch_ns)) {
        (void)range_error(cx, "instant outside the representable range");
        return value::undefined();
    }
    std::vector<value> items;
    push_epoch(items, epoch_ns);
    return fill(cx, kind::instant, self, std::move(items));
}

value make_zoned(context & cx, const zoned & z, value self) {
    if (!tc::is_valid_epoch_nanoseconds(z.epoch_ns)) {
        (void)range_error(cx, "instant outside the representable range");
        return value::undefined();
    }
    std::vector<value> items;
    push_epoch(items, z.epoch_ns);
    items.push_back(cx.string(z.zone));
    items.push_back(cx.string(z.calendar));
    return fill(cx, kind::zoned_date_time, self, std::move(items));
}

// --- BigInt ------------------------------------------------------------------

value bigint_value(context & cx, i128 v) {
    const auto parsed = bigint_from_string(tc::i128_to_string(v));
    return value::object(cx.allocate<bigint_object>(parsed ? *parsed : bigint{0}));
}

bool to_i128_bigint(context & cx, value v, i128 & out) {
    bigint big;
    if (!to_bigint(cx, v, big)) { return false; }
    // Anything beyond +/-1e26 ns is far outside the valid instant range. Clamp to
    // 10x the instant limits so callers still reject it, and the digit loop below
    // cannot overflow i128.
    const bigint limit{"100000000000000000000000000"}; // 1e26
    if (big > limit) {
        out = tc::ns_max_instant * 10;
        return true;
    }
    if (big < -limit) {
        out = tc::ns_min_instant * 10;
        return true;
    }
    const std::string digits = bigint_to_string(big);
    i128 result = 0;
    const bool negative = !digits.empty() && digits[0] == '-';
    for (std::size_t i = negative ? 1 : 0; i < digits.size(); ++i) {
        result = result * 10 + (digits[i] - '0');
    }
    out = negative ? -result : result;
    return true;
}

// --- numbers -----------------------------------------------------------------

// An exception may already have been delivered. One the VM raises itself (a
// revoked proxy, ToPrimitive with nothing callable) unwinds to its handler at
// once and leaves no pending exception, so each helper checks
// detail::unwind_watch (builtins/internal.hpp) instead of throwing again,
// which would consume a second handler.
bool get(context & cx, value object, std::string_view name, value & out) {
    const detail::unwind_watch watch{cx};
    out = cx.lookup_property(object, std::string{name});
    return !watch.threw();
}

bool to_integer_with_truncation(context & cx, value v, double & out) {
    if (!numeric_arg(cx, v)) { return false; }
    if (v.is_kind(heap_kind::bigint)) {
        return type_error(cx, "Cannot convert a BigInt value to a number");
    }
    const detail::unwind_watch watch{cx};
    const double number = cx.to_number_value(v);
    if (watch.threw()) { return false; }
    if (!std::isfinite(number)) { return range_error(cx, "the value must be a finite number"); }
    out = std::trunc(number) + 0.0;
    return true;
}

bool to_positive_integer_with_truncation(context & cx, value v, double & out) {
    if (!to_integer_with_truncation(cx, v, out)) { return false; }
    if (out <= 0) { return range_error(cx, "the value must be a positive integer"); }
    return true;
}

bool to_integer_if_integral(context & cx, value v, double & out) {
    if (!numeric_arg(cx, v)) { return false; }
    if (v.is_kind(heap_kind::bigint)) {
        return type_error(cx, "Cannot convert a BigInt value to a number");
    }
    const detail::unwind_watch watch{cx};
    const double number = cx.to_number_value(v);
    if (watch.threw()) { return false; }
    if (!std::isfinite(number) || std::trunc(number) != number) {
        return range_error(cx, "the value must be an integer");
    }
    out = number + 0.0;
    return true;
}

// --- options -----------------------------------------------------------------

bool get_options_object(context & cx, value options, value & out) {
    if (options.is_undefined()) {
        out = cx.make_object();
        static_cast<object_object *>(out.as_heap())->prototype = value::undefined();
        return true;
    }
    if (options.is_object_like()) {
        out = options;
        return true;
    }
    return type_error(cx, "options must be an object or undefined");
}

bool get_string_option(context & cx, value options, std::string_view name,
                       std::initializer_list<std::string_view> allowed,
                       std::optional<std::string_view> fallback, std::optional<std::string> & out,
                       bool required) {
    value v;
    if (!get(cx, options, name, v)) { return false; }
    if (v.is_undefined()) {
        if (required) { return range_error(cx, std::string{name} + " is required"); }
        if (fallback) {
            out = std::string{*fallback};
        } else {
            out.reset();
        }
        return true;
    }
    const detail::unwind_watch watch{cx};
    std::string s = string_arg(cx, v);
    if (watch.threw()) { return false; }
    if (allowed.size() != 0 && std::find(allowed.begin(), allowed.end(), s) == allowed.end()) {
        return range_error(cx, "invalid value for " + std::string{name} + ": " + s);
    }
    out = std::move(s);
    return true;
}

bool get_overflow_option(context & cx, value options, tc::overflow & out) {
    std::optional<std::string> s;
    if (!get_string_option(cx, options, "overflow", {"constrain", "reject"}, "constrain", s)) {
        return false;
    }
    out = *s == "constrain" ? tc::overflow::constrain : tc::overflow::reject;
    return true;
}

bool get_disambiguation_option(context & cx, value options, tc::disambiguation & out) {
    std::optional<std::string> s;
    if (!get_string_option(cx, options, "disambiguation",
                           {"compatible", "earlier", "later", "reject"}, "compatible", s)) {
        return false;
    }
    out = *s == "compatible" ? tc::disambiguation::compatible
          : *s == "earlier"  ? tc::disambiguation::earlier
          : *s == "later"    ? tc::disambiguation::later
                             : tc::disambiguation::reject;
    return true;
}

bool get_offset_option(context & cx, value options, tc::offset_option fallback,
                       tc::offset_option & out) {
    static constexpr std::string_view names[] = {"prefer", "use", "ignore", "reject"};
    std::optional<std::string> s;
    if (!get_string_option(cx, options, "offset", {"prefer", "use", "ignore", "reject"},
                           names[int(fallback)], s)) {
        return false;
    }
    for (int i = 0; i < 4; ++i) {
        if (*s == names[i]) { out = static_cast<tc::offset_option>(i); }
    }
    return true;
}

bool get_show_calendar_option(context & cx, value options, tc::show_calendar & out) {
    std::optional<std::string> s;
    if (!get_string_option(cx, options, "calendarName", {"auto", "always", "never", "critical"},
                           "auto", s)) {
        return false;
    }
    out = *s == "always"     ? tc::show_calendar::always
          : *s == "never"    ? tc::show_calendar::never
          : *s == "critical" ? tc::show_calendar::critical
                             : tc::show_calendar::auto_;
    return true;
}

bool get_show_time_zone_option(context & cx, value options, tc::show_calendar & out) {
    std::optional<std::string> s;
    if (!get_string_option(cx, options, "timeZoneName", {"auto", "never", "critical"}, "auto", s)) {
        return false;
    }
    out = *s == "never"      ? tc::show_calendar::never
          : *s == "critical" ? tc::show_calendar::critical
                             : tc::show_calendar::auto_;
    return true;
}

bool get_show_offset_option(context & cx, value options, bool & out) {
    std::optional<std::string> s;
    if (!get_string_option(cx, options, "offset", {"auto", "never"}, "auto", s)) { return false; }
    out = *s == "auto";
    return true;
}

bool get_rounding_mode_option(context & cx, value options, tc::rounding_mode fallback,
                              tc::rounding_mode & out) {
    static constexpr std::string_view names[] = {"ceil",       "floor",     "expand",
                                                 "trunc",      "halfCeil",  "halfFloor",
                                                 "halfExpand", "halfTrunc", "halfEven"};
    std::optional<std::string> s;
    if (!get_string_option(cx, options, "roundingMode",
                           {"ceil", "floor", "expand", "trunc", "halfCeil", "halfFloor",
                            "halfExpand", "halfTrunc", "halfEven"},
                           names[int(fallback)], s)) {
        return false;
    }
    out = *tc::rounding_mode_from_name(*s);
    return true;
}

bool get_rounding_increment_option(context & cx, value options, std::int64_t & out) {
    value v;
    if (!get(cx, options, "roundingIncrement", v)) { return false; }
    if (v.is_undefined()) {
        out = 1;
        return true;
    }
    double n = 0;
    if (!to_integer_with_truncation(cx, v, n)) { return false; }
    if (n < 1 || n > 1e9) { return range_error(cx, "roundingIncrement out of range"); }
    out = static_cast<std::int64_t>(n);
    return true;
}

bool get_fractional_second_digits_option(context & cx, value options, int & out) {
    value v;
    if (!get(cx, options, "fractionalSecondDigits", v)) { return false; }
    if (v.is_undefined()) {
        out = tc::precision_auto;
        return true;
    }
    if (!v.is_number()) {
        const detail::unwind_watch watch{cx};
        const std::string s = string_arg(cx, v);
        if (watch.threw()) { return false; }
        if (s != "auto") { return range_error(cx, "fractionalSecondDigits must be 0-9 or auto"); }
        out = tc::precision_auto;
        return true;
    }
    const double n = v.as_number();
    if (!std::isfinite(n)) { return range_error(cx, "fractionalSecondDigits must be finite"); }
    const double digits = std::floor(n);
    if (digits < 0 || digits > 9) { return range_error(cx, "fractionalSecondDigits out of range"); }
    out = static_cast<int>(digits);
    return true;
}

bool get_unit_option(context & cx, value options, std::string_view name, unit_value & out) {
    std::optional<std::string> s;
    if (!get_string_option(cx, options, name,
                           {"year",        "years",        "month",       "months",
                            "week",        "weeks",        "day",         "days",
                            "hour",        "hours",        "minute",      "minutes",
                            "second",      "seconds",      "millisecond", "milliseconds",
                            "microsecond", "microseconds", "nanosecond",  "nanoseconds",
                            "auto"},
                           std::nullopt, s)) {
        return false;
    }
    out = unit_value{};
    if (!s) { return true; }
    out.present = true;
    if (*s == "auto") {
        out.is_auto = true;
        return true;
    }
    out.u = *tc::unit_from_name(*s);
    return true;
}

bool validate_unit_value(context & cx, const unit_value & v, unit_group group, bool allow_auto,
                         std::initializer_list<tc::unit> extra) {
    if (!v.present) { return true; }
    if (v.is_auto) { return allow_auto ? true : range_error(cx, "\"auto\" is not allowed here"); }
    if (std::find(extra.begin(), extra.end(), v.u) != extra.end()) { return true; }
    const bool date = tc::is_date_unit(v.u);
    if (date && (group == unit_group::date || group == unit_group::datetime)) { return true; }
    if (!date && (group == unit_group::time || group == unit_group::datetime)) { return true; }
    return range_error(cx, "invalid unit " + std::string{tc::unit_name(v.u)});
}

bool get_difference_settings(context & cx, bool since, value options, unit_group group,
                             std::initializer_list<tc::unit> disallowed, tc::unit fallback_smallest,
                             tc::unit smallest_largest_default, difference_settings & out) {
    unit_value largest;
    std::int64_t increment = 1;
    tc::rounding_mode mode = tc::rounding_mode::trunc;
    unit_value smallest;
    if (!get_unit_option(cx, options, "largestUnit", largest) ||
        !get_rounding_increment_option(cx, options, increment) ||
        !get_rounding_mode_option(cx, options, tc::rounding_mode::trunc, mode) ||
        !get_unit_option(cx, options, "smallestUnit", smallest)) {
        return false;
    }
    if (!validate_unit_value(cx, largest, group, true)) { return false; }
    const auto banned = [&](tc::unit u) {
        return std::find(disallowed.begin(), disallowed.end(), u) != disallowed.end();
    };
    if (largest.present && !largest.is_auto && banned(largest.u)) {
        return range_error(cx, "largestUnit not allowed here");
    }
    if (!validate_unit_value(cx, smallest, group)) { return false; }
    const tc::unit small = smallest.present ? smallest.u : fallback_smallest;
    if (banned(small)) { return range_error(cx, "smallestUnit not allowed here"); }
    const tc::unit default_largest = tc::larger_unit(smallest_largest_default, small);
    const tc::unit large = largest.present && !largest.is_auto ? largest.u : default_largest;
    if (tc::larger_unit(large, small) != large) {
        return range_error(cx, "largestUnit must be larger than smallestUnit");
    }
    if (const std::int64_t maximum = tc::maximum_rounding_increment(small);
        maximum != 0 && !tc::valid_rounding_increment(increment, maximum, false)) {
        return range_error(cx, "invalid roundingIncrement");
    }
    out =
        difference_settings{small, large, since ? tc::negate_rounding_mode(mode) : mode, increment};
    return true;
}

precision_record seconds_string_precision(const unit_value & smallest, int digits) {
    if (smallest.present) {
        switch (smallest.u) {
        case tc::unit::minute: return {tc::precision_minute, tc::unit::minute, 1};
        case tc::unit::second: return {0, tc::unit::second, 1};
        case tc::unit::millisecond: return {3, tc::unit::millisecond, 1};
        case tc::unit::microsecond: return {6, tc::unit::microsecond, 1};
        default: return {9, tc::unit::nanosecond, 1};
        }
    }
    if (digits == tc::precision_auto) { return {tc::precision_auto, tc::unit::nanosecond, 1}; }
    if (digits == 0) { return {0, tc::unit::second, 1}; }
    std::int64_t increment = 1;
    for (int i = digits; i < (digits <= 3 ? 3 : digits <= 6 ? 6 : 9); ++i) { increment *= 10; }
    if (digits <= 3) { return {digits, tc::unit::millisecond, increment}; }
    if (digits <= 6) { return {digits, tc::unit::microsecond, increment}; }
    return {digits, tc::unit::nanosecond, increment};
}

// --- calendars and time zones ------------------------------------------------

bool to_calendar_identifier(context & cx, value v, std::string & out) {
    if (v.is_object()) {
        if (auto calendar = calendar_of_object(cx, v)) {
            out = *calendar;
            return true;
        }
    }
    if (!v.is_string()) { return type_error(cx, "a calendar must be a string"); }
    const auto parsed = tc::parse_temporal_calendar_string(cx.to_string(v));
    if (!parsed) { return range_error(cx, "invalid calendar"); }
    const auto canonical = tc::canonicalize_calendar(*parsed);
    if (!canonical) { return range_error(cx, "unsupported calendar " + *parsed); }
    out = *canonical;
    return true;
}

bool calendar_with_iso_default(context & cx, value item, std::string & out) {
    if (auto calendar = calendar_of_object(cx, item)) {
        out = *calendar;
        return true;
    }
    value calendar_like;
    if (!get(cx, item, "calendar", calendar_like)) { return false; }
    if (calendar_like.is_undefined()) {
        out = "iso8601";
        return true;
    }
    return to_calendar_identifier(cx, calendar_like, out);
}

bool to_time_zone_identifier(context & cx, value v, std::string & out) {
    if (v.is_object()) {
        if (auto z = zoned_slot(cx, v)) {
            out = z->zone;
            return true;
        }
    }
    if (!v.is_string()) { return type_error(cx, "a time zone must be a string"); }
    const auto parsed = tc::parse_temporal_time_zone_string(cx.to_string(v));
    if (!parsed) { return range_error(cx, "invalid time zone"); }
    const auto canonical = tc::canonical_time_zone(*parsed);
    if (!canonical) { return range_error(cx, "unsupported time zone " + parsed->name); }
    out = *canonical;
    return true;
}

// --- calendar fields ---------------------------------------------------------

namespace {

// The calendar fields from the Temporal spec's Calendar Fields Record table,
// sorted by property key: the order PrepareCalendarFields reads them.
struct field_row {
    std::string_view key;
    field bit;
};
constexpr field_row sorted_fields[] = {
    {"day", f_day},
    {"hour", f_hour},
    {"microsecond", f_microsecond},
    {"millisecond", f_millisecond},
    {"minute", f_minute},
    {"month", f_month},
    {"monthCode", f_month_code},
    {"nanosecond", f_nanosecond},
    {"offset", f_offset},
    {"second", f_second},
    {"timeZone", f_time_zone},
    {"year", f_year},
};

/// Parses a monthCode value (ParseMonthCode). Syntax errors throw here as a
/// TypeError or RangeError; leap months are accepted here and rejected later by
/// CalendarResolveFields with a RangeError. On success \p out is the month
/// number, negated for a leap month ("M05L" gives -5, "M00L" gives 0).
[[nodiscard]] bool parse_month_code_value(context & cx, value v, int & out) {
    value prim = v;
    if (v.is_object_like() && !cx.to_primitive_hint(v, "string", prim)) { return false; }
    if (!prim.is_string()) { return type_error(cx, "monthCode must be a string"); }
    const std::string code = cx.to_string(prim);
    // MonthCode ::: M00L | M0 NonZeroDigit L? | M NonZeroDigit DecimalDigit L?
    const bool leap = code.size() == 4 && code[3] == 'L';
    if ((code.size() != 3 && !leap) || code[0] != 'M' || code[1] < '0' || code[1] > '9' ||
        code[2] < '0' || code[2] > '9') {
        return range_error(cx, "invalid monthCode " + code);
    }
    const int number = (code[1] - '0') * 10 + (code[2] - '0');
    if (number == 0 && !leap) { return range_error(cx, "invalid monthCode " + code); }
    out = leap ? -number : number;
    return true;
}

} // namespace

bool prepare_calendar_fields(context & cx, value fields, std::uint32_t names,
                             std::uint32_t required, bool partial, calendar_fields & out) {
    out = calendar_fields{};
    bool any = false;
    for (const field_row & row : sorted_fields) {
        if ((names & row.bit) == 0) { continue; }
        value v;
        if (!get(cx, fields, row.key, v)) { return false; }
        if (v.is_undefined()) {
            if (!partial) {
                if ((required & row.bit) != 0) {
                    return type_error(cx,
                                      "required property " + std::string{row.key} + " is missing");
                }
                if ((row.bit & time_fields) != 0) {
                    switch (row.bit) {
                    case f_hour: out.hour = 0; break;
                    case f_minute: out.minute = 0; break;
                    case f_second: out.second = 0; break;
                    case f_millisecond: out.millisecond = 0; break;
                    case f_microsecond: out.microsecond = 0; break;
                    default: out.nanosecond = 0; break;
                    }
                }
            }
            continue;
        }
        any = true;
        double n = 0;
        switch (row.bit) {
        case f_year:
            if (!to_integer_with_truncation(cx, v, n)) { return false; }
            out.year = n;
            break;
        case f_month:
            if (!to_positive_integer_with_truncation(cx, v, n)) { return false; }
            out.month = n;
            break;
        case f_day:
            if (!to_positive_integer_with_truncation(cx, v, n)) { return false; }
            out.day = n;
            break;
        case f_month_code: {
            int code = 0;
            if (!parse_month_code_value(cx, v, code)) { return false; }
            out.month_code = code;
            break;
        }
        case f_offset: {
            value prim = v;
            if (v.is_object_like() && !cx.to_primitive_hint(v, "string", prim)) { return false; }
            if (!prim.is_string()) { return type_error(cx, "offset must be a string"); }
            std::string text_value = cx.to_string(prim);
            if (!tc::parse_utc_offset(text_value)) {
                return range_error(cx, "invalid offset " + text_value);
            }
            out.offset = std::move(text_value);
            break;
        }
        case f_time_zone: {
            std::string zone;
            if (!to_time_zone_identifier(cx, v, zone)) { return false; }
            out.time_zone = std::move(zone);
            break;
        }
        default:
            if (!to_integer_with_truncation(cx, v, n)) { return false; }
            switch (row.bit) {
            case f_hour: out.hour = n; break;
            case f_minute: out.minute = n; break;
            case f_second: out.second = n; break;
            case f_millisecond: out.millisecond = n; break;
            case f_microsecond: out.microsecond = n; break;
            default: out.nanosecond = n; break;
            }
        }
    }
    if (partial && !any) { return type_error(cx, "at least one property is required"); }
    return true;
}

bool calendar_resolve_fields(context & cx, calendar_fields & f, date_type type) {
    const bool needs_year = type == date_type::date || type == date_type::year_month;
    const bool needs_day = type == date_type::date || type == date_type::month_day;
    if (needs_year && !f.year) { return type_error(cx, "year is required"); }
    if (needs_day && !f.day) { return type_error(cx, "day is required"); }
    if (!f.month && !f.month_code) { return type_error(cx, "month or monthCode is required"); }
    if (f.month_code) {
        if (*f.month_code <= 0) { return range_error(cx, "the ISO calendar has no leap months"); }
        const int month = *f.month_code;
        if (month > 12) { return range_error(cx, "invalid monthCode"); }
        if (f.month && *f.month != month) {
            return range_error(cx, "month and monthCode disagree");
        }
        f.month = month;
    }
    return true;
}

bool calendar_date_from_fields(context & cx, calendar_fields f, tc::overflow how,
                               tc::iso_date & out) {
    if (!calendar_resolve_fields(cx, f, date_type::date)) { return false; }
    const auto date = tc::regulate_iso_date(*f.year, *f.month, *f.day, how);
    if (!date) { return range_error(cx, "invalid date"); }
    if (!tc::iso_date_within_limits(*date)) {
        return range_error(cx, "date outside the representable range");
    }
    out = *date;
    return true;
}

bool calendar_year_month_from_fields(context & cx, calendar_fields f, tc::overflow how,
                                     tc::iso_date & out) {
    f.day = 1;
    if (!calendar_resolve_fields(cx, f, date_type::year_month)) { return false; }
    const auto date = tc::regulate_iso_date(*f.year, *f.month, *f.day, how);
    if (!date) { return range_error(cx, "invalid year-month"); }
    if (!tc::iso_year_month_within_limits(*date)) {
        return range_error(cx, "year-month outside the representable range");
    }
    out = *date;
    return true;
}

bool calendar_month_day_from_fields(context & cx, calendar_fields f, tc::overflow how,
                                    tc::iso_date & out) {
    if (!calendar_resolve_fields(cx, f, date_type::month_day)) { return false; }
    const double year = f.year ? *f.year : 1972;
    const auto date = tc::regulate_iso_date(year, *f.month, *f.day, how);
    if (!date) { return range_error(cx, "invalid month-day"); }
    out = tc::iso_date{1972, date->month, date->day};
    return true;
}

calendar_fields iso_date_to_fields(const tc::iso_date & date, date_type type) {
    calendar_fields f;
    f.month_code = date.month;
    if (type == date_type::month_day || type == date_type::date) { f.day = date.day; }
    if (type == date_type::year_month || type == date_type::date) { f.year = double(date.year); }
    return f;
}

/// CalendarMergeFields for "iso8601": a `month` or `monthCode` in \p additional
/// replaces both of them (CalendarFieldKeysToIgnore); every other field is
/// overridden one by one.
calendar_fields calendar_merge_fields(const calendar_fields & fields,
                                      const calendar_fields & additional) {
    calendar_fields merged = fields;
    const bool month_given = additional.month.has_value() || additional.month_code.has_value();
    if (month_given) {
        merged.month = additional.month;
        merged.month_code = additional.month_code;
    }
    const auto over = [](auto & into, const auto & from) {
        if (from) { into = from; }
    };
    over(merged.year, additional.year);
    over(merged.day, additional.day);
    over(merged.hour, additional.hour);
    over(merged.minute, additional.minute);
    over(merged.second, additional.second);
    over(merged.millisecond, additional.millisecond);
    over(merged.microsecond, additional.microsecond);
    over(merged.nanosecond, additional.nanosecond);
    over(merged.offset, additional.offset);
    over(merged.time_zone, additional.time_zone);
    return merged;
}

bool interpret_date_time_fields(context & cx, const calendar_fields & f, tc::overflow how,
                                tc::iso_datetime & out) {
    tc::iso_date date;
    if (!calendar_date_from_fields(cx, f, how, date)) { return false; }
    const auto time = tc::regulate_time(f.hour.value_or(0), f.minute.value_or(0),
                                        f.second.value_or(0), f.millisecond.value_or(0),
                                        f.microsecond.value_or(0), f.nanosecond.value_or(0), how);
    if (!time) { return range_error(cx, "invalid time"); }
    out = tc::iso_datetime{date, *time};
    return true;
}

// --- conversions -------------------------------------------------------------

bool to_partial_duration(context & cx, value item, tc::duration_record & out) {
    if (!item.is_object_like()) { return type_error(cx, "a duration-like must be an object"); }
    const double unset = std::nan("");
    out = tc::duration_record{unset, unset, unset, unset, unset, unset, unset, unset, unset, unset};
    // Alphabetical, as the specification reads them.
    struct row {
        std::string_view key;
        double tc::duration_record::* field;
    };
    static constexpr row rows[] = {
        {"days", &tc::duration_record::days},
        {"hours", &tc::duration_record::hours},
        {"microseconds", &tc::duration_record::microseconds},
        {"milliseconds", &tc::duration_record::milliseconds},
        {"minutes", &tc::duration_record::minutes},
        {"months", &tc::duration_record::months},
        {"nanoseconds", &tc::duration_record::nanoseconds},
        {"seconds", &tc::duration_record::seconds},
        {"weeks", &tc::duration_record::weeks},
        {"years", &tc::duration_record::years},
    };
    bool any = false;
    for (const row & r : rows) {
        value v;
        if (!get(cx, item, r.key, v)) { return false; }
        if (v.is_undefined()) { continue; }
        double n = 0;
        if (!to_integer_if_integral(cx, v, n)) { return false; }
        out.*r.field = n;
        any = true;
    }
    if (!any) { return type_error(cx, "a duration-like needs at least one unit property"); }
    return true;
}

bool to_temporal_duration(context & cx, value item, tc::duration_record & out) {
    if (auto d = duration_slot(item)) {
        out = *d;
        return true;
    }
    if (!item.is_object_like()) {
        if (!item.is_string()) { return type_error(cx, "a duration must be a string or object"); }
        const auto parsed = tc::parse_temporal_duration_string(cx.to_string(item));
        if (!parsed) { return range_error(cx, "invalid duration string"); }
        out = *parsed;
        return true;
    }
    tc::duration_record partial;
    if (!to_partial_duration(cx, item, partial)) { return false; }
    const auto pick = [](double v) { return std::isnan(v) ? 0.0 : v; };
    out = tc::duration_record{pick(partial.years),        pick(partial.months),
                              pick(partial.weeks),        pick(partial.days),
                              pick(partial.hours),        pick(partial.minutes),
                              pick(partial.seconds),      pick(partial.milliseconds),
                              pick(partial.microseconds), pick(partial.nanoseconds)};
    if (!tc::is_valid_duration(out)) { return range_error(cx, "invalid duration"); }
    return true;
}

namespace {

/// Reads the options for an item that needs no conversion: GetOptionsObject
/// then GetTemporalOverflowOption, only for the errors they throw.
[[nodiscard]] bool check_overflow_only(context & cx, value options) {
    value resolved;
    tc::overflow ignored{};
    return get_options_object(cx, options, resolved) && get_overflow_option(cx, resolved, ignored);
}

/// The canonical calendar identifier of a parsed string's annotation, with
/// "iso8601" when it has none.
[[nodiscard]] bool parse_calendar(context & cx, const tc::parse_record & r, std::string & out) {
    const std::string raw = r.calendar.empty() ? std::string{"iso8601"} : r.calendar;
    const auto canonical = tc::canonicalize_calendar(raw);
    if (!canonical) { return range_error(cx, "unsupported calendar " + raw); }
    out = *canonical;
    return true;
}

} // namespace

bool to_temporal_date(context & cx, value item, value options, plain_date & out) {
    if (item.is_object_like()) {
        if (auto d = date_slot(cx, item)) {
            if (!check_overflow_only(cx, options)) { return false; }
            out = *d;
            return true;
        }
        if (auto z = zoned_slot(cx, item)) {
            const tc::iso_datetime dt = tc::get_iso_datetime_for(z->zone, z->epoch_ns);
            if (!check_overflow_only(cx, options)) { return false; }
            out = plain_date{dt.date, z->calendar};
            return true;
        }
        if (auto dt = date_time_slot(cx, item)) {
            if (!check_overflow_only(cx, options)) { return false; }
            out = plain_date{dt->dt.date, dt->calendar};
            return true;
        }
        std::string calendar;
        if (!calendar_with_iso_default(cx, item, calendar)) { return false; }
        calendar_fields f;
        if (!prepare_calendar_fields(cx, item, date_fields, 0, false, f)) { return false; }
        value resolved;
        tc::overflow how{};
        if (!get_options_object(cx, options, resolved) || !get_overflow_option(cx, resolved, how)) {
            return false;
        }
        tc::iso_date date;
        if (!calendar_date_from_fields(cx, f, how, date)) { return false; }
        out = plain_date{date, calendar};
        return true;
    }
    if (!item.is_string()) { return type_error(cx, "a date must be a string or object"); }
    const auto r = tc::parse_iso_datetime(cx.to_string(item), {tc::goal::date_time});
    if (!r) { return range_error(cx, "invalid date string"); }
    std::string calendar;
    if (!parse_calendar(cx, *r, calendar) || !check_overflow_only(cx, options)) { return false; }
    const tc::iso_date date{*r->year, r->month, r->day};
    if (!tc::iso_date_within_limits(date)) {
        return range_error(cx, "date outside the representable range");
    }
    out = plain_date{date, calendar};
    return true;
}

bool to_time_record(context & cx, value item, bool partial, double (&out)[6]) {
    const double initial = partial ? std::nan("") : 0.0;
    for (double & v : out) { v = initial; }
    // Read in ToTemporalTimeRecord's order (hour, microsecond, millisecond,
    // minute, nanosecond, second) into out's [h, min, s, ms, us, ns] layout.
    static constexpr std::pair<std::string_view, int> order[] = {
        {"hour", 0},   {"microsecond", 4}, {"millisecond", 3},
        {"minute", 1}, {"nanosecond", 5},  {"second", 2}};
    bool any = false;
    for (const auto & [key, index] : order) {
        value v;
        if (!get(cx, item, key, v)) { return false; }
        if (v.is_undefined()) { continue; }
        double n = 0;
        if (!to_integer_with_truncation(cx, v, n)) { return false; }
        out[index] = n;
        any = true;
    }
    if (!any) { return type_error(cx, "a time-like needs at least one time property"); }
    return true;
}

bool to_temporal_time(context & cx, value item, value options, tc::time_record & out) {
    if (item.is_object_like()) {
        if (auto t = time_slot(item)) {
            if (!check_overflow_only(cx, options)) { return false; }
            out = *t;
            return true;
        }
        if (auto dt = date_time_slot(cx, item)) {
            if (!check_overflow_only(cx, options)) { return false; }
            out = dt->dt.time;
            return true;
        }
        if (auto z = zoned_slot(cx, item)) {
            const tc::iso_datetime dt = tc::get_iso_datetime_for(z->zone, z->epoch_ns);
            if (!check_overflow_only(cx, options)) { return false; }
            out = dt.time;
            return true;
        }
        double fields[6];
        if (!to_time_record(cx, item, false, fields)) { return false; }
        value resolved;
        tc::overflow how{};
        if (!get_options_object(cx, options, resolved) || !get_overflow_option(cx, resolved, how)) {
            return false;
        }
        const auto t = tc::regulate_time(fields[0], fields[1], fields[2], fields[3], fields[4],
                                         fields[5], how);
        if (!t) { return range_error(cx, "invalid time"); }
        out = *t;
        return true;
    }
    if (!item.is_string()) { return type_error(cx, "a time must be a string or object"); }
    const auto r = tc::parse_iso_datetime(cx.to_string(item), {tc::goal::time});
    if (!r || !r->time) { return range_error(cx, "invalid time string"); }
    if (!check_overflow_only(cx, options)) { return false; }
    out = *r->time;
    return true;
}

bool to_time_record_or_midnight(context & cx, value item, tc::time_record & out) {
    if (item.is_undefined()) {
        out = tc::time_record{};
        return true;
    }
    return to_temporal_time(cx, item, value::undefined(), out);
}

bool to_temporal_date_time(context & cx, value item, value options, plain_date_time & out) {
    if (item.is_object_like()) {
        if (auto dt = date_time_slot(cx, item)) {
            if (!check_overflow_only(cx, options)) { return false; }
            out = *dt;
            return true;
        }
        if (auto z = zoned_slot(cx, item)) {
            const tc::iso_datetime dt = tc::get_iso_datetime_for(z->zone, z->epoch_ns);
            if (!check_overflow_only(cx, options)) { return false; }
            out = plain_date_time{dt, z->calendar};
            return true;
        }
        if (auto d = date_slot(cx, item)) {
            if (!check_overflow_only(cx, options)) { return false; }
            out = plain_date_time{tc::iso_datetime{d->date, tc::time_record{}}, d->calendar};
            if (!tc::iso_datetime_within_limits(out.dt)) {
                return range_error(cx, "date-time outside the representable range");
            }
            return true;
        }
        std::string calendar;
        if (!calendar_with_iso_default(cx, item, calendar)) { return false; }
        calendar_fields f;
        if (!prepare_calendar_fields(cx, item, date_fields | time_fields, 0, false, f)) {
            return false;
        }
        value resolved;
        tc::overflow how{};
        if (!get_options_object(cx, options, resolved) || !get_overflow_option(cx, resolved, how)) {
            return false;
        }
        tc::iso_datetime dt;
        if (!interpret_date_time_fields(cx, f, how, dt)) { return false; }
        if (!tc::iso_datetime_within_limits(dt)) {
            return range_error(cx, "date-time outside the representable range");
        }
        out = plain_date_time{dt, calendar};
        return true;
    }
    if (!item.is_string()) { return type_error(cx, "a date-time must be a string or object"); }
    const auto r = tc::parse_iso_datetime(cx.to_string(item), {tc::goal::date_time});
    if (!r) { return range_error(cx, "invalid date-time string"); }
    std::string calendar;
    if (!parse_calendar(cx, *r, calendar) || !check_overflow_only(cx, options)) { return false; }
    const tc::iso_datetime dt{tc::iso_date{*r->year, r->month, r->day},
                              r->time.value_or(tc::time_record{})};
    if (!tc::iso_datetime_within_limits(dt)) {
        return range_error(cx, "date-time outside the representable range");
    }
    out = plain_date_time{dt, calendar};
    return true;
}

bool to_temporal_year_month(context & cx, value item, value options, plain_date & out) {
    if (item.is_object_like()) {
        if (auto ym = year_month_slot(cx, item)) {
            if (!check_overflow_only(cx, options)) { return false; }
            out = *ym;
            return true;
        }
        std::string calendar;
        if (!calendar_with_iso_default(cx, item, calendar)) { return false; }
        calendar_fields f;
        if (!prepare_calendar_fields(cx, item, f_year | f_month | f_month_code, 0, false, f)) {
            return false;
        }
        value resolved;
        tc::overflow how{};
        if (!get_options_object(cx, options, resolved) || !get_overflow_option(cx, resolved, how)) {
            return false;
        }
        tc::iso_date date;
        if (!calendar_year_month_from_fields(cx, f, how, date)) { return false; }
        out = plain_date{date, calendar};
        return true;
    }
    if (!item.is_string()) { return type_error(cx, "a year-month must be a string or object"); }
    const auto r = tc::parse_iso_datetime(cx.to_string(item), {tc::goal::year_month});
    if (!r) { return range_error(cx, "invalid year-month string"); }
    std::string calendar;
    if (!parse_calendar(cx, *r, calendar) || !check_overflow_only(cx, options)) { return false; }
    const tc::iso_date date{*r->year, r->month, r->day};
    if (!tc::iso_year_month_within_limits(date)) {
        return range_error(cx, "year-month outside the representable range");
    }
    out = plain_date{tc::iso_date{date.year, date.month, 1}, calendar};
    return true;
}

bool to_temporal_month_day(context & cx, value item, value options, plain_date & out) {
    if (item.is_object_like()) {
        if (auto md = month_day_slot(cx, item)) {
            if (!check_overflow_only(cx, options)) { return false; }
            out = *md;
            return true;
        }
        std::string calendar;
        if (!calendar_with_iso_default(cx, item, calendar)) { return false; }
        calendar_fields f;
        if (!prepare_calendar_fields(cx, item, date_fields, 0, false, f)) { return false; }
        value resolved;
        tc::overflow how{};
        if (!get_options_object(cx, options, resolved) || !get_overflow_option(cx, resolved, how)) {
            return false;
        }
        tc::iso_date date;
        if (!calendar_month_day_from_fields(cx, f, how, date)) { return false; }
        out = plain_date{date, calendar};
        return true;
    }
    if (!item.is_string()) { return type_error(cx, "a month-day must be a string or object"); }
    const auto r = tc::parse_iso_datetime(cx.to_string(item), {tc::goal::month_day});
    if (!r) { return range_error(cx, "invalid month-day string"); }
    std::string calendar;
    if (!parse_calendar(cx, *r, calendar) || !check_overflow_only(cx, options)) { return false; }
    out = plain_date{tc::iso_date{1972, r->month, r->day}, calendar};
    return true;
}

bool to_temporal_instant(context & cx, value item, i128 & out) {
    if (item.is_object_like()) {
        if (auto ns = instant_slot(item)) {
            out = *ns;
            return true;
        }
        if (auto z = zoned_slot(cx, item)) {
            out = z->epoch_ns;
            return true;
        }
        value prim;
        if (!cx.to_primitive_hint(item, "string", prim)) { return false; }
        item = prim;
    }
    if (!item.is_string()) { return type_error(cx, "an instant must be a string or object"); }
    const auto r = tc::parse_iso_datetime(cx.to_string(item), {tc::goal::instant});
    if (!r || !r->time) { return range_error(cx, "invalid instant string"); }
    const i128 offset = r->z ? 0 : *tc::parse_utc_offset(r->offset);
    const tc::time_record & t = *r->time;
    const tc::iso_datetime balanced =
        tc::balance_iso_datetime(*r->year, r->month, r->day, t.hour, t.minute, t.second,
                                 t.millisecond, t.microsecond, i128{t.nanosecond} - offset);
    if (!tc::check_iso_days_range(balanced.date)) {
        return range_error(cx, "instant outside the representable range");
    }
    const i128 ns = tc::get_utc_epoch_nanoseconds(balanced);
    if (!tc::is_valid_epoch_nanoseconds(ns)) {
        return range_error(cx, "instant outside the representable range");
    }
    out = ns;
    return true;
}

namespace {

/// Whether a UTC offset string such as "+01:00:30" includes seconds, i.e. has
/// more than four digits before any fractional part.
[[nodiscard]] bool offset_has_seconds(std::string_view offset) {
    std::size_t digits = 0;
    for (const char c : offset) {
        if (c == '.' || c == ',') { break; }
        if (c >= '0' && c <= '9') { ++digits; }
    }
    return digits > 4;
}

/// What InterpretISODateTimeOffset needs, gathered from a property bag or string.
struct zoned_inputs {
    tc::iso_date date;
    std::optional<tc::time_record> time;
    std::string zone;
    std::string calendar;
    std::optional<std::string> offset;
    bool utc_designator = false;
    tc::match_behaviour match = tc::match_behaviour::exactly;
};

/// Resolves \p in to an exact time: a Z designator uses the offset exactly, a
/// missing offset means wall-clock time, and otherwise \p option decides.
[[nodiscard]] bool finish_zoned(context & cx, const zoned_inputs & in, tc::disambiguation how,
                                tc::offset_option option, zoned & out) {
    tc::offset_behaviour behaviour = tc::offset_behaviour::option;
    if (in.utc_designator) {
        behaviour = tc::offset_behaviour::exact;
    } else if (!in.offset || in.offset->empty()) {
        behaviour = tc::offset_behaviour::wall;
    }
    const i128 offset_ns =
        behaviour == tc::offset_behaviour::option ? *tc::parse_utc_offset(*in.offset) : 0;
    const auto ns = tc::interpret_iso_datetime_offset(in.date, in.time, behaviour, offset_ns,
                                                      in.zone, how, option, in.match);
    if (!ns) { return range_error(cx, "no exact time for that date, time, offset and zone"); }
    out = zoned{*ns, in.zone, in.calendar};
    return true;
}

} // namespace

bool to_temporal_zoned(context & cx, value item, value options, zoned & out) {
    zoned_inputs in;
    tc::disambiguation how{};
    tc::offset_option option{};
    tc::overflow overflow_how{};
    value resolved;
    if (item.is_object_like()) {
        if (auto z = zoned_slot(cx, item)) {
            if (!get_options_object(cx, options, resolved) ||
                !get_disambiguation_option(cx, resolved, how) ||
                !get_offset_option(cx, resolved, tc::offset_option::reject, option) ||
                !get_overflow_option(cx, resolved, overflow_how)) {
                return false;
            }
            out = *z;
            return true;
        }
        if (!calendar_with_iso_default(cx, item, in.calendar)) { return false; }
        calendar_fields f;
        if (!prepare_calendar_fields(cx, item, date_fields | time_fields | f_offset | f_time_zone,
                                     f_time_zone, false, f)) {
            return false;
        }
        in.zone = *f.time_zone;
        in.offset = f.offset;
        if (!get_options_object(cx, options, resolved) ||
            !get_disambiguation_option(cx, resolved, how) ||
            !get_offset_option(cx, resolved, tc::offset_option::reject, option) ||
            !get_overflow_option(cx, resolved, overflow_how)) {
            return false;
        }
        tc::iso_datetime dt;
        if (!interpret_date_time_fields(cx, f, overflow_how, dt)) { return false; }
        in.date = dt.date;
        in.time = dt.time;
        return finish_zoned(cx, in, how, option, out);
    }
    if (!item.is_string()) {
        return type_error(cx, "a zoned date-time must be a string or object");
    }
    const auto r = tc::parse_iso_datetime(cx.to_string(item), {tc::goal::date_time_zoned});
    if (!r) { return range_error(cx, "invalid zoned date-time string"); }
    if (!to_time_zone_identifier(cx, str(cx, r->annotation), in.zone)) { return false; }
    in.offset = r->offset;
    in.utc_designator = r->z;
    if (!parse_calendar(cx, *r, in.calendar)) { return false; }
    in.match = !r->offset.empty() && offset_has_seconds(r->offset) ? tc::match_behaviour::exactly
                                                                   : tc::match_behaviour::minutes;
    if (!get_options_object(cx, options, resolved) ||
        !get_disambiguation_option(cx, resolved, how) ||
        !get_offset_option(cx, resolved, tc::offset_option::reject, option) ||
        !get_overflow_option(cx, resolved, overflow_how)) {
        return false;
    }
    in.date = tc::iso_date{*r->year, r->month, r->day};
    in.time = r->time;
    return finish_zoned(cx, in, how, option, out);
}

bool is_partial_temporal_object(context & cx, value v, bool & out) {
    out = false;
    if (!v.is_object_like()) { return true; }
    if (const auto k = kind_of(v); k && *k != kind::duration && *k != kind::instant) {
        return true;
    }
    value calendar;
    if (!get(cx, v, "calendar", calendar)) { return false; }
    if (!calendar.is_undefined()) { return true; }
    value zone;
    if (!get(cx, v, "timeZone", zone)) { return false; }
    if (!zone.is_undefined()) { return true; }
    out = true;
    return true;
}

bool get_relative_to_option(context & cx, value options, std::optional<plain_date> & plain,
                            std::optional<zoned> & zoned_out) {
    plain.reset();
    zoned_out.reset();
    value v;
    if (!get(cx, options, "relativeTo", v)) { return false; }
    if (v.is_undefined()) { return true; }
    zoned_inputs in;
    if (v.is_object_like()) {
        if (auto z = zoned_slot(cx, v)) {
            zoned_out = *z;
            return true;
        }
        if (auto d = date_slot(cx, v)) {
            plain = *d;
            return true;
        }
        if (auto dt = date_time_slot(cx, v)) {
            plain = plain_date{dt->dt.date, dt->calendar};
            return true;
        }
        if (!calendar_with_iso_default(cx, v, in.calendar)) { return false; }
        calendar_fields f;
        if (!prepare_calendar_fields(cx, v, date_fields | time_fields | f_offset | f_time_zone, 0,
                                     false, f)) {
            return false;
        }
        tc::iso_datetime dt;
        if (!interpret_date_time_fields(cx, f, tc::overflow::constrain, dt)) { return false; }
        in.date = dt.date;
        in.time = dt.time;
        in.offset = f.offset;
        if (!f.time_zone) {
            plain = plain_date{dt.date, in.calendar};
            if (!tc::iso_date_within_limits(dt.date)) {
                return range_error(cx, "date outside the representable range");
            }
            return true;
        }
        in.zone = *f.time_zone;
    } else {
        if (!v.is_string()) { return type_error(cx, "relativeTo must be a string or object"); }
        const auto r = tc::parse_iso_datetime(cx.to_string(v),
                                              {tc::goal::date_time_zoned, tc::goal::date_time});
        if (!r) { return range_error(cx, "invalid relativeTo string"); }
        if (!parse_calendar(cx, *r, in.calendar)) { return false; }
        in.date = tc::iso_date{*r->year, r->month, r->day};
        in.time = r->time;
        if (r->annotation.empty()) {
            if (!tc::iso_date_within_limits(in.date)) {
                return range_error(cx, "date outside the representable range");
            }
            plain = plain_date{in.date, in.calendar};
            return true;
        }
        if (!to_time_zone_identifier(cx, str(cx, r->annotation), in.zone)) { return false; }
        in.utc_designator = r->z;
        in.offset = r->offset;
        in.match = !r->offset.empty() && offset_has_seconds(r->offset)
                       ? tc::match_behaviour::exactly
                       : tc::match_behaviour::minutes;
    }
    zoned result;
    if (!finish_zoned(cx, in, tc::disambiguation::compatible, tc::offset_option::reject, result)) {
        return false;
    }
    zoned_out = std::move(result);
    return true;
}

bool options_or_string(context & cx, value arg, std::string_view key, value & out) {
    if (arg.is_undefined()) { return type_error(cx, "an options argument is required"); }
    if (arg.is_string()) {
        out = cx.make_object();
        auto * object = static_cast<object_object *>(out.as_heap());
        object->prototype = value::undefined();
        object->set(std::string{key}, arg);
        return true;
    }
    return get_options_object(cx, arg, out);
}

bool get_round_options(context & cx, value arg, std::int64_t & increment, tc::rounding_mode & mode,
                       unit_value & smallest, std::initializer_list<tc::unit> extra) {
    value round_to;
    if (!options_or_string(cx, arg, "smallestUnit", round_to) ||
        !get_rounding_increment_option(cx, round_to, increment) ||
        !get_rounding_mode_option(cx, round_to, tc::rounding_mode::half_expand, mode) ||
        !get_unit_option(cx, round_to, "smallestUnit", smallest)) {
        return false;
    }
    if (!smallest.present) { return range_error(cx, "smallestUnit is required"); }
    return validate_unit_value(cx, smallest, unit_group::time, false, extra);
}

// --- installation ------------------------------------------------------------

void install_calendar_getters(context & cx, object_object * proto, kind k, std::uint32_t which) {
    struct row {
        calendar_getter bit;
        const char * name;
    };
    static constexpr row rows[] = {
        {g_calendar_id, "calendarId"},
        {g_era, "era"},
        {g_era_year, "eraYear"},
        {g_year, "year"},
        {g_month, "month"},
        {g_month_code, "monthCode"},
        {g_day, "day"},
        {g_day_of_week, "dayOfWeek"},
        {g_day_of_year, "dayOfYear"},
        {g_week_of_year, "weekOfYear"},
        {g_year_of_week, "yearOfWeek"},
        {g_days_in_week, "daysInWeek"},
        {g_days_in_month, "daysInMonth"},
        {g_days_in_year, "daysInYear"},
        {g_months_in_year, "monthsInYear"},
        {g_in_leap_year, "inLeapYear"},
    };
    for (const row & r : rows) {
        if ((which & r.bit) == 0) { continue; }
        const calendar_getter bit = r.bit;
        const std::string method =
            std::string{"get "} + kind_names[int(k)] + ".prototype." + r.name;
        getter(cx, proto, r.name, [k, bit, method](context & c, std::span<value>) {
            const value self = c.current_this();
            std::optional<plain_date> found;
            switch (k) {
            case kind::plain_date: found = date_slot(c, self); break;
            case kind::plain_year_month: found = year_month_slot(c, self); break;
            case kind::plain_month_day: found = month_day_slot(c, self); break;
            case kind::plain_date_time:
                if (auto dt = date_time_slot(c, self)) {
                    found = plain_date{dt->dt.date, dt->calendar};
                }
                break;
            case kind::zoned_date_time:
                if (auto z = zoned_slot(c, self)) {
                    found = plain_date{tc::get_iso_datetime_for(z->zone, z->epoch_ns).date,
                                       z->calendar};
                }
                break;
            default: break;
            }
            if (!found) {
                (void)type_error(c, receiver_message(method, k));
                return value::undefined();
            }
            const tc::iso_date & d = found->date;
            switch (bit) {
            case g_calendar_id: return c.string(found->calendar);
            case g_era:
            case g_era_year: return value::undefined(); // The ISO calendar has no eras.
            case g_year: return value::number(double(d.year));
            case g_month: return value::number(d.month);
            case g_month_code: return c.string(tc::month_code(d.month));
            case g_day: return value::number(d.day);
            case g_day_of_week: return value::number(tc::iso_day_of_week(d));
            case g_day_of_year: return value::number(tc::iso_day_of_year(d));
            case g_week_of_year: return value::number(tc::iso_week_of_year(d).week);
            case g_year_of_week: return value::number(double(tc::iso_week_of_year(d).year));
            case g_days_in_week: return value::number(7);
            case g_days_in_month: return value::number(tc::iso_days_in_month(d.year, d.month));
            case g_days_in_year: return value::number(tc::iso_days_in_year(d.year));
            case g_months_in_year: return value::number(12);
            default: return value::boolean(tc::is_leap_year(d.year));
            }
        });
    }
}

class_parts make_class(context & cx, object_object & ns, kind k, const char * name, double arity,
                       native_fn construct) {
    class_parts parts;
    parts.proto = detail::new_table(cx);
    parts.ctor = cx.allocate<native_object>(name, std::move(construct));
    detail::constant(parts.ctor, "prototype", value::object(parts.proto));
    link_constructor(cx, parts.proto, name, arity, value::object(parts.ctor));
    parts.proto->define(CTBROWSER_SYMBOL_KEY("toStringTag"), cx.string(kind_names[int(k)]),
                        attr_configurable);
    ns.define(name, value::object(parts.ctor), attr_builtin);
    set_prototype(cx, k, parts.proto);
    return parts;
}

void getter(context & cx, object_object * proto, const std::string & name, native_fn fn) {
    auto * made = detail::method_native(cx, "get " + name, std::move(fn));
    detail::install_arity(cx, made, 0);
    proto->define_accessor(name, value::object(made), value::undefined(), attr_configurable);
}

} // namespace ctbrowser::script::builtins_detail::temporal_js
