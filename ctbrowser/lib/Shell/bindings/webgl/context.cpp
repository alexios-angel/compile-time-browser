// dom_bindings - the WebGL context: creating it once per canvas, the wrapper
// object getContext hands back, resizing, presenting, and the refused-call list.
//
// One of four files carved out of a 1,431-line bindings/webgl.cpp on
// 2026-09-08. webgl_context_object was ONE 1,213-line function; it is split
// at three seams that share no local into private member functions, called
// in the order the original installed things. The argument helpers every
// file needs are in internal.hpp beside this, in ctbrowser::shell::detail.

#include "internal.hpp"

namespace ctbrowser::shell {

using namespace detail;

void dom_bindings::resize_webgl_context(node_id id, int width, int height) {
    const auto found = webgl_contexts_.find(pack(id));
    if (found == webgl_contexts_.end() || !found->second || canvases_ == nullptr) { return; }
    canvas_context * surface = canvases_->context_for(id, width, height);
    if (surface == nullptr) { return; }
    found->second->resize(const_cast<paint::bitmap *>(surface->surface().get()), width, height);
    // The page reads these, and p5 reads them to size its projection matrix.
    if (const auto seen = webgl_objects_.find(pack(id)); seen != webgl_objects_.end()) {
        seen->second->set("drawingBufferWidth", value::number(width));
        seen->second->set("drawingBufferHeight", value::number(height));
    }
}

value dom_bindings::webgl_context_object(context & cx, node_id id, int version) {
    if (canvases_ == nullptr) { return value::null(); }
    const auto txn = doc_->read();
    const auto attribute = [&](std::string_view name, int fallback) {
        const std::string_view text = txn.attribute_value(id, atoms_->intern(name));
        int out = 0;
        bool any = false;
        for (const char c : text) {
            if (c < '0' || c > '9') { break; }
            out = out * 10 + (c - '0');
            any = true;
        }
        return any ? out : fallback;
    };
    const int width = attribute("width", 300);
    const int height = attribute("height", 150);
    // THE SAME SURFACE THE 2D PATH DRAWS INTO. A canvas has one set of pixels
    // whichever context it handed out, and the painter reads them through
    // canvases_->pixels_of - so a WebGL draw has to land there or nothing
    // composites it.
    canvas_context * surface = canvases_->context_for(id, width, height);
    if (surface == nullptr) { return value::null(); }

    auto & made = webgl_contexts_[pack(id)];
    // `made` IS A REFERENCE INTO THE MAP, so it is non-null from here on
    // whatever happened - which is why the freshness has to be captured before
    // the create rather than tested after it. Checking `made != nullptr`
    // afterwards is always true, and that mistake converted an existing WebGL 1
    // context into a WebGL 2 one the moment a page asked for `webgl2`.
    const bool created = !made;
    if (!made) {
        made = std::make_unique<webgl_context>(
            const_cast<paint::bitmap *>(surface->surface().get()), width, height);
        // THERE IS NO BACKEND TO CHOOSE ANY MORE. The software rasteriser is
        // gone, so a context is an ANGLE context or it is nothing - see
        // docs/plans/webgl-rewrite.md. What used to be `use_angle()` is now the
        // only path, and `driver::deterministic` is the runtime switch that
        // replaces it.
        //
        // AND IF THE DEVICE WILL NOT COME UP, getContext RETURNS null. That is
        // what a browser does when there is no driver, and it is the difference
        // between a page taking its own no-WebGL branch and a page throwing
        // halfway through its first frame - which is what nine render tests
        // reported as "Unable to create VAO" the moment the software fallback
        // was removed.
        if (!made->ok()) {
            made.reset();
            return value::null();
        }
    }
    webgl_context * gl = made.get();
    // THE VERSION IS DECIDED ONCE, WHEN THE CONTEXT IS CREATED. A canvas has
    // one context type for ever: the spec says a request for a different id
    // returns null rather than converting what is there, and the cache check
    // below is where that null comes from.
    if (created && version >= 2) { gl->set_version(2); }
    const bool webgl2 = gl->version() >= 2;

    // THE SAME JS OBJECT, not just the same state. getContext is idempotent in
    // the spec, and a page compares what it gets - `if (this.gl !== canvas
    // .getContext('webgl'))` is a real pattern - so handing back a fresh wrapper
    // each call is observably wrong even when the state behind it is shared.
    if (const auto seen = webgl_objects_.find(pack(id)); seen != webgl_objects_.end()) {
        // A CANVAS HAS ONE CONTEXT TYPE, FOR EVER. The spec is explicit: once
        // a canvas has a context, asking for a DIFFERENT id returns null - it
        // does not convert, and it does not hand back the one it has under the
        // wrong name. `webgl` and `webgl2` are different ids.
        //
        // Handing back the WebGL 1 object for a `webgl2` request is the shape
        // that matters here: a page would feature-detect on a non-null answer
        // and then reach for constants and methods that are not on it.
        if (gl->version() != version) { return value::null(); }
        return value::object(seen->second);
    }

    auto * obj = static_cast<script::object_object *>(cx.make_object().as_heap());
    // So `gl instanceof WebGLRenderingContext` is true, which Phaser asks - and
    // `instanceof WebGL2RenderingContext` for a version 2 context, which is a
    // DIFFERENT interface rather than a subclass. A page gets the same answer
    // from both tests that a browser would give it.
    const value & interface_prototype = webgl2 ? webgl2_prototype_ : webgl_prototype_;
    if (interface_prototype.is_object()) { obj->prototype = interface_prototype; }
    webgl_objects_[pack(id)] = obj;
    obj->set("canvas", wrap(cx, id));
    obj->set("drawingBufferWidth", value::number(width));
    obj->set("drawingBufferHeight", value::number(height));

    // THE SURFACE, in the order it was always installed: the constant table,
    // then every method. One function until 2026-09-08; the three below are
    // its halves, split where they share no local.
    install_webgl_constants(obj, webgl2);
    install_webgl_methods(cx, obj, gl, surface);
    install_webgl_draw_methods(cx, obj, gl, surface, width, height, webgl2);

    return value::object(obj);
}

void dom_bindings::present_webgl_contexts() {
    for (const auto & [id, context] : webgl_contexts_) {
        if (context != nullptr) { context->present(); }
    }
}

std::vector<std::string> dom_bindings::unforwarded_gl_calls() const {
    std::vector<std::string> out;
    for (const auto & [id, context] : webgl_contexts_) {
        if (context == nullptr) { continue; }
        for (const std::string & call : context->refused()) {
            if (std::ranges::find(out, call) == out.end()) { out.push_back(call); }
        }
    }
    return out;
}

} // namespace ctbrowser::shell
