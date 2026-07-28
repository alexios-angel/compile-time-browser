#pragma once
#include <string_view>

#include <ctbrowser/script/bytecode.hpp>

// AST to bytecode.
//
// The front end is ctjs's existing Pratt parser, reused rather than rewritten
// for the same reason cthtml's parser is reused in the DOM: it works, and
// blocking a VM on a fresh JavaScript parser would be the wrong order to
// build in. What is replaced is everything after it - the previous engine walked this tree on
// every execution, re-deciding what each node meant each time round a loop.
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

class compiler {
public:
    // Compile `source` to a program. On a parse error the program comes back
    // with ok == false and error set, rather than throwing.
    [[nodiscard]] static program compile(std::string_view source);
};

} // namespace ctbrowser::script
