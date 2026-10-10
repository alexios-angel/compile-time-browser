//===- year_month_day.cpp - PlainYearMonth, PlainMonthDay -------*- C++ -*-===//
//
// Temporal.PlainYearMonth (Temporal proposal section 9) and
// Temporal.PlainMonthDay (section 10): a date with its day, or its year, left
// out. Each keeps a full ISO date, filling the gap with a reference day or
// year, as the specification's [[ISODate]] does. Section numbers below refer
// to the same specification.
//
//===----------------------------------------------------------------------===//

#include "objects.hpp"

namespace ctbrowser::script::builtins_detail::temporal_js {

namespace {

/// The calendar argument of a constructor: absent is "iso8601", a non-string
/// a TypeError, an unknown one a RangeError.
[[nodiscard]] bool constructor_calendar(context & cx, value v, std::string & out) {
    out = "iso8601";
    if (v.is_undefined()) { return true; }
    if (!v.is_string()) { return type_error(cx, "the calendar must be a string"); }
    const auto canonical = tc::canonicalize_calendar(cx.to_string(v));
    if (!canonical) { return range_error(cx, "unsupported calendar"); }
    out = *canonical;
    return true;
}

/// The first day of the year-month, where DifferenceTemporalPlainYearMonth and
/// AddDurationToYearMonth start.
[[nodiscard]] bool first_of_month(context & cx, const tc::iso_date & date, tc::iso_date & out) {
    calendar_fields fields = iso_date_to_fields(date, date_type::year_month);
    fields.day = 1;
    return calendar_date_from_fields(cx, fields, tc::overflow::constrain, out);
}

/// DifferenceTemporalPlainYearMonth (9.5.7): until() and, with \p since, since().
value difference(context & cx, bool since, const plain_date & self, std::span<value> a) {
    plain_date other;
    if (!to_temporal_year_month(cx, arg_at(a, 0), value::undefined(), other)) {
        return value::undefined();
    }
    if (self.calendar != other.calendar) {
        (void)range_error(cx, "cannot compute a difference between two calendars");
        return value::undefined();
    }
    value options;
    difference_settings settings;
    if (!get_options_object(cx, arg_at(a, 1), options) ||
        !get_difference_settings(cx, since, options, unit_group::date,
                                 {tc::unit::week, tc::unit::day}, tc::unit::month, tc::unit::year,
                                 settings)) {
        return value::undefined();
    }
    if (tc::compare_iso_date(self.date, other.date) == 0) {
        return make_duration(cx, tc::duration_record{});
    }
    tc::iso_date this_date;
    tc::iso_date other_date;
    if (!first_of_month(cx, self.date, this_date) || !first_of_month(cx, other.date, other_date)) {
        return value::undefined();
    }
    const tc::date_duration until =
        tc::calendar_date_until(this_date, other_date, settings.largest);
    tc::internal_duration duration{tc::date_duration{until.years, until.months, 0, 0}, 0};
    if (settings.smallest != tc::unit::month || settings.increment != 1) {
        const tc::iso_datetime origin{this_date, tc::time_record{}};
        const tc::iso_datetime dest{other_date, tc::time_record{}};
        const auto rounded = tc::round_relative_duration(
            duration, tc::get_utc_epoch_nanoseconds(origin), tc::get_utc_epoch_nanoseconds(dest),
            origin, {}, settings.largest, settings.increment, settings.smallest, settings.mode);
        if (!rounded) {
            (void)range_error(cx, "the result is outside the representable range");
            return value::undefined();
        }
        duration = *rounded;
    }
    const auto result = tc::temporal_duration_from_internal(duration, tc::unit::day);
    if (!result) {
        (void)range_error(cx, "invalid duration");
        return value::undefined();
    }
    return make_duration(cx, since ? tc::negate(*result) : *result);
}

/// AddDurationToYearMonth (9.5.8): add() and, with \p subtract, subtract().
value add_duration(context & cx, bool subtract, const plain_date & self, std::span<value> a) {
    tc::duration_record d;
    if (!to_temporal_duration(cx, arg_at(a, 0), d)) { return value::undefined(); }
    if (subtract) { d = tc::negate(d); }
    const tc::internal_duration internal = tc::to_internal_duration(d);
    value options;
    tc::overflow how{};
    if (!get_options_object(cx, arg_at(a, 1), options) || !get_overflow_option(cx, options, how)) {
        return value::undefined();
    }
    if (internal.date.weeks != 0 || internal.date.days != 0 || internal.time != 0) {
        (void)range_error(cx, "a PlainYearMonth can add only years and months");
        return value::undefined();
    }
    tc::iso_date start;
    if (!first_of_month(cx, self.date, start)) { return value::undefined(); }
    const auto added = tc::calendar_date_add(start, internal.date, how);
    if (!added) {
        (void)range_error(cx, "the result is outside the representable range");
        return value::undefined();
    }
    tc::iso_date result;
    if (!calendar_year_month_from_fields(cx, iso_date_to_fields(*added, date_type::year_month), how,
                                         result)) {
        return value::undefined();
    }
    return make_year_month(cx, plain_date{result, self.calendar});
}

value year_month_with(context & cx, std::span<value> a) {
    const auto self = this_year_month(cx, "Temporal.PlainYearMonth.prototype.with");
    if (!self) { return value::undefined(); }
    bool partial_object = false;
    if (!is_partial_temporal_object(cx, arg_at(a, 0), partial_object)) {
        return value::undefined();
    }
    if (!partial_object) {
        (void)type_error(cx, "with() needs an object of year and month properties");
        return value::undefined();
    }
    calendar_fields partial;
    if (!prepare_calendar_fields(cx, arg_at(a, 0), f_year | f_month | f_month_code, 0, true,
                                 partial)) {
        return value::undefined();
    }
    const calendar_fields fields =
        calendar_merge_fields(iso_date_to_fields(self->date, date_type::year_month), partial);
    value options;
    tc::overflow how{};
    tc::iso_date result;
    if (!get_options_object(cx, arg_at(a, 1), options) || !get_overflow_option(cx, options, how) ||
        !calendar_year_month_from_fields(cx, fields, how, result)) {
        return value::undefined();
    }
    return make_year_month(cx, plain_date{result, self->calendar});
}

value month_day_with(context & cx, std::span<value> a) {
    const auto self = this_month_day(cx, "Temporal.PlainMonthDay.prototype.with");
    if (!self) { return value::undefined(); }
    bool partial_object = false;
    if (!is_partial_temporal_object(cx, arg_at(a, 0), partial_object)) {
        return value::undefined();
    }
    if (!partial_object) {
        (void)type_error(cx, "with() needs an object of month and day properties");
        return value::undefined();
    }
    calendar_fields partial;
    if (!prepare_calendar_fields(cx, arg_at(a, 0), date_fields, 0, true, partial)) {
        return value::undefined();
    }
    const calendar_fields fields =
        calendar_merge_fields(iso_date_to_fields(self->date, date_type::month_day), partial);
    value options;
    tc::overflow how{};
    tc::iso_date result;
    if (!get_options_object(cx, arg_at(a, 1), options) || !get_overflow_option(cx, options, how) ||
        !calendar_month_day_from_fields(cx, fields, how, result)) {
        return value::undefined();
    }
    return make_month_day(cx, plain_date{result, self->calendar});
}

/// toPlainDate of either class: the field the object lacks (day or year) is
/// read from \p item, merged in and constrained.
value to_plain_date(context & cx, const plain_date & self, value item, date_type type,
                    std::uint32_t input) {
    if (!item.is_object_like()) {
        (void)type_error(cx, "toPlainDate() needs an object");
        return value::undefined();
    }
    calendar_fields fields;
    if (!prepare_calendar_fields(cx, item, input, 0, false, fields)) { return value::undefined(); }
    const calendar_fields merged =
        calendar_merge_fields(iso_date_to_fields(self.date, type), fields);
    tc::iso_date result;
    if (!calendar_date_from_fields(cx, merged, tc::overflow::constrain, result)) {
        return value::undefined();
    }
    return make_date(cx, plain_date{result, self.calendar});
}

} // namespace

void install_plain_year_month(context & cx, object_object & ns) {
    const class_parts parts = make_class(
        cx, ns, kind::plain_year_month, "PlainYearMonth", 2, [](context & c, std::span<value> a) {
            const value self = c.current_this();
            if (!detail::constructing_this(self)) {
                (void)type_error(c, "Constructor Temporal.PlainYearMonth requires 'new'");
                return value::undefined();
            }
            double y = 0, m = 0, ref = 1;
            std::string calendar;
            if (!to_integer_with_truncation(c, arg_at(a, 0), y) ||
                !to_integer_with_truncation(c, arg_at(a, 1), m) ||
                !constructor_calendar(c, arg_at(a, 2), calendar) ||
                (!arg_at(a, 3).is_undefined() &&
                 !to_integer_with_truncation(c, arg_at(a, 3), ref))) {
                return value::undefined();
            }
            const auto date = tc::regulate_iso_date(y, m, ref, tc::overflow::reject);
            if (!date) {
                (void)range_error(c, "invalid year-month");
                return value::undefined();
            }
            return make_year_month(c, plain_date{*date, calendar}, self);
        });
    native_object * ctor = parts.ctor;
    object_object * proto = parts.proto;

    detail::method(cx, ctor, "from", 1, [](context & c, std::span<value> a) {
        plain_date d;
        if (!to_temporal_year_month(c, arg_at(a, 0), arg_at(a, 1), d)) {
            return value::undefined();
        }
        return make_year_month(c, d);
    });
    detail::method(cx, ctor, "compare", 2, [](context & c, std::span<value> a) {
        plain_date one;
        plain_date two;
        if (!to_temporal_year_month(c, arg_at(a, 0), value::undefined(), one) ||
            !to_temporal_year_month(c, arg_at(a, 1), value::undefined(), two)) {
            return value::undefined();
        }
        return value::number(tc::compare_iso_date(one.date, two.date));
    });

    install_calendar_getters(cx, proto, kind::plain_year_month,
                             g_calendar_id | g_era | g_era_year | g_year | g_month | g_month_code |
                                 g_days_in_year | g_days_in_month | g_months_in_year |
                                 g_in_leap_year);
    detail::method(cx, proto, "with", 1, year_month_with);
    detail::method(cx, proto, "add", 1, [](context & c, std::span<value> a) {
        const auto d = this_year_month(c, "Temporal.PlainYearMonth.prototype.add");
        return d ? add_duration(c, false, *d, a) : value::undefined();
    });
    detail::method(cx, proto, "subtract", 1, [](context & c, std::span<value> a) {
        const auto d = this_year_month(c, "Temporal.PlainYearMonth.prototype.subtract");
        return d ? add_duration(c, true, *d, a) : value::undefined();
    });
    detail::method(cx, proto, "until", 1, [](context & c, std::span<value> a) {
        const auto d = this_year_month(c, "Temporal.PlainYearMonth.prototype.until");
        return d ? difference(c, false, *d, a) : value::undefined();
    });
    detail::method(cx, proto, "since", 1, [](context & c, std::span<value> a) {
        const auto d = this_year_month(c, "Temporal.PlainYearMonth.prototype.since");
        return d ? difference(c, true, *d, a) : value::undefined();
    });
    detail::method(cx, proto, "equals", 1, [](context & c, std::span<value> a) {
        const auto d = this_year_month(c, "Temporal.PlainYearMonth.prototype.equals");
        if (!d) { return value::undefined(); }
        plain_date other;
        if (!to_temporal_year_month(c, arg_at(a, 0), value::undefined(), other)) {
            return value::undefined();
        }
        return value::boolean(tc::compare_iso_date(d->date, other.date) == 0 &&
                              d->calendar == other.calendar);
    });
    detail::method(cx, proto, "toString", 0, [](context & c, std::span<value> a) {
        const auto d = this_year_month(c, "Temporal.PlainYearMonth.prototype.toString");
        if (!d) { return value::undefined(); }
        value options;
        tc::show_calendar show{};
        if (!get_options_object(c, arg_at(a, 0), options) ||
            !get_show_calendar_option(c, options, show)) {
            return value::undefined();
        }
        return str(c, tc::year_month_to_string(d->date, d->calendar, show));
    });
    for (const char * name : {"toLocaleString", "toJSON"}) {
        const std::string method = std::string{"Temporal.PlainYearMonth.prototype."} + name;
        detail::method(cx, proto, name, 0, [method](context & c, std::span<value>) {
            const auto d = this_year_month(c, method);
            return d ? str(c,
                           tc::year_month_to_string(d->date, d->calendar, tc::show_calendar::auto_))
                     : value::undefined();
        });
    }
    detail::method(cx, proto, "valueOf", 0, [](context & c, std::span<value>) {
        (void)type_error(c, "Temporal.PlainYearMonth has no primitive value; use compare()");
        return value::undefined();
    });
    detail::method(cx, proto, "toPlainDate", 1, [](context & c, std::span<value> a) {
        const auto d = this_year_month(c, "Temporal.PlainYearMonth.prototype.toPlainDate");
        return d ? to_plain_date(c, *d, arg_at(a, 0), date_type::year_month, f_day)
                 : value::undefined();
    });
}

void install_plain_month_day(context & cx, object_object & ns) {
    const class_parts parts = make_class(
        cx, ns, kind::plain_month_day, "PlainMonthDay", 2, [](context & c, std::span<value> a) {
            const value self = c.current_this();
            if (!detail::constructing_this(self)) {
                (void)type_error(c, "Constructor Temporal.PlainMonthDay requires 'new'");
                return value::undefined();
            }
            double m = 0, d = 0, ref = 1972;
            std::string calendar;
            if (!to_integer_with_truncation(c, arg_at(a, 0), m) ||
                !to_integer_with_truncation(c, arg_at(a, 1), d) ||
                !constructor_calendar(c, arg_at(a, 2), calendar) ||
                (!arg_at(a, 3).is_undefined() &&
                 !to_integer_with_truncation(c, arg_at(a, 3), ref))) {
                return value::undefined();
            }
            const auto date = tc::regulate_iso_date(ref, m, d, tc::overflow::reject);
            if (!date) {
                (void)range_error(c, "invalid month-day");
                return value::undefined();
            }
            return make_month_day(c, plain_date{*date, calendar}, self);
        });
    native_object * ctor = parts.ctor;
    object_object * proto = parts.proto;

    detail::method(cx, ctor, "from", 1, [](context & c, std::span<value> a) {
        plain_date d;
        if (!to_temporal_month_day(c, arg_at(a, 0), arg_at(a, 1), d)) { return value::undefined(); }
        return make_month_day(c, d);
    });

    install_calendar_getters(cx, proto, kind::plain_month_day,
                             g_calendar_id | g_month_code | g_day);
    detail::method(cx, proto, "with", 1, month_day_with);
    detail::method(cx, proto, "equals", 1, [](context & c, std::span<value> a) {
        const auto d = this_month_day(c, "Temporal.PlainMonthDay.prototype.equals");
        if (!d) { return value::undefined(); }
        plain_date other;
        if (!to_temporal_month_day(c, arg_at(a, 0), value::undefined(), other)) {
            return value::undefined();
        }
        return value::boolean(tc::compare_iso_date(d->date, other.date) == 0 &&
                              d->calendar == other.calendar);
    });
    detail::method(cx, proto, "toString", 0, [](context & c, std::span<value> a) {
        const auto d = this_month_day(c, "Temporal.PlainMonthDay.prototype.toString");
        if (!d) { return value::undefined(); }
        value options;
        tc::show_calendar show{};
        if (!get_options_object(c, arg_at(a, 0), options) ||
            !get_show_calendar_option(c, options, show)) {
            return value::undefined();
        }
        return str(c, tc::month_day_to_string(d->date, d->calendar, show));
    });
    for (const char * name : {"toLocaleString", "toJSON"}) {
        const std::string method = std::string{"Temporal.PlainMonthDay.prototype."} + name;
        detail::method(cx, proto, name, 0, [method](context & c, std::span<value>) {
            const auto d = this_month_day(c, method);
            return d ? str(c,
                           tc::month_day_to_string(d->date, d->calendar, tc::show_calendar::auto_))
                     : value::undefined();
        });
    }
    detail::method(cx, proto, "valueOf", 0, [](context & c, std::span<value>) {
        (void)type_error(c, "Temporal.PlainMonthDay has no primitive value");
        return value::undefined();
    });
    detail::method(cx, proto, "toPlainDate", 1, [](context & c, std::span<value> a) {
        const auto d = this_month_day(c, "Temporal.PlainMonthDay.prototype.toPlainDate");
        return d ? to_plain_date(c, *d, arg_at(a, 0), date_type::month_day, f_year)
                 : value::undefined();
    });
}

} // namespace ctbrowser::script::builtins_detail::temporal_js
