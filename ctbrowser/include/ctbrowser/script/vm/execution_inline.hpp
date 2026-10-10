#pragma once
//===- execution_inline.hpp - Inline execution members ----------*- C++ -*-===//
//
// Inline definitions of `context`'s execution plumbing: allocation, strings,
// globals and realms, the module hooks, errors and throws, the microtask
// queue, stack traces and the failure state.
//
//===----------------------------------------------------------------------===//

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
    // No JS string holds an internal key's lead byte (core/symbol.hpp): one
    // arriving here is a producer's undecoded bytes, and becomes the U+FFFD
    // that decoding would have made it, so no page ever holds a string that
    // spells a symbol's key or a slot's.
    ctbrowser::replace_internal_key_lead(s);
    return minted_string(std::move(s));
}

[[nodiscard]] inline auto context::minted_string(std::string s) -> value {
    // Two halves of a surrogate pair that meet here (`'\uD800' + '\uDC00'`,
    // String.fromCharCode(0xD800, 0xDC00)) are one code point, as they would
    // be in UTF-16 (core's join_surrogates).
    ctbrowser::join_surrogates(s);
    string_bytes_ += s.size();
    return value::object(allocate<string_object>(std::move(s)));
}

[[nodiscard]] inline auto context::make_object() -> value {
    return value::object(allocate<object_object>());
}

[[nodiscard]] inline auto context::make_array() -> value {
    return value::object(allocate<array_object>());
}

/// CreateIterResultObject (7.4.14): `{value, done}`, in that order.
[[nodiscard]] inline auto context::iter_result(value v, bool done) -> value {
    const value out = make_object();
    auto * obj = static_cast<object_object *>(out.as_heap());
    obj->set("value", v);
    obj->set("done", value::boolean(done));
    return out;
}

inline auto context::define_global(std::string name, value v) -> void {
    current_realm().globals[std::move(name)] = v;
}

inline auto context::define_builtin_global(std::string name, value v) -> void {
    realm_record & realm = current_realm();
    if (!is_intrinsic_name(name) && v.is_callable()) {
        const value prototype = lookup_property(v, "prototype");
        if (prototype.is_object()) { realm.platform_prototypes[name] = prototype; }
    }
    realm.builtin[name] = true;
    realm.globals[std::move(name)] = v;
}

inline auto context::mark_globals_builtin() -> void {
    realm_record & realm = current_realm();
    for (const auto & [name, held] : realm.globals) {
        realm.builtin[name] = true;
        intrinsic_names_[name] = true;
        realm.platform_prototypes.erase(name);
    }
}

inline auto context::declare_global_var(std::string name, bool configurable) -> void {
    realm_record & realm = current_realm();
    if (realm.globals.find(name) != realm.globals.end()) { return; }
    // The global object is a proxy over the table; what
    // `Object.defineProperty(globalThis, ...)` defines lands on its target,
    // and that is an own property of the global object too. Ask the target
    // directly, so a page's named elements, which the window's traps also
    // report, do not pass for declared variables.
    value holder = realm.global_this;
    if (holder.is_kind(heap_kind::proxy)) {
        holder = static_cast<proxy_object *>(holder.as_heap())->target;
    }
    property_descriptor own;
    if (holder.is_object() && own_property(holder, name, own)) { return; }
    if (!configurable) {
        realm.declared[name] = true;
        // And a property the proxy's target really has: a trap may report a
        // non-configurable property only if the target owns one (10.5.5's
        // invariants; `getOwnPropertyDescriptor` is a TypeError otherwise).
        // This is a placeholder; the table stays the storage the VM and
        // compiled code read, and the traps answer a declared name from it.
        if (holder.is_object()) {
            static_cast<object_object *>(holder.as_heap())
                ->define(name, value::undefined(), attr_writable | attr_enumerable);
        }
    }
    realm.globals[std::move(name)] = value::undefined();
}

