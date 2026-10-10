//===- child_slots.hpp - Real children of an AST node -----------*- C++ -*-===//
//
// Which of a node's four fixed slots (a, b, c, d) are actually child node
// indices.
//
// The parser reuses `c` and `d` as bitfields on the kinds that need flags:
// a rest parameter is `d == 1`, an async function is `c & 1`, a static class
// member is `d & 1`, an object-literal accessor is `c == 3`. Nothing on a
// node says which reading applies, so a generic walk over {a, b, c, d}
// would treat those flags as node indices; index 1 is a real node, so the
// walk would go back round the tree and never terminate.
//
// Shared by the compiler's walks (capture.cpp) and the early-error checker
// (early_errors/expressions.cpp); keep it the single copy. Inline because it
// is hot at compile time (docs/performance.md lists child_slots at 2.4%).
//
// Private to lib/Script/compile/: it includes <ctjs/vparse.hpp>, and the rule
// "no third-party header in a public header" (CLAUDE.md) keeps it out of
// include/.
//
//===----------------------------------------------------------------------===//
#pragma once

#include <array>
#include <cstdint>

#include <ctjs/vparse.hpp>

namespace ctbrowser::script::detail {

/// Returns the node indices among `n`'s slots {a, b, c, d}, with -1 for every
/// slot that is empty or holds a flag rather than a child.
[[nodiscard]] inline std::array<std::int32_t, 4> child_slots(const ctjs::vp::node & n) {
    namespace vp = ctjs::vp;
    switch (n.kind) {
    // a = the default expression, b = a destructuring pattern; d = the rest
    // flag.
    case vp::nk::param: return {n.a, n.b, -1, -1};
    // a = the body; c = the async/generator bits.
    case vp::nk::func_decl:
    case vp::nk::func_expr:
    case vp::nk::arrow: return {n.a, -1, -1, -1};
    // a = a computed key, b = the value or method; c and d are both flags.
    case vp::nk::class_member:
    case vp::nk::prop:
    case vp::nk::pattern_prop: return {n.a, n.b, -1, -1};
    // a = the callee, the arguments are the list; d says whether there were
    // parentheses at all.
    case vp::nk::new_expr: return {n.a, -1, -1, -1};
    // a = the target, b = the iterable, c = the body; d carries `const` and
    // whether there is anything to declare.
    case vp::nk::forof_stmt: return {n.a, n.b, n.c, -1};
    // a = the operand; d = 1 says `yield*`.
    case vp::nk::yield_expr: return {n.a, -1, -1, -1};
    // a = the test, the statements are the list; d marks `default:`.
    case vp::nk::case_clause: return {n.a, -1, -1, -1};
    // Modules: `c` is a flag on all of these (the import binding form, or
    // whether an export is `default` or `*`), so it must never be followed as a
    // node index; `export default 42` would walk into node 1. Bindings live in
    // `list`, which callers walk separately; `a` is the only child (the
    // exported declaration, a dynamic import's specifier, or the str node
    // holding the original name of a renamed binding).
    case vp::nk::import_decl:
    case vp::nk::import_meta: return {-1, -1, -1, -1};
    case vp::nk::import_spec:
    case vp::nk::export_decl:
    case vp::nk::export_spec: return {n.a, -1, -1, -1};
    case vp::nk::dynamic_import:
        return {n.a, n.b, -1, -1}; // b is the options argument.
    // `++x` / `x++`: a is the operand and b is the prefix flag (1 or 0), not a
    // node. Following b would loop forever when the update node is node 1, as
    // in `eval("++x")` (test262 eval-code/direct/cptn-*).
    case vp::nk::update: return {n.a, -1, -1, -1};
    // `-a` / `a ?? b`: d = 1 says the node was parenthesised (the parser's
    // one trace of parentheses, for the `**` and `??` grammar), never a child.
    case vp::nk::unary: return {n.a, -1, -1, -1};
    case vp::nk::logical:
    case vp::nk::binary: return {n.a, n.b, -1, -1};
    // a = the tag, b = the template.
    case vp::nk::tagged: return {n.a, n.b, -1, -1};
    default: return {n.a, n.b, n.c, n.d};
    }
}

} // namespace ctbrowser::script::detail
