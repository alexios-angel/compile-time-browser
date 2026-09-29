// ctbrowser.script builtins - Temporal.Instant (8), an exact time, and
// Temporal.Now (2), the system clock read through each type.

#include "objects.hpp"

#include <cmath>

namespace ctbrowser::script::builtins_detail::temporal_js {

namespace {

// DifferenceTemporalInstant (8.5.9).
value difference(context & cx, bool since, tc::i128 ns, std::span<value> a) {
    tc::i128 other = 0;
    value options;
    difference_settings settings;
    if (!to_temporal_instant(cx, arg_at(a, 0), other) ||
        !get_options_object(cx, arg_at(a, 1), options) ||
        !get_difference_settings(cx, since, options, unit_group::time, {}, tc::unit::nanosecond,
                                 tc::unit::second, settings)) {
        return value::undefined();
    }
    const tc::internal_duration internal =
        tc::difference_instant(ns, other, settings.increment, settings.smallest, settings.mode);
    const auto result = tc::temporal_duration_from_internal(internal, settings.largest);
    if (!result) {
        (void)range_error(cx, "the result is outside the representable range");
        return value::undefined();
    }
    return make_duration(cx, since ? tc::negate(*result) : *result);
}

// AddDurationToInstant (8.5.10).
value add_duration(context & cx, bool subtract, tc::i128 ns, value like) {
    tc::duration_record d;
    if (!to_temporal_duration(cx, like, d)) { return value::undefined(); }
    if (subtract) { d = tc::negate(d); }
    if (tc::is_date_unit(tc::default_largest_unit(d))) {
        (void)range_error(cx, "an Instant cannot add years, months, weeks or days");
        return value::undefined();
    }
    const auto result = tc::add_instant(ns, tc::to_internal_duration_24h_days(d).time);
    if (!result) {
        (void)range_error(cx, "the result is outside the representable range");
        return value::undefined();
    }
    return make_instant(cx, *result);
}

value instant_round(context & cx, std::span<value> a) {
    const auto ns = this_instant(cx, "Temporal.Instant.prototype.round");
    if (!ns) { return value::undefined(); }
    std::int64_t increment = 1;
    tc::rounding_mode mode = tc::rounding_mode::half_expand;
    unit_value smallest;
    if (!get_round_options(cx, arg_at(a, 0), increment, mode, smallest)) {
        return value::undefined();
    }
    // The increments that divide a solar day.
    const auto maximum = static_cast<std::int64_t>(tc::ns_per_day / tc::unit_length_ns(smallest.u));
    if (!tc::valid_rounding_increment(increment, maximum, true)) {
        (void)range_error(cx, "invalid roundingIncrement");
        return value::undefined();
    }
    return make_instant(cx, tc::round_temporal_instant(*ns, increment, smallest.u, mode));
}

value instant_to_string(context & cx, std::span<value> a) {
    const auto ns = this_instant(cx, "Temporal.Instant.prototype.toString");
    if (!ns) { return value::undefined(); }
    value options;
    int digits = tc::precision_auto;
    tc::rounding_mode mode = tc::rounding_mode::trunc;
    unit_value smallest;
    value zone_value;
    if (!get_options_object(cx, arg_at(a, 0), options) ||
        !get_fractional_second_digits_option(cx, options, digits) ||
        !get_rounding_mode_option(cx, options, tc::rounding_mode::trunc, mode) ||
        !get_unit_option(cx, options, "smallestUnit", smallest) ||
        !get(cx, options, "timeZone", zone_value) ||
        !validate_unit_value(cx, smallest, unit_group::time)) {
        return value::undefined();
    }
    if (smallest.present && smallest.u == tc::unit::hour) {
        (void)range_error(cx, "smallestUnit must be minutes or smaller");
        return value::undefined();
    }
    std::string zone;
    if (!zone_value.is_undefined() && !to_time_zone_identifier(cx, zone_value, zone)) {
        return value::undefined();
    }
    const precision_record precision = seconds_string_precision(smallest, digits);
    const tc::i128 rounded =
        tc::round_temporal_instant(*ns, precision.increment, precision.u, mode);
    return str(cx, tc::instant_to_string(rounded, zone, precision.precision));
}

// ToBigInt, then the instant range.
[[nodiscard]] bool epoch_from_bigint(context & cx, value v, tc::i128 & out) {
    if (!to_i128_bigint(cx, v, out)) { return false; }
    if (!tc::is_valid_epoch_nanoseconds(out)) {
        return range_error(cx, "epoch nanoseconds outside the representable range");
    }
    return true;
}

// SystemUTCEpochNanoseconds, from the context's clock: the one Date.now
// reads (context::set_clock), so a page with a virtual clock sees one time.
[[nodiscard]] tc::i128 now_ns(context & cx) {
    return static_cast<tc::i128>(std::llround(cx.clock_ms() * 1'000'000.0));
}

// SystemDateTime: the zone given, else the system's ("UTC" here - this engine
// has no time zone database, and its Date's local time is UTC too).
[[nodiscard]] bool system_date_time(context & cx, value zone_like, tc::iso_datetime & out) {
    std::string zone = "UTC";
    if (!zone_like.is_undefined() && !to_time_zone_identifier(cx, zone_like, zone)) {
        return false;
    }
    out = tc::get_iso_datetime_for(zone, now_ns(cx));
    return true;
}

} // namespace

void install_instant(context & cx, object_object & ns) {
    const class_parts parts =
        make_class(cx, ns, kind::instant, "Instant", 1, [](context & c, std::span<value> a) {
            const value self = c.current_this();
            if (!detail::constructing_this(self)) {
                (void)type_error(c, "Constructor Temporal.Instant requires 'new'");
                return value::undefined();
            }
            tc::i128 epoch = 0;
            if (!epoch_from_bigint(c, arg_at(a, 0), epoch)) { return value::undefined(); }
            return make_instant(c, epoch, self);
        });
    native_object * ctor = parts.ctor;
    object_object * proto = parts.proto;

    detail::method(cx, ctor, "from", 1, [](context & c, std::span<value> a) {
        tc::i128 epoch = 0;
        if (!to_temporal_instant(c, arg_at(a, 0), epoch)) { return value::undefined(); }
        return make_instant(c, epoch);
    });
    detail::method(cx, ctor, "fromEpochMilliseconds", 1, [](context & c, std::span<value> a) {
        const value v = arg_at(a, 0);
        if (!numeric_arg(c, v)) { return value::undefined(); }
        if (v.is_kind(heap_kind::bigint)) {
            (void)type_error(c, "Cannot convert a BigInt value to a number");
            return value::undefined();
        }
        const detail::unwind_watch watch{c};
        const double ms = c.to_number_value(v);
        if (watch.threw()) { return value::undefined(); }
        // NumberToBigInt: a RangeError for a non-integral Number.
        if (!std::isfinite(ms) || std::trunc(ms) != ms) {
            (void)range_error(c, "epochMilliseconds must be an integer");
            return value::undefined();
        }
        if (std::fabs(ms) > 8.64e15) {
            (void)range_error(c, "epoch milliseconds outside the representable range");
            return value::undefined();
        }
        return make_instant(c, static_cast<tc::i128>(ms) * 1'000'000);
    });
    detail::method(cx, ctor, "fromEpochNanoseconds", 1, [](context & c, std::span<value> a) {
        tc::i128 epoch = 0;
        if (!epoch_from_bigint(c, arg_at(a, 0), epoch)) { return value::undefined(); }
        return make_instant(c, epoch);
    });
    detail::method(cx, ctor, "compare", 2, [](context & c, std::span<value> a) {
        tc::i128 one = 0;
        tc::i128 two = 0;
        if (!to_temporal_instant(c, arg_at(a, 0), one) ||
            !to_temporal_instant(c, arg_at(a, 1), two)) {
            return value::undefined();
        }
        return value::number(one > two ? 1 : (one < two ? -1 : 0));
    });

    getter(cx, proto, "epochMilliseconds", [](context & c, std::span<value>) {
        const auto ns = this_instant(c, "get Temporal.Instant.prototype.epochMilliseconds");
        if (!ns) { return value::undefined(); }
        tc::i128 ms = *ns / 1'000'000;
        if (*ns % 1'000'000 < 0) { ms -= 1; } // floor
        return value::number(static_cast<double>(ms) + 0.0);
    });
    getter(cx, proto, "epochNanoseconds", [](context & c, std::span<value>) {
        const auto ns = this_instant(c, "get Temporal.Instant.prototype.epochNanoseconds");
        return ns ? bigint_value(c, *ns) : value::undefined();
    });
    detail::method(cx, proto, "add", 1, [](context & c, std::span<value> a) {
        const auto ns = this_instant(c, "Temporal.Instant.prototype.add");
        return ns ? add_duration(c, false, *ns, arg_at(a, 0)) : value::undefined();
    });
    detail::method(cx, proto, "subtract", 1, [](context & c, std::span<value> a) {
        const auto ns = this_instant(c, "Temporal.Instant.prototype.subtract");
        return ns ? add_duration(c, true, *ns, arg_at(a, 0)) : value::undefined();
    });
    detail::method(cx, proto, "until", 1, [](context & c, std::span<value> a) {
        const auto ns = this_instant(c, "Temporal.Instant.prototype.until");
        return ns ? difference(c, false, *ns, a) : value::undefined();
    });
    detail::method(cx, proto, "since", 1, [](context & c, std::span<value> a) {
        const auto ns = this_instant(c, "Temporal.Instant.prototype.since");
        return ns ? difference(c, true, *ns, a) : value::undefined();
    });
    detail::method(cx, proto, "round", 1, instant_round);
    detail::method(cx, proto, "equals", 1, [](context & c, std::span<value> a) {
        const auto ns = this_instant(c, "Temporal.Instant.prototype.equals");
        if (!ns) { return value::undefined(); }
        tc::i128 other = 0;
        if (!to_temporal_instant(c, arg_at(a, 0), other)) { return value::undefined(); }
        return value::boolean(*ns == other);
    });
    detail::method(cx, proto, "toString", 0, instant_to_string);
    for (const char * name : {"toLocaleString", "toJSON"}) {
        const std::string method = std::string{"Temporal.Instant.prototype."} + name;
        detail::method(cx, proto, name, 0, [method](context & c, std::span<value>) {
            const auto ns = this_instant(c, method);
            return ns ? str(c, tc::instant_to_string(*ns, {}, tc::precision_auto))
                      : value::undefined();
        });
    }
    detail::method(cx, proto, "valueOf", 0, [](context & c, std::span<value>) {
        (void)type_error(c, "Temporal.Instant has no primitive value; use compare()");
        return value::undefined();
    });
    detail::method(cx, proto, "toZonedDateTimeISO", 1, [](context & c, std::span<value> a) {
        const auto ns = this_instant(c, "Temporal.Instant.prototype.toZonedDateTimeISO");
        if (!ns) { return value::undefined(); }
        std::string zone;
        if (!to_time_zone_identifier(c, arg_at(a, 0), zone)) { return value::undefined(); }
        return make_zoned(c, zoned{*ns, zone, "iso8601"});
    });
}

void install_now(context & cx, object_object & ns) {
    object_object * now = detail::new_table(cx);
    now->define(CTBROWSER_SYMBOL_KEY("toStringTag"), cx.string("Temporal.Now"), attr_configurable);
    detail::method(cx, now, "timeZoneId", 0,
                   [](context & c, std::span<value>) { return c.string("UTC"); });
    detail::method(cx, now, "instant", 0,
                   [](context & c, std::span<value>) { return make_instant(c, now_ns(c)); });
    detail::method(cx, now, "plainDateTimeISO", 0, [](context & c, std::span<value> a) {
        tc::iso_datetime dt;
        if (!system_date_time(c, arg_at(a, 0), dt)) { return value::undefined(); }
        return make_date_time(c, plain_date_time{dt, "iso8601"});
    });
    detail::method(cx, now, "zonedDateTimeISO", 0, [](context & c, std::span<value> a) {
        std::string zone = "UTC";
        const value zone_like = arg_at(a, 0);
        if (!zone_like.is_undefined() && !to_time_zone_identifier(c, zone_like, zone)) {
            return value::undefined();
        }
        return make_zoned(c, zoned{now_ns(c), zone, "iso8601"});
    });
    detail::method(cx, now, "plainDateISO", 0, [](context & c, std::span<value> a) {
        tc::iso_datetime dt;
        if (!system_date_time(c, arg_at(a, 0), dt)) { return value::undefined(); }
        return make_date(c, plain_date{dt.date, "iso8601"});
    });
    detail::method(cx, now, "plainTimeISO", 0, [](context & c, std::span<value> a) {
        tc::iso_datetime dt;
        if (!system_date_time(c, arg_at(a, 0), dt)) { return value::undefined(); }
        return make_time(c, dt.time);
    });
    ns.define("Now", value::object(now), attr_builtin);
}

} // namespace ctbrowser::script::builtins_detail::temporal_js
