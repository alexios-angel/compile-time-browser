//===- plain_date_time.cpp - Temporal.PlainDateTime -------------*- C++ -*-===//
//
// Temporal.PlainDateTime (Temporal proposal section 5): a calendar date and a
// wall-clock time, with no time zone. Section numbers below refer to the same
// specification.
//
//===----------------------------------------------------------------------===//

#include "objects.hpp"

namespace ctbrowser::script::builtins_detail::temporal_js {

namespace {

/// DifferenceTemporalPlainDateTime (5.5.15): until() and, with \p since, since().
value difference(context & cx, bool since, const plain_date_time & self, std::span<value> a) {
    plain_date_time other;
    if (!to_temporal_date_time(cx, arg_at(a, 0), value::undefined(), other)) {
        return value::undefined();
    }
    if (self.calendar != other.calendar) {
        (void)range_error(cx, "cannot compute a difference between two calendars");
        return value::undefined();
    }
    value options;
    difference_settings settings;
    if (!get_options_object(cx, arg_at(a, 1), options) ||
        !get_difference_settings(cx, since, options, unit_group::datetime, {}, tc::unit::nanosecond,
                                 tc::unit::day, settings)) {
        return value::undefined();
    }
    if (tc::compare_iso_datetime(self.dt, other.dt) == 0) {
        return make_duration(cx, tc::duration_record{});
    }
    const auto internal = tc::difference_plain_datetime_with_rounding(
        self.dt, other.dt, settings.largest, settings.increment, settings.smallest, settings.mode);
    const auto result =
        internal ? tc::temporal_duration_from_internal(*internal, settings.largest) : std::nullopt;
    if (!result) {
        (void)range_error(cx, "the result is outside the representable range");
        return value::undefined();
    }
    return make_duration(cx, since ? tc::negate(*result) : *result);
}

/// AddDurationToDateTime (5.5.16): add() and, with \p subtract, subtract().
value add_duration(context & cx, bool subtract, const plain_date_time & self, std::span<value> a) {
    tc::duration_record d;
    if (!to_temporal_duration(cx, arg_at(a, 0), d)) { return value::undefined(); }
    if (subtract) { d = tc::negate(d); }
    value options;
    tc::overflow how{};
    if (!get_options_object(cx, arg_at(a, 1), options) || !get_overflow_option(cx, options, how)) {
        return value::undefined();
    }
    const tc::internal_duration internal = tc::to_internal_duration_24h_days(d);
    tc::time_record time = tc::add_time(self.dt.time, internal.time);
    const auto date_duration = tc::create_date_duration(internal.date.years, internal.date.months,
                                                        internal.date.weeks, double(time.days));
    const auto added =
        date_duration ? tc::calendar_date_add(self.dt.date, *date_duration, how) : std::nullopt;
    if (!added) {
        (void)range_error(cx, "the result is not a valid date in range");
        return value::undefined();
    }
    time.days = 0;
    return make_date_time(cx, plain_date_time{tc::iso_datetime{*added, time}, self.calendar});
}

value date_time_with(context & cx, std::span<value> a) {
    const auto self = this_date_time(cx, "Temporal.PlainDateTime.prototype.with");
    if (!self) { return value::undefined(); }
    bool partial_object = false;
    if (!is_partial_temporal_object(cx, arg_at(a, 0), partial_object)) {
        return value::undefined();
    }
    if (!partial_object) {
        (void)type_error(cx, "with() needs an object of date and time properties");
        return value::undefined();
    }
    calendar_fields fields = iso_date_to_fields(self->dt.date, date_type::date);
    const tc::time_record & t = self->dt.time;
    fields.hour = t.hour;
    fields.minute = t.minute;
    fields.second = t.second;
    fields.millisecond = t.millisecond;
    fields.microsecond = t.microsecond;
    fields.nanosecond = t.nanosecond;
    calendar_fields partial;
    if (!prepare_calendar_fields(cx, arg_at(a, 0), date_fields | time_fields, 0, true, partial)) {
        return value::undefined();
    }
    fields = calendar_merge_fields(fields, partial);
    value options;
    tc::overflow how{};
    tc::iso_datetime result;
    if (!get_options_object(cx, arg_at(a, 1), options) || !get_overflow_option(cx, options, how) ||
        !interpret_date_time_fields(cx, fields, how, result)) {
        return value::undefined();
    }
    return make_date_time(cx, plain_date_time{result, self->calendar});
}

value date_time_round(context & cx, std::span<value> a) {
    const auto self = this_date_time(cx, "Temporal.PlainDateTime.prototype.round");
    if (!self) { return value::undefined(); }
    std::int64_t increment = 1;
    tc::rounding_mode mode = tc::rounding_mode::half_expand;
    unit_value smallest;
    if (!get_round_options(cx, arg_at(a, 0), increment, mode, smallest, {tc::unit::day})) {
        return value::undefined();
    }
    const bool day = smallest.u == tc::unit::day;
    const std::int64_t maximum = day ? 1 : tc::maximum_rounding_increment(smallest.u);
    if (!tc::valid_rounding_increment(increment, maximum, day)) {
        (void)range_error(cx, "invalid roundingIncrement");
        return value::undefined();
    }
    if (smallest.u == tc::unit::nanosecond && increment == 1) { return make_date_time(cx, *self); }
    return make_date_time(
        cx, plain_date_time{tc::round_iso_datetime(self->dt, increment, smallest.u, mode),
                            self->calendar});
}

value date_time_to_string(context & cx, std::span<value> a) {
    const auto self = this_date_time(cx, "Temporal.PlainDateTime.prototype.toString");
    if (!self) { return value::undefined(); }
    value options;
    tc::show_calendar show{};
    int digits = tc::precision_auto;
    tc::rounding_mode mode = tc::rounding_mode::trunc;
    unit_value smallest;
    if (!get_options_object(cx, arg_at(a, 0), options) ||
        !get_show_calendar_option(cx, options, show) ||
        !get_fractional_second_digits_option(cx, options, digits) ||
        !get_rounding_mode_option(cx, options, tc::rounding_mode::trunc, mode) ||
        !get_unit_option(cx, options, "smallestUnit", smallest) ||
        !validate_unit_value(cx, smallest, unit_group::time)) {
        return value::undefined();
    }
    if (smallest.present && smallest.u == tc::unit::hour) {
        (void)range_error(cx, "smallestUnit must be minutes or smaller");
        return value::undefined();
    }
    const precision_record precision = seconds_string_precision(smallest, digits);
    const tc::iso_datetime result =
        tc::round_iso_datetime(self->dt, precision.increment, precision.u, mode);
    if (!tc::iso_datetime_within_limits(result)) {
        (void)range_error(cx, "the rounded date-time is outside the representable range");
        return value::undefined();
    }
    return str(cx, tc::iso_datetime_to_string(result, self->calendar, precision.precision, show));
}

} // namespace

void install_plain_date_time(context & cx, object_object & ns) {
    const class_parts parts = make_class(
        cx, ns, kind::plain_date_time, "PlainDateTime", 3, [](context & c, std::span<value> a) {
            const value self = c.current_this();
            if (!detail::constructing_this(self)) {
                (void)type_error(c, "Constructor Temporal.PlainDateTime requires 'new'");
                return value::undefined();
            }
            double fields[9] = {};
            for (std::size_t i = 0; i < 9; ++i) {
                const value v = arg_at(a, i);
                if ((i < 3 || !v.is_undefined()) && !to_integer_with_truncation(c, v, fields[i])) {
                    return value::undefined();
                }
            }
            const value calendar_value = arg_at(a, 9);
            std::string calendar = "iso8601";
            if (!calendar_value.is_undefined()) {
                if (!calendar_value.is_string()) {
                    (void)type_error(c, "the calendar must be a string");
                    return value::undefined();
                }
                const auto canonical = tc::canonicalize_calendar(c.to_string(calendar_value));
                if (!canonical) {
                    (void)range_error(c, "unsupported calendar");
                    return value::undefined();
                }
                calendar = *canonical;
            }
            const auto date =
                tc::regulate_iso_date(fields[0], fields[1], fields[2], tc::overflow::reject);
            if (!date) {
                (void)range_error(c, "invalid date");
                return value::undefined();
            }
            const auto time = tc::regulate_time(fields[3], fields[4], fields[5], fields[6],
                                                fields[7], fields[8], tc::overflow::reject);
            if (!time) {
                (void)range_error(c, "invalid time");
                return value::undefined();
            }
            return make_date_time(c, plain_date_time{tc::iso_datetime{*date, *time}, calendar},
                                  self);
        });
    native_object * ctor = parts.ctor;
    object_object * proto = parts.proto;

    detail::method(cx, ctor, "from", 1, [](context & c, std::span<value> a) {
        plain_date_time d;
        if (!to_temporal_date_time(c, arg_at(a, 0), arg_at(a, 1), d)) { return value::undefined(); }
        return make_date_time(c, d);
    });
    detail::method(cx, ctor, "compare", 2, [](context & c, std::span<value> a) {
        plain_date_time one;
        plain_date_time two;
        if (!to_temporal_date_time(c, arg_at(a, 0), value::undefined(), one) ||
            !to_temporal_date_time(c, arg_at(a, 1), value::undefined(), two)) {
            return value::undefined();
        }
        return value::number(tc::compare_iso_datetime(one.dt, two.dt));
    });

    install_calendar_getters(cx, proto, kind::plain_date_time, all_calendar_getters);
    struct field_getter {
        const char * name;
        int tc::time_record::* field;
    };
    static constexpr field_getter time_getters[] = {
        {"hour", &tc::time_record::hour},
        {"minute", &tc::time_record::minute},
        {"second", &tc::time_record::second},
        {"millisecond", &tc::time_record::millisecond},
        {"microsecond", &tc::time_record::microsecond},
        {"nanosecond", &tc::time_record::nanosecond},
    };
    for (const field_getter & f : time_getters) {
        const auto member = f.field;
        const std::string name = f.name;
        getter(cx, proto, name, [member, name](context & c, std::span<value>) {
            const auto d = this_date_time(c, "get Temporal.PlainDateTime.prototype." + name);
            return d ? value::number(d->dt.time.*member) : value::undefined();
        });
    }

    detail::method(cx, proto, "with", 1, date_time_with);
    detail::method(cx, proto, "withPlainTime", 0, [](context & c, std::span<value> a) {
        const auto d = this_date_time(c, "Temporal.PlainDateTime.prototype.withPlainTime");
        if (!d) { return value::undefined(); }
        tc::time_record time;
        if (!to_time_record_or_midnight(c, arg_at(a, 0), time)) { return value::undefined(); }
        return make_date_time(c, plain_date_time{tc::iso_datetime{d->dt.date, time}, d->calendar});
    });
    detail::method(cx, proto, "withCalendar", 1, [](context & c, std::span<value> a) {
        const auto d = this_date_time(c, "Temporal.PlainDateTime.prototype.withCalendar");
        if (!d) { return value::undefined(); }
        std::string calendar;
        if (!to_calendar_identifier(c, arg_at(a, 0), calendar)) { return value::undefined(); }
        return make_date_time(c, plain_date_time{d->dt, calendar});
    });
    detail::method(cx, proto, "add", 1, [](context & c, std::span<value> a) {
        const auto d = this_date_time(c, "Temporal.PlainDateTime.prototype.add");
        return d ? add_duration(c, false, *d, a) : value::undefined();
    });
    detail::method(cx, proto, "subtract", 1, [](context & c, std::span<value> a) {
        const auto d = this_date_time(c, "Temporal.PlainDateTime.prototype.subtract");
        return d ? add_duration(c, true, *d, a) : value::undefined();
    });
    detail::method(cx, proto, "until", 1, [](context & c, std::span<value> a) {
        const auto d = this_date_time(c, "Temporal.PlainDateTime.prototype.until");
        return d ? difference(c, false, *d, a) : value::undefined();
    });
    detail::method(cx, proto, "since", 1, [](context & c, std::span<value> a) {
        const auto d = this_date_time(c, "Temporal.PlainDateTime.prototype.since");
        return d ? difference(c, true, *d, a) : value::undefined();
    });
    detail::method(cx, proto, "round", 1, date_time_round);
    detail::method(cx, proto, "equals", 1, [](context & c, std::span<value> a) {
        const auto d = this_date_time(c, "Temporal.PlainDateTime.prototype.equals");
        if (!d) { return value::undefined(); }
        plain_date_time other;
        if (!to_temporal_date_time(c, arg_at(a, 0), value::undefined(), other)) {
            return value::undefined();
        }
        return value::boolean(tc::compare_iso_datetime(d->dt, other.dt) == 0 &&
                              d->calendar == other.calendar);
    });
    detail::method(cx, proto, "toString", 0, date_time_to_string);
    for (const char * name : {"toLocaleString", "toJSON"}) {
        const std::string method = std::string{"Temporal.PlainDateTime.prototype."} + name;
        detail::method(cx, proto, name, 0, [method](context & c, std::span<value>) {
            const auto d = this_date_time(c, method);
            return d ? str(c, tc::iso_datetime_to_string(d->dt, d->calendar, tc::precision_auto,
                                                         tc::show_calendar::auto_))
                     : value::undefined();
        });
    }
    detail::method(cx, proto, "valueOf", 0, [](context & c, std::span<value>) {
        (void)type_error(c, "Temporal.PlainDateTime has no primitive value; use compare()");
        return value::undefined();
    });
    detail::method(cx, proto, "toZonedDateTime", 1, [](context & c, std::span<value> a) {
        const auto d = this_date_time(c, "Temporal.PlainDateTime.prototype.toZonedDateTime");
        if (!d) { return value::undefined(); }
        std::string zone;
        value options;
        tc::disambiguation how{};
        if (!to_time_zone_identifier(c, arg_at(a, 0), zone) ||
            !get_options_object(c, arg_at(a, 1), options) ||
            !get_disambiguation_option(c, options, how)) {
            return value::undefined();
        }
        const auto epoch_ns = tc::get_epoch_nanoseconds_for(zone, d->dt, how);
        if (!epoch_ns) {
            (void)range_error(c, "the date-time is outside the representable range");
            return value::undefined();
        }
        return make_zoned(c, zoned{*epoch_ns, zone, d->calendar});
    });
    detail::method(cx, proto, "toPlainDate", 0, [](context & c, std::span<value>) {
        const auto d = this_date_time(c, "Temporal.PlainDateTime.prototype.toPlainDate");
        return d ? make_date(c, plain_date{d->dt.date, d->calendar}) : value::undefined();
    });
    detail::method(cx, proto, "toPlainTime", 0, [](context & c, std::span<value>) {
        const auto d = this_date_time(c, "Temporal.PlainDateTime.prototype.toPlainTime");
        return d ? make_time(c, d->dt.time) : value::undefined();
    });
}

} // namespace ctbrowser::script::builtins_detail::temporal_js
