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

std::optional<std::string> get_element_attribute(document & doc, node_id id,
                                                 std::string_view name) {
    const atom key = attribute_key(doc, id, name);
    const attribute * held = doc.read().find_attribute(id, key);
    return held == nullptr ? std::nullopt : std::optional{held->value};
}

std::expected<void, dom_error> set_element_attribute(document & doc, node_id id,
                                                     std::string_view name, std::string_view text) {
    if (!is_valid_attribute_name(name)) {
        return std::unexpected{dom_error::invalid_attribute_name};
    }
    return doc.set_attribute(id, attribute_key(doc, id, name), text);
}

std::expected<attribute_toggle_result, dom_error> toggle_element_attribute(
    document & doc, node_id id, std::string_view name, std::optional<bool> force) {
    if (!is_valid_attribute_name(name)) {
        return std::unexpected{dom_error::invalid_attribute_name};
    }
    const atom key = attribute_key(doc, id, name);
    const bool present = doc.read().has_attribute(id, key);
    const bool want = force.value_or(!present);
    if (want == present) { return attribute_toggle_result{present, false}; }
    const auto written = want ? doc.set_attribute(id, key, "") : doc.remove_attribute(id, key);
    attribute_toggle_result result{want, true};
    if (!written) { result.update = std::unexpected{written.error()}; }
    return result;
}

} // namespace ctbrowser
