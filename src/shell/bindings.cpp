#include <ctbrowser/shell/bindings.hpp>

// dom_bindings' method bodies - the API a page's script actually calls.
//
// The header lists what a page can reach; this is how each one works.

namespace ctbrowser::shell {

void dom_bindings::observe_resources(asset_registry & assets, image_store & images) {
    assets_ = &assets;
    images_ = &images;
}

void dom_bindings::observe_location(std::string href, std::string hash) {
    location_href_ = std::move(href);
    location_hash_ = std::move(hash);
    // WRITE THEM THROUGH. `href` was set once when the object was built,
    // which made it a snapshot: a page reading location.href after
    // following a link got whatever was true at page load, forever.
    if (cx_ != nullptr && location_.is_object()) {
        auto * loc = static_cast<script::object_object *>(location_.as_heap());
        loc->set("href", cx_->string(location_href_));
        loc->set("hash", cx_->string(location_hash_));
    }
}

void dom_bindings::set_alert_hook(std::function<void(const std::string &)> hook) {
    on_alert_ = std::move(hook);
}

bool dom_bindings::refresh_wrappers() {
    if (cx_ == nullptr) { return false; }
    wrote_to_control_ = false;
    for (auto & [packed, obj] : wrappers_) {
        if (obj != nullptr) { refresh_element(*cx_, *obj, unpack(packed)); }
    }
    // The document's live properties too - this loop walks `wrappers_`, which
    // the document object is not in, so activeElement and title would go stale
    // the moment focus moved.
    refresh_document();
    return wrote_to_control_;
}

// WHERE FOCUS IS, pushed in. The hook the bindings already hold is write-only:
// script can call element.focus() and the browser hears it, but nothing came
// back, so `document.activeElement` had nothing to read. Same shape as
// observe_location, and for the same reason - a value set once at install is a
// snapshot, and this one changes on every click.
void dom_bindings::observe_focus(node_id id) {
    focused_ = id;
    refresh_document();
}

void dom_bindings::observe_viewport(int width, int height) {
    viewport_width_ = width;
    viewport_height_ = height;
}

void dom_bindings::register_roots(context & cx) {
    cx.set_external_roots([this](const context::root_visitor & mark) {
        for (const listener & l : listeners_) { mark(l.callback); }
        for (const timer & t : timers_) { mark(t.callback); }
        for (const value & callback : animation_callbacks_) { mark(callback); }
        for (const auto & [packed, obj] : wrappers_) {
            if (obj != nullptr) { mark(value::object(obj)); }
        }
        mark(location_);
        mark(document_);
        mark(window_);
    });
}

void dom_bindings::install(context & cx) {
    cx_ = &cx;
    register_roots(cx);
    install_console(cx);
    location_ = make_location(cx);
    install_document(cx);
    install_window(cx);
    install_timers(cx);
    install_resources(cx);
    install_navigation(cx);
}

bool dom_bindings::dispatch(std::string_view type, node_id target) {
    if (cx_ == nullptr) { return false; }
    return dispatch_event(type, target, make_event(*cx_, type, target));
}

bool dom_bindings::dispatch_key(std::string_view type, node_id target, const input_event & input) {
    if (cx_ == nullptr) { return false; }
    value event = make_event(*cx_, type, target);
    auto * object = static_cast<script::object_object *>(event.as_heap());
    object->set("code", cx_->string(input.key));
    object->set("key", cx_->string(dom_key_value(input.key, input.shift)));
    object->set("shiftKey", value::boolean(input.shift));
    object->set("ctrlKey", value::boolean(input.ctrl));
    object->set("altKey", value::boolean(false));
    object->set("metaKey", value::boolean(false));
    object->set("repeat", value::boolean(false));
    return dispatch_event(type, target, event);
}

bool dom_bindings::dispatch_mouse(std::string_view type, node_id target,
                                  const input_event & input) {
    if (cx_ == nullptr) { return false; }
    value event = make_event(*cx_, type, target);
    auto * object = static_cast<script::object_object *>(event.as_heap());
    object->set("clientX", value::number(input.x));
    object->set("clientY", value::number(input.y));
    object->set("pageX", value::number(input.x));
    object->set("pageY", value::number(input.y));
    // SDL numbers buttons from 1; the DOM numbers them from 0, with 2 for
    // the right button rather than 3.
    const int dom_button = input.button == 3 ? 2 : (input.button > 0 ? input.button - 1 : 0);
    object->set("button", value::number(dom_button));
    object->set("shiftKey", value::boolean(input.shift));
    object->set("ctrlKey", value::boolean(input.ctrl));
    return dispatch_event(type, target, event);
}

bool dom_bindings::dispatch_event(std::string_view type, node_id target, value event) {
    // BEFORE the listeners run. A handler for `input` reads the field's new
    // value, so a wrapper still holding the old one is the whole bug.
    (void)refresh_wrappers();
    const auto txn = doc_->read();
    for (node_id at = target; at; at = txn.parent(at)) { fire_at(at, type, event); }
    fire_global(type, event);
    return prevented(event);
}

std::size_t dom_bindings::run_due_callbacks() {
    if (cx_ == nullptr) { return 0; }
    std::size_t ran = 0;
    // Copied before running: a callback may add or cancel timers, and
    // iterating the live list while it does is how a timer that
    // re-registers itself becomes an infinite loop inside one tick.
    std::vector<timer> due;
    for (timer & t : timers_) {
        if (!t.cancelled && t.due_ms <= now_ms_) { due.push_back(t); }
    }
    for (const timer & t : due) {
        const auto still = std::ranges::find_if(
            timers_, [&](const timer & x) { return x.id == t.id && !x.cancelled; });
        if (still == timers_.end()) { continue; }
        if (still->repeating) {
            still->due_ms = now_ms_ + still->interval_ms;
        } else {
            still->cancelled = true;
        }
        (void)cx_->call(t.callback, {});
        ++ran;
    }
    std::erase_if(timers_, [](const timer & t) { return t.cancelled; });

    std::vector<value> frame_callbacks;
    frame_callbacks.swap(animation_callbacks_);
    for (const value & cb : frame_callbacks) {
        const value ms = value::number(now_ms_);
        (void)cx_->call(cb, std::span<const value>{&ms, 1});
        ++ran;
    }
    return ran;
}

double dom_bindings::next_callback_ms() const {
    if (!animation_callbacks_.empty()) { return 0; }
    double soonest = std::numeric_limits<double>::infinity();
    for (const timer & t : timers_) {
        if (t.cancelled) { continue; }
        soonest = std::min(soonest, std::max(0.0, t.due_ms - now_ms_));
    }
    return soonest;
}

std::size_t dom_bindings::pending_animation_frames() const noexcept {
    return animation_callbacks_.size();
}

const std::vector<std::string> & dom_bindings::console_output() const noexcept {
    return console_;
}

value dom_bindings::wrap(context & cx, node_id id) {
    if (!id) { return value::null(); }
    if (const auto it = wrappers_.find(pack(id)); it != wrappers_.end()) {
        refresh_element(cx, *it->second, id);
        return value::object(it->second);
    }
    auto * obj = static_cast<script::object_object *>(cx.make_object().as_heap());
    value wrapper = value::object(obj);
    obj->set(std::string{handle_property}, value::number(static_cast<double>(pack(id))));
    install_element_methods(cx, *obj);
    refresh_element(cx, *obj, id);
    wrappers_.emplace(pack(id), obj);
    return wrapper;
}

std::uint64_t dom_bindings::pack(node_id id) {
    return (static_cast<std::uint64_t>(id.generation) << 32) | id.slot;
}

node_id dom_bindings::unpack(std::uint64_t bits) {
    node_id id;
    id.slot = static_cast<std::uint32_t>(bits & 0xFFFFFFFFu);
    id.generation = static_cast<std::uint32_t>(bits >> 32);
    return id;
}

node_id dom_bindings::receiver(context & cx) {
    const value self = cx.current_this();
    if (!self.is_object()) { return node_id{}; }
    auto * obj = static_cast<script::object_object *>(self.as_heap());
    const value * slot = obj->find(std::string{handle_property});
    if (slot == nullptr) { return node_id{}; }
    return unpack(static_cast<std::uint64_t>(context::to_number(*slot)));
}

void dom_bindings::refresh_element(context & cx, script::object_object & obj, node_id id) {
    const auto txn = doc_->read();
    obj.set("tagName", cx.string(std::string{atoms_->text(txn.tag(id).value_or(atom{}))}));
    obj.set("id", cx.string(std::string{txn.attribute_value(id, atoms_->intern("id"))}));
    obj.set("className", cx.string(std::string{txn.attribute_value(id, atoms_->intern("class"))}));
    // `width` and `height` are the ATTRIBUTES, not the laid-out box. For a
    // <canvas> they are its pixel buffer's size, and essentially every
    // canvas page computes with them - `canvas.width/2` is the first line
    // of most of them. Without these they read as undefined and every
    // coordinate derived from them becomes NaN, which draws nothing at all
    // and reports no error.
    const auto attribute_number = [&](std::string_view name) {
        const std::string_view text = txn.attribute_value(id, atoms_->intern(name));
        double parsed = 0;
        bool any = false;
        for (const char c : text) {
            if (c < '0' || c > '9') { break; }
            parsed = parsed * 10 + (c - '0');
            any = true;
        }
        return any ? parsed : 0.0;
    };
    const std::string_view tag_text = atoms_->text(txn.tag(id).value_or(atom{}));
    if (tag_text == "canvas") {
        // The HTML defaults, which a page that omits the attributes relies on.
        const double w = attribute_number("width");
        const double h = attribute_number("height");
        obj.set("width", value::number(w > 0 ? w : 300));
        obj.set("height", value::number(h > 0 ? h : 150));
    } else if (txn.has_attribute(id, atoms_->intern("width")) ||
               txn.has_attribute(id, atoms_->intern("height"))) {
        obj.set("width", value::number(attribute_number("width")));
        obj.set("height", value::number(attribute_number("height")));
    }

    refresh_control(cx, obj, txn, id, tag_text);

    const rect box = box_of(id);
    obj.set("offsetLeft", value::number(static_cast<double>(box.x)));
    obj.set("offsetTop", value::number(static_cast<double>(box.y)));
    obj.set("offsetWidth", value::number(static_cast<double>(box.width)));
    obj.set("offsetHeight", value::number(static_cast<double>(box.height)));
}

rect dom_bindings::box_of(node_id id) const {
    if (fragments_ == nullptr) { return rect{}; }
    const auto find = [&](auto && self, const layout::fragment & f, float dx, float dy) -> rect {
        const rect box{f.bounds.x + dx, f.bounds.y + dy, f.bounds.width, f.bounds.height};
        if (f.source == id) { return box; }
        for (const auto & child : f.children) {
            if (const rect hit = self(self, child, box.x, box.y); !hit.empty()) { return hit; }
        }
        return rect{};
    };
    return find(find, *fragments_, 0, 0);
}

void dom_bindings::install_element_methods(context & cx, script::object_object & obj) {
    const auto method = [&](std::string name, script::native_fn fn) {
        obj.set(name, value::object(cx.allocate<script::native_object>(name, std::move(fn))));
    };

    method("setAttribute", [this](context & c, std::span<value> args) {
        const node_id id = receiver(c);
        if (!id) { return value::undefined(); }
        (void)doc_->set_attribute(id, atoms_->intern_lower(arg_string(c, args, 0)),
                                  arg_string(c, args, 1));
        mutated();
        return value::undefined();
    });
    method("getAttribute", [this](context & c, std::span<value> args) {
        const node_id id = receiver(c);
        if (!id) { return value::null(); }
        const auto txn = doc_->read();
        // PRESENT-BUT-EMPTY is not absent. `<details open>`, `<input
        // disabled>` and `<option selected>` all have an empty value, and
        // returning null for them made every boolean attribute unreadable
        // from script - the one shape of attribute that is only ever tested
        // for presence.
        const atom name = atoms_->intern_lower(arg_string(c, args, 0));
        if (!txn.has_attribute(id, name)) { return value::null(); }
        return c.string(std::string{txn.attribute_value(id, name)});
    });
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
    method("appendChild", [this](context & c, std::span<value> args) {
        const node_id parent = receiver(c);
        const node_id child = handle_of(arg(args, 0));
        if (parent && child) {
            (void)doc_->append_child(parent, child);
            mutated();
        }
        return arg(args, 0);
    });
    method("removeChild", [this](context &, std::span<value> args) {
        const node_id child = handle_of(arg(args, 0));
        if (child) {
            (void)doc_->remove_child(child);
            mutated();
        }
        return arg(args, 0);
    });
    method("addEventListener", [this](context & c, std::span<value> args) {
        const node_id id = receiver(c);
        if (id) { listeners_.push_back(listener{id, arg_string(c, args, 0), arg(args, 1)}); }
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
        // "2d" ONLY. Returning an object for "webgl" that cannot draw is
        // worse than returning null: a page feature-detects with exactly
        // this call and would take the WebGL path into a dead end.
        if (!id || arg_string(c, args, 0) != "2d") { return value::null(); }
        return canvas_context_object(c, id);
    });
}

std::shared_ptr<const paint::bitmap> dom_bindings::image_argument(value v) {
    if (images_ == nullptr) { return nullptr; }
    if (v.is_number()) { return images_->at(static_cast<int>(context::to_number(v))); }
    if (!v.is_object()) { return nullptr; }
    auto * wrapper = static_cast<script::object_object *>(v.as_heap());
    const value * handle = wrapper->find(handle_property);
    if (handle == nullptr || assets_ == nullptr) { return nullptr; }
    const node_id id = unpack(static_cast<std::uint64_t>(context::to_number(*handle)));
    const auto txn = doc_->read();
    const std::string_view src = txn.attribute_value(id, atoms_->intern("src"));
    return src.empty() ? nullptr : images_->load(*assets_, src);
}

value dom_bindings::canvas_context_object(context & cx, node_id id) {
    const auto txn = doc_->read();
    const auto attribute = [&](std::string_view name, int fallback) {
        const std::string_view text = txn.attribute_value(id, atoms_->intern(name));
        if (text.empty()) { return fallback; }
        int value = 0;
        for (const char c : text) {
            if (c < '0' || c > '9') { break; }
            value = value * 10 + (c - '0');
        }
        return value == 0 ? fallback : value;
    };
    canvas_context * canvas =
        canvases_->context_for(id, attribute("width", 300), attribute("height", 150));
    if (canvas == nullptr) { return value::null(); }

    auto * obj = static_cast<script::object_object *>(cx.make_object().as_heap());
    const value self = value::object(obj);
    obj->set("canvas", wrap(cx, id));
    const auto method = [&](std::string name, script::native_fn fn) {
        obj->set(name, value::object(cx.allocate<script::native_object>(name, std::move(fn))));
    };
    // fillStyle and strokeStyle are PROPERTIES that the drawing calls read
    // back, which is the real canvas idiom - `ctx.fillStyle = 'red'` then
    // `ctx.fillRect(...)`. Reading them at draw time rather than at
    // assignment is what makes that work.
    obj->set("fillStyle", cx.string("#000000"));
    obj->set("strokeStyle", cx.string("#000000"));
    obj->set("lineWidth", value::number(1));
    obj->set("globalAlpha", value::number(1));
    obj->set("font", cx.string("10px sans-serif"));

    const auto sync = [canvas](context & c) {
        const value self_value = c.current_this();
        if (!self_value.is_object()) { return; }
        auto * o = static_cast<script::object_object *>(self_value.as_heap());
        // The spec strings are remembered alongside the parsed values, and only
        // when the parse SUCCEEDED - an unreadable colour leaves both halves
        // alone, so what restore() puts back always matches what is drawn.
        if (const value * v = o->find("fillStyle")) {
            std::string spec = c.to_string(*v);
            if (const auto parsed = paint::parse_color(spec)) {
                canvas->fill_style = *parsed;
                canvas->fill_spec = std::move(spec);
            }
        }
        if (const value * v = o->find("strokeStyle")) {
            std::string spec = c.to_string(*v);
            if (const auto parsed = paint::parse_color(spec)) {
                canvas->stroke_style = *parsed;
                canvas->stroke_spec = std::move(spec);
            }
        }
        if (const value * v = o->find("lineWidth")) {
            canvas->line_width = static_cast<float>(context::to_number(*v));
        }
        if (const value * v = o->find("globalAlpha")) {
            canvas->global_alpha = static_cast<float>(context::to_number(*v));
        }
        if (const value * v = o->find("font")) {
            std::string font = c.to_string(*v);
            canvas->font_size = font_size_from(font);
            font_face_from(font, canvas->font_family, canvas->font_bold, canvas->font_italic);
            canvas->font_spec = std::move(font);
        }
    };

    // A drawing call does NOT report a document mutation. The canvas's
    // pixels are shared with the display list, so nothing needs re-recording
    // - the browser notices the canvas's revision moved and re-rasters. An
    // animation that marked the document dirty would re-run style and layout
    // sixty times a second for a page that did not change.
    const auto draws = [sync](auto body) {
        return [sync, body](context & c, std::span<value> args) {
            sync(c);
            body(c, args);
            return value::undefined();
        };
    };

    method("fillRect", draws([canvas](context &, std::span<value> a) {
               canvas->fill_rect(number(a, 0), number(a, 1), number(a, 2), number(a, 3));
           }));
    method("clearRect", draws([canvas](context &, std::span<value> a) {
               canvas->clear_rect(number(a, 0), number(a, 1), number(a, 2), number(a, 3));
           }));
    method("strokeRect", draws([canvas](context &, std::span<value> a) {
               canvas->stroke_rect(number(a, 0), number(a, 1), number(a, 2), number(a, 3));
           }));
    method("beginPath", draws([canvas](context &, std::span<value>) { canvas->begin_path(); }));
    method("closePath", draws([canvas](context &, std::span<value>) { canvas->close_path(); }));
    method("moveTo", draws([canvas](context &, std::span<value> a) {
               canvas->move_to(number(a, 0), number(a, 1));
           }));
    method("lineTo", draws([canvas](context &, std::span<value> a) {
               canvas->line_to(number(a, 0), number(a, 1));
           }));
    method("rect", draws([canvas](context &, std::span<value> a) {
               canvas->rect_path(number(a, 0), number(a, 1), number(a, 2), number(a, 3));
           }));
    method("arc", draws([canvas](context & c, std::span<value> a) {
               canvas->arc(number(a, 0), number(a, 1), number(a, 2), number(a, 3), number(a, 4),
                           a.size() > 5 && context::truthy(a[5]));
               (void)c;
           }));
    // drawImage(image, dx, dy [, dw, dh]) and the source-rect form. `image`
    // is either a loadImage() handle or an <img> element wrapper - the same
    // two things a page can hold, and both have to work.
    method("drawImage", draws([this, canvas](context &, std::span<value> a) {
               const std::shared_ptr<const paint::bitmap> source = image_argument(arg(a, 0));
               if (!source) { return; }
               const auto natural_w = static_cast<float>(source->width);
               const auto natural_h = static_cast<float>(source->height);
               if (a.size() >= 9) {
                   // The nine-argument form takes a rectangle OUT of the source.
                   canvas->draw_image_region(*source, number(a, 1), number(a, 2), number(a, 3),
                                             number(a, 4), number(a, 5), number(a, 6), number(a, 7),
                                             number(a, 8));
                   return;
               }
               const float w = a.size() >= 4 ? number(a, 3) : natural_w;
               const float h = a.size() >= 5 ? number(a, 4) : natural_h;
               canvas->draw_image(*source, number(a, 1), number(a, 2), w, h);
           }));
    method("fill", draws([canvas](context &, std::span<value>) { canvas->fill(); }));
    method("stroke", draws([canvas](context &, std::span<value>) { canvas->stroke(); }));
    method("save", draws([canvas](context &, std::span<value>) { canvas->save(); }));
    // restore() has to write the state BACK TO THE JAVASCRIPT OBJECT, not just
    // pop the C++ stack. Everything on that stack except the transform is also
    // a property script can assign - fillStyle, strokeStyle, lineWidth,
    // globalAlpha, font - and sync() copies those onto the context before every
    // call. A restore that only popped was therefore undone by the very next
    // draw, and the transform appeared to be the only thing save() protected
    // for exactly that reason: it is the one piece of state with no property
    // behind it.
    //
    // Ordering is what makes this correct: draws() runs sync() FIRST, so any
    // assignment made since the last call is folded in before restore() pops
    // over it - which is what the spec means by restoring the state as of the
    // matching save().
    method("restore", draws([canvas](context & c, std::span<value>) {
               canvas->restore();
               const value self_value = c.current_this();
               if (!self_value.is_object()) { return; }
               auto * o = static_cast<script::object_object *>(self_value.as_heap());
               o->set("fillStyle", c.string(canvas->fill_spec));
               o->set("strokeStyle", c.string(canvas->stroke_spec));
               o->set("font", c.string(canvas->font_spec));
               o->set("lineWidth", value::number(canvas->line_width));
               o->set("globalAlpha", value::number(canvas->global_alpha));
           }));
    method("translate", draws([canvas](context &, std::span<value> a) {
               canvas->translate(number(a, 0), number(a, 1));
           }));
    method("scale", draws([canvas](context &, std::span<value> a) {
               canvas->scale(number(a, 0), number(a, 1));
           }));
    method("rotate",
           draws([canvas](context &, std::span<value> a) { canvas->rotate(number(a, 0)); }));
    method("resetTransform",
           draws([canvas](context &, std::span<value>) { canvas->reset_transform(); }));
    method("fillText", draws([canvas](context & c, std::span<value> a) {
               canvas->fill_text(a.empty() ? std::string{} : c.to_string(a[0]), number(a, 1),
                                 number(a, 2));
           }));
    // NOT wrapped in draws() - it changes no pixels - but it must still SYNC,
    // and it did not. It is the one method that read the canvas's font state
    // without refreshing it first, so `ctx.font = '20px X'; ctx.measureText(s)`
    // measured in whatever font the last DRAWING call happened to leave behind.
    method("measureText", [canvas, sync](context & c, std::span<value> a) {
        sync(c);
        auto * metrics = static_cast<script::object_object *>(c.make_object().as_heap());
        const std::string text = a.empty() ? std::string{} : c.to_string(a[0]);
        // Through the canvas, so the object that measures is the object that
        // draws - see docs/raster.md. Measuring with font8x8 while fillText
        // drew with a real face is exactly how text ends up where it was not.
        metrics->set("width", value::number(static_cast<double>(canvas->measure_text(text))));
        return value::object(metrics);
    });
    return self;
}

float dom_bindings::number(std::span<value> args, std::size_t i) {
    return i < args.size() ? static_cast<float>(context::to_number(args[i])) : 0.0f;
}

float dom_bindings::font_size_from(std::string_view font) {
    const std::size_t px = font.find("px");
    if (px == std::string_view::npos) { return 10; }
    std::size_t start = px;
    while (start > 0 && font[start - 1] >= '0' && font[start - 1] <= '9') { --start; }
    float size = 0;
    for (std::size_t i = start; i < px; ++i) {
        size = size * 10 + static_cast<float>(font[i] - '0');
    }
    return size > 0 ? size : 10;
}

void dom_bindings::font_face_from(std::string_view font, std::string & family, bool & bold,
                                  bool & italic) {
    family.clear();
    bold = false;
    italic = false;
    const std::size_t px = font.find("px");
    if (px == std::string_view::npos) { return; }

    // Before the size: the style and weight keywords.
    const std::string_view before = font.substr(0, px);
    bold = before.find("bold") != std::string_view::npos;
    italic = before.find("italic") != std::string_view::npos ||
             before.find("oblique") != std::string_view::npos;

    // After it: the family list. The FIRST entry, which is what the rest of the
    // engine resolves too (layout takes the first name of the list as well).
    std::string_view rest = font.substr(px + 2);
    if (const std::size_t comma = rest.find(','); comma != std::string_view::npos) {
        rest = rest.substr(0, comma);
    }
    const std::size_t first = rest.find_first_not_of(" \t'\"");
    if (first == std::string_view::npos) { return; }
    const std::size_t last = rest.find_last_not_of(" \t'\"");
    family = rest.substr(first, last - first + 1);
}

node_id dom_bindings::handle_of(value v) {
    if (!v.is_object()) { return node_id{}; }
    auto * obj = static_cast<script::object_object *>(v.as_heap());
    const value * slot = obj->find(std::string{handle_property});
    return slot == nullptr ? node_id{}
                           : unpack(static_cast<std::uint64_t>(context::to_number(*slot)));
}

std::string dom_bindings::text_of(node_id id) const {
    const auto txn = doc_->read();
    std::string out;
    const auto walk = [&](auto && self, node_id at) -> void {
        out += txn.text(at);
        for (const node_id child : txn.children(at)) { self(self, child); }
    };
    walk(walk, id);
    return out;
}

void dom_bindings::set_text(node_id id, std::string text) {
    if (!id) { return; }
    // Copied before removing: children() is a view onto the live child
    // list, and removing while iterating it is a use-after-free waiting for
    // the second child.
    std::vector<node_id> existing;
    {
        const auto txn = doc_->read();
        for (const node_id child : txn.children(id)) { existing.push_back(child); }
    }
    for (const node_id child : existing) { (void)doc_->remove_child(child); }
    if (const node_id created = doc_->create_text(text)) { (void)doc_->append_child(id, created); }
    mutated();
}

void dom_bindings::edit_classes(node_id id, const std::string & name, bool add) {
    if (!id || name.empty()) { return; }
    const auto txn = doc_->read();
    std::vector<std::string> classes;
    for (const std::string_view cls : split(txn.attribute_value(id, atoms_->intern("class")))) {
        if (cls != name) { classes.emplace_back(cls); }
    }
    if (add) { classes.push_back(name); }
    std::string joined;
    for (const std::string & cls : classes) {
        if (!joined.empty()) { joined += ' '; }
        joined += cls;
    }
    (void)doc_->set_attribute(id, atoms_->intern("class"), joined);
    mutated();
}

std::vector<std::string_view> dom_bindings::split(std::string_view text) {
    std::vector<std::string_view> out;
    std::size_t at = 0;
    while (at < text.size()) {
        while (at < text.size() && text[at] == ' ') { ++at; }
        const std::size_t start = at;
        while (at < text.size() && text[at] != ' ') { ++at; }
        if (at > start) { out.push_back(text.substr(start, at - start)); }
    }
    return out;
}

void dom_bindings::mutated() {
    if (on_mutation_) { on_mutation_(); }
}

void dom_bindings::install_console(context & cx) {
    auto * console = static_cast<script::object_object *>(cx.make_object().as_heap());
    const auto log = [this](context & c, std::span<value> args) {
        std::string line;
        for (std::size_t i = 0; i < args.size(); ++i) {
            if (i > 0) { line += ' '; }
            line += c.to_string(args[i]);
        }
        console_.push_back(std::move(line));
        return value::undefined();
    };
    console->set("log", value::object(cx.allocate<script::native_object>("log", log)));
    console->set("warn", value::object(cx.allocate<script::native_object>("warn", log)));
    console->set("error", value::object(cx.allocate<script::native_object>("error", log)));
    cx.define_global("console", value::object(console));
}

void dom_bindings::install_document(context & cx) {
    auto * doc = static_cast<script::object_object *>(cx.make_object().as_heap());
    const auto method = [&](std::string name, script::native_fn fn) {
        doc->set(name, value::object(cx.allocate<script::native_object>(name, std::move(fn))));
    };

    method("getElementById", [this](context & c, std::span<value> args) {
        return wrap(c, find_by_id(arg_string(c, args, 0)));
    });
    method("createElement", [this](context & c, std::span<value> args) {
        // NOT a mutation: a created element is detached and changes nothing
        // on screen until it is appended.
        return wrap(c, doc_->create_element(atoms_->intern_lower(arg_string(c, args, 0))));
    });
    method("createTextNode", [this](context & c, std::span<value> args) {
        return wrap(c, doc_->create_text(arg_string(c, args, 0)));
    });
    method("addEventListener", [this](context & c, std::span<value> args) {
        listeners_.push_back(listener{node_id{}, arg_string(c, args, 0), arg(args, 1)});
        return value::undefined();
    });
    method("getElementsByTagName", [this](context & c, std::span<value> args) {
        const std::vector<node_id> found = all_by_tag(arg_string(c, args, 0));
        auto * list = static_cast<script::object_object *>(c.make_object().as_heap());
        // An ARRAY-SHAPED object: the VM has no Array, so a live collection is
        // indices plus a length, which is what `for (i = 0; i < n; i++)` - the
        // way every page walks one - actually reads.
        for (std::size_t i = 0; i < found.size(); ++i) {
            list->set(std::to_string(i), wrap(c, found[i]));
        }
        list->set("length", value::number(static_cast<double>(found.size())));
        return value::object(list);
    });

    doc->set("body", wrap(cx, find_by_tag("body")));
    doc->set("documentElement", wrap(cx, find_by_tag("html")));
    document_ = value::object(doc);
    cx.define_global("document", document_);
    refresh_document();
}

// The document's own live properties. `body` and `documentElement` can be set
// once because the node never changes; these cannot - a title is rewritten by
// script and the focused element changes on every click - so they are pushed
// again whenever the wrappers are, exactly as location.href is.
void dom_bindings::refresh_document() {
    auto * doc = document_object();
    if (doc == nullptr || cx_ == nullptr) { return; }
    doc->set("title", cx_->string(text_of(find_by_tag("title"))));
    doc->set("activeElement", wrap(*cx_, focused_));
}

void dom_bindings::install_navigation(context & cx) {
    cx.define_native("alert", [this](context & c, std::span<value> args) {
        const std::string message = arg_string(c, args, 0);
        if (on_alert_) { on_alert_(message); }
        return value::undefined();
    });
    cx.define_global("location", location_);
    // `document.location` and `window.location` are the SAME object as the
    // global one, not three copies - a page reads whichever it learned, and
    // they have to agree.
    if (auto * doc = document_object()) { doc->set("location", location_); }
    if (auto * window = window_object()) { window->set("location", location_); }
}

value dom_bindings::make_location(context & cx) {
    auto * loc = static_cast<script::object_object *>(cx.make_object().as_heap());
    const auto method = [&](std::string name, script::native_fn fn) {
        loc->set(name, value::object(cx.allocate<script::native_object>(name, std::move(fn))));
    };
    method("reload", [this](context &, std::span<value>) {
        reload_requested_ = true;
        return value::undefined();
    });
    method("toString", [this](context & c, std::span<value>) { return c.string(location_href_); });
    loc->set("href", cx.string(location_href_));
    loc->set("hash", cx.string(location_hash_));
    return value::object(loc);
}

script::object_object * dom_bindings::document_object() {
    return document_.is_object() ? static_cast<script::object_object *>(document_.as_heap())
                                 : nullptr;
}

script::object_object * dom_bindings::window_object() {
    return window_.is_object() ? static_cast<script::object_object *>(window_.as_heap()) : nullptr;
}

void dom_bindings::install_window(context & cx) {
    auto * window = static_cast<script::object_object *>(cx.make_object().as_heap());
    window->set("innerWidth", value::number(viewport_width_));
    window->set("innerHeight", value::number(viewport_height_));
    window->set("devicePixelRatio", value::number(1));
    window->set("addEventListener",
                value::object(cx.allocate<script::native_object>(
                    "addEventListener", [this](context & c, std::span<value> args) {
                        listeners_.push_back(
                            listener{node_id{}, arg_string(c, args, 0), arg(args, 1)});
                        return value::undefined();
                    })));
    auto * performance = static_cast<script::object_object *>(cx.make_object().as_heap());
    performance->set(
        "now", value::object(cx.allocate<script::native_object>(
                   "now", [this](context &, std::span<value>) { return value::number(now_ms_); })));
    window->set("performance", value::object(performance));
    window_ = value::object(window);
    cx.define_global("window", window_);
    cx.define_global("performance", value::object(performance));
}

void dom_bindings::install_resources(context & cx) {
    cx.define_native("loadImage", [this](context & c, std::span<value> args) {
        if (assets_ == nullptr || images_ == nullptr) { return value::number(-1); }
        return value::number(images_->handle_for(*assets_, arg_string(c, args, 0)));
    });
    cx.define_native("imageWidth", [this](context &, std::span<value> args) {
        const auto image =
            images_ != nullptr ? images_->at(static_cast<int>(arg_number(args, 0))) : nullptr;
        return value::number(image ? image->width : 0);
    });
    cx.define_native("imageHeight", [this](context &, std::span<value> args) {
        const auto image =
            images_ != nullptr ? images_->at(static_cast<int>(arg_number(args, 0))) : nullptr;
        return value::number(image ? image->height : 0);
    });

    // fetch(url) -> a settled Response. The registry is consulted FIRST, so
    // a page that ships its resources never opens a socket and a test run
    // is reproducible; a miss goes to the network when the application
    // allows it.
    cx.define_native("fetch", [this](context & c, std::span<value> args) {
        const std::string url = arg_string(c, args, 0);
        return fetch_now(c, url);
    });
}

value dom_bindings::fetch_now(context & cx, const std::string & url) {
    std::vector<std::byte> body;
    int status = 200;
    std::string type;
    std::string failure;

    if (assets_ != nullptr && assets_->contains(url)) {
        const std::span<const std::byte> baked = assets_->find(url);
        body.assign(baked.begin(), baked.end());
    } else if (url.find("://") == std::string::npos && assets_ != nullptr) {
        // A relative url is a file next to the page, which is what a
        // page-local `fetch("data.json")` means.
        body = assets_->load(url);
        if (body.empty()) {
            status = 404;
            failure = "no such resource: " + url;
        }
    } else if (network_allowed_) {
        const http_response response = http_get(url);
        status = response.status;
        type = response.content_type;
        body = std::move(response.body);
        if (!response.completed()) { failure = response.error; }
    } else {
        failure = "network access is off and " + url + " was not baked in";
    }

    if (!failure.empty()) {
        // A network failure REJECTS, which is what a page's catch branch is
        // written for. A 404 does not - it is a Response with ok false.
        return make_rejection(cx, failure);
    }
    return make_response(cx, url, status, type, std::move(body));
}

value dom_bindings::make_rejection(context & cx, const std::string & message) {
    auto * error = static_cast<script::object_object *>(cx.make_object().as_heap());
    error->set("message", cx.string(message));
    error->set("name", cx.string("TypeError"));
    return cx.make_promise(value::object(error), true);
}

void dom_bindings::install_timers(context & cx) {
    cx.define_native("setTimeout", [this](context &, std::span<value> args) {
        return value::number(add_timer(arg(args, 0), arg_number(args, 1), false));
    });
    cx.define_native("setInterval", [this](context &, std::span<value> args) {
        return value::number(add_timer(arg(args, 0), arg_number(args, 1), true));
    });
    const auto cancel = [this](context &, std::span<value> args) {
        const auto id = static_cast<std::uint32_t>(arg_number(args, 0));
        for (timer & t : timers_) {
            if (t.id == id) { t.cancelled = true; }
        }
        return value::undefined();
    };
    cx.define_native("clearTimeout", cancel);
    cx.define_native("clearInterval", cancel);
    cx.define_native("requestAnimationFrame", [this](context &, std::span<value> args) {
        animation_callbacks_.push_back(arg(args, 0));
        return value::number(++next_timer_id_);
    });
}

std::uint32_t dom_bindings::add_timer(value callback, double delay_ms, bool repeating) {
    const std::uint32_t id = ++next_timer_id_;
    timers_.push_back(timer{id, callback, now_ms_ + std::max(0.0, delay_ms),
                            std::max(0.0, delay_ms), repeating, false});
    return id;
}

value dom_bindings::make_event(context & cx, std::string_view type, node_id target) {
    auto * event = static_cast<script::object_object *>(cx.make_object().as_heap());
    event->set("type", cx.string(std::string{type}));
    event->set("target", wrap(cx, target));
    event->set("defaultPrevented", value::boolean(false));
    // One SHARED event object per dispatch, so preventDefault called by any
    // listener is visible to the browser and to every later listener - which
    // is what makes it mean anything at all.
    event->set("preventDefault", value::object(cx.allocate<script::native_object>(
                                     "preventDefault", [](context & c, std::span<value>) {
                                         const value self = c.current_this();
                                         if (self.is_object()) {
                                             static_cast<script::object_object *>(self.as_heap())
                                                 ->set("defaultPrevented", value::boolean(true));
                                         }
                                         return value::undefined();
                                     })));
    return value::object(event);
}

bool dom_bindings::prevented(value event) {
    if (!event.is_object()) { return false; }
    const value * slot =
        static_cast<script::object_object *>(event.as_heap())->find("defaultPrevented");
    return slot != nullptr && context::truthy(*slot);
}

void dom_bindings::fire_at(node_id target, std::string_view type, value event) {
    for (const listener & l : listeners_) {
        if (l.target == target && l.type == type) {
            (void)cx_->call(l.callback, std::span<const value>{&event, 1});
        }
    }
}

void dom_bindings::fire_global(std::string_view type, value event) {
    for (const listener & l : listeners_) {
        if (!l.target && l.type == type) {
            (void)cx_->call(l.callback, std::span<const value>{&event, 1});
        }
    }
}

node_id dom_bindings::find_by_id(const std::string & want) {
    const auto txn = doc_->read();
    const atom key = atoms_->intern("id");
    node_id found{};
    const auto walk = [&](auto && self, node_id at) -> void {
        if (!found && txn.attribute_value(at, key) == want) { found = at; }
        for (const node_id child : txn.children(at)) { self(self, child); }
    };
    walk(walk, txn.root());
    return found;
}

std::vector<node_id> dom_bindings::all_by_tag(std::string_view tag) {
    const auto txn = doc_->read();
    // "*" is every ELEMENT, which is how a page asks for the whole document.
    const bool every = tag == "*";
    const atom want = every ? atom{} : atoms_->intern_lower(tag);
    std::vector<node_id> found;
    const auto walk = [&](auto && self, node_id at) -> void {
        if (const auto tagged = txn.tag(at); tagged && (every || *tagged == want)) {
            found.push_back(at);
        }
        for (const node_id child : txn.children(at)) { self(self, child); }
    };
    walk(walk, txn.root());
    return found;
}

node_id dom_bindings::find_by_tag(std::string_view tag) {
    const auto txn = doc_->read();
    const atom want = atoms_->intern_lower(tag);
    node_id found{};
    const auto walk = [&](auto && self, node_id at) -> void {
        if (!found && txn.tag(at).value_or(atom{}) == want) { found = at; }
        for (const node_id child : txn.children(at)) { self(self, child); }
    };
    walk(walk, txn.root());
    return found;
}

} // namespace ctbrowser::shell
