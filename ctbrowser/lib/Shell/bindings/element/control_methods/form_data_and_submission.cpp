//===- form_data_and_submission.cpp - FormData and form submission --------===//
//
// FormData and the form's entry list (HTML 4.10.21.4), the SubmitEvent and
// FormDataEvent interfaces, and HTMLFormElement's submit(), requestSubmit()
// and reset(), including how a submission becomes a planned navigation.
//
//===----------------------------------------------------------------------===//

#include "helpers.hpp"

#include <ctbrowser/dom/encoding.hpp>

namespace ctbrowser::shell {
using namespace detail;
using namespace input_types;

namespace {
/// A form's "constructing entry list" and "firing submission events" flags.
constexpr std::string_view constructing_slot = "__constructingEntryList";
constexpr std::string_view firing_slot = "__firingSubmissionEvents";

/// `window[name].prototype`, or undefined when no such interface is defined.
[[nodiscard]] value interface_prototype_of(context & c, std::string_view name) {
    if (!c.has_global(name)) { return value::undefined(); }
    const value ctor = c.global(name);
    return ctor.is_object_like() ? c.lookup_property(ctor, "prototype") : value::undefined();
}
} // namespace

void dom_bindings::install_control_form_data(context & cx) {
    const control_helpers helpers{this};
    // --- the entry list and FormData, HTML 4.10.21.4 and XHR's FormData -----------------
    //
    // A FormData is a list of (name, value) pairs held as a hidden JS array of
    // two-element arrays on the object (so the collector traces them), with the
    // prototype's methods over it. A value is a string, or the object it was
    // given when that is a Blob or a File.
    const auto entries_of = [](context & c) -> script::array_object * {
        const value self = c.current_this();
        if (!self.is_object()) { return nullptr; }
        const value * held =
            static_cast<script::object_object *>(self.as_heap())->find(entries_slot);
        if (held == nullptr || !held->is_array()) { return nullptr; }
        return static_cast<script::array_object *>(held->as_heap());
    };
    // "Create an entry" (XHR 5.1): the name, and a string value, converted to
    // scalar value strings (a lone surrogate becomes U+FFFD), whether the entry
    // comes from a form control or from FormData's append/set (USVString).
    const auto make_entry = [](context & c, const std::string & name, value held) {
        const value pair = c.make_array();
        auto & items = static_cast<script::array_object *>(pair.as_heap())->items;
        items.push_back(c.string(to_usv_string(name)));
        items.push_back(held.is_object_like() ? held : c.string(to_usv_string(c.to_string(held))));
        return pair;
    };
    const auto entry_name = [](context & c, const value & pair) {
        return c.to_string(static_cast<script::array_object *>(pair.as_heap())->items.at(0));
    };
    const auto entry_value = [](const value & pair) {
        return static_cast<script::array_object *>(pair.as_heap())->items.at(1);
    };
    // "Construct the entry list" for a form: the submittable elements'
    // successful controls, in tree order, then the `formdata` event. Returns
    // false (the specification's null) when the form is already constructing one
    // (its "constructing entry list" flag, a slot on the form): a `formdata`
    // listener that submits the form again must not recurse.
    // `encoding` is the one "_charset_" reports: the submission's picked
    // encoding, and UTF-8 for a FormData (which has none).
    const auto entry_list = [helpers, make_entry](context & c, dom_bindings * b, node_id form,
                                                  node_id submitter, value form_data,
                                                  std::string_view encoding = "UTF-8") -> bool {
        if (context::truthy(helpers.slot_of(c, b, form, constructing_slot))) { return false; }
        helpers.set_slot(c, b, form, constructing_slot, value::boolean(true));
        auto * list = static_cast<script::array_object *>(
            static_cast<script::object_object *>(form_data.as_heap())
                ->find(entries_slot)
                ->as_heap());
        const auto add = [&](const std::string & name, value held) {
            list->items.push_back(make_entry(c, name, held));
        };
        // The form's submittable elements (HTML 4.10.21.4 step 4), including
        // image buttons, which form.elements does not list.
        for (const node_id one : helpers.submittable_of_form(b, form)) {
            const auto txn = b->doc_->read();
            if (!txn.contains(one) || helpers.is_disabled(txn, b, one)) { continue; }
            // A form-associated custom element (steps 5.5-5.7) contributes its
            // submission value: nothing for null, a FormData's entries, else
            // the value under its name.
            if (const value face =
                    b->face_submission_value_ ? b->face_submission_value_(one) : value::undefined();
                !face.is_undefined()) {
                if (face.is_null()) { continue; }
                if (face.is_object()) {
                    if (const value * held = static_cast<script::object_object *>(face.as_heap())
                                                 ->find(entries_slot);
                        held != nullptr && held->is_array()) {
                        for (const value & pair :
                             static_cast<script::array_object *>(held->as_heap())->items) {
                            list->items.push_back(pair);
                        }
                        continue;
                    }
                }
                const std::string name = helpers.attribute_of(txn, b, one, "name");
                if (!name.empty()) { add(name, face); }
                continue;
            }
            if (!helpers.is_submittable(txn, one)) { continue; }
            bool in_datalist = false;
            for (node_id up = txn.parent(one); up; up = txn.parent(up)) {
                if (helpers.is(txn, up, "datalist")) { in_datalist = true; }
            }
            if (in_datalist) { continue; }
            const std::string_view local = txn.local_name(one);
            const std::string type =
                local == "input" ? helpers.input_type(txn, b, one) : std::string{};
            if (local == "object") { continue; }
            if ((local == "button" || type == "submit" || type == "reset" || type == "button") &&
                one != submitter) {
                continue;
            }
            if (local == "button" && one == submitter) {
                const std::string_view button_type =
                    txn.attribute_value(one, b->atoms_->intern("type"));
                if (ascii_lower_copy(button_type) == "reset" ||
                    ascii_lower_copy(button_type) == "button") {
                    continue;
                }
            }
            if ((type == "checkbox" || type == "radio") &&
                !b->forms_->state_of(txn, *b->atoms_, one).checked) {
                continue;
            }
            if (type == "image" && one != submitter) { continue; }
            const std::string name = helpers.attribute_of(txn, b, one, "name");
            if (type == "image") {
                const std::string prefix = name.empty() ? "" : name + ".";
                add(prefix + "x", value::number(0));
                add(prefix + "y", value::number(0));
                continue;
            }
            if (name.empty()) { continue; }
            if (local == "select") {
                std::vector<node_id> options;
                const std::vector<bool> flags = helpers.selected_flags(c, txn, b, one, options);
                for (std::size_t i = 0; i < options.size(); ++i) {
                    if (flags[i] && !helpers.is_disabled(txn, b, options[i])) {
                        add(name, c.string(helpers.option_value(txn, b, options[i])));
                    }
                }
                continue;
            }
            if (type == "checkbox" || type == "radio") {
                add(name, c.string(helpers.has_attribute(txn, b, one, "value")
                                       ? helpers.value_of(txn, b, one)
                                       : std::string{"on"}));
                continue;
            }
            if (type == "file") {
                // Each selected File under the name, or, with none selected, one
                // empty File of type application/octet-stream (HTML 4.10.21.4,
                // "construct the entry list").
                const std::vector<value> files = files_in(helpers.slot_of(c, b, one, "__files"));
                for (const value & file : files) { add(name, file); }
                if (files.empty()) {
                    const value file_ctor = c.global("File");
                    if (file_ctor.is_callable()) {
                        const value options = c.make_object();
                        c.store_property(options, "type", c.string("application/octet-stream"));
                        const value args[3] = {c.make_array(), c.string(""), options};
                        add(name, c.construct(file_ctor, args));
                    }
                }
                continue;
            }
            if (type == "hidden" && ascii_lower_copy(name) == "_charset_" &&
                !helpers.has_attribute(txn, b, one, "value")) {
                add(name, c.string(std::string{encoding}));
                continue;
            }
            if (local == "textarea") {
                // The wrapping transformation (HTML 4.10.11): with wrap=hard,
                // insert LF breaks so no line is longer than `cols` characters
                // (default 20). A line of exactly `cols` code points re-wraps
                // to itself.
                std::string text = helpers.value_of(txn, b, one);
                if (ascii_iequals(helpers.attribute_of(txn, b, one, "wrap"), "hard")) {
                    long long cols = 0;
                    if (!parse_html_integer(helpers.attribute_of(txn, b, one, "cols"), cols) ||
                        cols <= 0) {
                        cols = 20;
                    }
                    std::string wrapped;
                    long long column = 0;
                    for (std::size_t i = 0; i < text.size(); ++i) {
                        const auto byte = static_cast<unsigned char>(text[i]);
                        if (text[i] == '\n') {
                            column = 0;
                        } else if ((byte & 0xC0u) != 0x80u) {
                            if (column == cols) {
                                wrapped += '\n';
                                column = 0;
                            }
                            ++column;
                        }
                        wrapped += text[i];
                    }
                    text = std::move(wrapped);
                }
                add(name, c.string(text));
            } else {
                add(name, c.string(helpers.value_of(txn, b, one)));
            }
            // The dirname entry (HTML 4.10.18.2): where dirname applies (a
            // textarea and the hidden, text, search, tel, url, email, password
            // and submit states), the element's directionality under its name.
            const bool dirname_applies = local == "textarea" || type == "hidden" ||
                                         type == "text" || type == "search" || type == "tel" ||
                                         type == "url" || type == "email" || type == "password" ||
                                         type == "submit";
            if (dirname_applies && !helpers.attribute_of(txn, b, one, "dirname").empty()) {
                const bool rtl = b->selector_engine_ != nullptr &&
                                 b->selector_engine_->direction_is_rtl(txn, one);
                add(helpers.attribute_of(txn, b, one, "dirname"), c.string(rtl ? "rtl" : "ltr"));
            }
        }
        // A FormDataEvent when the interface exists (the events' installer
        // defines it), so `e instanceof FormDataEvent` holds.
        const value formdata_proto = interface_prototype_of(c, "FormDataEvent");
        (void)helpers.fire(c, b, form, "formdata", true, false,
                           [form_data, formdata_proto](script::object_object & event) {
                               if (formdata_proto.is_object()) { event.prototype = formdata_proto; }
                               event.set("formData", form_data);
                           });
        helpers.erase_slot(c, b, form, constructing_slot);
        return true;
    };
    if (!secondary_) {
        helpers.install_data_transfer(cx);
        auto * form_data_proto = cx.allocate<script::object_object>();
        const value form_data_proto_value = value::object(form_data_proto);
        form_data_proto->define(CTBROWSER_SYMBOL_KEY("toStringTag"), cx.string("FormData"),
                                script::attr_configurable);
        const auto fd_method = [&](const char * name, unsigned length, script::native_fn fn) {
            auto * made = cx.allocate<script::native_object>(name, std::move(fn));
            made->define("length", value::number(length), script::attr_configurable);
            form_data_proto->define(name, value::object(made), script::attr_builtin);
        };
        fd_method("append", 2, [entries_of, make_entry](context & c, std::span<value> a) {
            if (a.size() < 2) {
                c.throw_error("TypeError", "FormData.append needs a name and a value");
                return value::undefined();
            }
            if (auto * list = entries_of(c)) {
                list->items.push_back(make_entry(c, c.to_string(a[0]), a[1]));
            }
            return value::undefined();
        });
        fd_method("delete", 1, [entries_of, entry_name](context & c, std::span<value> a) {
            if (auto * list = entries_of(c)) {
                const std::string name = to_usv_string(arg_string(c, a, 0));
                std::erase_if(list->items,
                              [&](const value & pair) { return entry_name(c, pair) == name; });
            }
            return value::undefined();
        });
        fd_method("get", 1, [entries_of, entry_name, entry_value](context & c, std::span<value> a) {
            if (auto * list = entries_of(c)) {
                const std::string name = to_usv_string(arg_string(c, a, 0));
                for (const value & pair : list->items) {
                    if (entry_name(c, pair) == name) { return entry_value(pair); }
                }
            }
            return value::null();
        });
        fd_method("getAll", 1,
                  [entries_of, entry_name, entry_value](context & c, std::span<value> a) {
                      const value out = c.make_array();
                      if (auto * list = entries_of(c)) {
                          const std::string name = to_usv_string(arg_string(c, a, 0));
                          for (const value & pair : list->items) {
                              if (entry_name(c, pair) == name) {
                                  static_cast<script::array_object *>(out.as_heap())
                                      ->items.push_back(entry_value(pair));
                              }
                          }
                      }
                      return out;
                  });
        fd_method("has", 1, [entries_of, entry_name](context & c, std::span<value> a) {
            if (auto * list = entries_of(c)) {
                const std::string name = to_usv_string(arg_string(c, a, 0));
                for (const value & pair : list->items) {
                    if (entry_name(c, pair) == name) { return value::boolean(true); }
                }
            }
            return value::boolean(false);
        });
        fd_method("set", 2, [entries_of, entry_name, make_entry](context & c, std::span<value> a) {
            if (a.size() < 2) {
                c.throw_error("TypeError", "FormData.set needs a name and a value");
                return value::undefined();
            }
            auto * list = entries_of(c);
            if (list == nullptr) { return value::undefined(); }
            const std::string name = to_usv_string(c.to_string(a[0]));
            const value fresh = make_entry(c, name, a[1]);
            bool replaced = false;
            std::vector<value> kept;
            for (const value & pair : list->items) {
                if (entry_name(c, pair) != name) {
                    kept.push_back(pair);
                } else if (!replaced) {
                    kept.push_back(fresh);
                    replaced = true;
                }
            }
            if (!replaced) { kept.push_back(fresh); }
            list->items = std::move(kept);
            return value::undefined();
        });
        // The iterable declaration: a snapshot of the pairs as an array's
        // iterator - keys, values, entries, @@iterator and forEach.
        const auto iterate = [entries_of, entry_name, entry_value](context & c, int kind) {
            const value out = c.make_array();
            auto & items = static_cast<script::array_object *>(out.as_heap())->items;
            if (auto * list = entries_of(c)) {
                for (const value & pair : list->items) {
                    if (kind == 0) {
                        items.push_back(c.string(entry_name(c, pair)));
                    } else if (kind == 1) {
                        items.push_back(entry_value(pair));
                    } else {
                        const value copy = c.make_array();
                        auto & two = static_cast<script::array_object *>(copy.as_heap())->items;
                        two.push_back(c.string(entry_name(c, pair)));
                        two.push_back(entry_value(pair));
                        items.push_back(copy);
                    }
                }
            }
            const value fn = c.lookup_property(out, "values");
            return fn.is_callable() ? c.call(fn, {}, out) : out;
        };
        fd_method("keys", 0, [iterate](context & c, std::span<value>) { return iterate(c, 0); });
        fd_method("values", 0, [iterate](context & c, std::span<value>) { return iterate(c, 1); });
        fd_method("entries", 0, [iterate](context & c, std::span<value>) { return iterate(c, 2); });
        form_data_proto->define(CTBROWSER_SYMBOL_KEY("iterator"), *form_data_proto->find("entries"),
                                script::attr_builtin);
        fd_method(
            "forEach", 1, [entries_of, entry_name, entry_value](context & c, std::span<value> a) {
                const value fn = arg(a, 0);
                if (!fn.is_callable()) {
                    c.throw_error("TypeError", "FormData.forEach: the callback is not a function");
                    return value::undefined();
                }
                auto * list = entries_of(c);
                if (list == nullptr) { return value::undefined(); }
                const value self = c.current_this();
                for (std::size_t i = 0; i < list->items.size(); ++i) {
                    const value pair = list->items[i];
                    const value args[3] = {entry_value(pair), c.string(entry_name(c, pair)), self};
                    (void)c.call(fn, args, arg(a, 1));
                    if (c.throw_pending()) { break; }
                }
                return value::undefined();
            });
        auto * form_data_ctor = cx.allocate<script::native_object>(
            "FormData",
            [this, form_data_proto_value, entry_list, helpers](context & c,
                                                               std::span<value> a) -> value {
                const value self = c.current_this();
                if (!self.is_object()) {
                    c.throw_error("TypeError", "Failed to construct 'FormData': please use the "
                                               "'new' operator.");
                    return value::undefined();
                }
                auto * made = static_cast<script::object_object *>(self.as_heap());
                if (!made->prototype.is_object()) { made->prototype = form_data_proto_value; }
                made->define(std::string{entries_slot}, c.make_array(), script::attr_none);
                const value form_value = arg(a, 0);
                if (form_value.is_undefined()) { return self; }
                dom_bindings * b = owner_of(form_value);
                const node_id form = b == nullptr ? node_id{} : b->handle_of(form_value);
                bool is_form = false;
                if (form) {
                    const auto txn = b->doc_->read();
                    is_form = helpers.is(txn, form, "form");
                }
                if (!is_form) {
                    c.throw_error("TypeError", "Failed to construct 'FormData': parameter 1 is "
                                               "not of type 'HTMLFormElement'.");
                    return value::undefined();
                }
                node_id submitter;
                if (const value given = arg(a, 1); !given.is_nullish()) {
                    submitter = b->handle_of(given);
                    bool is_button = false;
                    bool owned = false;
                    if (submitter) {
                        const auto txn = b->doc_->read();
                        is_button = form_store::is_submit_button(txn, *b->atoms_, submitter);
                        // "Its form owner is not form": the owner, `form=` and
                        // all, not the nearest form ancestor.
                        owned = helpers.form_owner(txn, b, submitter) == form;
                    }
                    if (!is_button) {
                        c.throw_error("TypeError", "Failed to construct 'FormData': the submitter "
                                                   "is not a submit button");
                        return value::undefined();
                    }
                    if (!owned) {
                        throw_dom_exception(c, "NotFoundError",
                                            "the submitter is not owned by the form");
                        return value::undefined();
                    }
                }
                if (!entry_list(c, b, form, submitter, self)) {
                    b->throw_dom_exception(c, "InvalidStateError",
                                           "Failed to construct 'FormData': the form is already "
                                           "constructing its entry list");
                    return value::undefined();
                }
                return self;
            });
        form_data_ctor->define("prototype", form_data_proto_value, script::attr_none);
        form_data_ctor->define("length", value::number(0), script::attr_configurable);
        form_data_proto->define("constructor", value::object(form_data_ctor), script::attr_builtin);
        cx.define_builtin_global("FormData", value::object(form_data_ctor));

        // --- SubmitEvent and FormDataEvent, HTML 4.10.21.2 and 4.10.21.3 -------------
        //
        // Events, so `Event` does the EventInit half - the type, bubbles,
        // cancelable, composed, in that order - and each adds its own member
        // after: `submitter` an HTMLElement or null, `formData` a FormData
        // and required. The events the forms fire take these prototypes
        // (requestSubmit's `submit`, the entry list's `formdata`).
        const auto form_event = [&cx](const char * name, auto member) {
            auto * proto = cx.allocate<script::object_object>();
            const value event_ctor = cx.global("Event");
            if (event_ctor.is_object_like()) {
                proto->prototype = cx.lookup_property(event_ctor, "prototype");
            }
            const value proto_value = value::object(proto);
            auto * ctor = cx.allocate<script::native_object>(
                name, [name, proto_value, member](context & c, std::span<value> a) -> value {
                    const value self = c.current_this();
                    if (!self.is_object()) {
                        c.throw_error("TypeError", std::string{"Failed to construct '"} + name +
                                                       "': please use the 'new' operator.");
                        return value::undefined();
                    }
                    const value init = arg(a, 1);
                    if (!init.is_nullish() && !init.is_object_like()) {
                        c.throw_error("TypeError", std::string{"Failed to construct '"} + name +
                                                       "': parameter 2 is not an object.");
                        return value::undefined();
                    }
                    auto * event = static_cast<script::object_object *>(self.as_heap());
                    if (!event->prototype.is_object()) { event->prototype = proto_value; }
                    (void)c.call(c.global("Event"), a, self);
                    if (c.throw_pending()) { return value::undefined(); }
                    if (!member(c, *event, init)) { return value::undefined(); }
                    return self;
                });
            ctor->retained.push_back(proto_value);
            ctor->define("prototype", proto_value, script::attr_none);
            ctor->define("length", value::number(1), script::attr_configurable);
            proto->define(CTBROWSER_SYMBOL_KEY("toStringTag"), cx.string(name),
                          script::attr_configurable);
            proto->define("constructor", value::object(ctor), script::attr_builtin);
            cx.define_builtin_global(name, value::object(ctor));
        };
        form_event("SubmitEvent", [this](context & c, script::object_object & event, value init) {
            value submitter = dict_object(c, init, "submitter");
            if (!submitter.is_null()) {
                dom_bindings * b = owner_of(submitter);
                const node_id id = b == nullptr ? node_id{} : b->handle_of(submitter);
                if (!id || b->doc_->read().element_ns(id) != node_ns::html) {
                    c.throw_error("TypeError", "Failed to construct 'SubmitEvent': member "
                                               "submitter is not of type 'HTMLElement'.");
                    return false;
                }
            }
            event.set("submitter", submitter);
            return true;
        });
        form_event("FormDataEvent", [](context & c, script::object_object & event, value init) {
            const value form_data = dict_member(c, init, "formData");
            if (!form_data.is_object() ||
                static_cast<script::object_object *>(form_data.as_heap())->find(entries_slot) ==
                    nullptr) {
                c.throw_error("TypeError", "Failed to construct 'FormDataEvent': member "
                                           "formData is not of type 'FormData'.");
                return false;
            }
            event.set("formData", form_data);
            return true;
        });
    }
    // A fresh FormData for a submission, made through the constructor so the
    // page's `FormData.prototype` is the one on it.
    const auto new_form_data = [](context & c) -> value {
        const value ctor = c.global("FormData");
        return ctor.is_callable() ? c.construct(ctor, {}) : value::undefined();
    };

    // --- submit, requestSubmit, reset, HTML 4.10.21 ------------------------------------
    //
    // `submit()` fires no `submit` event; `requestSubmit()` validates, fires
    // it, and builds the list unless it was cancelled. Then the submission
    // itself, HTML 4.10.21.3 from step 6: the entry list; the submitter's
    // method, action, enctype and target over the form's; the row of the
    // scheme table; and a navigation planned at the navigable the target names
    // (navigate_form_target, frames.cpp), or, for the page itself, by the
    // browser (last_submission).
    const auto build_submission = [new_form_data, entry_list, entry_name,
                                   entry_value](context & c, dom_bindings * b, node_id form,
                                                node_id submitter) -> bool {
        const value data = new_form_data(c);
        if (!data.is_object()) { return false; }
        // "Picking an encoding for the form" (HTML 4.10.21.3): the first label
        // in `accept-charset` that names an encoding (UTF-8 when the attribute
        // names none), else the document's; UTF-8 replaces the three encodings
        // that cannot be an output encoding.
        std::string encoding;
        {
            const auto txn = b->doc_->read();
            encoding = b->doc_->encoding();
            if (txn.has_attribute(form, b->atoms_->intern("accept-charset"))) {
                encoding = "UTF-8";
                for (const std::string_view label :
                     split_top_level(txn.attribute_value(form, b->atoms_->intern("accept-charset")),
                                     html_whitespace)) {
                    if (const std::string_view named = encoding_from_label(label); !named.empty()) {
                        encoding = std::string{named};
                        break;
                    }
                }
            }
            if (encoding == "UTF-16BE" || encoding == "UTF-16LE" || encoding == "replacement") {
                encoding = "UTF-8";
            }
        }
        if (!entry_list(c, b, form, submitter, data, encoding) || c.throw_pending()) {
            return false;
        }
        // "If form cannot navigate, then return": a `formdata` listener
        // that removed the form ends the submission.
        if (!b->is_connected(form)) { return false; }
        // The entry list for the encoders: a string entry's text, and a File's
        // name, type and bytes kept apart (multipart/form-data sends all
        // three, the other two the name alone). Read from the entries' and the
        // Files' own slots, the internal slots the specifications give them,
        // never through a getter: no page code runs here, so a `formdata`
        // listener that kept `e.formData` cannot grow the list under the walk
        // (which goes by index regardless).
        std::vector<form_entry> entries;
        if (const value * held =
                static_cast<script::object_object *>(data.as_heap())->find(entries_slot);
            held != nullptr && held->is_array()) {
            const auto & items = static_cast<script::array_object *>(held->as_heap())->items;
            for (std::size_t i = 0; i < items.size(); ++i) {
                const value pair = items[i];
                form_entry entry;
                entry.name = entry_name(c, pair);
                const value v = entry_value(pair);
                if (v.is_object()) {
                    auto * file = static_cast<script::object_object *>(v.as_heap());
                    const auto own_string = [&](std::string_view key) {
                        const value * slot = file->find(key);
                        return slot != nullptr && slot->is_string() ? c.to_string(*slot)
                                                                    : std::string{};
                    };
                    entry.file = true;
                    entry.filename = to_usv_string(own_string("name"));
                    entry.type = own_string("type");
                    if (const value * bytes = file->find("__bytes");
                        bytes != nullptr && bytes->is_array()) {
                        for (const value & byte :
                             static_cast<script::array_object *>(bytes->as_heap())->items) {
                            entry.value.push_back(static_cast<char>(
                                static_cast<std::uint32_t>(context::to_number(byte)) & 0xFFu));
                        }
                    }
                } else {
                    entry.value = c.to_string(v);
                }
                entries.push_back(std::move(entry));
            }
        }
        // The submitter's own attributes win (HTML 4.10.18.6): a submit button's
        // formaction, formmethod, formenctype and formtarget, and where it has
        // none the form's. submit() has no submitter, and requestSubmit()
        // without one submits from the form.
        std::string action;
        std::string method = "get";
        std::string enctype = "application/x-www-form-urlencoded";
        std::string target;
        std::string base;
        {
            const auto txn = b->doc_->read();
            const bool button =
                submitter && form_store::is_submit_button(txn, *b->atoms_, submitter);
            const auto pick = [&](std::string_view own,
                                  std::string_view of_form) -> std::optional<std::string> {
                if (const atom name = b->atoms_->intern(own);
                    button && txn.has_attribute(submitter, name)) {
                    return std::string{txn.attribute_value(submitter, name)};
                }
                if (const atom name = b->atoms_->intern(of_form); txn.has_attribute(form, name)) {
                    return std::string{txn.attribute_value(form, name)};
                }
                return std::nullopt;
            };
            action = pick("formaction", "action").value_or(std::string{});
            // The two enumerated attributes: GET and urlencoded when absent or
            // not one of their keywords (the invalid value defaults).
            if (const auto stated = pick("formmethod", "method")) {
                const std::string folded = ascii_lower_copy(*stated);
                if (folded == "post" || folded == "dialog") { method = folded; }
            }
            if (const auto stated = pick("formenctype", "enctype")) {
                const std::string folded = ascii_lower_copy(*stated);
                if (folded == "multipart/form-data" || folded == "text/plain") { enctype = folded; }
            }
            // "Getting an element's target" (HTML 4.6.4): formtarget, the form's
            // target, else the first <base target>; one with an ASCII tab or
            // newline and a `<` becomes `_blank`.
            target = pick("formtarget", "target").value_or(document_base_target(txn, *b->atoms_));
            if (target.find_first_of("\t\n\r") != std::string::npos &&
                target.find('<') != std::string::npos) {
                target = "_blank";
            }
            base = document_base_url(txn, *b->atoms_, b->fallback_base_url());
        }
        // ponytail: a dialog submission closes no <dialog>; implementing "close
        // the dialog" with the submitter's value is the upgrade.
        if (method == "dialog") { return true; }
        // "If action is the empty string, let action be the URL of the form
        // document", then parsed against its base URL; a failure ends it. A
        // page with no address keeps the action as written, a name the asset
        // registry resolves.
        if (action.empty()) { action = b->location_href_; }
        std::string url = action;
        if (!base.empty()) {
            url = resolve(base, action);
            if (url.empty()) { return true; }
        }
        const std::string scheme =
            ascii_lower_copy(url.substr(0, std::min(url.find(':'), url.size())));
        // ponytail: a javascript: action runs in the target and a mailto: one
        // leaves the browser; neither is here.
        if (scheme == "javascript" || scheme == "mailto") { return true; }
        // "Mutate action URL": the query is replaced by the urlencoded entry
        // list, and the fragment kept.
        const auto with_query = [](const std::string & at, const std::string & query) {
            const std::size_t hash = at.find('#');
            const std::string fragment =
                hash == std::string::npos ? std::string{} : at.substr(hash);
            return at.substr(0, std::min(at.find('?'), hash)) + "?" + query + fragment;
        };
        form_navigation navigation;
        navigation.target = std::move(target);
        if (method == "get" && scheme != "ftp") {
            const std::string query = urlencoded_form_body(entries, encoding);
            navigation.src = with_query(action, query);
            navigation.request.url = with_query(url, query);
        } else if (method == "post" && scheme != "ftp" && scheme != "data") {
            // "Submit as entity body": the enctype's encoding of the list. A
            // scheme the table does not list (file:) is taken as http's.
            navigation.src = action;
            navigation.request.method = "POST";
            navigation.request.url = url;
            if (enctype == "multipart/form-data") {
                multipart_body made = multipart_form_body(entries, encoding);
                navigation.request.body = std::move(made.body);
                navigation.request.content_type = "multipart/form-data; boundary=" + made.boundary;
            } else if (enctype == "text/plain") {
                navigation.request.body = text_plain_form_body(entries, encoding);
                navigation.request.content_type = "text/plain";
            } else {
                navigation.request.body = urlencoded_form_body(entries, encoding);
                navigation.request.content_type = "application/x-www-form-urlencoded";
            }
        } else {
            // "Get action URL": the action as it is, the entry list unused.
            navigation.src = action;
            navigation.request.url = url;
        }
        (void)b->navigate_form_target(form, std::move(navigation));
        return true;
    };
    // HTML 4.10.21.3 "submit" from a submitter (not from submit()): the form
    // must be connected; interactive validation unless `novalidate` or the
    // submitter's `formnovalidate`; the firing-submission-events flag (step
    // 6: while this submission validates and fires `submit`, another one
    // started from an `invalid` or `submit` listener returns at once); a
    // cancelable `submit` SubmitEvent carrying the submitter; then the entry
    // list and the navigation. requestSubmit() runs it after its own checks,
    // and a submit button's activation behaviour is the same call
    // (`submit_from_`, for the browser's default action). True when the
    // submission went ahead: not refused, invalid or cancelled.
    const auto submit_from = [this, helpers, build_submission](context & c, node_id form,
                                                               node_id submitter) -> bool {
        if (!is_connected(form)) { return false; }
        bool validate = true;
        {
            const auto txn = doc_->read();
            if (helpers.has_attribute(txn, this, form, "novalidate") ||
                (submitter && helpers.has_attribute(txn, this, submitter, "formnovalidate"))) {
                validate = false;
            }
        }
        if (context::truthy(helpers.slot_of(c, this, form, firing_slot))) { return false; }
        helpers.set_slot(c, this, form, firing_slot, value::boolean(true));
        bool cancelled = validate && !helpers.validate_form(c, this, form);
        if (!cancelled && !c.throw_pending()) {
            const value submitter_value = submitter ? wrap(c, submitter) : value::null();
            const value submit_proto = interface_prototype_of(c, "SubmitEvent");
            cancelled =
                helpers.fire(c, this, form, "submit", true, true,
                             [submitter_value, submit_proto](script::object_object & event) {
                                 if (submit_proto.is_object()) { event.prototype = submit_proto; }
                                 event.set("submitter", submitter_value);
                             });
        }
        helpers.erase_slot(c, this, form, firing_slot);
        if (cancelled || c.throw_pending()) { return false; }
        return build_submission(c, this, form, submitter);
    };
    submit_from_ = [this, submit_from](node_id form, node_id submitter) {
        return cx_ != nullptr && submit_from(*cx_, form, submitter);
    };
    helpers.operation(cx, "HTMLFormElement", "submit", 0,
                      [this, build_submission](context & c, std::span<value>) {
                          const node_id form = receiver(c);
                          if (!form || !is_connected(form)) { return value::undefined(); }
                          (void)build_submission(c, this, form, node_id{});
                          return value::undefined();
                      });
    helpers.operation(
        cx, "HTMLFormElement", "requestSubmit", 0,
        [this, helpers, submit_from](context & c, std::span<value> a) {
            const node_id form = receiver(c);
            if (!form) { return value::undefined(); }
            node_id submitter;
            if (const value given = arg(a, 0); !given.is_nullish()) {
                submitter = handle_of(given);
                bool is_button = false;
                bool owned = false;
                {
                    const auto txn = doc_->read();
                    if (submitter) {
                        is_button = form_store::is_submit_button(txn, *atoms_, submitter);
                        // The submitter's form owner must be this form, so
                        // one inside it with `form=""` (no owner) is refused.
                        owned = helpers.form_owner(txn, this, submitter) == form;
                    }
                }
                if (!is_button) {
                    c.throw_error("TypeError", "Failed to execute 'requestSubmit': the "
                                               "submitter is not a submit button");
                    return value::undefined();
                }
                if (!owned) {
                    throw_dom_exception(c, "NotFoundError",
                                        "requestSubmit: the submitter is not owned by the form");
                    return value::undefined();
                }
            }
            (void)submit_from(c, form, submitter);
            return value::undefined();
        });
    // "Reset" a form (HTML 4.10.21.5): a cancelable `reset` first, then each
    // resettable control whose owner it is. reset() and a reset button's
    // activation behaviour (`reset_form_`, the browser's) are both this.
    const auto reset_steps = [this, helpers](context & c, node_id form, bool ce_reactions) {
        if (helpers.fire(c, this, form, "reset", true, true, helpers.plain)) { return; }
        // HTML 4.10.21.5: each resettable element whose form owner is this
        // form runs its reset algorithm, in tree order. A control inside the
        // form that names another form with `form=` belongs to that form; one
        // outside that names this form belongs here.
        std::vector<std::pair<node_id, std::string>> outputs;
        std::vector<node_id> custom;
        {
            const auto txn = doc_->read();
            for (const node_id one : helpers.elements_of_form(this, form)) {
                if (helpers.is_face(this, one)) {
                    custom.push_back(one);
                    continue;
                }
                // The store forgets the control and reseeds it from its
                // attributes, which is the reset. Indeterminate is not the
                // reset's to clear, and the selection stays where it was,
                // clamped to the default value (a changed value moves no cursor).
                const control_state * held = forms_->find(one);
                if (held == nullptr) {
                    forms_->reset_form(txn, one);
                } else {
                    const bool indeterminate = held->indeterminate;
                    const std::size_t caret = held->caret;
                    const std::size_t anchor = held->selection;
                    forms_->reset_form(txn, one);
                    control_state & fresh = forms_->state_of(txn, *atoms_, one);
                    fresh.indeterminate = indeterminate;
                    fresh.caret = std::min(caret, fresh.value.size());
                    fresh.selection = std::min(anchor, fresh.value.size());
                }
                if (helpers.is(txn, one, "select")) {
                    helpers.erase_slot(c, this, one, "__syncedValue");
                    helpers.walk_tree(txn, one, [&](node_id at2) {
                        if (helpers.is(txn, at2, "option")) {
                            helpers.erase_slot(c, this, at2, selected_slot);
                            helpers.erase_slot(c, this, at2, dirty_slot);
                        }
                    });
                }
                // An output goes back to the default value mode and its text
                // to the default value (HTML 4.10.12, "string replace all").
                if (helpers.is(txn, one, "output")) {
                    const value held_default = helpers.slot_of(c, this, one, "__defaultValue");
                    if (held_default.is_string()) {
                        outputs.emplace_back(one, c.to_string(held_default));
                    }
                    helpers.erase_slot(c, this, one, "__defaultValue");
                }
            }
        }
        for (const auto & [one, text] : outputs) { set_text(one, text); }
        wrote_to_control_ = true;
        mutated();
        // A form-associated custom element's reset is its formResetCallback, a
        // reaction that runs after every built-in control's reset. reset() is
        // [CEReactions], so they run as it returns; a reset button's activation
        // is no such scope, so they wait on the backup element queue, which a
        // microtask drains (HTML 4.13.6): the click returns before any
        // formResetCallback runs.
        const auto enqueue = [this, custom] {
            const std::size_t from = custom_reactions_.size();
            for (const node_id one : custom) {
                const auto state = custom_elements_.find(one.key());
                if (state == custom_elements_.end()) { continue; }
                custom_element_reaction reaction;
                reaction.target = one;
                reaction.definition = state->second.definition;
                reaction.what = custom_element_reaction::kind::form_reset;
                custom_reactions_.push_back(std::move(reaction));
            }
            if (custom_reactions_.size() > from) { flush_custom_element_reactions(from); }
        };
        if (ce_reactions) {
            enqueue();
        } else if (!custom.empty()) {
            c.queue_microtask(
                native(c, "formResetCallback", [enqueue](context &, std::span<value>) {
                    enqueue();
                    return value::undefined();
                }));
        }
    };
    // "Locked for reset" (HTML 4.10.21.5 steps 1, 2 and 5): while a form is
    // resetting, a nested reset of the same form (form.reset() or a
    // reset-button click from a `reset` listener) does nothing.
    const auto locked = std::make_shared<std::vector<std::uint64_t>>();
    const auto reset_form = [reset_steps, locked](context & c, node_id form, bool ce_reactions) {
        if (std::ranges::find(*locked, form.key()) != locked->end()) { return; }
        locked->push_back(form.key());
        reset_steps(c, form, ce_reactions);
        std::erase(*locked, form.key());
    };
    reset_form_ = [this, reset_form](node_id form) {
        if (cx_ != nullptr && form) { reset_form(*cx_, form, false); }
    };
    helpers.operation(cx, "HTMLFormElement", "reset", 0,
                      [this, reset_form](context & c, std::span<value>) {
                          if (const node_id form = receiver(c)) { reset_form(c, form, true); }
                          return value::undefined();
                      });
}

} // namespace ctbrowser::shell
