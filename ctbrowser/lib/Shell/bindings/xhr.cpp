// dom_bindings - `XMLHttpRequest`, the XMLHttpRequest Standard (§4), over the
// same resource loading fetch() uses (load_resource: the asset registry, a
// file beside the page, the network when allowed).
//
// An XHR is an EventTarget whose state machine the page watches:
// `readyState` UNSENT -> OPENED -> HEADERS_RECEIVED -> LOADING -> DONE, a
// `readystatechange` event at each step, then `load` (or `error`/`abort`/
// `timeout`) and `loadend`, with `progress` events on the way. An
// asynchronous send does the work on a later turn - one of the page's own
// timers, as fetch() queues - so `xhr.onload` assigned after `send()` still
// hears it; a synchronous one (`open(m, u, false)`) finishes inside `send`.
//
// What is here is what a test or a loader reads: the state, the events, the
// handler properties, `responseText`/`response` in the text, json,
// arraybuffer, blob and document types, the status, `getResponseHeader`,
// `overrideMimeType`, `abort()`, `timeout`. Request headers and bodies are
// taken and not sent: nothing here opens a socket for a POST.

#include <ctbrowser/dom/encoding.hpp>
#include <ctbrowser/shell/bindings.hpp>

#include "events/internal.hpp"

#include <string>
#include <vector>

