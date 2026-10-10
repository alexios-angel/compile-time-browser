//===- Browser.hpp - Native DOM element and document views ------*- C++ -*-===//
//
// js_element_t and js_document_t: the views generated native DOM code uses
// for elements and the document, and document_scope, which binds the
// document that native calls on this thread see.
//
// A DOM entry defines CTNATIVE_DOM before including either runtime header, so
// primitive-only programs and raw element_ref clients never pull in the style
// engine.
//
//===----------------------------------------------------------------------===//

#pragma once

#ifndef CTNATIVE_DOM
#error "Define CTNATIVE_DOM before including the native browser views"
#endif

#include "ctnative.hpp"

#include <ctbrowser/style/engine.hpp>

namespace ctnative {

/// A borrowed view of one element and the style engine used for selector
/// matching. Views own nothing: the document, atom table and style engine must
/// stay alive at stable addresses, as for element_ref and std::span. Copies keep
/// the association; detaching a node does not change its identity. A selector
/// that finds nothing gives null (an empty optional); undefined is never an
/// element.
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

    /// Element.tagName (DOM §4.9): the qualified name, ASCII-uppercased for an
    /// HTML element in an HTML document, as the VM's element wrapper spells it.
    js_string tagName() const {
        const ctbrowser::element_ref element = value();
        const auto txn = element.owner->read();
        std::string name{
            element.owner->atoms().text(txn.tag(element.id).value_or(ctbrowser::atom{}))};
        if (txn.element_ns(element.id) == ctbrowser::node_ns::html && !element.owner->xml()) {
            ctbrowser::ascii_upper_in_place(name);
        }
        return js_string{std::move(name)};
    }
    /// Element.id (DOM §4.9): the `id` attribute, or "" without one.
    js_string id() const { return reflected("id"); }
    /// Element.className (DOM §4.9): the `class` attribute, or "" without one.
    js_string className() const { return reflected("class"); }
    /// ParentNode.children (DOM §4.2.6): the element children in tree order.
    /// A snapshot, where the VM's HTMLCollection is live: a program that
    /// changes the children while holding the result must not be admitted.
    std::vector<js_element_t> children() const {
        const ctbrowser::element_ref element = value();
        const auto txn = element.owner->read();
        std::vector<js_element_t> snapshot;
        for (const ctbrowser::node_id child : txn.children(element.id)) {
            if (txn.tag(child).has_value()) {
                snapshot.emplace_back(ctbrowser::element_ref{element.owner, child}, *styles);
            }
        }
        return snapshot;
    }

private:
    /// A reflected DOMString attribute (HTML §2.6.1): the content attribute's
    /// value, or "" when it is absent.
    js_string reflected(std::string_view attribute) const {
        const ctbrowser::element_ref element = value();
        return js_string{
            ctbrowser::get_element_attribute(*element.owner, element.id, attribute).value_or("")};
    }
};

/// A borrowed view of a document and its style engine, for document-level
/// queries. Owns nothing, like js_element_t.
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

/// Makes `borrowed` the document that native calls on this thread see
/// (through document_object) for the lifetime of the scope. Nested scopes
/// restore their caller's document on exit, including when they throw. The
/// document and engine must outlive the scope.
class document_scope {
    inline static thread_local const js_document_t * current = nullptr;
    js_document_t bound;
    const js_document_t * previous;

    friend struct document_object;
    static const js_document_t & active() {
        if (!current) { throw std::logic_error("No native document scope is active"); }
        return *current;
    }

public:
    document_scope(ctbrowser::document & borrowed, ctbrowser::style::engine & engine)
        : bound(borrowed, engine), previous(current) {
        current = &bound;
    }
    document_scope(const document_scope &) = delete;
    document_scope & operator=(const document_scope &) = delete;
    document_scope(document_scope &&) = delete;
    document_scope & operator=(document_scope &&) = delete;
    ~document_scope() { current = previous; }
};

inline std::optional<js_element_t> document_object::documentElement() const {
    return document_scope::active().documentElement();
}
inline std::optional<js_element_t> document_object::querySelector(
    const js_string & selector) const {
    return document_scope::active().querySelector(selector);
}
inline std::vector<js_element_t> document_object::querySelectorAll(
    const js_string & selector) const {
    return document_scope::active().querySelectorAll(selector);
}

/// `document.documentElement` where the program uses it as an element without
/// a null check (css-dump.js's walk starts there). A document with no root
/// element throws; the VM throws a TypeError at the first member read of the
/// null, so the message names it. Like the other DOM errors in this header it
/// is a C++ exception native `catch` cannot see, so admission keeps a `try`
/// around such a read refused.
inline js_element_t require_document_element() {
    std::optional<js_element_t> root = document.documentElement();
    if (!root) { throw std::invalid_argument("TypeError: document.documentElement is null"); }
    return *std::move(root);
}

/// Converts an optional view to the plain element_ref carrier, with an empty
/// element_ref for null.
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
