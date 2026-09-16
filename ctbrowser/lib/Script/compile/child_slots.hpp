#pragma once
// WHICH OF A NODE'S FOUR FIXED SLOTS ARE ACTUALLY CHILDREN.
//
// The parser reuses `c` and `d` as BITFIELDS on the kinds that need flags:
// a rest parameter is `d == 1`, an async function is `c & 1`, a static class
// member is `d & 1`, an object-literal accessor is `c == 3`. Nothing on a
// node says which reading applies, so a generic walk over {a, b, c, d}
// treats those flags as node indices - and index 1 is a real node, so the
// walk goes back round the tree and never terminates.
//
// ONE TABLE, shared by the compiler's walks (capture.cpp) and the early-error
// checker (early_errors/expressions.cpp): it was two copies until 2026-09-12,
// and every fix below had to be made twice. Inline because both walks are on
// the compile profile (docs/performance.md lists child_slots at 2.4%).
//
// PRIVATE to lib/Script/compile/: it includes <ctjs/vparse.hpp>, and "no
// third-party header in a public header" (CLAUDE.md) keeps it out of include/.

#include <array>
#include <cstdint>

#include <ctjs/vparse.hpp>

namespace ctbrowser::script::detail {

[[nodiscard]] inline std::array<std::int32_t, 4> child_slots(const ctjs::vp::node & n) {
    namespace vp = ctjs::vp;
    switch (n.kind) {
    // a = the default expression, b = a destructuring pattern; d = the rest
    // flag
    case vp::nk::param: return {n.a, n.b, -1, -1};
    // a = the body; c = async/generator bits
    case vp::nk::func_decl:
    case vp::nk::func_expr:
    case vp::nk::arrow: return {n.a, -1, -1, -1};
    // a = a computed key, b = the value or method; c and d are both flags
    case vp::nk::class_member:
    case vp::nk::prop:
    case vp::nk::pattern_prop: return {n.a, n.b, -1, -1};
    // a = the callee, the arguments are the list; d says whether there were
    // parentheses at all
    case vp::nk::new_expr: return {n.a, -1, -1, -1};
    // a = the target, b = the iterable, c = the body; d carries `const` and
    // whether there is anything to declare
    case vp::nk::forof_stmt: return {n.a, n.b, n.c, -1};
    // a = the operand; d = 1 says `yield*`
    case vp::nk::yield_expr: return {n.a, -1, -1, -1};
    // a = the test, the statements are the list; d marks `default:`
    case vp::nk::case_clause: return {n.a, -1, -1, -1};
    // ES MODULES. `c` IS A FLAG ON ALL OF THESE - which binding form an
    // import_spec is, whether an export is `default` or `*` - and the
    // default branch below would follow it as a NODE INDEX. It did: walking
    // `export default 42` segfaulted, because `c = 1` sent the tour into
    // node 1 and off from there.
    //
    // The bindings live in `list`, which every walk handles separately, and
    // `a` is the only real child: the declaration an export wraps, the
    // specifier a dynamic import takes, or the str node holding the
    // original name of a renamed binding.
    case vp::nk::import_decl:
    case vp::nk::import_meta: return {-1, -1, -1, -1};
    case vp::nk::import_spec:
    case vp::nk::export_decl:
    case vp::nk::export_spec: return {n.a, -1, -1, -1};
    case vp::nk::dynamic_import:
        return {n.a, n.b, -1, -1}; // b: the options argument
    // `++x` / `x++`: a is the operand and b IS THE PREFIX FLAG (1 or 0). The
    // default arm followed b as a node index; a program whose update node
    // is node 1 - `++x;` as the first statement, which is what
    // `eval("++x")` compiles - toured itself forever and overflowed the
    // stack. test262's eval-code/direct/cptn-* found it.
    case vp::nk::update: return {n.a, -1, -1, -1};
    // `-a` / `a ?? b`: d = 1 says the node was PARENTHESISED (the parser's
    // one trace of parentheses, for the `**` and `??` grammar), never a child.
    case vp::nk::unary: return {n.a, -1, -1, -1};
    case vp::nk::logical:
    case vp::nk::binary: return {n.a, n.b, -1, -1};
    // a = the tag, b = the template
    case vp::nk::tagged: return {n.a, n.b, -1, -1};
    default: return {n.a, n.b, n.c, n.d};
    }
}

} // namespace ctbrowser::script::detail