namespace ctbrowser::shell {

using namespace detail;

namespace {

const std::string state_slot = std::string{script::private_key_prefix} + "xhr:state";
const std::string url_slot = std::string{script::private_key_prefix} + "xhr:url";
const std::string method_slot = std::string{script::private_key_prefix} + "xhr:method";
const std::string async_slot = std::string{script::private_key_prefix} + "xhr:async";
const std::string status_slot = std::string{script::private_key_prefix} + "xhr:status";
const std::string type_slot = std::string{script::private_key_prefix} + "xhr:type";
const std::string bytes_slot = std::string{script::private_key_prefix} + "xhr:bytes";
const std::string text_slot = std::string{script::private_key_prefix} + "xhr:text";
const std::string sent_slot = std::string{script::private_key_prefix} + "xhr:sent";
const std::string error_slot = std::string{script::private_key_prefix} + "xhr:error";
const std::string override_slot = std::string{script::private_key_prefix} + "xhr:override";
const std::string headers_slot = std::string{script::private_key_prefix} + "xhr:headers";
const std::string generation_slot = std::string{script::private_key_prefix} + "xhr:generation";
const std::string document_slot = std::string{script::private_key_prefix} + "xhr:document";

constexpr int unsent = 0;
constexpr int opened = 1;
constexpr int headers_received = 2;
constexpr int loading = 3;
constexpr int done = 4;

[[nodiscard]] int number_slot(script::object_object & self, const std::string & slot) {
    const value * held = self.find(slot);
    return held == nullptr ? 0 : static_cast<int>(context::to_number(*held));
}

[[nodiscard]] std::string string_slot(context & cx, script::object_object & self,
                                      const std::string & slot) {
    const value * held = self.find(slot);
    return held == nullptr || !held->is_string() ? std::string{} : cx.to_string(*held);
}

// A MIME type's `charset` parameter, empty when there is none. ponytail: found
// the way a <meta> content's is, not by a full MIME type parse; a quoted `;`
// in an earlier parameter would confuse it.
[[nodiscard]] std::string_view charset_parameter(std::string_view mime) {
    const std::size_t semicolon = mime.find(';');
    return semicolon == std::string_view::npos
               ? std::string_view{}
               : encoding_from_meta_content(mime.substr(semicolon + 1));
}

// "Get a final encoding" (XHR 3.6.8): the RESPONSE's charset, and then the
// override MIME type's when that has one - an override naming only a type
// leaves the response's charset standing - by the Encoding Standard's label
// table; empty when there is none or it names no encoding.
[[nodiscard]] std::string final_encoding(std::string_view response_type,
                                         std::string_view override_type) {
    std::string_view label = charset_parameter(response_type);
    if (const std::string_view overriding = charset_parameter(override_type); !overriding.empty()) {
        label = overriding;
    }
    return label.empty() ? std::string{} : std::string{encoding_from_label(label)};
}

} // namespace

namespace detail {

// "UTF-8 decode" (Encoding 6): a UTF-8 BOM dropped and NO OTHER sniffed - FE FF
// at the front of a fetch() body is two U+FFFDs, not UTF-16 - then the UTF-8
// decoder (4.2), each ill-formed subsequence one U+FFFD, so a page reads
// scalar values, never raw bytes taken for Latin-1. What a Body's text() and
// json() read with.
[[nodiscard]] std::string utf8_decode(std::string_view bytes) {
    if (bytes.starts_with("\xEF\xBB\xBF")) { bytes.remove_prefix(3); }
    std::string out;
    out.reserve(bytes.size());
    std::uint32_t code_point = 0;
    std::size_t needed = 0;
    unsigned lower = 0x80;
    unsigned upper = 0xBF;
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        const auto byte = static_cast<unsigned char>(bytes[i]);
        if (needed == 0) {
            if (byte < 0x80) {
                out += static_cast<char>(byte);
            } else if (byte >= 0xC2 && byte <= 0xDF) {
                needed = 1;
                code_point = byte & 0x1Fu;
            } else if (byte >= 0xE0 && byte <= 0xEF) {
                lower = byte == 0xE0 ? 0xA0 : 0x80;
                upper = byte == 0xED ? 0x9F : 0xBF;
                needed = 2;
                code_point = byte & 0x0Fu;
            } else if (byte >= 0xF0 && byte <= 0xF4) {
                lower = byte == 0xF0 ? 0x90 : 0x80;
                upper = byte == 0xF4 ? 0x8F : 0xBF;
                needed = 3;
                code_point = byte & 0x07u;
            } else {
                append_utf8(out, 0xFFFD);
            }
            continue;
        }
        if (byte < lower || byte > upper) {
            // The sequence ends before this byte, which starts afresh.
            needed = 0;
            lower = 0x80;
            upper = 0xBF;
            append_utf8(out, 0xFFFD);
            --i;
            continue;
        }
        lower = 0x80;
        upper = 0xBF;
        code_point = (code_point << 6) | (byte & 0x3Fu);
        if (--needed == 0) { append_utf8(out, static_cast<char32_t>(code_point)); }
    }
    if (needed != 0) { append_utf8(out, 0xFFFD); }
    return out;
}

// "Decode" (Encoding 6) a body by the encoding `name`, UTF-8 when it is empty:
// a BOM decides first.
[[nodiscard]] std::string decode_body(std::string_view bytes, std::string_view name) {
    if (bytes.starts_with("\xEF\xBB\xBF")) { return utf8_decode(bytes); }
    if (bytes.starts_with("\xFE\xFF")) {
        name = "UTF-16BE";
    } else if (bytes.starts_with("\xFF\xFE")) {
        name = "UTF-16LE";
    }
    if (!name.empty() && name != "UTF-8") { return decode_document_bytes(bytes, name); }
    return utf8_decode(bytes);
}

} // namespace detail

