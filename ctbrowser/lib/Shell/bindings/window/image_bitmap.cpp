//===- image_bitmap.cpp - createImageBitmap and ImageBitmap -----*- C++ -*-===//
//
// `createImageBitmap` and `ImageBitmap` (HTML 8.10), installed by
// dom_bindings::install_image_bitmap.
//
// An ImageBitmap is a bitmap cut from an image source, ready to draw: an <img>,
// a <canvas>, an ImageData, another ImageBitmap, or a Blob whose bytes decode
// as an image. Errors reject the returned promise and are never thrown: a
// source with nothing to draw (an <img> not yet decoded, a detached bitmap, a
// Blob that is not an image) is an InvalidStateError, a zero-sized crop a
// RangeError, and a value that is no image source at all the TypeError of its
// IDL conversion.
//
// The pixels live in the image store under a handle of their own
// (image_store::adopt), which is what drawImage reads through image_argument;
// the object carries the handle. `close()` releases them.
//
// ponytail: no resizeWidth/resizeHeight, imageOrientation or premultiplyAlpha
// options, no <video>, SVG image or OffscreenCanvas source, and no structured
// clone or transfer of an ImageBitmap.
//
//===----------------------------------------------------------------------===//

#include <ctbrowser/shell/bindings.hpp>

#include "internal.hpp"

#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace ctbrowser::shell {

namespace {

/// The most pixels one bitmap may hold: 2^26, i.e. 256 MiB of RGBA. Larger
/// requests are rejected so a page cannot force an arbitrarily large
/// allocation.
constexpr double largest_area = 67108864.0;

/// The crop of 8.10 step "the source rectangle": (sx, sy, sw, sh), with a
/// negative width or height measured back from its corner. Whatever falls
/// outside the source is transparent black.
[[nodiscard]] paint::bitmap crop(const paint::bitmap & source, double sx, double sy, double sw,
                                 double sh) {
    if (sw < 0) {
        sx += sw;
        sw = -sw;
    }
    if (sh < 0) {
        sy += sh;
        sh = -sh;
    }
    const auto x0 = static_cast<long long>(std::floor(sx));
    const auto y0 = static_cast<long long>(std::floor(sy));
    // Cap each side at 16384 pixels: the caller already rejects crops above
    // largest_area, and this keeps a page from choosing the size of the
    // allocation through the crop rectangle.
    constexpr double largest = 16384.0;
    const int w = static_cast<int>(std::min(std::ceil(sw), largest));
    const int h = static_cast<int>(std::min(std::ceil(sh), largest));
    paint::bitmap out{w, h};
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const long long from_x = x0 + x;
            const long long from_y = y0 + y;
            if (from_x < 0 || from_y < 0 || from_x >= source.width || from_y >= source.height) {
                continue;
            }
            out.put(x, y, source.at(static_cast<int>(from_x), static_cast<int>(from_y)));
        }
    }
    return out;
}

} // namespace

