//===- builtins.hpp - JavaScript standard library ---------------*- C++ -*-===//
//
// The JavaScript standard library: install_builtins and create_intrinsic_realm,
// plus the reserved names of the internal natives the compiler emits calls to.
// The library is a subset chosen by what pages actually call rather than by
// what the spec lists. Bare section numbers in this file are ECMA-262's.
//
//===----------------------------------------------------------------------===//

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

namespace ctbrowser::script {

/// What a regex literal compiles to a call of. A reserved name rather than
/// `RegExp` so a page that shadows the constructor cannot change what its own
/// literals mean.
inline constexpr std::string_view regexp_factory_name = "__ctbrowser_regexp";
/// `Promise.reject`, under a name a page cannot shadow: the compiler's async
/// fence returns `__ctbrowser_reject(e)` for a throw an async body did not
/// catch. See compile_function_body.
inline constexpr std::string_view promise_reject_name = "__ctbrowser_reject";
/// Called once per class, after its methods and accessors are installed and
/// before its static fields are: makes every own property of the constructor
/// and of its prototype non-enumerable, which is what ClassDefinitionEvaluation
/// (15.7.14) gives a method, an accessor, `prototype` and `constructor`.
/// op::set_prop has no attribute operand and a new opcode is an ABI change, so
/// this is one native call rather than one per method.
inline constexpr std::string_view class_defined_name = "__ctbrowser_class_defined";
/// GetIterator(obj, async) for `for await`: the object's @@asyncIterator, or
/// its @@iterator wrapped so that every `next()` returns a promise of the
/// record (CreateAsyncFromSyncIterator, 27.1.6.1). The loop itself is bytecode:
/// `next()` through call_receiver, await_value, get_prop done/value.
inline constexpr std::string_view async_iterator_name = "__ctbrowser_async_iterator";
/// The iterator record of an array destructuring (8.6.2
/// IteratorBindingInitialization, 13.15.5.5
/// IteratorDestructuringAssignmentEvaluation), as three natives over one plain
/// object {iterator, next, done}: `open(v)` is GetIterator(v) into a record,
/// `next(rec)` is IteratorStep + IteratorValue writing rec.done, and
/// `close(rec, suppress)` is IteratorClose: `return()` when the record is not
/// done, its own throw swallowed when `suppress` is true because a throw is
/// already in flight. The pattern itself is bytecode around these calls.
inline constexpr std::string_view iterator_open_name = "__ctbrowser_iter_open";
inline constexpr std::string_view iterator_next_name = "__ctbrowser_iter_next";
inline constexpr std::string_view iterator_close_name = "__ctbrowser_iter_close";
/// Opens a synchronous for-of (14.7.5.6). Returns `undefined` when the source
/// is one the index loop over op::iterable already handles exactly (an array,
/// a string, a proxy, a Map or Set, a library iterator, or an array-like with
/// no @@iterator), and otherwise GetIterator(v) into the same record as
/// iterator_open_name, so a generator or a page's own iterable is pulled one
/// value at a time (an infinite one runs until `break`) and closed by
/// iterator_close_name on `break`. The loop tests that register on each
/// iteration, so an array keeps the index loop.
inline constexpr std::string_view for_of_open_name = "__ctbrowser_for_of_open";
/// `yield*` (14.4.14), as three natives around the generator's own yield:
/// `open(v, async)` is GetIterator(v, kind) into the same record shape;
/// `call(rec, sent)` is Call(rec.next, rec.iterator, sent), returning the raw
/// result (a promise for an async iterator, which the body awaits); and
/// `settle(rec, result)` checks the result is an object, records `done` and
/// the final value, and marks the coroutine as delegating so a sync `.next()`
/// hands the result object out untouched and `.throw()`/`.return()` reach
/// the inner iterator (context::generator_resume). The loop is bytecode.
inline constexpr std::string_view yield_delegate_open_name = "__ctbrowser_delegate_open";
inline constexpr std::string_view yield_delegate_call_name = "__ctbrowser_delegate_call";
inline constexpr std::string_view yield_delegate_settle_name = "__ctbrowser_delegate_settle";
/// A generator's return completion, carried as a throw. `.return(v)` at a
/// yield resumes the frame with a marker object ({@#return: v}, a key no
/// source can spell) thrown at the yield, so every `finally` on the way out
/// runs as 27.5.3.4 says and nothing else can catch it: the compiler starts
/// every catch clause with catch_filter_name, which rethrows a marker and
/// returns for anything else, and generator_resume fences the frame and turns
/// the escaping marker back into {value: v, done: true}.
inline constexpr std::string_view return_marker_key = CTBROWSER_PRIVATE_KEY("return");
inline constexpr std::string_view catch_filter_name = "__ctbrowser_catch_filter";
/// `import defer * as ns from "x"` (16.2.2): called with the specifier, it
/// returns the module's deferred namespace object (see
/// context::deferred_module_namespace), as one ordinary native call.
inline constexpr std::string_view import_defer_name = "__ctbrowser_import_defer";
/// An async generator's `.throw(e)` / `.return(v)` at a `yield*`: the frame
/// resumes with {@#resume: "throw"|"return", @#resume_value: v} in the
/// yield's register, and the delegate loop's call native forwards it to the
/// inner iterator's method instead of `next` (14.4.14 steps 7.b and 7.c).
inline constexpr std::string_view resume_record_key = CTBROWSER_PRIVATE_KEY("resume");
inline constexpr std::string_view resume_record_value_key = CTBROWSER_PRIVATE_KEY("resume_value");
/// An array literal's elisions (13.2.4.1): (array, index...) marks each index
/// a hole. The literal appended undefined there because `append` is the only
/// opcode for adding an element, and a hole is an attribute on the slot.
inline constexpr std::string_view array_holes_name = "__ctbrowser_array_holes";
/// RequireObjectCoercible (7.2.1) for an object pattern that reads nothing
/// (`{} = null`, `{...r} = undefined`): TypeError on null or undefined.
inline constexpr std::string_view require_object_name = "__ctbrowser_require_object";
/// `in` requires an object RHS before ToPropertyKey runs on its LHS (13.10.1).
inline constexpr std::string_view in_object_name = "__ctbrowser_in_object";
/// An accessor under a computed key (`get [k]() {}` in a class or a literal):
/// (target, key, getter, setter), the halves undefined when absent.
/// define_getter/define_setter take a name index, so a key that is only known
/// at run time goes through this one native rather than a new opcode.
inline constexpr std::string_view define_accessor_name = "__ctbrowser_define_accessor";
/// `define_own(obj, key, value, enumerable)`: [[DefineOwnProperty]] of a
/// writable, configurable data property. A static class member named `name`
/// or `length` needs it, since those are already own properties of every
/// function and a set_prop to them in strict (class) code is a TypeError.
/// Object literals use it too; this and ToPropertyKey exist in a bare context.
inline constexpr std::string_view define_own_name = "__ctbrowser_define_own";
/// ToPropertyKey (7.1.19): a symbol, or the string ToPrimitive(hint string)
/// makes. A class field's computed name is evaluated with it, once, when the
/// class is defined (15.7.10).
inline constexpr std::string_view to_property_key_name = "__ctbrowser_to_property_key";
/// SetFunctionName (10.2.9) for a key only known at run time: (fn, key[,
/// prefix]). A symbol names `[description]`, and "get"/"set" go in front. A
/// function that already has an own `name` (a class's static member) keeps it.
inline constexpr std::string_view set_function_name_name = "__ctbrowser_set_function_name";
/// `super.x = v` and `super[k] = v`: (base, key, v, this, strict), PutValue
/// of a Super Reference (6.2.5.6). That is [[Set]] on the home object's
/// prototype with `this` as the receiver, so a setter up there runs on `this`
/// and a data property lands on `this`. A refusal is strict code's TypeError.
inline constexpr std::string_view super_set_name = "__ctbrowser_super_set";

/// Installs the standard library into a context.
///
/// Defined out of line in builtins.cpp so the large definition is compiled
/// once rather than in every translation unit that includes this header.
///
/// `seed` makes Math.random deterministic by default: tests compare rendered
/// output byte for byte against goldens, which a page drawing with random
/// numbers could not have otherwise.
void install_builtins(context & cx, std::uint64_t seed = 0x2545F4914F6CDD1DULL);

/// Creates a realm with intrinsics of its own (9.3.1 CreateRealm, 9.3.2
/// SetDefaultGlobalBindings): a new realm, its global object, and every
/// intrinsic install_builtins makes, made again inside it, so its `Array` is
/// not this realm's `Array`. ShadowRealm is built on it. The realm lives as
/// long as the context.
realm_record & create_intrinsic_realm(context & cx);

} // namespace ctbrowser::script
