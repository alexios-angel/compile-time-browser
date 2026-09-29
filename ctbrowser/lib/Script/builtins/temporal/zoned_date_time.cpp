// ctbrowser.script builtins - Temporal.ZonedDateTime (6): an exact time in a
// time zone and calendar. The zones here have fixed offsets, so there are no
// transitions: getTimeZoneTransition answers null and every day is 24 hours.

#include "objects.hpp"

namespace ctbrowser::script::builtins_detail::temporal_js {

namespace {

[[nodiscard]] tc::iso_datetime local(const zoned & z) {
    return tc::get_iso_datetime_for(z.zone, z.epoch_ns);
}

// TemporalZonedDateTimeToString (6.5.4).
[[nodiscard]] std::string zoned_to_string(const zoned & z, int precision, tc::show_calendar show,
                                          tc::show_calendar show_zone, bool show_offset,
                                          std::int64_t increment = 1,
                                          tc::unit u = tc::unit::nanosecond,
                                          tc::rounding_mode mode = tc::rounding_mode::trunc) {
    const tc::i128 ns = tc::round_temporal_instant(z.epoch_ns, increment, u, mode);
    const tc::i128 offset = tc::get_offset_nanoseconds_for(z.zone, ns);
    std::string result = tc::iso_datetime_to_string(tc::get_iso_datetime_for(z.zone, ns), "iso8601",
                                                    precision, tc::show_calendar::never);
    if (show_offset) { result += tc::format_datetime_utc_offset_rounded(offset); }
    if (show_zone != tc::show_calendar::never) {
        result +=
            std::string{"["} + (show_zone == tc::show_calendar::critical ? "!" : "") + z.zone + "]";
    }
    return result + tc::format_calendar_annotation(z.calendar, show);
}

// DifferenceTemporalZonedDateTime (6.5.9).
value difference(context & cx, bool since, const zoned & self, std::span<value> a) {
    zoned other;
    if (!to_temporal_zoned(cx, arg_at(a, 0), value::undefined(), other)) {
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
                                 tc::unit::hour, settings)) {
        return value::undefined();
    }
    std::optional<tc::internal_duration> internal;
    tc::unit balance = tc::unit::hour;
    if (!tc::is_date_unit(settings.largest)) {
        internal = tc::difference_instant(self.epoch_ns, other.epoch_ns, settings.increment,
                                          settings.smallest, settings.mode);
        balance = settings.largest;
    } else {
        if (!tc::time_zone_equals(self.zone, other.zone)) {
            (void)range_error(cx, "differences in days or larger need the same time zone");
            return value::undefined();
        }
        if (self.epoch_ns == other.epoch_ns) { return make_duration(cx, tc::duration_record{}); }
        internal = tc::difference_zoned_datetime_with_rounding(
            self.epoch_ns, other.epoch_ns, self.zone, settings.largest, settings.increment,
            settings.smallest, settings.mode);
    }
    const auto result =
        internal ? tc::temporal_duration_from_internal(*internal, balance) : std::nullopt;
    if (!result) {
        (void)range_error(cx, "the result is outside the representable range");
        return value::undefined();
    }
    return make_duration(cx, since ? tc::negate(*result) : *result);
}

// AddDurationToZonedDateTime (6.5.10).
value add_duration(context & cx, bool subtract, const zoned & self, std::span<value> a) {
    tc::duration_record d;
    if (!to_temporal_duration(cx, arg_at(a, 0), d)) { return value::undefined(); }
    if (subtract) { d = tc::negate(d); }
    value options;
    tc::overflow how{};
    if (!get_options_object(cx, arg_at(a, 1), options) || !get_overflow_option(cx, options, how)) {
        return value::undefined();
    }
    const auto ns =
        tc::add_zoned_datetime(self.epoch_ns, self.zone, tc::to_internal_duration(d), how);
    if (!ns) {
        (void)range_error(cx, "the result is outside the representable range");
        return value::undefined();
    }
    return make_zoned(cx, zoned{*ns, self.zone, self.calendar});
}

value zoned_with(context & cx, std::span<value> a) {
    const auto self = this_zoned(cx, "Temporal.ZonedDateTime.prototype.with");
    if (!self) { return value::undefined(); }
    bool partial_object = false;
    if (!is_partial_temporal_object(cx, arg_at(a, 0), partial_object)) {
        return value::undefined();
    }
    if (!partial_object) {
        (void)type_error(cx, "with() needs an object of date and time properties");
        return value::undefined();
    }
    const tc::i128 offset_ns = tc::get_offset_nanoseconds_for(self->zone, self->epoch_ns);
    const tc::iso_datetime dt = local(*self);
    calendar_fields fields = iso_date_to_fields(dt.date, date_type::date);
    fields.hour = dt.time.hour;
    fields.minute = dt.time.minute;
    fields.second = dt.time.second;
    fields.millisecond = dt.time.millisecond;
    fields.microsecond = dt.time.microsecond;
    fields.nanosecond = dt.time.nanosecond;
    fields.offset = tc::format_utc_offset_nanoseconds(offset_ns);
    calendar_fields partial;
    if (!prepare_calendar_fields(cx, arg_at(a, 0), date_fields | time_fields | f_offset, 0, true,
                                 partial)) {
        return value::undefined();
    }
    fields = calendar_merge_fields(fields, partial);
    value options;
    tc::disambiguation how{};
    tc::offset_option option{};
    tc::overflow overflow_how{};
    tc::iso_datetime result;
    if (!get_options_object(cx, arg_at(a, 1), options) ||
        !get_disambiguation_option(cx, options, how) ||
        !get_offset_option(cx, options, tc::offset_option::prefer, option) ||
        !get_overflow_option(cx, options, overflow_how) ||
        !interpret_date_time_fields(cx, fields, overflow_how, result)) {
        return value::undefined();
    }
    const auto ns =
        tc::interpret_iso_datetime_offset(result.date, result.time, tc::offset_behaviour::option,
                                          *tc::parse_utc_offset(*fields.offset), self->zone, how,
                                          option, tc::match_behaviour::exactly);
    if (!ns) {
        (void)range_error(cx, "no exact time for that date, time and offset");
        return value::undefined();
    }
    return make_zoned(cx, zoned{*ns, self->zone, self->calendar});
}

value zoned_round(context & cx, std::span<value> a) {
    const auto self = this_zoned(cx, "Temporal.ZonedDateTime.prototype.round");
    if (!self) { return value::undefined(); }
    std::int64_t increment = 1;
    tc::rounding_mode mode = tc::rounding_mode::half_expand;
    unit_value smallest;
    if (!get_round_options(cx, arg_at(a, 0), increment, mode, smallest, {tc::unit::day})) {
        return value::undefined();
    }
    const bool day = smallest.u == tc::unit::day;
    if (!tc::valid_rounding_increment(increment,
                                      day ? 1 : tc::maximum_rounding_increment(smallest.u), day)) {
        (void)range_error(cx, "invalid roundingIncrement");
        return value::undefined();
    }
    if (smallest.u == tc::unit::nanosecond && increment == 1) { return make_zoned(cx, *self); }
    const tc::iso_datetime dt = local(*self);
    std::optional<tc::i128> ns;
    if (day) {
        const auto start = tc::get_start_of_day(self->zone, dt.date);
        const auto end = tc::get_start_of_day(self->zone, tc::add_days_to_iso_date(dt.date, 1));
        if (start && end) {
            ns = *start +
                 tc::round_number_to_increment(self->epoch_ns - *start, *end - *start, mode);
        }
    } else {
        const tc::iso_datetime rounded = tc::round_iso_datetime(dt, increment, smallest.u, mode);
        ns = tc::interpret_iso_datetime_offset(
            rounded.date, rounded.time, tc::offset_behaviour::option,
            tc::get_offset_nanoseconds_for(self->zone, self->epoch_ns), self->zone,
            tc::disambiguation::compatible, tc::offset_option::prefer,
            tc::match_behaviour::exactly);
    }
    if (!ns) {
        (void)range_error(cx, "the result is outside the representable range");
        return value::undefined();
    }
    return make_zoned(cx, zoned{*ns, self->zone, self->calendar});
}

value zoned_to_string_method(context & cx, std::span<value> a) {
    const auto self = this_zoned(cx, "Temporal.ZonedDateTime.prototype.toString");
    if (!self) { return value::undefined(); }
    value options;
    tc::show_calendar show{};
    int digits = tc::precision_auto;
    bool show_offset = true;
    tc::rounding_mode mode = tc::rounding_mode::trunc;
    unit_value smallest;
    tc::show_calendar show_zone{};
    if (!get_options_object(cx, arg_at(a, 0), options) ||
        !get_show_calendar_option(cx, options, show) ||
        !get_fractional_second_digits_option(cx, options, digits) ||
        !get_show_offset_option(cx, options, show_offset) ||
        !get_rounding_mode_option(cx, options, tc::rounding_mode::trunc, mode) ||
        !get_unit_option(cx, options, "smallestUnit", smallest) ||
        !get_show_time_zone_option(cx, options, show_zone) ||
        !validate_unit_value(cx, smallest, unit_group::time)) {
        return value::undefined();
    }
    if (smallest.present && smallest.u == tc::unit::hour) {
        (void)range_error(cx, "smallestUnit must be minutes or smaller");
        return value::undefined();
    }
    const precision_record precision = seconds_string_precision(smallest, digits);
    return str(cx, zoned_to_string(*self, precision.precision, show, show_zone, show_offset,
                                   precision.increment, precision.u, mode));
}

} // namespace

