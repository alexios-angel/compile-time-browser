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

void control_helpers::getter(context & cx, const char * which, const char * name,
                             script::native_fn get, script::native_fn set) const {
    const auto helpers = *this;
    auto * on = helpers.proto(which);
    if (on == nullptr) { return; }
    on->define_accessor(
        name, value::object(cx.allocate<script::native_object>(name, std::move(get))),
        set == nullptr ? value::undefined()
                       : value::object(cx.allocate<script::native_object>(name, std::move(set))));
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

void control_helpers::set_slot(context & c, dom_bindings * b, node_id id, std::string_view key,
                               value v) const {
    const value w = b->wrap(c, id);
    if (w.is_object()) {
        static_cast<script::object_object *>(w.as_heap())
            ->define(std::string{key}, v, script::attr_none);
    }
}

void control_helpers::erase_slot(context & c, dom_bindings * b, node_id id,
                                 std::string_view key) const {
    const value w = b->wrap(c, id);
    if (w.is_object()) { (void)static_cast<script::object_object *>(w.as_heap())->erase(key); }
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
    return b->forms_->state_of(txn, *b->atoms_, id).value;
}

node_id control_helpers::tree_top(const read_txn & txn, dom_bindings * b, node_id id) const {
    return b->root_of_tree(txn, id, false);
}

node_id control_helpers::form_owner(const read_txn & txn, dom_bindings * b, node_id id) const {
    const auto helpers = *this;
    const std::string named = helpers.attribute_of(txn, b, id, "form");
    if (txn.has_attribute(id, b->atoms_->intern("form"))) {
        if (named.empty()) { return node_id{}; }
        node_id found;
        const auto walk = [&](auto && self, node_id at2) -> void {
            if (found) { return; }
            if (txn.attribute_value(at2, b->atoms_->intern("id")) == named) {
                found = at2;
                return;
            }
            for (const node_id child : txn.children(at2)) { self(self, child); }
        };
        walk(walk, helpers.tree_top(txn, b, id));
        return found && helpers.is(txn, found, "form") ? found : node_id{};
    }
    for (node_id up = txn.parent(id); up; up = txn.parent(up)) {
        if (helpers.is(txn, up, "form")) { return up; }
    }
    return node_id{};
}

bool control_helpers::is_listed(const read_txn & txn, dom_bindings * b, node_id id) const {
    const auto helpers = *this;
    if (txn.element_ns(id) != node_ns::html) { return false; }
    const std::string_view local = txn.local_name(id);
    if (local == "input") { return helpers.input_type(txn, b, id) != "image"; }
    return local == "button" || local == "fieldset" || local == "object" || local == "output" ||
           local == "select" || local == "textarea";
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
           local == "select" || local == "textarea";
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

bool control_helpers::is_disabled(const read_txn & txn, dom_bindings * b, node_id id) const {
    const auto helpers = *this;
    const atom disabled = b->atoms_->intern("disabled");
    if (txn.element_ns(id) != node_ns::html) { return false; }
    const std::string_view local = txn.local_name(id);
    if (local != "button" && local != "input" && local != "select" && local != "textarea" &&
        local != "fieldset" && local != "optgroup" && local != "option") {
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

void control_helpers::plain(script::object_object &) {}

} // namespace ctbrowser::shell::detail
