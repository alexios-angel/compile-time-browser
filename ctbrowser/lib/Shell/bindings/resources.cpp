// dom_bindings - fetch, images and the asset registry.
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

void dom_bindings::observe_resources(asset_registry & assets, image_store & images) {
    assets_ = &assets;
    images_ = &images;
}

// An `<img>`, which `new Image()` also is.
//
// THE DECISION THAT MADE THE REST FALL OUT: `new Image()` builds a real detached
// <img> element rather than a parallel Image type. So `src` is already a
// reflected attribute, `image_argument` already resolves it, drawImage already
// accepts it, appendChild already displays it, and the CSS box already sizes it.
// A separate object would have needed every one of those taught about it.
//
// What an <img> needs beyond a plain element is the LOADING surface: a src whose
// assignment starts work, a size that falls back to the decoded pixels, and the
// two ways a page learns the load finished.
void dom_bindings::install_image_views(context & cx, script::object_object & obj, node_id id) {
    auto * wrapper = &obj;
    // `naturalWidth` is the DECODED size and `width` is the attribute when there
    // is one, the decoded size otherwise. p5's loadImage reads `img.width`
    // straight after the load to size its own canvas, so answering 0 there gave
    // a zero-by-zero p5.Image that drew nothing.
    const auto decoded = [this, id]() -> std::shared_ptr<const paint::bitmap> {
        if (images_ == nullptr || assets_ == nullptr) { return nullptr; }
        const auto txn = doc_->read();
        const std::string_view src = txn.attribute_value(id, atoms_->intern("src"));
        return src.empty() ? nullptr : images_->load(*assets_, src);
    };
    const auto size_view = [&](std::string property, bool natural_only, bool horizontal) {
        obj.define_accessor(property,
                            value::object(cx.allocate<script::native_object>(
                                property,
                                [this, id, property, natural_only, horizontal,
                                 decoded](context &, std::span<value>) {
                                    if (!natural_only) {
                                        const auto txn = doc_->read();
                                        const std::string_view text =
                                            txn.attribute_value(id, atoms_->intern(property));
                                        double parsed = 0;
                                        bool any = false;
                                        for (const char c : text) {
                                            if (c < '0' || c > '9') { break; }
                                            parsed = parsed * 10 + (c - '0');
                                            any = true;
                                        }
                                        if (any) { return value::number(parsed); }
                                    }
                                    const std::shared_ptr<const paint::bitmap> image = decoded();
                                    if (!image) { return value::number(0); }
                                    return value::number(static_cast<double>(
                                        horizontal ? image->width : image->height));
                                })),
                            value::object(cx.allocate<script::native_object>(
                                property, [this, id, property](context & c, std::span<value> a) {
                                    (void)doc_->set_attribute(
                                        id, atoms_->intern(property),
                                        std::to_string(static_cast<long long>(arg_number(a, 0))));
                                    mutated();
                                    (void)c;
                                    return value::undefined();
                                })));
    };
    size_view("width", false, true);
    size_view("height", false, false);
    size_view("naturalWidth", true, true);
    size_view("naturalHeight", true, false);

    // `src` REFLECTS, and assigning it STARTS A LOAD. The generic reflection two
    // screens up would have given the first half only: the attribute changed and
    // nothing ever told the page the pixels had arrived.
    obj.define_accessor(
        "src",
        value::object(cx.allocate<script::native_object>(
            "src",
            [this, id](context & c, std::span<value>) {
                const auto txn = doc_->read();
                return c.string(std::string{txn.attribute_value(id, atoms_->intern("src"))});
            })),
        value::object(cx.allocate<script::native_object>(
            "src", [this, id, wrapper](context & c, std::span<value> a) {
                const std::string url = arg_string(c, a, 0);
                (void)doc_->set_attribute(id, atoms_->intern("src"), url);
                mutated();
                begin_image_load(value::object(wrapper), id, url, value::undefined());
                return value::undefined();
            })));

    // `complete` is what a page checks before waiting: p5 and many others do
    // `if (img.complete) use(img); else img.onload = ...`, and a `complete` that
    // is always false makes the second branch the only one, forever.
    obj.define_accessor("complete",
                        value::object(cx.allocate<script::native_object>(
                            "complete",
                            [decoded](context &, std::span<value>) {
                                return value::boolean(decoded() != nullptr);
                            })),
                        value::undefined());

    // `decode()` - the promise-shaped way to wait, and the one that does not
    // need a handler property. Already decoded resolves on the next turn rather
    // than immediately, because a promise that settles inside the call it was
    // created by is not one.
    obj.set("decode",
            value::object(cx.allocate<script::native_object>(
                "decode", [this, id, wrapper](context & c, std::span<value>) {
                    const value promise = c.make_pending_promise();
                    const auto txn = doc_->read();
                    begin_image_load(value::object(wrapper), id,
                                     std::string{txn.attribute_value(id, atoms_->intern("src"))},
                                     promise);
                    return promise;
                })));
}

void dom_bindings::begin_image_load(value target, node_id id, std::string url, value promise) {
    image_loads_.push_back(pending_image{target, id, std::move(url), promise});
}

