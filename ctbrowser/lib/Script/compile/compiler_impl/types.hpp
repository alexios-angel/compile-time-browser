#pragma once

#include <ctbrowser/core/containers.hpp>

#include <boost/container/small_vector.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ctbrowser::script::detail::compiler_types {

struct local {
    std::string name;
    std::uint16_t reg = 0;
    bool boxed = false; // lives in a heap cell; see mark_captured
    // WHERE A LEXICAL BINDING IS INITIALISED: the source offset past its
    // declarator (or class), 0 for a `var`, a parameter, a function. A
    // read of the same frame textually before it is in the temporal
    // dead zone whenever it runs (the scope is entered once per run, the
    // declaration always after such a read), so compile_ident throws
    // there statically. A read from a nested function is not decided
    // here - that needs a runtime check this engine does not make.
    std::uint32_t initialized_at = 0;
    // WHERE THIS LOCAL'S ENTRY IN `function_proto::locals` IS, or none when
    // the debug tables are off. The compiler's `locals` is a STACK that
    // shrinks at every scope exit, so by the time a function is finished
    // the only names left are the ones still in scope - which for a body
    // full of blocks is almost none of them. The debug table is written as
    // each local is DECLARED and closed as its scope is popped, and this is
    // the link between the two.
    static constexpr std::uint32_t no_slot = 0xFFFFFFFFu;
    std::uint32_t debug_slot = no_slot;
};

struct interval {
    std::int32_t lo = 0;
    std::int32_t hi = 0;
    [[nodiscard]] bool empty() const noexcept { return lo >= hi; }
};

struct frame {
    std::uint32_t proto = 0;
    std::vector<local> locals;
    // NAME -> THE POSITIONS IN `locals` THAT CARRY IT, innermost last. A
    // backward scan of `locals` runs once per identifier MENTION, so a big
    // function was quadratic in its own size (docs/performance.md).
    //
    // A vector per name rather than one index because names SHADOW: two
    // `let x` in sibling scopes are two entries, and popping the inner one
    // has to uncover the outer rather than erase the name. Entries are
    // pushed in increasing order, so `pop_scope` unwinding from the top
    // pops each name's stack from the top too.
    string_flat_map<boost::container::small_vector<std::uint32_t, 2>> local_index;
    // The same scan, on the upvalue list. This one only ever grows within a
    // frame, so it is a plain name -> position.
    string_flat_map<std::uint32_t> upvalue_index;
    std::vector<std::string> declared; // pre-scanned; see collect_declared_names
    // WHERE THIS FUNCTION SITS IN THE EULER TOUR, which is how
    // is_captured() is answered. Empty (lo >= hi) means nothing is
    // captured, which is what a field initialiser's frame gets - it never
    // had a captured set either.
    interval captures;
    std::vector<std::string> upvalue_names; // parallel to proto().upvalues
    std::vector<std::string> predeclared;   // hoisted at body entry; see predeclare_locals
    // The block-level function DECLARATIONS that B.3.3 gave a var binding
    // of this function (or, in a script, a global) too - as node indices,
    // because the same name may be declared in two blocks and only one of
    // them applicable: `{ function h() {} { function h() {} } }` gives the
    // inner one nothing, and a list of names could not say so.
    // See predeclare_locals and compile_function_decl.
    std::vector<std::int32_t> annex_b_decls;
    std::vector<std::size_t> scope_marks; // locals.size() at each scope entry
    // WIDER THAN THE OPERAND THEY FEED, on purpose: counting in a wider
    // type lets the compiler SAY how many registers were wanted instead
    // of wrapping in silence.
    std::uint32_t next_reg = 0;
    std::uint32_t high_water = 0;
    bool is_async = false;     // `return v` hands back a settled promise of v
    bool is_generator = false; // `function*` - calling it does not run it
    bool is_strict = false;    // see function_proto::is_strict
    // THE CONSTRUCTOR OF A DERIVED CLASS: the hidden boxed local that says
    // whether `super()` has run - `this` before it, a second `super()`,
    // and a return without it are the ReferenceErrors of 10.2.1.3 /
    // 13.3.7.1 / 9.2.1.2. Empty for every other function. An arrow inside
    // the constructor asks its nearest non-arrow frame (derived_flag()).
    std::string derived_flag;
    // WHERE A NAME OR STRING ALREADY WENT. `function_proto::add_name` and
    // `add_string` deduplicate by LINEAR SCAN, quadratic in the distinct
    // names a function mentions. The index lives HERE rather than on the
    // proto because it is wanted only while compiling.
    flat_map<std::string, std::uint32_t> name_index;
    flat_map<std::string, std::uint32_t> string_index;
};

struct reference {
    enum class kind : std::uint8_t {
        local,
        boxed_local,
        upvalue,
        global,
        member,
        index
    };
    kind what = kind::local;
    std::uint16_t reg = 0;  // local/boxed: its register. member/index: the object.
    std::uint16_t key = 0;  // index: the key register
    std::uint16_t name = 0; // global/member: the name index
    // A NAME INSIDE A `with`: `with_reg` holds the object that bound it
    // when the reference was prepared, or undefined, and the fields above
    // are the fallback. See emit_with_object.
    bool with = false;
    std::uint16_t with_reg = 0;
    std::uint16_t with_name = 0; // the name, as a property-name operand
};

} // namespace ctbrowser::script::detail::compiler_types
