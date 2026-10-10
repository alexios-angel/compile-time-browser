//===- types.hpp - Compiler bookkeeping types -------------------*- C++ -*-===//
//
// The plain data types compiler_impl keeps per function: locals, frames and
// resolved references.
//
//===----------------------------------------------------------------------===//
#pragma once

#include <ctbrowser/core/containers.hpp>
#include <ctbrowser/script/compile.hpp>

#include <boost/container/small_vector.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ctbrowser::script::detail::compiler_types {

/// One local binding of a frame: its name, register and binding rules.
struct local {
    std::string name;
    std::uint16_t reg = 0;
    bool boxed = false; // Lives in a heap cell; see compiler_impl::is_captured.
    /// Where a lexical binding is initialised: the source offset past its
    /// declarator (or class), 0 for a `var`, a parameter or a function. A
    /// read in the same frame textually before it is in the temporal dead
    /// zone whenever it runs (the scope is entered once per run, the
    /// declaration always after such a read), so compile_ident emits the
    /// throw statically. A read from a nested function is not decided here;
    /// forward-captured block lexicals use ready_flag below.
    std::uint32_t initialized_at = 0;
    /// The index of this local's entry in `function_proto::locals`, or
    /// no_slot when the debug tables are off. The compiler's `locals` is a
    /// stack that shrinks at every scope exit, so by the time a function is
    /// finished the only names left are the ones still in scope, which for a
    /// body full of blocks is almost none of them. The debug table is written
    /// as each local is declared and closed as its scope is popped, and this
    /// index links the two.
    static constexpr std::uint32_t no_slot = 0xFFFFFFFFu;
    std::uint32_t debug_slot = no_slot;
    /// Whether an assignment may write it; see immutability and
    /// compiler_impl::binding_immutability. The declaration's own write is
    /// not an assignment and is never refused (compiler_impl::declaring_).
    immutability readonly = immutability::none;
    /// True for a `let`, `const`, `using` or `class`: what a sloppy direct
    /// eval's `var` may not redeclare (see eval_binding). False for a
    /// parameter, a `var`, a function or a catch parameter (B.3.4 lets a var
    /// share that name).
    bool lexical = false;
    /// The hidden boxed boolean that says whether a forward-captured block
    /// lexical has been initialised, or empty.
    std::string ready_flag{};
};

/// A half-open range [lo, hi) of Euler-tour positions.
struct interval {
    std::int32_t lo = 0;
    std::int32_t hi = 0;
    [[nodiscard]] bool empty() const noexcept { return lo >= hi; }
};

/// The compiler's state for one function being compiled.
struct frame {
    std::uint32_t proto = 0;
    std::vector<local> locals;
    /// Name to the positions in `locals` that carry it, innermost last. It
    /// replaces a backward scan of `locals` per identifier mention, which is
    /// quadratic in the function's size (docs/performance.md).
    ///
    /// A vector per name rather than one index because names shadow: two
    /// `let x` in nested scopes are two entries, and popping the inner one
    /// has to uncover the outer rather than erase the name. Entries are
    /// pushed in increasing order, so `pop_scope` unwinding from the top
    /// pops each name's stack from the top too.
    string_flat_map<boost::container::small_vector<std::uint32_t, 2>> local_index;
    /// The same index for the upvalue list. That list only grows within a
    /// frame, so it is a plain name to position map.
    string_flat_map<std::uint32_t> upvalue_index;
    std::vector<std::string> declared; // Pre-scanned; see collect_declared_names.
    /// Where this function sits in the Euler tour, which is how
    /// is_captured() is answered. Empty (lo >= hi) means nothing is
    /// captured, which is what a field initialiser's frame gets.
    interval captures;
    std::vector<std::string> upvalue_names; // Parallel to proto().upvalues.
    std::vector<std::string> predeclared;   // Hoisted at body entry; see predeclare_locals.
    /// The block-level function declarations that B.3.3 also gave a var
    /// binding of this function (or, in a script, a global). Stored as node
    /// indices because the same name may be declared in two blocks and only
    /// one of them apply: `{ function h() {} { function h() {} } }` gives the
    /// inner one nothing, and a list of names could not say so.
    /// See predeclare_locals and compile_function_decl.
    std::vector<std::int32_t> annex_b_decls;
    std::vector<std::size_t> scope_marks; // locals.size() at each scope entry.
    /// Register counters, deliberately wider than the 16-bit operand they
    /// feed, so the compiler can report how many registers were wanted
    /// instead of wrapping silently.
    std::uint32_t next_reg = 0;
    std::uint32_t high_water = 0;
    bool is_async = false;     // `return v` hands back a settled promise of v.
    bool is_generator = false; // `function*`: calling it does not run it.
    bool is_strict = false;    // See function_proto::is_strict.
    /// In the constructor of a derived class, the hidden boxed local that
    /// says whether `super()` has run: `this` before it, a second `super()`,
    /// and a return without it are the ReferenceErrors of 10.2.1.3 /
    /// 13.3.7.1 / 9.2.1.2. Empty for every other function. An arrow inside
    /// the constructor asks its nearest non-arrow frame (derived_flag()).
    std::string derived_flag;
    /// In a sloppy function whose own body calls `eval(`, the hidden boxed
    /// local holding the object its direct evals' `var`s are made on (see
    /// compiler_impl::with_scope::eval_vars). Empty for every other function.
    std::string eval_vars;
    /// True for a function with a [[HomeObject]] (a method, an accessor, a
    /// constructor or a class's field initialiser, see field_initializer),
    /// which is where a direct eval may use `super.x` (see
    /// compiler_impl::eval_site_here).
    bool has_home = false;
    bool field_initializer = false;
    /// The index each name or string was already given in the proto's
    /// tables. `function_proto::add_name` and `add_string` deduplicate by
    /// linear scan, quadratic in the distinct names a function mentions. The
    /// index lives here rather than on the proto because it is wanted only
    /// while compiling.
    flat_map<std::string, std::uint32_t> name_index;
    flat_map<std::string, std::uint32_t> string_index;
};

/// A resolved assignment or read target: where a name, member or index lives
/// and which instructions load and store it.
struct reference {
    enum class kind : std::uint8_t {
        local,
        boxed_local,
        upvalue,
        global,
        member,
        index,
        // `super.x` / `super[k]`: `reg` is the home object's prototype and
        // `key` the key; read and written with `this` as the receiver.
        super_property
    };
    kind what = kind::local;
    std::uint16_t reg = 0;  // local/boxed: its register. member/index: the object.
    std::uint16_t key = 0;  // index: the key register.
    std::uint16_t name = 0; // global/member: the name index.
    /// For a name inside a `with`: `with_reg` holds the object that bound it
    /// when the reference was prepared, or undefined, and the fields above
    /// are the fallback. See emit_with_object.
    bool with = false;
    std::uint16_t with_reg = 0;
    std::uint16_t with_name = 0; // The name, as a property-name operand.
    /// Set when the name resolved to an immutable binding: emit_store refuses
    /// the write (the `with` object, when one binds the name, still takes it).
    immutability readonly = immutability::none;
    /// The binding's TDZ flag (see local::ready_flag), or empty.
    std::string ready_flag;
};

} // namespace ctbrowser::script::detail::compiler_types