/// A global assignment. NaN, Infinity and undefined refuse it; a binding the
/// table has takes it; otherwise it goes to a data property the global object
/// owns (9.1.1.4.5 SetMutableBinding: the object record's Set). A property
/// that `Object.defineProperty(globalThis, ...)` made lives on the proxy's
/// target, and it is written there so its descriptor and a bare read agree.
/// An accessor keeps the table's shadow, and the table is where every other
/// global is created.
///
/// Both the interpreter (VM_CASE(set_global)) and AOT-compiled code
/// (ct_aot_global_set) store through this function, so a compiled program and
/// its interpreted run cannot disagree about where a global assignment lands.
inline auto context::assign_global(realm_record & realm, const std::string & name, value v,
                                   bool strict) -> void {
    if (immutable_global(name)) [[unlikely]] {
        if (strict) {
            throw_error("TypeError",
                        "Cannot assign to read only property '" + name + "' of object");
        }
        return;
    }
    if (const auto it = realm.globals.find(name); it != realm.globals.end()) {
        it->second = v;
        return;
    }
    value holder = realm.global_this;
    if (holder.is_kind(heap_kind::proxy)) {
        holder = static_cast<proxy_object *>(holder.as_heap())->target;
    }
    if (holder.is_object()) {
        auto * target = static_cast<object_object *>(holder.as_heap());
        if (value * slot = target->find(name)) {
            if ((target->attrs_of(name) & attr_writable) != 0) {
                *slot = v;
            } else if (strict) {
                throw_error("TypeError",
                            "Cannot assign to read only property '" + name + "' of object");
            }
            return;
        }
    }
    realm.globals[name] = v;
}

inline auto context::declare_global_function(std::string name, bool configurable) -> void {
    realm_record & realm = current_realm();
    if (realm.declared.contains(name)) { return; }
    value holder = realm.global_this;
    if (holder.is_kind(heap_kind::proxy)) {
        holder = static_cast<proxy_object *>(holder.as_heap())->target;
    }
    property_descriptor own;
    if (configurable) {
        // D true: { writable, enumerable, configurable } is the table's own
        // description of a binding, so a configurable property of the target
        // steps aside for it (the value is the one the eval's hoisted
        // declaration writes); a non-configurable one keeps its storage.
        if (holder.is_object() && own_property(holder, name, own) && own.configurable) {
            (void)static_cast<object_object *>(holder.as_heap())->erase(name);
            (void)static_cast<object_object *>(holder.as_heap())->erase_accessor(name);
        }
        if (!realm.globals.contains(name)) { realm.globals[std::move(name)] = value::undefined(); }
        return;
    }
    if (!holder.is_object() || own_property(holder, name, own)) { return; }
    // A configurable table binding (a page's assignment, an intrinsic) is
    // redefined, which here means the declared mark and its placeholder (see
    // declare_global_var); the value is the one the script's hoisted
    // declaration writes before its first statement.
    realm.declared[name] = true;
    static_cast<object_object *>(holder.as_heap())
        ->define(name, value::undefined(), attr_writable | attr_enumerable);
    if (!realm.globals.contains(name)) { realm.globals[std::move(name)] = value::undefined(); }
}

/// `delete globalThis.x`: the binding is a table entry, and every global is
/// { configurable: true } (clause 17), so the delete succeeds.
inline auto context::erase_global(std::string_view name) -> bool {
    return current_realm().globals.erase(name) != 0;
}

inline auto context::define_native(std::string name, native_fn fn) -> void {
    value v = value::object(allocate<native_object>(name, std::move(fn)));
    current_realm().globals[std::move(name)] = v;
}

[[nodiscard]] inline auto context::global(std::string_view name) const -> value {
    const auto & globals = current_realm().globals;
    const auto it = globals.find(name);
    return it == globals.end() ? value::undefined() : it->second;
}

/// Whether the name is defined, which is not whether it is truthy: a global
/// explicitly set to undefined must still report as present, or `'foo' in
/// window` and a window that proxies to the globals disagree with
/// `typeof foo`.
[[nodiscard]] inline auto context::has_global(std::string_view name) const -> bool {
    const auto & globals = current_realm().globals;
    return globals.find(name) != globals.end();
}

/// Every global, for a window that enumerates itself.
[[nodiscard]] inline auto context::globals() const noexcept -> const string_flat_map<value> & {
    return current_realm().globals;
}

/// Whether script is running right now, that is, a native called from the
/// interpreter is on the C++ stack. `frames_` cannot answer this: a VM-level
/// raise (the call-stack ceiling) returns out of the loop without unwinding,
/// so the frames of a script that is over stay behind until the next
/// top-level `run` clears them.
[[nodiscard]] inline auto context::in_native() const noexcept -> bool {
    return native_depth_ > 0;
}

