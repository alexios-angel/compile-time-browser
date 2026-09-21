#pragma once

// DOM entries opt in before including either runtime header. Keep the Style
// implementation out of primitive-only and raw element_ref clients.
#ifndef CTNATIVE_DOM
#error "Define CTNATIVE_DOM before including the native browser views"
#endif

#include "ctnative.hpp"

#include <ctbrowser/style/engine.hpp>

namespace ctnative {

// These views own nothing. Their document, atom table and Style engine must
// remain alive at stable addresses, as for element_ref and std::span. Copies
// preserve the association; detaching a node does not invalidate its identity.
// Selector absence is null, represented by optional; undefined is not an element.
class js_element_t {
    ctbrowser::element_ref element;
    ctbrowser::style::engine * styles;

    std::optional<js_element_t> result(ctbrowser::element_ref found) const {
        if (!found.owner) { return std::nullopt; }
        return js_element_t{found, *styles};
    }

public:
    explicit js_element_t(ctbrowser::element_ref borrowed, ctbrowser::style::engine & engine)
        : element(borrowed), styles(&engine) {
        (void)value();
    }

    ctbrowser::element_ref value() const {
        require_element(element);
        require_style(element, *styles);
        return element;
    }
    friend bool operator==(const js_element_t & a, const js_element_t & b) noexcept {
        return a.element == b.element;
    }

    js_boolean_t matches(const js_string & selector) const {
        return Element.prototype.matches.call(value(), *styles, selector.value());
    }
    std::optional<js_element_t> closest(const js_string & selector) const {
        return result(Element.prototype.closest.call(value(), *styles, selector.value()));
    }
    std::optional<js_element_t> querySelector(const js_string & selector) const {
        return result(Element.prototype.querySelector.call(value(), *styles, selector.value()));
    }
    std::vector<js_element_t> querySelectorAll(const js_string & selector) const {
        const auto found =
            Element.prototype.querySelectorAll.call(value(), *styles, selector.value());
        std::vector<js_element_t> snapshot;
        snapshot.reserve(found.size());
        for (const auto child : found) { snapshot.emplace_back(child, *styles); }
        return snapshot;
    }
};

class js_document_t {
    ctbrowser::document * document;
    ctbrowser::style::engine * styles;

    std::vector<ctbrowser::node_id> select(const js_string & selector, bool firstOnly) const {
        require_style(*document, *styles);
        const auto parsed = parse_selector(*document, selector.value());
        return styles->select(document->read(), {}, parsed.selectors, firstOnly);
    }

public:
    explicit js_document_t(ctbrowser::document & borrowed, ctbrowser::style::engine & engine)
        : document(&borrowed), styles(&engine) {
        require_style(*document, *styles);
    }

    std::optional<js_element_t> documentElement() const {
        require_style(*document, *styles);
        const auto root = document->root();
        if (document->read().kind(root).value() != ctbrowser::node_kind::element) {
            return std::nullopt;
        }
        return js_element_t{{document, root}, *styles};
    }
    std::optional<js_element_t> querySelector(const js_string & selector) const {
        const auto found = select(selector, true);
        if (found.empty()) { return std::nullopt; }
        return js_element_t{{document, found.front()}, *styles};
    }
    std::vector<js_element_t> querySelectorAll(const js_string & selector) const {
        const auto found = select(selector, false);
        std::vector<js_element_t> snapshot;
        snapshot.reserve(found.size());
        for (const auto id : found) {
            snapshot.emplace_back(ctbrowser::element_ref{document, id}, *styles);
        }
        return snapshot;
    }
};

// Bridge the proved null-only view result to the existing native element carrier.
inline ctbrowser::element_ref element_or_null(const std::optional<js_element_t> & element) {
    return element ? element->value() : ctbrowser::element_ref{};
}

inline js_boolean_t matches_method::call(const js_element_t & element,
                                         const js_string & selector) const {
    return element.matches(selector);
}
inline std::optional<js_element_t> closest_method::call(const js_element_t & element,
                                                        const js_string & selector) const {
    return element.closest(selector);
}
inline std::optional<js_element_t> query_selector_method::call(const js_element_t & element,
                                                               const js_string & selector) const {
    return element.querySelector(selector);
}
inline std::vector<js_element_t> query_selector_all_method::call(const js_element_t & element,
                                                                 const js_string & selector) const {
    return element.querySelectorAll(selector);
}

} // namespace ctnative
