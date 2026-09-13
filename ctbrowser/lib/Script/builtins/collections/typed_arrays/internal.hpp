#pragma once
// Private to lib/Script/builtins/collections/typed_arrays/: what ArrayBuffer,
// %TypedArray%, the eleven concrete constructors, DataView and the Uint8Array
// base64/hex statics share. The one-file typed_arrays.cpp of 2026-09-08 was
// split here on 2026-09-12 when it grew the rest of clauses 23.2, 25.1 and
// 25.3.
//
// THE STORAGE MODEL, in one place.
//
// An ArrayBuffer is an ordinary object on ArrayBuffer.prototype whose
// [[ArrayBufferData]] is the `__bytes` property: an array_object of
// element_kind::u8 holding one `value` per byte. That array - the STORE - is
// what a typed array's `viewed` points at (value.hpp, array_object::viewed) and
// what the VM's view_get/view_set read and write. It is a visible property and
// not a private slot because lib/Shell (fetch, Blob, WebGL) built the same
// shape first and reads it by that name; it is non-enumerable, non-writable
// and non-configurable, so Object.keys and JSON never see it.
//
// The store's own `named` table carries what the buffer knows about itself
// under private keys (value.hpp says a `@#` key is exactly as invisible as an
// internal slot): the maxByteLength when resizable, the detached flag, and
// the list of views whose length has to follow a resize or a detach - the
// VM reads `view_length` off a view without asking anybody, so the buffer
// has to write it. A view registers itself when it is made over a resizable
// store, or by a constructor over any store; a `subarray` of a fixed-length
// buffer is not registered.
// ponytail: the register is a strong list, so a page that makes a fresh
// `new Float32Array(buffer, ...)` every frame over one buffer keeps every view
// alive as long as the buffer is. A weak list wants collector support.
//
// A typed array is an array_object with `elements` set to its kind, and its
// elements in ONE of two places. Made over a buffer - `new Uint8Array(buf,
// 4, 8)`, `subarray` - it is a VIEW: `viewed` is the store, `byte_offset` and
// `view_length` are in place, and `named` carries `@#ByteOffset`, plus either
// `@#LengthTracking` (the view follows the buffer's length) or `@#ArrayLength`
// (its fixed element count, for re-checking bounds after a resize). Made from
// a length, an array, an iterable or another typed array it OWNS its
// elements in `items`, exactly as a Shell-made one does (ImageData.data, a
// fetch body) - and stays that way until something asks for its `buffer` or
// a `subarray`, when ensure_store moves the elements into a fresh store IN
// PLACE, so the aliasing a page then relies on is real. Owning is the
// default because lib/Shell reads `items` off the typed arrays a page hands
// it (readPixels, putImageData) and a view's `items` is empty by design.
// Every accessor below takes both shapes.

#include "../../internal.hpp"

#include <optional>

