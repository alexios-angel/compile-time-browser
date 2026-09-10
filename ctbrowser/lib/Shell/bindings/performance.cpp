// dom_bindings - `performance`, and the two Performance Timeline interfaces a
// page feature-detects for paint timing.
//
// https://w3c.github.io/performance-timeline/ and https://w3c.github.io/paint-timing/.
// `now` was the whole object until 2026-09-10; html/dom/render-blocking asks
// `assert_implements(window.PerformancePaintTiming)` and then polls
// `performance.getEntriesByType('paint')` for the first frame, which is what
// made both interfaces exist. The entries themselves are plain data on the
// bindings (`performance_entries_`); the browser records them from its first
// frame and the objects a page sees are built here, on demand.
//
// ponytail: no PerformanceObserver. Nothing in the corpus observes 'paint'
// through one - LoadObserver uses it only for resource timing, which this
// engine does not record. Add it when a page asks.

#include <ctbrowser/shell/bindings.hpp>

namespace ctbrowser::shell {

script::object_object * dom_bindings::install_performance(context & cx) {
    // PerformanceEntry.prototype, and PerformancePaintTiming.prototype behind it.
    auto * entry_proto = static_cast<script::object_object *>(cx.make_object().as_heap());
    entry_proto->set(
        "toJSON", value::object(cx.allocate<script::native_object>("toJSON", [](context & c,
                                                                                std::span<value>) {
            auto * out = static_cast<script::object_object *>(c.make_object().as_heap());
            const value self = c.current_this();
            for (const char * key : {"name", "entryType", "startTime", "duration"}) {
                out->set(key, c.lookup_property(self, key));
            }
            return value::object(out);
        })));
    auto * paint_proto = static_cast<script::object_object *>(cx.make_object().as_heap());
    paint_proto->prototype = value::object(entry_proto);
    for (const auto & [name, proto] : {std::pair{"PerformanceEntry", entry_proto},
                                       std::pair{"PerformancePaintTiming", paint_proto}}) {
        auto * ctor =
            cx.allocate<script::native_object>(name, [name](context & c, std::span<value>) {
                c.throw_error("TypeError", std::string{name} + " has no constructor");
                return value::undefined();
            });
        // The attributes every interface object's `prototype` and every
        // prototype's `constructor` have (WebIDL 3.6), as events/interfaces.cpp.
        ctor->define("prototype", value::object(proto), script::attr_none);
        proto->define("constructor", value::object(ctor), script::attr_builtin);
        cx.define_global(name, value::object(ctor));
    }
    const value paint_prototype = value::object(paint_proto);

    // `getEntries()`, `getEntriesByType(type)`, `getEntriesByName(name, type?)`:
    // one filter, three spellings. Every entry is a paint entry today, so the
    // prototype is one value; a second entry type is a second prototype here.
    const auto entries = [this, paint_prototype](context & c, std::string_view name,
                                                 std::string_view type) {
        value list = c.make_array();
        auto * items = static_cast<script::array_object *>(list.as_heap());
        for (const performance_entry & entry : performance_entries_) {
            if (!name.empty() && entry.name != name) { continue; }
            if (!type.empty() && entry.type != type) { continue; }
            auto * object = static_cast<script::object_object *>(c.make_object().as_heap());
            object->prototype = paint_prototype;
            object->set("name", c.string(entry.name));
            object->set("entryType", c.string(entry.type));
            object->set("startTime", value::number(entry.start_ms));
            object->set("duration", value::number(0));
            items->items.push_back(value::object(object));
        }
        return list;
    };
    auto * performance = static_cast<script::object_object *>(cx.make_object().as_heap());
    const auto method = [&](const char * name, auto fn) {
        auto * native = cx.allocate<script::native_object>(name, std::move(fn));
        // The prototype lives in a C++ capture the collector cannot see, and
        // the constructor global that also holds it can be deleted by a page.
        native->retained.push_back(paint_prototype);
        performance->set(name, value::object(native));
    };
    performance->set(
        "now", value::object(cx.allocate<script::native_object>(
                   "now", [this](context &, std::span<value>) { return value::number(now_ms_); })));
    performance->set("timeOrigin", value::number(0));
    method("getEntries", [entries](context & c, std::span<value>) { return entries(c, "", ""); });
    method("getEntriesByType", [entries](context & c, std::span<value> a) {
        return entries(c, "", arg_string(c, a, 0));
    });
    method("getEntriesByName", [entries](context & c, std::span<value> a) {
        return entries(c, arg_string(c, a, 0), a.size() > 1 ? arg_string(c, a, 1) : "");
    });
    return performance;
}

// ponytail: both marks at the first frame. `first-contentful-paint` should wait
// for a frame with text or an image in it; every page this engine draws has
// been one so far. Split them when a blank-then-content page needs the gap.
void dom_bindings::record_first_paint() {
    if (!performance_entries_.empty()) { return; }
    performance_entries_.push_back({"first-paint", "paint", now_ms_});
    performance_entries_.push_back({"first-contentful-paint", "paint", now_ms_});
}

} // namespace ctbrowser::shell
