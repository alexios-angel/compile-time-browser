//===- early_errors.hpp - ECMAScript early errors ---------------*- C++ -*-===//
//
// The early errors: the source text a conforming implementation must refuse
// before it evaluates any of it.
//
// A JavaScript engine has two ways to say no. The parser says no to text that
// is not in the grammar (`var a = ;`), and the static semantics say no to text
// that parses perfectly and is still not a program: `let x; let x;`,
// `1 = 2`, `break` with nothing to break out of. The specification calls the
// second kind Early Errors, and clause 17 is explicit that they are reported
// "prior to the first evaluation of the source text", so an engine that runs
// `let x; let x;` is not being lenient, it is accepting a different language.
//
// This pass lives beside the compiler rather than inside it for two reasons.
// It walks the tree once, before any code is emitted, which is what "before
// the first evaluation" means and what the compiler, which emits as it walks,
// cannot promise. And it needs nothing from the compiler but the AST, so it
// can be exercised on its own.
//
// Private to lib/Script/compile/, like compiler_impl.hpp beside it: it
// includes <ctjs/vparse.hpp>, and test/lint/api_surface polices third-party
// headers in include/.
//
// Strict-mode rules (`delete x`, assignment to `eval`, legacy octal literals,
// duplicate simple parameters) apply only where the code is strict: a module
// (`strict_root`), a "use strict" body, or an eval that inherits strictness.
// Sloppy code accepts them. A few rules are deliberately relaxed for web
// compatibility and are marked where they are checked.
//
//===----------------------------------------------------------------------===//
#pragma once

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <ctbrowser/script/compile.hpp>

#include <ctjs/vparse.hpp>

namespace ctbrowser::script::detail {

/// One refused construct: a message and where it is.
struct early_error {
    /// What is wrong, written for a person: the construct and the name it is
    /// wrong about. Never a clause number alone.
    std::string message;
    /// Where, as a byte offset into the source the AST was parsed from, or
    /// `nowhere` when the offending node carries no lexeme to subtract against.
    /// The same trick compiler_impl::offset_of uses: `node::text` is a view
    /// into the source, so its address gives its position.
    static constexpr std::size_t nowhere = static_cast<std::size_t>(-1);
    std::size_t offset = nowhere;
};

/// Returns the first early error in `tree`, in source order as nearly as one
/// walk can manage, or nothing when the program is well formed.
///
/// `source` must be the buffer the tree was parsed from, not a copy of it;
/// otherwise the offsets are meaningless and are reported as `nowhere`
/// instead of wrong. `strict_root` says the whole program is strict code
/// before any directive is read, as a module is (16.2.1). `eval` says the
/// source is an eval's, and where the eval was (PerformEval's early errors,
/// and its inherited strictness). `script_goal` says the source is an
/// ECMAScript Script rather than the embedder's program
/// (compiler::compile_script): a top-level `return` and `new.target` are then
/// the SyntaxErrors 16.1.1 makes them.
[[nodiscard]] std::optional<early_error> find_early_error(const ctjs::vp::ast & tree,
                                                          std::string_view source,
                                                          bool strict_root = false,
                                                          const eval_site * eval = nullptr,
                                                          bool script_goal = false);

/// Returns whether the token before `offset` is a `(`. The parser keeps no
/// parentheses, so `export default function () {}` and
/// `export default (function () {})` (a declaration and an expression) are
/// one tree, and the source has to be consulted. It checks by token and not
/// by byte, because a comment may stand between the two and may hold a `(`
/// or hide one: the text up to `offset`, which must be a token's start, is
/// lexed again. Callers can pass a source slice to limit the rescan.
[[nodiscard]] inline bool after_open_paren(std::string_view source, std::size_t offset) {
    if (offset > source.size()) { return false; }
    const std::vector<ctjs::vp::token> before = ctjs::vp::lex(source.substr(0, offset));
    const auto last = std::find_if(before.rbegin(), before.rend(),
                                   [](const auto & t) { return t.kind != ctjs::vp::tk::end; });
    return last != before.rend() && last->kind == ctjs::vp::tk::punct && last->s == "(";
}

} // namespace ctbrowser::script::detail
