//===- Page.hpp - Native page geometry and computed style -------*- C++ -*-===//
//
// What generated native code asks of a laid-out page beyond the DOM:
// getBoundingClientRect, getComputedStyle(el).getPropertyValue, the viewport
// element's clientWidth and clientHeight, and window.devicePixelRatio. Each
// answer comes from ctbrowser::page (the same functions the VM's bindings
// call), over the page a page_scope binds.
//
// Only programs that ask these include this header; it pulls in the page
// library's headers, which DOM-only programs do not need.
//
//===----------------------------------------------------------------------===//

#pragma once

#ifndef CTNATIVE_DOM
#error "Define CTNATIVE_DOM before including the native page views"
#endif

#include "Browser.hpp"

#include <ctbrowser/page/static_page.hpp>
#include <ctbrowser/style/css/properties.hpp>

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace ctnative {

/// Makes `page` the page and document that native calls on this thread see,
/// for the lifetime of the scope: document_scope over its document, plus the
/// page itself for geometry and computed style. Nested scopes restore their
/// caller's page on exit. The page must outlive the scope.
class page_scope {
    inline static thread_local ctbrowser::page::static_page * current = nullptr;
    document_scope dom;
    ctbrowser::page::static_page * previous;

public:
    explicit page_scope(ctbrowser::page::static_page & page)
        : dom(page.doc(), page.styles()), previous(current) {
        current = &page;
    }
    page_scope(const page_scope &) = delete;
    page_scope & operator=(const page_scope &) = delete;
    page_scope(page_scope &&) = delete;
    page_scope & operator=(page_scope &&) = delete;
    ~page_scope() { current = previous; }

    /// The bound page. Asking with no scope active is a logic error, as for
    /// document_scope.
    static ctbrowser::page::static_page & active() {
        if (!current) { throw std::logic_error("No native page scope is active"); }
        return *current;
    }
    /// The bound page's node for `element`, which must belong to its document.
    static ctbrowser::node_id node_of(const js_element_t & element) {
        const ctbrowser::element_ref ref = element.value();
        if (ref.owner != &active().doc()) {
            throw std::logic_error("The element is not in the page the native scope binds");
        }
        return ref.id;
    }
};

/// A DOMRect's four fields css-dump reads (CSSOM View §7), as Numbers: the
/// float layout coordinates widened to double, as the VM's DOMRect holds them.
struct js_dom_rect {
    js_num left;
    js_num top;
    js_num width;
    js_num height;
};

/// Element.getBoundingClientRect (CSSOM View §7).
inline js_dom_rect getBoundingClientRect(const js_element_t & element) {
    const ctbrowser::rect r =
        page_scope::active().bounding_client_rect(page_scope::node_of(element));
    return {js_num{static_cast<double>(r.x)}, js_num{static_cast<double>(r.y)},
            js_num{static_cast<double>(r.width)}, js_num{static_cast<double>(r.height)}};
}

/// The result of getComputedStyle(element) (CSSOM §9). Live, like the VM's
/// object: a read sees the page as of its last frame(). The page answers the
/// same until its next frame (static_page::frames), so the element's entries are
/// computed at the first read after a frame and looked up by the reads after
/// it, as the VM's object does while its cache is fresh. It borrows the page and
/// names the element; the page must outlive it.
class js_computed_style {
    ctbrowser::page::static_page * page;
    ctbrowser::node_id id;
    mutable std::vector<std::pair<std::string, std::string>> entries;
    mutable std::optional<std::uint64_t> computed_at;

public:
    js_computed_style(ctbrowser::page::static_page & bound, ctbrowser::node_id element)
        : page(&bound), id(element) {}

    /// CSSStyleDeclaration.getPropertyValue (CSSOM §6.6): the value by CSS
    /// name, the IDL spelling accepted too, or "" for a property with none.
    js_string getPropertyValue(const js_string & name) const {
        const std::string css = ctbrowser::style::css::css_name_of(name.value());
        if (computed_at != page->frames()) {
            entries = page->computed_style(id);
            computed_at = page->frames();
        }
        for (const auto & [property, text] : entries) {
            if (property == css) { return js_string{text}; }
        }
        return js_string{};
    }
};

/// window.getComputedStyle(element) (CSSOM §9).
inline js_computed_style getComputedStyle(const js_element_t & element) {
    return js_computed_style{page_scope::active(), page_scope::node_of(element)};
}

/// Element.clientWidth and clientHeight (CSSOM View §7) of the viewport
/// element: the layout viewport. Any other element is refused at run time
/// until its padding-box answer is lifted out of the bindings too.
inline js_num clientWidth(const js_element_t & element) {
    ctbrowser::page::static_page & page = page_scope::active();
    if (page_scope::node_of(element) != page.doc().root()) {
        throw std::logic_error("clientWidth of an element other than the root is not native");
    }
    return js_num{static_cast<double>(page.viewport_client_width())};
}
inline js_num clientHeight(const js_element_t & element) {
    ctbrowser::page::static_page & page = page_scope::active();
    if (page_scope::node_of(element) != page.doc().root()) {
        throw std::logic_error("clientHeight of an element other than the root is not native");
    }
    return js_num{static_cast<double>(page.viewport_client_height())};
}

/// The global `window`, for the properties a page asks of it natively.
struct window_object {
    /// window.devicePixelRatio (CSSOM View §4), which the page answers.
    static constexpr js_num devicePixelRatio{ctbrowser::page::static_page::device_pixel_ratio};
};
inline constexpr window_object window{};

} // namespace ctnative
