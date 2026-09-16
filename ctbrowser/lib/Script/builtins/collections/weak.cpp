// ctbrowser.script builtins - WeakRef and FinalizationRegistry (26.1, 26.2).
//
// New on 2026-09-12. Both exist for their SHAPE and their TypeErrors: this
// engine's collector has no weak references and runs no finalisers, so a
// WeakRef's target is held strongly (`deref` always answers it) and a
// registry's cleanup callback never runs. That is the same deviation
// WeakMap and WeakSet carry (keyed.cpp) - a leak, not a wrong answer - and it
// is what a page can observe: nothing, short of measuring memory.

#include "../internal.hpp"

namespace ctbrowser::script::builtins_detail {

namespace {

constexpr std::string_view weak_ref_slot = "@#WeakRefTarget";
constexpr std::string_view registry_slot = "@#FinalizationRegistryCells";

// CanBeHeldWeakly, 9.13: an object, or a symbol that is not registered with
// Symbol.for (whose key is "@@for:<description>", text/symbol.cpp). The same
// test keyed.cpp makes for WeakMap and WeakSet.
[[nodiscard]] bool can_be_held_weakly(value v) {
    if (v.is_object_like()) { return true; }
    if (!v.is_kind(heap_kind::symbol)) { return false; }
    return !static_cast<symbol_object *>(v.as_heap())->key.starts_with("@@for:");
}

// RequireInternalSlot: the receiver's slot, or null with the TypeError in flight.
[[nodiscard]] value * slot_of(context & cx, std::string_view slot, const char * method) {
    const value self = cx.current_this();
    if (self.is_object()) {
        if (value * held = static_cast<object_object *>(self.as_heap())->find(slot)) {
            return held;
        }
    }
    cx.throw_error("TypeError", std::string{method} + " called on an incompatible receiver");
    return nullptr;
}

} // namespace

void install_weak_refs(context & cx) {
    using detail::method;
    using detail::new_table;

    // --- WeakRef, 26.1 ----------------------------------------------------------
    object_object * ref_proto = new_table(cx);
    auto * ref_ctor = cx.allocate<native_object>("WeakRef", [](context & c, std::span<value> a) {
        const value self = c.current_this();
        if (!detail::constructing_this(self)) {
            c.throw_error("TypeError", "Constructor WeakRef requires 'new'");
            return value::undefined();
        }
        if (!can_be_held_weakly(arg_at(a, 0))) {
            c.throw_error("TypeError",
                          "WeakRef: target must be an object or a non-registered symbol");
            return value::undefined();
        }
        static_cast<object_object *>(self.as_heap())->define(weak_ref_slot, a[0], attr_none);
        return self;
    });
    method(cx, ref_proto, "deref", 0, [](context & c, std::span<value>) {
        const value * held = slot_of(c, weak_ref_slot, "WeakRef.prototype.deref");
        return held == nullptr ? value::undefined() : *held;
    });
    ref_proto->define("@@toStringTag", cx.string("WeakRef"), attr_configurable); // 26.1.3.3
    detail::constant(ref_ctor, "prototype", value::object(ref_proto));
    link_constructor(cx, ref_proto, "WeakRef", 1, value::object(ref_ctor));
    cx.define_global("WeakRef", value::object(ref_ctor));

    // --- FinalizationRegistry, 26.2 ------------------------------------------
    object_object * registry_proto = new_table(cx);
    auto * registry_ctor =
        cx.allocate<native_object>("FinalizationRegistry", [](context & c, std::span<value> a) {
            const value self = c.current_this();
            if (!detail::constructing_this(self)) {
                c.throw_error("TypeError", "Constructor FinalizationRegistry requires 'new'");
                return value::undefined();
            }
            if (!arg_at(a, 0).is_callable()) {
                c.throw_error("TypeError", "FinalizationRegistry: cleanup must be callable");
                return value::undefined();
            }
            auto * made = static_cast<object_object *>(self.as_heap());
            made->define(registry_slot, c.make_array(), attr_none);
            made->define("@#CleanupCallback", a[0], attr_none);
            return self;
        });
    // 26.2.3.2 register(target, heldValue [, unregisterToken]): a cell per
    // call, [target, held, token], appended to the registry's list.
    method(cx, registry_proto, "register", 2, [](context & c, std::span<value> a) {
        const value * cells = slot_of(c, registry_slot, "FinalizationRegistry.prototype.register");
        if (cells == nullptr) { return value::undefined(); }
        const value target = arg_at(a, 0);
        const value held = arg_at(a, 1);
        const value token = arg_at(a, 2);
        if (!can_be_held_weakly(target)) {
            c.throw_error("TypeError", "FinalizationRegistry.prototype.register: invalid target");
            return value::undefined();
        }
        if (target.strict_equals(held)) {
            c.throw_error(
                "TypeError",
                "FinalizationRegistry.prototype.register: target and holdings must not be same");
            return value::undefined();
        }
        if (!can_be_held_weakly(token) && !token.is_undefined()) {
            c.throw_error("TypeError",
                          "FinalizationRegistry.prototype.register: invalid unregister token");
            return value::undefined();
        }
        const value cell = c.make_array();
        auto * triple = static_cast<array_object *>(cell.as_heap());
        triple->items = {target, held, token};
        static_cast<array_object *>(cells->as_heap())->items.push_back(cell);
        return value::undefined();
    });
    // 26.2.3.3 unregister(unregisterToken): every cell registered under it
    // goes, and the answer is whether any did.
    method(cx, registry_proto, "unregister", 1, [](context & c, std::span<value> a) {
        const value * cells =
            slot_of(c, registry_slot, "FinalizationRegistry.prototype.unregister");
        if (cells == nullptr) { return value::undefined(); }
        const value token = arg_at(a, 0);
        if (!can_be_held_weakly(token)) {
            c.throw_error("TypeError",
                          "FinalizationRegistry.prototype.unregister: invalid unregister token");
            return value::undefined();
        }
        auto & list = static_cast<array_object *>(cells->as_heap())->items;
        const std::size_t before = list.size();
        std::erase_if(list, [token](const value & cell) {
            const auto & triple = static_cast<array_object *>(cell.as_heap())->items;
            return triple.size() == 3 && triple[2].strict_equals(token);
        });
        return value::boolean(list.size() != before);
    });
    registry_proto->define("@@toStringTag", cx.string("FinalizationRegistry"),
                           attr_configurable); // 26.2.3.4
    detail::constant(registry_ctor, "prototype", value::object(registry_proto));
    link_constructor(cx, registry_proto, "FinalizationRegistry", 1, value::object(registry_ctor));
    cx.define_global("FinalizationRegistry", value::object(registry_ctor));
}

} // namespace ctbrowser::script::builtins_detail
