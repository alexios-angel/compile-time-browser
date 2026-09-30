// `crypto` - the Web Cryptography API's Crypto interface (W3C WebCryptoAPI
// §10), its two synchronous methods: getRandomValues and randomUUID.
//
// FROM THE OS CSPRNG (os_random.hpp), never Math.random: that one is seeded
// and deterministic so the goldens can byte-compare, which is exactly what a
// game's session token, shuffle seed or nonce must not be.
//
// ponytail: no `crypto.subtle`. SubtleCrypto is async key management and
// digests; add it when a page needs one.

#include "internal.hpp"
#include "os_random.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace ctbrowser::shell {

namespace {

// §10.1.1 step 2: the quota, in bytes.
constexpr std::size_t quota_bytes = 65536;

[[nodiscard]] bool integer_kind(script::element_kind kind) {
    using enum script::element_kind;
    return kind == i8 || kind == u8 || kind == u8_clamped || kind == i16 || kind == u16 ||
           kind == i32 || kind == u32 || kind == big_i64 || kind == big_u64;
}

} // namespace

void install_crypto(context & cx, script::object_object & window, const throw_dom_fn & throw_dom) {
    const value crypto_proto = value::object(cx.allocate<script::object_object>());
    const context::rooted keep_proto{cx, crypto_proto};
    auto & proto = *static_cast<script::object_object *>(crypto_proto.as_heap());
    auto * ctor = cx.allocate<script::native_object>("Crypto", illegal_constructor);
    ctor->define("prototype", crypto_proto, script::attr_none);
    proto.define("constructor", value::object(ctor), script::attr_builtin);
    proto.define(CTBROWSER_SYMBOL_KEY("toStringTag"), cx.string("Crypto"),
                 script::attr_configurable);
    cx.define_builtin_global("Crypto", value::object(ctor));

    set_operation(cx, proto, "getRandomValues", 1, [throw_dom](context & c, std::span<value> a) {
        const value target = a[0];
        if (!target.is_array() || static_cast<script::array_object *>(target.as_heap())->elements ==
                                      script::element_kind::none) {
            c.throw_error("TypeError", "Failed to execute 'getRandomValues' on 'Crypto': "
                                       "parameter 1 is not of type 'ArrayBufferView'.");
            return value::undefined();
        }
        auto * array = static_cast<script::array_object *>(target.as_heap());
        const script::element_kind kind = array->elements;
        if (!integer_kind(kind)) {
            throw_dom(c, "TypeMismatchError",
                      "Failed to execute 'getRandomValues' on 'Crypto': The provided "
                      "ArrayBufferView is of type '" +
                          std::string{kind == script::element_kind::f64   ? "Float64"
                                      : kind == script::element_kind::f32 ? "Float32"
                                                                          : "Float16"} +
                          "', which is not an integer array type.");
            return value::undefined();
        }
        const std::size_t width = script::bytes_per_element(kind);
        // A BigInt kind that owns its elements holds bigint objects; asking
        // for its `buffer` moves them into a store, whose bytes are then
        // written like any view's.
        const bool big =
            kind == script::element_kind::big_i64 || kind == script::element_kind::big_u64;
        if (big && !array->is_view()) {
            (void)c.lookup_property(target, "buffer");
            if (c.throw_pending()) { return value::undefined(); }
            if (!array->is_view()) {
                c.throw_error("TypeError", "getRandomValues: the array has no buffer");
                return value::undefined();
            }
        }
        const std::size_t count = array->length();
        if (count * width > quota_bytes) {
            throw_dom(c, "QuotaExceededError",
                      "Failed to execute 'getRandomValues' on 'Crypto': The ArrayBufferView's "
                      "byte length (" +
                          std::to_string(count * width) +
                          ") exceeds the number of bytes of entropy available via this API "
                          "(65536).");
            return value::undefined();
        }
        std::vector<std::uint8_t> bytes(count * width);
        if (!os_random(bytes)) {
            throw_dom(c, "OperationError", "getRandomValues: the system CSPRNG failed");
            return value::undefined();
        }
        for (std::size_t i = 0; i < count; ++i) {
            std::uint64_t raw = 0;
            for (std::size_t b = 0; b < width; ++b) {
                raw |= static_cast<std::uint64_t>(bytes[i * width + b]) << (8 * b);
            }
            if (array->is_view()) {
                script::view_set_raw(*array, i, raw);
            } else {
                array->items[i] = value::number(script::element_from_raw(kind, raw));
            }
        }
        return target;
    });

    set_operation(cx, proto, "randomUUID", 0, [throw_dom](context & c, std::span<value>) {
        // RFC 9562 §5.4: 122 random bits, version 4, variant 10.
        std::array<std::uint8_t, 16> b{};
        if (!os_random(b)) {
            throw_dom(c, "OperationError", "randomUUID: the system CSPRNG failed");
            return value::undefined();
        }
        b[6] = static_cast<std::uint8_t>((b[6] & 0x0F) | 0x40);
        b[8] = static_cast<std::uint8_t>((b[8] & 0x3F) | 0x80);
        static constexpr char hex[] = "0123456789abcdef";
        std::string out;
        out.reserve(36);
        for (std::size_t i = 0; i < b.size(); ++i) {
            if (i == 4 || i == 6 || i == 8 || i == 10) { out += '-'; }
            out += hex[b[i] >> 4];
            out += hex[b[i] & 0x0F];
        }
        return c.string(out);
    });

    auto * crypto = cx.allocate<script::object_object>();
    crypto->prototype = crypto_proto;
    window.set("crypto", value::object(crypto));
    cx.define_global("crypto", value::object(crypto));
}

} // namespace ctbrowser::shell
