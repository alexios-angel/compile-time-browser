// The body the GC-roots test compiles. It is a separate file because the build
// runs the real pipeline over it - ctjs-translate, ctjs-opt, mlir-translate -
// and a fixture embedded in a C++ string could not be fed to those tools. It
// must stay textually identical to the copy in GCRoots.cpp, which is what
// gives the interpreter the same function_proto to compare against.
//
// TWO SAFEPOINTS WITH A LIVE VALUE BETWEEN THEM, which is the shape that
// matters. `a + b` allocates a string; `k(...)` is a call that runs user
// JavaScript and can collect. The intermediate is live across the call, and it
// also has to be in the call's CONTIGUOUS argument window - so this exercises
// both kinds of rooting the backend does: one slot per produced value, and a
// reserved run per call site.
function f(a, b, c, k) { return k(a + b, c); }
