//===- prototypes.cpp - CSSOM interface install -----------------*- C++ -*-===//
//
// dom_bindings::install_stylesheet_prototypes: installs the CSSStyleSheet,
// CSSRuleList, CSSRule, MediaList and CSSStyleDeclaration interfaces (the
// pieces live in prototypes/), then gives every member the Web IDL shape.
//
//===----------------------------------------------------------------------===//

#include "internal.hpp"

#include <ctbrowser/shell/net/url.hpp>

namespace ctbrowser::shell {

using namespace detail;

namespace {

/// Returns whether `self` is one of the objects an interface's members may run
/// on: an object whose chain holds that interface's prototype, and not an
/// interface prototype itself (which carries its own `constructor`, as no
/// CSSOM object does). The chain walk stops after 64 links as a cycle guard.
[[nodiscard]] bool branded(value self, const script::object_object * proto) {
    // A declaration with indexed properties is a proxy the bindings made
    // over the object that holds its slots, so it is one of the interface's
    // objects whatever its target's chain says.
    // ponytail: a page's own Proxy passes too; brand the target when one must not.
    if (self.is_kind(script::heap_kind::proxy)) { return true; }
    script::object_object * object = object_of(self);
    if (object == nullptr || object == proto || object->find("constructor") != nullptr) {
        return false;
    }
    for (int depth = 0; depth < 64; ++depth) {
        object = object_of(object->prototype);
        if (object == nullptr) { return false; }
        if (object == proto) { return true; }
    }
    return false;
}

} // namespace

void dom_bindings::install_stylesheet_prototypes(context & cx) {
    script::object_object * internals = cssom_internals(cx);
    if (internals == nullptr) { return; }
    install_stylesheet_collections(cx, internals);
    install_stylesheet_rules(cx, internals);
    install_stylesheet_declarations(cx, internals);
    // Give the members Web IDL's shape (§3.7.5-3.7.6) once every one is
    // installed: an operation is an enumerable data property, and an attribute
    // an enumerable accessor whose getter throws a TypeError for a receiver
    // that is not one of the interface's objects, the prototype itself first
    // of all (WPT css/cssom/idlharness). The unwrapped getter stays reachable
    // from the internals object, which roots it.
    std::vector<std::pair<std::string, script::object_object *>> prototypes;
    for (const auto & [key, held] : internals->props) {
        if (key.ends_with(".prototype")) {
            if (script::object_object * proto = object_of(held)) {
                prototypes.emplace_back(key, proto);
            }
        }
    }
    // An operation's required arguments set its function's `length`, and a
    // call with fewer throws a TypeError before the body runs (Web IDL §3.6.8
    // step 4).
    struct required_arguments {
        std::string_view interface_name, operation;
        unsigned count;
    };
    static constexpr required_arguments required[] = {
        {"MediaList", "item", 1},
        {"MediaList", "appendMedium", 1},
        {"MediaList", "deleteMedium", 1},
        {"StyleSheetList", "item", 1},
        {"CSSRuleList", "item", 1},
        {"CSSStyleSheet", "insertRule", 1},
        {"CSSStyleSheet", "deleteRule", 1},
        {"CSSStyleSheet", "replaceSync", 1},
        {"CSSGroupingRule", "insertRule", 1},
        {"CSSGroupingRule", "deleteRule", 1},
        {"CSSKeyframesRule", "appendRule", 1},
        {"CSSKeyframesRule", "findRule", 1},
        {"CSSKeyframesRule", "deleteRule", 1},
        {"CSSStyleDeclaration", "item", 1},
        {"CSSStyleDeclaration", "getPropertyValue", 1},
        {"CSSStyleDeclaration", "getPropertyPriority", 1},
        {"CSSStyleDeclaration", "setProperty", 2},
        {"CSSStyleDeclaration", "removeProperty", 1},
    };
    for (const auto & [name, proto] : prototypes) {
        const std::string_view interface_name =
            std::string_view{name}.substr(0, name.size() - std::string_view{".prototype"}.size());
        for (std::size_t i = 0; i < proto->props.size(); ++i) {
            const std::string key = proto->props[i].first;
            if (key == "constructor" || key.starts_with(internal_key_lead)) { continue; }
            if (!proto->props[i].second.is_callable()) { continue; }
            proto->set_attrs_at(i, script::attr_default);
            for (const required_arguments & each : required) {
                if (each.interface_name != interface_name || each.operation != key) { continue; }
                const value inner = proto->props[i].second;
                internals->set(name + "." + key, inner);
                auto * checked = cx.allocate<script::native_object>(
                    key, [inner, key, count = each.count](context & c, std::span<value> args) {
                        if (args.size() < count) {
                            c.throw_error("TypeError", key + ": " + std::to_string(count) +
                                                           " argument(s) required, but only " +
                                                           std::to_string(args.size()) +
                                                           " present");
                            return value::undefined();
                        }
                        return c.call(inner, args, c.current_this());
                    });
                checked->define("length", value::number(each.count), script::attr_configurable);
                proto->set(key, value::object(checked));
                proto->set_attrs_at(i, script::attr_default);
            }
        }
        for (script::accessor_entry & entry : proto->accessors.entries) {
            if (entry.key.starts_with(internal_key_lead)) { continue; }
            entry.attrs |= script::attr_enumerable;
            if (!entry.getter.is_callable()) { continue; }
            const value inner = entry.getter;
            internals->set(name + ".get " + entry.key, inner);
            entry.getter =
                native(cx, "get " + entry.key,
                       [inner, proto, key = entry.key](context & c, std::span<value> args) {
                           if (!branded(c.current_this(), proto)) {
                               c.throw_error("TypeError", "Illegal invocation: " + key);
                               return value::undefined();
                           }
                           return c.call(inner, args, c.current_this());
                       });
        }
    }
}

} // namespace ctbrowser::shell