/// Installs what an undeclared name means, when the embedder has an answer.
/// HTML 7.3.3: an element with an `id` is reachable as a bare identifier, and
/// web-platform-tests rely on it constantly. The shell installs a function
/// that answers named elements; it is consulted only when the name is not a
/// global, so the declared path is one map lookup.
inline auto context::set_undeclared_name_hook(std::function<value(std::string_view)> hook) -> void {
    current_realm().undeclared_name = std::move(hook);
}

/// GetValue of an identifier reference (6.2.5.5, 9.1.1.4.6). The global
/// environment's object record is the global object, so a name that is not in
/// the binding table is read off `globalThis`: own, inherited (`toString`
/// resolves to Object.prototype's) or what the embedder's hook answers. A
/// name that is nowhere is an unresolvable reference and throws a catchable
/// ReferenceError, exactly what feature detection written as
/// `try { x } catch (e) {}` expects, unless the read is the operand of
/// `typeof` (13.5.3 step 2), the one silent case: `silent` is how the run loop
/// and the AOT bridge ask for it.
[[nodiscard]] inline auto context::global_or_named(std::string_view name, bool silent) -> value {
    realm_record & realm = current_realm();
    const auto it = realm.globals.find(name);
    if (it != realm.globals.end()) { return it->second; }
    if (realm.undeclared_name) {
        const value named = realm.undeclared_name(name);
        if (!named.is_undefined()) { return named; }
    }
    // The global object is a proxy in both embeddings, and a proxy is not
    // is_object(): it is a heap value of its own kind. Held in a local because
    // the hook above may have run script, and `has_property` may run a trap.
    const value global_object = realm.global_this;
    if (global_object.is_heap() && has_property(global_object, string(std::string{name}))) {
        return lookup_property(global_object, std::string{name});
    }
    if (!silent) { throw_error("ReferenceError", std::string{name} + " is not defined"); }
    return value::undefined();
}

/// The realm's script receiver, independent of the writable globalThis
/// binding. An embedder selects its host object before running scripts;
/// assigning globalThis in JavaScript never changes this identity.
[[nodiscard]] inline auto context::global_this() const noexcept -> value {
    return current_realm().global_this;
}

inline auto context::set_global_this(value receiver) -> void {
    current_realm().global_this = receiver;
    define_global("globalThis", receiver);
}

[[nodiscard]] inline auto context::current_realm() const noexcept -> realm_record & {
    if (frames_.size() > scope_depth_) {
        if (realm_record * running = frames_.back().realm; running != nullptr) { return *running; }
    }
    return *scope_realm_;
}

[[nodiscard]] inline auto context::entry_realm() const noexcept -> realm_record & {
    if (frames_.size() > scope_depth_) {
        if (realm_record * first = frames_[scope_depth_].realm; first != nullptr) { return *first; }
    }
    return current_realm();
}

[[nodiscard]] inline auto context::main_realm() const noexcept -> realm_record & {
    return *realms_.front();
}

/// The realm a call to `callee` runs in. A closure made by the embedder without
/// a realm runs in the caller's.
[[nodiscard]] inline auto context::realm_for(const closure_object * callee) const noexcept
    -> realm_record * {
    return callee != nullptr && callee->realm != nullptr ? callee->realm : &current_realm();
}

/// Installs what `import(specifier)` calls. The VM knows nothing about paths,
/// URLs or fetching, so a dynamic import hands the specifier and the module
/// that wrote it to the embedder and expects a promise back.
inline auto context::set_module_loader(
    std::function<value(context &, const std::string &, const std::string &)> loader) -> void {
    module_loader_ = std::move(loader);
}

/// Modules by specifier. The loader owns the graph; the VM only needs to look
/// a name up when `load_import` runs.
[[nodiscard]] inline auto context::modules() noexcept
    -> std::unordered_map<std::string, module_record> & {
    return modules_;
}

/// Installs what a deferred namespace calls to run its module: the loader's
/// own post-order evaluation, so the module's not-yet-run dependencies run
/// first. The evaluator answers false when the evaluation threw (last_thrown
/// says what).
inline auto context::set_module_evaluator(std::function<bool(context &, module_record &)> evaluator)
    -> void {
    module_evaluator_ = std::move(evaluator);
}

