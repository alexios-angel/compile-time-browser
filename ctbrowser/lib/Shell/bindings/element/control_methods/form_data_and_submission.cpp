#include "helpers.hpp"

namespace ctbrowser::shell {
using namespace detail;
using namespace input_types;

void dom_bindings::install_control_form_data(context & cx) {
    const control_helpers helpers{this};
    // --- the entry list and FormData, HTML 4.10.21.4 and XHR's FormData -----------------
    //
    // A FormData is a list of (name, value) pairs held as a hidden JS array of
    // two-element arrays on the object - traced by the collector - with the
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
    const auto make_entry = [](context & c, const std::string & name, value held) {
        const value pair = c.make_array();
        auto & items = static_cast<script::array_object *>(pair.as_heap())->items;
        items.push_back(c.string(name));
        items.push_back(held.is_object_like() ? held : c.string(c.to_string(held)));
        return pair;
    };
    const auto entry_name = [](context & c, const value & pair) {
        return c.to_string(static_cast<script::array_object *>(pair.as_heap())->items.at(0));
    };
    const auto entry_value = [](const value & pair) {
        return static_cast<script::array_object *>(pair.as_heap())->items.at(1);
    };
    // "Construct the entry list" for a form: the submittable elements'
    // successful controls, in tree order, then the `formdata` event.
    const auto entry_list = [helpers, make_entry](context & c, dom_bindings * b, node_id form,
                                                  node_id submitter, value form_data) {
        auto * list = static_cast<script::array_object *>(
            static_cast<script::object_object *>(form_data.as_heap())
                ->find(entries_slot)
                ->as_heap());
        const auto add = [&](const std::string & name, value held) {
            list->items.push_back(make_entry(c, name, held));
        };
        for (const node_id one : helpers.elements_of_form(b, form)) {
            const auto txn = b->doc_->read();
            if (!txn.contains(one) || helpers.is_disabled(txn, b, one)) { continue; }
            // A FORM-ASSOCIATED CUSTOM ELEMENT (step 5.5-5.7): its submission
            // value - nothing for null, a FormData's entries, else the value
            // under its name.
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
            if (type == "file") { continue; }
            if (type == "hidden" && ascii_lower_copy(name) == "_charset_" &&
                !helpers.has_attribute(txn, b, one, "value")) {
                add(name, c.string("UTF-8"));
                continue;
            }
            add(name, c.string(helpers.value_of(txn, b, one)));
            if ((local == "textarea" || type == "text" || type == "search") &&
                !helpers.attribute_of(txn, b, one, "dirname").empty()) {
                add(helpers.attribute_of(txn, b, one, "dirname"), c.string("ltr"));
            }
        }
        (void)helpers.fire(
            c, b, form, "formdata", true, false,
            [form_data](script::object_object & event) { event.set("formData", form_data); });
    };
    if (!secondary_) {
        auto * form_data_proto = cx.allocate<script::object_object>();
        const value form_data_proto_value = value::object(form_data_proto);
        form_data_proto->define("@@toStringTag", cx.string("FormData"), script::attr_configurable);
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
                const std::string name = arg_string(c, a, 0);
                std::erase_if(list->items,
                              [&](const value & pair) { return entry_name(c, pair) == name; });
            }
            return value::undefined();
        });
        fd_method("get", 1, [entries_of, entry_name, entry_value](context & c, std::span<value> a) {
            if (auto * list = entries_of(c)) {
                const std::string name = arg_string(c, a, 0);
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
                          const std::string name = arg_string(c, a, 0);
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
                const std::string name = arg_string(c, a, 0);
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
            const std::string name = c.to_string(a[0]);
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
        form_data_proto->define("@@iterator", *form_data_proto->find("entries"),
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
                        is_button = helpers.is(txn, submitter, "button") ||
                                    (helpers.is(txn, submitter, "input") &&
                                     (helpers.input_type(txn, b, submitter) == "submit" ||
                                      helpers.input_type(txn, b, submitter) == "image"));
                        for (node_id up = txn.parent(submitter); up; up = txn.parent(up)) {
                            if (up == form) { owned = true; }
                        }
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
                entry_list(c, b, form, submitter, self);
                return self;
            });
        form_data_ctor->define("prototype", form_data_proto_value, script::attr_none);
        form_data_ctor->define("length", value::number(0), script::attr_configurable);
        form_data_proto->define("constructor", value::object(form_data_ctor), script::attr_builtin);
        cx.define_global("FormData", value::object(form_data_ctor));
    }
    // A fresh FormData for a submission, made through the constructor so the
    // page's `FormData.prototype` is the one on it.
    const auto new_form_data = [](context & c) -> value {
        const value ctor = c.global("FormData");
        return ctor.is_callable() ? c.construct(ctor, {}) : value::undefined();
    };

    // --- submit, requestSubmit, reset, HTML 4.10.21 ------------------------------------
    //
    // Submission stops where the network would begin: the entry list is
    // built (which fires `formdata`), and nothing navigates. `submit()`
    // fires no `submit` event; `requestSubmit()` validates, fires it, and
    // builds the list unless it was cancelled.
    const auto build_submission = [new_form_data, entry_list, entry_name,
                                   entry_value](context & c, dom_bindings * b, node_id form,
                                                node_id submitter) {
        const value data = new_form_data(c);
        if (!data.is_object()) { return; }
        entry_list(c, b, form, submitter, data);
        if (c.throw_pending()) { return; }
        // ...AND THE NAVIGATION, when the form aims a GET at a frame this
        // document names (frames.cpp): the entries as strings - a File by
        // its name, as the urlencoded serialiser says.
        std::vector<std::pair<std::string, std::string>> pairs;
        const value * held =
            static_cast<script::object_object *>(data.as_heap())->find(entries_slot);
        if (held != nullptr && held->is_array()) {
            for (const value & pair : static_cast<script::array_object *>(held->as_heap())->items) {
                const value v = entry_value(pair);
                std::string text =
                    v.is_object_like() ? c.to_string(c.lookup_property(v, "name")) : c.to_string(v);
                pairs.emplace_back(entry_name(c, pair), std::move(text));
            }
        }
        (void)b->navigate_form_target(form, pairs);
    };
    submit_form_ = [this, build_submission](node_id form, node_id submitter) {
        if (cx_ != nullptr) { build_submission(*cx_, this, form, submitter); }
    };
    helpers.operation(cx, "HTMLFormElement", "submit", 0,
                      [this, build_submission](context & c, std::span<value>) {
                          const node_id form = receiver(c);
                          if (!form || !is_connected(form)) { return value::undefined(); }
                          build_submission(c, this, form, node_id{});
                          return value::undefined();
                      });
    helpers.operation(
        cx, "HTMLFormElement", "requestSubmit", 0,
        [this, helpers, build_submission](context & c, std::span<value> a) {
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
                        is_button = (helpers.is(txn, submitter, "button") &&
                                     ascii_lower_copy(txn.attribute_value(
                                         submitter, atoms_->intern("type"))) != "reset" &&
                                     ascii_lower_copy(txn.attribute_value(
                                         submitter, atoms_->intern("type"))) != "button") ||
                                    (helpers.is(txn, submitter, "input") &&
                                     (helpers.input_type(txn, this, submitter) == "submit" ||
                                      helpers.input_type(txn, this, submitter) == "image"));
                        for (node_id up = txn.parent(submitter); up; up = txn.parent(up)) {
                            if (up == form) { owned = true; }
                        }
                        if (!owned && submitter) {
                            const std::string_view named =
                                txn.attribute_value(submitter, atoms_->intern("form"));
                            owned = !named.empty() &&
                                    txn.attribute_value(form, atoms_->intern("id")) == named;
                        }
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
            if (!is_connected(form)) { return value::undefined(); }
            bool validate = true;
            {
                const auto txn = doc_->read();
                if (helpers.has_attribute(txn, this, form, "novalidate") ||
                    (submitter && helpers.has_attribute(txn, this, submitter, "formnovalidate"))) {
                    validate = false;
                }
            }
            if (validate && !helpers.validate_form(c, this, form)) { return value::undefined(); }
            const value submitter_value = submitter ? wrap(c, submitter) : value::null();
            const bool cancelled = helpers.fire(c, this, form, "submit", true, true,
                                                [submitter_value](script::object_object & event) {
                                                    event.set("submitter", submitter_value);
                                                });
            if (cancelled || c.throw_pending()) { return value::undefined(); }
            build_submission(c, this, form, submitter);
            return value::undefined();
        });
    helpers.operation(cx, "HTMLFormElement", "reset", 0,
                      [this, helpers](context & c, std::span<value>) {
                          const node_id form = receiver(c);
                          if (!form) { return value::undefined(); }
                          if (helpers.fire(c, this, form, "reset", true, true, helpers.plain)) {
                              return value::undefined();
                          }
                          {
                              const auto txn = doc_->read();
                              forms_->reset_form(txn, form);
                              for (const node_id one : helpers.elements_of_form(this, form)) {
                                  forms_->reset_form(txn, one);
                                  if (helpers.is(txn, one, "select")) {
                                      helpers.walk_tree(txn, one, [&](node_id at2) {
                                          if (helpers.is(txn, at2, "option")) {
                                              helpers.erase_slot(c, this, at2, selected_slot);
                                              helpers.erase_slot(c, this, at2, dirty_slot);
                                          }
                                      });
                                  }
                                  if (helpers.is(txn, one, "output")) {
                                      helpers.erase_slot(c, this, one, "__defaultValue");
                                  }
                              }
                          }
                          wrote_to_control_ = true;
                          mutated();
                          return value::undefined();
                      });
}

} // namespace ctbrowser::shell