void dom_bindings::install_xhr(context & cx) {
    // XMLHttpRequestEventTarget.prototype, then XMLHttpRequest.prototype on it.
    auto * target_proto = cx.allocate<script::object_object>();
    target_proto->prototype = event_target_prototype_;
    auto * proto = cx.allocate<script::object_object>();
    proto->prototype = value::object(target_proto);
    const value prototype = value::object(proto);

    // The handler IDL attributes, on the event target prototype as the IDL
    // has them: data slots the dispatch reads by name.
    for (const char * name :
         {"onloadstart", "onprogress", "onabort", "onerror", "onload", "ontimeout", "onloadend"}) {
        const std::string key{name};
        define_getter(
            cx, *target_proto, key,
            [key](context & c, std::span<value>) {
                auto * self = object_of(c.current_this());
                const value * held = self == nullptr ? nullptr : self->find(key);
                return held == nullptr ? value::null() : *held;
            },
            [key](context & c, std::span<value> a) {
                if (auto * self = object_of(c.current_this())) {
                    const value given = arg(a, 0);
                    self->set(key, given.is_object_like() ? given : value::null());
                }
                return value::undefined();
            });
    }
    define_getter(
        cx, *proto, "onreadystatechange",
        [](context & c, std::span<value>) {
            auto * self = object_of(c.current_this());
            const value * held = self == nullptr ? nullptr : self->find("onreadystatechange");
            return held == nullptr ? value::null() : *held;
        },
        [](context & c, std::span<value> a) {
            if (auto * self = object_of(c.current_this())) {
                const value given = arg(a, 0);
                self->set("onreadystatechange", given.is_object_like() ? given : value::null());
            }
            return value::undefined();
        });

    // One event at the request: `readystatechange` is plain, the rest are
    // ProgressEvents with `loaded`/`total`/`lengthComputable`.
    const auto fire = [this](context & c, value self, const char * type, bool progress) {
        value event = make_event_object(c, type, false, false);
        auto * carrier = object_of(event);
        if (carrier == nullptr) { return; }
        carrier->set(std::string{trusted_property}, value::boolean(true));
        carrier->set(std::string{initialised_property}, value::boolean(true));
        if (progress) {
            double total = 0;
            if (auto * s = object_of(self)) {
                if (const value * bytes = s->find(bytes_slot);
                    bytes != nullptr && bytes->is_array()) {
                    total = static_cast<double>(
                        static_cast<script::array_object *>(bytes->as_heap())->items.size());
                }
            }
            carrier->set("lengthComputable", value::boolean(total > 0));
            carrier->set("loaded", value::number(total));
            carrier->set("total", value::number(total));
        }
        (void)dispatch_to(event, path_step{node_id{}, listen_on::object, self});
    };
    const auto set_state = [fire](context & c, value self, int state) {
        auto * s = object_of(self);
        if (s == nullptr) { return; }
        s->set(state_slot, value::number(state));
        fire(c, self, "readystatechange", false);
    };
    // THE RESPONSE ARRIVES: the bytes, the status and the type recorded, then
    // the state walk with its events. `generation` guards a request that was
    // re-opened or aborted while its send was queued.
    const auto finish = [this, fire, set_state](context & c, value self, int generation) {
        auto * s = object_of(self);
        if (s == nullptr || number_slot(*s, generation_slot) != generation) { return; }
        const std::string url = string_slot(c, *s, url_slot);
        loaded_resource loaded = load_resource(url);
        const bool failed = !loaded.failure.empty() && loaded.status != 404;
        s->set(status_slot, value::number(failed ? 0 : loaded.status));
        s->set(type_slot, c.string(loaded.type));
        // THE TEXT RESPONSE (3.6.9): the bytes decoded by the final
        // encoding, else as UTF-8.
        s->set(text_slot, c.string(decode_body(
                              std::string_view{reinterpret_cast<const char *>(loaded.body.data()),
                                               loaded.body.size()},
                              final_encoding(loaded.type, string_slot(c, *s, override_slot)))));
        s->set(bytes_slot, make_u8_array(c, loaded.body));
        s->set(error_slot, value::boolean(failed));
        if (failed) {
            set_state(c, self, done);
            fire(c, self, "error", true);
            fire(c, self, "loadend", true);
            return;
        }
        set_state(c, self, headers_received);
        set_state(c, self, loading);
        fire(c, self, "progress", true);
        set_state(c, self, done);
        fire(c, self, "load", true);
        fire(c, self, "loadend", true);
    };

    set_method(cx, *proto, "open", [this, set_state](context & c, std::span<value> a) {
        const value self = c.current_this();
        auto * s = object_of(self);
        if (s == nullptr) { return value::undefined(); }
        if (a.size() < 2) {
            c.throw_error("TypeError", "XMLHttpRequest.open: 2 arguments required");
            return value::undefined();
        }
        // A method token, normalised: `get` is `GET`; CONNECT/TRACE/TRACK are
        // forbidden (§4.5.1).
        std::string method = c.to_string(a[0]);
        for (const std::string_view upper : {"DELETE", "GET", "HEAD", "OPTIONS", "POST", "PUT"}) {
            if (ascii_iequals(method, upper)) { method = std::string{upper}; }
        }
        if (ascii_iequals_any(method, {"CONNECT", "TRACE", "TRACK"})) {
            throw_dom_exception(c, "SecurityError", "XMLHttpRequest.open: forbidden method");
            return value::undefined();
        }
        const std::string given = c.to_string(a[1]);
        const std::string url = location_href_.empty() || given.find("://") != std::string::npos ||
                                        given.starts_with("blob:") || given.starts_with("data:")
                                    ? given
                                    : resolve(location_href_, given);
        // "Encoding-parse" it (4.5.1 steps 7-8): a URL the parser refuses
        // against the document's address - `http://user@/`, `file://a%/` -
        // is a SyntaxError, not a request for the string as written. A page
        // with no address has nothing to parse against, and its relative
        // paths are the asset registry's.
        if (url.empty() ||
            (!location_href_.empty() && !parse_url(given, std::string_view{location_href_}))) {
            throw_dom_exception(c, "SyntaxError", "XMLHttpRequest.open: invalid URL " + given);
            return value::undefined();
        }
        // Re-opening ABORTS the request in flight: its queued finish sees a
        // new generation and does nothing.
        s->set(generation_slot, value::number(number_slot(*s, generation_slot) + 1));
        s->set(method_slot, c.string(method));
        s->set(url_slot, c.string(url));
        s->set(async_slot, value::boolean(a.size() < 3 || context::truthy(a[2])));
        s->set(sent_slot, value::boolean(false));
        s->set(error_slot, value::boolean(false));
        s->set(status_slot, value::number(0));
        s->set(text_slot, c.string(""));
        s->set(bytes_slot, c.make_array());
        s->set(headers_slot, c.make_object());
        (void)s->erase(document_slot);
        set_state(c, self, opened);
        return value::undefined();
    });
    set_method(cx, *proto, "setRequestHeader", [this](context & c, std::span<value> a) {
        auto * s = object_of(c.current_this());
        if (s == nullptr) { return value::undefined(); }
        if (number_slot(*s, state_slot) != opened ||
            (s->find(sent_slot) != nullptr && context::truthy(*s->find(sent_slot)))) {
            throw_dom_exception(c, "InvalidStateError",
                                "XMLHttpRequest.setRequestHeader: the request is not opened");
            return value::undefined();
        }
        if (value * headers = s->find(headers_slot); headers != nullptr && headers->is_object()) {
            c.store_property(*headers, ascii_lower_copy(arg_string(c, a, 0)),
                             c.string(arg_string(c, a, 1)));
        }
        return value::undefined();
    });
    set_method(cx, *proto, "send", [this, finish, fire](context & c, std::span<value>) {
        const value self = c.current_this();
        auto * s = object_of(self);
        if (s == nullptr) { return value::undefined(); }
        if (number_slot(*s, state_slot) != opened ||
            (s->find(sent_slot) != nullptr && context::truthy(*s->find(sent_slot)))) {
            throw_dom_exception(c, "InvalidStateError",
                                "XMLHttpRequest.send: the request is not opened, or was sent");
            return value::undefined();
        }
        s->set(sent_slot, value::boolean(true));
        const int generation = number_slot(*s, generation_slot);
        const bool is_async =
            s->find(async_slot) != nullptr && context::truthy(*s->find(async_slot));
        if (!is_async) {
            finish(c, self, generation);
            return value::undefined();
        }
        fire(c, self, "loadstart", true);
        auto * task = c.allocate<script::native_object>(
            "xhr", [finish, self, generation](context & inner, std::span<value>) {
                finish(inner, self, generation);
                return value::undefined();
            });
        // The request lives in the C++ capture until the timer runs it.
        task->retained.push_back(self);
        (void)add_timer(value::object(task), 0, false);
        return value::undefined();
    });
    set_method(cx, *proto, "abort", [set_state, fire](context & c, std::span<value>) {
        const value self = c.current_this();
        auto * s = object_of(self);
        if (s == nullptr) { return value::undefined(); }
        s->set(generation_slot, value::number(number_slot(*s, generation_slot) + 1));
        const int state = number_slot(*s, state_slot);
        const bool sent = s->find(sent_slot) != nullptr && context::truthy(*s->find(sent_slot));
        if ((state == opened && sent) || state == headers_received || state == loading) {
            s->set(error_slot, value::boolean(true));
            set_state(c, self, done);
            fire(c, self, "abort", true);
            fire(c, self, "loadend", true);
        }
        if (number_slot(*s, state_slot) == done) { s->set(state_slot, value::number(unsent)); }
        return value::undefined();
    });
    set_method(cx, *proto, "overrideMimeType", [this](context & c, std::span<value> a) {
        auto * s = object_of(c.current_this());
        if (s == nullptr) { return value::undefined(); }
        const int state = number_slot(*s, state_slot);
        if (state == loading || state == done) {
            throw_dom_exception(c, "InvalidStateError",
                                "XMLHttpRequest.overrideMimeType: the response is in");
            return value::undefined();
        }
        s->set(override_slot, c.string(arg_string(c, a, 0)));
        return value::undefined();
    });
    set_method(cx, *proto, "getResponseHeader", [](context & c, std::span<value> a) {
        auto * s = object_of(c.current_this());
        if (s == nullptr || number_slot(*s, state_slot) < headers_received) {
            return value::null();
        }
        const std::string name = ascii_lower_copy(arg_string(c, a, 0));
        const std::string type = string_slot(c, *s, type_slot);
        if (name == "content-type" && !type.empty()) { return c.string(type); }
        return value::null();
    });
    set_method(cx, *proto, "getAllResponseHeaders", [](context & c, std::span<value>) {
        auto * s = object_of(c.current_this());
        if (s == nullptr || number_slot(*s, state_slot) < headers_received) { return c.string(""); }
        const std::string type = string_slot(c, *s, type_slot);
        return c.string(type.empty() ? std::string{} : "content-type: " + type + "\r\n");
    });

    const auto getter = [&cx, proto](const char * name, script::native_fn get) {
        define_getter(cx, *proto, name, std::move(get));
    };
    // THE DOCUMENT RESPONSE (XHR 3.6.9): the body parsed by its FINAL MIME
    // type - the override when there is one, else the response's, else
    // text/xml - as XML for an XML type, as HTML only when `responseType`
    // is "document" (an empty one never parses HTML), and null for anything
    // else. Parsed once and kept: `responseXML` is the same Document every
    // time. By the parser DOMParser runs, not by the page's `DOMParser`.
    //
    // AN HTML DOCUMENT HAS ITS OWN ENCODING: the final MIME type's charset,
    // else what a prescan of the first 1024 bytes finds, else UTF-8 - and
    // `characterSet` says which. Not the text response's, and not the
    // page's: a response is never decoded by the document that asked.
    const auto document_response = [this](context & c, script::object_object & s,
                                          bool html_allowed) -> value {
        if (const value * held = s.find(document_slot); held != nullptr) { return *held; }
        std::string type = string_slot(c, s, override_slot);
        if (type.empty()) { type = string_slot(c, s, type_slot); }
        std::string essence = ascii_lower_copy(
            trim(std::string_view{type}.substr(0, std::min(type.find(';'), type.size())),
                 html_whitespace));
        if (essence.empty()) { essence = "text/xml"; }
        const bool xml =
            essence == "text/xml" || essence == "application/xml" || essence.ends_with("+xml");
        const bool html = essence == "text/html";
        value made = value::null();
        if (xml) {
            made = parse_from_string(c, string_slot(c, s, text_slot), "application/xml");
        } else if (html && html_allowed) {
            std::string raw;
            if (const value * bytes = s.find(bytes_slot); bytes != nullptr && bytes->is_array()) {
                for (const value & one :
                     static_cast<script::array_object *>(bytes->as_heap())->items) {
                    raw += static_cast<char>(static_cast<unsigned char>(context::to_number(one)));
                }
            }
            std::string charset =
                final_encoding(string_slot(c, s, type_slot), string_slot(c, s, override_slot));
            if (charset.empty()) {
                charset = prescan_encoding(std::string_view{raw}.substr(0, 1024));
            }
            if (charset.empty()) { charset = "UTF-8"; }
            made = parse_from_string(c, decode_body(raw, charset), "text/html");
            for (const auto & owner : primary().secondary_documents_) {
                if (owner->document_ != made) { continue; }
                owner->doc_->set_encoding(charset);
                if (auto * target = object_of(owner->document_target_)) {
                    for (const char * name : {"characterSet", "charset", "inputEncoding"}) {
                        target->set(name, c.string(charset));
                    }
                }
            }
        }
        if (c.throw_pending()) { return value::null(); }
        s.define(document_slot, made, script::attr_none);
        return made;
    };
    getter("readyState", [](context & c, std::span<value>) {
        auto * s = object_of(c.current_this());
        return value::number(s == nullptr ? 0 : number_slot(*s, state_slot));
    });
    getter("status", [](context & c, std::span<value>) {
        auto * s = object_of(c.current_this());
        return value::number(s == nullptr ? 0 : number_slot(*s, status_slot));
    });
    getter("statusText", [](context & c, std::span<value>) {
        auto * s = object_of(c.current_this());
        const int status = s == nullptr ? 0 : number_slot(*s, status_slot);
        return c.string(status == 200 ? "OK" : status == 404 ? "Not Found" : "");
    });
    getter("responseURL", [](context & c, std::span<value>) {
        auto * s = object_of(c.current_this());
        if (s == nullptr || number_slot(*s, state_slot) < headers_received) { return c.string(""); }
        return c.string(string_slot(c, *s, url_slot));
    });
    // `responseType`: one of the seven, settable until LOADING; `""` is
    // "text". `responseText` is for the two text types only, `responseXML`
    // for "" and "document" (InvalidStateError otherwise).
    define_getter(
        cx, *proto, "responseType",
        [](context & c, std::span<value>) {
            auto * s = object_of(c.current_this());
            return c.string(s == nullptr ? std::string{} : string_slot(c, *s, "responseType"));
        },
        [this](context & c, std::span<value> a) {
            auto * s = object_of(c.current_this());
            if (s == nullptr) { return value::undefined(); }
            const std::string wanted = arg_string(c, a, 0);
            if (!ascii_iequals_any(wanted,
                                   {"", "arraybuffer", "blob", "document", "json", "text"})) {
                return value::undefined();
            }
            if (number_slot(*s, state_slot) >= loading) {
                throw_dom_exception(c, "InvalidStateError",
                                    "XMLHttpRequest.responseType: the response is loading");
                return value::undefined();
            }
            s->set("responseType", c.string(wanted));
            return value::undefined();
        });
    getter("responseText", [this](context & c, std::span<value>) {
        auto * s = object_of(c.current_this());
        if (s == nullptr) { return c.string(""); }
        const std::string kind = string_slot(c, *s, "responseType");
        if (!kind.empty() && kind != "text") {
            throw_dom_exception(c, "InvalidStateError",
                                "XMLHttpRequest.responseText: responseType is not text");
            return value::undefined();
        }
        const int state = number_slot(*s, state_slot);
        if (state != loading && state != done) { return c.string(""); }
        return c.string(string_slot(c, *s, text_slot));
    });
    getter("response", [this, document_response](context & c, std::span<value>) {
        auto * s = object_of(c.current_this());
        if (s == nullptr) { return value::undefined(); }
        const std::string kind = string_slot(c, *s, "responseType");
        const int state = number_slot(*s, state_slot);
        const bool errored =
            s->find(error_slot) != nullptr && context::truthy(*s->find(error_slot));
        if (kind.empty() || kind == "text") {
            if (state != loading && state != done) { return c.string(""); }
            return c.string(string_slot(c, *s, text_slot));
        }
        if (state != done || errored) { return value::null(); }
        const value * bytes = s->find(bytes_slot);
        if (kind == "json") {
            const value parser = c.global("JSON");
            const value parse =
                parser.is_object() ? c.lookup_property(parser, "parse") : value::undefined();
            if (!parse.is_callable()) { return value::null(); }
            const value text = c.string(string_slot(c, *s, text_slot));
            const value out = c.call(parse, std::span<const value>{&text, 1});
            if (c.throw_pending()) {
                (void)c.take_error();
                return value::null();
            }
            return out;
        }
        std::vector<std::byte> raw;
        if (bytes != nullptr && bytes->is_array()) {
            for (const value & b : static_cast<script::array_object *>(bytes->as_heap())->items) {
                raw.push_back(
                    static_cast<std::byte>(static_cast<unsigned char>(context::to_number(b))));
            }
        }
        if (kind == "arraybuffer") { return make_array_buffer(c, raw); }
        if (kind == "blob") {
            return make_blob(c, make_u8_array(c, raw), string_slot(c, *s, type_slot));
        }
        return document_response(c, *s, true);
    });
    getter("responseXML", [this, document_response](context & c, std::span<value>) {
        auto * s = object_of(c.current_this());
        if (s == nullptr) { return value::null(); }
        const std::string kind = string_slot(c, *s, "responseType");
        if (!kind.empty() && kind != "document") {
            throw_dom_exception(c, "InvalidStateError",
                                "XMLHttpRequest.responseXML: responseType is not document");
            return value::undefined();
        }
        if (number_slot(*s, state_slot) != done) { return value::null(); }
        const bool errored =
            s->find(error_slot) != nullptr && context::truthy(*s->find(error_slot));
        return errored ? value::null() : document_response(c, *s, !kind.empty());
    });
    define_getter(
        cx, *proto, "timeout",
        [](context & c, std::span<value>) {
            auto * s = object_of(c.current_this());
            return value::number(s == nullptr ? 0 : number_slot(*s, "timeout"));
        },
        [](context & c, std::span<value> a) {
            if (auto * s = object_of(c.current_this())) {
                s->set("timeout", value::number(arg_number(a, 0)));
            }
            return value::undefined();
        });
    define_getter(
        cx, *proto, "withCredentials",
        [](context & c, std::span<value>) {
            auto * s = object_of(c.current_this());
            const value * held = s == nullptr ? nullptr : s->find("withCredentials");
            return value::boolean(held != nullptr && context::truthy(*held));
        },
        [](context & c, std::span<value> a) {
            if (auto * s = object_of(c.current_this())) {
                s->set("withCredentials", value::boolean(context::truthy(arg(a, 0))));
            }
            return value::undefined();
        });
    getter("upload", [](context & c, std::span<value>) {
        auto * s = object_of(c.current_this());
        if (s == nullptr) { return value::undefined(); }
        if (const value * held = s->find("upload"); held != nullptr) { return *held; }
        // An XMLHttpRequestUpload: an event target nothing here ever fires at.
        const value made = c.make_object();
        s->set("upload", made);
        return made;
    });

    auto * ctor = cx.allocate<script::native_object>(
        "XMLHttpRequest", [prototype](context & c, std::span<value>) {
            value self = c.current_this();
            if (!self.is_object()) { self = c.make_object(); }
            auto * made = static_cast<script::object_object *>(self.as_heap());
            if (!made->prototype.is_object()) { made->prototype = prototype; }
            made->set(state_slot, value::number(unsent));
            made->set(generation_slot, value::number(0));
            made->set("responseType", c.string(""));
            return self;
        });
    ctor->retained.push_back(prototype);
    ctor->define("prototype", prototype, script::attr_none);
    proto->define("constructor", value::object(ctor), script::attr_builtin);
    for (const auto & [name, n] :
         {std::pair{"UNSENT", 0}, std::pair{"OPENED", 1}, std::pair{"HEADERS_RECEIVED", 2},
          std::pair{"LOADING", 3}, std::pair{"DONE", 4}}) {
        ctor->define(name, value::number(n), script::attr_enumerable);
        proto->define(name, value::number(n), script::attr_enumerable);
    }
    cx.define_builtin_global("XMLHttpRequest", value::object(ctor));

    auto * target_ctor =
        cx.allocate<script::native_object>("XMLHttpRequestEventTarget", illegal_constructor);
    target_ctor->define("prototype", value::object(target_proto), script::attr_none);
    target_proto->define("constructor", value::object(target_ctor), script::attr_builtin);
    cx.define_builtin_global("XMLHttpRequestEventTarget", value::object(target_ctor));
}

} // namespace ctbrowser::shell
