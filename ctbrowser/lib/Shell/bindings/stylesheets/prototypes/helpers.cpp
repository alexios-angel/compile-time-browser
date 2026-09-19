#include "helpers.hpp"

namespace ctbrowser::shell::detail {

// One interface object plus its prototype, wired the way
// `bindings/exceptions.cpp` wires DOMException: the constructor carries a
// non-writable `prototype`, the prototype carries `constructor`, and the
// prototype is reachable from the internals object so a page deleting the
// global cannot collect it.
script::object_object * cssom_interface(context & cx, script::object_object * internals,
                                        const char * name, const char * inherits,
                                        script::native_fn construct) {
    auto * proto = cx.allocate<script::object_object>();
    if (inherits != nullptr) {
        if (const value * parent = internals->find(std::string{inherits} + ".prototype")) {
            proto->prototype = *parent;
        }
    }
    auto * ctor = cx.allocate<script::native_object>(
        name,
        construct ? std::move(construct) : script::native_fn{[name](context & c, std::span<value>) {
            c.throw_error("TypeError", std::string{"Illegal constructor: "} + name);
            return value::undefined();
        }});
    // The interface object inherits too (Web IDL §3.7.1):
    // `CSSStyleRule.__proto__ === CSSGroupingRule`.
    if (inherits != nullptr) {
        if (const value * parent = internals->find(std::string{inherits})) {
            ctor->proto_link = *parent;
        }
    }
    ctor->define("prototype", value::object(proto), script::attr_none);
    proto->define("constructor", value::object(ctor), script::attr_builtin);
    // `@@toStringTag`, which is what `Object.prototype.toString` - and so
    // `rule.toString()` - reads: `[object CSSFontFaceRule]`, not
    // `[object Object]`. The same key element/interfaces.cpp stamps on the
    // element prototypes; Web IDL puts one on every interface prototype.
    proto->define("@@toStringTag", cx.string(name), script::attr_configurable);
    internals->set(std::string{name} + ".prototype", value::object(proto));
    internals->set(std::string{name}, value::object(ctor));
    cx.define_global(name, value::object(ctor));
    return proto;
}

// `@@iterator` on every collection here. `for (const x of list)` already
// worked - context::iterable_values reads `length` and the indices - but
// `Symbol.iterator in CSSStyleDeclaration.prototype` is asked by name, and
// a page driving the iterator by hand needs a real one. Web IDL gives an
// indexed-getter interface exactly the Array iterator, so it IS that one,
// over the snapshot iterable_values already takes.
void cssom_iterable(context & cx, script::object_object * on) {
    set_method(
        cx, *on, "@@iterator",
        [](context & c, std::span<value>) {
            const value items = c.iterable_values(c.current_this());
            return c.call(c.lookup_property(items, "values"), std::span<const value>{}, items);
        },
        script::attr_builtin);
}

} // namespace ctbrowser::shell::detail

namespace ctbrowser::shell {

using namespace detail;

// A `cssRules` list a page holds on to is [SameObject] and must not go
// stale: `const {cssRules} = sheet; sheet.insertRule(...)` then reads
// `cssRules.length` (css/support/parsing-testcommon.js does exactly this)
// without going through the getter that refreshes it. So every mutation
// refreshes the list cached on its receiver, when there is one.
void dom_bindings::refresh_cached_rules(context & c, std::span<const std::size_t> rules) {
    script::object_object * self = as_object(c.current_this());
    if (self == nullptr) { return; }
    if (const value * held = self->find(rules_key)) { refresh_rule_list(c, *held, rules); }
}

void dom_bindings::declaration_accessor(context & cx, script::object_object * on) {
    define_getter(
        cx, *on, "style",
        [this](context & c, std::span<value>) {
            // [SameObject], and LAZY. The declaration object is where the
            // ~290 property accessors are reachable from, and a sheet with
            // three thousand rules in it would otherwise build three
            // thousand of them for a page that never reads one.
            script::object_object * self = as_object(c.current_this());
            if (self == nullptr) { return value::undefined(); }
            if (const value * held = self->find(style_key)) {
                refresh_declaration_object(c, *held);
                return *held;
            }
            const std::size_t at = slot_index(self, rule_key);
            const value made = declaration_object(c, at);
            self->define(style_key, made, script::attr_none);
            return made;
        },
        [](context & c, std::span<value> a) {
            // [PutForwards=cssText]: `rule.style = "margin: 42px"` assigns
            // to `rule.style.cssText` and the object itself never changes.
            const value self = c.current_this();
            const value declarations = c.lookup_property(self, "style");
            c.store_property(declarations, "cssText", a.empty() ? c.string("") : a[0]);
            return value::undefined();
        },
        script::attr_configurable);
}

} // namespace ctbrowser::shell
