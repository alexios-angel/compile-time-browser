// dom_bindings - the IDL operations, on the interface prototypes: the one
// mechanism every method of Node, Element, ParentNode, ChildNode and the rest
// goes through, and the engine's own legacy helpers beside them.

#include "internal.hpp"

namespace ctbrowser::shell {

using namespace detail;

// ONE NATIVE PER REALM, on the prototype the interface names - not one per
// wrapper. Before this every wrapper carried ~80 own native_objects, so
// `Node.prototype.insertBefore` was undefined, `"append" in Text.prototype`
// could only be answered by erasing per wrapper, and `for...in` over an
// element listed the whole method surface.
//
// THE TRAMPOLINE is what a second Document costs. The prototypes are shared by
// every dom_bindings in the realm (adopt_interfaces_of), so a native captured
// `this` on the primary would run `createHTMLDocument().body.appendChild(x)`
// against the primary's tree, with a node_id that means something else there.
// `owner_of` names the instance whose wrapper `this` is, and that instance's
// copy runs; a receiver that is no wrapper at all - `document`, a plain object
// - falls through to the primary's, whose `receiver()` answers nothing and
// whose method then throws or does nothing, as before.
void dom_bindings::define_operation(context & cx, std::initializer_list<const char *> interfaces,
                                    const char * name, unsigned length, script::native_fn fn) {
    const std::size_t index = operations_.size();
    operations_.push_back(operation{name, std::move(fn)});
    if (secondary_) { return; }
    for (const char * which : interfaces) {
        auto * proto = prototype_object(interface_prototype(which));
        if (proto == nullptr) { continue; }
        auto * native = cx.allocate<script::native_object>(
            name, [this, index, name](context & c, std::span<value> args) {
                dom_bindings * owner = owner_of(c.current_this());
                dom_bindings & target = owner == nullptr ? *this : *owner;
                if (target.operations_.empty()) { target.install_operations(c); }
                // Every instance runs the same installers in the same order, so
                // the index agrees; the name check is what says so.
                if (index < target.operations_.size() && target.operations_[index].name == name) {
                    return target.operations_[index].fn(c, args);
                }
                for (const operation & op : target.operations_) {
                    if (op.name == name) { return op.fn(c, args); }
                }
                return value::undefined();
            });
        // `length` is the number of REQUIRED arguments - WebIDL's, so
        // `appendChild.length` is 1 and `insertBefore.length` 2 - and it is
        // configurable and nothing else, as on every built-in function.
        native->define("length", value::number(length), script::attr_configurable);
        // NOT ENUMERABLE: an IDL operation is a built-in, and
        // `Body-FrameSet-Event-Handlers.html` counts what `for...in` reports.
        proto->define(name, value::object(native), script::attr_builtin);
    }
}

void dom_bindings::install_operations(context & cx) {
    operations_.clear();
    install_node_methods(cx);
    install_attribute_methods(cx);
    install_control_methods(cx);

    // --- the engine's own helpers, from before the DOM surface existed --------
    //
    // Not on any interface; examples/pages/widgets.html and a handful of unit
    // tests still write them. On Element, where they have always been reachable.
    define_operation(cx, {"Element"}, "setText", 1, [this](context & c, std::span<value> args) {
        set_text(receiver(c), arg_string(c, args, 0));
        return value::undefined();
    });
    define_operation(cx, {"Element"}, "getText", 0, [this](context & c, std::span<value>) {
        const node_id id = receiver(c);
        return id ? c.string(text_of(id)) : c.string(std::string{});
    });
    define_operation(cx, {"Element"}, "addClass", 1, [this](context & c, std::span<value> args) {
        edit_classes(receiver(c), arg_string(c, args, 0), true);
        return value::undefined();
    });
    define_operation(cx, {"Element"}, "removeClass", 1, [this](context & c, std::span<value> args) {
        edit_classes(receiver(c), arg_string(c, args, 0), false);
        return value::undefined();
    });
    define_operation(cx, {"Element"}, "hasClass", 1, [this](context & c, std::span<value> args) {
        const node_id id = receiver(c);
        if (!id) { return value::boolean(false); }
        const auto txn = doc_->read();
        const std::string want = arg_string(c, args, 0);
        for (const std::string_view cls : split(txn.attribute_value(id, atoms_->intern("class")))) {
            if (cls == want) { return value::boolean(true); }
        }
        return value::boolean(false);
    });
}

void dom_bindings::install_element_methods(context &, script::object_object &) {
    // Nothing is per wrapper any more - see define_operation. A custom element
    // instance reaches every operation through its class's chain, which ends at
    // HTMLElement.prototype.
}

} // namespace ctbrowser::shell