// One FileReader read, delivered. `result` is set BEFORE the handlers run,
// because every one of them reads it.
void dom_bindings::settle_read(context & cx, const pending_read & waiting) {
    if (!waiting.reader.is_object()) { return; }
    auto * reader = static_cast<script::object_object *>(waiting.reader.as_heap());

    // The bytes, from whatever it was handed. A Blob, a File and a Response body
    // all carry them in the same slot, so this needs to know about none of them.
    std::string bytes;
    bool readable = false;
    if (waiting.blob.is_object()) {
        const value held = cx.lookup_property(waiting.blob, "__bytes");
        if (held.is_array()) {
            readable = true;
            for (const value & b : static_cast<script::array_object *>(held.as_heap())->items) {
                bytes += static_cast<char>(
                    static_cast<unsigned char>(std::clamp(context::to_number(b), 0.0, 255.0)));
            }
        }
    }

    reader->set("readyState", value::number(2)); // DONE, either way
    auto * event = static_cast<script::object_object *>(cx.make_object().as_heap());
    event->set("target", waiting.reader);
    if (!readable) {
        // NOT a silent empty string. A page that reads something that is not a
        // blob gets the error branch, which is what it is written for.
        auto * failure = static_cast<script::object_object *>(cx.make_object().as_heap());
        failure->set("name", cx.string("NotReadableError"));
        failure->set("message", cx.string("FileReader was given something with no bytes"));
        reader->set("error", value::object(failure));
        event->set("type", cx.string("error"));
        const value as_event = value::object(event);
        fire_handler_property(waiting.reader, "error", as_event);
        fire_handler_property(waiting.reader, "loadend", as_event);
        return;
    }

    switch (waiting.kind) {
    case read_kind::text:
    case read_kind::binary_string:
        // The same thing here, and honestly so: strings are BYTES in this engine,
        // so a "binary string" and a decoded UTF-8 one are one value. A page that
        // reads text it wrote as UTF-8 gets it back unchanged.
        reader->set("result", cx.string(bytes));
        break;
    case read_kind::data_url: {
        std::string type = "application/octet-stream";
        const value given = cx.lookup_property(waiting.blob, "type");
        if (!given.is_undefined() && !cx.to_string(given).empty()) { type = cx.to_string(given); }
        // Through the standard library's btoa, so one base64 encoder decides what
        // this means here.
        const value encoder = cx.global("btoa");
        std::string encoded;
        if (encoder.is_callable()) {
            const value text = cx.string(bytes);
            const value args[1] = {text};
            encoded = cx.to_string(cx.call(encoder, args));
        }
        reader->set("result", cx.string("data:" + type + ";base64," + encoded));
        break;
    }
    case read_kind::array_buffer: {
        // The shape install_typed_arrays recognises, so `new Uint8Array(result)`
        // is a view over these bytes rather than a copy of nothing.
        auto * buffer = static_cast<script::object_object *>(cx.make_object().as_heap());
        value stored = cx.make_array();
        auto * items = static_cast<script::array_object *>(stored.as_heap());
        items->elements = script::element_kind::u8;
        items->items.reserve(bytes.size());
        for (const char b : bytes) {
            items->items.push_back(
                value::number(static_cast<double>(static_cast<unsigned char>(b))));
        }
        buffer->set("byteLength", value::number(static_cast<double>(bytes.size())));
        buffer->set("length", value::number(static_cast<double>(bytes.size())));
        buffer->set("__bytes", stored);
        reader->set("result", value::object(buffer));
        break;
    }
    }
    event->set("type", cx.string("load"));
    const value as_event = value::object(event);
    // `onload` then `onloadend`, which is the order a page relies on when it uses
    // both - loadend is the "whatever happened, I am done" hook.
    fire_handler_property(waiting.reader, "load", as_event);
    fire_handler_property(waiting.reader, "loadend", as_event);
}

