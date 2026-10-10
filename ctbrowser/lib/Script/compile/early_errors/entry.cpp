//===- entry.cpp - Early-error entry point ----------------------*- C++ -*-===//
//
// The entry point of the early-error pass: build a checker over the tree,
// walk it, and report the first error in source order. The checker class is
// declared in checker.hpp (namespace ctbrowser::script::detail::early); its
// member bodies live in the sibling file for each concern. early_errors.hpp
// is the pass's public interface.
//
//===----------------------------------------------------------------------===//

#include "checker.hpp"

namespace ctbrowser::script::detail {

namespace early {

void checker::run() {
    frames_.push_back(frame{frame_kind::script, {}, {}, 0, 0, false, false});
    const vp::node & root = at(ast_.root);
    if (root.kind != nk::program) { return; }
    frames_.back().strict = strict_root_ || has_use_strict_directive(ast_.root);
    // An eval's top level behaves as the place the eval was (19.2.1.1 steps
    // 4-7): strict when its caller is (step 12's strictCaller), and a
    // method's, a field initialiser's or a function's for `super`,
    // `arguments` and `new.target`, which run() then answers as it would
    // there.
    if (eval_ != nullptr) {
        frames_.back().strict = frames_.back().strict || eval_->strict;
        frames_.back().what = eval_->in_class_field_initializer ? frame_kind::field_init
                              : eval_->in_method                ? frame_kind::method
                              : eval_->in_function              ? frame_kind::function
                                                                : frame_kind::script;
        frames_.back().super_call_ok = eval_->in_derived_constructor;
        // The private names of the classes around the eval are in scope too.
        if (!eval_->private_names.empty()) {
            private_names_.emplace_back();
            for (const auto & [name, key] : eval_->private_names) {
                private_names_.back().push_back(name);
            }
        }
    }
    // A module's top level is [+Await] (16.2.1): `await` is not a name there.
    frames_.back().is_async = strict_root_;
    // A `using` declaration at the top level of a classic script is not in
    // the grammar (14.3.2: a module's top level, a block, a body).
    if (!strict_root_) {
        for (const std::int32_t s : kids(root)) {
            if (at(s).kind == nk::var_decl &&
                (at(s).text == "using" || at(s).text == "await using")) {
                report("a `using` declaration is not allowed at the top level of a script", s);
            }
        }
    }
    const std::vector<binding> vars = check_list(kids(root), list_kind::script, nullptr, "");
    if (strict_root_) { check_module_items(ast_.root, vars); }
    frames_.pop_back();
}

} // namespace early

using early::checker;

std::optional<early_error> find_early_error(const ctjs::vp::ast & tree, std::string_view source,
                                            bool strict_root, const eval_site * eval,
                                            bool script_goal) {
    if (!tree.ok || tree.root < 0) { return std::nullopt; }
    checker walk{tree, source, strict_root, eval, script_goal};
    walk.run();
    return walk.result();
}

} // namespace ctbrowser::script::detail