namespace ctbrowser::script::builtins_detail {

inline constexpr std::string_view bytes_key = "__bytes";
inline constexpr std::string_view store_max_key = "@#ArrayBufferMaxByteLength";
inline constexpr std::string_view store_detached_key = "@#ArrayBufferDetached";
inline constexpr std::string_view store_immutable_key = "@#ArrayBufferImmutable";
inline constexpr std::string_view store_views_key = "@#ArrayBufferViews";
inline constexpr std::string_view store_buffer_key = "@#ArrayBuffer";
inline constexpr std::string_view view_tracking_key = "@#LengthTracking";
inline constexpr std::string_view view_length_key = "@#ArrayLength";
inline constexpr std::string_view view_offset_key = "@#ByteOffset";
inline constexpr std::string_view data_view_store_key = "@#DataViewStore";
inline constexpr std::string_view data_view_buffer_key = "@#DataViewBuffer";
inline constexpr std::string_view data_view_offset_key = "@#DataViewByteOffset";
inline constexpr std::string_view data_view_length_key = "@#DataViewByteLength";

// THE LARGEST BUFFER THIS ENGINE WILL MAKE. A byte is a `value` here - eight
// bytes of host memory per byte of buffer - so the cap is 2^28 bytes (2 GiB of
// host memory); CreateByteDataBlock's "impossible to allocate" RangeError
// (6.2.9.1) is the honest answer past it.
inline constexpr double max_buffer_bytes = 268435456.0;

// --- the store -----------------------------------------------------------

// The store behind an ArrayBuffer-shaped object - its OWN `__bytes` - or null.
[[nodiscard]] array_object * buffer_store(value buffer);
// A fresh ArrayBuffer of `byte_length` zero bytes on ArrayBuffer.prototype -
// or `into`, the instance `new` already made, filled in place - resizable up
// to `max` when given. Undefined with a RangeError in flight when it cannot
// be made.
[[nodiscard]] value make_array_buffer(context & cx, double byte_length, std::optional<double> max,
                                      value into = value::undefined());
// SpeciesConstructor (7.3.22): `default_ctor` unless O's `constructor` names
// a @@species. Undefined with the TypeError in flight.
[[nodiscard]] value species_constructor(context & cx, value o, value default_ctor);
// The ArrayBuffer object a store belongs to: the one it was made with, or a
// fresh wrapper over the same store (a Shell-made store has none).
[[nodiscard]] value buffer_of_store(context & cx, array_object * store);
[[nodiscard]] bool store_detached(const array_object * store);
// IsImmutableBuffer (the immutable-arraybuffer proposal): made by
// transferToImmutable or sliceToImmutable, fixed-length, and refused by
// every method that writes. The VM's own `ta[i] = v` path does not ask.
[[nodiscard]] bool store_immutable(const array_object * store);
[[nodiscard]] std::optional<double> store_max_byte_length(array_object * store);
[[nodiscard]] inline bool store_resizable(array_object * store) {
    return store_max_byte_length(store).has_value();
}
// DetachArrayBuffer (25.1.3.5): the bytes go, and every registered view
// reads as length 0 from then on.
void detach_store(context & cx, array_object * store);
// The resize behind ArrayBuffer.prototype.resize: the store grows with zeros
// or shrinks, and every registered view is re-bounded.
void resize_store(context & cx, array_object * store, std::size_t byte_length);
void register_view(context & cx, array_object * store, array_object * view);

// --- typed arrays --------------------------------------------------------

[[nodiscard]] inline bool is_typed_array(value v) {
    return v.is_array() && static_cast<array_object *>(v.as_heap())->elements != element_kind::none;
}
// The store a typed array views, or null for one that owns its elements.
[[nodiscard]] inline array_object * typed_array_store(array_object * arr) {
    return arr->is_view() ? static_cast<array_object *>(arr->viewed.as_heap()) : nullptr;
}
// The store a typed array views, MADE if it owns its elements: the elements
// move into a fresh fixed-length buffer and the array becomes a view over it,
// in place - the header says why this is deferred until asked. Null with a
// RangeError in flight when the buffer cannot be made.
[[nodiscard]] array_object * ensure_store(context & cx, array_object * arr);
// IsTypedArrayOutOfBounds (10.4.5.12) including the detached case.
[[nodiscard]] bool typed_array_out_of_bounds(array_object * arr);
[[nodiscard]] bool typed_array_length_tracking(array_object * arr);
// ValidateTypedArray (23.2.4.4): `v` as a typed array that is neither
// detached nor out of bounds - nor over an immutable buffer when the caller
// means to `write` - or null with the TypeError in flight.
[[nodiscard]] array_object * validate_typed_array(context & cx, value v, const char * method,
                                                  bool write = false);
// The receiver of a %TypedArray%.prototype method, validated.
[[nodiscard]] inline array_object * this_typed_array(context & cx, const char * method) {
    return validate_typed_array(cx, cx.current_this(), method);
}
// TypedArrayLength, the element count as of now.
[[nodiscard]] inline std::size_t typed_array_length(array_object * arr) {
    return arr->length();
}
// One element, read and written the way the VM's own index path does it:
// undefined past the current length, coerced to the kind on write and
// dropped past the length. The ToNumber of the value is the CALLER's - it can
// run script, and the specification orders it before the index check.
[[nodiscard]] value typed_array_get(array_object * arr, std::size_t i);
void typed_array_set(array_object * arr, std::size_t i, double v);
// A fresh typed array of `kind` OWNING `length` zero elements; undefined with
// a RangeError in flight when it cannot be made.
[[nodiscard]] value allocate_typed_array(context & cx, element_kind kind, double length);
// A fresh typed array of `kind` over `store` from `byte_offset`, either of a
// fixed `length` or length-tracking when `length` is empty. No checks: the
// caller did 23.2.5.1.3's. Registered with the store when the store is
// resizable or `always_register` says so (the constructors do; `subarray`
// over a fixed-length buffer does not - the header says why).
[[nodiscard]] value make_typed_array_view(context & cx, element_kind kind, array_object * store,
                                          std::size_t byte_offset,
                                          std::optional<std::size_t> length, bool always_register);
// The global constructor for a kind.
[[nodiscard]] value typed_array_constructor(context & cx, element_kind kind);
// TypedArraySpeciesCreate (23.2.4.1) and TypedArrayCreateFromConstructor
// (23.2.4.2): undefined with the throw in flight.
[[nodiscard]] value typed_array_species_create(context & cx, array_object * exemplar,
                                               std::span<const value> args, bool write = false);
[[nodiscard]] value typed_array_create_from_constructor(context & cx, value ctor,
                                                        std::span<const value> args,
                                                        bool write = false);
// ToIndex (7.1.22): false with the RangeError (or a Symbol's TypeError) in flight.
[[nodiscard]] bool to_index(context & cx, value v, double & out);
// ToIntegerOrInfinity through ToPrimitive: false with a throw in flight.
[[nodiscard]] bool to_integer_or_infinity(context & cx, value v, double & out);
// A relative index the way `slice` and `fill` clamp one: negative from the
// end, then into [0, len].
[[nodiscard]] std::size_t relative_index(double rel, std::size_t len);
// SortCompare over numbers with an optional comparator; a bottom-up merge sort
// that survives any comparator. False with a throw in flight.
[[nodiscard]] bool sort_numbers(context & cx, std::vector<double> & work, value comparator);

// The installers, called in order from install_typed_arrays.
void install_array_buffer(context & cx);
void install_typed_array_prototype(context & cx, object_object * typed_proto);
void install_data_view(context & cx);
void install_uint8array_codecs(context & cx, native_object * uint8_ctor,
                               object_object * uint8_proto);

} // namespace ctbrowser::script::builtins_detail
