//===- plain_date.cpp - Temporal.PlainDate ----------------------*- C++ -*-===//
//
// Temporal.PlainDate (Temporal proposal section 3): a calendar date with no
// time and no time zone. Section numbers below refer to the same
// specification.
//
//===----------------------------------------------------------------------===//

#include "objects.hpp"

namespace ctbrowser::script::builtins_detail::temporal_js {

namespace {

constexpr std::string_view prefix = "Temporal.PlainDate.prototype.";

/// DifferenceTemporalPlainDate (3.5.14): until() and, with \p since, since().
value difference(context & cx, bool since, const plain_date & date, std::span<value> a) {
    plain_date other;
    if (!to_temporal_date(cx, arg_at(a, 0), value::undefined(), other)) {
        return value::undefined();
    }
    if (date.calendar != other.calendar) {
        (void)range_error(cx, "cannot compute a difference between two calendars");
        return value::undefined();
    }
    value options;
    difference_settings settings;
    if (!get_options_object(cx, arg_at(a, 1), options) ||
        !get_difference_settings(cx, since, options, unit_group::date, {}, tc::unit::day,
                                 tc::unit::day, settings)) {
        return value::undefined();
    }
    if (tc::compare_iso_date(date.date, other.date) == 0) {
        return make_duration(cx, tc::duration_record{});
    }
    tc::internal_duration duration{tc::calendar_date_until(date.date, other.date, settings.largest),
                                   0};
    if (settings.smallest != tc::unit::day || settings.increment != 1) {
        const tc::iso_datetime origin{date.date, tc::time_record{}};
        const tc::iso_datetime dest{other.date, tc::time_record{}};
        const auto rounded = tc::round_relative_duration(
            duration, tc::get_utc_epoch_nanoseconds(origin), tc::get_utc_epoch_nanoseconds(dest),
            origin, {}, settings.largest, settings.increment, settings.smallest, settings.mode);
        if (!rounded) {
            (void)range_error(cx, "the result is outside the representable range");
            return value::undefined();
        }
        duration = *rounded;
    }
    auto result = tc::temporal_duration_from_internal(duration, tc::unit::day);
    if (!result) {
        (void)range_error(cx, "invalid duration");
        return value::undefined();
    }
    return make_duration(cx, since ? tc::negate(*result) : *result);
}

/// AddDurationToDate (3.5.15): add() and, with \p subtract, subtract().
value add_duration(context & cx, bool subtract, const plain_date & date, std::span<value> a) {
    tc::duration_record d;
    if (!to_temporal_duration(cx, arg_at(a, 0), d)) { return value::undefined(); }
    if (subtract) { d = tc::negate(d); }
    const auto date_duration = tc::to_date_duration_without_time(d);
    value options;
    tc::overflow how{};
    if (!get_options_object(cx, arg_at(a, 1), options) || !get_overflow_option(cx, options, how)) {
        return value::undefined();
    }
    const auto result =
        date_duration ? tc::calendar_date_add(date.date, *date_duration, how) : std::nullopt;
    if (!result) {
        (void)range_error(cx, "the result is not a valid date in range");
        return value::undefined();
    }
    return make_date(cx, plain_date{*result, date.calendar});
}

value date_with(context & cx, std::span<value> a) {
    const auto date = this_date(cx, "Temporal.PlainDate.prototype.with");
    if (!date) { return value::undefined(); }
    bool partial_object = false;
    if (!is_partial_temporal_object(cx, arg_at(a, 0), partial_object)) {
        return value::undefined();
    }
    if (!partial_object) {
        (void)type_error(cx, "with() needs an object of date properties");
        return value::undefined();
    }
    calendar_fields partial;
    if (!prepare_calendar_fields(cx, arg_at(a, 0), date_fields, 0, true, partial)) {
        return value::undefined();
    }
    const calendar_fields fields =
        calendar_merge_fields(iso_date_to_fields(date->date, date_type::date), partial);
    value options;
    tc::overflow how{};
    tc::iso_date result;
    if (!get_options_object(cx, arg_at(a, 1), options) || !get_overflow_option(cx, options, how) ||
        !calendar_date_from_fields(cx, fields, how, result)) {
        return value::undefined();
    }
    return make_date(cx, plain_date{result, date->calendar});
}

value date_to_zoned(context & cx, std::span<value> a) {
    const auto date = this_date(cx, "Temporal.PlainDate.prototype.toZonedDateTime");
    if (!date) { return value::undefined(); }
    const value item = arg_at(a, 0);
    std::string zone;
    value time_like = value::undefined();
    if (item.is_object_like()) {
        value zone_like;
        if (!get(cx, item, "timeZone", zone_like)) { return value::undefined(); }
        if (zone_like.is_undefined()) {
            if (!to_time_zone_identifier(cx, item, zone)) { return value::undefined(); }
        } else {
            if (!to_time_zone_identifier(cx, zone_like, zone) ||
                !get(cx, item, "plainTime", time_like)) {
                return value::undefined();
            }
        }
    } else if (!to_time_zone_identifier(cx, item, zone)) {
        return value::undefined();
    }
    std::optional<tc::i128> epoch_ns;
    if (time_like.is_undefined()) {
        epoch_ns = tc::get_start_of_day(zone, date->date);
    } else {
        tc::time_record time;
        if (!to_temporal_time(cx, time_like, value::undefined(), time)) {
            return value::undefined();
        }
        const tc::iso_datetime dt{date->date, time};
        if (tc::iso_datetime_within_limits(dt)) {
            epoch_ns = tc::get_epoch_nanoseconds_for(zone, dt, tc::disambiguation::compatible);
        }
    }
    if (!epoch_ns) {
        (void)range_error(cx, "the date is outside the representable range");
        return value::undefined();
    }
    return make_zoned(cx, zoned{*epoch_ns, zone, date->calendar});
}

} // namespace

void install_plain_date(context & cx, object_object & ns) {
    const class_parts parts =
        make_class(cx, ns, kind::plain_date, "PlainDate", 3, [](context & c, std::span<value> a) {
            const value self = c.current_this();
            if (!detail::constructing_this(self)) {
                (void)type_error(c, "Constructor Temporal.PlainDate requires 'new'");
                return value::undefined();
            }
            double y = 0, m = 0, d = 0;
            if (!to_integer_with_truncation(c, arg_at(a, 0), y) ||
                !to_integer_with_truncation(c, arg_at(a, 1), m) ||
                !to_integer_with_truncation(c, arg_at(a, 2), d)) {
                return value::undefined();
            }
            value calendar_value = arg_at(a, 3);
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
            const auto date = tc::regulate_iso_date(y, m, d, tc::overflow::reject);
            // Years beyond +/-1e9 are far outside the representable range
            // (about +/-275760); reject them before the date is built.
            if (!date || std::fabs(y) > 1e9) {
                (void)range_error(c, "invalid date");
                return value::undefined();
            }
            return make_date(c, plain_date{*date, calendar}, self);
        });
    native_object * ctor = parts.ctor;
    object_object * proto = parts.proto;

    detail::method(cx, ctor, "from", 1, [](context & c, std::span<value> a) {
        plain_date d;
        if (!to_temporal_date(c, arg_at(a, 0), arg_at(a, 1), d)) { return value::undefined(); }
        return make_date(c, d);
    });
    detail::method(cx, ctor, "compare", 2, [](context & c, std::span<value> a) {
        plain_date one;
        plain_date two;
        if (!to_temporal_date(c, arg_at(a, 0), value::undefined(), one) ||
            !to_temporal_date(c, arg_at(a, 1), value::undefined(), two)) {
            return value::undefined();
        }
        return value::number(tc::compare_iso_date(one.date, two.date));
    });

    install_calendar_getters(cx, proto, kind::plain_date, all_calendar_getters);

    detail::method(cx, proto, "toPlainYearMonth", 0, [](context & c, std::span<value>) {
        const auto d = this_date(c, "Temporal.PlainDate.prototype.toPlainYearMonth");
        if (!d) { return value::undefined(); }
        tc::iso_date result;
        if (!calendar_year_month_from_fields(c, iso_date_to_fields(d->date, date_type::date),
                                             tc::overflow::constrain, result)) {
            return value::undefined();
        }
        return make_year_month(c, plain_date{result, d->calendar});
    });
    detail::method(cx, proto, "toPlainMonthDay", 0, [](context & c, std::span<value>) {
        const auto d = this_date(c, "Temporal.PlainDate.prototype.toPlainMonthDay");
        if (!d) { return value::undefined(); }
        tc::iso_date result;
        if (!calendar_month_day_from_fields(c, iso_date_to_fields(d->date, date_type::date),
                                            tc::overflow::constrain, result)) {
            return value::undefined();
        }
        return make_month_day(c, plain_date{result, d->calendar});
    });
    detail::method(cx, proto, "add", 1, [](context & c, std::span<value> a) {
        const auto d = this_date(c, "Temporal.PlainDate.prototype.add");
        return d ? add_duration(c, false, *d, a) : value::undefined();
    });
    detail::method(cx, proto, "subtract", 1, [](context & c, std::span<value> a) {
        const auto d = this_date(c, "Temporal.PlainDate.prototype.subtract");
        return d ? add_duration(c, true, *d, a) : value::undefined();
    });
    detail::method(cx, proto, "with", 1, date_with);
    detail::method(cx, proto, "withCalendar", 1, [](context & c, std::span<value> a) {
        const auto d = this_date(c, "Temporal.PlainDate.prototype.withCalendar");
        if (!d) { return value::undefined(); }
        std::string calendar;
        if (!to_calendar_identifier(c, arg_at(a, 0), calendar)) { return value::undefined(); }
        return make_date(c, plain_date{d->date, calendar});
    });
    detail::method(cx, proto, "until", 1, [](context & c, std::span<value> a) {
        const auto d = this_date(c, "Temporal.PlainDate.prototype.until");
        return d ? difference(c, false, *d, a) : value::undefined();
    });
    detail::method(cx, proto, "since", 1, [](context & c, std::span<value> a) {
        const auto d = this_date(c, "Temporal.PlainDate.prototype.since");
        return d ? difference(c, true, *d, a) : value::undefined();
    });
    detail::method(cx, proto, "equals", 1, [](context & c, std::span<value> a) {
        const auto d = this_date(c, "Temporal.PlainDate.prototype.equals");
        if (!d) { return value::undefined(); }
        plain_date other;
        if (!to_temporal_date(c, arg_at(a, 0), value::undefined(), other)) {
            return value::undefined();
        }
        return value::boolean(tc::compare_iso_date(d->date, other.date) == 0 &&
                              d->calendar == other.calendar);
    });
    detail::method(cx, proto, "toPlainDateTime", 0, [](context & c, std::span<value> a) {
        const auto d = this_date(c, "Temporal.PlainDate.prototype.toPlainDateTime");
        if (!d) { return value::undefined(); }
        tc::time_record time;
        if (!to_time_record_or_midnight(c, arg_at(a, 0), time)) { return value::undefined(); }
        return make_date_time(c, plain_date_time{tc::iso_datetime{d->date, time}, d->calendar});
    });
    detail::method(cx, proto, "toZonedDateTime", 1, date_to_zoned);
    detail::method(cx, proto, "toString", 0, [](context & c, std::span<value> a) {
        const auto d = this_date(c, "Temporal.PlainDate.prototype.toString");
        if (!d) { return value::undefined(); }
        value options;
        tc::show_calendar show{};
        if (!get_options_object(c, arg_at(a, 0), options) ||
            !get_show_calendar_option(c, options, show)) {
            return value::undefined();
        }
        return str(c, tc::temporal_date_to_string(d->date, d->calendar, show));
    });
    for (const char * name : {"toLocaleString", "toJSON"}) {
        const std::string method = std::string{prefix} + name;
        detail::method(cx, proto, name, 0, [method](context & c, std::span<value>) {
            const auto d = this_date(c, method);
            return d ? str(c, tc::temporal_date_to_string(d->date, d->calendar,
                                                          tc::show_calendar::auto_))
                     : value::undefined();
        });
    }
    detail::method(cx, proto, "valueOf", 0, [](context & c, std::span<value>) {
        (void)type_error(c, "Temporal.PlainDate has no primitive value; use compare()");
        return value::undefined();
    });
}

} // namespace ctbrowser::script::builtins_detail::temporal_js
