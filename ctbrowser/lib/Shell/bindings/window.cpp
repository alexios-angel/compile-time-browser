// dom_bindings - window, console, timers and the callback queue.
//
// One of six files carved out of a 3,926-line bindings.cpp on 2026-08-09.
// These are all member functions of one class declared in
// include/ctbrowser/shell/bindings.hpp, so they split across translation
// units with nothing to declare and no linkage to arrange.

#include <ctbrowser/core/algorithms.hpp>
#include <ctbrowser/shell/bindings.hpp>
#include <ctbrowser/shell/net/url.hpp>

#include <numbers>

// dom_bindings' method bodies - the API a page's script actually calls.
//
// The header lists what a page can reach; this is how each one works.

namespace ctbrowser::shell {

void dom_bindings::set_alert_hook(std::function<void(const std::string &)> hook) {
    on_alert_ = std::move(hook);
}

std::size_t dom_bindings::run_due_callbacks() {
    if (cx_ == nullptr) { return 0; }
    std::size_t ran = 0;
    // FETCHES FIRST, so a handler waiting on one runs in the same turn as the
    // timers rather than a turn behind them. Copied before running, because a
    // handler resolved by one may start another.
    if (!fetches_.empty()) {
        std::vector<pending_fetch> due;
        due.swap(fetches_);
        for (const pending_fetch & waiting : due) {
            settle_fetch(*cx_, waiting);
            note_callback_fault("fetch");
            ++ran;
        }
    }
    // IMAGE LOADS with them, and for the same reason: p5's loadImage awaits a
    // fetch and then awaits an image load, so a turn that ran one but not the
    // other would need two ticks per image instead of one.
    if (!image_loads_.empty()) {
        std::vector<pending_image> due;
        due.swap(image_loads_);
        for (const pending_image & waiting : due) {
            settle_image(*cx_, waiting);
            note_callback_fault("image load");
            ++ran;
        }
    }
    // FileReader results, beside the image loads and for the same reason.
    if (!reads_.empty()) {
        std::vector<pending_read> due;
        due.swap(reads_);
        for (const pending_read & waiting : due) {
            settle_read(*cx_, waiting);
            note_callback_fault("FileReader");
            ++ran;
        }
    }
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
        note_callback_fault("setTimeout");
        ++ran;
    }
    std::erase_if(timers_, [](const timer & t) { return t.cancelled; });
    // AFTER THE TIMERS, BEFORE THE ANIMATION FRAMES. That is where a browser
    // puts its microtask checkpoint, and the ordering is observable: a promise
    // resolved by a timer must have run its handlers before the frame that
    // follows draws.
    cx_->drain_microtasks();

    std::vector<value> frame_callbacks;
    frame_callbacks.swap(animation_callbacks_);
    for (const value & cb : frame_callbacks) {
        const value ms = value::number(now_ms_);
        (void)cx_->call(cb, std::span<const value>{&ms, 1});
        note_callback_fault("requestAnimationFrame");
        ++ran;
    }
    cx_->drain_microtasks();
    note_callback_fault("microtask");
    // THE FRAME IS OVER, SO THE CANVAS GETS ITS PIXELS. A WebGL context draws
    // into a GL surface the compositor cannot see; this is the one copy back,
    // and here is the only place that is a FRAME rather than a draw. The old
    // engine did it after every drawArrays - 267 full-surface copies for one
    // Babylon frame - and the rewrite moved it here and then, for one commit,
    // called it from nowhere at all.
    present_webgl_contexts();
    return ran;
}

// A FAULT IN A CALLBACK IS REPORTED AND CLEARED, not left set.
//
// `context::run` clears the failure flag on entry, so a fault in a <script> is
// reported once and forgotten. `call` has no such entry point, so a fault in
// the first animation frame stayed set for the life of the page: every later
// callback was refused and the page silently stopped moving. p5.js drives its
// entire draw loop through requestAnimationFrame, so that is one faulting frame
// between a sketch that runs and a sketch that renders one frame and dies with
// no message.
//
// Clearing matches a browser, where an exception in one callback cancels
// neither the rest of the queue nor the next frame.
void dom_bindings::note_callback_fault(std::string_view source) {
    if (cx_ == nullptr || !cx_->failed()) { return; }
    const std::string message = std::string{source} + " callback: " + cx_->take_error();
    // The FIRST one is kept: a loop that faults every frame would otherwise
    // replace the original diagnosis with the thousandth copy of it.
    if (callback_error_.empty()) { callback_error_ = message; }
    ++callback_faults_;
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
    // EVERY name a page calls, all writing to the one list. `console.debug` was
    // absent, and a missing console method is worse than a silent one: p5's own
    // error REPORTER calls it, so a library that had something to say about a
    // failed load threw a second error on top of the first and the real message
    // was never printed. A page's diagnostics must not be able to fail.
    //
    // The grouping and timing ones exist and do nothing, deliberately: a page
    // calls them for a console a test has no way to look at, and throwing is the
    // only outcome that would change what the page does.
    for (const char * name : {"log", "warn", "error", "debug", "info", "trace", "dir"}) {
        console->set(name, value::object(cx.allocate<script::native_object>(name, log)));
    }
    const auto ignore = [](context &, std::span<value>) { return value::undefined(); };
    for (const char * name :
         {"group", "groupEnd", "groupCollapsed", "table", "time", "timeEnd", "assert", "count"}) {
        console->set(name, value::object(cx.allocate<script::native_object>(name, ignore)));
    }
    cx.define_global("console", value::object(console));
}

