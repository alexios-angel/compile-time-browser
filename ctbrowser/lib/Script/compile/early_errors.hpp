#pragma once
// THE EARLY ERRORS - the source text a conforming implementation must REFUSE
// before it evaluates a line of it.
//
// A JavaScript engine has two ways to say no. The parser says no to text that
// is not in the grammar (`var a = ;`), and the STATIC SEMANTICS say no to text
// that parses perfectly and is still not a program: `let x; let x;`,
// `1 = 2`, `break` with nothing to break out of. The specification calls the
// second kind Early Errors, and clause 17 is explicit that they are reported
// "prior to the first evaluation of the source text" - so an engine that runs
// `let x; let x;` is not being lenient, it is answering a different language.
//
// This is that pass, and it lives beside the compiler rather than inside it for
// two reasons. It walks the tree ONCE, before any code is emitted, which is
// what "before the first evaluation" means and what the compiler - which emits
// as it walks - cannot promise. And it needs nothing from the compiler but the
// AST, so it can be exercised on its own.
//
// PRIVATE to lib/Script/compile/, like compiler_impl.hpp beside it: it includes
// <ctjs/vparse.hpp>, and test/lint/api_surface polices third-party headers in
// include/.
//
// WHAT IS NOT HERE, and why: every rule that only bites in STRICT MODE. This
// engine has no strict mode (docs/test262.md says so, at length), so
// `delete x`, assignment to `eval`, an octal literal and a duplicate simple
// parameter list are all legal sloppy JavaScript here and refusing them would
// be refusing valid source. Two rules are relaxed for the same reason and are
// marked where they are checked.

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

#include <ctjs/vparse.hpp>

namespace ctbrowser::script::detail {

struct early_error {
    // WHAT IS WRONG, in the voice a person reads: the construct and the name it
    // is wrong about. Never a clause number alone.
    std::string message;
    // WHERE, as a byte offset into the source the AST was parsed from, or
    // `nowhere` when the offending node carries no lexeme to subtract against.
    // The same trick compiler_impl::offset_of uses: `node::text` is a view INTO
    // the source, so its address is its position.
    static constexpr std::size_t nowhere = static_cast<std::size_t>(-1);
    std::size_t offset = nowhere;
};

// The FIRST early error in `tree`, in source order as nearly as one walk can
// manage, or nothing when the program is well formed.
//
// `source` must be the buffer the tree was parsed from - not a copy of it - or
// the offsets are meaningless and are reported as `nowhere` instead of wrong.
[[nodiscard]] std::optional<early_error> find_early_error(const ctjs::vp::ast & tree,
                                                          std::string_view source);

} // namespace ctbrowser::script::detail
