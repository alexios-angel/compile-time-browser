//===- image_data.hpp - ImageData construction helpers ----------*- C++ -*-===//
//
// Helpers that build ImageData objects for the 2D canvas bindings
// (createImageData, getImageData) and the ImageData constructor: argument
// conversion, the size check, and the object itself.
//
//===----------------------------------------------------------------------===//
#pragma once

#include <ctbrowser/script/script.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <optional>
#include <string_view>
#include <vector>

namespace ctbrowser::shell::detail {

/// Converts `value` as a WebIDL [EnforceRange] long, the type of
/// CanvasImageData's coordinates and dimensions. Throws a TypeError and
/// returns nullopt when the number is not finite or outside int32.
[[nodiscard]] inline std::optional<std::int32_t> image_data_long(script::context & cx,
                                                                 script::value value) {
    const double number = std::trunc(cx.to_number_value(value));
    if (cx.throw_pending()) { return std::nullopt; }
    if (!std::isfinite(number) || number < std::numeric_limits<std::int32_t>::min() ||
        number > std::numeric_limits<std::int32_t>::max()) {
        cx.throw_error("TypeError", "ImageData argument is outside the signed 32-bit range");
        return std::nullopt;
    }
    return static_cast<std::int32_t>(number);
}

/// The number of channel bytes (width * height * 4) for an ImageData, or
/// nullopt after throwing a RangeError when it exceeds the VM's array limit.
/// The VM's dense typed-array representation stores one value per channel,
/// so the check runs before multiplying or allocating.
[[nodiscard]] inline std::optional<std::size_t> image_data_length(script::context & cx,
                                                                  std::uint64_t width,
                                                                  std::uint64_t height) {
    const auto limit = std::min(static_cast<std::size_t>(script::array_object::max_length),
                                std::vector<script::value>{}.max_size());
    if (width > limit / 4 || (width != 0 && height > limit / 4 / width)) {
        cx.throw_error("RangeError", "ImageData dimensions exceed the array storage limit");
        return std::nullopt;
    }
    return static_cast<std::size_t>(width * height * 4);
}

/// The ImageData brand. Every ImageData made here carries it under a key no
/// page can spell, and putImageData recognises its argument by it rather than
/// by the prototype, which a subclass changes and a page can replace.
inline constexpr std::string_view image_data_brand = CTBROWSER_PRIVATE_KEY("ctbrowser:image-data");

/// Builds an ImageData of `width` x `height`. `bytes` becomes its `data`;
/// when undefined, a zero-filled Uint8ClampedArray is allocated. Returns
/// undefined with a RangeError pending when the size is too large.
[[nodiscard]] inline script::value make_image_data(
    script::context & cx, std::uint64_t width, std::uint64_t height,
    script::value bytes = script::value::undefined()) {
    const auto length = image_data_length(cx, width, height);
    if (!length) { return script::value::undefined(); }
    if (bytes.is_undefined()) {
        bytes = cx.make_array();
        auto * store = static_cast<script::array_object *>(bytes.as_heap());
        store->elements = script::element_kind::u8_clamped;
        try {
            store->items.assign(*length, script::value::number(0));
        } catch (const std::bad_alloc &) {
            cx.throw_error("RangeError", "ImageData allocation failed");
            return script::value::undefined();
        }
    }
    auto * out = cx.allocate<script::object_object>();
    out->set("width", script::value::number(static_cast<double>(width)));
    out->set("height", script::value::number(static_cast<double>(height)));
    out->set("data", bytes);
    out->set(image_data_brand, script::value::boolean(true));
    // An ImageData for `instanceof` as well: its prototype is the realm's
    // ImageData.prototype (install_window gives the constructor one).
    if (const script::value ctor = cx.global("ImageData"); ctor.is_callable()) {
        if (const script::value proto = cx.lookup_property(ctor, "prototype"); proto.is_object()) {
            out->prototype = proto;
        }
    }
    return script::value::object(out);
}

} // namespace ctbrowser::shell::detail
