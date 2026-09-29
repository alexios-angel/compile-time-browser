#pragma once
// Private to lib/Shell/bindings/window/.
#include <ctbrowser/shell/bindings.hpp>

namespace ctbrowser::shell {

// An interface's constructor and prototype, wired the WebIDL way: `prototype`
// on the constructor, `constructor` and `@@toStringTag` on the prototype, and
// the constructor a global. `construct` may be empty and set afterwards, when
// it needs the constructor itself (TextEncoder's `new` check).
struct interface_pair {
    script::native_object * ctor;
    script::object_object * proto;
};

// `length` is the constructor's number of required arguments (WebIDL 3.7.1).
[[nodiscard]] inline interface_pair make_interface(context & cx, const char * name,
                                                   script::native_fn construct = {},
                                                   unsigned length = 0) {
    auto * proto = cx.allocate<script::object_object>();
    auto * ctor = cx.allocate<script::native_object>(name, std::move(construct));
    ctor->define("length", value::number(length), script::attr_configurable);
    ctor->define("prototype", value::object(proto), script::attr_none);
    proto->define("constructor", value::object(ctor), script::attr_builtin);
    proto->define(CTBROWSER_SYMBOL_KEY("toStringTag"), cx.string(name), script::attr_configurable);
    cx.define_builtin_global(name, value::object(ctor));
    return {ctor, proto};
}

// `locationbar`, `menubar` and the rest onto `window` (window.cpp).
void install_bar_props(context & cx, script::object_object & window, value prototype);

// `URL` and `URLSearchParams`, the URL Standard's two interfaces (§6), as thin
// adapters over shell/net/url.hpp - defined in url.cpp, installed as globals.
// Answers the URL constructor so install_window can hang the two blob methods
// (`createObjectURL`, `revokeObjectURL`) on it: those belong to the File API
// and need the bindings' asset registry, which nothing else here does.
[[nodiscard]] script::native_object * install_url(context & cx);

// `TextEncoder` and `TextDecoder`, the Encoding Standard §8 - encoding.cpp,
// which also carries the label table and the single-byte indexes.
void install_encoding(context & cx);

} // namespace ctbrowser::shell
