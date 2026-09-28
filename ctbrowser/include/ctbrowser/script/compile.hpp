#pragma once
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <ctbrowser/script/bytecode.hpp>

// AST to bytecode.
//
// The front end is ctjs's existing Pratt parser, reused rather than rewritten
// for the same reason cthtml's parser was initially reused in the DOM: it works,
// and blocking a VM on a fresh JavaScript parser would be the wrong order to
// build in. What is replaced is everything after it - the previous engine walked
// this tree on every execution, re-deciding what each node meant each time round
// a loop.
// Compiling once and dispatching on 4-byte instructions is the entire point.
//
// Register allocation is a high-water mark. Locals take the low registers of
// a frame and keep them for their scope; expression temporaries are allocated
// above the locals and released as each statement finishes. No liveness
// analysis, no spilling - the frame is simply as large as the deepest
// expression needed.
//
// CLOSURES use boxed cells. A local that any nested function mentions is
// allocated as a heap cell instead of living directly in its register, and
// every read and write goes through the cell. Nested functions capture the
// CELL, not the value, which is what makes mutation through a closure visible
// to the enclosing scope - the whole point, and what capture-by-value gets
// silently wrong.
//
// Which locals need boxing is decided by a pre-pass (mark_captured). It
// deliberately OVER-APPROXIMATES: if a nested function mentions the name at
// all, the outer local is boxed, without checking whether the nested function
// shadows it with its own binding. The cost of being wrong that way is one
// heap cell for a variable that did not need one; the cost of the precise
// analysis is a scope-resolution pass that has to be right. Worth revisiting
// when there is a benchmark that cares.

//
// The compiler itself is an implementation detail of compile.cpp: nothing
// outside calls anything but compile(), so nothing outside needs to see the
// register allocator, the scope stack or the 60-odd compile_* methods.

namespace ctbrowser::script {

// A BINDING THAT CANNOT BE ASSIGNED, and what an assignment to one does
// (9.1.1.1.5 SetMutableBinding step 5). Decided where the write is compiled:
// the compiler knows which binding a name resolves to, so the refusal costs
// nothing on any other write. A direct eval is handed it for each of its
// caller's bindings, so `eval("x = 1")` refuses a `const x` as the caller would.
enum class immutability : std::uint8_t {
    none,
    // CreateImmutableBinding(N, true): `const`, `using`, an import, a class's
    // own name inside its body - every assignment is the TypeError.
    strict,
    // CreateImmutableBinding(N, false): a named function expression's own
    // name (15.2.5) - the TypeError in strict code, and in sloppy code the
    // assignment does nothing at all.
    sloppy,
};

// WHERE AN EVAL'S SOURCE RUNS (19.2.1.1 PerformEval steps 4-7 and 12):
// what it may say that a program may not, and whether it is strict before
// any directive of its own. An indirect eval is all false. A direct eval
// answers for the function whose `this` it shares - the nearest one that is
// not an arrow - so `new.target` is refused outside a function, `super.x`
// outside a method, `super()` outside a derived constructor and `arguments`
// in a class field initialiser.
struct eval_site {
    bool strict = false;
    bool in_function = false;
    bool in_method = false;
    bool in_derived_constructor = false;
    bool in_class_field_initializer = false;
    // The private names of the classes around the eval (step 7.c's
    // PrivateEnvironment): each spelling, `#x`, and the key the caller's
    // compiler gave it - so `eval('this.#x')` reads the caller's field.
    std::vector<std::pair<std::string, std::string>> private_names;
};

// ONE BINDING A DIRECT EVAL'S CALLER HANDS IT: the name, whether it can be
// assigned, and whether it is a `let`, `const` or `class` of the caller's own
// function - which a sloppy eval's `var` may not share a name with (19.2.1.3
// step 3: the var would hoist through it).
struct eval_binding {
    std::string name;
    immutability readonly = immutability::none;
    bool lexical = false;
};

// WHICH KIND OF SCRIPT THIS IS, which decides where its top-level declarations
// go.
//
// A CLASSIC script's `var x = 1` at top level becomes a GLOBAL - that is the
// whole reason two <script> tags can see each other's variables, and why this
// engine could concatenate them all into one program and be right.
//
// A MODULE's does not. Its top level is a scope of its own; `globalThis.x = 1`
// still reaches the global object because that is an explicit write, but a bare
// declaration is the module's own. Getting this wrong is how "modules work"
// while every module on a page silently shares state with every other.
//
// `script_kind` itself is declared in bytecode.hpp, beside `program`, because a
// compiled program carries which one it is.
class compiler {
public:
    // Compile `source` to a program. On a parse error the program comes back
    // with ok == false and error set, rather than throwing.
    [[nodiscard]] static program compile(std::string_view source,
                                         script_kind kind = script_kind::classic);
    // An ECMAScript SCRIPT (16.1), parsed with the goal symbol the
    // specification gives a classic script: a top-level `return` or
    // `new.target` is a SyntaxError. compile() admits both - a program's
    // top-level `return` is how it hands an embedder a value
    // (run_result::returned) - so a caller whose source is a real Script, a
    // test262 test, says so here. Script also parses `await` as an identifier;
    // compile() admits top-level await for embedding async bodies and modules.
    [[nodiscard]] static program compile_script(std::string_view source);
    // `eval`'s shape: the program RETURNS the value of a trailing expression
    // statement (its completion value, 16.1.6 step 12 for the common case),
    // so `eval("1 + 1")` is 2. Otherwise the same classic script.
    // `site` is an indirect eval's - all false - unless the caller says.
    [[nodiscard]] static program compile_for_eval(std::string_view source,
                                                  const eval_site & site = {});
    // A DIRECT eval's source (19.2.1.1 PerformEval with direct = true): as
    // compile_for_eval, with the caller's bindings `outer` visible as the
    // entry function's upvalues, in that order, and where it was written.
    [[nodiscard]] static program compile_for_direct_eval(std::string_view source,
                                                         std::span<const eval_binding> outer,
                                                         const eval_site & site);
};

// WHETHER THIS BUILD FILLS `function_proto::locals` AND `code_offsets`.
//
// A build knob - CTBROWSER_SCRIPT_DEBUG_NAMES, ON by default - so it has to be
// ASKABLE at run time rather than tested with a preprocessor conditional in
// every reader. The same reason `allocator_name()` exists: a test that guesses
// which build it is in is a test that passes for the wrong reason.
[[nodiscard]] bool debug_names_enabled() noexcept;

} // namespace ctbrowser::script
