// early errors - the entry: build a checker over the tree, walk it, report the
// first error in source order.
//
// One of six files carved out of a 1,476-line compile/early_errors.cpp on
// 2026-09-08 - the checker was one class with every member inline, as
// compile.cpp was before it became compile/. The class is declared in
// checker.hpp beside this, in ctbrowser::script::detail::early; the bodies are
// where they were, in the file that owns the concern. early_errors.hpp, the
// public face of the pass, did not change.

#include "checker.hpp"

namespace ctbrowser::script::detail {

namespace early {

void checker::run() {
    frames_.push_back(frame{frame_kind::script, {}, {}, 0, 0, false, false});
    const vp::node & root = at(ast_.root);
    if (root.kind != nk::program) { return; }
    frames_.back().strict = strict_root_ || has_use_strict_directive(ast_.root);
    (void)check_list(kids(root), list_kind::script, nullptr, "");
    frames_.pop_back();
}

} // namespace early

using early::checker;

std::optional<early_error> find_early_error(const ctjs::vp::ast & tree, std::string_view source,
                                            bool strict_root) {
    if (!tree.ok || tree.root < 0) { return std::nullopt; }
    checker walk{tree, source, strict_root};
    walk.run();
    return walk.result();
}

} // namespace ctbrowser::script::detail