/// The receiver of the method call currently running, for natives. JS methods
/// get `this` from the call site; a native has no frame to read it from, so
/// the VM hands it over here. It is how one native object's methods tell which
/// object they were called on: `el.setText(...)` and `other.setText(...)` are
/// the same native.
[[nodiscard]] inline auto context::current_this() const noexcept -> value {
    return current_this_;
}

/// Runs a program nested inside another, such as `new Function(...)`
/// evaluating its own body while a script is halfway through a statement.
///
/// Not `run()`, which clears the failure flag and drains the microtask queue;
/// both belong to the turn rather than to the program. And not `execute()`,
/// which is the top-level entry: it clears `frames_` and `registers_` and
/// points `program_` at its argument, which, called nested, would throw away
/// the stack of whoever was running.
///
/// It calls a closure over the program's entry function normally. The
/// closure's `owner` is what makes every `op::closure` inside it index the
/// right function table.
[[nodiscard]] inline auto context::run_nested(const program & prog) -> value {
    if (!prog.ok || prog.functions.empty()) { return value::undefined(); }
    if (!global_declaration_instantiation(prog)) { return value::undefined(); }
    // Its `var`s exist before it starts, as run() gives a script's
    // (19.2.1.3 EvalDeclarationInstantiation binds them the same way).
    for (const std::string & name : prog.hoisted_vars) {
        if (!annex_b_skipped(prog, name)) { declare_global_var(name, prog.eval_code); }
    }
    for (const std::string & name : prog.function_names) {
        declare_global_function(name, prog.eval_code);
    }
    auto * entry = allocate<closure_object>(&prog.functions[0]);
    entry->owner = &prog;
    entry->realm = &current_realm();
    return call(value::object(entry), std::span<const value>{});
}

/// Keeps a compiled program alive for the life of the context. Compiling is
/// the caller's job because the VM does not depend on the compiler:
/// `compile.hpp` includes `vm.hpp`, not the other way round.
inline auto context::own_program(program compiled) -> const program & {
    owned_programs_.push_back(std::make_unique<program>(std::move(compiled)));
    return *owned_programs_.back();
}

/// Ends the run with a named message because a native refuses, the way any
/// other fault does. Not catchable from script; a native that needs to be
/// caught wants throw_error.
inline auto context::refuse(std::string_view what, std::string why) -> void {
    raise(std::string{what} + ": " + std::move(why));
}

/// Builds an error object without throwing it. A rejected promise carries one
/// and is not a throw, so building and throwing are separate.
[[nodiscard]] inline auto context::make_error(std::string_view kind, std::string message) -> value {
    value made = make_object();
    auto * o = static_cast<object_object *>(made.as_heap());
    // Use the prototype the kind names, not Error's, because
    // `assert.throws(TypeError, ...)` compares the constructor. `name` is not
    // an own property: 20.5.6.5 puts it on the prototype, and an own one would
    // show in `Object.keys(e)` and `e.hasOwnProperty('name')`.
    object_object * table = error_prototype(kind);
    if (table == nullptr) {
        // A kind with no constructor: "DataCloneError" is a DOMException name
        // rather than an ECMAScript one, and this engine has no DOMException.
        // Error.prototype plus an own `name` is the honest fallback: the name
        // is still right and `e instanceof Error` holds.
        table = prototype(proto_kind::error);
        o->define("name", string(std::string{kind}), attr_builtin);
    }
    // { true, false, true }, as 20.5.1.1 step 3 installs it.
    o->define("message", string(message), attr_builtin);
    // The frames it happened on, exactly as a constructed Error gets them: a
    // page catching a TypeError the VM raised should be able to report where
    // as easily as one it threw itself. Stored in the [[ErrorData]] slot the
    // Error constructor uses (builtins/objects/errors.cpp's error_stack_slot):
    // Error.prototype's `stack` accessor answers it, and Error.isError tests
    // for it.
    o->define(CTBROWSER_PRIVATE_KEY("ErrorData"),
              string(std::string{kind} + ": " + message + current_stack()), attr_none);
    if (table != nullptr) { o->prototype = value::object(table); }
    return made;
}

