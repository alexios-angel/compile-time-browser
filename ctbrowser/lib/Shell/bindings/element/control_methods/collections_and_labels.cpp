#include "helpers.hpp"

namespace ctbrowser::shell {
using namespace detail;
using namespace input_types;

void dom_bindings::install_control_collections_labels(context & cx) {
    const control_helpers helpers{this};
    // --- RadioNodeList.value, HTML 4.10.20.1 --------------------------------------------
    //
    // The value of the first checked radio in the list ("on" without a value
    // attribute), and setting it checks the first radio whose value matches.
    if (!secondary_) {
        if (auto * on = helpers.proto("RadioNodeList")) {
            const auto radios = [this, helpers](context & c) {
                std::vector<std::pair<dom_bindings *, node_id>> out;
                const value self = c.current_this();
                const double length = context::to_number(c.lookup_property(self, "length"));
                for (double i = 0; i < length; ++i) {
                    const value item =
                        c.lookup_property(self, std::to_string(static_cast<long long>(i)));
                    dom_bindings * b = owner_of(item);
                    const node_id id = b == nullptr ? node_id{} : b->handle_of(item);
                    if (!id) { continue; }
                    const auto txn = b->doc_->read();
                    if (helpers.is(txn, id, "input") && helpers.input_type(txn, b, id) == "radio") {
                        out.emplace_back(b, id);
                    }
                }
                return out;
            };
            on->define_accessor(
                "value",
                native(cx, "value",
                       [radios, helpers](context & c, std::span<value>) {
                           for (const auto & radio : radios(c)) {
                               dom_bindings * b = radio.first;
                               const node_id id = radio.second;
                               const auto txn = b->doc_->read();
                               if (!b->forms_->state_of(txn, *b->atoms_, id).checked) { continue; }
                               return c.string(helpers.has_attribute(txn, b, id, "value")
                                                   ? helpers.value_of(txn, b, id)
                                                   : std::string{"on"});
                           }
                           return c.string("");
                       }),
                native(cx, "value", [radios, helpers](context & c, std::span<value> a) {
                    const std::string want = arg_string(c, a, 0);
                    for (const auto & radio : radios(c)) {
                        dom_bindings * b = radio.first;
                        const node_id id = radio.second;
                        const auto txn = b->doc_->read();
                        const std::string held = helpers.has_attribute(txn, b, id, "value")
                                                     ? helpers.value_of(txn, b, id)
                                                     : std::string{"on"};
                        if (held != want) { continue; }
                        b->forms_->toggle(txn, *b->atoms_, id, control_kind::radio);
                        b->wrote_to_control_ = true;
                        b->mutated();
                        break;
                    }
                    return value::undefined();
                }));
        }
    }

    // --- HTMLDataListElement.options -------------------------------------------------
    helpers.accessor(cx, "HTMLDataListElement", "options",
                     [helpers](context & c, std::span<value>) {
                         const auto where = helpers.at(c);
                         dom_bindings * b = where.first;
                         const node_id id = where.second;
                         if (!id) { return value::null(); }
                         return b->make_live_collection(c, [b, id, helpers] {
                             std::vector<node_id> out;
                             const auto txn = b->doc_->read();
                             if (!txn.contains(id)) { return out; }
                             const auto walk = [&](auto && self, node_id at2) -> void {
                                 for (const node_id child : txn.children(at2)) {
                                     if (helpers.is(txn, child, "option")) { out.push_back(child); }
                                     self(self, child);
                                 }
                             };
                             walk(walk, id);
                             return out;
                         });
                     });

    // --- labels, HTML 4.10.4 -----------------------------------------------------------
    //
    // A label's labeled control: the element its `for` names when that is
    // labelable, else its first labelable descendant. `labels` on a control
    // is every label in its tree whose control it is.
    const auto labeled_control = [helpers](const read_txn & txn, dom_bindings * b,
                                           node_id label) -> node_id {
        const atom for_attr = b->atoms_->intern("for");
        if (txn.has_attribute(label, for_attr)) {
            const std::string_view want = txn.attribute_value(label, for_attr);
            if (want.empty()) { return node_id{}; }
            node_id found;
            const atom id_attr = b->atoms_->intern("id");
            helpers.walk_tree(txn, helpers.tree_top(txn, b, label), [&](node_id at2) {
                if (!found && txn.attribute_value(at2, id_attr) == want) { found = at2; }
            });
            return helpers.is_labelable(txn, b, found) ? found : node_id{};
        }
        node_id found;
        const auto walk = [&](auto && self, node_id at2) -> void {
            for (const node_id child : txn.children(at2)) {
                if (found) { return; }
                if (helpers.is_labelable(txn, b, child)) {
                    found = child;
                    return;
                }
                self(self, child);
            }
        };
        walk(walk, label);
        return found;
    };
    helpers.accessor(cx, "HTMLLabelElement", "control",
                     [helpers, labeled_control](context & c, std::span<value>) {
                         const auto where = helpers.at(c);
                         dom_bindings * b = where.first;
                         const node_id id = where.second;
                         if (!id) { return value::null(); }
                         node_id control;
                         {
                             const auto txn = b->doc_->read();
                             control = labeled_control(txn, b, id);
                         }
                         return control ? b->wrap(c, control) : value::null();
                     });
    helpers.accessor(cx, "HTMLLabelElement", "form",
                     [helpers, labeled_control](context & c, std::span<value>) {
                         const auto where = helpers.at(c);
                         dom_bindings * b = where.first;
                         const node_id id = where.second;
                         if (!id) { return value::null(); }
                         node_id form;
                         {
                             const auto txn = b->doc_->read();
                             if (const node_id control = labeled_control(txn, b, id)) {
                                 form = helpers.form_owner(txn, b, control);
                             }
                         }
                         return form ? b->wrap(c, form) : value::null();
                     });
    for (const char * which :
         {"HTMLButtonElement", "HTMLInputElement", "HTMLMeterElement", "HTMLOutputElement",
          "HTMLProgressElement", "HTMLSelectElement", "HTMLTextAreaElement"}) {
        helpers.accessor(
            cx, which, "labels", [helpers, labeled_control](context & c, std::span<value>) {
                const auto where = helpers.at(c);
                dom_bindings * b = where.first;
                const node_id id = where.second;
                if (!id) { return value::null(); }
                {
                    const auto txn = b->doc_->read();
                    if (!helpers.is_labelable(txn, b, id)) { return value::null(); }
                }
                return b->make_live_collection(
                    c,
                    [b, id, labeled_control, helpers] {
                        std::vector<node_id> out;
                        const auto txn = b->doc_->read();
                        if (!txn.contains(id)) { return out; }
                        helpers.walk_tree(txn, helpers.tree_top(txn, b, id), [&](node_id at2) {
                            if (helpers.is(txn, at2, "label") &&
                                labeled_control(txn, b, at2) == id) {
                                out.push_back(at2);
                            }
                        });
                        return out;
                    },
                    "NodeList");
            });
    }

    // --- HTMLFieldSetElement, HTMLOutputElement, HTMLTextAreaElement bits -------------
    helpers.accessor(cx, "HTMLFieldSetElement", "type",
                     [](context & c, std::span<value>) { return c.string("fieldset"); });
    helpers.accessor(cx, "HTMLFieldSetElement", "elements",
                     [helpers](context & c, std::span<value>) {
                         const auto where = helpers.at(c);
                         dom_bindings * b = where.first;
                         const node_id id = where.second;
                         if (!id) { return value::null(); }
                         return b->make_live_collection(c, [b, id, helpers] {
                             std::vector<node_id> out;
                             const auto txn = b->doc_->read();
                             if (!txn.contains(id)) { return out; }
                             helpers.walk_tree(txn, id, [&](node_id at2) {
                                 if (at2 != id && helpers.is_listed(txn, b, at2)) {
                                     out.push_back(at2);
                                 }
                             });
                             return out;
                         });
                     });
    helpers.accessor(cx, "HTMLOutputElement", "type",
                     [](context & c, std::span<value>) { return c.string("output"); });
    // An output's `value` and `defaultValue`, HTML 4.10.12: default mode
    // reads the text; setting `value` switches to value mode and keeps the
    // text that was the default.
    helpers.accessor(
        cx, "HTMLOutputElement", "defaultValue",
        [helpers](context & c, std::span<value>) {
            const auto where = helpers.at(c);
            dom_bindings * b = where.first;
            const node_id id = where.second;
            if (!id) { return c.string(""); }
            const value held = helpers.slot_of(c, b, id, "__defaultValue");
            return held.is_undefined() ? c.string(b->text_content(id)) : held;
        },
        [helpers](context & c, std::span<value> a) {
            const auto where = helpers.at(c);
            dom_bindings * b = where.first;
            const node_id id = where.second;
            if (!id) { return value::undefined(); }
            const value text = c.string(arg_string(c, a, 0));
            if (helpers.slot_of(c, b, id, "__defaultValue").is_undefined()) {
                b->set_text(id, c.to_string(text));
            } else {
                helpers.set_slot(c, b, id, "__defaultValue", text);
            }
            return value::undefined();
        });
    helpers.accessor(
        cx, "HTMLOutputElement", "value",
        [helpers](context & c, std::span<value>) {
            const auto where = helpers.at(c);
            dom_bindings * b = where.first;
            const node_id id = where.second;
            return c.string(id ? b->text_content(id) : std::string{});
        },
        [helpers](context & c, std::span<value> a) {
            const auto where = helpers.at(c);
            dom_bindings * b = where.first;
            const node_id id = where.second;
            if (!id) { return value::undefined(); }
            if (helpers.slot_of(c, b, id, "__defaultValue").is_undefined()) {
                helpers.set_slot(c, b, id, "__defaultValue", c.string(b->text_content(id)));
            }
            b->set_text(id, arg_string(c, a, 0));
            return value::undefined();
        });
    helpers.accessor(cx, "HTMLTextAreaElement", "type",
                     [](context & c, std::span<value>) { return c.string("textarea"); });
    helpers.accessor(cx, "HTMLTextAreaElement", "textLength",
                     [helpers](context & c, std::span<value>) {
                         const auto where = helpers.at(c);
                         dom_bindings * b = where.first;
                         const node_id id = where.second;
                         if (!id) { return value::number(0); }
                         const auto txn = b->doc_->read();
                         return value::number(
                             static_cast<double>(units_length_of(helpers.value_of(txn, b, id))));
                     });
    helpers.accessor(
        cx, "HTMLTextAreaElement", "defaultValue",
        [helpers](context & c, std::span<value>) {
            const auto where = helpers.at(c);
            dom_bindings * b = where.first;
            const node_id id = where.second;
            return c.string(id ? b->text_content(id) : std::string{});
        },
        [helpers](context & c, std::span<value> a) {
            const auto where = helpers.at(c);
            dom_bindings * b = where.first;
            const node_id id = where.second;
            if (id) { b->set_text(id, arg_string(c, a, 0)); }
            return value::undefined();
        });
    helpers.accessor(cx, "HTMLInputElement", "list", [helpers](context & c, std::span<value>) {
        const auto where = helpers.at(c);
        dom_bindings * b = where.first;
        const node_id id = where.second;
        if (!id) { return value::null(); }
        node_id found;
        {
            const auto txn = b->doc_->read();
            const std::string type = helpers.input_type(txn, b, id);
            if (type == "hidden" || type == "password" || type == "checkbox" || type == "radio" ||
                type == "file" || type == "submit" || type == "image" || type == "reset" ||
                type == "button") {
                return value::null();
            }
            const std::string_view want = txn.attribute_value(id, b->atoms_->intern("list"));
            if (want.empty()) { return value::null(); }
            const atom id_attr = b->atoms_->intern("id");
            helpers.walk_tree(txn, helpers.tree_top(txn, b, id), [&](node_id at2) {
                if (!found && txn.attribute_value(at2, id_attr) == want) { found = at2; }
            });
            if (!helpers.is(txn, found, "datalist")) { found = node_id{}; }
        }
        return found ? b->wrap(c, found) : value::null();
    });

    // --- HTMLFormElement: elements, length, submit, requestSubmit, reset ----------------
    const auto form_elements = [helpers](context & c, dom_bindings * b, node_id form) {
        const value held = helpers.slot_of(c, b, form, "__elements");
        if (held.is_object_like()) { return held; }
        const value list = b->make_live_collection(
            c, [b, form, helpers] { return helpers.elements_of_form(b, form); },
            "HTMLFormControlsCollection");
        helpers.set_slot(c, b, form, "__elements", list);
        return list;
    };
    helpers.accessor(cx, "HTMLFormElement", "elements",
                     [helpers, form_elements](context & c, std::span<value>) {
                         const auto where = helpers.at(c);
                         dom_bindings * b = where.first;
                         const node_id id = where.second;
                         return id ? form_elements(c, b, id) : value::null();
                     });
    helpers.accessor(cx, "HTMLFormElement", "length", [helpers](context & c, std::span<value>) {
        const auto where = helpers.at(c);
        dom_bindings * b = where.first;
        const node_id id = where.second;
        return value::number(id ? static_cast<double>(helpers.elements_of_form(b, id).size()) : 0);
    });
}

} // namespace ctbrowser::shell
