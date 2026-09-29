#include "helpers.hpp"

namespace ctbrowser::shell::detail {
using namespace input_types;

script::object_object * control_helpers::proto(const char * which) const {
    return prototype_object(bindings->interface_prototype(which));
}

void control_helpers::operation(context & cx, const char * which, const char * name,
                                unsigned length, script::native_fn fn) const {
    bindings->define_operation(cx, {which}, name, length, std::move(fn));
}

// AN ACCESSOR ON A SHARED PROTOTYPE, answered by the document that owns the
// receiver - define_operation's arrangement, for a getter/setter pair. Every
// document's bindings run this installer and keep their own pair (closing over
// their own tree) in their operations; only the primary defines the accessor,
// and it routes to the owner's pair. It used to define the accessor from every
// document that ran it, so after a frame's first operation `table.caption`
// read the FRAME's tree with the page's node ids, and before one a frame's
// table was read in the page's tree - either way null.
void control_helpers::getter(context & cx, const char * which, const char * name,
                             script::native_fn get, script::native_fn set) const {
    dom_bindings * b = bindings;
    const std::string key = std::string{which} + "." + name;
    const std::size_t index = b->operations_.size();
    const bool settable = set != nullptr;
    b->operations_.push_back(binding_detail::operation{"get " + key, std::move(get)});
    b->operations_.push_back(binding_detail::operation{"set " + key, std::move(set)});
    if (b->secondary_) { return; }
    auto * on = proto(which);
    if (on == nullptr) { return; }
    const auto route = [b, index, key](bool setter) {
        return [b, index, setter, op = (setter ? "set " : "get ") + key](context & c,
                                                                         std::span<value> a) {
            dom_bindings * owner = b->owner_of(c.current_this());
            dom_bindings & target = owner == nullptr ? *b : *owner;
            if (target.operations_.empty()) { target.install_operations(c); }
            const std::size_t at = index + (setter ? 1 : 0);
            if (at < target.operations_.size() && target.operations_[at].name == op) {
                return target.operations_[at].fn(c, a);
            }
            for (const binding_detail::operation & each : target.operations_) {
                if (each.name == op) { return each.fn(c, a); }
            }
            return value::undefined();
        };
    };
    on->define_accessor(name, value::object(cx.allocate<script::native_object>(name, route(false))),
                        settable
                            ? value::object(cx.allocate<script::native_object>(name, route(true)))
                            : value::undefined());
}

bool control_helpers::is(const read_txn & txn, node_id id, std::string_view name) const {
    return id && txn.element_ns(id) == node_ns::html && txn.local_name(id) == name;
}

node_id control_helpers::first_child(node_id parent, std::string_view name) const {
    const auto helpers = *this;
    const auto txn = bindings->doc_->read();
    for (const node_id child : txn.children(parent)) {
        if (helpers.is(txn, child, name)) { return child; }
    }
    return node_id{};
}

std::pair<dom_bindings *, node_id> control_helpers::at(context & c) const {
    dom_bindings * owner = bindings->owner_of(c.current_this());
    if (owner == nullptr) { owner = bindings; }
    return std::pair{owner, owner->receiver(c)};
}

void control_helpers::accessor(context & cx, const char * which, const char * name,
                               script::native_fn get, script::native_fn set) const {
    const auto helpers = *this;
    if (bindings->secondary_) { return; }
    auto * on = helpers.proto(which);
    if (on == nullptr) { return; }
    on->define_accessor(name, native(cx, name, std::move(get)),
                        set ? native(cx, name, std::move(set)) : value::undefined());
}

value control_helpers::slot_of(context & c, dom_bindings * b, node_id id,
                               std::string_view key) const {
    const value w = b->wrap(c, id);
    if (!w.is_object()) { return value::undefined(); }
    const value * held = static_cast<script::object_object *>(w.as_heap())->find(key);
    return held == nullptr ? value::undefined() : *held;
}

// AN OPTION'S SELECTEDNESS IS A SLOT, not a node, so the document's version
// does not see it move - and `select.selectedOptions` is one live collection
// ([SameObject]) that refreshes on that version. A change to the slot says so.
void control_helpers::set_slot(context & c, dom_bindings * b, node_id id, std::string_view key,
                               value v) const {
    const value w = b->wrap(c, id);
    if (w.is_object()) {
        auto * obj = static_cast<script::object_object *>(w.as_heap());
        if (key == selected_slot) {
            const value * was = obj->find(key);
            if (was == nullptr || was->bits() != v.bits()) { b->doc_->note_unversioned_change(); }
        }
        obj->define(std::string{key}, v, script::attr_none);
    }
}

void control_helpers::erase_slot(context & c, dom_bindings * b, node_id id,
                                 std::string_view key) const {
    const value w = b->wrap(c, id);
    if (!w.is_object()) { return; }
    if (static_cast<script::object_object *>(w.as_heap())->erase(key) && key == selected_slot) {
        b->doc_->note_unversioned_change();
    }
}

std::string control_helpers::attribute_of(const read_txn & txn, dom_bindings * b, node_id id,
                                          std::string_view name) const {
    return std::string{txn.attribute_value(id, b->atoms_->intern(name))};
}

bool control_helpers::has_attribute(const read_txn & txn, dom_bindings * b, node_id id,
                                    std::string_view name) const {
    return txn.has_attribute(id, b->atoms_->intern(name));
}

std::string control_helpers::input_type(const read_txn & txn, dom_bindings * b, node_id id) const {
    return type_state_of(txn.attribute_value(id, b->atoms_->intern("type")));
}

std::string control_helpers::value_of(const read_txn & txn, dom_bindings * b, node_id id) const {
    // The store holds what the control SHOWS; a number control's value is
    // what the user agent made of it (input_types::number_value_of_input).
    const control_state & held = b->forms_->state_of(txn, *b->atoms_, id);
    return held.type == "number" ? input_types::number_value_of_input(held.value) : held.value;
}

node_id control_helpers::tree_top(const read_txn & txn, dom_bindings * b, node_id id) const {
    return b->root_of_tree(txn, id, false);
}

node_id control_helpers::form_owner(const read_txn & txn, dom_bindings * b, node_id id) const {
    // HTML 4.10.18.3 - form_store::owning_form is the one algorithm.
    return form_store::owning_form(txn, *b->atoms_, id);
}

bool control_helpers::is_face(dom_bindings * b, node_id id) const {
    // A FORM-ASSOCIATED CUSTOM ELEMENT (HTML 4.13.7.2): a custom element -
    // defined, not failed nor mid-upgrade - whose definition said
    // `formAssociated`. It is listed, labelable, submittable and resettable,
    // so every category below asks.
    const auto state = b->custom_elements_.find(id.key());
    return state != b->custom_elements_.end() &&
           state->second.state == binding_detail::custom_element_state::status::custom &&
           state->second.definition < b->primary().custom_definitions_.size() &&
           b->primary().custom_definitions_[state->second.definition].form_associated;
}

bool control_helpers::face_invalid(dom_bindings * b, node_id id) const {
    // Any of the validity flags setValidity() left on the element's
    // ElementInternals (element_internals.cpp keeps them there).
    const auto wrapper = b->wrappers_.find(id.key());
    if (wrapper == b->wrappers_.end() || wrapper->second == nullptr) { return false; }
    const value * internals = wrapper->second->find(std::string{internals_key});
    if (internals == nullptr || !internals->is_object()) { return false; }
    auto & held = *static_cast<script::object_object *>(internals->as_heap());
    bool invalid = false;
    held.each_own_key([&](const std::string & key) {
        if (!key.starts_with(internals_flag_prefix)) { return; }
        if (const value * flag = held.find(key); flag != nullptr && context::truthy(*flag)) {
            invalid = true;
        }
    });
    return invalid;
}

bool control_helpers::is_listed(const read_txn & txn, dom_bindings * b, node_id id) const {
    const auto helpers = *this;
    if (txn.element_ns(id) != node_ns::html) { return false; }
    const std::string_view local = txn.local_name(id);
    if (local == "input") { return helpers.input_type(txn, b, id) != "image"; }
    return local == "button" || local == "fieldset" || local == "object" || local == "output" ||
           local == "select" || local == "textarea" || helpers.is_face(b, id);
}

bool control_helpers::is_submittable(const read_txn & txn, node_id id) const {
    if (txn.element_ns(id) != node_ns::html) { return false; }
    const std::string_view local = txn.local_name(id);
    return local == "button" || local == "input" || local == "object" || local == "select" ||
           local == "textarea";
}

bool control_helpers::is_labelable(const read_txn & txn, dom_bindings * b, node_id id) const {
    const auto helpers = *this;
    if (!id || txn.element_ns(id) != node_ns::html) { return false; }
    const std::string_view local = txn.local_name(id);
    if (local == "input") { return helpers.input_type(txn, b, id) != "hidden"; }
    return local == "button" || local == "meter" || local == "output" || local == "progress" ||
           local == "select" || local == "textarea" || helpers.is_face(b, id);
}

std::vector<node_id> control_helpers::elements_of_form(dom_bindings * b, node_id form) const {
    const auto helpers = *this;
    std::vector<node_id> out;
    const auto txn = b->doc_->read();
    if (!txn.contains(form)) { return out; }
    helpers.walk_tree(txn, helpers.tree_top(txn, b, form), [&](node_id at2) {
        if (helpers.is_listed(txn, b, at2) && helpers.form_owner(txn, b, at2) == form) {
            out.push_back(at2);
        }
    });
    return out;
}

std::vector<node_id> control_helpers::submittable_of_form(dom_bindings * b, node_id form) const {
    const auto helpers = *this;
    std::vector<node_id> out;
    const auto txn = b->doc_->read();
    if (!txn.contains(form)) { return out; }
    helpers.walk_tree(txn, helpers.tree_top(txn, b, form), [&](node_id at2) {
        if ((helpers.is_submittable(txn, at2) || helpers.is_face(b, at2)) &&
            helpers.form_owner(txn, b, at2) == form) {
            out.push_back(at2);
        }
    });
    return out;
}

bool control_helpers::is_disabled(const read_txn & txn, dom_bindings * b, node_id id) const {
    const auto helpers = *this;
    const atom disabled = b->atoms_->intern("disabled");
    if (txn.element_ns(id) != node_ns::html) { return false; }
    const std::string_view local = txn.local_name(id);
    if (local != "button" && local != "input" && local != "select" && local != "textarea" &&
        local != "fieldset" && local != "optgroup" && local != "option" &&
        !helpers.is_face(b, id)) {
        return false;
    }
    if (txn.has_attribute(id, disabled)) { return true; }
    node_id child = id;
    for (node_id up = txn.parent(id); up; child = up, up = txn.parent(up)) {
        if (!helpers.is(txn, up, "fieldset") || !txn.has_attribute(up, disabled)) { continue; }
        // Inside the fieldset's FIRST legend child: not disabled by it.
        node_id first_legend;
        for (const node_id kid : txn.children(up)) {
            if (helpers.is(txn, kid, "legend")) {
                first_legend = kid;
                break;
            }
        }
        if (child != first_legend) { return true; }
    }
    return false;
}

bool control_helpers::in_radio_group(const read_txn & txn, dom_bindings * b, node_id a,
                                     node_id other) const {
    // HTML 4.10.5.1.16's radio button group: both radio inputs, the same
    // form owner (or none), the same tree, and the same non-empty name -
    // compared exactly, as the name is not a keyword.
    const auto helpers = *this;
    const auto radio = [&](node_id id) {
        return helpers.is(txn, id, "input") && helpers.input_type(txn, b, id) == "radio";
    };
    if (!radio(a) || !radio(other)) { return false; }
    const std::string name = helpers.attribute_of(txn, b, a, "name");
    return !name.empty() && helpers.attribute_of(txn, b, other, "name") == name &&
           helpers.form_owner(txn, b, a) == helpers.form_owner(txn, b, other) &&
           helpers.tree_top(txn, b, a) == helpers.tree_top(txn, b, other);
}

void control_helpers::uncheck_radio_group(const read_txn & txn, dom_bindings * b,
                                          node_id id) const {
    // "When the element's checkedness becomes true, every other element in
    // its radio button group has its checkedness set to false."
    const auto helpers = *this;
    if (!helpers.is(txn, id, "input") || helpers.input_type(txn, b, id) != "radio" ||
        helpers.attribute_of(txn, b, id, "name").empty()) {
        return;
    }
    helpers.walk_tree(txn, helpers.tree_top(txn, b, id), [&](node_id other) {
        if (other != id && helpers.in_radio_group(txn, b, id, other)) {
            control_state & held = b->forms_->state_of(txn, *b->atoms_, other);
            held.checked = false;
            held.checked_edited = true;
        }
    });
}

void control_helpers::children_replaced(dom_bindings * b, node_id id) const {
    // "String replace all" on a textarea (textContent, defaultValue) removes
    // every child BEFORE it inserts the new one, so a clean textarea's value
    // passed through "" and its selection was clamped to 0 on the way - the
    // store, which only sees the result, is told so here.
    const auto helpers = *this;
    const auto txn = b->doc_->read();
    if (!txn.contains(id) || !helpers.is(txn, id, "textarea") || b->forms_->find(id) == nullptr) {
        return;
    }
    control_state & held = b->forms_->state_of(txn, *b->atoms_, id);
    if (held.value_edited) { return; }
    held.caret = 0;
    held.selection = 0;
}

node_id control_helpers::labeled_control(const read_txn & txn, dom_bindings * b,
                                         node_id label) const {
    // A label's labeled control (4.10.4): the element its `for` names when
    // that is labelable, else its first labelable descendant.
    const auto helpers = *this;
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
}

void control_helpers::plain(script::object_object &) {}

} // namespace ctbrowser::shell::detail
