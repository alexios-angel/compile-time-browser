#include <ctbrowser/dom/element.hpp>

namespace ctbrowser {

std::expected<void, dom_error> validate_element(element_ref element) {
    if (element.owner == nullptr) { return std::unexpected{dom_error::no_such_node}; }
    const auto kind = element.owner->read().kind(element.id);
    if (!kind) { return std::unexpected{kind.error()}; }
    if (*kind != node_kind::element) { return std::unexpected{dom_error::not_an_element}; }
    return {};
}

bool is_valid_attribute_name(std::string_view name) {
    // Attribute names follow the HTML serialization rule, not XML Name:
    // punctuation and initial digits are allowed. All forbidden bytes are
    // ASCII, so UTF-8 needs no decoding. U+0000 would round-trip as U+FFFD.
    constexpr std::string_view breaks = "\t\n\f\r /=>";
    return !name.empty() && name.find_first_of(breaks) == std::string_view::npos &&
           name.find('\0') == std::string_view::npos;
}

atom attribute_key(document & doc, node_id id, std::string_view qualified) {
    const bool folds = doc.read().element_ns(id) == node_ns::html && !doc.xml();
    return folds ? doc.atoms().intern_lower(qualified) : doc.atoms().intern(qualified);
}

std::expected<void, dom_error> set_element_attribute(document & doc, node_id id,
                                                     std::string_view name, std::string_view text) {
    if (!is_valid_attribute_name(name)) {
        return std::unexpected{dom_error::invalid_attribute_name};
    }
    return doc.set_attribute(id, attribute_key(doc, id, name), text);
}

} // namespace ctbrowser
