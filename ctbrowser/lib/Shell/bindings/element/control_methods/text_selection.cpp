#include "helpers.hpp"

namespace ctbrowser::shell {
using namespace detail;
using namespace input_types;

void dom_bindings::install_control_text_selection(context & cx) {
    const control_helpers helpers{this};
    // --- the selection API, HTML 4.10.19.5 --------------------------------------------
    //
    // On a textarea and on an input of a text-like type. The store keeps the
    // caret and the anchor as byte offsets into the UTF-8 value; the API
    // speaks UTF-16 code units, and `direction` is a slot on the wrapper.
    const auto selection_applies = [helpers](const read_txn & txn, dom_bindings * b, node_id id) {
        if (helpers.is(txn, id, "textarea")) { return true; }
        if (!helpers.is(txn, id, "input")) { return false; }
        const std::string type = helpers.input_type(txn, b, id);
        return type == "text" || type == "search" || type == "url" || type == "tel" ||
               type == "password";
    };
    struct text_selection {
        std::size_t start = 0; // code units
        std::size_t end = 0;
        std::string direction = "none";
    };
    const auto selection_of = [helpers](context & c, const read_txn & txn, dom_bindings * b,
                                        node_id id) {
        const control_state & held = b->forms_->state_of(txn, *b->atoms_, id);
        text_selection out;
        const std::size_t lo = std::min(held.caret, held.selection);
        const std::size_t hi = std::max(held.caret, held.selection);
        out.start = units_before(held.value, lo);
        out.end = units_before(held.value, hi);
        const value direction = helpers.slot_of(c, b, id, "__selectionDirection");
        if (direction.is_string()) { out.direction = c.to_string(direction); }
        if (out.start == out.end) {
            out.direction = out.direction == "backward"
                                ? "backward"
                                : (out.direction == "forward" ? "forward" : "none");
        }
        return out;
    };
    // "Set the selection range", with the `select` event queued when it moved.
    const auto set_selection = [helpers, selection_of](context & c, dom_bindings * b, node_id id,
                                                       double start, double end,
                                                       std::string direction) {
        bool changed = false;
        {
            const auto txn = b->doc_->read();
            control_state & held = b->forms_->state_of(txn, *b->atoms_, id);
            const std::size_t length = units_length_of(held.value);
            const auto clamp = [&](double x) {
                if (std::isnan(x) || x < 0) { return std::size_t{0}; }
                return std::min(static_cast<std::size_t>(x), length);
            };
            std::size_t s = clamp(start);
            std::size_t e = clamp(end);
            if (e < s) { s = e; }
            const text_selection before = selection_of(c, txn, b, id);
            if (direction != "forward" && direction != "backward") { direction = "none"; }
            const std::size_t s_bytes = bytes_before(held.value, s);
            const std::size_t e_bytes = bytes_before(held.value, e);
            if (direction == "backward") {
                held.caret = s_bytes;
                held.selection = e_bytes;
            } else {
                held.selection = s_bytes;
                held.caret = e_bytes;
            }
            helpers.set_slot(c, b, id, "__selectionDirection", c.string(direction));
            changed = before.start != s || before.end != e || before.direction != direction;
        }
        if (changed) {
            // Queued, as the specification says: a task, not a synchronous call.
            auto * task = c.allocate<script::native_object>(
                "select", [b, id, helpers](context & inner, std::span<value>) {
                    (void)helpers.fire(inner, b, id, "select", true, false, helpers.plain);
                    return value::undefined();
                });
            (void)b->add_timer(value::object(task), 0, false);
        }
    };
    for (const char * which : {"HTMLInputElement", "HTMLTextAreaElement"}) {
        const auto selection_accessor = [&](const char * name, auto read, auto write) {
            helpers.accessor(
                cx, which, name,
                [helpers, selection_applies, selection_of, read](context & c, std::span<value>) {
                    const auto where = helpers.at(c);
                    dom_bindings * b = where.first;
                    const node_id id = where.second;
                    if (!id) { return value::null(); }
                    const auto txn = b->doc_->read();
                    if (!selection_applies(txn, b, id)) { return value::null(); }
                    return read(c, selection_of(c, txn, b, id));
                },
                [this, helpers, selection_applies, selection_of, set_selection,
                 write](context & c, std::span<value> a) {
                    const auto where = helpers.at(c);
                    dom_bindings * b = where.first;
                    const node_id id = where.second;
                    if (!id) { return value::undefined(); }
                    text_selection current;
                    {
                        const auto txn = b->doc_->read();
                        if (!selection_applies(txn, b, id)) {
                            throw_dom_exception(c, "InvalidStateError",
                                                "the selection does not apply to this control");
                            return value::undefined();
                        }
                        current = selection_of(c, txn, b, id);
                    }
                    write(c, current, arg(a, 0));
                    set_selection(c, b, id, static_cast<double>(current.start),
                                  static_cast<double>(current.end), current.direction);
                    return value::undefined();
                });
        };
        selection_accessor(
            "selectionStart",
            [](context &, const text_selection & s) {
                return value::number(static_cast<double>(s.start));
            },
            [](context &, text_selection & s, value given) {
                if (given.is_null()) { return; }
                const double start = static_cast<double>(context::to_uint32(given));
                s.start = static_cast<std::size_t>(start);
                if (s.end < s.start) { s.end = s.start; }
            });
        selection_accessor(
            "selectionEnd",
            [](context &, const text_selection & s) {
                return value::number(static_cast<double>(s.end));
            },
            [](context &, text_selection & s, value given) {
                if (given.is_null()) { return; }
                s.end = static_cast<std::size_t>(context::to_uint32(given));
            });
        selection_accessor(
            "selectionDirection",
            [](context & c, const text_selection & s) { return c.string(s.direction); },
            [](context & c, text_selection & s, value given) {
                if (given.is_null()) { return; }
                s.direction = c.to_string(given);
            });
        helpers.operation(
            cx, which, "setSelectionRange", 2,
            [this, helpers, selection_applies, set_selection](context & c, std::span<value> a) {
                const auto where = helpers.at(c);
                dom_bindings * b = where.first;
                const node_id id = where.second;
                if (!id) { return value::undefined(); }
                {
                    const auto txn = b->doc_->read();
                    if (!selection_applies(txn, b, id)) {
                        throw_dom_exception(c, "InvalidStateError",
                                            "setSelectionRange does not apply to this control");
                        return value::undefined();
                    }
                }
                const value direction = arg(a, 2);
                set_selection(c, b, id, static_cast<double>(context::to_uint32(arg(a, 0))),
                              static_cast<double>(context::to_uint32(arg(a, 1))),
                              direction.is_undefined() ? std::string{"none"}
                                                       : c.to_string(direction));
                return value::undefined();
            });
        helpers.operation(
            cx, which, "select", 0,
            [helpers, selection_applies, set_selection](context & c, std::span<value>) {
                const auto where = helpers.at(c);
                dom_bindings * b = where.first;
                const node_id id = where.second;
                if (!id) { return value::undefined(); }
                std::size_t length = 0;
                {
                    const auto txn = b->doc_->read();
                    if (!selection_applies(txn, b, id)) { return value::undefined(); }
                    length = units_length_of(b->forms_->state_of(txn, *b->atoms_, id).value);
                }
                set_selection(c, b, id, 0, static_cast<double>(length), "none");
                return value::undefined();
            });
        // setRangeText(replacement, start?, end?, selectMode): replace the
        // code units [start, end) of the value and move the selection as the
        // mode says.
        helpers.operation(
            cx, which, "setRangeText", 1,
            [this, helpers, selection_applies, selection_of, set_selection](context & c,
                                                                            std::span<value> a) {
                const auto where = helpers.at(c);
                dom_bindings * b = where.first;
                const node_id id = where.second;
                if (!id) { return value::undefined(); }
                if (a.empty()) {
                    c.throw_error("TypeError", "setRangeText needs a replacement string");
                    return value::undefined();
                }
                const std::string replacement = c.to_string(a[0]);
                text_selection current;
                std::string text;
                {
                    const auto txn = b->doc_->read();
                    if (!selection_applies(txn, b, id)) {
                        throw_dom_exception(c, "InvalidStateError",
                                            "setRangeText does not apply to this control");
                        return value::undefined();
                    }
                    current = selection_of(c, txn, b, id);
                    text = b->forms_->state_of(txn, *b->atoms_, id).value;
                }
                const std::size_t length = units_length_of(text);
                std::size_t start = current.start;
                std::size_t end = current.end;
                std::string mode = "preserve";
                if (a.size() >= 3) {
                    start = std::min(static_cast<std::size_t>(context::to_uint32(a[1])), length);
                    end = std::min(static_cast<std::size_t>(context::to_uint32(a[2])), length);
                    if (start > end) {
                        throw_dom_exception(c, "IndexSizeError", "setRangeText: start is past end");
                        return value::undefined();
                    }
                    if (a.size() >= 4 && !a[3].is_undefined()) { mode = c.to_string(a[3]); }
                    if (mode != "select" && mode != "start" && mode != "end" &&
                        mode != "preserve") {
                        c.throw_error("TypeError", "setRangeText: unknown selectMode");
                        return value::undefined();
                    }
                } else if (a.size() == 2) {
                    c.throw_error("TypeError", "setRangeText: `end` is required with `start`");
                    return value::undefined();
                }
                const std::size_t start_bytes = bytes_before(text, start);
                const std::size_t end_bytes = bytes_before(text, end);
                text.replace(start_bytes, end_bytes - start_bytes, replacement);
                const std::size_t new_length = units_length_of(replacement);
                const std::size_t new_end = start + new_length;
                std::size_t s = current.start;
                std::size_t e = current.end;
                if (mode == "select") {
                    s = start;
                    e = new_end;
                } else if (mode == "start") {
                    s = e = start;
                } else if (mode == "end") {
                    s = e = new_end;
                } else {
                    const auto shift = [&](std::size_t x) {
                        if (x > end) { return x + new_length - (end - start); }
                        if (x > start) { return new_end; }
                        return x;
                    };
                    const std::size_t old_start = s;
                    const std::size_t old_end = e;
                    s = shift(old_start);
                    e = shift(old_end);
                    if (old_start > start && old_start < end) { s = start; }
                    if (old_end > start && old_end < end) { e = new_end; }
                }
                {
                    // The value, with the selection LEFT WHERE IT WAS: the
                    // replacement itself moves no boundary (HTML 4.10.19.7
                    // step 12 sets the range afterwards), so `select` fires
                    // only when that range differs from the one before the
                    // call - a second identical setRangeText fires nothing
                    // (select-event.html). store_value would have put the
                    // caret at the end and made every call a change.
                    const auto txn = b->doc_->read();
                    control_state & held = b->forms_->state_of(txn, *b->atoms_, id);
                    const std::size_t caret = std::min(held.caret, text.size());
                    const std::size_t anchor = std::min(held.selection, text.size());
                    held.value = std::move(text);
                    held.caret = caret;
                    held.selection = anchor;
                    held.value_edited = true;
                    b->wrote_to_control_ = true;
                }
                set_selection(c, b, id, static_cast<double>(s), static_cast<double>(e),
                              current.direction);
                b->mutated();
                return value::undefined();
            });
    }
}

} // namespace ctbrowser::shell
