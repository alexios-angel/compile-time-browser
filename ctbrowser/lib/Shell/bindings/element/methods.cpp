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

    // THE `on...` HANDLER PROPERTIES, PRESENT AND NULL.
    //
    // A browser gives every element one of these per event, defaulting to null,
    // and libraries FEATURE-DETECT with `'onwheel' in element`. Assignment
    // already worked here - `el.onclick = fn` made a property - but `in`
    // answered false, because the property did not exist until something wrote
    // it. That is not a distinction a page can be expected to know about.
    //
    // IT COST A ZOOM. Babylon picks which wheel event to listen for with
    //
    //     "onwheel" in document.createElement("div") ? "wheel"
    //       : document.onmousewheel !== undefined ? "mousewheel" : "DOMMouseScroll"
    //
    // so it fell all the way through to DOMMouseScroll - a Firefox-only name
    // nothing here dispatches - and its ArcRotateCamera could be dragged and
    // not zoomed. Every listener was attached, every event was sent, and the
    // two sets had different names: the same shape as the pointerdown/mousedown
    // fault this file already records.
    //
    // EXACTLY THE EVENTS THIS ENGINE CAN DISPATCH, and no more. A handler
    // property for an event that never fires is a detection that answers yes
    // and a page that then waits forever - which is worse than answering no.
    for (const char * handler :
         {"onclick", "onwheel", "onmousedown", "onmouseup", "onmousemove", "oncontextmenu",
          "onpointerdown", "onpointerup", "onpointermove", "onkeydown", "onkeyup", "oninput",
          "onchange", "onsubmit", "ontoggle", "onfocus", "onblur", "onload", "onerror"}) {
        if (obj.find(handler) == nullptr) { obj.set(handler, value::null()); }
    }

    // `element.click()` - CLICKING WITHOUT A MOUSE.
    //
    // It was absent, and that is how p5's save() reaches the outside world:
    // downloadFile makes an <a href download>, calls click() on it, and revokes
    // the URL on the next line. So the whole export path was one missing method
    // wide, and the failure was that nothing happened - no error, no file.
    //
    // Both halves, in the right order: the event first, through the ordinary
    // capture-and-bubble dispatch, and the DEFAULT ACTION after it unless a
    // listener called preventDefault. A click() that only dispatched would leave
    // `link.click()` doing nothing and `checkbox.click()` not checking anything.
    method("click", [this](context & c, std::span<value> args) {
        (void)args;
        const node_id id = receiver(c);
        if (!id) { return value::undefined(); }
        if (!dispatch_event("click", id, make_event(c, "click", id)) && on_activate_) {
            on_activate_(id);
        }
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