// --- One prototype per error kind -----------------------------------------
//
// `proto_kind` is a fixed enum over the value kinds property lookup falls
// back to, and the error types are not that: they are seven ordinary objects
// chained to one another, and the runtime only ever looks one up by the name
// a throw site wrote. A small keyed list rather than seven more enumerators
// keeps the fallback array, which every property read on a primitive
// indexes, exactly the size it was.

/// Registers the current realm's prototype for error kind `kind`, like the
/// other prototypes.
inline auto context::register_error_prototype(std::string kind, object_object * table) -> void {
    current_realm().intrinsics->error_prototypes.emplace_back(std::move(kind), table);
}

[[nodiscard]] inline auto context::error_prototype(std::string_view kind) const -> object_object * {
    for (const auto & [name, table] : current_realm().intrinsics->error_prototypes) {
        if (name == kind) { return table; }
    }
    return nullptr;
}

/// Throws a catchable error, ending the run only if nothing catches it. This
/// differs from raise(), which ends the run outright: calling a non-function
/// is a TypeError in JavaScript and pages catch it, since feature detection is
/// written as `try { thing() } catch (e) {}`.
inline auto context::throw_error(std::string_view kind, std::string message) -> void {
    // A throw is already crossing this native (see `call`): the first
    // one propagates, this one is the native carrying on after it.
    if (has_pending_throw_) { return; }
    thrown_ = make_error(kind, std::move(message));
    if (!unwind_to_handler()) { raise("uncaught " + describe_thrown(thrown_)); }
}

/// BindThisValue for the frame a native was called from (see
/// bind_this_name): a native pushes no frame, so frames_.back() is its
/// caller's, the derived constructor whose `super()` just returned. An arrow
/// shares its enclosing constructor's binding through a cell.
///
/// This also applies when the parent is native: Error, Map and Date fill the
/// instance and return it, and Array and the typed arrays return their own
/// array carrying the instance's prototype (adopt_subclass_prototype). Either
/// way the [[Construct]] result becomes `this` from here on, so
/// `class A extends Array` builds a real array.
inline auto context::rebind_receiver(value v) -> void {
    if (frames_.empty() || frames_.back().closure == nullptr) { return; }
    call_frame & frame = frames_.back();
    const value cell = frame.closure->proto != nullptr && frame.closure->proto->is_arrow
                           ? frame.closure->captured_this
                           : frame.receiver_cell;
    if (cell.is_kind(heap_kind::cell)) { static_cast<cell_object *>(cell.as_heap())->slot = v; }
    frame.receiver = v;
}

/// Takes the parked throw as a value rather than rethrowing it, for a native
/// that has to convert a throw crossing one of its `call`s, for example into a
/// rejected promise, as EvaluateImportCall does with a specifier whose
/// toString threw. Undefined when nothing is parked.
[[nodiscard]] inline auto context::take_pending_throw() -> value {
    if (!has_pending_throw_) { return value::undefined(); }
    has_pending_throw_ = false;
    const value taken = pending_throw_;
    pending_throw_ = value::undefined();
    return taken;
}

/// Throws again, from the native's call site, the throw `call` parked.
/// Returns whether there was one; the caller then continues as after any other
/// unwind.
inline auto context::rethrow_pending() -> bool {
    if (!has_pending_throw_) { return false; }
    has_pending_throw_ = false;
    thrown_ = pending_throw_;
    pending_throw_ = value::undefined();
    // Thrown where `call` caught it, not at this native's call site.
    const throw_site began = throw_site_;
    const bool landed = unwind_to_handler();
    throw_site_ = began;
    if (!landed) { raise("uncaught " + describe_thrown(thrown_)); }
    return true;
}

/// Throws something that is not an ECMAScript Error. A DOM method must throw a
/// DOMException, and `assert_throws_dom` checks `code`, `name` and
/// `e.constructor === DOMException`. The unwinding is exactly `throw_error`'s,
/// with the object supplied rather than made.
inline auto context::throw_value(value thrown) -> void {
    if (has_pending_throw_) { return; }
    thrown_ = thrown;
    if (!unwind_to_handler()) { raise("uncaught " + describe_thrown(thrown_)); }
}