void dom_bindings::settle_image(context & cx, const pending_image & waiting) {
    // The decode happens HERE, on the turn that announces it, and is cached by
    // name in image_store - which is what makes p5's loadImage work at all: it
    // revokes the object URL inside onload, BEFORE drawing the image. The bytes
    // are gone from the registry by then, and the cached bitmap is the browser's
    // "already decoded" state made real.
    const std::shared_ptr<const paint::bitmap> image =
        waiting.url.empty() || images_ == nullptr || assets_ == nullptr
            ? nullptr
            : images_->load(*assets_, waiting.url);
    const bool ok = image != nullptr;

    auto * event = static_cast<script::object_object *>(cx.make_object().as_heap());
    event->set("type", cx.string(ok ? "load" : "error"));
    event->set("target", waiting.target);
    if (!ok) { event->set("message", cx.string("could not load " + waiting.url)); }
    const value as_event = value::object(event);

    if (waiting.promise.is_object()) {
        // decode() rejects with an Error rather than an event, which is what its
        // caller catches.
        value outcome = waiting.target;
        if (!ok) {
            // The ERROR OBJECT, not make_rejection's rejected promise: settling
            // one promise with another gave `undefined` to the catch branch,
            // which is a message about nothing.
            auto * failure = static_cast<script::object_object *>(cx.make_object().as_heap());
            failure->set("name", cx.string("EncodingError"));
            failure->set("message", cx.string("could not decode " + waiting.url));
            outcome = value::object(failure);
        }
        cx.settle_promise(waiting.promise, outcome, !ok);
    }
    // At the <img> ITSELF and nowhere else: an image's load event does not
    // bubble, so this fires the element's own listeners rather than dispatching
    // along a path.
    fire_at(path_step{waiting.id, listen_on::node}, ok ? "load" : "error", as_event, false);
    // A LOADED IMAGE CHANGES WHAT IS DRAWN. Without this an <img> appended
    // before its bytes arrived kept its empty box until something else happened
    // to invalidate the page.
    if (ok) { mutated(); }
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
    // `new Request(url, init)`.
    //
    // p5.js does not call fetch with a string. Every loader builds a Request
    // first - `new Request(path, { method: 'GET', mode: 'cors' })` - and hands
    // that to fetch, so an absent Request constructor made loadImage, loadModel,
    // loadShader and loadBytes throw on their first line. The failure named
    // `Request`, which is not obviously about loading an image.
    //
    // The fields are the ones a caller reads back. `method` and `mode` are
    // recorded rather than honoured: there is one transport here and it does a
    // GET, so a POST would be a wrong answer either way - and a page that sets
    // one can at least see what it set.
    cx.define_native("Request", [](context & c, std::span<value> a) {
        auto * request = static_cast<script::object_object *>(c.make_object().as_heap());
        request->set("url", c.string(a.empty() ? std::string{} : c.to_string(a[0])));
        std::string method = "GET";
        std::string mode = "cors";
        if (a.size() > 1 && a[1].is_object()) {
            const value given_method = c.lookup_property(a[1], "method");
            const value given_mode = c.lookup_property(a[1], "mode");
            if (!given_method.is_undefined()) { method = c.to_string(given_method); }
            if (!given_mode.is_undefined()) { mode = c.to_string(given_mode); }
            const value headers = c.lookup_property(a[1], "headers");
            if (!headers.is_undefined()) { request->set("headers", headers); }
            const value body = c.lookup_property(a[1], "body");
            if (!body.is_undefined()) { request->set("body", body); }
        }
        request->set("method", c.string(method));
        request->set("mode", c.string(mode));
        return value::object(request);
    });

    cx.define_native("fetch", [this](context & c, std::span<value> args) {
        // A REQUEST OR A STRING. `fetch(request)` is the form every p5 loader
        // uses, and to_string on an object would have produced "[object Object]"
        // and looked for an asset by that name - a 404 that says nothing about
        // why.
        value target = arg(args, 0);
        if (target.is_object()) {
            const value from_request = c.lookup_property(target, "url");
            if (!from_request.is_undefined()) { target = from_request; }
        }
        const std::string url = c.to_string(target);
        // QUEUED, not done. The promise is pending and the event loop settles
        // it, so a page's other work happens while the request is outstanding -
        // which is the whole point of the API and was not observable before.
        value signal = value::undefined();
        if (arg(args, 1).is_object()) { signal = c.lookup_property(arg(args, 1), "signal"); }
        const value promise = c.make_pending_promise();
        if (promise.is_undefined()) {
            // No promise machinery installed - a bare VM with no standard
            // library. Do it the old way rather than returning nothing.
            return fetch_now(c, url);
        }
        fetches_.push_back(pending_fetch{promise, url, signal});
        return promise;
    });
}

// One queued fetch, resolved. The body work is fetch_now's, which still does
// the deciding - asset registry, then a file next to the page, then the network -
// so there is one answer to "where does a url come from" rather than two.
void dom_bindings::settle_fetch(context & cx, const pending_fetch & waiting) {
    // ABORTED BEFORE IT RAN. The signal is the only reason a queued fetch does
    // not happen, and it is now possible to observe: the request is outstanding
    // for at least one turn, so a page has somewhere to call abort() from.
    if (waiting.signal.is_object() &&
        context::truthy(cx.lookup_property(waiting.signal, "aborted"))) {
        auto * error = static_cast<script::object_object *>(cx.make_object().as_heap());
        error->set("name", cx.string("AbortError"));
        error->set("message", cx.string("fetch of " + waiting.url + " was aborted"));
        cx.settle_promise(waiting.promise, value::object(error), true);
        return;
    }
    // fetch_now hands back a SETTLED promise, so its outcome is adopted rather
    // than wrapped again.
    const value done = fetch_now(cx, waiting.url);
    bool rejected = false;
    value with = done;
    if (done.is_object()) {
        auto * obj = static_cast<script::object_object *>(done.as_heap());
        if (obj->find("__settled") != nullptr) {
            value * held = obj->find("__value");
            value * state = obj->find("__rejected");
            with = held == nullptr ? value::undefined() : *held;
            rejected = state != nullptr && context::truthy(*state);
        }
    }
    cx.settle_promise(waiting.promise, with, rejected);
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

} // namespace ctbrowser::shell
