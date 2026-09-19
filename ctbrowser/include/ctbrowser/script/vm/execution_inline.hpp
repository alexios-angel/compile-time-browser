#pragma once

#include "../vm.hpp"

namespace ctbrowser::script {

inline context::~context() {
    sweep_all();
}

template <typename T, typename... Args>
[[nodiscard]] inline auto context::allocate(Args &&... args) -> T * {
    if (++allocations_ > allocation_ceiling && !failed_) {
        raise("allocation ceiling reached (" + std::to_string(allocation_ceiling) +
              " objects) - a loop is not terminating");
    }
    auto * p = new T(std::forward<Args>(args)...);
    p->next = heap_;
    heap_ = p;
    ++live_objects_;
    // The escape oracle's hook: one predictable not-taken branch per
    // allocation, which the `new` above dwarfs.
    if (recorder_ != nullptr) [[unlikely]] { note_allocation(p); }
    return p;
}

[[nodiscard]] inline auto context::string(std::string s) -> value {
    // Two halves of a surrogate pair that met here - `'\uD800' +
    // '\uDC00'`, String.fromCharCode(0xD800, 0xDC00) - are one code
    // point, as they would be in UTF-16 (core's join_surrogates).
    ctbrowser::join_surrogates(s);
    return value::object(allocate<string_object>(std::move(s)));
}

[[nodiscard]] inline auto context::make_object() -> value {
    return value::object(allocate<object_object>());
}

[[nodiscard]] inline auto context::make_array() -> value {
    return value::object(allocate<array_object>());
}

// CreateIterResultObject (7.4.14): `{value, done}`, in that order.
[[nodiscard]] inline auto context::iter_result(value v, bool done) -> value {
    const value out = make_object();
    auto * obj = static_cast<object_object *>(out.as_heap());
    obj->set("value", v);
    obj->set("done", value::boolean(done));
    return out;
}

inline auto context::define_global(std::string name, value v) -> void {
    globals_[std::move(name)] = v;
}

// `delete globalThis.x`: the binding is a table entry, and every global
// is { configurable: true } (clause 17), so the delete succeeds.
inline auto context::erase_global(std::string_view name) -> bool {
    return globals_.erase(name) != 0;
}

inline auto context::define_native(std::string name, native_fn fn) -> void {
    value v = value::object(allocate<native_object>(name, std::move(fn)));
    globals_[std::move(name)] = v;
}

[[nodiscard]] inline auto context::global(std::string_view name) const -> value {
    const auto it = globals_.find(name);
    return it == globals_.end() ? value::undefined() : it->second;
}

// Whether the name is DEFINED, which is not whether it is truthy or even
// defined-and-undefined: `window.foo` on a global explicitly set to
// undefined must still report the global as present, or `'foo' in window`
// and a window that proxies to the globals disagree with `typeof foo`.
[[nodiscard]] inline auto context::has_global(std::string_view name) const -> bool {
    return globals_.find(name) != globals_.end();
}

// Every global, for a window that enumerates itself.
[[nodiscard]] inline auto context::globals() const noexcept -> const string_flat_map<value> & {
    return globals_;
}

// WHETHER SCRIPT IS RUNNING RIGHT NOW - a native called from the
// interpreter is on the C++ stack. `frames_` cannot answer this: a
// VM-level raise (the call-stack ceiling) returns out of the loop without
// unwinding, so the frames of a script that is OVER stay behind until the
// next top-level `run` clears them.
[[nodiscard]] inline auto context::in_native() const noexcept -> bool {
    return native_depth_ > 0;
}

// WHAT AN UNDECLARED NAME MEANS, when the embedder has an answer. HTML
// 7.3.3: an element with an `id` is reachable as a bare identifier, and
// web-platform-tests leans on it constantly. The shell installs a
// function that answers named elements; it is consulted ONLY when the
// name is not a global, so the declared path is one map lookup.
inline auto context::set_undeclared_name_hook(std::function<value(std::string_view)> hook) -> void {
    undeclared_name_ = std::move(hook);
}

// GetValue OF AN IDENTIFIER REFERENCE (6.2.5.5, 9.1.1.4.6). The global
// environment's object record IS the global object, so a name that is
// not in the binding table is read off `globalThis` - own, inherited
// (`toString` resolves to Object.prototype's) or what the embedder's hook
// answers - and a name that is nowhere is an unresolvable reference:
// ReferenceError, catchable, exactly what feature detection written as
// `try { x } catch (e) {}` expects - unless the read is the operand of
// `typeof` (13.5.3 step 2), which is the one silent one: `silent` is how
// the run loop and the AOT bridge ask for it.
[[nodiscard]] inline auto context::global_or_named(std::string_view name, bool silent) -> value {
    const auto it = globals_.find(name);
    if (it != globals_.end()) { return it->second; }
    if (undeclared_name_) {
        const value named = undeclared_name_(name);
        if (!named.is_undefined()) { return named; }
    }
    // The global object is a PROXY in both embeddings, and a proxy is not
    // is_object() - it is a heap value of its own kind.
    if (global_this_.is_heap() && has_property(global_this_, string(std::string{name}))) {
        return lookup_property(global_this_, std::string{name});
    }
    if (!silent) { throw_error("ReferenceError", std::string{name} + " is not defined"); }
    return value::undefined();
}

// The realm's script receiver is independent of the writable globalThis
// binding. An embedder selects its host object before running scripts;
// assigning globalThis in JavaScript never changes this identity.
[[nodiscard]] inline auto context::global_this() const noexcept -> value {
    return global_this_;
}

inline auto context::set_global_this(value receiver) -> void {
    global_this_ = receiver;
    define_global("globalThis", receiver);
}

// WHAT `import(specifier)` CALLS. The VM knows nothing about paths, URLs or
// fetching, so a dynamic import hands the specifier and the module that
// wrote it to the embedder and expects a promise back.
inline auto context::set_module_loader(
    std::function<value(context &, const std::string &, const std::string &)> loader) -> void {
    module_loader_ = std::move(loader);
}

// Where a module is found by specifier. The loader owns the graph; the VM
// only needs to look a name up when `load_import` runs.
[[nodiscard]] inline auto context::modules() noexcept -> flat_map<std::string, module_record> & {
    return modules_;
}

// WHAT A DEFERRED NAMESPACE CALLS TO RUN ITS MODULE: the loader's own
// post-order evaluation, so the module's not-yet-run dependencies run
// first. Answers false when the evaluation threw (last_thrown says what).
inline auto context::set_module_evaluator(std::function<bool(context &, module_record &)> evaluator)
    -> void {
    module_evaluator_ = std::move(evaluator);
}

// The receiver of the method call currently running, for natives. JS
// methods get `this` from the call site; a native has no frame to read it
// from, so the VM hands it over here. It is how one native object's methods
// tell which object they were called on - `el.setText(...)` and
// `other.setText(...)` are the same native.
[[nodiscard]] inline auto context::current_this() const noexcept -> value {
    return current_this_;
}

// Run a program NESTED inside another - `new Function(...)` evaluating its
// own body while a script is halfway through a statement.
//
// Not `run()`, which clears the failure flag and drains the microtask
// queue - both belong to the TURN rather than to the program. And not
// `execute()`, which is the TOP-LEVEL entry: it clears `frames_` and
// `registers_` and points `program_` at its argument, which called nested
// throws away the stack of whoever was running.
//
// A closure over the program's entry function, called normally. Its `owner`
// is what makes every `op::closure` inside it index the right table.
[[nodiscard]] inline auto context::run_nested(const program & prog) -> value {
    if (!prog.ok || prog.functions.empty()) { return value::undefined(); }
    // Its `var`s exist before it starts, as run() gives a script's
    // (19.2.1.3 EvalDeclarationInstantiation binds them the same way).
    for (const std::string & name : prog.hoisted_vars) {
        if (!has_global(name)) { define_global(name, value::undefined()); }
    }
    auto * entry = allocate<closure_object>(&prog.functions[0]);
    entry->owner = &prog;
    return call(value::object(entry), std::span<const value>{});
}

// KEEP a compiled program for the life of the context. The compiler is
// the caller's because the VM does not depend on it - `compile.hpp`
// includes `vm.hpp`, not the other way round.
inline auto context::own_program(program compiled) -> const program & {
    owned_programs_.push_back(std::make_unique<program>(std::move(compiled)));
    return *owned_programs_.back();
}

// A NATIVE REFUSING. Ends the run with a named message, the way any other
// fault does. Not catchable from script; a native that needs to be caught
// wants throw_error.
inline auto context::refuse(std::string_view what, std::string why) -> void {
    raise(std::string{what} + ": " + std::move(why));
}

// AN ERROR OBJECT WITHOUT THROWING IT. A rejected promise carries one and
// is not a throw, so building and throwing are separate.
[[nodiscard]] inline auto context::make_error(std::string_view kind, std::string message) -> value {
    value made = make_object();
    auto * o = static_cast<object_object *>(made.as_heap());
    // ON THE PROTOTYPE THE KIND NAMES, not on Error's: `assert.throws
    // (TypeError, ...)` compares the CONSTRUCTOR. `name` is NOT an own
    // property - 20.5.6.5 puts it on the prototype, and an own one would
    // show in `Object.keys(e)` and `e.hasOwnProperty('name')`.
    object_object * table = error_prototype(kind);
    if (table == nullptr) {
        // A KIND WITH NO CONSTRUCTOR - "DataCloneError" is a DOMException
        // name rather than an ECMAScript one, and this engine has no
        // DOMException. Error.prototype plus an own `name` is the honest
        // fallback: the name is still right and `e instanceof Error` holds.
        table = prototype(proto_kind::error);
        o->define("name", string(std::string{kind}), attr_builtin);
    }
    // { true, false, true }, as 20.5.1.1 step 3 installs it.
    o->define("message", string(message), attr_builtin);
    // The frames it happened on, exactly as a constructed Error gets them -
    // a page catching a TypeError the VM raised should be able to report
    // where as easily as one it threw itself. IN THE [[ErrorData]] SLOT
    // the Error constructor uses (builtins/objects/errors.cpp's
    // error_stack_slot): Error.prototype's `stack` accessor answers it,
    // and Error.isError tests for it.
    o->define("@#ErrorData", string(std::string{kind} + ": " + message + current_stack()),
              attr_none);
    if (table != nullptr) { o->prototype = value::object(table); }
    return made;
}

// --- ONE PROTOTYPE PER ERROR KIND ------------------------------------
//
// `proto_kind` is a fixed enum over the value KINDS property lookup falls
// back to, and the error types are not that: they are seven ordinary
// objects chained to one another, and the runtime only ever looks one up by
// the name a throw site wrote. A small keyed list rather than seven more
// enumerators keeps the fallback array - which every property read on a
// primitive indexes - exactly the size it was.
inline auto context::register_error_prototype(std::string kind, object_object * table) -> void {
    error_prototypes_.emplace_back(std::move(kind), table);
}

[[nodiscard]] inline auto context::error_prototype(std::string_view kind) const -> object_object * {
    for (const auto & [name, table] : error_prototypes_) {
        if (name == kind) { return table; }
    }
    return nullptr;
}

// THROW A CATCHABLE ERROR, and only end the run if nothing catches it.
// Different from raise(), which ends the run outright: calling a
// non-function is a TypeError in JavaScript and pages CATCH it - feature
// detection is written as `try { thing() } catch (e) {}`.
inline auto context::throw_error(std::string_view kind, std::string message) -> void {
    // A throw is already crossing this native (see `call`): the first
    // one propagates, this one is the native carrying on after it.
    if (has_pending_throw_) { return; }
    thrown_ = make_error(kind, std::move(message));
    if (!unwind_to_handler()) { raise("uncaught " + describe_thrown(thrown_)); }
}

// BindThisValue for the frame a native was called from (see
// bind_this_name): a native pushes no frame, so frames_.back() is its
// caller's - the derived constructor whose `super()` just returned. An
// arrow's frame (a `super()` inside one) rebinds only the arrow's own
// receiver. A NATIVE PARENT TOO: Error, Map, Date fill the instance and
// answer it, and Array and the typed arrays answer an array of their own
// carrying the instance's prototype (adopt_subclass_prototype) - either
// way the answer is the [[Construct]] result and is `this` from here.
// Until 2026-09-17 a native parent was skipped, so `class A extends
// Array` built a plain object that was never an array.
inline auto context::rebind_receiver(value v) -> void {
    if (frames_.empty() || frames_.back().closure == nullptr) { return; }
    frames_.back().receiver = v;
}

// THE PARKED THROW, TAKEN AS A VALUE rather than rethrown: for a native
// that has to CONVERT a throw crossing one of its `call`s - into a rejected
// promise, as EvaluateImportCall does with a specifier whose toString
// threw. Undefined when nothing is parked.
[[nodiscard]] inline auto context::take_pending_throw() -> value {
    if (!has_pending_throw_) { return value::undefined(); }
    has_pending_throw_ = false;
    const value taken = pending_throw_;
    pending_throw_ = value::undefined();
    return taken;
}

// The throw `call` parked, thrown again from the native's call site.
// Answers whether there was one; the caller then continues as after any
// other unwind.
inline auto context::rethrow_pending() -> bool {
    if (!has_pending_throw_) { return false; }
    has_pending_throw_ = false;
    thrown_ = pending_throw_;
    pending_throw_ = value::undefined();
    if (!unwind_to_handler()) { raise("uncaught " + describe_thrown(thrown_)); }
    return true;
}

// THROW SOMETHING THAT IS NOT AN ECMAScript Error - a DOM method must throw
// a DOMException, and `assert_throws_dom` checks `code`, `name` AND
// `e.constructor === DOMException`. The unwinding is `throw_error`'s
// exactly, with the object supplied rather than made.
inline auto context::throw_value(value thrown) -> void {
    if (has_pending_throw_) { return; }
    thrown_ = thrown;
    if (!unwind_to_handler()) { raise("uncaught " + describe_thrown(thrown_)); }
}

// Queue a job for the end of the turn. FIFO, and a job queued BY a job runs
// in the same drain - that is what makes a promise chain complete before
// the turn ends rather than one link per turn.
inline auto context::queue_microtask(value fn, std::vector<value> args) -> void {
    if (fn.is_callable()) { microtasks_.push_back(microtask{fn, std::move(args)}); }
}

// Run the queue to exhaustion. Called after the top-level script and from
// the event loop - after timers, before animation frames, and again after
// each event dispatch, which is where a browser puts its checkpoints.
inline auto context::drain_microtasks() -> void {
    // Bounded: a job that queues a job that queues a job forever is a
    // runaway page, and the alternative to a cap is a hang with no message.
    for (std::size_t ran = 0; !microtasks_.empty() && ran < 1'000'000 && !failed_; ++ran) {
        const microtask job = std::move(microtasks_.front());
        microtasks_.pop_front();
        (void)call(job.fn, job.args);
    }
    if (!microtasks_.empty() && !failed_) {
        microtasks_.clear();
        raise("the microtask queue did not drain - a promise chain is not terminating");
    }
}

// THE STACK AS IT IS RIGHT NOW - for raise() and for a constructed
// Error's `stack`. The function's INDEX as well as its name: most of a
// bundle's functions are anonymous, and the index is what points the
// corpus ratchet (tools/corpus/ratchet.py) straight at the failing code.
[[nodiscard]] inline auto context::current_stack(std::size_t skip) const -> std::string {
    std::string trace;
    int shown = 0;
    const std::size_t depth = frames_.size() > skip ? frames_.size() - skip : 0;
    for (std::size_t i = depth; i-- > 0 && shown < 12; ++shown) {
        const function_proto * fp = frames_[i].proto;
        if (fp == nullptr) { continue; }
        std::size_t which = 0;
        const program * owner =
            frames_[i].closure != nullptr && frames_[i].closure->owner != nullptr
                ? frames_[i].closure->owner
                : program_;
        if (owner != nullptr) {
            for (std::size_t k = 0; k < owner->functions.size(); ++k) {
                if (&owner->functions[k] == fp) {
                    which = k;
                    break;
                }
            }
        }
        // A COMPILED FRAME HAS NO BYTECODE OFFSET, so it does not get one:
        // `ip` on a compiled frame is where the unwinder puts the LANDING
        // PAD, with CT_AOT_PAD_BIT set, so a trace captured between a catch
        // firing and ct_aot_catch_land reading it would print
        // 9223372036854775815 as an offset.
        const bool compiled = fp->aot_entry != nullptr;
        trace +=
            "\n        at " + (fp->name.empty() ? std::string{"<anonymous>"} : fp->name) + " (fn#" +
            std::to_string(which) +
            (compiled ? std::string{", compiled)"} : " +" + std::to_string(frames_[i].ip) + ")");
    }
    if (depth > 12) { trace += "\n        ... " + std::to_string(depth - 12) + " more"; }
    return trace;
}

// Whether a fault is outstanding, and the message. `run` clears these on
// entry and reports them in its result; `call` has no result to report
// through, so a host that drives callbacks must ask.
[[nodiscard]] inline auto context::failed() const noexcept -> bool {
    return failed_;
}

[[nodiscard]] inline auto context::error() const noexcept -> const std::string & {
    return error_;
}

// See store_rejected_: a store from strict code asks this afterwards.
inline auto context::clear_store_rejected() noexcept -> void {
    store_rejected_ = false;
}

// Was the last store refused (a non-writable property, a `set` trap
// answering false, a non-extensible receiver)? Reflect.set's answer.
[[nodiscard]] inline auto context::store_rejected() const noexcept -> bool {
    return store_rejected_;
}

inline auto context::strict_store_check(std::string_view name) -> void {
    if (!store_rejected_) { return; }
    store_rejected_ = false;
    throw_error("TypeError",
                "Cannot assign to read only property '" + std::string{name} + "' of object");
}

// WHETHER A NATIVE SHOULD STOP: a throw crossed one of its `call`s and is
// parked for rethrow at its call site, or the run has failed outright.
// Every further `call` would answer undefined without running anything.
[[nodiscard]] inline auto context::throw_pending() const noexcept -> bool {
    return has_pending_throw_ || failed_;
}

// HOW MANY THROWS HAVE UNWOUND, EVER. A throw a native raised itself
// through throw_error is not parked - it has already landed on a handler
// by the time the native's next line runs, and throw_pending cannot see
// it - so a native that keeps going compares this before and after.
[[nodiscard]] inline auto context::unwinds() const noexcept -> std::size_t {
    return unwinds_;
}

// THE VALUE AN UNCAUGHT THROW LEFT BEHIND, for a host that has to NAME its
// constructor rather than print it (`tools/ct262` on a `negative:` test).
// Undefined when a run failed WITHOUT a throw (the allocation ceiling, the
// call-stack ceiling); stale after a run that succeeded, so read it only
// when `run_result::ok` is false.
[[nodiscard]] inline auto context::last_thrown() const noexcept -> value {
    return thrown_;
}

// Report it and carry on, which is what a browser does: an exception in one
// callback does not cancel the next one or end the page.
[[nodiscard]] inline auto context::take_error() -> std::string {
    std::string out = std::move(error_);
    error_.clear();
    failed_ = false;
    return out;
}

} // namespace ctbrowser::script
