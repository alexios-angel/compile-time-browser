//===- context.cpp - getContext for WebGL -----------------------*- C++ -*-===//
//
// The WebGL context of a canvas: creating it once per canvas, the wrapper
// object getContext returns, resizing, presenting, and the list of GL calls
// the context refused.
//
//===----------------------------------------------------------------------===//

#include "internal.hpp"

namespace ctbrowser::shell {

using namespace detail;

void dom_bindings::resize_webgl_context(node_id id, int width, int height) {
    const auto found = webgl_contexts_.find(id.key());
    if (found == webgl_contexts_.end() || !found->second || canvases_ == nullptr) { return; }
    canvas_context * surface = canvases_->context_for(id, width, height);
    if (surface == nullptr) { return; }
    found->second->resize(const_cast<paint::bitmap *>(surface->surface().get()), width, height);
    // The page reads these, and p5 reads them to size its projection matrix.
    if (const auto seen = webgl_objects_.find(id.key()); seen != webgl_objects_.end()) {
        seen->second->set("drawingBufferWidth", value::number(width));
        seen->second->set("drawingBufferHeight", value::number(height));
    }
}

value dom_bindings::webgl_context_object(context & cx, node_id id, int version) {
    if (canvases_ == nullptr) { return value::null(); }
    const auto txn = doc_->read();
    const int width = static_cast<int>(size_attribute(txn, id, "width", 300));
    const int height = static_cast<int>(size_attribute(txn, id, "height", 150));
    // WebGL draws into the same surface the 2D path uses: a canvas has one set
    // of pixels whichever context it handed out, and the painter reads them
    // through canvases_->pixels_of, so a WebGL draw must land there to be
    // composited.
    // One context per canvas: a canvas that already handed out a 2D context
    // answers null to `webgl` (HTML 4.12.5.1, getContext step 3). Sharing the
    // surface would also overread the heap, because the 2D clip mask is sized
    // to the bitmap and present() re-dimensions that bitmap. The map key is
    // canvas.cpp's cache key.
    const auto existing = webgl_contexts_.find(id.key());
    const bool first = existing == webgl_contexts_.end() || !existing->second;
    if (first) {
        const value wrapper = wrap(cx, id);
        if (wrapper.is_object() &&
            static_cast<script::object_object *>(wrapper.as_heap())
                    ->find(CTBROWSER_PRIVATE_KEY("ctbrowser:context2d")) != nullptr) {
            return value::null();
        }
    }
    canvas_context * surface = canvases_->context_for(id, width, height);
    if (surface == nullptr) { return value::null(); }
    // The buffer matches the device. A surface made at an older size (by
    // toDataURL, before a width attribute changed) is re-made at this one, so
    // present() never has to re-dimension a bitmap it only borrows. This is
    // done only before the context exists, because afterwards the context
    // holds a pointer into the surface.
    if (first && (surface->width() != width || surface->height() != height)) {
        canvases_->resize(id, width, height);
    }

    auto & made = webgl_contexts_[id.key()];
    // `made` is a reference into the map, so test for a fresh context before
    // creating one; after the create it is always non-null.
    const bool created = !made;
    if (!made) {
        made = std::make_unique<webgl_context>(
            const_cast<paint::bitmap *>(surface->surface().get()), width, height);
        // Every context is an ANGLE context (docs/history/webgl-rewrite.md);
        // `driver::deterministic` selects the software device at runtime. If
        // the device does not come up, getContext returns null, as a browser
        // with no driver does, so the page takes its own no-WebGL branch
        // instead of throwing partway through its first frame.
        if (!made->device().ok()) {
            made.reset();
            return value::null();
        }
    }
    webgl_context * gl = made.get();
    // The version is decided once, when the context is created. A canvas keeps
    // one context type: a request for a different id returns null rather than
    // converting the existing context, and the cache check below returns that
    // null.
    if (created && version >= 2) { gl->set_version(2); }
    const bool webgl2 = gl->version() >= 2;

    // Return the same JS object, not just the same state. getContext is
    // idempotent in the spec, and pages compare what they get (`if (this.gl
    // !== canvas.getContext('webgl'))` is a real pattern), so a fresh wrapper
    // per call is observably wrong even when the state behind it is shared.
    if (const auto seen = webgl_objects_.find(id.key()); seen != webgl_objects_.end()) {
        // A canvas keeps one context type: asking for a different id (`webgl`
        // vs `webgl2`) returns null rather than the existing context, which a
        // page would otherwise feature-detect as support it lacks.
        if (gl->version() != version) { return value::null(); }
        return value::object(seen->second);
    }

    auto * obj = cx.allocate<script::object_object>();
    // So `gl instanceof WebGLRenderingContext` is true, which Phaser checks,
    // and `instanceof WebGL2RenderingContext` for a version 2 context.
    // WebGL2RenderingContext is a separate interface, not a subclass, so a
    // page gets the same answer from both tests that a browser gives.
    const value & interface_prototype = webgl2 ? webgl2_prototype_ : webgl_prototype_;
    if (interface_prototype.is_object()) { obj->prototype = interface_prototype; }
    webgl_objects_[id.key()] = obj;
    obj->set("canvas", wrap(cx, id));
    obj->set("drawingBufferWidth", value::number(width));
    obj->set("drawingBufferHeight", value::number(height));

    // Install the constant table, then the methods.
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
