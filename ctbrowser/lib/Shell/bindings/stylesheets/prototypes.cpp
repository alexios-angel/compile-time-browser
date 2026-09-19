// dom_bindings' CSSOM - install_stylesheet_prototypes: the CSSStyleSheet,
// CSSRuleList, CSSRule, MediaList and CSSStyleDeclaration interfaces.

#include "internal.hpp"

#include <ctbrowser/shell/net/url.hpp>

namespace ctbrowser::shell {

using namespace detail;

// --- the interfaces ---------------------------------------------------------

void dom_bindings::install_stylesheet_prototypes(context & cx) {
    script::object_object * internals = cssom_internals(cx);
    if (internals == nullptr) { return; }
    install_stylesheet_collections(cx, internals);
    install_stylesheet_rules(cx, internals);
    install_stylesheet_declarations(cx, internals);
}

} // namespace ctbrowser::shell
