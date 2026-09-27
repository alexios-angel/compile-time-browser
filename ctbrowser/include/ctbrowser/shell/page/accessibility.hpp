#pragma once

// The accessibility tree's two answers a driver can ask for - an element's
// computed ROLE and its accessible NAME - and the tables they are read from.
// The computation itself walks the bindings' view of the page (the
// ElementInternals defaults and the element-reference attributes live there),
// so it is dom_bindings::computed_accessibility, in lib/Shell/page/
// accessibility.cpp beside these.

#include <ctbrowser/core/core.hpp>
#include <ctbrowser/dom/document.hpp>

#include <string_view>

namespace ctbrowser::shell::accessibility {

// The role a `role` attribute - or ElementInternals' `role` - names: its first
// token that is a concrete ARIA role, compared ASCII case-insensitively, with
// the synonyms folded to the name a computed role reports (`img` is `image`,
// `presentation` is `none`, `directory` is `list`). Empty when no token is.
[[nodiscard]] std::string_view explicit_role(std::string_view tokens);

// HTML-AAM's implicit role for an element with no explicit one: `generic` for
// an element that maps to nothing more specific, empty for one that is not in
// the accessibility tree at all (an <input type=hidden>).
[[nodiscard]] std::string_view implicit_role(const read_txn & txn, atom_table & atoms,
                                             node_id element);

// ARIA 1.2's "roles supporting name from content" - the ones whose own name is
// their subtree's text when nothing else names them.
[[nodiscard]] bool names_from_content(std::string_view role);

// The embedded controls of Accessible Name 1.2 step 2C - a control inside a
// label contributes its VALUE to the name it is part of.
[[nodiscard]] bool is_embedded_control(std::string_view role);

} // namespace ctbrowser::shell::accessibility
