// ctbrowser.script context - construction: the global object proxy a bare VM
// starts with, and the per-site literal caches.
//
// One of six files carved out of a 1,396-line vm/objects.cpp on 2026-09-08 -
// which was itself one of four carved out of a 3,232-line vm.cpp on
// 2026-08-09. All members of `context`, declared in
// include/ctbrowser/script/vm.hpp - so they split across translation units
// with nothing to declare.

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include <ctbrowser/script/bigint.hpp>
#include <ctbrowser/script/vm.hpp>

namespace ctbrowser::script {

context::context() {
    // A bare VM has a global object too. Like Shell's Window view, this proxy
    // shares the existing global binding table instead of copying it. The
    // table remains the own-data environment used by the VM and AOT helpers;
    // this does not introduce accessor calls into pure global_get.
    const value target = make_object();
    auto * handler = static_cast<object_object *>(make_object().as_heap());
    const auto trap = [&](const char * name, native_fn fn) {
        handler->set(name, value::object(allocate<native_object>(name, std::move(fn))));
    };
    trap("get", [](context & cx, std::span<value> args) {
        auto * object = static_cast<object_object *>(args[0].as_heap());
        const std::string name = cx.to_string(args[1]);
        if (object->find(name) != nullptr || object->find_accessor(name) != nullptr) {
            return cx.lookup_property(args[0], name);
        }
        if (cx.has_global(name)) { return cx.global(name); }
        return cx.lookup_property(args[0], name);
    });
    trap("set", [](context & cx, std::span<value> args) {
        auto * object = static_cast<object_object *>(args[0].as_heap());
        const std::string name = cx.to_string(args[1]);
        if (object->find(name) != nullptr || object->find_accessor(name) != nullptr) {
            cx.store_property(args[0], name, args[2]);
        } else {
            cx.define_global(name, args[2]);
        }
        return value::boolean(true);
    });
    // THE WHOLE CHAIN, not the own table: `'toString' in globalThis` is true
    // in every engine, and global_or_named asks this before it decides a
    // bare identifier is unresolvable.
    trap("has", [](context & cx, std::span<value> args) {
        return value::boolean(cx.has_property(args[0], args[1]) ||
                              cx.has_global(cx.to_string(args[1])));
    });
    // [[GetOwnProperty]] SEES THE BINDINGS TOO: `Object.getOwnPropertyDescriptor
    // (this, "Array")` is how test262 verifies every global's attributes, and a
    // binding in the table is what clause 17 describes - { writable: true,
    // enumerable: false, configurable: true }. The target's own properties
    // answer first, as `get` has them.
    trap("getOwnPropertyDescriptor", [](context & cx, std::span<value> args) {
        const std::string name = cx.to_string(args[1]);
        property_descriptor found;
        if (cx.own_property(args[0], name, found)) { return cx.from_property_descriptor(found); }
        if (!cx.has_global(name)) { return value::undefined(); }
        return cx.from_property_descriptor(
            property_descriptor::data(cx.global(name), attr_writable | attr_configurable));
    });
    // `delete globalThis.x` reaches the binding table the way `set` does;
    // an own property of the target deletes as one.
    trap("deleteProperty", [](context & cx, std::span<value> args) {
        const std::string name = cx.to_string(args[1]);
        property_descriptor found;
        if (cx.own_property(args[0], name, found)) {
            return value::boolean(cx.delete_own_property(args[0], name));
        }
        (void)cx.erase_global(name);
        return value::boolean(true);
    });
    set_global_this(value::object(allocate<proxy_object>(target, value::object(handler))));
}

// VM_CASE(load_string)'s memo, extracted so a compiled body shares it.
value context::interned_bigint_literal(const void * site, std::uint32_t slot,
                                       std::string_view text) {
    const auto parse = [&] {
        // A LITERAL THE LEXER ACCEPTED BUT THAT IS NOT AN INTEGER - `1.5n` -
        // has no value to load, and 0n is the honest answer for a program that
        // should have been refused at compile time. It does NOT throw.
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

value context::interned_string(const void * site, std::uint32_t slot, std::string_view text) {
    if (site == nullptr) { return string(std::string{text}); }
    auto & cache = string_cache_[site];
    const auto found = cache.find(slot);
    if (found != cache.end()) { return found->second; }
    const value made = string(std::string{text});
    cache.emplace(slot, made);
    return made;
}

} // namespace ctbrowser::script
