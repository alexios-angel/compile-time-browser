#pragma once

#include "../internal.hpp"

namespace ctbrowser::shell::range_detail {

inline constexpr unsigned type_element = 1;
inline constexpr unsigned type_attr = 2;
inline constexpr unsigned type_text = 3;
inline constexpr unsigned type_cdata = 4;
inline constexpr unsigned type_pi = 7;
inline constexpr unsigned type_comment = 8;
inline constexpr unsigned type_document = 9;
inline constexpr unsigned type_doctype = 10;
inline constexpr unsigned type_fragment = 11;

inline constexpr std::string_view start_node_slot = "__startContainer";
inline constexpr std::string_view start_offset_slot = "__startOffset";
inline constexpr std::string_view end_node_slot = "__endContainer";
inline constexpr std::string_view end_offset_slot = "__endOffset";

// A NODE OF THE REALM, as a range sees it: the bindings that own it and its
// id - or an Attr object, which has neither.
struct spot {
    dom_bindings * owner = nullptr;
    node_id id;
    value attr = value::undefined();
    [[nodiscard]] bool none() const { return owner == nullptr && attr.is_undefined(); }
    [[nodiscard]] bool is_attr() const { return !attr.is_undefined(); }
    [[nodiscard]] bool operator==(const spot & other) const {
        if (is_attr() || other.is_attr()) {
            return is_attr() && other.is_attr() && attr.bits() == other.attr.bits();
        }
        return owner == other.owner && id == other.id;
    }
};
inline const spot no_spot{};

struct point {
    spot node;
    double offset = 0;
};

// UTF-16 CODE UNITS OVER UTF-8 BYTES: every CharacterData offset is one, and
// `length` counts them, so a boundary inside "🌠" is at 1 of 2.
[[nodiscard]] inline std::size_t units_length(std::string_view text) {
    std::size_t n = 0;
    for (std::size_t at = 0; at < text.size();) { n += decode_utf8(text, at) >= 0x10000 ? 2 : 1; }
    return n;
}

[[nodiscard]] inline script::object_object * self_object(context & c) {
    const value self = c.current_this();
    if (!self.is_object()) {
        c.throw_error("TypeError", "Illegal invocation");
        return nullptr;
    }
    return static_cast<script::object_object *>(self.as_heap());
}

[[nodiscard]] inline value slot(script::object_object * self, std::string_view name) {
    const value * held = self->find(name);
    return held == nullptr ? value::undefined() : *held;
}

} // namespace ctbrowser::shell::range_detail
