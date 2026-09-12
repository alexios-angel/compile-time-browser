#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <ctbrowser/core/core.hpp>

#include <ctbrowser/script/value.hpp>
#include <ctbrowser/script/vm.hpp>

// The JavaScript standard library.
//
// The VM started with NONE of this. The whole property surface was `.length`, numeric
// indexing and named lookup on plain objects - no `arr.push`, no `str.split`,
// no `Math.floor`, no `JSON.parse`. A VM can be complete and still useless if
// nothing can be done with a value once you have one, and that is what this
// closes.
//
// It is a SUBSET, chosen by what pages actually call rather than by what the
// spec lists. The omissions that matter are named at the bottom of this file
// rather than left to be discovered.

namespace ctbrowser::script {

// What a regex LITERAL compiles to. A reserved name rather than `RegExp` so a
// page that shadows the constructor cannot change what its own literals mean.
inline constexpr std::string_view regexp_factory_name = "__ctbrowser_regexp";
// `Promise.reject`, under a name a page cannot shadow: the compiler's async
// fence returns `__ctbrowser_reject(e)` for a throw an async body did not
// catch. See compile_function_body.
inline constexpr std::string_view promise_reject_name = "__ctbrowser_reject";
// Called once per class, after its methods and accessors are installed and
// before its static fields are: makes every own property of the constructor
// and of its prototype NON-ENUMERABLE, which is what ClassDefinitionEvaluation
// (15.7.14) gives a method, an accessor, `prototype` and `constructor`.
// op::set_prop has no attribute operand and a new opcode is an ABI change, so
// this is one native call rather than one per method.
inline constexpr std::string_view class_defined_name = "__ctbrowser_class_defined";
// GetIterator(obj, async) for `for await`: the object's @@asyncIterator, or
// its @@iterator wrapped so that every `next()` answers a promise of the
// record (CreateAsyncFromSyncIterator, 27.1.6.1). The loop itself is bytecode:
// `next()` through call_receiver, await_value, get_prop done/value.
inline constexpr std::string_view async_iterator_name = "__ctbrowser_async_iterator";
// THE ITERATOR RECORD of an array destructuring (8.6.2 IteratorBindingInitialization,
// 13.15.5.5 IteratorDestructuringAssignmentEvaluation), as three natives over
// one plain object {iterator, next, done}: `open(v)` is GetIterator(v) into a
// record, `next(rec)` is IteratorStep + IteratorValue writing rec.done, and
// `close(rec, suppress)` is IteratorClose - `return()` when the record is not
// done, its own throw swallowed when `suppress` is true because a throw is
// already in flight. The pattern itself is bytecode around these calls.
inline constexpr std::string_view iterator_open_name = "__ctbrowser_iter_open";
inline constexpr std::string_view iterator_next_name = "__ctbrowser_iter_next";
inline constexpr std::string_view iterator_close_name = "__ctbrowser_iter_close";
// RequireObjectCoercible (7.2.1) for an object pattern that reads nothing
// (`{} = null`, `{...r} = undefined`): TypeError on null or undefined.
inline constexpr std::string_view require_object_name = "__ctbrowser_require_object";
// An accessor under a COMPUTED key - `get [k]() {}` in a class or a literal:
// (target, key, getter, setter), the halves undefined when absent.
// define_getter/define_setter take a name index, so a key that is only known
// at run time goes through this one native rather than a new opcode.
inline constexpr std::string_view define_accessor_name = "__ctbrowser_define_accessor";

// Install the standard library into a context.
//
// DECLARED here and DEFINED in builtins.cpp, which is the whole point: the
// definition is ~1000 lines of lambdas, and while it lived in this interface
// every translation unit that imported the module re-instantiated and
// re-optimised all of it. It was the single largest symbol in a test object
// file - 21 KB of the 1.6 MB - and there are twenty-six of those.
//
// `seed` makes Math.random deterministic by default: the test story is
// byte-comparable goldens, and a page drawing with random cannot have one
// otherwise.
void install_builtins(context & cx, std::uint64_t seed = 0x2545F4914F6CDD1DULL);

} // namespace ctbrowser::script