/// Queues a job for the end of the turn. FIFO, and a job queued by a job runs
/// in the same drain, which is what makes a promise chain complete before the
/// turn ends rather than one link per turn.
inline auto context::queue_microtask(value fn, std::vector<value> args) -> void {
    if (fn.is_callable()) { microtasks_.push_back(microtask{fn, std::move(args)}); }
}

/// Runs the queue to exhaustion. Called after the top-level script and from
/// the event loop: after timers, before animation frames, and again after each
/// event dispatch, which is where a browser puts its checkpoints.
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
    if (!failed_ && checkpoint_hook_) { checkpoint_hook_(); }
}

/// The stack as it is right now, for raise() and for a constructed Error's
/// `stack`. It prints each function's index as well as its name: most of a
/// bundle's functions are anonymous, and the index is what points the corpus
/// ratchet (tools/corpus/ratchet.py) straight at the failing code.
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
        // A compiled frame has no bytecode offset, so none is printed. `ip` on
        // a compiled frame is where the unwinder puts the landing pad, with
        // CT_AOT_PAD_BIT set, so a trace captured between a catch firing and
        // ct_aot_catch_land reading it would print 9223372036854775815
        // (2^63 + 7) as an offset.
        const bool compiled = fp->aot_entry != nullptr;
        trace +=
            "\n        at " + (fp->name.empty() ? std::string{"<anonymous>"} : fp->name) + " (fn#" +
            std::to_string(which) +
            (compiled ? std::string{", compiled)"} : " +" + std::to_string(frames_[i].ip) + ")");
    }
    if (depth > 12) { trace += "\n        ... " + std::to_string(depth - 12) + " more"; }
    return trace;
}

/// Whether a fault is outstanding; error() is the message. `run` clears these
/// on entry and reports them in its result; `call` has no result to report
/// through, so a host that drives callbacks must ask.
[[nodiscard]] inline auto context::failed() const noexcept -> bool {
    return failed_;
}

[[nodiscard]] inline auto context::error() const noexcept -> const std::string & {
    return error_;
}

/// Resets store_rejected_; a store from strict code checks it afterwards.
inline auto context::clear_store_rejected() noexcept -> void {
    store_rejected_ = false;
}

/// Whether the last store was refused (a non-writable property, a `set` trap
/// answering false, a non-extensible receiver). This is Reflect.set's answer.
[[nodiscard]] inline auto context::store_rejected() const noexcept -> bool {
    return store_rejected_;
}

/// Throws the strict-mode TypeError for an assignment to `name` if the last
/// store was rejected, and clears the flag.
inline auto context::strict_store_check(std::string_view name) -> void {
    if (!store_rejected_) { return; }
    store_rejected_ = false;
    throw_error("TypeError",
                "Cannot assign to read only property '" + std::string{name} + "' of object");
}

/// Whether a native should stop: a throw crossed one of its `call`s, reached
/// its enclosing C++ fence, or the run has failed outright. A fence leaves the
/// interpreted caller's stack unchanged until call_fenced consumes it.
[[nodiscard]] inline auto context::throw_pending() const noexcept -> bool {
    return has_pending_throw_ || fence_hit_ || failed_;
}

/// How many throws have unwound, ever. A throw a native raised itself through
/// throw_error is not parked: it has already landed on a handler by the time
/// the native's next line runs, and throw_pending cannot see it, so a native
/// that keeps going compares this before and after.
[[nodiscard]] inline auto context::unwinds() const noexcept -> std::size_t {
    return unwinds_;
}

/// The value an uncaught throw left behind, for a host that has to name its
/// constructor rather than print it (`tools/ct262` on a `negative:` test).
/// Undefined when a run failed without a throw (the allocation ceiling, the
/// call-stack ceiling); stale after a run that succeeded, so read it only when
/// `run_result::ok` is false.
[[nodiscard]] inline auto context::last_thrown() const noexcept -> value {
    return thrown_;
}

/// Takes the outstanding failure message and clears the failure, which is what
/// a browser does: an exception in one callback does not cancel the next one
/// or end the page.
[[nodiscard]] inline auto context::take_error() -> std::string {
    std::string out = std::move(error_);
    error_.clear();
    failed_ = false;
    return out;
}

} // namespace ctbrowser::script
