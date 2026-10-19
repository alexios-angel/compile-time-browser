//===- window.cpp - The window object ---------------------------*- C++ -*-===//
//
// dom_bindings::install_window: the window object, the globals it carries, and
// the proxy that makes it the global object.
//
//===----------------------------------------------------------------------===//

#include <ctbrowser/shell/bindings.hpp>
#include <ctbrowser/shell/net/url.hpp>

#include "../image_data.hpp"
#include "internal.hpp"

namespace ctbrowser::shell {

/// Gives `window` its six BarProps (HTML 7.2.2.4): its own objects over the
/// shared BarProp.prototype `prototype`. Called for the page's window here and
/// for a frame's in install_frame_realm.
void install_bar_props(context & cx, script::object_object & window, value prototype) {
    for (const char * name :
         {"locationbar", "menubar", "personalbar", "scrollbars", "statusbar", "toolbar"}) {
        auto * bar = cx.allocate<script::object_object>();
        bar->prototype = prototype;
        window.set(name, value::object(bar));
    }
}

value dom_bindings::make_blob(context & cx, value bytes, std::string_view type) {
    auto * blob = cx.allocate<script::object_object>();
    if (blob_prototype_.is_object()) { blob->prototype = blob_prototype_; }
    const auto * items = static_cast<const script::array_object *>(bytes.as_heap());
    blob->set("size", value::number(static_cast<double>(items->items.size())));
    blob->set("type", cx.string(std::string{type}));
    blob->set("__bytes", bytes);
    return value::object(blob);
}

void dom_bindings::install_window(context & cx) {
    auto * window = cx.allocate<script::object_object>();
    window->set("innerWidth", value::number(viewport_width_));
    window->set("innerHeight", value::number(viewport_height_));
    window->set("devicePixelRatio", value::number(1));
    install_window_scrolling(cx,
                             *window); // scrollX/scrollY, scroll/scrollTo/scrollBy (scrolling.cpp)
    install_window_listeners(cx, *window);
    install_post_message(cx, *window);
    install_window_open(cx, *window); // window.open(), bindings/frames.cpp
    // The interface objects of the window's own members: Storage, Navigator,
    // BarProp and Location (HTML 12.2.1, 8.9.1, 7.2.2.4, 7.2.4). Each of those
    // objects is then an instance of its interface (`localStorage instanceof
    // Storage`), and a frame's `navigator` keeps its prototype through
    // document.open() (WPT no-new-global.window.js). None is constructible.
    // Their members stay own properties of the instances; Location's are
    // [LegacyUnforgeable], which is where WebIDL puts them anyway.
    const value storage_proto =
        value::object(make_interface(cx, "Storage", illegal_constructor).proto);
    const value navigator_proto =
        value::object(make_interface(cx, "Navigator", illegal_constructor).proto);
    static_cast<script::object_object *>(location_.as_heap())->prototype =
        value::object(make_interface(cx, "Location", illegal_constructor).proto);
    // `locationbar` and its five siblings: a BarProp each, with `visible` true
    // because this is not a popup (7.2.2.4).
    {
        auto * bar_prop = make_interface(cx, "BarProp", illegal_constructor).proto;
        bar_prop->define_accessor(
            "visible",
            native(cx, "visible", [](context &, std::span<value>) { return value::boolean(true); }),
            value::undefined(), script::attr_enumerable | script::attr_configurable);
        install_bar_props(cx, *window, value::object(bar_prop));
    }
    // `localStorage` and `sessionStorage`: in memory, per page, empty at every
    // load (p5.js reads localStorage before it draws anything). Deliberately
    // not persisted, so a test cannot leave state behind for the next run, and
    // because the engine has no origin to scope a store to.
    const auto storage = [&](const char * name) {
        auto * store = cx.allocate<script::object_object>();
        store->prototype = storage_proto;
        auto * items = cx.allocate<script::object_object>();
        const value backing = value::object(items);
        // The natives below hold `items` by raw pointer, so its one heap
        // reference must be a slot no page can delete or overwrite.
        store->define(CTBROWSER_PRIVATE_KEY("ctbrowser:storage-items"), backing, script::attr_none);
        set_method(cx, *store, "getItem", [items](context & c, std::span<value> args) {
            value * held = items->find(arg_string(c, args, 0));
            // A miss is null, not undefined: pages branch on `=== null`.
            return held == nullptr ? value::null() : *held;
        });
        set_method(cx, *store, "setItem", [items](context & c, std::span<value> args) {
            items->set(arg_string(c, args, 0), c.string(arg_string(c, args, 1)));
            return value::undefined();
        });
        set_method(cx, *store, "removeItem", [items](context & c, std::span<value> args) {
            (void)items->erase(arg_string(c, args, 0));
            return value::undefined();
        });
        set_method(cx, *store, "clear", [items](context &, std::span<value>) {
            items->props.clear();
            items->index.clear();
            return value::undefined();
        });
        set_method(cx, *store, "key", [items](context & c, std::span<value> args) {
            const auto at =
                static_cast<std::size_t>(std::max(0.0, script::context::to_number(arg(args, 0))));
            return at < items->props.size() ? c.string(items->props[at].first) : value::null();
        });
        auto * length =
            cx.allocate<script::native_object>("length", [items](context &, std::span<value>) {
                return value::number(static_cast<double>(items->props.size()));
            });
        store->define_accessor("length", value::object(length), value::undefined());
        // Each native retains it too: a page can hold `localStorage.getItem`
        // and delete the store itself. See native_object::retained.
        length->retained.push_back(backing);
        for (const char * method : {"getItem", "setItem", "removeItem", "clear", "key"}) {
            static_cast<script::native_object *>(store->find(method)->as_heap())
                ->retained.push_back(backing);
        }
        window->set(name, value::object(store));
        cx.define_global(name, value::object(store));
    };
    storage("localStorage");
    storage("sessionStorage");

    // `AbortController` and `AbortSignal` live in bindings/abort.cpp and are
    // installed after the event interfaces they build on.

    // `reportError(e)`, HTML 8.1.3.3: "report an exception" for a value the
    // page hands over, as if it had been thrown uncaught: an `error` event at
    // the window whose `error` is the value. The message is not read off the
    // value (its getters must not run; WPT reporterror.any.js checks this),
    // and the argument is required. The window is the one `this` names, so a
    // frame's method borrowed by the page reports in the frame (WPT
    // reporterror-cross-realm-method.html); a bare call uses the caller's.
    cx.define_native("reportError", [this](context & c, std::span<value> a) {
        if (a.empty()) {
            c.throw_error("TypeError", "reportError: 1 argument required, but only 0 present.");
            return value::undefined();
        }
        const value self = c.current_this();
        (void)bindings_of(self.is_heap() ? self : c.global_this())
            .dispatch_error_value("Uncaught exception", a[0]);
        return value::undefined();
    });
    // `print()` and `blur()` (HTML 8.8.1 and 7.2.2.1) do nothing here, which
    // the spec allows: the printing steps let a user agent with no printer (a
    // kiosk, a headless engine) return without printing, and so without
    // beforeprint/afterprint, and Window's blur() steps are "do nothing".
    // They exist because pages call them unconditionally; a frame's realm has
    // them too (WPT print-in-detached-frame.html borrows a detached frame's).
    for (const char * name : {"print", "blur"}) {
        cx.define_native(name, [](context &, std::span<value>) { return value::undefined(); });
    }

    // `Blob` and `URL.createObjectURL`.
    //
    // These serve two p5.js features: `loadImage` fetches bytes, wraps them in
    // a Blob, makes an object URL and points an Image at it, and `save()` wraps
    // bytes in a Blob, makes an object URL and clicks an `<a download>`.
    //
    // An object URL is an asset: `createObjectURL` registers the bytes in the
    // asset registry under a synthetic `blob:` name, so every path that already
    // resolves a URL (`<img src>`, `fetch()`, p5's loaders) resolves this one
    // without learning a new kind of URL.
    //
    // Blob has a real prototype so `x instanceof Blob` is true; p5's
    // downloadFile wraps anything that is not one in another Blob, copying
    // every byte of an exported image.
    auto * blob_proto = cx.allocate<script::object_object>();
    blob_prototype_ = value::object(blob_proto);
    auto * blob_ctor =
        cx.allocate<script::native_object>("Blob", [this](context & c, std::span<value> a) {
            // `new Blob([parts], { type })`. A part is a string or something
            // with bytes (a typed array, another Blob).
            value bytes = c.make_array();
            const context::rooted keep_bytes{c, bytes};
            auto * out = static_cast<script::array_object *>(bytes.as_heap());
            out->elements = script::element_kind::u8;
            if (!a.empty() && a[0].is_array()) {
                // A rooted copy: a part's `__bytes` getter or toString can grow
                // or shrink the page's array while this walks it.
                const std::vector<value> parts =
                    static_cast<script::array_object *>(a[0].as_heap())->items;
                const context::rooted_values keep_parts{c, parts};
                for (const value & part : parts) {
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
            return make_blob(c, bytes, type);
        });
    blob_ctor->set("prototype", blob_prototype_);
    cx.define_builtin_global("Blob", value::object(blob_ctor));

    // --- the DOM's interface objects -----------------------------------
    //
    // `window.CanvasRenderingContext2D` and friends; the element interfaces
    // (`HTMLCanvasElement` included) come from the table in
    // element/interfaces.cpp. Libraries use them two ways and both must work:
    // feature detection (Phaser throws at startup unless
    // `!!window['CanvasRenderingContext2D']`) and identity
    // (`ctx instanceof CanvasRenderingContext2D`).
    //
    // A bare marker object would satisfy the first and make the second silently
    // false, so each of these carries a real prototype and the objects that are
    // instances are linked to it. They are not constructible: `new
    // CanvasRenderingContext2D()` throws in a browser too.
    const auto interface_object = [&cx](const char * name, value & prototype_out) {
        auto * prototype = cx.allocate<script::object_object>();
        prototype_out = value::object(prototype);
        auto * ctor =
            cx.allocate<script::native_object>(name, [name](context & c, std::span<value>) {
                c.throw_error("TypeError", std::string{"Illegal constructor: "} + name +
                                               " cannot be constructed by a page");
                return value::undefined();
            });
        ctor->set("prototype", prototype_out);
        cx.define_builtin_global(name, value::object(ctor));
    };
    // `HTMLElement` is the one a page's class extends, so it is constructible
    // and lives with the custom element registry (bindings/custom_elements.cpp).
    // It has to exist before install_dom_interfaces builds the table around it.
    install_custom_elements(cx);
    interface_object("CanvasRenderingContext2D", canvas2d_prototype_);
    interface_object("WebGLRenderingContext", webgl_prototype_);
    interface_object("WebGL2RenderingContext", webgl2_prototype_);

    // `File`, `FileList` and `FileReader`: the input side of the Blob machinery
    // above.
    //
    // p5's createFileInput returns undefined unless all four of `window.File`,
    // `window.FileReader`, `window.FileList` and `window.Blob` exist, and
    // FileReader is how any page reads a file a user gave it:
    // `reader.onload = e => ...; reader.readAsText(file)`.
    //
    // Nothing here can open a file picker: an <input type=file> has an empty
    // FileList, because there is no user to choose with. What works is
    // everything a page does with a file it already has (construct one, read
    // it, hand it to createObjectURL), which is what a test, a drag-and-drop
    // shim and p5's own loader path need.
    {
        // A File is a Blob with a name: File.prototype inherits Blob's, so
        // `file instanceof Blob` is true (p5's downloadFile branches on it)
        // and a Blob is not a File (FileAPI 4.1; WPT structured-clone.any.js
        // checks `instanceof File` of a cloned Blob).
        auto * file_proto = cx.allocate<script::object_object>();
        file_proto->prototype = blob_prototype_;
        file_proto->define(CTBROWSER_SYMBOL_KEY("toStringTag"), cx.string("File"),
                           script::attr_configurable);
        const value file_prototype = value::object(file_proto);
        auto * file_ctor = cx.allocate<script::native_object>(
            "File", [file_prototype](context & c, std::span<value> a) {
                const value blob_ctor_value = c.global("Blob");
                // Built through Blob rather than beside it: one place decides what
                // a part list means, so a File made of strings, typed arrays and
                // other Blobs behaves exactly as a Blob made the same way.
                value parts[2] = {arg(a, 0), arg(a, 2)};
                const value made = blob_ctor_value.is_callable()
                                       ? c.construct(blob_ctor_value, parts)
                                       : c.make_object();
                if (!made.is_object()) { return made; }
                const context::rooted keep_made{c, made};
                auto * file = static_cast<script::object_object *>(made.as_heap());
                file->prototype = file_prototype;
                file->set("name", c.string(a.size() > 1 ? c.to_string(a[1]) : std::string{}));
                // The options' lastModified, a WebIDL `long long` (NaN and the
                // infinities are 0), else the clock rather than zero, so a page
                // that sorts by lastModified does not find every file
                // identical. ponytail: no modulo 2^64; a timestamp past 2^63
                // ms is not one a page makes.
                const value given = a.size() > 2 && a[2].is_object_like()
                                        ? c.lookup_property(a[2], "lastModified")
                                        : value::undefined();
                const double stamp =
                    given.is_undefined() ? c.clock_ms() : context::to_number(given);
                file->set("lastModified",
                          value::number(std::isfinite(stamp) ? std::trunc(stamp) + 0.0 : 0.0));
                return made;
            });
        // The prototype is in a C++ capture - see native_object::retained.
        file_ctor->retained.push_back(file_prototype);
        file_ctor->define("prototype", file_prototype, script::attr_none);
        file_proto->define("constructor", value::object(file_ctor), script::attr_builtin);
        cx.define_builtin_global("File", value::object(file_ctor));

        // A FileList is array-shaped: indices, a length, and item(). Pages walk
        // one with `for (const f of files)` or `files[0]`, and an array already
        // supports both.
        cx.define_native("FileList", [](context & c, std::span<value>) {
            auto * list = c.allocate<script::object_object>();
            list->set("length", value::number(0));
            list->set("item", value::object(c.allocate<script::native_object>(
                                  "item", [](context & inner, std::span<value> a) {
                                      const value self = inner.current_this();
                                      return inner.lookup_index(self, arg(a, 0));
                                  })));
            return value::object(list);
        });

        // `new FileReader()`. The result arrives on a later turn, through the
        // same queue an image load uses (`reads_`): a page assigns `onload`
        // after calling `readAsText`, and a reader that delivered synchronously
        // would fire before the handler existed.
        cx.define_native("FileReader", [this](context & c, std::span<value>) {
            auto * reader = c.allocate<script::object_object>();
            reader->set("result", value::null());
            reader->set("error", value::null());
            reader->set("readyState", value::number(0)); // FileReader.EMPTY
            const value self = value::object(reader);
            const auto read = [this, self](const char * name, read_kind kind) {
                return std::pair<std::string, script::native_fn>{
                    name, [this, self, kind](context & inner, std::span<value> a) {
                        auto * target = static_cast<script::object_object *>(self.as_heap());
                        target->set("readyState", value::number(1)); // FileReader.LOADING
                        reads_.push_back(pending_read{self, arg(a, 0), kind});
                        (void)inner;
                        return value::undefined();
                    }};
            };
            // Each method captures `self`, and a C++ capture is not a GC root
            // (see native_object::retained). Retaining it keeps the reader
            // alive for a page that holds only a detached method, e.g.
            // `const read = new FileReader().readAsText`.
            const auto install = [&](const std::string & name, const script::native_fn & fn) {
                auto * made = c.allocate<script::native_object>(name, fn);
                made->retained.push_back(self);
                reader->set(name, value::object(made));
            };
            for (const auto & [name, fn] :
                 {read("readAsText", read_kind::text), read("readAsDataURL", read_kind::data_url),
                  read("readAsArrayBuffer", read_kind::array_buffer),
                  read("readAsBinaryString", read_kind::binary_string)}) {
                install(name, fn);
            }
            install("abort", [this, self](context &, std::span<value>) {
                std::erase_if(reads_, [&](const pending_read & r) {
                    return r.reader.is_object() && self.is_object() &&
                           r.reader.as_heap() == self.as_heap();
                });
                return value::undefined();
            });
            return self;
        });
    }

    {
        // `URL` and `URLSearchParams` (bindings/window/url.cpp), over the same
        // parser `location` and every `src` resolve through. The File API's two
        // blob methods are added to the constructor here because they need the
        // bindings' asset registry.
        script::native_object * url = install_url(cx);
        // `TextEncoder`/`TextDecoder`, from bindings/window/encoding.cpp.
        install_encoding(cx);
        set_method(cx, *url, "createObjectURL", [this](context & c, std::span<value> a) {
            if (assets_ == nullptr || a.empty() || !a[0].is_object()) { return c.string(""); }
            const value held = c.lookup_property(a[0], "__bytes");
            std::vector<std::byte> bytes;
            if (held.is_array()) {
                for (const value & b : static_cast<script::array_object *>(held.as_heap())->items) {
                    bytes.push_back(static_cast<std::byte>(
                        static_cast<unsigned char>(std::clamp(context::to_number(b), 0.0, 255.0))));
                }
            }
            // Counted rather than random: `Math.random` is seeded here so
            // goldens are reproducible, and a URL that changed between runs
            // would break that for any page that prints one.
            dom_bindings & top = primary_ == nullptr ? *this : *primary_;
            const std::string name = "blob:ctbrowser/" + std::to_string(++top.next_object_url_);
            assets_->add(name, std::move(bytes));
            const value type = c.lookup_property(a[0], "type");
            top.object_url_types_.emplace_back(name, type.is_string() ? c.to_string(type)
                                                                      : std::string{});
            return c.string(name);
        });
        set_method(cx, *url, "revokeObjectURL", [this](context & c, std::span<value> a) {
            // Replaced with nothing rather than erased: the registry has no
            // remove, and an empty entry is indistinguishable from a missing one
            // to every reader. A page that revokes and then loads gets a 404.
            //
            // An image already decoded from this URL survives, because
            // image_store caches by name. That matches browsers (revoking frees
            // the bytes, not the decoded image), and p5's loadImage depends on
            // it: it revokes inside onload and draws the image on the next line.
            if (assets_ != nullptr) { assets_->add(arg_string(c, a, 0), {}); }
            return value::undefined();
        });
    }

    // `new Image()`, which is a detached <img>.
    //
    // See install_image_views for why that is the whole implementation: every
    // path that already handles an <img> (src reflection, image_argument,
    // drawImage, appendChild, the CSS box) handles this one with no changes.
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

    // `new Path2D()`, CanvasRenderingContext2D's members, CanvasGradient,
    // CanvasPattern and DOMMatrix: the 2D canvas, installed by
    // bindings/canvas.cpp. Every 2D shape p5.js draws goes through a Path2D.
    install_canvas_2d(cx);

    // `new ImageData(w, h)` or `new ImageData(data, w, h)`. A page builds one
    // to hand to putImageData, and a filter that returns a fresh ImageData
    // rather than mutating in place (p5.js's convention) needs to make one.
    cx.define_native("ImageData", [this](context & c, std::span<value> args) {
        const bool given = !args.empty() && args[0].is_array();
        // These constructor parameters are unsigned long, unlike the canvas
        // methods' [EnforceRange] long: use the VM's existing modulo conversion.
        const double width_number = c.to_number_value(arg(args, given ? 1 : 0));
        if (c.throw_pending()) { return value::undefined(); }
        const auto width = context::to_uint32(value::number(width_number));
        const bool height_given = !arg(args, given ? 2 : 1).is_undefined();
        const double height_number = c.to_number_value(arg(args, given ? 2 : 1));
        if (c.throw_pending()) { return value::undefined(); }
        std::uint64_t height = context::to_uint32(value::number(height_number));
        if (given && width != 0) {
            const auto length = static_cast<script::array_object *>(args[0].as_heap())->length();
            const auto row_bytes = std::uint64_t{width} * 4;
            const auto derived_height = length / row_bytes;
            if (length % row_bytes != 0 || (height_given && height != derived_height)) {
                throw_dom_exception(c, "IndexSizeError",
                                    "ImageData dimensions do not match its data");
                return value::undefined();
            }
            height = derived_height;
        }
        if (width == 0 || height == 0) {
            throw_dom_exception(c, "IndexSizeError", "ImageData dimensions must be nonzero");
            return value::undefined();
        }
        return detail::make_image_data(c, width, height, given ? args[0] : value::undefined());
    });
    // An interface object has a prototype object (WebIDL 3.7.3). instanceof is
    // OrdinaryHasInstance, so testing against a constructor without one throws
    // a TypeError, and p5's WebGL textures test `obj instanceof ImageData` on
    // every source. The objects each one makes inherit from it
    // (make_image_data). Path2D makes its own, methods and all (canvas_path2d.cpp).
    for (const char * name : {"ImageData"}) {
        const value ctor = cx.global(name);
        if (!ctor.is_kind(script::heap_kind::native)) { continue; }
        auto * proto = cx.allocate<script::object_object>();
        proto->define("constructor", ctor, script::attr_builtin);
        proto->define(CTBROWSER_SYMBOL_KEY("toStringTag"), cx.string(name),
                      script::attr_configurable);
        static_cast<script::native_object *>(ctor.as_heap())
            ->define("prototype", value::object(proto), script::attr_none);
    }

    // Event, CustomEvent, EventTarget and window.dispatchEvent are installed by
    // install_event_interfaces, which `install` calls after this function
    // because it needs the window object to exist.

    // `navigator`, which a page reads to decide what it is running in.
    //
    // The user agent names this engine rather than imitating a browser: a page
    // that sniffs for Chrome should not take a Chrome-only path here.
    {
        auto * navigator = cx.allocate<script::object_object>();
        navigator->prototype = navigator_proto;
        navigator->set("userAgent", cx.string("Mozilla/5.0 (compatible; ctbrowser)"));
        navigator->set("appVersion", cx.string("5.0 (compatible; ctbrowser)"));
        navigator->set("platform", cx.string("ctbrowser"));
        navigator->set("vendor", cx.string(""));
        navigator->set("language", cx.string("en-US"));
        navigator->set("onLine", value::boolean(network_allowed_));
        navigator->set("maxTouchPoints", value::number(0));
        navigator->set("hardwareConcurrency", value::number(1));
        install_user_activation(cx, *navigator);
        auto * languages = static_cast<script::array_object *>(cx.make_array().as_heap());
        languages->items.push_back(cx.string("en-US"));
        navigator->set("languages", value::object(languages));
        // The NavigatorID constants (HTML 8.9.1.1): every browser returns these
        // fixed strings because legacy sniffing reads them, and the
        // specification now requires them. `taintEnabled` is the legacy method
        // that is always false, `pdfViewerEnabled` is false here, and
        // `plugins`/`mimeTypes` are the two always-empty lists.
        navigator->set("appCodeName", cx.string("Mozilla"));
        navigator->set("appName", cx.string("Netscape"));
        navigator->set("product", cx.string("Gecko"));
        navigator->set("productSub", cx.string("20100101"));
        navigator->set("vendorSub", cx.string(""));
        navigator->set("pdfViewerEnabled", value::boolean(false));
        navigator->set("cookieEnabled", value::boolean(true));
        set_method(cx, *navigator, "taintEnabled",
                   [](context &, std::span<value>) { return value::boolean(false); });
        set_method(cx, *navigator, "javaEnabled",
                   [](context &, std::span<value>) { return value::boolean(false); });
        for (const char * list : {"plugins", "mimeTypes"}) {
            auto * empty = cx.allocate<script::object_object>();
            empty->set("length", value::number(0));
            set_method(cx, *empty, "item",
                       [](context &, std::span<value>) { return value::null(); });
            set_method(cx, *empty, "namedItem",
                       [](context &, std::span<value>) { return value::null(); });
            set_method(cx, *empty, "refresh",
                       [](context &, std::span<value>) { return value::undefined(); });
            navigator->set(list, value::object(empty));
        }
        // mediaDevices and getUserMedia are absent rather than stubbed: a page
        // feature-detects them, and a stub that cannot deliver a stream fails
        // later and worse than one that was never there.
        //
        // `oscpu` is the Gecko compatibility mode's NavigatorID member (HTML
        // 8.9.1.1). This engine's userAgent names neither Chrome nor WebKit, so
        // it is in that mode, and oscpu reports the platform.
        navigator->set("oscpu", cx.string("ctbrowser"));
        window->set("navigator", value::object(navigator));
        cx.define_global("navigator", value::object(navigator));
        // `clientInformation`, the legacy [Replaceable] alias: the same object.
        window->set("clientInformation", value::object(navigator));
        cx.define_global("clientInformation", value::object(navigator));
    }

    // `screen`: there is one window, so the screen is the viewport.
    {
        auto * screen = cx.allocate<script::object_object>();
        screen->set("width", value::number(viewport_width_));
        screen->set("height", value::number(viewport_height_));
        screen->set("availWidth", value::number(viewport_width_));
        screen->set("availHeight", value::number(viewport_height_));
        screen->set("colorDepth", value::number(24));
        screen->set("pixelDepth", value::number(24));
        window->set("screen", value::object(screen));
        cx.define_global("screen", value::object(screen));
    }

    script::object_object * performance = install_performance(cx);
    window->set("performance", value::object(performance));
    window_ = value::object(window);

    const value window_view = make_window_view(cx, window_, cx.current_realm());
    window_view_ = window_view;
    cx.define_global("window", window_view);
    // Select the realm receiver separately from its writable globalThis alias.
    cx.set_global_this(window_view);
    // `self` is the same object as `window`; libraries and testharness.js name
    // the global by it so they also run in workers.
    cx.define_global("self", window_view);
    cx.define_global("performance", value::object(performance));
    install_history(cx, *window); // `history`, window/history.cpp
    install_dom_parser(cx);       // `DOMParser`, defined at the end of this file

    // The page's window is the top-level browsing context, so `parent` and `top`
    // are this window and `opener` and `frameElement` are null (frames get their
    // own chain in install_frame_realm). They are set to the proxy, not the raw
    // target, because scripts walk `w != w.parent` by identity and a mismatch
    // would never terminate (testharness.js broadcasts this way).
    window->set("parent", window_view);
    window->set("top", window_view);
    window->set("opener", value::null());
    window->set("frameElement", value::null());
    // `frames` is the window itself (HTML 7.2.2.1), and `length` counts its
    // child navigables; the frame_at lookup in make_window_view is how
    // `frames[0]` reaches one.
    window->set("frames", window_view);
    window->define_accessor("length",
                            value::object(cx.allocate<script::native_object>(
                                "length",
                                [this](context &, std::span<value>) {
                                    return value::number(
                                        static_cast<double>(child_navigables().size()));
                                })),
                            value::undefined());
    // `self.origin` (HTML 7.2.2.1 WindowOrWorkerGlobalScope): the document's
    // origin serialised; "null" for a file: page, as location.origin says.
    window->define_accessor("origin",
                            value::object(cx.allocate<script::native_object>(
                                "origin",
                                [this](context & c, std::span<value>) {
                                    const std::string origin =
                                        location_parts(location_href_).origin;
                                    return c.string(origin.empty() ? "null" : origin);
                                })),
                            value::undefined());
}

/// Makes the window a realm's scripts see, which is also its global object: a
/// proxy over `target` (the object holding the window's own state). A property
/// the target does not own is looked up as a child frame index, then in
/// `realm`'s binding table, then among this document's named elements, then on
/// the prototype chain. The page's window is one (install_window) and every
/// frame that runs its own scripts has another (load_frame).
///
/// The traps hold their own realm: `frame.contentWindow.run` read by the page
/// is the frame's `run`, and `parent.x` read by a frame is the page's `x`, so a
/// trap reading "the current realm's" globals would answer about whoever asked.
/// The realm's undeclared-name hook is set here too, so a bare identifier in
/// this realm's code reaches this document's named elements.
value dom_bindings::make_window_view(context & cx, value target, script::realm_record & realm) {
    // The window is the global object through a proxy rather than a copy: the
    // realm's binding table stays the single storage and the window forwards
    // to it, so the two cannot drift apart. A miss on the window's own
    // properties reads a global, and a write to a name the window does not
    // already own defines one.
    //
    // p5.js relies on both directions. In global mode it assigns
    // `window.ellipse = ...` for each of its drawing functions, which the
    // sketch then calls as bare `ellipse(...)`; and `_globalInit` reads
    // `window.setup`, which a sketch defines as a top-level function, to
    // decide whether the sketch is in global mode.
    auto * window_handler = cx.allocate<script::object_object>();
    const value window_target = target;
    context::define_immutable_globals(*static_cast<script::object_object *>(target.as_heap()));
    script::realm_record * const realm_of = &realm;
    // Named access on the window (HTML 7.3.3): `window.someId`, and
    // `window.someName` for the few elements whose `name` attribute is exposed
    // that way. It is why an inline `onclick="doThing(theForm)"` works.
    //
    // The tag list is the specification's, not "any element with a name":
    // `<input name=q>` is not a named property of the window, and treating it
    // as one would shadow a global a page had defined. An <iframe name=x> is
    // there as a child navigable, and `window.x` returns its WindowProxy
    // (`contentWindow`), not the element (HTML 7.3.3, "determine the value of a
    // named property"; WPT nameditem-02.html).
    //
    // This walks the whole document, and runs on every `window.x` that is
    // neither an own property nor a global, which includes `window.hasOwnProperty`
    // and the rest of the prototype chain, because a named property beats
    // Object.prototype in the specification's order. ponytail: O(nodes) with no
    // cache, since a cache would need invalidating on every mutation; if it
    // shows up in a profile, add an id index on the document.
    const auto named_element = [this](std::string_view name) -> std::vector<node_id> {
        std::vector<node_id> found;
        if (name.empty()) { return found; }
        const auto txn = doc_->read();
        const atom id_attribute = atoms_->intern("id");
        const atom name_attribute = atoms_->intern("name");
        const auto exposes_name = [&](node_id node) {
            const atom tag = txn.tag(node).value_or(atom{});
            // `a`, `area` and `frameset` are not in the specification's list;
            // they stay because unittests/unit/tree_accessors.cpp pins
            // `anchor1` resolving to an `<a name=anchor1>`.
            for (const std::string_view exposed :
                 {"a", "area", "embed", "form", "frame", "frameset", "iframe", "img", "object"}) {
                if (tag == atoms_->intern_lower(exposed)) { return true; }
            }
            return false;
        };
        const auto walk = [&](auto && self, node_id at) -> void {
            if (txn.kind(at).value_or(node_kind::text) == node_kind::element) {
                if (txn.attribute_value(at, id_attribute) == name ||
                    (exposes_name(at) && txn.attribute_value(at, name_attribute) == name)) {
                    found.push_back(at);
                }
            }
            for (const node_id child : txn.children(at)) { self(self, child); }
        };
        walk(walk, txn.root());
        return found;
    };
    // One named property's value: a frame matched by its name is its window,
    // one element is itself, several are a collection.
    const auto named_value = [this, named_element](context & c, const std::string & name) {
        const std::vector<node_id> found = named_element(name);
        if (found.empty()) { return value::undefined(); }
        {
            const auto txn = doc_->read();
            for (const node_id node : found) {
                const atom tag = txn.tag(node).value_or(atom{});
                if ((tag == atoms_->intern_lower("iframe") ||
                     tag == atoms_->intern_lower("frame")) &&
                    txn.attribute_value(node, atoms_->intern("name")) == name) {
                    const value window = c.lookup_property(wrap(c, node), "contentWindow");
                    if (window.is_object_like()) { return window; }
                }
            }
        }
        if (found.size() == 1) { return wrap(c, found.front()); }
        return make_live_collection(c, [this, name, named_element] {
            (void)this;
            return named_element(name);
        });
    };
    // A bare identifier gets the same answer. HTML 7.3.3 makes an element with
    // an `id` a named property of the global object, and a bare identifier
    // resolves against that object, so `target1.style` with `target1` written
    // only in the markup is specified behaviour (WPT relies on it heavily).
    //
    // Only named elements are reached this way, not the whole get trap: a bare
    // identifier that reaches Object.prototype (`toString` with no receiver)
    // is not supported. See context::set_undeclared_name_hook.
    realm.undeclared_name = [this, named_value](std::string_view name) {
        if (cx_ == nullptr) { return value::undefined(); }
        return named_value(*cx_, std::string{name});
    };
    // `window[0]`, HTML 7.2.2.2: the child navigables in tree order, each
    // returned as its WindowProxy (WPT Node-removeChild reaches a frame's
    // document through `frames[0].document`). An index at or past `length` is
    // not a property at all.
    const auto frame_at = [this](context & c, std::string_view name) -> value {
        if (name.empty() || name.size() > 9 ||
            name.find_first_not_of("0123456789") != std::string_view::npos) {
            return value::undefined();
        }
        const std::vector<node_id> frames = child_navigables();
        const std::size_t index = static_cast<std::size_t>(std::stoul(std::string{name}));
        if (index >= frames.size()) { return value::undefined(); }
        return c.lookup_property(wrap(c, frames[index]), "contentWindow");
    };
    set_method(cx, *window_handler, "get",
               [named_value, frame_at, realm_of](context & c, std::span<value> args) {
                   if (args.size() < 2 || !args[0].is_object()) { return value::undefined(); }
                   auto * target = static_cast<script::object_object *>(args[0].as_heap());
                   const std::string name = c.to_string(args[1]);
                   // A script's `var`: the window's own property of that name is
                   // a placeholder (context::declare_global_var); the table has it.
                   if (realm_of->declared.contains(name)) { return realm_of->globals[name]; }
                   if (target->find(name) != nullptr || target->find_accessor(name) != nullptr) {
                       return c.lookup_property(args[0], name);
                   }
                   if (const value frame = frame_at(c, name); !frame.is_undefined()) {
                       return frame;
                   }
                   // A global, the main reason this proxy exists: from this
                   // window's realm, whichever realm is asking.
                   if (const auto it = realm_of->globals.find(name);
                       it != realm_of->globals.end() && !realm_of->lexical.contains(name)) {
                       return it->second;
                   }
                   // Then a named element, which comes before the prototype chain and
                   // after everything a page defined for itself. Several of one name
                   // are an HTMLCollection rather than the first of them, which is
                   // what makes `window.radios.length` work.
                   if (const value named = named_value(c, name); !named.is_undefined()) {
                       return named;
                   }
                   // Failing that, the prototype chain: `window` is an ordinary
                   // object as well as the global scope, so `window.hasOwnProperty(...)`
                   // must reach Object.prototype (Phaser checks
                   // `window.hasOwnProperty('HTMLVideoElement')` before building a
                   // texture).
                   return c.lookup_property(args[0], name);
               });
    set_method(cx, *window_handler, "set", [realm_of](context & c, std::span<value> args) {
        if (args.size() < 3 || !args[0].is_object()) { return value::boolean(false); }
        auto * target = static_cast<script::object_object *>(args[0].as_heap());
        const std::string name = c.to_string(args[1]);
        if (context::immutable_global(name)) { return value::boolean(false); }
        // An own property of the window keeps its own storage: `innerWidth` is
        // refreshed on the object every frame, and routing it to the globals
        // would leave a read seeing the stale one.
        if (realm_of->declared.contains(name)) {
            realm_of->globals[name] = args[2];
        } else if (realm_of->lexical.contains(name) || target->find(name) != nullptr ||
                   target->find_accessor(name) != nullptr) {
            c.store_property(args[0], name, args[2]);
        } else {
            realm_of->globals[name] = args[2];
        }
        return value::boolean(true);
    });
    set_method(cx, *window_handler, "has",
               [named_element, frame_at, realm_of](context & c, std::span<value> args) {
                   if (args.size() < 2 || !args[0].is_object()) { return value::boolean(false); }
                   const std::string name = c.to_string(args[1]);
                   // `'x' in window` must agree with `window.x`, or a page's feature
                   // detection and its use of the feature disagree. It must also agree
                   // with a bare `x`: global_or_named asks this before deciding a name
                   // is unresolvable, so the whole chain counts (`toString` is
                   // Object.prototype's, `addEventListener` is on the interface), and
                   // a named frame is a window property too.
                   return value::boolean(
                       c.has_property(args[0], args[1]) ||
                       (realm_of->globals.contains(name) && !realm_of->lexical.contains(name)) ||
                       !frame_at(c, name).is_undefined() || !named_element(name).empty());
               });
    // A descriptor for each, so `Object.getOwnPropertyDescriptor(window, 'x')`
    // and a hasOwnProperty that asks [[GetOwnProperty]] (ES 20.1.3.2) agree
    // with `has`. A global is an own data property of the Window (the globals
    // table is its property storage here), and a named element or frame is an
    // HTML 7.3.3 named property: enumerable, configurable, not writable.
    set_method(
        cx, *window_handler, "getOwnPropertyDescriptor",
        [named_value, frame_at, realm_of](context & c, std::span<value> args) {
            if (args.size() < 2 || !args[0].is_object()) { return value::undefined(); }
            const std::string name = c.to_string(args[1]);
            // A script's `var` is not configurable (16.1.7), and the
            // window owns a placeholder of it so the proxy may say so.
            if (realm_of->declared.contains(name)) {
                return c.from_property_descriptor(context::property_descriptor::data(
                    realm_of->globals[name], script::attr_writable | script::attr_enumerable));
            }
            context::property_descriptor own;
            if (c.own_property(args[0], name, own)) { return c.from_property_descriptor(own); }
            // An interface object or an intrinsic is not enumerable; the
            // Window's own members and a page's assignments are
            // (realm_record::builtin).
            if (const auto it = realm_of->globals.find(name);
                it != realm_of->globals.end() && !realm_of->lexical.contains(name)) {
                return c.from_property_descriptor(context::property_descriptor::data(
                    it->second, realm_of->builtin.contains(name)
                                    ? script::attr_writable | script::attr_configurable
                                    : script::attr_default));
            }
            value held = frame_at(c, name);
            if (held.is_undefined()) { held = named_value(c, name); }
            if (held.is_undefined()) { return value::undefined(); }
            return c.from_property_descriptor(context::property_descriptor::data(
                held, script::attr_enumerable | script::attr_configurable));
        });
    // `delete window.x` removes an own property of the window, or else the
    // global binding, as `set` writes it. A script's `var` has a
    // non-configurable placeholder own property (context::declare_global_var),
    // so deleting it fails (16.1.7).
    set_method(cx, *window_handler, "deleteProperty",
               [realm_of](context & c, std::span<value> args) {
                   if (args.size() < 2 || !args[0].is_object()) { return value::boolean(true); }
                   const std::string name = c.to_string(args[1]);
                   context::property_descriptor own;
                   if (c.own_property(args[0], name, own)) {
                       return value::boolean(c.delete_own_property(args[0], name));
                   }
                   if (!realm_of->lexical.contains(name)) {
                       (void)realm_of->globals.erase(name);
                       (void)realm_of->builtin.erase(name);
                   }
                   return value::boolean(true);
               });
    return value::object(
        cx.allocate<script::proxy_object>(window_target, value::object(window_handler)));
}

/// Installs `addEventListener`/`removeEventListener` for this document's
/// window: the page's from install_window, a frame's from install_frame_realm,
/// so a frame script's `addEventListener("load", f)` listens at the frame, not
/// at the page.
void dom_bindings::install_window_listeners(context & cx, script::object_object & window) {
    // One function under two names: a window property and a bare global. The
    // window proxy only forwards `window.x` to globals, never a bare `x` to a
    // window property, so a bare `addEventListener(...)` (as testharness.js
    // calls it) needs its own global binding.
    const value add_listener = value::object(cx.allocate<script::native_object>(
        "addEventListener", [this](context & c, std::span<value> args) {
            this->add_listener(make_listener(c, path_step{node_id{}, listen_on::window}, args));
            return value::undefined();
        }));
    // A listener is identified by (type, callback, capture), as add_listener
    // registers it. The third argument is a boolean or a dictionary; a
    // dictionary's `capture` is read either way, because pages detect
    // dictionary support by watching that getter run.
    const value remove_listener = value::object(cx.allocate<script::native_object>(
        "removeEventListener", [this](context & c, std::span<value> args) {
            const std::string type = arg_string(c, args, 0);
            const value callback = arg(args, 1);
            const value options = arg(args, 2);
            const bool capture = options.is_object()
                                     ? context::truthy(c.lookup_property(options, "capture"))
                                     : context::truthy(options);
            listeners_.remove({listen_on::window, 0}, type, callback.bits(), capture);
            return value::undefined();
        }));
    window.set("addEventListener", add_listener);
    window.set("removeEventListener", remove_listener);
    cx.define_global("addEventListener", add_listener);
    cx.define_global("removeEventListener", remove_listener);
}

namespace {

// The window a DOMParser was made for, and DOMParser.prototype stored on the
// page's window so the frames' constructors share it.
const std::string parser_window_slot = std::string{script::private_key_prefix} + "DOMParser:window";
const std::string parser_prototype_slot = std::string{script::private_key_prefix} + "DOMParser";

} // namespace

/// Installs `DOMParser` (HTML 8.5.1): a constructor that requires `new`, with
/// parseFromString on its prototype. Its documents are "a new Document whose
/// URL is this's relevant global object's associated Document's URL", so a
/// parser remembers the window it was made for, each window's constructor
/// makes its own (`new frame.contentWindow.DOMParser()` is the frame's), and
/// the method, whichever realm's it is, parses as the parser's document (WPT
/// DOMParser-parseFromString-url.html). text/html uses the HTML parser and the
/// four XML types the XML parser (a `<parsererror>` when the markup is not
/// well-formed); any other type is a TypeError, as for a WebIDL enumeration.
/// p5's loadXML and its SVG path use it.
///
/// Installs into the realm current when it is called: the page's from
/// install_window, a frame's from install_frame_realm, each after window_view_
/// is set to its window.
void dom_bindings::install_dom_parser(context & cx) {
    dom_bindings * const top = &primary();
    script::object_object * page_window = top->window_object();
    const value * held_prototype =
        page_window == nullptr ? nullptr : page_window->find(parser_prototype_slot);
    script::native_object * ctor = nullptr;
    if (held_prototype != nullptr && held_prototype->is_object()) {
        ctor = cx.allocate<script::native_object>("DOMParser", script::native_fn{});
        ctor->define("length", value::number(0), script::attr_configurable);
        ctor->define("prototype", *held_prototype, script::attr_none);
        cx.define_builtin_global("DOMParser", value::object(ctor));
    } else {
        const interface_pair made = make_interface(cx, "DOMParser");
        ctor = made.ctor;
        if (page_window != nullptr) {
            page_window->define(parser_prototype_slot, value::object(made.proto),
                                script::attr_none);
        }
        set_operation(
            cx, *made.proto, "parseFromString", 2, [top](context & c, std::span<value> a) {
                const value self = c.current_this();
                const value * window = self.is_object()
                                           ? static_cast<script::object_object *>(self.as_heap())
                                                 ->find(parser_window_slot)
                                           : nullptr;
                if (window == nullptr) {
                    c.throw_error("TypeError", "Illegal invocation");
                    return value::undefined();
                }
                const std::string markup = c.to_string(a[0]);
                if (c.throw_pending()) { return value::undefined(); }
                const std::string type = c.to_string(a[1]);
                if (c.throw_pending()) { return value::undefined(); }
                for (const std::string_view known : {"text/html", "text/xml", "application/xml",
                                                     "application/xhtml+xml", "image/svg+xml"}) {
                    if (type != known) { continue; }
                    // A parser whose window has gone (a removed frame's) parses
                    // as the page's, since that document is gone too.
                    dom_bindings * owner = top->find_bindings_of(*window);
                    return (owner == nullptr ? *top : *owner).parse_from_string(c, markup, type);
                }
                c.throw_error("TypeError",
                              "DOMParser.parseFromString: '" + type + "' is not a supported type");
                return value::undefined();
            });
    }
    const value window = window_view_;
    ctor->fn = [ctor, window](context & c, std::span<value>) {
        const value self = c.current_this();
        if (!self.is_object() || !c.instance_of(self, value::object(ctor))) {
            c.throw_error("TypeError", "DOMParser constructor: 'new' is required");
            return value::undefined();
        }
        static_cast<script::object_object *>(self.as_heap())
            ->define(parser_window_slot, window, script::attr_none);
        return self;
    };
    // The window is in a C++ capture, which the collector cannot see, so the
    // constructor retains it.
    ctor->retained.push_back(window);
}

} // namespace ctbrowser::shell