void dom_bindings::install_window(context & cx) {
    auto * window = static_cast<script::object_object *>(cx.make_object().as_heap());
    window->set("innerWidth", value::number(viewport_width_));
    window->set("innerHeight", value::number(viewport_height_));
    window->set("devicePixelRatio", value::number(1));
    // ON THE WINDOW AND AS A BARE GLOBAL, which is one function reachable by two
    // names rather than two functions. The window IS the global object in a
    // browser, so `addEventListener("error", f)` with no receiver is the same
    // call as `window.addEventListener(...)` - and the proxy's fallback only
    // goes one way, from `window.x` down to a global, never from a bare `x` up
    // to a window property.
    //
    // WPT is what found it. testharness.js installs its uncaught-exception
    // handler as `addEventListener("error", ...)` inside
    // `(function (global_scope) { ... }(self))`, so a missing bare name threw a
    // TypeError at the very end of the harness's own initialisation - after the
    // asserts were exposed and BEFORE `on_tests_ready()`, which is the worst
    // possible place: the file half-ran and said nothing. `getComputedStyle`
    // already works exactly this way and says so.
    const value add_listener = value::object(cx.allocate<script::native_object>(
        "addEventListener", [this](context & c, std::span<value> args) {
            listeners_.push_back(make_listener(c, node_id{}, args));
            return value::undefined();
        }));
    const value remove_listener = value::object(cx.allocate<script::native_object>(
        "removeEventListener", [this](context & c, std::span<value> args) {
            const std::string type = arg_string(c, args, 0);
            const value callback = arg(args, 1);
            std::erase_if(listeners_, [&](const listener & l) {
                return !l.target && l.type == type && l.callback.bits() == callback.bits();
            });
            return value::undefined();
        }));
    window->set("addEventListener", add_listener);
    window->set("removeEventListener", remove_listener);
    cx.define_global("addEventListener", add_listener);
    cx.define_global("removeEventListener", remove_listener);
    // `localStorage`, IN MEMORY AND FOR THIS PAGE ONLY. 38 uses in p5.js, which
    // reads it before it draws anything.
    //
    // TODO: persist per origin once there IS an origin to scope a store to.
    // Not persisted, deliberately: a test that leaves state behind fails the
    // next run for reasons that have nothing to do with the code, and this
    // engine has no origin to scope a store to anyway. A page gets a working
    // store that starts empty every time, which is what the API promises minus
    // the durability.
    const auto storage = [&](const char * name) {
        auto * store = static_cast<script::object_object *>(cx.make_object().as_heap());
        auto * items = static_cast<script::object_object *>(cx.make_object().as_heap());
        const value backing = value::object(items);
        store->set("__items", backing);
        const auto method = [&](std::string method_name, script::native_fn fn) {
            store->set(method_name, value::object(cx.allocate<script::native_object>(
                                        method_name, std::move(fn))));
        };
        method("getItem", [items](context & c, std::span<value> args) {
            value * held = items->find(arg_string(c, args, 0));
            // A MISS IS null, not undefined - pages branch on `=== null`.
            return held == nullptr ? value::null() : *held;
        });
        method("setItem", [items](context & c, std::span<value> args) {
            items->set(arg_string(c, args, 0), c.string(arg_string(c, args, 1)));
            return value::undefined();
        });
        method("removeItem", [items](context & c, std::span<value> args) {
            (void)items->erase(arg_string(c, args, 0));
            return value::undefined();
        });
        method("clear", [items](context &, std::span<value>) {
            items->props.clear();
            items->index.clear();
            return value::undefined();
        });
        method("key", [items](context & c, std::span<value> args) {
            const auto at =
                static_cast<std::size_t>(std::max(0.0, script::context::to_number(arg(args, 0))));
            return at < items->props.size() ? c.string(items->props[at].first) : value::null();
        });
        store->define_accessor("length",
                               value::object(cx.allocate<script::native_object>(
                                   "length",
                                   [items](context &, std::span<value>) {
                                       return value::number(
                                           static_cast<double>(items->props.size()));
                                   })),
                               value::undefined());
        window->set(name, value::object(store));
        cx.define_global(name, value::object(store));
    };
    storage("localStorage");
    storage("sessionStorage");

    // `new AbortController()`. p5.js makes one in its constructor and passes
    // its signal to every listener it installs, so that removing a sketch can
    // remove them all at once.
    //
    // The signal is carried and honoured by removeEventListener via `abort`;
    // TODO: aborting should cancel an in-flight fetch once fetch stops blocking
    // the frame. Today there is nothing in flight to cancel.
    // what is NOT modelled is aborting an in-flight fetch, because a fetch here
    // does not overlap with anything. A page that aborts one gets a request
    // that already finished, which is a difference worth knowing about.
    cx.define_native("AbortController", [this](context & c, std::span<value>) {
        auto * controller = static_cast<script::object_object *>(c.make_object().as_heap());
        auto * signal = static_cast<script::object_object *>(c.make_object().as_heap());
        signal->set("aborted", value::boolean(false));
        signal->set("reason", value::undefined());
        const value signal_value = value::object(signal);
        controller->set("signal", signal_value);
        controller->set("abort",
                        value::object(c.allocate<script::native_object>(
                            "abort", [this, signal_value](context & inner, std::span<value> args) {
                                auto * s =
                                    static_cast<script::object_object *>(signal_value.as_heap());
                                s->set("aborted", value::boolean(true));
                                s->set("reason", arg(args, 0));
                                // Every listener registered with this signal goes.
                                std::erase_if(listeners_, [&](const listener & l) {
                                    return l.abort_signal.is_heap() &&
                                           l.abort_signal.as_heap() == signal_value.as_heap();
                                });
                                (void)inner;
                                return value::undefined();
                            })));
        return value::object(controller);
    });

    // `new Event(type)` and `window.dispatchEvent(event)`.
    //
    // p5.js finishes starting by announcing itself - `window.dispatchEvent(new
    // Event('p5Ready'))` - and a page that listens for that is how anything
    // else on the page knows p5 is ready. Both were absent, so the library
    // could not complete its own bootstrap.
    //
    // Dispatch here is FLAT: a window event runs the window's listeners for
    // that type, in registration order. There is no capture phase and no
    // bubbling, because a window event has nowhere to bubble from.
    // `Blob`, `URL.createObjectURL` and base64.
    //
    // ONE PIECE OF MACHINERY SERVING TWO FEATURES. `loadImage` fetches bytes,
    // wraps them in a Blob, makes an object URL and points an Image at it; and
    // `save()` wraps bytes in a Blob, makes an object URL and clicks an
    // `<a download>`. Neither works without this and both work with it.
    //
    // AN OBJECT URL IS AN ASSET. Rather than a private table that only images
    // consult, `createObjectURL` registers the bytes in the asset registry under
    // a synthetic `blob:` name - so every path that already resolves a URL
    // resolves this one: `<img src>`, `fetch()`, and p5's loaders. One mechanism
    // instead of three, and nothing had to learn a new kind of URL.
    // A PROTOTYPE, so `x instanceof Blob` is true. p5's downloadFile branches on
    // exactly that - `if (!(saveData instanceof Blob)) saveData = new Blob([data])`
    // - and got the wrong branch, wrapping a Blob in another Blob and copying
    // every byte of an exported image for nothing.
    auto * blob_proto = static_cast<script::object_object *>(cx.make_object().as_heap());
    blob_prototype_ = value::object(blob_proto);
    const value blob_prototype = blob_prototype_;
    auto * blob_ctor = cx.allocate<script::native_object>(
        "Blob", [blob_prototype](context & c, std::span<value> a) {
            auto * blob = static_cast<script::object_object *>(c.make_object().as_heap());
            blob->prototype = blob_prototype;
            // `new Blob([parts], { type })`. A part is a string or something with
            // bytes - a typed array, another Blob - which is what a page actually
            // passes: `new Blob([data], { type: contentType })`.
            value bytes = c.make_array();
            auto * out = static_cast<script::array_object *>(bytes.as_heap());
            out->elements = script::element_kind::u8;
            if (!a.empty() && a[0].is_array()) {
                for (const value & part :
                     static_cast<script::array_object *>(a[0].as_heap())->items) {
                    if (part.is_string()) {
                        for (const char ch : c.to_string(part)) {
                            out->items.push_back(
                                value::number(static_cast<double>(static_cast<unsigned char>(ch))));
                        }
                        continue;
                    }
                    // A typed array is bytes already; a Blob carries them in the
                    // same slot this one does.
                    value inner = part;
                    if (part.is_object()) { inner = c.lookup_property(part, "__bytes"); }
                    if (inner.is_array()) {
                        for (const value & b :
                             static_cast<script::array_object *>(inner.as_heap())->items) {
                            out->items.push_back(b);
                        }
                    }
                }
            }
            std::string type;
            if (a.size() > 1 && a[1].is_object()) {
                const value given = c.lookup_property(a[1], "type");
                if (!given.is_undefined()) { type = c.to_string(given); }
            }
            blob->set("size", value::number(static_cast<double>(out->items.size())));
            blob->set("type", c.string(type));
            blob->set("__bytes", bytes);
            return value::object(blob);
        });
    blob_ctor->set("prototype", blob_prototype_);
    cx.define_global("Blob", value::object(blob_ctor));

    // --- the DOM's interface objects -----------------------------------
    //
    // `window.HTMLCanvasElement` and friends. A library uses them two ways and
    // both have to work: FEATURE DETECTION, which is why Phaser refused to
    // start at all - `Features.canvas = !!window['CanvasRenderingContext2D']`
    // and then `if (!Features.canvas) throw` - and IDENTITY, `el instanceof
    // HTMLCanvasElement`, which Phaser asks nine times and p5 four.
    //
    // A bare marker object would answer the first and make the second silently
    // false, so each of these carries a real prototype and the objects that are
    // instances are linked to it. They are NOT constructible: `new
    // HTMLCanvasElement()` throws in a browser too, and saying so is better
    // than handing back an object that is not an element.
    const auto interface_object = [&cx](const char * name, value & prototype_out) {
        auto * prototype = static_cast<script::object_object *>(cx.make_object().as_heap());
        prototype_out = value::object(prototype);
        auto * ctor =
            cx.allocate<script::native_object>(name, [name](context & c, std::span<value>) {
                c.throw_error("TypeError", std::string{"Illegal constructor: "} + name +
                                               " cannot be constructed by a page");
                return value::undefined();
            });
        ctor->set("prototype", prototype_out);
        cx.define_global(name, value::object(ctor));
    };
    interface_object("HTMLCanvasElement", canvas_element_prototype_);
    interface_object("HTMLImageElement", image_element_prototype_);
    interface_object("CanvasRenderingContext2D", canvas2d_prototype_);
    interface_object("WebGLRenderingContext", webgl_prototype_);
    interface_object("WebGL2RenderingContext", webgl2_prototype_);

    // `File`, `FileList` and `FileReader` - THE INPUT SIDE of the machinery that
    // already does export.
    //
    // p5's createFileInput refuses to build anything unless all four of
    // `window.File`, `window.FileReader`, `window.FileList` and `window.Blob`
    // exist, so their absence made it return undefined and a sketch's drag-and-drop
    // never happened. More than that, FileReader is how any page reads a file a
    // user gave it: `reader.onload = e => ...; reader.readAsText(file)`.
    //
    // NOTHING HERE CAN OPEN A FILE PICKER, and it does not pretend to: an <input
    // type=file> has an EMPTY FileList, because there is no user to choose with.
    // What works is everything a page does with a file it already has - construct
    // one, read it, hand it to createObjectURL - which is what a test, a
    // drag-and-drop shim and p5's own loader path all need.
    {
        // A File IS a Blob with a name, so it shares the prototype and
        // `file instanceof Blob` is true - which is what p5's downloadFile
        // branches on.
        auto * file_ctor =
            cx.allocate<script::native_object>("File", [](context & c, std::span<value> a) {
                const value blob_ctor_value = c.global("Blob");
                // Built THROUGH Blob rather than beside it: one place decides what
                // a part list means, so a File made of strings, typed arrays and
                // other Blobs behaves exactly as a Blob made the same way.
                value parts[2] = {arg(a, 0), arg(a, 2)};
                const value made = blob_ctor_value.is_callable()
                                       ? c.construct(blob_ctor_value, parts)
                                       : c.make_object();
                if (!made.is_object()) { return made; }
                auto * file = static_cast<script::object_object *>(made.as_heap());
                file->set("name", c.string(a.size() > 1 ? c.to_string(a[1]) : std::string{}));
                // The clock, not zero: a page that sorts by lastModified would
                // otherwise find every file identical.
                file->set("lastModified", value::number(c.clock_ms()));
                return made;
            });
        file_ctor->set("prototype", blob_prototype_);
        cx.define_global("File", value::object(file_ctor));

        // A FileList is array-SHAPED: indices, a length, and item(). Every page
        // walks one with `for (const f of files)` or `files[0]`, and both of those
        // an array already answers.
        cx.define_native("FileList", [](context & c, std::span<value>) {
            auto * list = static_cast<script::object_object *>(c.make_object().as_heap());
            list->set("length", value::number(0));
            list->set("item", value::object(c.allocate<script::native_object>(
                                  "item", [](context & inner, std::span<value> a) {
                                      const value self = inner.current_this();
                                      return inner.lookup_index(self, arg(a, 0));
                                  })));
            return value::object(list);
        });

        // `new FileReader()`. The result arrives on a LATER TURN, through the same
        // queue an image load uses - a page assigns `onload` after calling
        // `readAsText`, and a reader that delivered synchronously would fire before
        // the handler existed.
        cx.define_native("FileReader", [this](context & c, std::span<value>) {
            auto * reader = static_cast<script::object_object *>(c.make_object().as_heap());
            reader->set("result", value::null());
            reader->set("error", value::null());
            reader->set("readyState", value::number(0)); // EMPTY
            const value self = value::object(reader);
            const auto read = [this, self](const char * name, read_kind kind) {
                return std::pair<std::string, script::native_fn>{
                    name, [this, self, kind](context & inner, std::span<value> a) {
                        auto * target = static_cast<script::object_object *>(self.as_heap());
                        target->set("readyState", value::number(1)); // LOADING
                        reads_.push_back(pending_read{self, arg(a, 0), kind});
                        (void)inner;
                        return value::undefined();
                    }};
            };
            for (const auto & [name, fn] :
                 {read("readAsText", read_kind::text), read("readAsDataURL", read_kind::data_url),
                  read("readAsArrayBuffer", read_kind::array_buffer),
                  read("readAsBinaryString", read_kind::binary_string)}) {
                reader->set(name, value::object(c.allocate<script::native_object>(name, fn)));
            }
            reader->set("abort", value::object(c.allocate<script::native_object>(
                                     "abort", [this, self](context &, std::span<value>) {
                                         std::erase_if(reads_, [&](const pending_read & r) {
                                             return r.reader.is_object() && self.is_object() &&
                                                    r.reader.as_heap() == self.as_heap();
                                         });
                                         return value::undefined();
                                     })));
            return self;
        });
    }

    {
        auto * url = static_cast<script::object_object *>(cx.make_object().as_heap());
        const auto url_method = [&](std::string name, script::native_fn fn) {
            url->set(name, value::object(cx.allocate<script::native_object>(name, std::move(fn))));
        };
        url_method("createObjectURL", [this](context & c, std::span<value> a) {
            if (assets_ == nullptr || a.empty() || !a[0].is_object()) { return c.string(""); }
            const value held = c.lookup_property(a[0], "__bytes");
            std::vector<std::byte> bytes;
            if (held.is_array()) {
                for (const value & b : static_cast<script::array_object *>(held.as_heap())->items) {
                    bytes.push_back(static_cast<std::byte>(
                        static_cast<unsigned char>(std::clamp(context::to_number(b), 0.0, 255.0))));
                }
            }
            // Counted rather than random: `Math.random` is seeded here so a
            // golden can exist, and a URL that changed between runs would defeat
            // that for any page that prints one.
            const std::string name = "blob:ctbrowser/" + std::to_string(++next_object_url_);
            assets_->add(name, std::move(bytes));
            return c.string(name);
        });
        url_method("revokeObjectURL", [this](context & c, std::span<value> a) {
            // Replaced with nothing rather than erased: the registry has no
            // remove, and an empty entry is indistinguishable from a missing one
            // to every reader. A page that revokes and then loads gets the 404
            // it should.
            //
            // An image ALREADY DECODED from this URL survives, because
            // image_store caches by name. That is not an accident to be tidied
            // up - it is the browser's own rule, that revoking frees the bytes
            // and not the decoded image, and p5's loadImage depends on it: it
            // revokes inside onload and draws the image on the next line.
            if (assets_ != nullptr) { assets_->add(arg_string(c, a, 0), {}); }
            return value::undefined();
        });
        cx.define_global("URL", value::object(url));
    }

    // `new Image()`, which is a detached <img>.
    //
    // See install_image_views for why that is the whole implementation: every
    // path that already handles an <img> - src reflection, image_argument,
    // drawImage, appendChild, the CSS box - handles this one with no changes.
    // `new Image(w, h)` sets the presentational size, which p5 uses when it
    // copies a region out of a canvas.
    cx.define_native("Image", [this](context & c, std::span<value> a) {
        const value wrapper = wrap(c, doc_->create_element(atoms_->intern("img")));
        if (!wrapper.is_object()) { return wrapper; }
        for (const auto & [index, name] :
             std::initializer_list<std::pair<std::size_t, const char *>>{{0, "width"},
                                                                         {1, "height"}}) {
            if (a.size() > index) { c.store_property(wrapper, name, a[index]); }
        }
        return wrapper;
    });

    // `new DOMParser().parseFromString(text, type)`.
    //
    // p5's loadXML fetches the text and hands it to one of these, so an absent
    // DOMParser made loadXML fail on its first line - and p5's SVG path and its
    // FileReader-based XML loader use it too.
    //
    // Built on the fragment parse `innerHTML` already does: the markup is parsed
    // into a DETACHED subtree hung off a synthetic root, and the root is wrapped
    // like any other element. Everything p5.XML walks - children, attributes,
    // textContent, tagName, getElementsByTagName, appendChild - is then the
    // ordinary element surface, with nothing XML-specific to maintain.
    //
    // THE DEVIATION, and it is real: this is the HTML parser, not an XML one. Tag
    // names are lowercased, HTML's implied elements apply, and a malformed
    // document is repaired rather than rejected - where XML would report an error.
    // For the documents p5 reads (a flat tree of elements with attributes and
    // text) the two agree, and writing a second parser to disagree in different
    // ways would be worse than saying this.
    cx.define_native("DOMParser", [this](context & c, std::span<value>) {
        auto * parser = static_cast<script::object_object *>(c.make_object().as_heap());
        parser->set("parseFromString",
                    value::object(c.allocate<script::native_object>(
                        "parseFromString", [this](context & inner, std::span<value> a) {
                            // A synthetic root, so the result has ONE element to be the
                            // document element even when the source has several - which is
                            // what `documentElement` means and what p5 walks from.
                            const node_id root =
                                doc_->create_element(atoms_->intern("ctbrowser-document"));
                            set_inner_html(root, arg_string(inner, a, 0));
                            const value wrapper = wrap(inner, root);
                            if (!wrapper.is_object()) { return wrapper; }
                            auto * document_like =
                                static_cast<script::object_object *>(wrapper.as_heap());
                            // The document surface over the same node: `documentElement`
                            // is the first child if there is one, and the root otherwise.
                            value first = wrapper;
                            {
                                const auto txn = doc_->read();
                                const std::span<const node_id> kids = txn.children(root);
                                if (!kids.empty()) { first = wrap(inner, kids.front()); }
                            }
                            document_like->set("documentElement", first);
                            // A page checks this to decide whether the parse worked. It is
                            // always empty here because the HTML parser repairs rather than
                            // rejects - said in the comment above rather than pretended
                            // otherwise.
                            document_like->set("parsererror", value::null());
                            return wrapper;
                        })));
        return value::object(parser);
    });

    // `new Path2D()` - a path recorded now and drawn later.
    //
    // Every 2D shape p5.js draws goes through one: a visitor walks the shape
    // and emits path verbs, and the renderer hands the result to fill() or
    // stroke(). So this is not an optional corner of the canvas API here, it is
    // the whole 2D drawing path.
    //
    // The verbs are kept in a script array (see path_commands_property) rather
    // than a C++ type: nothing but the canvas replay reads them, the GC already
    // traces arrays, and `new Path2D(other)` is then a copy of one vector.
    cx.define_native("Path2D", [](context & c, std::span<value> args) {
        auto * path = static_cast<script::object_object *>(c.make_object().as_heap());
        value commands = c.make_array();
        auto * steps = static_cast<script::array_object *>(commands.as_heap());
        // `new Path2D(other)` starts as a copy. p5 makes separate fill and
        // stroke paths from one built path exactly this way.
        if (!args.empty() && args[0].is_object()) {
            const value source = c.lookup_property(args[0], std::string{path_commands_property});
            if (source.is_array()) {
                steps->items = static_cast<script::array_object *>(source.as_heap())->items;
            }
        }
        path->set(std::string{path_commands_property}, commands);
        // Each verb records the letter and its numbers, which is the same shape
        // the canvas replay reads back.
        const auto record = [&](std::string name, std::string letter, std::size_t count) {
            path->set(name, value::object(c.allocate<script::native_object>(
                                name, [steps, letter, count](context & inner, std::span<value> a) {
                                    value step = inner.make_array();
                                    auto * parts =
                                        static_cast<script::array_object *>(step.as_heap());
                                    parts->items.push_back(inner.string(letter));
                                    for (std::size_t i = 0; i < count; ++i) {
                                        parts->items.push_back(value::number(arg_number(a, i)));
                                    }
                                    steps->items.push_back(step);
                                    return value::undefined();
                                })));
        };
        record("moveTo", "M", 2);
        record("lineTo", "L", 2);
        record("quadraticCurveTo", "Q", 4);
        record("bezierCurveTo", "C", 6);
        record("rect", "R", 4);
        record("arc", "A", 6);
        record("ellipse", "E", 8);
        record("closePath", "Z", 0);
        // `addPath(other, matrix)` APPLIES THE MATRIX. The verbs are copied
        // with their coordinates already transformed, which is what makes a
        // path built once reusable at several places - p5 passes one when it
        // clips, so this and clip() are the same feature arriving.
        //
        // A point-valued operand transforms exactly. An arc's or an ellipse's
        // RADII do not: a matrix with a skew turns a circle into an ellipse at
        // an angle, which these verbs cannot express. The centre is placed
        // correctly and the radii take the matrix's scale, which is right for
        // the translate/scale/rotate a page actually passes, and is written
        // down here rather than discovered.
        path->set("addPath",
                  value::object(c.allocate<script::native_object>(
                      "addPath", [steps](context & inner, std::span<value> a) {
                          if (a.empty() || !a[0].is_object()) { return value::undefined(); }
                          const value source =
                              inner.lookup_property(a[0], std::string{path_commands_property});
                          if (!source.is_array()) { return value::undefined(); }
                          const auto & from =
                              static_cast<script::array_object *>(source.as_heap())->items;
                          if (a.size() < 2 || !a[1].is_object()) {
                              steps->items.insert(steps->items.end(), from.begin(), from.end());
                              return value::undefined();
                          }
                          const auto part = [&](const char * name, double fallback) {
                              const value v = inner.lookup_property(a[1], name);
                              return v.is_undefined() ? fallback : context::to_number(v);
                          };
                          const double ma = part("a", 1), mb = part("b", 0);
                          const double mc = part("c", 0), md = part("d", 1);
                          const double me = part("e", 0), mf = part("f", 0);
                          // The scale each axis picks up, for the
                          // radius-valued operands.
                          const double sx = std::sqrt(ma * ma + mb * mb);
                          const double sy = std::sqrt(mc * mc + md * md);
                          for (const value & step : from) {
                              if (!step.is_array()) { continue; }
                              const auto & parts =
                                  static_cast<script::array_object *>(step.as_heap())->items;
                              if (parts.empty()) { continue; }
                              value moved = inner.make_array();
                              auto * out = static_cast<script::array_object *>(moved.as_heap());
                              const std::string verb = inner.to_string(parts[0]);
                              out->items.push_back(parts[0]);
                              const auto number = [&](std::size_t i) {
                                  return i < parts.size() ? context::to_number(parts[i]) : 0.0;
                              };
                              const auto push_point = [&](std::size_t i) {
                                  const double x = number(i);
                                  const double y = number(i + 1);
                                  out->items.push_back(value::number(ma * x + mc * y + me));
                                  out->items.push_back(value::number(mb * x + md * y + mf));
                              };
                              if (verb == "M" || verb == "L") {
                                  push_point(1);
                              } else if (verb == "Q") {
                                  push_point(1);
                                  push_point(3);
                              } else if (verb == "C") {
                                  push_point(1);
                                  push_point(3);
                                  push_point(5);
                              } else if (verb == "R") {
                                  push_point(1);
                                  out->items.push_back(value::number(number(3) * sx));
                                  out->items.push_back(value::number(number(4) * sy));
                              } else if (verb == "A") {
                                  push_point(1);
                                  out->items.push_back(value::number(number(3) * sx));
                                  for (std::size_t i = 4; i < parts.size(); ++i) {
                                      out->items.push_back(parts[i]);
                                  }
                              } else if (verb == "E") {
                                  push_point(1);
                                  out->items.push_back(value::number(number(3) * sx));
                                  out->items.push_back(value::number(number(4) * sy));
                                  for (std::size_t i = 5; i < parts.size(); ++i) {
                                      out->items.push_back(parts[i]);
                                  }
                              } else {
                                  for (std::size_t i = 1; i < parts.size(); ++i) {
                                      out->items.push_back(parts[i]);
                                  }
                              }
                              steps->items.push_back(moved);
                          }
                          return value::undefined();
                      })));
        return value::object(path);
    });

    // `new ImageData(w, h)` or `new ImageData(data, w, h)`. A page builds one
    // to hand to putImageData, and a filter that returns a fresh ImageData
    // rather than mutating in place - which is p5.js's own convention - needs
    // to be able to make one.
    cx.define_native("ImageData", [](context & c, std::span<value> args) {
        auto * out = static_cast<script::object_object *>(c.make_object().as_heap());
        // The array form comes FIRST, and the size arguments shift along - the
        // two overloads are told apart by whether argument 0 is a buffer.
        const bool given = !args.empty() && args[0].is_array();
        const int width = static_cast<int>(arg_number(args, given ? 1 : 0));
        int height = static_cast<int>(arg_number(args, given ? 2 : 1));
        value bytes = given ? args[0] : c.make_array();
        auto * store = static_cast<script::array_object *>(bytes.as_heap());
        if (given) {
            // Height is optional when the data is given: it follows from the
            // length, because the buffer is four bytes a pixel by definition.
            if (height <= 0 && width > 0) {
                height =
                    static_cast<int>(store->items.size() / (static_cast<std::size_t>(width) * 4));
            }
        } else {
            store->elements = script::element_kind::u8_clamped;
            store->items.assign(static_cast<std::size_t>(std::max(0, width)) *
                                    static_cast<std::size_t>(std::max(0, height)) * 4,
                                value::number(0));
        }
        out->set("width", value::number(std::max(0, width)));
        out->set("height", value::number(std::max(0, height)));
        out->set("data", bytes);
        return value::object(out);
    });

    cx.define_native("Event", [](context & c, std::span<value> args) {
        auto * event = static_cast<script::object_object *>(c.make_object().as_heap());
        event->set("type", c.string(arg_string(c, args, 0)));
        event->set("bubbles", value::boolean(false));
        event->set("cancelable", value::boolean(false));
        event->set("defaultPrevented", value::boolean(false));
        event->set("target", value::null());
        const auto no_op = [&](const char * name) {
            event->set(name,
                       value::object(c.allocate<script::native_object>(
                           name, [](context &, std::span<value>) { return value::undefined(); })));
        };
        no_op("preventDefault");
        no_op("stopPropagation");
        no_op("stopImmediatePropagation");
        return value::object(event);
    });
    window->set("dispatchEvent", value::object(cx.allocate<script::native_object>(
                                     "dispatchEvent", [this](context & c, std::span<value> args) {
                                         const value event = arg(args, 0);
                                         const std::string type =
                                             event.is_object()
                                                 ? c.to_string(c.lookup_property(event, "type"))
                                                 : c.to_string(event);
                                         // COPIED before dispatch: a listener may add or remove
                                         // one, and appending to the vector being walked
                                         // invalidates it.
                                         std::vector<value> callbacks;
                                         for (const listener & l : listeners_) {
                                             if (!l.target && l.type == type) {
                                                 callbacks.push_back(l.callback);
                                             }
                                         }
                                         const value one[1] = {event};
                                         for (const value & callback : callbacks) {
                                             if (callback.is_callable()) {
                                                 (void)c.call(callback, one);
                                             }
                                         }
                                         return value::boolean(true);
                                     })));

    // `navigator`. A page reads it to decide what it is running in, and
    // `navigator.userAgent.replace(...)` on an absent navigator is undefined
    // twice over before anything notices.
    //
    // The string names this engine rather than imitating a browser. A page that
    // sniffs for Chrome will not find it, which is correct: this is not Chrome,
    // and a page taking a Chrome-only path here would be worse served by a lie.
    {
        auto * navigator = static_cast<script::object_object *>(cx.make_object().as_heap());
        navigator->set("userAgent", cx.string("Mozilla/5.0 (compatible; ctbrowser)"));
        navigator->set("appVersion", cx.string("5.0 (compatible; ctbrowser)"));
        navigator->set("platform", cx.string("ctbrowser"));
        navigator->set("vendor", cx.string(""));
        navigator->set("language", cx.string("en-US"));
        navigator->set("onLine", value::boolean(network_allowed_));
        navigator->set("maxTouchPoints", value::number(0));
        navigator->set("hardwareConcurrency", value::number(1));
        auto * languages = static_cast<script::array_object *>(cx.make_array().as_heap());
        languages->items.push_back(cx.string("en-US"));
        navigator->set("languages", value::object(languages));
        // mediaDevices and getUserMedia are ABSENT rather than stubbed: a page
        // feature-detects them, and a stub that exists but cannot deliver a
        // stream fails later and worse than one that was never there.
        window->set("navigator", value::object(navigator));
        cx.define_global("navigator", value::object(navigator));
    }

    // `screen`. One window, and it is the viewport.
    {
        auto * screen = static_cast<script::object_object *>(cx.make_object().as_heap());
        screen->set("width", value::number(viewport_width_));
        screen->set("height", value::number(viewport_height_));
        screen->set("availWidth", value::number(viewport_width_));
        screen->set("availHeight", value::number(viewport_height_));
        screen->set("colorDepth", value::number(24));
        screen->set("pixelDepth", value::number(24));
        window->set("screen", value::object(screen));
        cx.define_global("screen", value::object(screen));
    }

    auto * performance = static_cast<script::object_object *>(cx.make_object().as_heap());
    performance->set(
        "now", value::object(cx.allocate<script::native_object>(
                   "now", [this](context &, std::span<value>) { return value::number(now_ms_); })));
    window->set("performance", value::object(performance));
    window_ = value::object(window);

    // WINDOW IS THE GLOBAL OBJECT, through a proxy rather than a copy.
    //
    // `globals_` stays the single storage and the window forwards to it, so the
    // two cannot drift - which a second table synchronised at some cadence
    // always eventually does. A miss on the window's own properties reads a
    // global, and a write to a name the window does not already own DEFINES
    // one.
    //
    // Both directions are load-bearing for p5.js. It calls
    // `window.requestAnimationFrame(...)`, which is an ordinary global here; and
    // in global mode it assigns `window.ellipse = ...` for every one of its
    // ~200 drawing functions, which the sketch then calls as bare `ellipse(...)`
    // - so a write that did not reach the globals would leave every one of them
    // undefined at the call site.
    //
    // The other direction matters just as much: `_globalInit` reads
    // `window.setup` to decide whether the sketch is in global mode, and a
    // sketch writes `function setup() {}` at its top level, which is a global.
    auto * window_handler = static_cast<script::object_object *>(cx.make_object().as_heap());
    const value window_target = window_;
    const auto window_trap = [&](std::string name, script::native_fn fn) {
        window_handler->set(name,
                            value::object(cx.allocate<script::native_object>(name, std::move(fn))));
    };
    window_trap("get", [](context & c, std::span<value> args) {
        if (args.size() < 2 || !args[0].is_object()) { return value::undefined(); }
        auto * target = static_cast<script::object_object *>(args[0].as_heap());
        const std::string name = c.to_string(args[1]);
        if (target->find(name) != nullptr || target->find_accessor(name) != nullptr) {
            return c.lookup_property(args[0], name);
        }
        // A GLOBAL, WHICH IS MOST OF WHY THIS PROXY EXISTS.
        if (c.has_global(name)) { return c.global(name); }
        // AND FAILING THAT, THE PROTOTYPE CHAIN - `window` is an ordinary
        // object as well as the global scope, so `window.hasOwnProperty(...)`
        // has to reach Object.prototype like any other object's would. Stopping
        // at the globals meant it read `undefined`, and that is the single most
        // common feature-detection idiom there is: Phaser asks
        // `window.hasOwnProperty('HTMLVideoElement')` before it will build a
        // texture, so every texture in the framework threw instead.
        return c.lookup_property(args[0], name);
    });
    window_trap("set", [](context & c, std::span<value> args) {
        if (args.size() < 3 || !args[0].is_object()) { return value::boolean(false); }
        auto * target = static_cast<script::object_object *>(args[0].as_heap());
        const std::string name = c.to_string(args[1]);
        // An own property of the window keeps its own storage - `innerWidth` is
        // refreshed on the object every frame, and routing it to the globals
        // would leave a read seeing the stale one.
        if (target->find(name) != nullptr || target->find_accessor(name) != nullptr) {
            c.store_property(args[0], name, args[2]);
        } else {
            c.define_global(name, args[2]);
        }
        return value::boolean(true);
    });
    window_trap("has", [](context & c, std::span<value> args) {
        if (args.size() < 2 || !args[0].is_object()) { return value::boolean(false); }
        auto * target = static_cast<script::object_object *>(args[0].as_heap());
        const std::string name = c.to_string(args[1]);
        return value::boolean(target->find(name) != nullptr ||
                              target->find_accessor(name) != nullptr || c.has_global(name));
    });
    const value window_view = value::object(
        cx.allocate<script::proxy_object>(window_target, value::object(window_handler)));
    cx.define_global("window", window_view);
    cx.define_global("globalThis", window_view);
    // `self`, WHICH IS THE SAME OBJECT AND WAS MISSING. It is what a script
    // that means to run in a window OR a worker names the global by, so a
    // library never writes `window` at all - and the first thing
    // web-platform-tests found here was that it is not defined.
    //
    // The failure it produced is the reason this line has a comment. Every
    // testharness.js test is `(function (global_scope) { ... expose(test,
    // "test") ... }(self))`, so `global_scope` was undefined, and this engine
    // treats a property STORE on undefined as a no-op rather than a TypeError -
    // so the harness ran to completion, reported no error, and defined not one
    // of its globals. The page then failed with "`test` is undefined", forty
    // lines from the cause. 100% of WPT was unrunnable for one missing alias.
    cx.define_global("self", window_view);
    cx.define_global("performance", value::object(performance));

    // THE BROWSING-CONTEXT CHAIN, WHICH TERMINATES HERE. This document is
    // always the top one: there are no iframes and no `window.open`, so `parent`
    // and `top` are this window and `opener` is null. That is not a stub - it is
    // what the specification says a top-level browsing context reports, and it
    // is the ANSWER rather than the absence of one.
    //
    // SET AFTER THE PROXY EXISTS, and to the PROXY, which is the whole subtlety:
    // scripts compare `w != w.parent` by identity, so handing them the raw
    // target object while `self` is the proxy makes the two differ and the walk
    // never terminates.
    //
    // WPT is what found it, and the failure was three layers from the cause.
    // testharness.js walks `[self ... top, opener]` to broadcast test state, and
    // with `parent` undefined the walk ran one step past the end and called
    // `postMessage` on `undefined`. That threw inside a COMPLETION CALLBACK -
    // and `Tests.prototype.notify_complete` runs its callbacks in a bare
    // `forEach` with no try/catch, so the throw killed every later callback
    // including the one that reports results. Every test that ran perfectly
    // reported nothing at all and was recorded as a timeout.
    //
    // `postMessage` is deliberately still absent. With this chain correct
    // nothing on a single-document page reaches it, and defining a fake one
    // would let tests that genuinely need a second browsing context appear to
    // work instead of failing honestly.
    window->set("parent", window_view);
    window->set("top", window_view);
    window->set("opener", value::null());
    window->set("frameElement", value::null());
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

} // namespace ctbrowser::shell
