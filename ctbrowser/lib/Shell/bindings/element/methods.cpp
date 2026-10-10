//===- methods.cpp - IDL operations on interface prototypes ---------------===//
//
// The IDL operations on the interface prototypes: define_operation, the one
// mechanism every method of Node, Element, ParentNode, ChildNode and the rest
// goes through, and install_operations, which runs every operation installer.
//
//===----------------------------------------------------------------------===//

#include "internal.hpp"

namespace ctbrowser::shell {

using namespace detail;

/// Defines operation `name` with `length` required arguments on the prototype
/// of each of `interfaces`. There is one native per realm, on the prototype,
/// rather than one per wrapper, so `Node.prototype.insertBefore` exists,
/// `"append" in Text.prototype` is true, and `for...in` over an element does
/// not list the methods.
///
/// The native is a trampoline because the prototypes are shared by every
/// dom_bindings in the realm (adopt_interfaces_of): a native that captured the
/// primary's `this` would run `createHTMLDocument().body.appendChild(x)`
/// against the primary's tree, with a node_id that means something else there.
/// Every instance stores its own `fn` in operations_; `owner_of` names the
/// instance whose wrapper the receiver is, and that instance's copy runs. A
/// receiver that is no wrapper at all (a plain object) falls through to the
/// primary's, whose `receiver()` answers nothing and whose method then throws
/// or does nothing.
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
                const value self = c.current_this();
                // A document as the receiver (`Node.prototype.replaceChild
                // .call(doc, ...)`) is a Node with no handle, and its own
                // object carries the method, so the document's method runs.
                if (is_a_document(self)) {
                    if (auto * own = target_owner(self).document_object()) {
                        if (const value * held = own->find(name);
                            held != nullptr && held->is_callable()) {
                            return c.call(*held, args, self);
                        }
                    }
                }
                dom_bindings * owner = owner_of(self);
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
        // `length` is the number of required arguments in WebIDL
        // (`appendChild.length` is 1, `insertBefore.length` 2), and it is
        // configurable and nothing else, as on every built-in function.
        native->define("length", value::number(length), script::attr_configurable);
        // Not enumerable: an IDL operation is a built-in, so `for...in` over
        // an element does not report it.
        proto->define(name, value::object(native), script::attr_builtin);
    }
}

/// Runs every operation installer, refilling operations_. Each dom_bindings
/// runs the same installers in the same order, which is what lets
/// define_operation's trampoline find an operation by index.
void dom_bindings::install_operations(context & cx) {
    operations_.clear();
    install_node_methods(cx);
    install_attribute_methods(cx);
    install_control_methods(cx);
    install_element_scrolling(cx); // scroll/scrollTo/scrollBy/scrollIntoView (geometry_views.cpp)
}

} // namespace ctbrowser::shell
