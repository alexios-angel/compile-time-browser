//===- compile.hpp - JavaScript to bytecode ---------------------*- C++ -*-===//
//
// The script compiler's public interface: AST to bytecode.
//
// The front end is ctjs's parser (vparse), reused rather than rewritten. The
// compiler turns its tree into a bytecode `program` once, so execution
// dispatches on 8-byte instructions instead of re-walking the tree each time
// round a loop.
//
// Register allocation is a high-water mark. Locals take the low registers of
// a frame and keep them for their scope; expression temporaries are allocated
// above the locals and released as each statement finishes. No liveness
// analysis, no spilling: the frame is simply as large as the deepest
// expression needed.
//
// Closures use boxed cells. A local that any nested function mentions is
// allocated as a heap cell instead of living directly in its register, and
// every read and write goes through the cell. Nested functions capture the
// cell, not the value, which is what makes mutation through a closure visible
// to the enclosing scope (capture by value gets this silently wrong).
//
// Which locals need boxing is decided by the capture analysis in
// compile/capture.cpp (build_capture_index, then is_captured). It deliberately
// over-approximates: if a nested function mentions the name at all, the outer
// local is boxed, even when the nested function shadows it with its own
// binding. Being wrong that way costs one heap cell; the precise analysis
// would need full scope resolution.
//
// The compiler itself is an implementation detail of lib/Script/compile/:
// nothing outside calls anything but the `compiler` entry points, so nothing
// outside needs to see the register allocator, the scope stack or the
// compile_* methods.
//
//===----------------------------------------------------------------------===//

#pragma once
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <ctbrowser/script/bytecode.hpp>

namespace ctbrowser::script {

/// Whether a binding can be assigned, and what an assignment to one does
/// (ECMA-262 9.1.1.1.5 SetMutableBinding step 5). Decided where the write is
/// compiled: the compiler knows which binding a name resolves to, so the
/// refusal costs nothing on any other write. A direct eval is handed it for
/// each of its caller's bindings, so `eval("x = 1")` refuses a `const x` as
/// the caller would.
enum class immutability : std::uint8_t {
    none,
    /// CreateImmutableBinding(N, true): `const`, `using`, an import, a class's
    /// own name inside its body. Every assignment is a TypeError.
    strict,
    /// CreateImmutableBinding(N, false): a named function expression's own
    /// name (15.2.5). A TypeError in strict code; in sloppy code the
    /// assignment does nothing at all.
    sloppy,
};

/// Where an eval's source runs (ECMA-262 19.2.1.1 PerformEval steps 4-7 and
/// 12): what it may say that a program may not, and whether it is strict
/// before any directive of its own. An indirect eval is all false. A direct
/// eval describes the function whose `this` it shares (the nearest one that is
/// not an arrow), so `new.target` is refused outside a function, `super.x`
/// outside a method, `super()` outside a derived constructor and `arguments`
/// in a class field initialiser.
struct eval_site {
    bool strict = false;
    bool in_function = false;
    bool in_method = false;
    bool in_derived_constructor = false;
    bool in_class_field_initializer = false;
    /// The private names of the classes around the eval (step 7.c's
    /// PrivateEnvironment): each spelling, `#x`, and the key the caller's
    /// compiler gave it, so `eval('this.#x')` reads the caller's field.
    std::vector<std::pair<std::string, std::string>> private_names;
};

/// One binding a direct eval's caller hands it: the name, whether it can be
/// assigned, and whether it is a `let`, `const` or `class` of the caller's own
/// function, which a sloppy eval's `var` may not share a name with (19.2.1.3
/// step 3: the var would hoist through it).
struct eval_binding {
    std::string name;
    immutability readonly = immutability::none;
    bool lexical = false;
};

/// Compiles JavaScript source to a bytecode `program`. The entry points differ
/// in the goal symbol and in what eval-specific context they accept. A
/// classic script's top-level declarations become globals, which is why two
/// <script> tags see each other's variables; a module's stay its own (see
/// `script_kind` in bytecode.hpp).
class compiler {
public:
    /// Compiles `source` to a program. On a parse error the program comes back
    /// with ok == false and error set, rather than throwing.
    [[nodiscard]] static program compile(std::string_view source,
                                         script_kind kind = script_kind::classic);
    /// Compiles an ECMAScript Script (16.1), parsed with the goal symbol the
    /// specification gives a classic script: a top-level `return` or
    /// `new.target` is a SyntaxError. compile() admits both (a program's
    /// top-level `return` is how it hands an embedder a value,
    /// run_result::returned), so a caller whose source is a real Script, such
    /// as a test262 test, uses this instead. Script also parses `await` as an
    /// identifier; compile() admits top-level await for embedding async bodies
    /// and modules.
    [[nodiscard]] static program compile_script(std::string_view source);
    /// Compiles for `eval`: the program returns the value of a trailing
    /// expression statement (its completion value, 16.1.6 step 12 for the
    /// common case), so `eval("1 + 1")` is 2. Otherwise the same classic
    /// script. `site` defaults to an indirect eval's (all false).
    [[nodiscard]] static program compile_for_eval(std::string_view source,
                                                  const eval_site & site = {});
    /// Compiles a direct eval's source (19.2.1.1 PerformEval with direct =
    /// true): as compile_for_eval, with the caller's bindings `outer` visible
    /// as the entry function's upvalues, in that order, and `site` describing
    /// where it was written.
    [[nodiscard]] static program compile_for_direct_eval(std::string_view source,
                                                         std::span<const eval_binding> outer,
                                                         const eval_site & site);
};

/// Whether this build fills `function_proto::locals` and `code_offsets`.
///
/// It is a build option (CTBROWSER_SCRIPT_DEBUG_NAMES, ON by default), so it
/// has to be queryable at run time rather than tested with a preprocessor
/// conditional in every reader. The same reason `allocator_name()` exists: a
/// test that guesses which build it is in can pass for the wrong reason.
[[nodiscard]] bool debug_names_enabled() noexcept;

} // namespace ctbrowser::script
