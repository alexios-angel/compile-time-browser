//===- script.hpp - Script engine umbrella header ---------------*- C++ -*-===//
//
// The script engine: a register-based bytecode VM. Including this header pulls
// in every part of it:
//
//   value      one 64-bit NaN-boxed word: doubles stored directly, four
//              singletons, 48-bit heap pointers with the kind in the object
//              header.
//   bytecode   a register instruction set; registers are frame slots.
//   compile    ctjs's parser (vparse) output compiled to bytecode.
//   vm         the dispatch loop and a mark-and-sweep GC over precise roots.
//   builtins   Math, Array, String, Number, Object, JSON and the rest of the
//              standard library.
//
// Unlike a tree-walking interpreter it does not re-walk the AST per execution,
// look identifiers up by string, refcount values, or scan name/value vectors
// for property access.
//
// A context is one agent and one thread, deliberately. Workers get their own
// context; what threads share is the DOM, which has its own concurrency
// control. That is also how the web platform defines agents.
//
//===----------------------------------------------------------------------===//

#pragma once

#include <ctbrowser/script/builtins.hpp>
#include <ctbrowser/script/bytecode.hpp>
#include <ctbrowser/script/compile.hpp>
#include <ctbrowser/script/program_image.hpp>
#include <ctbrowser/script/value.hpp>
#include <ctbrowser/script/vm.hpp>
