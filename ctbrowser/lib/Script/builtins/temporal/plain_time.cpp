//===- plain_time.cpp - Temporal.PlainTime ----------------------*- C++ -*-===//
//
// Temporal.PlainTime (Temporal proposal section 4): a wall-clock time with no
// date and no time zone.
//
//===----------------------------------------------------------------------===//

#include "objects.hpp"

namespace ctbrowser::script::builtins_detail::temporal_js {

namespace {

value difference(context & cx, bool since, const tc::time_record & time, std::span<value> a) {
    tc::time_record other;
    value options;
    difference_settings settings;
    if (!to_temporal_time(cx, arg_at(a, 0), value::undefined(), other) ||
        !get_options_object(cx, arg_at(a, 1), options) ||
        !get_difference_settings(cx, since, options, unit_group::time, {}, tc::unit::nanosecond,
                                 tc::unit::hour, settings)) {
        return value::undefined();
    }
    const tc::i128 d = tc::round_number_to_increment(
        tc::difference_time(time, other),
        tc::unit_length_ns(settings.smallest) * settings.increment, settings.mode);
    auto result =
        tc::temporal_duration_from_internal(tc::internal_duration{{}, d}, settings.largest);
    if (!result) {
        (void)range_error(cx, "invalid duration");
        return value::undefined();
    }
    return make_duration(cx, since ? tc::negate(*result) : *result);
}

value add_duration(context & cx, bool subtract, const tc::time_record & time, value like) {
    tc::duration_record d;
    if (!to_temporal_duration(cx, like, d)) { return value::undefined(); }
    if (subtract) { d = tc::negate(d); }
    tc::time_record result = tc::add_time(time, tc::to_internal_duration(d).time);
    result.days = 0;
    return make_time(cx, result);
}

value time_round(context & cx, std::span<value> a) {
    const auto time = this_time(cx, "Temporal.PlainTime.prototype.round");
    if (!time) { return value::undefined(); }
    std::int64_t increment = 1;
    tc::rounding_mode mode = tc::rounding_mode::half_expand;
    unit_value smallest;
    if (!get_round_options(cx, arg_at(a, 0), increment, mode, smallest)) {
        return value::undefined();
    }
    if (!tc::valid_rounding_increment(increment, tc::maximum_rounding_increment(smallest.u),
                                      false)) {
        (void)range_error(cx, "invalid roundingIncrement");
        return value::undefined();
    }
    tc::time_record result = tc::round_time(*time, increment, smallest.u, mode);
    result.days = 0;
    return make_time(cx, result);
}

value time_to_string(context & cx, std::span<value> a) {
    const auto time = this_time(cx, "Temporal.PlainTime.prototype.toString");
    if (!time) { return value::undefined(); }
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
    if (smallest.present && smallest.u == tc::unit::hour) {
        (void)range_error(cx, "smallestUnit must be minutes or smaller");
        return value::undefined();
    }
    const precision_record precision = seconds_string_precision(smallest, digits);
    const tc::time_record rounded = tc::round_time(*time, precision.increment, precision.u, mode);
    return str(cx, tc::time_record_to_string(rounded, precision.precision));
}

value time_with(context & cx, std::span<value> a) {
    const auto time = this_time(cx, "Temporal.PlainTime.prototype.with");
    if (!time) { return value::undefined(); }
    bool partial_object = false;
    if (!is_partial_temporal_object(cx, arg_at(a, 0), partial_object)) {
        return value::undefined();
    }
    if (!partial_object) {
        (void)type_error(cx, "with() needs an object of time properties");
        return value::undefined();
    }
    double fields[6];
    if (!to_time_record(cx, arg_at(a, 0), true, fields)) { return value::undefined(); }
    const double existing[6] = {double(time->hour),        double(time->minute),
                                double(time->second),      double(time->millisecond),
                                double(time->microsecond), double(time->nanosecond)};
    for (std::size_t i = 0; i < 6; ++i) {
        if (std::isnan(fields[i])) { fields[i] = existing[i]; }
    }
    value options;
    tc::overflow how{};
    if (!get_options_object(cx, arg_at(a, 1), options) || !get_overflow_option(cx, options, how)) {
        return value::undefined();
    }
    const auto result =
        tc::regulate_time(fields[0], fields[1], fields[2], fields[3], fields[4], fields[5], how);
    if (!result) {
        (void)range_error(cx, "invalid time");
        return value::undefined();
    }
    return make_time(cx, *result);
}

} // namespace

void install_plain_time(context & cx, object_object & ns) {
    const class_parts parts =
        make_class(cx, ns, kind::plain_time, "PlainTime", 0, [](context & c, std::span<value> a) {
            const value self = c.current_this();
            if (!detail::constructing_this(self)) {
                (void)type_error(c, "Constructor Temporal.PlainTime requires 'new'");
                return value::undefined();
            }
            double fields[6] = {};
            for (std::size_t i = 0; i < 6; ++i) {
                const value v = arg_at(a, i);
                if (!v.is_undefined() && !to_integer_with_truncation(c, v, fields[i])) {
                    return value::undefined();
                }
            }
            const auto time = tc::regulate_time(fields[0], fields[1], fields[2], fields[3],
                                                fields[4], fields[5], tc::overflow::reject);
            if (!time) {
                (void)range_error(c, "invalid time");
                return value::undefined();
            }
            return make_time(c, *time, self);
        });
    native_object * ctor = parts.ctor;
    object_object * proto = parts.proto;

    detail::method(cx, ctor, "from", 1, [](context & c, std::span<value> a) {
        tc::time_record t;
        if (!to_temporal_time(c, arg_at(a, 0), arg_at(a, 1), t)) { return value::undefined(); }
        return make_time(c, t);
    });
    detail::method(cx, ctor, "compare", 2, [](context & c, std::span<value> a) {
        tc::time_record one;
        tc::time_record two;
        if (!to_temporal_time(c, arg_at(a, 0), value::undefined(), one) ||
            !to_temporal_time(c, arg_at(a, 1), value::undefined(), two)) {
            return value::undefined();
        }
        return value::number(tc::compare_time(one, two));
    });

    struct field_getter {
        const char * name;
        int tc::time_record::* field;
    };
    static constexpr field_getter fields[] = {
        {"hour", &tc::time_record::hour},
        {"minute", &tc::time_record::minute},
        {"second", &tc::time_record::second},
        {"millisecond", &tc::time_record::millisecond},
        {"microsecond", &tc::time_record::microsecond},
        {"nanosecond", &tc::time_record::nanosecond},
    };
    for (const field_getter & f : fields) {
        const auto member = f.field;
        const std::string name = f.name;
        getter(cx, proto, name, [member, name](context & c, std::span<value>) {
            const auto t = this_time(c, "get Temporal.PlainTime.prototype." + name);
            return t ? value::number((*t).*member) : value::undefined();
        });
    }

    detail::method(cx, proto, "add", 1, [](context & c, std::span<value> a) {
        const auto t = this_time(c, "Temporal.PlainTime.prototype.add");
        return t ? add_duration(c, false, *t, arg_at(a, 0)) : value::undefined();
    });
    detail::method(cx, proto, "subtract", 1, [](context & c, std::span<value> a) {
        const auto t = this_time(c, "Temporal.PlainTime.prototype.subtract");
        return t ? add_duration(c, true, *t, arg_at(a, 0)) : value::undefined();
    });
    detail::method(cx, proto, "with", 1, time_with);
    detail::method(cx, proto, "until", 1, [](context & c, std::span<value> a) {
        const auto t = this_time(c, "Temporal.PlainTime.prototype.until");
        return t ? difference(c, false, *t, a) : value::undefined();
    });
    detail::method(cx, proto, "since", 1, [](context & c, std::span<value> a) {
        const auto t = this_time(c, "Temporal.PlainTime.prototype.since");
        return t ? difference(c, true, *t, a) : value::undefined();
    });
    detail::method(cx, proto, "round", 1, time_round);
    detail::method(cx, proto, "equals", 1, [](context & c, std::span<value> a) {
        const auto t = this_time(c, "Temporal.PlainTime.prototype.equals");
        if (!t) { return value::undefined(); }
        tc::time_record other;
        if (!to_temporal_time(c, arg_at(a, 0), value::undefined(), other)) {
            return value::undefined();
        }
        return value::boolean(tc::compare_time(*t, other) == 0);
    });
    detail::method(cx, proto, "toString", 0, time_to_string);
    detail::method(cx, proto, "toLocaleString", 0, [](context & c, std::span<value>) {
        const auto t = this_time(c, "Temporal.PlainTime.prototype.toLocaleString");
        return t ? str(c, tc::time_record_to_string(*t, tc::precision_auto)) : value::undefined();
    });
    detail::method(cx, proto, "toJSON", 0, [](context & c, std::span<value>) {
        const auto t = this_time(c, "Temporal.PlainTime.prototype.toJSON");
        return t ? str(c, tc::time_record_to_string(*t, tc::precision_auto)) : value::undefined();
    });
    detail::method(cx, proto, "valueOf", 0, [](context & c, std::span<value>) {
        (void)type_error(c, "Temporal.PlainTime has no primitive value; use compare()");
        return value::undefined();
    });
}

} // namespace ctbrowser::script::builtins_detail::temporal_js
