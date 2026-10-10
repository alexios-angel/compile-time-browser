//===- temporal/duration.cpp - Temporal.Duration builtin --------*- C++ -*-===//
//
// Temporal.Duration (Temporal specification chapter 7). Each method follows
// its specification clause's steps in order; the shared date and time
// arithmetic is in core.cpp.
//
//===----------------------------------------------------------------------===//

#include "objects.hpp"

namespace ctbrowser::script::builtins_detail::temporal_js {

namespace {

/// The start and end date-times of `d` measured from a plain-date relativeTo,
/// as round and total build them. False with a RangeError thrown.
[[nodiscard]] bool plain_target(context & cx, const tc::duration_record & d,
                                const plain_date & relative, tc::iso_datetime & origin,
                                tc::iso_datetime & target) {
    const tc::internal_duration internal = tc::to_internal_duration_24h_days(d);
    const tc::time_record target_time = tc::add_time(tc::time_record{}, internal.time);
    const auto date_duration = tc::create_date_duration(
        internal.date.years, internal.date.months, internal.date.weeks, double(target_time.days));
    if (!date_duration) { return range_error(cx, "invalid duration"); }
    const auto target_date =
        tc::calendar_date_add(relative.date, *date_duration, tc::overflow::constrain);
    if (!target_date) { return range_error(cx, "date outside the representable range"); }
    origin = tc::iso_datetime{relative.date, tc::time_record{}};
    tc::time_record time = target_time;
    time.days = 0;
    target = tc::iso_datetime{*target_date, time};
    return true;
}

/// Converts an internal duration back to a Temporal.Duration balanced up to
/// `largest`, throwing a RangeError when there is none.
[[nodiscard]] value from_internal(context & cx, const std::optional<tc::internal_duration> & d,
                                  tc::unit largest) {
    if (!d) {
        (void)range_error(cx, "the result is outside the representable range");
        return value::undefined();
    }
    const auto result = tc::temporal_duration_from_internal(*d, largest);
    if (!result) {
        (void)range_error(cx, "invalid duration");
        return value::undefined();
    }
    return make_duration(cx, *result);
}

/// AddDurations: `duration` plus (or minus) the duration-like `other_value`.
value add_durations(context & cx, bool subtract, const tc::duration_record & duration,
                    value other_value) {
    tc::duration_record other;
    if (!to_temporal_duration(cx, other_value, other)) { return value::undefined(); }
    if (subtract) { other = tc::negate(other); }
    const tc::unit largest =
        tc::larger_unit(tc::default_largest_unit(duration), tc::default_largest_unit(other));
    if (tc::is_calendar_unit(largest)) {
        (void)range_error(cx, "adding durations with years, months or weeks needs a relativeTo");
        return value::undefined();
    }
    const auto sum = tc::add_time_duration(tc::to_internal_duration_24h_days(duration).time,
                                           tc::to_internal_duration_24h_days(other).time);
    if (!sum) {
        (void)range_error(cx, "the sum is outside the representable range");
        return value::undefined();
    }
    return from_internal(cx, tc::internal_duration{{}, *sum}, largest);
}

value duration_round(context & cx, std::span<value> a) {
    const auto duration = this_duration(cx, "Temporal.Duration.prototype.round");
    if (!duration) { return value::undefined(); }
    value round_to;
    if (!options_or_string(cx, arg_at(a, 0), "smallestUnit", round_to)) {
        return value::undefined();
    }
    unit_value largest_option;
    std::optional<plain_date> plain;
    std::optional<zoned> zoned_relative;
    std::int64_t increment = 1;
    tc::rounding_mode mode = tc::rounding_mode::half_expand;
    unit_value smallest_option;
    if (!get_unit_option(cx, round_to, "largestUnit", largest_option) ||
        !get_relative_to_option(cx, round_to, plain, zoned_relative) ||
        !get_rounding_increment_option(cx, round_to, increment) ||
        !get_rounding_mode_option(cx, round_to, tc::rounding_mode::half_expand, mode) ||
        !get_unit_option(cx, round_to, "smallestUnit", smallest_option) ||
        !validate_unit_value(cx, smallest_option, unit_group::datetime)) {
        return value::undefined();
    }
    const bool smallest_present = smallest_option.present;
    const tc::unit smallest = smallest_present ? smallest_option.u : tc::unit::nanosecond;
    const tc::unit existing_largest = tc::default_largest_unit(*duration);
    const tc::unit default_largest = tc::larger_unit(existing_largest, smallest);
    const bool largest_present = largest_option.present;
    // A largestUnit is validated as a datetime unit or "auto".
    if (!validate_unit_value(cx, largest_option, unit_group::datetime, true)) {
        return value::undefined();
    }
    tc::unit largest = default_largest;
    if (largest_present && !largest_option.is_auto) { largest = largest_option.u; }
    if (!smallest_present && !largest_present) {
        (void)range_error(cx, "one of smallestUnit or largestUnit is required");
        return value::undefined();
    }
    if (tc::larger_unit(largest, smallest) != largest) {
        (void)range_error(cx, "largestUnit must be larger than smallestUnit");
        return value::undefined();
    }
    if (const std::int64_t maximum = tc::maximum_rounding_increment(smallest);
        maximum != 0 && !tc::valid_rounding_increment(increment, maximum, false)) {
        (void)range_error(cx, "invalid roundingIncrement");
        return value::undefined();
    }
    if (increment > 1 && largest != smallest && tc::is_date_unit(smallest)) {
        (void)range_error(cx, "roundingIncrement must be 1 when rounding to a date unit "
                              "below largestUnit");
        return value::undefined();
    }
    if (zoned_relative) {
        const tc::internal_duration internal = tc::to_internal_duration(*duration);
        const auto target = tc::add_zoned_datetime(zoned_relative->epoch_ns, zoned_relative->zone,
                                                   internal, tc::overflow::constrain);
        if (!target) {
            (void)range_error(cx, "the result is outside the representable range");
            return value::undefined();
        }
        const auto rounded = tc::difference_zoned_datetime_with_rounding(
            zoned_relative->epoch_ns, *target, zoned_relative->zone, largest, increment, smallest,
            mode);
        return from_internal(cx, rounded, tc::is_date_unit(largest) ? tc::unit::hour : largest);
    }
    if (plain) {
        tc::iso_datetime origin;
        tc::iso_datetime target;
        if (!plain_target(cx, *duration, *plain, origin, target)) { return value::undefined(); }
        const auto rounded = tc::difference_plain_datetime_with_rounding(origin, target, largest,
                                                                         increment, smallest, mode);
        return from_internal(cx, rounded, largest);
    }
    if (tc::is_calendar_unit(existing_largest) || tc::is_calendar_unit(largest)) {
        (void)range_error(cx, "rounding years, months or weeks needs a relativeTo");
        return value::undefined();
    }
    const tc::internal_duration internal = tc::to_internal_duration_24h_days(*duration);
    if (smallest == tc::unit::day) {
        const tc::i128 days =
            tc::round_number_to_increment(internal.time, tc::ns_per_day * increment, mode) /
            tc::ns_per_day;
        const auto date = tc::create_date_duration(0, 0, 0, static_cast<double>(days));
        if (!date) {
            (void)range_error(cx, "invalid duration");
            return value::undefined();
        }
        return from_internal(cx, tc::internal_duration{*date, 0}, largest);
    }
    const auto time = tc::round_time_duration(internal.time, increment, smallest, mode);
    if (!time) {
        (void)range_error(cx, "the result is outside the representable range");
        return value::undefined();
    }
    return from_internal(cx, tc::internal_duration{{}, *time}, largest);
}

value duration_total(context & cx, std::span<value> a) {
    const auto duration = this_duration(cx, "Temporal.Duration.prototype.total");
    if (!duration) { return value::undefined(); }
    value total_of;
    if (!options_or_string(cx, arg_at(a, 0), "unit", total_of)) { return value::undefined(); }
    std::optional<plain_date> plain;
    std::optional<zoned> zoned_relative;
    if (!get_relative_to_option(cx, total_of, plain, zoned_relative)) { return value::undefined(); }
    std::optional<std::string> unit_text;
    if (!get_string_option(cx, total_of, "unit",
                           {"year",        "years",        "month",       "months",
                            "week",        "weeks",        "day",         "days",
                            "hour",        "hours",        "minute",      "minutes",
                            "second",      "seconds",      "millisecond", "milliseconds",
                            "microsecond", "microseconds", "nanosecond",  "nanoseconds",
                            "auto"},
                           std::nullopt, unit_text, true)) {
        return value::undefined();
    }
    if (*unit_text == "auto") {
        (void)range_error(cx, "\"auto\" is not a unit to total in");
        return value::undefined();
    }
    const tc::unit u = *tc::unit_from_name(*unit_text);
    std::optional<double> total;
    if (zoned_relative) {
        const auto target =
            tc::add_zoned_datetime(zoned_relative->epoch_ns, zoned_relative->zone,
                                   tc::to_internal_duration(*duration), tc::overflow::constrain);
        if (target) {
            total = tc::difference_zoned_datetime_with_total(zoned_relative->epoch_ns, *target,
                                                             zoned_relative->zone, u);
        }
    } else if (plain) {
        tc::iso_datetime origin;
        tc::iso_datetime target;
        if (!plain_target(cx, *duration, *plain, origin, target)) { return value::undefined(); }
        total = tc::difference_plain_datetime_with_total(origin, target, u);
    } else {
        if (tc::is_calendar_unit(tc::default_largest_unit(*duration)) || tc::is_calendar_unit(u)) {
            (void)range_error(cx, "totalling years, months or weeks needs a relativeTo");
            return value::undefined();
        }
        total = tc::total_time_duration(tc::to_internal_duration_24h_days(*duration).time, u);
    }
    if (!total) {
        (void)range_error(cx, "the result is outside the representable range");
        return value::undefined();
    }
    return value::number(*total);
}

value duration_to_string(context & cx, std::span<value> a) {
    const auto duration = this_duration(cx, "Temporal.Duration.prototype.toString");
    if (!duration) { return value::undefined(); }
    value options;
    int digits = tc::precision_auto;
    tc::rounding_mode mode = tc::rounding_mode::trunc;
    unit_value smallest;
    if (!get_options_object(cx, arg_at(a, 0), options) ||
        !get_fractional_second_digits_option(cx, options, digits) ||
        !get_rounding_mode_option(cx, options, tc::rounding_mode::trunc, mode) ||
        !get_unit_option(cx, options, "smallestUnit", smallest) ||
        !validate_unit_value(cx, smallest, unit_group::time)) {
        return value::undefined();
    }
    if (smallest.present && (smallest.u == tc::unit::hour || smallest.u == tc::unit::minute)) {
        (void)range_error(cx, "smallestUnit must be seconds or smaller");
        return value::undefined();
    }
    const precision_record precision = seconds_string_precision(smallest, digits);
    if (precision.u == tc::unit::nanosecond && precision.increment == 1) {
        return str(cx, tc::temporal_duration_to_string(*duration, precision.precision));
    }
    const tc::unit largest = tc::default_largest_unit(*duration);
    tc::internal_duration internal = tc::to_internal_duration(*duration);
    const auto time =
        tc::round_time_duration(internal.time, precision.increment, precision.u, mode);
    if (!time) {
        (void)range_error(cx, "the result is outside the representable range");
        return value::undefined();
    }
    internal.time = *time;
    const auto rounded =
        tc::temporal_duration_from_internal(internal, tc::larger_unit(largest, tc::unit::second));
    if (!rounded) {
        (void)range_error(cx, "invalid duration");
        return value::undefined();
    }
    return str(cx, tc::temporal_duration_to_string(*rounded, precision.precision));
}

value duration_compare(context & cx, std::span<value> a) {
    tc::duration_record one;
    tc::duration_record two;
    value options;
    std::optional<plain_date> plain;
    std::optional<zoned> zoned_relative;
    if (!to_temporal_duration(cx, arg_at(a, 0), one) ||
        !to_temporal_duration(cx, arg_at(a, 1), two) ||
        !get_options_object(cx, arg_at(a, 2), options) ||
        !get_relative_to_option(cx, options, plain, zoned_relative)) {
        return value::undefined();
    }
    const auto same = [](const tc::duration_record & x, const tc::duration_record & y) {
        return x.years == y.years && x.months == y.months && x.weeks == y.weeks &&
               x.days == y.days && x.hours == y.hours && x.minutes == y.minutes &&
               x.seconds == y.seconds && x.milliseconds == y.milliseconds &&
               x.microseconds == y.microseconds && x.nanoseconds == y.nanoseconds;
    };
    if (same(one, two)) { return value::number(0); }
    const tc::unit largest1 = tc::default_largest_unit(one);
    const tc::unit largest2 = tc::default_largest_unit(two);
    const tc::internal_duration d1 = tc::to_internal_duration(one);
    const tc::internal_duration d2 = tc::to_internal_duration(two);
    if (zoned_relative && (tc::is_date_unit(largest1) || tc::is_date_unit(largest2))) {
        const auto after1 = tc::add_zoned_datetime(zoned_relative->epoch_ns, zoned_relative->zone,
                                                   d1, tc::overflow::constrain);
        if (!after1) {
            (void)range_error(cx, "the result is outside the representable range");
            return value::undefined();
        }
        const auto after2 = tc::add_zoned_datetime(zoned_relative->epoch_ns, zoned_relative->zone,
                                                   d2, tc::overflow::constrain);
        if (!after2) {
            (void)range_error(cx, "the result is outside the representable range");
            return value::undefined();
        }
        return value::number(*after1 > *after2 ? 1 : (*after1 < *after2 ? -1 : 0));
    }
    double days1 = one.days;
    double days2 = two.days;
    if (tc::is_calendar_unit(largest1) || tc::is_calendar_unit(largest2)) {
        if (!plain) {
            (void)range_error(cx, "comparing years, months or weeks needs a relativeTo");
            return value::undefined();
        }
        const auto x = tc::date_duration_days(d1.date, plain->date);
        const auto y = x ? tc::date_duration_days(d2.date, plain->date) : std::nullopt;
        if (!x || !y) {
            (void)range_error(cx, "the result is outside the representable range");
            return value::undefined();
        }
        days1 = *x;
        days2 = *y;
    }
    const auto t1 = tc::add_24_hour_days(d1.time, days1);
    const auto t2 = t1 ? tc::add_24_hour_days(d2.time, days2) : std::nullopt;
    if (!t1 || !t2) {
        (void)range_error(cx, "the result is outside the representable range");
        return value::undefined();
    }
    return value::number(*t1 > *t2 ? 1 : (*t1 < *t2 ? -1 : 0));
}

} // namespace

void install_duration(context & cx, object_object & ns) {
    const class_parts parts =
        make_class(cx, ns, kind::duration, "Duration", 0, [](context & c, std::span<value> a) {
            const value self = c.current_this();
            if (!detail::constructing_this(self)) {
                (void)type_error(c, "Constructor Temporal.Duration requires 'new'");
                return value::undefined();
            }
            double fields[10] = {};
            for (std::size_t i = 0; i < 10; ++i) {
                const value v = arg_at(a, i);
                if (!v.is_undefined() && !to_integer_if_integral(c, v, fields[i])) {
                    return value::undefined();
                }
            }
            return make_duration(c,
                                 tc::duration_record{fields[0], fields[1], fields[2], fields[3],
                                                     fields[4], fields[5], fields[6], fields[7],
                                                     fields[8], fields[9]},
                                 self);
        });
    native_object * ctor = parts.ctor;
    object_object * proto = parts.proto;

    detail::method(cx, ctor, "from", 1, [](context & c, std::span<value> a) {
        tc::duration_record d;
        if (!to_temporal_duration(c, arg_at(a, 0), d)) { return value::undefined(); }
        return make_duration(c, d);
    });
    detail::method(cx, ctor, "compare", 2, duration_compare);

    struct field_getter {
        const char * name;
        double tc::duration_record::* field;
    };
    static constexpr field_getter fields[] = {
        {"years", &tc::duration_record::years},
        {"months", &tc::duration_record::months},
        {"weeks", &tc::duration_record::weeks},
        {"days", &tc::duration_record::days},
        {"hours", &tc::duration_record::hours},
        {"minutes", &tc::duration_record::minutes},
        {"seconds", &tc::duration_record::seconds},
        {"milliseconds", &tc::duration_record::milliseconds},
        {"microseconds", &tc::duration_record::microseconds},
        {"nanoseconds", &tc::duration_record::nanoseconds},
    };
    for (const field_getter & f : fields) {
        const auto member = f.field;
        const std::string name = f.name;
        getter(cx, proto, name, [member, name](context & c, std::span<value>) {
            const auto d = this_duration(c, "get Temporal.Duration.prototype." + name);
            return d ? value::number((*d).*member) : value::undefined();
        });
    }
    getter(cx, proto, "sign", [](context & c, std::span<value>) {
        const auto d = this_duration(c, "get Temporal.Duration.prototype.sign");
        return d ? value::number(tc::duration_sign(*d)) : value::undefined();
    });
    getter(cx, proto, "blank", [](context & c, std::span<value>) {
        const auto d = this_duration(c, "get Temporal.Duration.prototype.blank");
        return d ? value::boolean(tc::duration_sign(*d) == 0) : value::undefined();
    });

    detail::method(cx, proto, "with", 1, [](context & c, std::span<value> a) {
        const auto d = this_duration(c, "Temporal.Duration.prototype.with");
        if (!d) { return value::undefined(); }
        tc::duration_record partial;
        if (!to_partial_duration(c, arg_at(a, 0), partial)) { return value::undefined(); }
        const auto pick = [](double given, double existing) {
            return std::isnan(given) ? existing : given;
        };
        return make_duration(
            c, tc::duration_record{pick(partial.years, d->years), pick(partial.months, d->months),
                                   pick(partial.weeks, d->weeks), pick(partial.days, d->days),
                                   pick(partial.hours, d->hours), pick(partial.minutes, d->minutes),
                                   pick(partial.seconds, d->seconds),
                                   pick(partial.milliseconds, d->milliseconds),
                                   pick(partial.microseconds, d->microseconds),
                                   pick(partial.nanoseconds, d->nanoseconds)});
    });
    detail::method(cx, proto, "negated", 0, [](context & c, std::span<value>) {
        const auto d = this_duration(c, "Temporal.Duration.prototype.negated");
        return d ? make_duration(c, tc::negate(*d)) : value::undefined();
    });
    detail::method(cx, proto, "abs", 0, [](context & c, std::span<value>) {
        const auto d = this_duration(c, "Temporal.Duration.prototype.abs");
        if (!d) { return value::undefined(); }
        return make_duration(
            c, tc::duration_record{std::fabs(d->years), std::fabs(d->months), std::fabs(d->weeks),
                                   std::fabs(d->days), std::fabs(d->hours), std::fabs(d->minutes),
                                   std::fabs(d->seconds), std::fabs(d->milliseconds),
                                   std::fabs(d->microseconds), std::fabs(d->nanoseconds)});
    });
    detail::method(cx, proto, "add", 1, [](context & c, std::span<value> a) {
        const auto d = this_duration(c, "Temporal.Duration.prototype.add");
        return d ? add_durations(c, false, *d, arg_at(a, 0)) : value::undefined();
    });
    detail::method(cx, proto, "subtract", 1, [](context & c, std::span<value> a) {
        const auto d = this_duration(c, "Temporal.Duration.prototype.subtract");
        return d ? add_durations(c, true, *d, arg_at(a, 0)) : value::undefined();
    });
    detail::method(cx, proto, "round", 1, duration_round);
    detail::method(cx, proto, "total", 1, duration_total);
    detail::method(cx, proto, "toString", 0, duration_to_string);
    detail::method(cx, proto, "toJSON", 0, [](context & c, std::span<value>) {
        const auto d = this_duration(c, "Temporal.Duration.prototype.toJSON");
        return d ? str(c, tc::temporal_duration_to_string(*d, tc::precision_auto))
                 : value::undefined();
    });
    detail::method(cx, proto, "toLocaleString", 0, [](context & c, std::span<value>) {
        const auto d = this_duration(c, "Temporal.Duration.prototype.toLocaleString");
        return d ? str(c, tc::temporal_duration_to_string(*d, tc::precision_auto))
                 : value::undefined();
    });
    detail::method(cx, proto, "valueOf", 0, [](context & c, std::span<value>) {
        (void)type_error(c, "Temporal.Duration has no primitive value; use compare()");
        return value::undefined();
    });
}

} // namespace ctbrowser::script::builtins_detail::temporal_js
