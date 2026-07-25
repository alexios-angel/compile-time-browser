export module ctbrowser.script;

// ctjs v2: a register-based bytecode VM.
//
//   value      one 64-bit NaN-boxed word - doubles native, 4 singletons,
//              48-bit heap pointers with the kind in the object header
//   bytecode   a register instruction set; registers are frame slots
//   compile    ctjs's existing Pratt parser to bytecode
//   vm         the dispatch loop, mark-and-sweep GC over precise roots
//
// What v1 did and this does not: walk the AST on every execution, look every
// identifier up by string each time round a loop, refcount every value, and
// scan a vector of name/value pairs linearly for each property access.
//
// A context is ONE AGENT AND ONE THREAD, deliberately. Workers get their own
// context; what threads share is the DOM, which has its own concurrency
// control. That is also how the web platform defines agents.

export import :value;
export import :bytecode;
export import :compile;
export import :vm;
