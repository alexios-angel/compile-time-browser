// The body the GC-roots test compiles. It is a separate file because the build
// runs the real pipeline over it - ctjs-translate, ctjs-opt, mlir-translate -
// and a fixture embedded in a C++ string could not be fed to those tools.
//
// `a + b + c` is the smallest program with the shape that matters: TWO
// safepoint calls, with the first one's result live across the second.
function f(a, b, c) { return a + b + c; }
