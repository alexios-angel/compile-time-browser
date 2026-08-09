#pragma once
// The script engine: a register-based bytecode VM.
//
//   value      one 64-bit NaN-boxed word - doubles native, 4 singletons,
//              48-bit heap pointers with the kind in the object header
//   bytecode   a register instruction set; registers are frame slots
//   compile    ctjs's existing Pratt parser to bytecode
//   vm         the dispatch loop, mark-and-sweep GC over precise roots
//   builtins   Math, Array, String, Number, Object, JSON - the standard
//              library, without which a complete VM is still useless
//
// What the previous engine did and this does not: walk the AST on every execution, look every
// identifier up by string each time round a loop, refcount every value, and
// scan a vector of name/value pairs linearly for each property access.
//
// A context is ONE AGENT AND ONE THREAD, deliberately. Workers get their own
// context; what threads share is the DOM, which has its own concurrency
// control. That is also how the web platform defines agents.

#include <ctbrowser/script/builtins.hpp>
#include <ctbrowser/script/bytecode.hpp>
#include <ctbrowser/script/compile.hpp>
#include <ctbrowser/script/number_format.hpp>
#include <ctbrowser/script/value.hpp>
#include <ctbrowser/script/vm.hpp>
