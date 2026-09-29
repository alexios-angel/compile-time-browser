// dom_bindings - fetch, images and the asset registry.

#include <ctbrowser/core/algorithms.hpp>
#include <ctbrowser/shell/bindings.hpp>
#include <ctbrowser/shell/net/url.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <optional>

// dom_bindings' method bodies - the API a page's script actually calls.
//
// The header lists what a page can reach; this is how each one works.

namespace ctbrowser::shell {

namespace detail {
// A body's bytes decoded, BOM and all - defined in xhr.cpp beside the
// response decoding that needed it first.
[[nodiscard]] std::string decode_body(std::string_view bytes, std::string_view name);
// "UTF-8 decode": a UTF-8 BOM dropped and none other sniffed - xhr.cpp.
[[nodiscard]] std::string utf8_decode(std::string_view bytes);
} // namespace detail

namespace {

// --- Fetch: Headers, Request, Response and the Body mixin (Fetch 5) -------------------
//
// State in private slots, the way XMLHttpRequest keeps its own: a Headers
// object's list as a script array of [name, value] pairs (the case kept,
// compared without it) and its guard; a Request's or a Response's Headers, its
// body as a string cell of the raw bytes - absent for a null body - and
// whether that has been read.
const std::string header_list_slot = std::string{script::private_key_prefix} + "fetch:headers";
const std::string header_guard_slot = std::string{script::private_key_prefix} + "fetch:guard";
const std::string headers_object_slot = std::string{script::private_key_prefix} + "fetch:owner";
const std::string body_slot = std::string{script::private_key_prefix} + "fetch:body";
const std::string body_used_slot = std::string{script::private_key_prefix} + "fetch:used";
// The prototypes make_response builds on, kept where a later fetch finds them
// whatever the page did to its `Response` and `Headers` globals.
const std::string headers_proto_global = std::string{script::private_key_prefix} + "Headers";
const std::string response_proto_global = std::string{script::private_key_prefix} + "Response";

using header_list = std::vector<std::pair<std::string, std::string>>;

// A header NAME is a token (RFC 9110 5.6.2).
[[nodiscard]] bool is_header_name(std::string_view name) {
    constexpr std::string_view marks = "!#$%&'*+-.^_`|~";
    return !name.empty() && std::ranges::all_of(name, [&](char ch) {
        return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
               marks.find(ch) != std::string_view::npos;
    });
}

// A header value NORMALIZED - leading and trailing HTTP whitespace off - and
// then refused when it still holds a NUL, CR or LF (Fetch 2.2.2).
[[nodiscard]] std::optional<std::string> header_value(std::string_view text) {
    text = trim(text, "\t\n\r ");
    if (text.find_first_of(std::string_view{"\0\r\n", 3}) != std::string_view::npos) {
        return std::nullopt;
    }
    return std::string{text};
}

[[nodiscard]] header_list headers_of(context & c, value headers) {
    header_list out;
    script::object_object * self = object_of(headers);
    const value * held = self == nullptr ? nullptr : self->find(header_list_slot);
    if (held == nullptr || !held->is_array()) { return out; }
    for (const value & pair : static_cast<script::array_object *>(held->as_heap())->items) {
        const auto & two = static_cast<script::array_object *>(pair.as_heap())->items;
        out.emplace_back(c.to_string(two.at(0)), c.to_string(two.at(1)));
    }
    return out;
}

void store_headers(context & c, script::object_object & self, const header_list & list) {
    const value array = c.make_array();
    auto & items = static_cast<script::array_object *>(array.as_heap())->items;
    for (const auto & [name, text] : list) {
        const value pair = c.make_array();
        auto & two = static_cast<script::array_object *>(pair.as_heap())->items;
        two.push_back(c.string(name));
        two.push_back(c.string(text));
        items.push_back(pair);
    }
    self.define(header_list_slot, array, script::attr_none);
}

// "Get" a header: every value under the name, joined by ", "; nullopt for none.
[[nodiscard]] std::optional<std::string> get_header(const header_list & list,
                                                    std::string_view name) {
    std::optional<std::string> out;
    for (const auto & [held, text] : list) {
        if (!ascii_iequals(held, name)) { continue; }
        out = out ? *out + ", " + text : text;
    }
    return out;
}

// "Sort and combine": lowercased names in byte order, one entry per name with
// its values combined - except `set-cookie`, one entry per value.
[[nodiscard]] header_list sort_and_combine(const header_list & list) {
    std::vector<std::string> names;
    for (const auto & [name, text] : list) {
        std::string lower = ascii_lower_copy(name);
        if (std::ranges::find(names, lower) == names.end()) { names.push_back(std::move(lower)); }
    }
    std::ranges::sort(names);
    header_list out;
    for (const std::string & name : names) {
        if (name != "set-cookie") {
            out.emplace_back(name, get_header(list, name).value_or(std::string{}));
            continue;
        }
        for (const auto & [held, text] : list) {
            if (ascii_iequals(held, name)) { out.emplace_back(name, text); }
        }
    }
    return out;
}

// A Headers object over `list`, under `guard`: "immutable" for a fetched
// response's, "none" for the rest. ponytail: the request and response
// guards' forbidden-name lists are not applied - nothing here sends a request
// or keeps a cookie for them to protect.
[[nodiscard]] value make_headers(context & c, const header_list & list, std::string_view guard) {
    auto * made = c.allocate<script::object_object>();
    if (const value proto = c.global(headers_proto_global); proto.is_object()) {
        made->prototype = proto;
    }
    store_headers(c, *made, list);
    made->define(header_guard_slot, c.string(std::string{guard}), script::attr_none);
    return value::object(made);
}

// "Fill" a header list from a HeadersInit: a sequence of pairs - a Headers is
// one, being iterable - or a record. False, with the TypeError thrown, when a
// pair is not a pair or a name or value is not one.
bool fill_headers(context & c, header_list & list, value init, const char * where) {
    if (init.is_undefined()) { return true; }
    if (!init.is_object_like()) {
        c.throw_error("TypeError", std::string{where} + ": headers is not a HeadersInit");
        return false;
    }
    const context::rooted keep{c, init};
    const auto add = [&](std::string name, std::string_view text) {
        const std::optional<std::string> normalized = header_value(text);
        if (!is_header_name(name) || !normalized) {
            c.throw_error("TypeError", std::string{where} + ": invalid header '" + name + "'");
            return false;
        }
        list.emplace_back(std::move(name), *normalized);
        return true;
    };
    if (const value method = c.lookup_property(init, CTBROWSER_SYMBOL_KEY("iterator"));
        method.is_callable()) {
        const value items = c.iterable_values(init);
        if (c.throw_pending()) { return false; }
        // COPIED AND ROOTED, both levels: iterable_values hands an Array
        // argument back ITSELF, so these vectors can be the page's own - and
        // a name's toString that pushes onto one reallocates it under a
        // range-for (or a reference) still walking it.
        std::vector<value> entries = static_cast<script::array_object *>(items.as_heap())->items;
        const context::rooted_values keep_entries{c, entries};
        for (const value item : entries) {
            const value pair = c.iterable_values(item);
            if (c.throw_pending()) { return false; }
            std::vector<value> parts = static_cast<script::array_object *>(pair.as_heap())->items;
            if (parts.size() != 2) {
                c.throw_error("TypeError",
                              std::string{where} + ": a header pair must have two elements");
                return false;
            }
            const context::rooted_values keep_parts{c, parts};
            std::string name = c.to_string(parts[0]);
            if (c.throw_pending()) { return false; }
            const std::string text = c.to_string(parts[1]);
            if (c.throw_pending()) { return false; }
            if (!add(std::move(name), text)) { return false; }
        }
        return true;
    }
    if (c.throw_pending()) { return false; }
    const value keys = c.own_keys(init);
    const context::rooted keep_keys{c, keys};
    for (const value & key : static_cast<script::array_object *>(keys.as_heap())->items) {
        const std::string name = c.to_string(key);
        const value held = c.lookup_property(init, name);
        if (c.throw_pending()) { return false; }
        if (!add(name, c.to_string(held))) { return false; }
    }
    return true;
}

// The bytes of a u8 array, or of anything carrying one in `__bytes` - a Blob,
// an ArrayBuffer, a File.
[[nodiscard]] std::string bytes_of(context & c, value source) {
    // A Blob's or an ArrayBuffer's `__bytes`, or a view's window onto its
    // buffer - a view's own `items` are empty (typed_arrays/internal.hpp).
    std::vector<std::uint8_t> bytes;
    if (!buffer_source_bytes(c, source, bytes)) { return {}; }
    return std::string{bytes.begin(), bytes.end()};
}

[[nodiscard]] value u8_of(context & c, std::string_view bytes) {
    return make_u8_array(c, std::as_bytes(std::span{bytes.data(), bytes.size()}));
}

// "Extract a body" (Fetch 5.1) from a BodyInit: its bytes, and the Content-Type
// it implies - empty for none. A string is UTF-8 text/plain, URLSearchParams
// urlencoded, a Blob its own type, a BufferSource none, and FormData
// multipart/form-data under a boundary of its own.
struct extracted_body {
    std::string bytes;
    std::string type;
};

[[nodiscard]] extracted_body extract_body(context & c, value body) {
    const auto is = [&](const char * name) {
        return c.has_global(name) && c.instance_of(body, c.global(name));
    };
    if (body.is_object() && is("Blob")) {
        return {bytes_of(c, body), c.to_string(c.lookup_property(body, "type"))};
    }
    if (body.is_object() && is("URLSearchParams")) {
        return {c.to_string(body), "application/x-www-form-urlencoded;charset=UTF-8"};
    }
    if (body.is_object() && is("FormData")) {
        // "The multipart/form-data encoding algorithm" (HTML 4.10.21.8) over
        // the entry list and UTF-8 - page/forms.cpp's, the one a form
        // submission runs: the line breaks of names and string values
        // normalised, a name's `"`, CR and LF escaped, a File with its
        // filename and type.
        const value entries = c.iterable_values(body);
        if (c.throw_pending() || !entries.is_array()) { return {}; }
        const context::rooted keep{c, entries};
        std::vector<form_entry> list;
        for (const value & pair : static_cast<script::array_object *>(entries.as_heap())->items) {
            const auto & two = static_cast<script::array_object *>(pair.as_heap())->items;
            if (two.size() != 2) { continue; }
            form_entry entry;
            entry.name = c.to_string(two[0]);
            if (two[1].is_object()) {
                entry.file = true;
                entry.filename = c.to_string(c.lookup_property(two[1], "name"));
                entry.type = c.to_string(c.lookup_property(two[1], "type"));
                entry.value = bytes_of(c, two[1]);
            } else {
                entry.value = c.to_string(two[1]);
            }
            list.push_back(std::move(entry));
        }
        return {multipart_form_body(list, "UTF-8"),
                "multipart/form-data; boundary=" + std::string{multipart_boundary}};
    }
    // An ArrayBuffer or a view onto one: its bytes, the view's offset and
    // length applied, and no type. A plain array is no BufferSource - it is
    // stringified, below, as WebIDL's union says.
    if (body.is_array()) {
        if (static_cast<script::array_object *>(body.as_heap())->elements !=
            script::element_kind::none) {
            return {bytes_of(c, body), {}};
        }
    } else if (body.is_object() && !c.lookup_property(body, "__bytes").is_undefined()) {
        return {bytes_of(c, body), {}};
    }
    return {c.to_string(body), "text/plain;charset=UTF-8"};
}

// A Body method's receiver: a Request or a Response - whatever carries its
// Headers in the private slot - else the TypeError WebIDL gives a method
// called on the wrong kind of object.
[[nodiscard]] script::object_object * body_this(context & c, const char * what) {
    script::object_object * self = object_of(c.current_this());
    if (self == nullptr || self->find(headers_object_slot) == nullptr) {
        c.throw_error("TypeError", std::string{what} + ": illegal invocation");
        return nullptr;
    }
    return self;
}

// The Content-Type in a Request's or a Response's header list, "" for none.
[[nodiscard]] std::string content_type_of(context & c, script::object_object & self) {
    const value * headers = self.find(headers_object_slot);
    return headers == nullptr
               ? std::string{}
               : get_header(headers_of(c, *headers), "content-type").value_or(std::string{});
}

// A body's bytes: a string cell holding them verbatim - made directly, not by
// context::string, whose surrogate joining would rewrite binary data.
[[nodiscard]] std::string body_text(value held) {
    return held.is_string() ? static_cast<script::string_object *>(held.as_heap())->text
                            : std::string{};
}

// The Body mixin's "consume body": the bytes, or nullopt when they have been
// read already. A null body reads as empty and is never "used".
[[nodiscard]] std::optional<std::string> consume_body(script::object_object & self) {
    const value * used = self.find(body_used_slot);
    if (used != nullptr && context::truthy(*used)) { return std::nullopt; }
    const value * held = self.find(body_slot);
    if (held == nullptr || !held->is_string()) { return std::string{}; }
    self.define(body_used_slot, value::boolean(true), script::attr_none);
    return body_text(*held);
}

// A Request or a Response gets its Headers and its body: the extracted type
// goes into the list when the list has no Content-Type of its own.
void attach_body(context & c, script::object_object & self, value headers,
                 const std::optional<extracted_body> & body) {
    self.define(headers_object_slot, headers, script::attr_none);
    self.set("headers", headers);
    if (!body) { return; }
    if (script::object_object * list = object_of(headers); list != nullptr && !body->type.empty()) {
        header_list held = headers_of(c, headers);
        if (!get_header(held, "content-type")) {
            held.emplace_back("Content-Type", body->type);
            store_headers(c, *list, held);
        }
    }
    self.define(body_slot, value::object(c.allocate<script::string_object>(body->bytes)),
                script::attr_none);
}

} // namespace

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
        obj.define_accessor(
            property,
            value::object(cx.allocate<script::native_object>(
                property,
                [this, id, property, natural_only, horizontal, decoded](context &,
                                                                        std::span<value>) {
                    if (!natural_only) {
                        const long long given = size_attribute(doc_->read(), id, property, -1);
                        if (given >= 0) { return value::number(static_cast<double>(given)); }
                    }
                    const std::shared_ptr<const paint::bitmap> image = decoded();
                    if (!image) { return value::number(0); }
                    return value::number(
                        static_cast<double>(horizontal ? image->width : image->height));
                })),
            value::object(cx.allocate<script::native_object>(
                property, [this, id, property](context &, std::span<value> a) {
                    // A reflected `unsigned long`: ToUint32, and
                    // anything past 2^31-1 writes the default 0.
                    long long given = number_to_uint32(arg_number(a, 0));
                    if (given > 2147483647LL) { given = 0; }
                    (void)doc_->set_attribute(id, atoms_->intern(property), std::to_string(given));
                    mutated();
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
    obj.define_accessor("src",
                        value::object(cx.allocate<script::native_object>(
                            "src",
                            [this, id](context & c, std::span<value>) {
                                // The URL reflection rule, as reflected_get spells it: absent
                                // is "", else parsed against the document and the raw text
                                // when that fails.
                                const auto txn = doc_->read();
                                const atom name = atoms_->intern("src");
                                if (!txn.has_attribute(id, name)) { return c.string(""); }
                                const std::string raw{txn.attribute_value(id, name)};
                                if (location_href_.empty()) { return c.string(raw); }
                                const std::string resolved = resolve(location_href_, raw);
                                return c.string(resolved.empty() ? raw : resolved);
                            })),
                        value::object(cx.allocate<script::native_object>(
                            "src", [this, id, wrapper](context & c, std::span<value> a) {
                                const std::string url = arg_string(c, a, 0);
                                (void)doc_->set_attribute(id, atoms_->intern("src"), url);
                                mutated();
                                begin_image_load(value::object(wrapper), id, url,
                                                 value::undefined());
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
    auto * event = cx.allocate<script::object_object>();
    // ROOTED ACROSS THE DISPATCH: the first listener dispatch compiles the
    // fence through run_nested, which can collect, and this event was in a
    // C++ local alone - freed before the handler read `e.target`. It never
    // showed until the live-object count at that moment shifted.
    const context::rooted keep_event{cx, value::object(event)};
    event->set("target", waiting.reader);
    if (!readable) {
        // NOT a silent empty string. A page that reads something that is not a
        // blob gets the error branch, which is what it is written for.
        auto * failure = cx.allocate<script::object_object>();
        const context::rooted keep_failure{cx, value::object(failure)};
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
        // readAsText DECODES (FileAPI "decode": a BOM decides, else UTF-8,
        // each ill-formed subsequence one U+FFFD); a "binary string" is each
        // byte as the code unit of the same value - Infra's isomorphic
        // decode - which is what btoa takes back.
        reader->set("result", cx.string(waiting.kind == read_kind::binary_string
                                            ? isomorphic_decode(bytes)
                                            : detail::decode_body(bytes, "UTF-8")));
        break;
    case read_kind::data_url: {
        std::string type = "application/octet-stream";
        const value given = cx.lookup_property(waiting.blob, "type");
        if (!given.is_undefined() && !cx.to_string(given).empty()) { type = cx.to_string(given); }
        // The bytes through core's encoder, not the page's `btoa`.
        reader->set("result", cx.string("data:" + type + ";base64," + base64_encode(bytes)));
        break;
    }
    case read_kind::array_buffer: {
        reader->set("result", make_array_buffer(cx, std::as_bytes(std::span{bytes})));
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

    auto * event = cx.allocate<script::object_object>();
    const context::rooted keep_event{cx, value::object(event)}; // as settle_read's
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
            auto * failure = cx.allocate<script::object_object>();
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

    // --- Headers, Request and Response (Fetch 5.2-5.5) -------------------------------
    //
    // WebIDL-shaped over the private slots above. Headers is the whole
    // interface; a Request and a Response carry their attributes as data
    // properties, as fetch()'s responses always have, with the Body mixin -
    // text, json, arrayBuffer, bytes, blob, formData, bodyUsed - on both
    // prototypes. Babylon.js builds `new Response(xhr.response, {status,
    // statusText, headers: new Headers()})` whenever the two exist, so these
    // are the objects its loader reads back.
    // `this` for a constructor: the object `new` made, else a TypeError.
    const auto constructed = [](context & c, script::native_object * ctor, value proto,
                                const char * name) -> script::object_object * {
        const value self_value = c.current_this();
        script::object_object * self = object_of(self_value);
        if (self == nullptr || !c.instance_of(self_value, value::object(ctor))) {
            c.throw_error("TypeError", std::string{"Failed to construct '"} + name +
                                           "': please use the 'new' operator.");
            return nullptr;
        }
        if (!self->prototype.is_object()) { self->prototype = proto; }
        return self;
    };

    const auto [headers_ctor, headers_proto] = make_interface(cx, "Headers", {}, {}, std::nullopt);
    const value headers_proto_value = value::object(headers_proto);
    cx.define_builtin_global(headers_proto_global, headers_proto_value);
    headers_ctor->fn = [headers_ctor, headers_proto_value, constructed](context & c,
                                                                        std::span<value> a) {
        script::object_object * self = constructed(c, headers_ctor, headers_proto_value, "Headers");
        if (self == nullptr) { return value::undefined(); }
        header_list list;
        if (!fill_headers(c, list, arg(a, 0), "Headers")) { return value::undefined(); }
        store_headers(c, *self, list);
        self->define(header_guard_slot, c.string("none"), script::attr_none);
        return c.current_this();
    };
    // Each operation: the receiver branded, the name a token, and - for the
    // three that write - a TypeError under the immutable guard.
    const auto headers_this = [](context & c, const char * what,
                                 bool writes) -> script::object_object * {
        script::object_object * self = object_of(c.current_this());
        if (self == nullptr || self->find(header_list_slot) == nullptr) {
            c.throw_error("TypeError", std::string{"Headers."} + what + ": illegal invocation");
            return nullptr;
        }
        const value * guard = self->find(header_guard_slot);
        if (writes && guard != nullptr && c.to_string(*guard) == "immutable") {
            c.throw_error("TypeError",
                          std::string{"Headers."} + what + ": the headers are immutable");
            return nullptr;
        }
        return self;
    };
    const auto header_name_arg = [](context & c, std::span<value> a,
                                    const char * what) -> std::optional<std::string> {
        std::string name = c.to_string(arg(a, 0));
        if (!is_header_name(name)) {
            c.throw_error("TypeError",
                          std::string{"Headers."} + what + ": invalid header name '" + name + "'");
            return std::nullopt;
        }
        return name;
    };
    const auto header_value_arg = [](context & c, std::span<value> a,
                                     const char * what) -> std::optional<std::string> {
        std::optional<std::string> text = header_value(c.to_string(arg(a, 1)));
        if (!text) {
            c.throw_error("TypeError", std::string{"Headers."} + what + ": invalid header value");
        }
        return text;
    };
    set_method(cx, *headers_proto, "append",
               [headers_this, header_name_arg, header_value_arg](context & c, std::span<value> a) {
                   script::object_object * self = headers_this(c, "append", true);
                   if (self == nullptr) { return value::undefined(); }
                   const std::optional<std::string> name = header_name_arg(c, a, "append");
                   if (!name) { return value::undefined(); }
                   const std::optional<std::string> text = header_value_arg(c, a, "append");
                   if (!text) { return value::undefined(); }
                   header_list list = headers_of(c, c.current_this());
                   list.emplace_back(*name, *text);
                   store_headers(c, *self, list);
                   return value::undefined();
               });
    set_method(cx, *headers_proto, "delete",
               [headers_this, header_name_arg](context & c, std::span<value> a) {
                   script::object_object * self = headers_this(c, "delete", true);
                   if (self == nullptr) { return value::undefined(); }
                   const std::optional<std::string> name = header_name_arg(c, a, "delete");
                   if (!name) { return value::undefined(); }
                   header_list list = headers_of(c, c.current_this());
                   std::erase_if(list,
                                 [&](const auto & p) { return ascii_iequals(p.first, *name); });
                   store_headers(c, *self, list);
                   return value::undefined();
               });
    set_method(cx, *headers_proto, "get",
               [headers_this, header_name_arg](context & c, std::span<value> a) {
                   if (headers_this(c, "get", false) == nullptr) { return value::undefined(); }
                   const std::optional<std::string> name = header_name_arg(c, a, "get");
                   if (!name) { return value::undefined(); }
                   const std::optional<std::string> found =
                       get_header(headers_of(c, c.current_this()), *name);
                   return found ? c.string(*found) : value::null();
               });
    set_method(cx, *headers_proto, "getSetCookie", [headers_this](context & c, std::span<value>) {
        if (headers_this(c, "getSetCookie", false) == nullptr) { return value::undefined(); }
        const value out = c.make_array();
        auto & items = static_cast<script::array_object *>(out.as_heap())->items;
        for (const auto & [name, text] : headers_of(c, c.current_this())) {
            if (ascii_iequals(name, "set-cookie")) { items.push_back(c.string(text)); }
        }
        return out;
    });
    set_method(cx, *headers_proto, "has",
               [headers_this, header_name_arg](context & c, std::span<value> a) {
                   if (headers_this(c, "has", false) == nullptr) { return value::undefined(); }
                   const std::optional<std::string> name = header_name_arg(c, a, "has");
                   if (!name) { return value::undefined(); }
                   return value::boolean(
                       get_header(headers_of(c, c.current_this()), *name).has_value());
               });
    // set(): the first of the name's entries takes the value and the rest go.
    set_method(cx, *headers_proto, "set",
               [headers_this, header_name_arg, header_value_arg](context & c, std::span<value> a) {
                   script::object_object * self = headers_this(c, "set", true);
                   if (self == nullptr) { return value::undefined(); }
                   const std::optional<std::string> name = header_name_arg(c, a, "set");
                   if (!name) { return value::undefined(); }
                   const std::optional<std::string> text = header_value_arg(c, a, "set");
                   if (!text) { return value::undefined(); }
                   header_list list = headers_of(c, c.current_this());
                   header_list out;
                   bool placed = false;
                   for (auto & [held, was] : list) {
                       if (!ascii_iequals(held, *name)) {
                           out.emplace_back(std::move(held), std::move(was));
                       } else if (!placed) {
                           out.emplace_back(std::move(held), *text);
                           placed = true;
                       }
                   }
                   if (!placed) { out.emplace_back(*name, *text); }
                   store_headers(c, *self, out);
                   return value::undefined();
               });
    set_method(cx, *headers_proto, "forEach", [headers_this](context & c, std::span<value> a) {
        if (headers_this(c, "forEach", false) == nullptr) { return value::undefined(); }
        const value callback = arg(a, 0);
        if (!callback.is_callable()) {
            c.throw_error("TypeError", "Headers.forEach: the callback is not a function");
            return value::undefined();
        }
        const value self = c.current_this();
        for (const auto & [name, text] : sort_and_combine(headers_of(c, self))) {
            const value args[3] = {c.string(text), c.string(name), self};
            (void)c.call(callback, args, arg(a, 1));
            if (c.throw_pending()) { break; }
        }
        return value::undefined();
    });
    // The pair iterator over the sorted and combined list, as of the call.
    // ponytail: an Array Iterator over a snapshot, not a live "Headers
    // Iterator" - a header set mid-iteration is not seen.
    for (const char * kind : {"entries", "keys", "values"}) {
        set_method(cx, *headers_proto, kind, [headers_this, kind](context & c, std::span<value>) {
            if (headers_this(c, kind, false) == nullptr) { return value::undefined(); }
            const value out = c.make_array();
            auto & items = static_cast<script::array_object *>(out.as_heap())->items;
            for (const auto & [name, text] : sort_and_combine(headers_of(c, c.current_this()))) {
                if (std::string_view{kind} == "keys") {
                    items.push_back(c.string(name));
                } else if (std::string_view{kind} == "values") {
                    items.push_back(c.string(text));
                } else {
                    const value pair = c.make_array();
                    auto & two = static_cast<script::array_object *>(pair.as_heap())->items;
                    two.push_back(c.string(name));
                    two.push_back(c.string(text));
                    items.push_back(pair);
                }
            }
            const context::rooted keep{c, out};
            return c.call(c.lookup_property(out, CTBROWSER_SYMBOL_KEY("iterator")), {}, out);
        });
    }
    if (const value * entries = headers_proto->find("entries"); entries != nullptr) {
        headers_proto->define(CTBROWSER_SYMBOL_KEY("iterator"), *entries, script::attr_builtin);
    }

    // The Body mixin, on both prototypes. blob() is the one that needs the
    // bindings - make_blob - so the mixin is built here.
    const auto body_mixin = [this, &cx](script::object_object & proto) {
        const auto read = [&cx, &proto](const char * name, auto finish) {
            set_method(cx, proto, name, [name, finish](context & c, std::span<value>) {
                script::object_object * self = body_this(c, name);
                if (self == nullptr) { return value::undefined(); }
                const std::optional<std::string> bytes = consume_body(*self);
                if (!bytes) {
                    return c.make_promise(
                        c.make_error("TypeError", std::string{name} + ": the body has been read"),
                        true);
                }
                return finish(c, *self, *bytes);
            });
        };
        read("text", [](context & c, script::object_object &, const std::string & bytes) {
            return c.make_promise(c.string(detail::utf8_decode(bytes)), false);
        });
        read("json", [](context & c, script::object_object &, const std::string & bytes) {
            const value parse = c.lookup_property(c.global("JSON"), "parse");
            const value text = c.string(detail::utf8_decode(bytes));
            const value parsed = c.call(parse, std::span<const value>{&text, 1});
            if (c.throw_pending()) { return c.make_promise(c.take_pending_throw(), true); }
            return c.make_promise(parsed, false);
        });
        read("arrayBuffer", [](context & c, script::object_object &, const std::string & bytes) {
            return c.make_promise(
                make_array_buffer(c, std::as_bytes(std::span{bytes.data(), bytes.size()})), false);
        });
        read("bytes", [](context & c, script::object_object &, const std::string & bytes) {
            return c.make_promise(u8_of(c, bytes), false);
        });
        read("blob", [this](context & c, script::object_object & self, const std::string & bytes) {
            return c.make_promise(
                make_blob(c, u8_of(c, bytes), ascii_lower_copy(content_type_of(c, self))), false);
        });
        // formData(): the urlencoded parser over the bytes for an
        // application/x-www-form-urlencoded body - its charset ignored, as
        // Fetch says - and a TypeError for any other type. ponytail: a
        // multipart/form-data body is not parsed; it rejects as another type.
        read("formData", [](context & c, script::object_object & self, const std::string & bytes) {
            const std::string type = content_type_of(c, self);
            const std::string essence = ascii_lower_copy(
                trim(std::string_view{type}.substr(0, std::min(type.find(';'), type.size())),
                     "\t\n\r "));
            if (essence != "application/x-www-form-urlencoded") {
                return c.make_promise(
                    c.make_error("TypeError", "formData: the body is not form data"), true);
            }
            const value form_data = c.construct(c.global("FormData"), {});
            if (c.throw_pending()) { return c.make_promise(c.take_pending_throw(), true); }
            const context::rooted keep{c, form_data};
            const value append = c.lookup_property(form_data, "append");
            for (const auto & [name, text] : parse_form_urlencoded(bytes)) {
                const value args[2] = {c.string(name), c.string(text)};
                (void)c.call(append, args, form_data);
            }
            return c.make_promise(form_data, false);
        });
        define_getter(cx, proto, "bodyUsed", [](context & c, std::span<value>) {
            script::object_object * self = body_this(c, "bodyUsed");
            if (self == nullptr) { return value::undefined(); }
            const value * used = self->find(body_used_slot);
            return value::boolean(used != nullptr && context::truthy(*used));
        });
    };
    // clone(): a copy with its own Headers and the same bytes - a TypeError
    // once the body has been read (Fetch 5.4/5.5).
    const auto clone_of = [](context & c, const char * what) -> value {
        script::object_object * self = body_this(c, what);
        if (self == nullptr) { return value::undefined(); }
        if (const value * used = self->find(body_used_slot);
            used != nullptr && context::truthy(*used)) {
            c.throw_error("TypeError", std::string{what} + ": the body has been read");
            return value::undefined();
        }
        auto * copy = c.allocate<script::object_object>();
        copy->prototype = self->prototype;
        self->each_own_enumerable_key([&](const std::string & key) {
            if (key != "headers") { copy->set(key, *self->find(key)); }
        });
        const value headers = *self->find(headers_object_slot);
        const value * guard = object_of(headers)->find(header_guard_slot);
        std::optional<extracted_body> body;
        if (const value * held = self->find(body_slot); held != nullptr) {
            body = extracted_body{body_text(*held), {}};
        }
        attach_body(c, *copy,
                    make_headers(c, headers_of(c, headers),
                                 guard == nullptr ? std::string{"none"} : c.to_string(*guard)),
                    body);
        return value::object(copy);
    };

    // `new Request(input, init)` (Fetch 5.4). THE URL IS KEPT AS GIVEN, not
    // serialised: fetch() takes a relative path to the asset registry by that
    // name, and p5 hands every loader's path through a Request first. It is
    // refused when it cannot parse against the document's address, or carries
    // credentials. The method is a token, not a forbidden one, normalised; a
    // body is refused for GET and HEAD.
    const auto [request_ctor, request_proto] = make_interface(cx, "Request", {}, {}, std::nullopt);
    const value request_proto_value = value::object(request_proto);
    request_ctor->fn = [this, request_ctor, request_proto_value, constructed](context & c,
                                                                              std::span<value> a) {
        script::object_object * self = constructed(c, request_ctor, request_proto_value, "Request");
        if (self == nullptr) { return value::undefined(); }
        const value input = arg(a, 0);
        const value init = arg(a, 1);
        std::string url;
        std::string method = "GET";
        header_list list;
        std::optional<extracted_body> body;
        script::object_object * from = object_of(input);
        if (from != nullptr && from->find(headers_object_slot) != nullptr) {
            url = c.to_string(c.lookup_property(input, "url"));
            method = c.to_string(c.lookup_property(input, "method"));
            list = headers_of(c, *from->find(headers_object_slot));
            if (const value * held = from->find(body_slot); held != nullptr) {
                body = extracted_body{body_text(*held), {}};
            }
        } else {
            url = c.to_string(input);
            if (c.throw_pending()) { return value::undefined(); }
            if (!location_href_.empty()) {
                const std::optional<url_record> parsed =
                    parse_url(url, std::string_view{location_href_});
                if (!parsed || parsed->has_credentials()) {
                    c.throw_error("TypeError", "Failed to construct 'Request': invalid URL " + url);
                    return value::undefined();
                }
            }
        }
        for (const char * name : {"mode", "credentials", "cache", "redirect", "referrer",
                                  "referrerPolicy", "integrity"}) {
            const value given = from != nullptr && from->find(headers_object_slot) != nullptr
                                    ? c.lookup_property(input, name)
                                    : value::undefined();
            if (!given.is_undefined()) { self->set(name, given); }
        }
        if (init.is_object_like()) {
            if (const value given = c.lookup_property(init, "method"); !given.is_undefined()) {
                method = c.to_string(given);
                if (!is_header_name(method) ||
                    ascii_iequals_any(method, {"CONNECT", "TRACE", "TRACK"})) {
                    c.throw_error("TypeError", "Failed to construct 'Request': '" + method +
                                                   "' is not a valid method");
                    return value::undefined();
                }
                for (const std::string_view normal :
                     {"DELETE", "GET", "HEAD", "OPTIONS", "POST", "PUT"}) {
                    if (ascii_iequals(method, normal)) { method = std::string{normal}; }
                }
            }
            if (const value given = c.lookup_property(init, "mode");
                !given.is_undefined() && c.to_string(given) == "navigate") {
                c.throw_error("TypeError",
                              "Failed to construct 'Request': mode cannot be navigate");
                return value::undefined();
            }
            for (const char * name : {"mode", "credentials", "cache", "redirect", "referrer",
                                      "referrerPolicy", "integrity", "keepalive", "signal"}) {
                if (const value given = c.lookup_property(init, name); !given.is_undefined()) {
                    self->set(name, given);
                }
            }
            if (const value given = c.lookup_property(init, "headers"); !given.is_undefined()) {
                list.clear();
                if (!fill_headers(c, list, given, "Request")) { return value::undefined(); }
            }
            if (const value given = c.lookup_property(init, "body"); !given.is_nullish()) {
                if (method == "GET" || method == "HEAD") {
                    c.throw_error("TypeError", "Failed to construct 'Request': a " + method +
                                                   " request cannot have a body");
                    return value::undefined();
                }
                body = extract_body(c, given);
                if (c.throw_pending()) { return value::undefined(); }
            }
        }
        const std::pair<const char *, const char *> defaults[] = {{"mode", "cors"},
                                                                  {"credentials", "same-origin"},
                                                                  {"cache", "default"},
                                                                  {"redirect", "follow"},
                                                                  {"referrer", "about:client"},
                                                                  {"referrerPolicy", ""},
                                                                  {"integrity", ""},
                                                                  {"destination", ""}};
        for (const auto & [name, fallback] : defaults) {
            if (self->find(name) == nullptr) { self->set(name, c.string(fallback)); }
        }
        if (self->find("keepalive") == nullptr) { self->set("keepalive", value::boolean(false)); }
        self->set("url", c.string(url));
        self->set("method", c.string(method));
        attach_body(c, *self, make_headers(c, list, "none"), body);
        return c.current_this();
    };
    request_ctor->retained.push_back(request_proto_value);
    body_mixin(*request_proto);
    set_method(cx, *request_proto, "clone",
               [clone_of](context & c, std::span<value>) { return clone_of(c, "Request.clone"); });

    // `new Response(body, init)` (Fetch 5.5): the status 200-599 else a
    // RangeError, a statusText with no CR or LF, the headers, and a body -
    // refused under a null body status.
    const auto [response_ctor, response_proto] =
        make_interface(cx, "Response", {}, {}, std::nullopt);
    const value response_proto_value = value::object(response_proto);
    cx.define_builtin_global(response_proto_global, response_proto_value);
    // "Initialize a response": the three init members onto `self`, or false
    // with the error thrown.
    const auto init_response = [](context & c, script::object_object & self, value init,
                                  header_list & list) {
        double status = 200;
        std::string status_text;
        if (init.is_object_like()) {
            if (const value given = c.lookup_property(init, "status"); !given.is_undefined()) {
                status = context::to_number(given);
            }
            if (const value given = c.lookup_property(init, "statusText"); !given.is_undefined()) {
                status_text = c.to_string(given);
            }
        }
        if (!(status >= 200 && status <= 599) || status != std::floor(status)) {
            c.throw_error("RangeError",
                          "Failed to construct 'Response': the status is out of range");
            return false;
        }
        if (status_text.find_first_of("\r\n") != std::string::npos) {
            c.throw_error("TypeError", "Failed to construct 'Response': invalid statusText");
            return false;
        }
        if (init.is_object_like() &&
            !fill_headers(c, list, c.lookup_property(init, "headers"), "Response")) {
            return false;
        }
        self.set("type", c.string("default"));
        self.set("url", c.string(""));
        self.set("redirected", value::boolean(false));
        self.set("status", value::number(status));
        self.set("ok", value::boolean(status >= 200 && status <= 299));
        self.set("statusText", c.string(status_text));
        return true;
    };
    response_ctor->fn = [response_ctor, response_proto_value, constructed,
                         init_response](context & c, std::span<value> a) {
        script::object_object * self =
            constructed(c, response_ctor, response_proto_value, "Response");
        if (self == nullptr) { return value::undefined(); }
        header_list list;
        if (!init_response(c, *self, arg(a, 1), list)) { return value::undefined(); }
        std::optional<extracted_body> body;
        if (const value given = arg(a, 0); !given.is_nullish()) {
            const double status = context::to_number(*self->find("status"));
            if (status == 101 || status == 103 || status == 204 || status == 205 || status == 304) {
                c.throw_error("TypeError",
                              "Failed to construct 'Response': a null body status has a body");
                return value::undefined();
            }
            body = extract_body(c, given);
            if (c.throw_pending()) { return value::undefined(); }
        }
        attach_body(c, *self, make_headers(c, list, "none"), body);
        return c.current_this();
    };
    response_ctor->retained.push_back(response_proto_value);
    body_mixin(*response_proto);
    set_method(cx, *response_proto, "clone",
               [clone_of](context & c, std::span<value>) { return clone_of(c, "Response.clone"); });
    // The three statics: a network error, a JSON body, and a redirect.
    const auto bare_response = [response_proto_value](context & c) {
        auto * made = c.allocate<script::object_object>();
        made->prototype = response_proto_value;
        return made;
    };
    set_method(cx, *response_ctor, "error", [bare_response](context & c, std::span<value>) {
        script::object_object * made = bare_response(c);
        made->set("type", c.string("error"));
        made->set("url", c.string(""));
        made->set("redirected", value::boolean(false));
        made->set("status", value::number(0));
        made->set("ok", value::boolean(false));
        made->set("statusText", c.string(""));
        attach_body(c, *made, make_headers(c, {}, "immutable"), std::nullopt);
        return value::object(made);
    });
    set_method(cx, *response_ctor, "json",
               [bare_response, init_response](context & c, std::span<value> a) {
                   const value stringify = c.lookup_property(c.global("JSON"), "stringify");
                   const value data = arg(a, 0);
                   const value text = c.call(stringify, std::span<const value>{&data, 1});
                   if (c.throw_pending()) { return value::undefined(); }
                   if (!text.is_string()) {
                       c.throw_error("TypeError", "Response.json: the data is not JSON");
                       return value::undefined();
                   }
                   script::object_object * made = bare_response(c);
                   header_list list;
                   if (!init_response(c, *made, arg(a, 1), list)) { return value::undefined(); }
                   attach_body(c, *made, make_headers(c, list, "none"),
                               extracted_body{c.to_string(text), "application/json"});
                   return value::object(made);
               });
    set_method(
        cx, *response_ctor, "redirect", [this, bare_response](context & c, std::span<value> a) {
            const std::string given = c.to_string(arg(a, 0));
            const std::optional<url_record> parsed =
                location_href_.empty() ? parse_url(given)
                                       : parse_url(given, std::string_view{location_href_});
            if (!parsed) {
                c.throw_error("TypeError", "Response.redirect: invalid URL " + given);
                return value::undefined();
            }
            const double status = arg(a, 1).is_undefined() ? 302 : context::to_number(arg(a, 1));
            if (status != 301 && status != 302 && status != 303 && status != 307 && status != 308) {
                c.throw_error("RangeError", "Response.redirect: not a redirect status");
                return value::undefined();
            }
            script::object_object * made = bare_response(c);
            made->set("type", c.string("default"));
            made->set("url", c.string(""));
            made->set("redirected", value::boolean(false));
            made->set("status", value::number(status));
            made->set("ok", value::boolean(false));
            made->set("statusText", c.string(""));
            attach_body(c, *made, make_headers(c, {{"Location", parsed->serialize()}}, "immutable"),
                        std::nullopt);
            return value::object(made);
        });

    // fetch(url) -> a settled Response. The registry is consulted FIRST, so
    // a page that ships its resources never opens a socket and a test run
    // is reproducible; a miss goes to the network when the application
    // allows it.
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
        auto * error = cx.allocate<script::object_object>();
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

dom_bindings::loaded_resource dom_bindings::load_resource(const std::string & url) {
    loaded_resource out;
    // THE EMBEDDER FIRST (browser_options::on_request): a GET of the URL,
    // resolved against this document's address. ponytail: a GET only - an
    // XHR's or a fetch's method and body do not reach the handler yet.
    if (!is_data_url(url) && !url.starts_with("blob:")) {
        resource_request asked;
        asked.url = location_href_.empty() ? url : resolve(location_href_, url);
        if (asked.url.empty()) { asked.url = url; }
        if (std::optional<resource_reply> reply = ask_request_handler(std::move(asked))) {
            out.status = reply->status;
            out.type = std::move(reply->content_type);
            out.body.resize(reply->body.size());
            std::memcpy(out.body.data(), reply->body.data(), reply->body.size());
            return out;
        }
    }
    if (assets_ != nullptr && assets_->contains(url)) {
        const std::span<const std::byte> baked = assets_->find(url);
        out.body.assign(baked.begin(), baked.end());
    } else if (url.find("://") == std::string::npos && assets_ != nullptr) {
        // A relative url is a file next to the page, which is what a
        // page-local `fetch("data.json")` means.
        out.body = assets_->load(url);
        if (out.body.empty()) {
            out.status = 404;
            out.failure = "no such resource: " + url;
        }
    } else if (url.starts_with("file:") && assets_ != nullptr) {
        // A file: URL IS A PATH, and it is what a relative reference becomes
        // once it is resolved against a page loaded from disk - an XHR for
        // `resources/x.html` from file:///a/b.html asks for
        // file:///a/resources/x.html. It is read the way the relative branch
        // reads, not sent to the network (which would refuse the scheme):
        // the host (empty, or `localhost`) dropped, the query and fragment
        // cut, and the path percent-decoded.
        std::string_view path = std::string_view{url}.substr(5);
        if (path.starts_with("//")) {
            path.remove_prefix(2);
            path = path.substr(std::min(path.find('/'), path.size()));
        }
        path = path.substr(0, std::min(path.find('?'), path.find('#')));
        out.body = assets_->load(percent_decode(path));
        if (out.body.empty()) {
            out.status = 404;
            out.failure = "no such resource: " + url;
        }
    } else if (network_allowed_) {
        const http_response response = http_get(url);
        out.status = response.status;
        out.type = response.content_type;
        out.body = std::move(response.body);
        if (!response.completed()) { out.failure = response.error; }
    } else {
        out.failure = "network access is off and " + url + " was not baked in";
    }
    // A data: URL's type is its OWN (Fetch's data: URL processor): the text
    // before the comma, less a trailing `;base64`, with `text/plain` in front
    // of a bare parameter list - and text/plain;charset=US-ASCII when there
    // is nothing there. A path's extension says nothing about one.
    if (out.type.empty() && ascii_istarts_with(url, "data:") &&
        url.find(',') != std::string::npos) {
        std::string_view head =
            trim(std::string_view{url}.substr(5, url.find(',') - 5), " \t\n\r\f");
        if (const std::size_t semicolon = head.rfind(';');
            semicolon != std::string_view::npos &&
            ascii_iequals(trim(head.substr(semicolon + 1), " "), "base64")) {
            head = trim(head.substr(0, semicolon), " \t\n\r\f");
        }
        std::string type{head};
        if (type.starts_with(';')) { type = "text/plain" + type; }
        out.type = type.empty() || type.find('/') == std::string::npos
                       ? std::string{"text/plain;charset=US-ASCII"}
                       : type;
    }
    if (out.type.empty()) { out.type = mime_for_path(url); }
    return out;
}

value dom_bindings::fetch_now(context & cx, const std::string & url) {
    loaded_resource loaded = load_resource(url);
    if (!loaded.failure.empty()) {
        // A network failure REJECTS, which is what a page's catch branch is
        // written for. A 404 does not - it is a Response with ok false.
        return make_rejection(cx, loaded.failure);
    }
    return make_response(cx, url, loaded.status, loaded.type, std::move(loaded.body));
}

value dom_bindings::make_rejection(context & cx, const std::string & message) {
    auto * error = cx.allocate<script::object_object>();
    error->set("message", cx.string(message));
    error->set("name", cx.string("TypeError"));
    return cx.make_promise(value::object(error), true);
}

value dom_bindings::make_response(context & cx, const std::string & url, int status,
                                  const std::string & content_type, std::vector<std::byte> body) {
    // A REAL Response, on the prototype `new Response()` makes - so the Body
    // mixin, clone() and bodyUsed are the same functions - with IMMUTABLE
    // headers, as a fetched response's are, carrying the one header this
    // engine knows: the content type.
    auto * response = cx.allocate<script::object_object>();
    if (const value proto = cx.global(response_proto_global); proto.is_object()) {
        response->prototype = proto;
    }
    response->set("type", cx.string("basic"));
    response->set("url", cx.string(url));
    response->set("redirected", value::boolean(false));
    response->set("status", value::number(status));
    response->set("ok", value::boolean(status >= 200 && status < 300));
    response->set("statusText", cx.string(status == 200 ? "OK" : status == 404 ? "Not Found" : ""));
    header_list list;
    if (!content_type.empty()) { list.emplace_back("Content-Type", content_type); }
    attach_body(
        cx, *response, make_headers(cx, list, "immutable"),
        extracted_body{std::string{reinterpret_cast<const char *>(body.data()), body.size()}, {}});
    return cx.make_promise(value::object(response), false);
}

} // namespace ctbrowser::shell
