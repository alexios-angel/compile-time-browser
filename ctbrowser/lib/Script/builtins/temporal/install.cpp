// ctbrowser.script builtins - the Temporal namespace object (1), and the
// classes on it. See core.hpp for what this engine's Temporal covers: the
// ISO 8601 calendar, and the UTC and fixed-offset time zones.

#include "objects.hpp"

namespace ctbrowser::script::builtins_detail {

void install_temporal(context & cx) {
    object_object * ns = detail::new_table(cx);
    ns->define(CTBROWSER_SYMBOL_KEY("toStringTag"), cx.string("Temporal"), attr_configurable);
    temporal_js::install_now(cx, *ns);
    temporal_js::install_instant(cx, *ns);
    temporal_js::install_plain_date_time(cx, *ns);
    temporal_js::install_plain_date(cx, *ns);
    temporal_js::install_plain_time(cx, *ns);
    temporal_js::install_plain_year_month(cx, *ns);
    temporal_js::install_plain_month_day(cx, *ns);
    temporal_js::install_zoned_date_time(cx, *ns);
    temporal_js::install_duration(cx, *ns);
    cx.define_global("Temporal", value::object(ns));

    // Date.prototype.toTemporalInstant (14.7.1): thisTimeValue, a RangeError
    // for an invalid date, and the milliseconds as nanoseconds. The time value
    // is Date's `__ms` slot (date.cpp).
    const value date_ctor = cx.global("Date");
    if (object_object * date_proto =
            date_ctor.is_object_like()
                ? static_cast<object_object *>(cx.lookup_property(date_ctor, "prototype").as_heap())
                : nullptr) {
        detail::method(cx, date_proto, "toTemporalInstant", 0, [](context & c, std::span<value>) {
            const value self = c.current_this();
            const value * held =
                self.is_object()
                    ? static_cast<object_object *>(self.as_heap())->find(date_value_key)
                    : nullptr;
            if (held == nullptr) {
                c.throw_error("TypeError", "Date.prototype.toTemporalInstant called on a non-Date");
                return value::undefined();
            }
            const double t = held->as_number();
            if (!std::isfinite(t)) {
                c.throw_error("RangeError", "Invalid Date");
                return value::undefined();
            }
            return temporal_js::make_instant(c, static_cast<temporal_js::i128>(t) * 1'000'000);
        });
    }
}

} // namespace ctbrowser::script::builtins_detail
