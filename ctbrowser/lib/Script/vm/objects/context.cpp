//===- ctbrowser/lib/Script/vm/objects/context.cpp - Context construction -===//
//
// Construction of a context: realms, the global object proxy a bare VM starts
// with, and the per-site literal caches. All functions here are members of
// `context`, declared in include/ctbrowser/script/vm.hpp.
//
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include <ctbrowser/script/bigint.hpp>
#include <ctbrowser/script/builtins.hpp>
#include <ctbrowser/script/vm.hpp>

namespace ctbrowser::script {

context::context() {
    // The first realm: the page's (main_realm).
    scope_realm_ = &create_empty_realm();
    set_realm_template(globals());
    set_global_this(make_global_object(*scope_realm_));
}

/// Creates a new realm whose globals start as a copy of the realm template and
/// whose intrinsics are shared with the main realm.
realm_record & context::create_realm() {
    realms_.push_back(std::make_unique<realm_record>());
    realm_record & made = *realms_.back();
    made.globals = realm_template_;
    made.internals = realm_template_internals_;
    made.builtin = realm_template_builtin_;
    made.platform_prototypes = realm_template_platform_prototypes_;
    // The page's intrinsics, shared: they are the ones the template's bindings
    // refer to (see realm_record).
    made.intrinsics = main_realm().intrinsics;
    return made;
}

// ponytail: a realm lives as long as its context (closures point at their
// realm by address), so a page making ShadowRealms in a loop grows realms_.
/// Creates a realm with no globals except the two compiler primitives.
realm_record & context::create_empty_realm() {
    realms_.push_back(std::make_unique<realm_record>());
    realm_record & made = *realms_.back();
    const realm_scope in{*this, made};
    // Compiler primitives needed even when the embedding installs no builtins.
    define_native(std::string{define_own_name}, [](context & c, std::span<value> a) {
        if (a.size() >= 3) {
            c.define_data_property(a[0], a[1], a[2], a.size() > 3 && context::truthy(a[3]));
        }
        return value::undefined();
    });
    // See to_property_key_name.
    define_native(std::string{to_property_key_name}, [](context & c, std::span<value> a) {
        value key = a.empty() ? value::undefined() : a[0];
        if (key.is_object_like() && !c.to_primitive_hint(key, "string", key)) {
            return value::undefined();
        }
        if (key.is_kind(heap_kind::symbol)) { return key; }
        std::string text = c.to_string(key);
        return c.throw_pending() ? value::undefined() : c.string(std::move(text));
    });
    return made;
}

/// Returns the realm whose global object is \p global, or null.
realm_record * context::realm_of(value global) const noexcept {
    for (const auto & realm : realms_) {
        if (global.is_heap() && realm->global_this.bits() == global.bits()) { return realm.get(); }
    }
    return nullptr;
}

void context::set_realm_template(const string_flat_map<value> & globals) {
    realm_template_ = globals;
    realm_template_internals_ = current_realm().internals;
    realm_template_builtin_ = current_realm().builtin;
    realm_template_platform_prototypes_ = current_realm().platform_prototypes;
}

/// Defines `NaN`, `Infinity` and `undefined` on \p global as non-writable,
/// non-enumerable, non-configurable properties (19.1).
void context::define_immutable_globals(object_object & global) {
    global.define("NaN", value::number(std::numeric_limits<double>::quiet_NaN()), attr_none);
    global.define("Infinity", value::number(std::numeric_limits<double>::infinity()), attr_none);
    global.define("undefined", value::undefined(), attr_none);
}

/// Makes the global object of realm \p r. A bare VM has a global object too.
/// Like Shell's Window view, this proxy shares the realm's global binding table
/// instead of copying it. The table remains the own-data environment used by
/// the VM and AOT helpers; this does not introduce accessor calls into pure
/// global_get. The traps hold their own realm, not the current one: `other.x`
/// read from this realm's code asks the other realm's table.
value context::make_global_object(realm_record & r) {
    realm_record * const realm = &r;
    const value target = make_object();
    define_immutable_globals(*static_cast<object_object *>(target.as_heap()));
    auto * handler = static_cast<object_object *>(make_object().as_heap());
    const auto trap = [&](const char * name, native_fn fn) {
        handler->set(name, value::object(allocate<native_object>(name, std::move(fn))));
    };
    // A declared name's own property on the target is a placeholder
    // (declare_global_var): the table answers it, in every trap below.
    trap("get", [realm](context & cx, std::span<value> args) {
        auto * object = static_cast<object_object *>(args[0].as_heap());
        const std::string name = cx.to_string(args[1]);
        if (realm->declared.contains(name)) { return realm->globals[name]; }
        if (object->find(name) != nullptr || object->find_accessor(name) != nullptr ||
            realm->lexical.contains(name)) {
            return cx.lookup_property(args[0], name);
        }
        if (const auto it = realm->globals.find(name); it != realm->globals.end()) {
            return it->second;
        }
        return cx.lookup_property(args[0], name);
    });
    trap("set", [realm](context & cx, std::span<value> args) {
        auto * object = static_cast<object_object *>(args[0].as_heap());
        const std::string name = cx.to_string(args[1]);
        if (immutable_global(name)) {
            return value::boolean(false); // 19.1: read-only; a TypeError in strict code.
        }
        if (realm->declared.contains(name)) {
            realm->globals[name] = args[2];
        } else if (realm->lexical.contains(name) || object->find(name) != nullptr ||
                   object->find_accessor(name) != nullptr) {
            cx.store_property(args[0], name, args[2]);
        } else {
            realm->globals[name] = args[2];
        }
        return value::boolean(true);
    });
    // The whole chain, not the own table: `'toString' in globalThis` is true
    // in every engine, and global_or_named asks this before it decides a
    // bare identifier is unresolvable.
    trap("has", [realm](context & cx, std::span<value> args) {
        const std::string name = cx.to_string(args[1]);
        return value::boolean(cx.has_property(args[0], args[1]) ||
                              (realm->globals.contains(name) && !realm->lexical.contains(name)));
    });
    // [[GetOwnProperty]] sees the bindings too: `Object.getOwnPropertyDescriptor
    // (this, "Array")` is how test262 verifies every global's attributes. An
    // intrinsic is clause 19's { writable: true, enumerable: false,
    // configurable: true } (realm_record::builtin); a script's `var` is
    // enumerable and not configurable (realm_record::declared); and a plain
    // assignment's binding is an ordinary [[Set]]'s, all three true.
    // The target's own properties answer first, as `get` has them.
    trap("getOwnPropertyDescriptor", [realm](context & cx, std::span<value> args) {
        const std::string name = cx.to_string(args[1]);
        if (realm->declared.contains(name)) {
            return cx.from_property_descriptor(
                property_descriptor::data(realm->globals[name], attr_writable | attr_enumerable));
        }
        property_descriptor found;
        if (cx.own_property(args[0], name, found)) { return cx.from_property_descriptor(found); }
        const auto it = realm->globals.find(name);
        if (it == realm->globals.end() || realm->lexical.contains(name)) {
            return value::undefined();
        }
        return cx.from_property_descriptor(property_descriptor::data(
            it->second,
            realm->builtin.contains(name) ? attr_writable | attr_configurable : attr_default));
    });
    // `delete globalThis.x` reaches the binding table the way `set` does;
    // an own property of the target deletes as one.
    trap("deleteProperty", [realm](context & cx, std::span<value> args) {
        const std::string name = cx.to_string(args[1]);
        property_descriptor found;
        if (cx.own_property(args[0], name, found)) {
            return value::boolean(cx.delete_own_property(args[0], name));
        }
        if (!realm->lexical.contains(name)) {
            (void)realm->globals.erase(name);
            (void)realm->builtin.erase(name);
        }
        return value::boolean(true);
    });
    return value::object(allocate<proxy_object>(target, value::object(handler)));
}

/// The per-site cache behind VM_CASE(load_bigint), shared with compiled bodies
/// so both paths return the same BigInt object for one literal.
value context::interned_bigint_literal(const void * site, std::uint32_t slot,
                                       std::string_view text) {
    const auto parse = [&] {
        // A literal the lexer accepted but that is not an integer (`1.5n`)
        // has no value to load, and 0n is the honest answer for a program that
        // should have been refused at compile time. It does not throw.
        const std::optional<bigint> parsed = bigint_from_literal(std::string{text});
        return value::object(allocate<bigint_object>(parsed.value_or(bigint{0})));
    };
    if (site == nullptr) { return parse(); }
    auto & cache = bigint_cache_[site];
    const auto found = cache.find(slot);
    if (found != cache.end()) { return found->second; }
    const value made = parse();
    cache.emplace(slot, made);
    return made;
}

/// The per-site string cache behind VM_CASE(load_string), shared with compiled
/// bodies. \p text is already the final string: a page's literals were decoded
/// when its source was compiled (compile.cpp), and a private name's key is
/// spelled by compiler_impl::member_key.
value context::interned_string(const void * site, std::uint32_t slot, std::string_view text) {
    if (site == nullptr) { return minted_string(std::string{text}); }
    auto & cache = string_cache_[site];
    const auto found = cache.find(slot);
    if (found != cache.end()) { return found->second; }
    const value made = minted_string(std::string{text});
    cache.emplace(slot, made);
    return made;
}

} // namespace ctbrowser::script