void install_zoned_date_time(context & cx, object_object & ns) {
    const class_parts parts = make_class(
        cx, ns, kind::zoned_date_time, "ZonedDateTime", 2, [](context & c, std::span<value> a) {
            const value self = c.current_this();
            if (!detail::constructing_this(self)) {
                (void)type_error(c, "Constructor Temporal.ZonedDateTime requires 'new'");
                return value::undefined();
            }
            tc::i128 epoch = 0;
            if (!to_i128_bigint(c, arg_at(a, 0), epoch)) { return value::undefined(); }
            if (!tc::is_valid_epoch_nanoseconds(epoch)) {
                (void)range_error(c, "epoch nanoseconds outside the representable range");
                return value::undefined();
            }
            const value zone_value = arg_at(a, 1);
            if (!zone_value.is_string()) {
                (void)type_error(c, "the time zone must be a string");
                return value::undefined();
            }
            const auto parsed = tc::parse_time_zone_identifier(c.to_string(zone_value));
            const auto zone = parsed ? tc::canonical_time_zone(*parsed) : std::nullopt;
            if (!zone) {
                (void)range_error(c, "invalid or unsupported time zone");
                return value::undefined();
            }
            const value calendar_value = arg_at(a, 2);
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
            return make_zoned(c, zoned{epoch, *zone, calendar}, self);
        });
    native_object * ctor = parts.ctor;
    object_object * proto = parts.proto;

    detail::method(cx, ctor, "from", 1, [](context & c, std::span<value> a) {
        zoned z;
        if (!to_temporal_zoned(c, arg_at(a, 0), arg_at(a, 1), z)) { return value::undefined(); }
        return make_zoned(c, z);
    });
    detail::method(cx, ctor, "compare", 2, [](context & c, std::span<value> a) {
        zoned one;
        zoned two;
        if (!to_temporal_zoned(c, arg_at(a, 0), value::undefined(), one) ||
            !to_temporal_zoned(c, arg_at(a, 1), value::undefined(), two)) {
            return value::undefined();
        }
        return value::number(one.epoch_ns > two.epoch_ns ? 1
                                                         : (one.epoch_ns < two.epoch_ns ? -1 : 0));
    });

    install_calendar_getters(cx, proto, kind::zoned_date_time, all_calendar_getters);
    getter(cx, proto, "timeZoneId", [](context & c, std::span<value>) {
        const auto z = this_zoned(c, "get Temporal.ZonedDateTime.prototype.timeZoneId");
        return z ? c.string(z->zone) : value::undefined();
    });
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
            const auto z = this_zoned(c, "get Temporal.ZonedDateTime.prototype." + name);
            return z ? value::number(local(*z).time.*member) : value::undefined();
        });
    }
    getter(cx, proto, "epochMilliseconds", [](context & c, std::span<value>) {
        const auto z = this_zoned(c, "get Temporal.ZonedDateTime.prototype.epochMilliseconds");
        if (!z) { return value::undefined(); }
        tc::i128 ms = z->epoch_ns / 1'000'000;
        if (z->epoch_ns % 1'000'000 < 0) { ms -= 1; }
        return value::number(static_cast<double>(ms) + 0.0);
    });
    getter(cx, proto, "epochNanoseconds", [](context & c, std::span<value>) {
        const auto z = this_zoned(c, "get Temporal.ZonedDateTime.prototype.epochNanoseconds");
        return z ? bigint_value(c, z->epoch_ns) : value::undefined();
    });
    getter(cx, proto, "hoursInDay", [](context & c, std::span<value>) {
        const auto z = this_zoned(c, "get Temporal.ZonedDateTime.prototype.hoursInDay");
        if (!z) { return value::undefined(); }
        const tc::iso_date today = local(*z).date;
        const auto start = tc::get_start_of_day(z->zone, today);
        const auto end = tc::get_start_of_day(z->zone, tc::add_days_to_iso_date(today, 1));
        if (!start || !end) {
            (void)range_error(c, "the day is outside the representable range");
            return value::undefined();
        }
        return value::number(tc::total_time_duration(*end - *start, tc::unit::hour));
    });
    getter(cx, proto, "offsetNanoseconds", [](context & c, std::span<value>) {
        const auto z = this_zoned(c, "get Temporal.ZonedDateTime.prototype.offsetNanoseconds");
        return z ? value::number(
                       static_cast<double>(tc::get_offset_nanoseconds_for(z->zone, z->epoch_ns)))
                 : value::undefined();
    });
    getter(cx, proto, "offset", [](context & c, std::span<value>) {
        const auto z = this_zoned(c, "get Temporal.ZonedDateTime.prototype.offset");
        return z ? c.string(tc::format_utc_offset_nanoseconds(
                       tc::get_offset_nanoseconds_for(z->zone, z->epoch_ns)))
                 : value::undefined();
    });

    detail::method(cx, proto, "with", 1, zoned_with);
    detail::method(cx, proto, "withPlainTime", 0, [](context & c, std::span<value> a) {
        const auto z = this_zoned(c, "Temporal.ZonedDateTime.prototype.withPlainTime");
        if (!z) { return value::undefined(); }
        const tc::iso_datetime dt = local(*z);
        std::optional<tc::i128> ns;
        if (arg_at(a, 0).is_undefined()) {
            ns = tc::get_start_of_day(z->zone, dt.date);
        } else {
            tc::time_record time;
            if (!to_temporal_time(c, arg_at(a, 0), value::undefined(), time)) {
                return value::undefined();
            }
            ns = tc::get_epoch_nanoseconds_for(z->zone, tc::iso_datetime{dt.date, time},
                                               tc::disambiguation::compatible);
        }
        if (!ns) {
            (void)range_error(c, "the result is outside the representable range");
            return value::undefined();
        }
        return make_zoned(c, zoned{*ns, z->zone, z->calendar});
    });
    detail::method(cx, proto, "withTimeZone", 1, [](context & c, std::span<value> a) {
        const auto z = this_zoned(c, "Temporal.ZonedDateTime.prototype.withTimeZone");
        if (!z) { return value::undefined(); }
        std::string zone;
        if (!to_time_zone_identifier(c, arg_at(a, 0), zone)) { return value::undefined(); }
        return make_zoned(c, zoned{z->epoch_ns, zone, z->calendar});
    });
    detail::method(cx, proto, "withCalendar", 1, [](context & c, std::span<value> a) {
        const auto z = this_zoned(c, "Temporal.ZonedDateTime.prototype.withCalendar");
        if (!z) { return value::undefined(); }
        std::string calendar;
        if (!to_calendar_identifier(c, arg_at(a, 0), calendar)) { return value::undefined(); }
        return make_zoned(c, zoned{z->epoch_ns, z->zone, calendar});
    });
    detail::method(cx, proto, "add", 1, [](context & c, std::span<value> a) {
        const auto z = this_zoned(c, "Temporal.ZonedDateTime.prototype.add");
        return z ? add_duration(c, false, *z, a) : value::undefined();
    });
    detail::method(cx, proto, "subtract", 1, [](context & c, std::span<value> a) {
        const auto z = this_zoned(c, "Temporal.ZonedDateTime.prototype.subtract");
        return z ? add_duration(c, true, *z, a) : value::undefined();
    });
    detail::method(cx, proto, "until", 1, [](context & c, std::span<value> a) {
        const auto z = this_zoned(c, "Temporal.ZonedDateTime.prototype.until");
        return z ? difference(c, false, *z, a) : value::undefined();
    });
    detail::method(cx, proto, "since", 1, [](context & c, std::span<value> a) {
        const auto z = this_zoned(c, "Temporal.ZonedDateTime.prototype.since");
        return z ? difference(c, true, *z, a) : value::undefined();
    });
    detail::method(cx, proto, "round", 1, zoned_round);
    detail::method(cx, proto, "equals", 1, [](context & c, std::span<value> a) {
        const auto z = this_zoned(c, "Temporal.ZonedDateTime.prototype.equals");
        if (!z) { return value::undefined(); }
        zoned other;
        if (!to_temporal_zoned(c, arg_at(a, 0), value::undefined(), other)) {
            return value::undefined();
        }
        return value::boolean(z->epoch_ns == other.epoch_ns &&
                              tc::time_zone_equals(z->zone, other.zone) &&
                              z->calendar == other.calendar);
    });
    detail::method(cx, proto, "toString", 0, zoned_to_string_method);
    for (const char * name : {"toLocaleString", "toJSON"}) {
        const std::string method = std::string{"Temporal.ZonedDateTime.prototype."} + name;
        detail::method(cx, proto, name, 0, [method](context & c, std::span<value>) {
            const auto z = this_zoned(c, method);
            return z ? str(c, zoned_to_string(*z, tc::precision_auto, tc::show_calendar::auto_,
                                              tc::show_calendar::auto_, true))
                     : value::undefined();
        });
    }
    detail::method(cx, proto, "valueOf", 0, [](context & c, std::span<value>) {
        (void)type_error(c, "Temporal.ZonedDateTime has no primitive value; use compare()");
        return value::undefined();
    });
    detail::method(cx, proto, "startOfDay", 0, [](context & c, std::span<value>) {
        const auto z = this_zoned(c, "Temporal.ZonedDateTime.prototype.startOfDay");
        if (!z) { return value::undefined(); }
        const auto ns = tc::get_start_of_day(z->zone, local(*z).date);
        if (!ns) {
            (void)range_error(c, "the result is outside the representable range");
            return value::undefined();
        }
        return make_zoned(c, zoned{*ns, z->zone, z->calendar});
    });
    detail::method(cx, proto, "getTimeZoneTransition", 1, [](context & c, std::span<value> a) {
        const auto z = this_zoned(c, "Temporal.ZonedDateTime.prototype.getTimeZoneTransition");
        if (!z) { return value::undefined(); }
        value options;
        std::optional<std::string> direction;
        if (!options_or_string(c, arg_at(a, 0), "direction", options) ||
            !get_string_option(c, options, "direction", {"next", "previous"}, std::nullopt,
                               direction, true)) {
            return value::undefined();
        }
        // A fixed-offset zone, and UTC, never change offset.
        return value::null();
    });
    detail::method(cx, proto, "toInstant", 0, [](context & c, std::span<value>) {
        const auto z = this_zoned(c, "Temporal.ZonedDateTime.prototype.toInstant");
        return z ? make_instant(c, z->epoch_ns) : value::undefined();
    });
    detail::method(cx, proto, "toPlainDate", 0, [](context & c, std::span<value>) {
        const auto z = this_zoned(c, "Temporal.ZonedDateTime.prototype.toPlainDate");
        return z ? make_date(c, plain_date{local(*z).date, z->calendar}) : value::undefined();
    });
    detail::method(cx, proto, "toPlainTime", 0, [](context & c, std::span<value>) {
        const auto z = this_zoned(c, "Temporal.ZonedDateTime.prototype.toPlainTime");
        return z ? make_time(c, local(*z).time) : value::undefined();
    });
    detail::method(cx, proto, "toPlainDateTime", 0, [](context & c, std::span<value>) {
        const auto z = this_zoned(c, "Temporal.ZonedDateTime.prototype.toPlainDateTime");
        return z ? make_date_time(c, plain_date_time{local(*z), z->calendar}) : value::undefined();
    });
}

} // namespace ctbrowser::script::builtins_detail::temporal_js