void dom_bindings::install_image_bitmap(context & cx) {
    const interface_pair made =
        make_interface(cx, "ImageBitmap", [](context & c, std::span<value>) {
            c.throw_error("TypeError", "ImageBitmap: Illegal constructor");
            return value::undefined();
        });
    const value proto = value::object(made.proto);
    // The image-store handle of `this`, or nullopt if `this` is not an
    // ImageBitmap. A detached bitmap's handle no longer resolves in images_.
    const auto bitmap_handle = [](context & c) -> std::optional<int> {
        const value self = c.current_this();
        if (!self.is_object()) { return std::nullopt; }
        const value * held =
            static_cast<script::object_object *>(self.as_heap())->find(image_bitmap_property);
        if (held == nullptr || !held->is_number()) { return std::nullopt; }
        return static_cast<int>(context::to_number(*held));
    };
    const auto dimension = [this, bitmap_handle](bool width) {
        return [this, bitmap_handle, width](context & c, std::span<value>) {
            const std::optional<int> handle = bitmap_handle(c);
            const std::shared_ptr<const paint::bitmap> held =
                handle && images_ != nullptr ? images_->at(*handle) : nullptr;
            return value::number(held == nullptr ? 0.0 : width ? held->width : held->height);
        };
    };
    define_getter(cx, *made.proto, "width", dimension(true));
    define_getter(cx, *made.proto, "height", dimension(false));
    set_method(cx, *made.proto, "close", [this, bitmap_handle](context & c, std::span<value>) {
        if (const std::optional<int> handle = bitmap_handle(c); handle && images_ != nullptr) {
            images_->release(*handle);
        }
        return value::undefined();
    });

    cx.define_native("createImageBitmap", [this, proto](context & c, std::span<value> a) {
        const auto reject = [&](std::string_view name, std::string message) {
            const value error = name == "TypeError" || name == "RangeError"
                                    ? c.make_error(std::string{name}, message)
                                    : make_dom_exception(c, name, std::move(message));
            return c.make_promise(error, true);
        };
        const value source = arg(a, 0);
        if (!source.is_object_like()) {
            return reject("TypeError", "createImageBitmap: the source is not an image");
        }
        // The crop, when there is one: all four or none.
        const bool cropped = a.size() >= 5;
        double sx = 0, sy = 0, sw = 0, sh = 0;
        if (cropped) {
            // IDL `long`s: a NaN or an infinity is 0, never a cast of one.
            sx = context::to_int32(a[1]);
            sy = context::to_int32(a[2]);
            sw = context::to_int32(a[3]);
            sh = context::to_int32(a[4]);
            if (c.throw_pending()) { return value::undefined(); }
            if (sw == 0 || sh == 0) {
                return reject("RangeError", "createImageBitmap: the crop is empty");
            }
            if (std::fabs(sw) * std::fabs(sh) > largest_area) {
                return reject("RangeError", "createImageBitmap: the crop is too large");
            }
        }
        std::shared_ptr<const paint::bitmap> pixels;
        auto * object =
            source.is_object() ? static_cast<script::object_object *>(source.as_heap()) : nullptr;
        if (object != nullptr && object->find(image_bitmap_property) != nullptr) {
            // Another ImageBitmap; a detached one is an InvalidStateError.
            pixels = images_ == nullptr ? nullptr
                                        : images_->at(static_cast<int>(context::to_number(
                                              *object->find(image_bitmap_property))));
            if (pixels == nullptr) {
                return reject("InvalidStateError", "createImageBitmap: the bitmap is detached");
            }
        } else if (const node_id element = handle_of(source)) {
            // <img> and <canvas>; an image not (yet) decoded has nothing.
            const auto txn = doc_->read();
            const std::string_view tag = atoms_->text(txn.tag(element).value_or(atom{}));
            if (tag != "img" && tag != "canvas") {
                return reject("TypeError", "createImageBitmap: the source is not an image");
            }
            pixels = image_argument(source);
            if (pixels == nullptr || pixels->width == 0 || pixels->height == 0) {
                return reject("InvalidStateError",
                              "createImageBitmap: the source has no image data");
            }
        } else if (c.instance_of(source, c.global("Blob"))) {
            // A Blob: its bytes decoded as an image, or the InvalidStateError.
            std::vector<std::uint8_t> bytes;
            (void)buffer_source_bytes(c, source, bytes);
            paint::bitmap decoded = images_ == nullptr
                                        ? paint::bitmap{}
                                        : images_->decode(std::as_bytes(std::span{bytes}), {});
            if (decoded.empty()) {
                return reject("InvalidStateError",
                              "createImageBitmap: the blob is not an image that decodes");
            }
            pixels = std::make_shared<const paint::bitmap>(std::move(decoded));
        } else if (c.instance_of(source, c.global("ImageData"))) {
            // An ImageData: RGBA bytes, row by row.
            // Its width and height are only believed when they describe its
            // data exactly: a page can shadow them, and the loop below trusts
            // them for an allocation.
            const double dw = context::to_number(c.lookup_property(source, "width"));
            const double dh = context::to_number(c.lookup_property(source, "height"));
            std::vector<std::uint8_t> rgba;
            if (!buffer_source_bytes(c, c.lookup_property(source, "data"), rgba) || !(dw >= 1) ||
                !(dh >= 1) || dw * dh > largest_area ||
                dw * dh * 4 != static_cast<double>(rgba.size())) {
                return reject("InvalidStateError", "createImageBitmap: the ImageData is detached");
            }
            const auto w = static_cast<int>(dw);
            const auto h = static_cast<int>(dh);
            paint::bitmap image{w, h};
            for (int y = 0; y < h; ++y) {
                for (int x = 0; x < w; ++x) {
                    const std::size_t at =
                        (static_cast<std::size_t>(y) * static_cast<std::size_t>(w) +
                         static_cast<std::size_t>(x)) *
                        4;
                    if (at + 3 >= rgba.size()) { continue; }
                    image.put(x, y,
                              (std::uint32_t{rgba[at + 3]} << 24) |
                                  (std::uint32_t{rgba[at]} << 16) |
                                  (std::uint32_t{rgba[at + 1]} << 8) | rgba[at + 2]);
                }
            }
            pixels = std::make_shared<const paint::bitmap>(std::move(image));
        } else {
            return reject("TypeError", "createImageBitmap: the source is not an image");
        }
        if (cropped) {
            pixels = std::make_shared<const paint::bitmap>(crop(*pixels, sx, sy, sw, sh));
        }
        if (images_ == nullptr) {
            return reject("InvalidStateError", "createImageBitmap: no image store");
        }
        auto * bitmap = c.allocate<script::object_object>();
        bitmap->prototype = proto;
        bitmap->define(image_bitmap_property,
                       value::number(static_cast<double>(images_->adopt(std::move(pixels)))),
                       script::attr_none);
        return c.make_promise(value::object(bitmap), false);
    });
}

} // namespace ctbrowser::shell
