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

} // namespace ctbrowser::shell::range_detail
