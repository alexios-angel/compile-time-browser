//===- focus_and_canvas.cpp - Focus and canvas bindings ---------*- C++ -*-===//
//
// The form-control test hooks (getValue/setValue/isChecked/setChecked),
// focus() and blur(), and the canvas methods getContext, toDataURL and toBlob.
//
//===----------------------------------------------------------------------===//

#include "helpers.hpp"

#include "../../canvas_objects.hpp"

namespace ctbrowser::shell {
using namespace detail;
using namespace input_types;

void dom_bindings::install_control_focus_canvas(context & cx) {
    const std::initializer_list<const char *> html = {"HTMLElement"};
    const std::initializer_list<const char *> focusable = {"HTMLElement", "SVGElement"};
    const std::initializer_list<const char *> canvas = {"HTMLCanvasElement"};
    // --- form controls -------------------------------------------------
    define_operation(cx, html, "getValue", 0, [this](context & c, std::span<value>) {
        const node_id id = receiver(c);
        if (!id) { return c.string(std::string{}); }
        const auto txn = doc_->read();
        return c.string(forms_->state_of(txn, *atoms_, id).value);
    });
    define_operation(cx, html, "setValue", 1, [this](context & c, std::span<value> args) {
        const node_id id = receiver(c);
        if (!id) { return value::undefined(); }
        const auto txn = doc_->read();
        control_state & control = forms_->state_of(txn, *atoms_, id);
        control.value = arg_string(c, args, 0);
        control.caret = control.value.size();
        control.selection = control.caret;
        control.value_edited = true;
        mutated();
        return value::undefined();
    });
    define_operation(cx, html, "isChecked", 0, [this](context & c, std::span<value>) {
        const node_id id = receiver(c);
        if (!id) { return value::boolean(false); }
        const auto txn = doc_->read();
        return value::boolean(forms_->state_of(txn, *atoms_, id).checked);
    });
    define_operation(cx, html, "setChecked", 1, [this](context & c, std::span<value> args) {
        const node_id id = receiver(c);
        if (!id) { return value::undefined(); }
        const auto txn = doc_->read();
        control_state & held = forms_->state_of(txn, *atoms_, id);
        held.checked = context::truthy(arg(args, 0));
        held.checked_edited = true;
        mutated();
        return value::undefined();
    });
    define_operation(cx, focusable, "focus", 0, [this](context & c, std::span<value>) {
        node_id id = receiver(c);
        if (!id || !on_focus_) { return value::undefined(); }
        // A <label> that is not itself a focusable area - no valid tabindex,
        // or not being rendered - hands focus to its labeled control
        // (HTML 4.10.4), which is the element the user meant.
        {
            const auto txn = doc_->read();
            const control_helpers helpers{this};
            if (helpers.is(txn, id, "label")) {
                long long tabindex = 0;
                const bool focusable_itself =
                    parse_html_integer(helpers.attribute_of(txn, this, id, "tabindex"), tabindex) &&
                    !element_unrendered(txn, *atoms_, styles_, id);
                if (const node_id control = helpers.labeled_control(txn, this, id);
                    control && !focusable_itself) {
                    id = control;
                }
            }
        }
        on_focus_(id);
        return value::undefined();
    });
    define_operation(cx, focusable, "blur", 0, [this](context &, std::span<value>) {
        if (on_focus_) { on_focus_(node_id{}); }
        return value::undefined();
    });

    // --- canvas --------------------------------------------------------
    define_operation(cx, canvas, "getContext", 1, [this](context & c, std::span<value> args) {
        // `contextId` is required (2d.canvas.context.invalid.args).
        if (args.empty()) {
            c.throw_error("TypeError", "getContext: 1 argument required, but only 0 present");
            return value::undefined();
        }
        const node_id id = receiver(c);
        const std::string kind = arg_string(c, args, 0);
        if (c.throw_pending()) { return value::undefined(); }
        // "webgl", "experimental-webgl" and "webgl2" return a WebGL context and
        // "2d" a 2D one. Any other id returns null, as the specification
        // requires: feature detection such as p5's
        // `getContext('webgl2') || getContext('webgl')` relies on a falsy
        // answer, and a throw would escape p5's createCanvas.
        if (kind == "webgl" || kind == "experimental-webgl") {
            if (!id) { return value::null(); }
            return webgl_context_object(c, id, 1);
        }
        // p5's RendererGL asks for `webgl2` first. The golden image for
        // examples/pages/p5-webgl.html checks that the same sketch draws the
        // same pixels through either context version.
        if (kind == "webgl2") {
            if (!id) { return value::null(); }
            return webgl_context_object(c, id, 2);
        }
        if (!id || kind != "2d") { return value::null(); }
        const value made = canvas_context_object(c, id);
        // The settings belong to the context's creation, so only the first
        // getContext('2d') reads them; later calls return the same context.
        if (made.is_object()) {
            auto * held = static_cast<script::object_object *>(made.as_heap());
            if (held->find(canvas_detail::settings_key) == nullptr) {
                const value settings = canvas_detail::context_settings(c, arg(args, 1));
                if (c.throw_pending()) { return value::undefined(); }
                held->set(canvas_detail::settings_key, settings);
            }
        }
        return made;
    });

    // `canvas.toDataURL()` and `canvas.toBlob()` read a canvas back out.
    //
    // Both mean PNG: that is what p5's save() asks for, and encode_png writes one
    // with no compression library (see include/ctbrowser/shell/image/images.hpp).
    // A `type` argument naming anything else still gets PNG, and the data URL
    // says image/png, so a page that reads it back is not misled about the format.
    const auto canvas_bytes = [this](context & c) -> std::vector<std::byte> {
        const node_id id = receiver(c);
        if (!id || canvases_ == nullptr) { return {}; }
        // context_for, not pixels_of: a canvas nobody asked getContext of has no
        // surface yet, and a browser still gives you a transparent PNG of the
        // right size rather than nothing. An empty answer here would look like a
        // broken encoder.
        const auto txn = doc_->read();
        (void)canvases_->context_for(id, static_cast<int>(size_attribute(txn, id, "width", 300)),
                                     static_cast<int>(size_attribute(txn, id, "height", 150)));
        const std::shared_ptr<const paint::bitmap> pixels = canvases_->pixels_of(id);
        return pixels ? encode_png(*pixels) : std::vector<std::byte>{};
    };
    define_operation(cx, canvas, "toDataURL", 0, [canvas_bytes](context & c, std::span<value>) {
        const std::vector<std::byte> png = canvas_bytes(c);
        // Encoded with core's base64_encode, not the page's `btoa`, which a page
        // may have replaced and which takes a string.
        return c.string("data:image/png;base64," +
                        base64_encode(std::string_view{reinterpret_cast<const char *>(png.data()),
                                                       png.size()}));
    });
    define_operation(cx, canvas, "toBlob", 1,
                     [this, canvas_bytes](context & c, std::span<value> args) {
                         const value callback = arg(args, 0);
                         if (!callback.is_callable()) { return value::undefined(); }
                         const std::vector<std::byte> png = canvas_bytes(c);
                         // Queued, not called: toBlob is asynchronous, and a page that wraps
                         // it in a promise (as p5's p5.Image.toBlob does) depends on the
                         // callback running after the call returns.
                         const value blob_value = make_blob(c, make_u8_array(c, png), "image/png");
                         c.queue_microtask(callback, std::vector<value>{blob_value});
                         return value::undefined();
                     });
}

} // namespace ctbrowser::shell
