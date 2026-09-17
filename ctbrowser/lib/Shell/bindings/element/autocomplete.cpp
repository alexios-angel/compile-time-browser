// dom_bindings - the `autocomplete` IDL attribute of input, select and
// textarea, HTML 4.10.18.7.1: the getter answers the IDL-exposed autofill
// value the "autofill detail tokens" algorithm computes from the content
// attribute (input_types::autocomplete_idl_value), not the attribute's text;
// the setter writes the attribute. A hidden input wears the autofill anchor
// mantle. form-autocomplete.html reads all of it.

#include "internal.hpp"

#include <ctbrowser/shell/page/input_types.hpp>

namespace ctbrowser::shell {

using namespace detail;

void dom_bindings::install_autocomplete(context & cx) {
    for (const char * which : {"HTMLInputElement", "HTMLSelectElement", "HTMLTextAreaElement"}) {
        const value iface = interface_prototype(which);
        if (!iface.is_object()) { continue; }
        auto * proto = static_cast<script::object_object *>(iface.as_heap());
        define_getter(
            cx, *proto, "autocomplete",
            [this](context & c, std::span<value>) {
                dom_bindings * owner = owner_of(c.current_this());
                if (owner == nullptr) { owner = this; }
                const node_id id = owner->receiver(c);
                if (!id) { return c.string(""); }
                const auto txn = owner->doc_->read();
                const atom name = owner->atoms_->intern("autocomplete");
                const bool hidden =
                    txn.local_name(id) == "input" &&
                    ascii_iequals(txn.attribute_value(id, owner->atoms_->intern("type")), "hidden");
                return c.string(input_types::autocomplete_idl_value(
                    txn.attribute_value(id, name), txn.has_attribute(id, name), hidden));
            },
            [this](context & c, std::span<value> a) {
                dom_bindings * owner = owner_of(c.current_this());
                if (owner == nullptr) { owner = this; }
                const node_id id = owner->receiver(c);
                if (!id) { return value::undefined(); }
                (void)owner->doc_->set_attribute(id, owner->atoms_->intern("autocomplete"),
                                                 arg_string(c, a, 0));
                owner->mutated();
                return value::undefined();
            });
    }
}

} // namespace ctbrowser::shell
