// dom_bindings - install_element_methods: the `on...` handler properties,
// `click()`, the legacy text and class helpers, and the calls into the three
// files that install the rest of the method surface.
//
// One of twelve files carved out of a 5,442-line bindings/element.cpp on
// 2026-09-08 - which was itself one of six carved out of bindings.cpp on
// 2026-08-09. All are member functions of one class declared in
// include/ctbrowser/shell/bindings.hpp; the helpers more than one of them
// needs are declared in internal.hpp beside this, with external linkage in
// ctbrowser::shell::detail. Nothing about the public header changed.

#include "internal.hpp"

namespace ctbrowser::shell {

using namespace detail;

void dom_bindings::install_element_methods(context & cx, script::object_object & obj) {
    const auto method = [&](std::string name, script::native_fn fn) {
        obj.set(name, value::object(cx.allocate<script::native_object>(name, std::move(fn))));
    };

    // `element.click()` - the whole of it is in dom_bindings::click, beside the
    // engine's own mouse events, because it IS one of those.
    method("click", [this](context & c, std::span<value>) {
        (void)click(receiver(c));
        return value::undefined();
    });

    // The attribute half of the surface - attributes.cpp.
    install_attribute_methods(cx, obj);

    method("setText", [this](context & c, std::span<value> args) {
        set_text(id_or_nothing(c), arg_string(c, args, 0));
        return value::undefined();
    });
    method("getText", [this](context & c, std::span<value>) {
        const node_id id = receiver(c);
        return id ? c.string(text_of(id)) : c.string(std::string{});
    });
    method("addClass", [this](context & c, std::span<value> args) {
        edit_classes(receiver(c), arg_string(c, args, 0), true);
        return value::undefined();
    });
    method("removeClass", [this](context & c, std::span<value> args) {
        edit_classes(receiver(c), arg_string(c, args, 0), false);
        return value::undefined();
    });
    method("hasClass", [this](context & c, std::span<value> args) {
        const node_id id = receiver(c);
        if (!id) { return value::boolean(false); }
        const auto txn = doc_->read();
        const std::string want = arg_string(c, args, 0);
        for (const std::string_view cls : split(txn.attribute_value(id, atoms_->intern("class")))) {
            if (cls == want) { return value::boolean(true); }
        }
        return value::boolean(false);
    });

    // The rest, in the order they were installed when this was one function:
    // the Node and ParentNode methods, then listeners, controls and canvas.
    install_node_methods(cx, obj);
    install_control_methods(cx, obj);
}

} // namespace ctbrowser::shell
