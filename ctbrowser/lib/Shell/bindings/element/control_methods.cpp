// dom_bindings - the event listener methods, form control value and focus, and
// the canvas methods getContext, toDataURL and toBlob.
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

void dom_bindings::install_control_methods(context & cx, script::object_object & obj) {
    const auto method = [&](std::string name, script::native_fn fn) {
        obj.set(name, value::object(cx.allocate<script::native_object>(name, std::move(fn))));
    };

    method("addEventListener", [this](context & c, std::span<value> args) {
        const node_id id = receiver(c);
        if (id) { add_listener(make_listener(c, path_step{id, listen_on::node}, args)); }
        return value::undefined();
    });
    // The other half. The WINDOW could remove a listener and an element could
    // not, so a page that tidied up after itself - which p5's Element does when
    // it is removed - threw instead. (The comment here used to say the document
    // could too. It could not, and that was found the same way, one corpus
    // later: see install_document.)
    // `element.dispatchEvent(event)` - the third of the three EventTarget
    // methods, and the one that was missing. addEventListener and
    // removeEventListener were here; nothing a page constructed could ever be
    // sent anywhere, so a page could only ever RECEIVE events the engine made.
    //
    // It returns whether the event was NOT cancelled, which is the opposite of
    // what the internal dispatch reports and is how a caller learns that a
    // listener refused the default action.
    method("dispatchEvent", [this](context & c, std::span<value> args) {
        const node_id id = receiver(c);
        const value event = arg(args, 0);
        if (!id || !event.is_object()) { return value::boolean(true); }
        return value::boolean(!dispatch_to(event, path_step{id, listen_on::node}));
    });
    method("removeEventListener", [this](context & c, std::span<value> args) {
        const node_id id = receiver(c);
        const std::string type = arg_string(c, args, 0);
        const value callback = arg(args, 1);
        // THE CAPTURE FLAG IS PART OF THE IDENTITY - (type, callback, capture)
        // is what the DOM says a listener IS, and only `add_listener` enforced
        // it. So `removeEventListener(t, f)` took a CAPTURING listener away and
        // `removeEventListener(t, f, true)` failed to, which is both subtests of
        // `EventListenerOptions-capture.html`. Reading the third argument is
        // also how a page detects that the option is supported at all.
        //
        // ...and `l.on` was not checked either, so an element wrapper whose id
        // failed to resolve matched `l.target == node_id{}` and could remove the
        // DOCUMENT's and the WINDOW's listeners.
        const value options = arg(args, 2);
        const bool capture = options.is_object()
                                 ? context::truthy(c.lookup_property(options, "capture"))
                                 : context::truthy(options);
        std::erase_if(listeners_, [&](const listener & l) {
            return l.on == listen_on::node && l.target == id && l.type == type &&
                   l.capture == capture && l.callback.bits() == callback.bits();
        });
        return value::undefined();
    });

    // --- form controls -------------------------------------------------
    method("getValue", [this](context & c, std::span<value>) {
        const node_id id = receiver(c);
        if (!id) { return c.string(std::string{}); }
        const auto txn = doc_->read();
        return c.string(forms_->state_of(txn, *atoms_, id).value);
    });
    method("setValue", [this](context & c, std::span<value> args) {
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
    method("isChecked", [this](context & c, std::span<value>) {
        const node_id id = receiver(c);
        if (!id) { return value::boolean(false); }
        const auto txn = doc_->read();
        return value::boolean(forms_->state_of(txn, *atoms_, id).checked);
    });
    method("setChecked", [this](context & c, std::span<value> args) {
        const node_id id = receiver(c);
        if (!id) { return value::undefined(); }
        const auto txn = doc_->read();
        forms_->state_of(txn, *atoms_, id).checked = context::truthy(arg(args, 0));
        mutated();
        return value::undefined();
    });
    method("focus", [this](context & c, std::span<value>) {
        if (const node_id id = receiver(c); id && on_focus_) { on_focus_(id); }
        return value::undefined();
    });
    method("blur", [this](context &, std::span<value>) {
        if (on_focus_) { on_focus_(node_id{}); }
        return value::undefined();
    });

    // --- canvas --------------------------------------------------------
    method("getContext", [this](context & c, std::span<value> args) {
        const node_id id = receiver(c);
        const std::string kind = arg_string(c, args, 0);
        // "2d" and "webgl". `webgl2` IS NOT IMPLEMENTED AND RETURNS NULL, and
        // getting that right took two wrong answers.
        //
        // It first threw for the whole WebGL family, on the grounds that a null
        // was the silent-wrong-answer shape: p5 would take it, fall back to its
        // 2D renderer, and a WEBGL sketch would draw nothing 3D while reporting
        // nothing. A blank canvas and a clear conscience.
        //
        // When a real context arrived, `webgl2` kept throwing - and the comment
        // here claimed the throw was what made p5 fall back to `webgl`. That was
        // backwards, and measurably so. p5's RendererGL asks for `webgl2` first
        // and relies on `getContext(...) || getContext('webgl')`, so it needs a
        // FALSY VALUE to fall through. It catches nothing, so the throw escaped
        // the constructor, escaped createCanvas, and left the sketch on the
        // Renderer2D it already had - which is precisely the outcome the throw
        // was supposed to prevent.
        //
        // Null is also simply what the specification says: an unsupported
        // context id returns null, and feature detection is BUILT on that. It is
        // a documented "not supported" signal rather than a plausible wrong
        // answer, which is the distinction the loud-failure rule turns on.
        if (kind == "webgl" || kind == "experimental-webgl") {
            if (!id) { return value::null(); }
            return webgl_context_object(c, id, 1);
        }
        // `webgl2` RETURNS A CONTEXT NOW (2026-08-02). Everything above is the
        // history of it returning null, and every word of it was right at the
        // time; what changed is that the language and the two capabilities
        // behind it exist - see docs/history/webgl2.md stages 1 to 3.
        //
        // p5's RendererGL asks for this FIRST, so from here on every p5 WEBGL
        // sketch takes a path it has never taken in this engine.
        // examples/pages/p5-webgl.html's golden is the tripwire: the same
        // sketch must produce the same pixels through either context, and if
        // that image moves, this path is wrong rather than new.
        if (kind == "webgl2") {
            if (!id) { return value::null(); }
            return webgl_context_object(c, id, 2);
        }
        if (!id || kind != "2d") { return value::null(); }
        return canvas_context_object(c, id);
    });

    // `canvas.toDataURL()` and `canvas.toBlob()` - READING A CANVAS BACK OUT.
    //
    // Both mean PNG: that is what p5's save() asks for, and encode_png writes one
    // with no compression library (see shell/image/images.hpp). A `type` argument
    // naming anything else still gets PNG rather than a lie about the format -
    // the data URL says image/png, so a page that reads it back is not misled.
    const auto canvas_bytes = [this](context & c) -> std::vector<std::byte> {
        const node_id id = receiver(c);
        if (!id || canvases_ == nullptr) { return {}; }
        // context_for, not pixels_of: a canvas nobody asked getContext of has no
        // surface yet, and a browser still gives you a transparent PNG of the
        // right size rather than nothing. An empty answer here would look like a
        // broken encoder.
        const auto number = [&](std::string_view name, int fallback) {
            const auto txn = doc_->read();
            const std::string_view text = txn.attribute_value(id, atoms_->intern(name));
            int out = 0;
            bool any = false;
            for (const char digit : text) {
                if (digit < '0' || digit > '9') { break; }
                out = out * 10 + (digit - '0');
                any = true;
            }
            return any ? out : fallback;
        };
        (void)canvases_->context_for(id, number("width", 300), number("height", 150));
        const std::shared_ptr<const paint::bitmap> pixels = canvases_->pixels_of(id);
        return pixels ? encode_png(*pixels) : std::vector<std::byte>{};
    };
    method("toDataURL", [canvas_bytes](context & c, std::span<value>) {
        const std::vector<std::byte> png = canvas_bytes(c);
        std::string binary;
        binary.reserve(png.size());
        for (const std::byte b : png) { binary += static_cast<char>(b); }
        // Through the standard library's own btoa, so ONE base64 encoder decides
        // what this means here.
        const value encoder = c.global("btoa");
        if (!encoder.is_callable()) { return c.string("data:image/png;base64,"); }
        const value text = c.string(binary);
        const value args[1] = {text};
        return c.string("data:image/png;base64," + c.to_string(c.call(encoder, args)));
    });
    method("toBlob", [this, canvas_bytes](context & c, std::span<value> args) {
        const value callback = arg(args, 0);
        if (!callback.is_callable()) { return value::undefined(); }
        std::vector<std::byte> png = canvas_bytes(c);
        auto * blob = static_cast<script::object_object *>(c.make_object().as_heap());
        value bytes = c.make_array();
        auto * out = static_cast<script::array_object *>(bytes.as_heap());
        out->elements = script::element_kind::u8;
        out->items.reserve(png.size());
        for (const std::byte b : png) {
            out->items.push_back(value::number(static_cast<double>(static_cast<unsigned char>(b))));
        }
        blob->set("size", value::number(static_cast<double>(png.size())));
        blob->set("type", c.string("image/png"));
        blob->set("__bytes", bytes);
        if (blob_prototype_.is_object()) {
            blob->prototype = blob_prototype_; // so `x instanceof Blob` is true
        }
        // QUEUED, not called: toBlob is asynchronous, and a page that wraps it in
        // a promise - which is what p5's p5.Image.toBlob does - depends on the
        // callback landing after the call returns.
        const value blob_value = value::object(blob);
        c.queue_microtask(callback, std::vector<value>{blob_value});
        return value::undefined();
    });
}

} // namespace ctbrowser::shell
