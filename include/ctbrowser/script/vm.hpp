#pragma once
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <ctbrowser/core/core.hpp>

#include <ctbrowser/script/bytecode.hpp>
#include <ctbrowser/script/value.hpp>

// The interpreter.
//
// the previous engine walked the AST on every execution: each time round a loop it re-decided
// what every node meant, re-looked-up every identifier by string, and
// re-dispatched through a virtual call per node. This walks a flat array of
// 4-byte instructions with registers already assigned.
//
// GC is mark-and-sweep over precise roots. Precise because the VM knows
// exactly where its roots are - the register stack, the globals table, and
// the call frames - so there is no conservative stack scanning and no
// pointer-shaped integer can accidentally keep an object alive. Generational
// collection is the next step, and the allocation list here is already the
// shape a nursery would slot into.
//
// One agent per thread, like a real JS agent: a context is NOT thread-safe
// and is not meant to be. Workers get their own context; what they share is
// the DOM, which has its own concurrency control.

namespace ctbrowser::script {

struct closure_object;

using native_fn = std::function<value(class context &, std::span<value>)>;

struct native_object final : heap_object {
    std::string name;
    native_fn fn;
    // A NATIVE IS A FUNCTION THAT IS ALSO AN OBJECT. `Symbol` has to be both -
    // `Symbol('x')` calls it and `Symbol.iterator` reads a property of it - and
    // without a table here the two were mutually exclusive. Closures have had
    // one since classes needed somewhere for their statics; this is the same
    // need arriving for the built-ins.
    std::vector<std::pair<std::string, value>> props;

    [[nodiscard]] value * find(std::string_view key) {
        for (auto & [k, item] : props) {
            if (k == key) { return &item; }
        }
        return nullptr;
    }
    void set(std::string_view key, value v) {
        if (value * existing = find(key)) {
            *existing = v;
            return;
        }
        props.emplace_back(std::string{key}, v);
    }

    native_object(std::string n, native_fn f)
        : heap_object(heap_kind::native), name(std::move(n)), fn(std::move(f)) {}
};

// A captured variable's box. Sharing the CELL rather than the value is what
// makes a mutation through a closure visible to everyone else holding it.
struct cell_object final : heap_object {
    value slot;
    explicit cell_object(value v) : heap_object(heap_kind::cell), slot(v) {}
};

struct closure_object final : heap_object {
    // A function IS an object in JavaScript, and a class compiles to one: its
    // statics and its `prototype` live here. Without a property table on a
    // closure, `class C { static make() {} }` had nowhere to put `make` and
    // `C.prototype` could not be read back, so `extends` found nothing.
    std::vector<std::pair<std::string, value>> props;

    [[nodiscard]] value * find(std::string_view name) {
        for (auto & [key, item] : props) {
            if (key == name) { return &item; }
        }
        return nullptr;
    }
    void set(std::string_view name, value v) {
        if (value * existing = find(name)) {
            *existing = v;
            return;
        }
        props.emplace_back(std::string{name}, v);
    }

    // A CLASS IS A CLOSURE, so `static get w()` has nowhere else to go. Same
    // table as an object's, and empty on every function that is not a class
    // with a static accessor.
    accessor_table accessors;
    [[nodiscard]] accessor_entry * find_accessor(std::string_view name) {
        return accessors.find(name);
    }
    void define_accessor(std::string_view name, value getter, value setter) {
        accessors.define(name, getter, setter);
    }

    // The function's OWN [[Prototype]] - what `Object.getPrototypeOf(F)`
    // returns - which is a different thing from the `prototype` PROPERTY that
    // its instances get. Babel's `_inherits` sets both: the subclass's
    // prototype property chains to the superclass's for instance methods, and
    // the subclass FUNCTION chains to the superclass function for static ones.
    // Without this, `_getPrototypeOf(I18n).call(this)` read null.
    value proto_link = value::null();

    const function_proto * proto = nullptr;
    // WHICH PROGRAM ITS NESTED FUNCTIONS LIVE IN.
    //
    // `op::closure` names a function by INDEX, and the index only means
    // anything in the program it was compiled against. One `program_` for the
    // whole context was fine while there was one program - and stopped being
    // fine the moment a page could run a second script, because a closure from
    // the first would then index the second program's function table and read
    // off the end of it.
    const program * owner = nullptr;
    std::vector<value> upvalues; // each one is a cell_object
    // Only meaningful when proto->is_arrow: the `this` in scope where the arrow
    // was written, captured when the closure was made. An arrow has no receiver
    // of its own, so this is the only place its `this` can come from.
    value captured_this = value::undefined();
    explicit closure_object(const function_proto * p)
        : heap_object(heap_kind::function), proto(p) {}
};

struct run_result {
    value returned = value::undefined();
    bool ok = true;
    std::string error;
};

class context {
public:
    context() = default;
    ~context() { sweep_all(); }

    context(const context &) = delete;
    context & operator=(const context &) = delete;

    // --- allocation -------------------------------------------------------
    // A RUNAWAY PAGE IS REFUSED, not left to exhaust the machine.
    //
    // std::bad_alloc from inside a script is the worst failure this engine can
    // have: it takes the process down with no line, no stack and nothing to act
    // on, and on a small machine it takes the machine with it. A cap turns that
    // into an ordinary fault with the JS stack attached, which is a bug report.
    //
    // The number is deliberately far above any real page - p5.js loading
    // allocates a few hundred thousand - so reaching it means a loop that does
    // not terminate rather than a page that is merely large.
    static constexpr std::size_t allocation_ceiling = 40'000'000;

    template <typename T, typename... Args> [[nodiscard]] T * allocate(Args &&... args) {
        if (++allocations_ > allocation_ceiling && !failed_) {
            raise("allocation ceiling reached (" + std::to_string(allocation_ceiling) +
                  " objects) - a loop is not terminating");
        }
        auto * p = new T(std::forward<Args>(args)...);
        p->next = heap_;
        heap_ = p;
        ++live_objects_;
        return p;
    }
    [[nodiscard]] value string(std::string s) {
        return value::object(allocate<string_object>(std::move(s)));
    }
    [[nodiscard]] value make_object() { return value::object(allocate<object_object>()); }
    [[nodiscard]] value make_array() { return value::object(allocate<array_object>()); }

    void define_global(std::string name, value v) { globals_[std::move(name)] = v; }
    void define_native(std::string name, native_fn fn) {
        value v = value::object(allocate<native_object>(name, std::move(fn)));
        globals_[std::move(name)] = v;
    }
    [[nodiscard]] value global(std::string_view name) const {
        const auto it = globals_.find(std::string{name});
        return it == globals_.end() ? value::undefined() : it->second;
    }
    // Whether the name is DEFINED, which is not whether it is truthy or even
    // defined-and-undefined: `window.foo` on a global explicitly set to
    // undefined must still report the global as present, or `'foo' in window`
    // and a window that proxies to the globals disagree with `typeof foo`.
    [[nodiscard]] bool has_global(std::string_view name) const {
        return globals_.find(std::string{name}) != globals_.end();
    }
    // Every global, for a window that enumerates itself.
    [[nodiscard]] const flat_map<std::string, value> & globals() const noexcept { return globals_; }

    // --- execution ---------------------------------------------------------
    //
    // THE PROGRAM MUST OUTLIVE THE CONTEXT. Closures hold `const function_proto *`
    // into it, and anything that calls back in later - a timer, an event
    // listener, requestAnimationFrame - dereferences them long after run()
    // returned. Passing a temporary works exactly until the first callback.
    run_result run(const program & prog);

    // The receiver of the method call currently running, for natives. JS
    // methods get `this` from the call site; a native has no frame to read it
    // from, so the VM hands it over here. It is how one native object's methods
    // tell which object they were called on - `el.setText(...)` and
    // `other.setText(...)` are the same native.
    [[nodiscard]] value current_this() const noexcept { return current_this_; }

    // A NATIVE REFUSING. Ends the run with a named message, the way any other
    // fault does - natives had no way to say "this cannot work" and were
    // returning undefined, which reappears later as a different error
    // somewhere else. Not catchable from script yet; a native that needs to be
    // caught wants a real thrown Error, which is a larger change.
    void refuse(std::string_view what, std::string why) {
        raise(std::string{what} + ": " + std::move(why));
    }

    // THROW A CATCHABLE ERROR, and only end the run if nothing catches it.
    //
    // Different from raise(), which ends the run outright. Calling a
    // non-function is a TypeError in JavaScript and pages CATCH it - feature
    // detection is written as `try { thing() } catch (e) {}` more often than as
    // a typeof test, and a library that probes for an optional method that way
    // took the whole page down here.
    //
    // It also makes a diagnosis possible: an uncatchable fault unwinds nothing,
    // so a probe wrapped in try/catch reported no error at all and the failure
    // appeared to come from wherever the run happened to stop.
    void throw_error(std::string_view kind, std::string message) {
        value made = make_object();
        auto * o = static_cast<object_object *>(made.as_heap());
        o->set("name", string(std::string{kind}));
        o->set("message", string(message));
        // On the Error prototype, so `e instanceof Error` and `e.toString()`
        // work on a thrown one exactly as on `new TypeError(...)`.
        if (object_object * table = prototype(proto_kind::error)) {
            o->prototype = value::object(table);
        }
        thrown_ = made;
        if (!unwind_to_handler()) { raise("uncaught " + describe_thrown(thrown_)); }
    }

    // Call a JS function FROM C++. This is what an event listener, a timer and
    // a requestAnimationFrame callback all need, and without it script can only
    // ever be entered at the top.
    //
    // Re-entrant: it runs a nested interpreter loop on the existing register
    // stack rather than resetting it, so a listener may itself call back into
    // script.
    value call(value callable, std::span<const value> args, value this_value = value::undefined());
    // Whether a fault is outstanding, and the message.
    //
    // `run` clears these on entry and reports them in its result; `call` has no
    // result to report through, so a host that drives callbacks - a timer, an
    // animation frame, an event - must ask. Without this a fault in the FIRST
    // rAF callback set `failed_` for good: every later callback was refused and
    // the page simply stopped, with nothing anywhere saying why.
    // Queue a job for the end of the turn. FIFO, and a job queued BY a job runs
    // in the same drain - that is what makes a promise chain complete before
    // the turn ends rather than one link per turn.
    void queue_microtask(value fn, std::vector<value> args = {}) {
        if (fn.is_callable()) { microtasks_.push_back(microtask{fn, std::move(args)}); }
    }
    // Run the queue to exhaustion. Called after the top-level script and from
    // the event loop - after timers, before animation frames, and again after
    // each event dispatch, which is where a browser puts its checkpoints.
    void drain_microtasks() {
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
    [[nodiscard]] std::size_t pending_microtasks() const noexcept { return microtasks_.size(); }

    [[nodiscard]] bool failed() const noexcept { return failed_; }
    [[nodiscard]] const std::string & error() const noexcept { return error_; }
    // Report it and carry on, which is what a browser does: an exception in one
    // callback does not cancel the next one or end the page.
    [[nodiscard]] std::string take_error() {
        std::string out = std::move(error_);
        error_.clear();
        failed_ = false;
        return out;
    }

    // `new callee(...args)` where the argument count is only known at run time.
    // op::construct keeps its own inline path because it does not need a nested
    // interpreter loop; this is for the spread form and for `Reflect.construct`,
    // which both do.
    [[nodiscard]] value construct(value callee, std::span<const value> args);

    // --- conversions (ECMA-262 shaped, and shared with the bindings) -------
    [[nodiscard]] static bool truthy(value v);
    // ECMA-262 ToInt32 / ToUint32: NaN and the infinities are 0, everything
    // else truncates toward zero and wraps modulo 2^32.
    [[nodiscard]] static std::int32_t to_int32(value v) {
        return static_cast<std::int32_t>(to_uint32(v));
    }
    [[nodiscard]] static std::uint32_t to_uint32(value v) {
        const double n = to_number(v);
        if (!std::isfinite(n)) { return 0; }
        const double truncated = std::trunc(n);
        return static_cast<std::uint32_t>(
            static_cast<std::int64_t>(std::fmod(truncated, 4294967296.0)));
    }
    [[nodiscard]] static double to_number(value v);
    [[nodiscard]] std::string to_string(value v);
    [[nodiscard]] static std::string_view type_of(value v);
    [[nodiscard]] static bool loose_equals(value a, value b);

    // --- prototypes ---------------------------------------------------------
    //
    // `"abc".split(...)` and `[1,2].push(...)` resolve to nothing without these:
    // a string is not an object_object, so there is nowhere on it to put a
    // method. Rather than special-case every builtin inside the interpreter,
    // each VALUE KIND gets a prototype object, and property lookup falls back to
    // it. Adding a method is then putting a native in a table - which is what
    // makes a standard library mechanical instead of 2000 lines of switch.
    //
    // Not a full prototype CHAIN: there is one level, and no user-visible
    // `__proto__` or `Object.create`. That is a real limitation, and it covers
    // everything a page does with builtins.
    enum class proto_kind : std::uint8_t {
        object,
        array,
        string,
        number,
        boolean,
        regexp,
        symbol,
        map,
        set,
        error,
        function,
        typed_array,
        count_
    };

    void set_prototype(proto_kind kind, object_object * table) {
        prototypes_[static_cast<std::size_t>(kind)] = table;
    }
    [[nodiscard]] object_object * prototype(proto_kind kind) const {
        return prototypes_[static_cast<std::size_t>(kind)];
    }

    // How an `async` function's return value becomes a promise. The VM cannot
    // build one itself - a promise is an ordinary object carrying then/catch/
    // finally natives, and those live in the standard library - so builtins
    // installs this hook. Without it (a VM with no builtins) an async function
    // returns its plain value, which `await` still handles.
    void set_pending_promise_factory(std::function<value(context &)> make) {
        pending_promise_factory_ = std::move(make);
    }
    void set_promise_settler(std::function<void(context &, value, value, bool)> settle) {
        promise_settler_ = std::move(settle);
    }
    void set_promise_factory(std::function<value(context &, value, bool)> make) {
        promise_factory_ = std::move(make);
    }
    [[nodiscard]] value make_promise(value v, bool rejected) {
        return promise_factory_ ? promise_factory_(*this, v, rejected) : v;
    }

    // One property lookup, shared by get_prop, get_index-with-a-string-key and
    // call_method. Three copies of this is three chances for `a.length` and
    // `a["length"]` to disagree.
    // NOT const: an accessor on the chain is called, and that re-enters the VM.
    [[nodiscard]] value lookup_property(value target, const std::string & name);
    // Assign through the chain, honouring a setter. Returns false when nothing
    // took the write, so the caller can fall back to defining an own property.
    bool assign_through_accessor(value target, const std::string & name, value v);
    // `target.name = v`, the whole write path: a proxy's `set` trap, then a
    // setter on the prototype chain, then an own data property.
    //
    // One function because set_prop and set_index are the same operation with
    // the key arriving differently, and the two copies had already drifted -
    // only one of them consulted a proxy. `element.style.width = "10px"` goes
    // through a proxy and it can be written either way.
    void store_property(value target, const std::string & name, value v);
    // `target[key]` for an arbitrary key value. Numeric keys index an array or
    // a string; anything else is a named lookup. Shared by get_index and by
    // computed method calls, because `a[0]()` and `a['push']()` must both work
    // and they take different branches.
    [[nodiscard]] value lookup_index(value target, value key);

    // --- gc ----------------------------------------------------------------
    std::size_t collect();

    // ROOTS THE VM CANNOT SEE. The DOM bindings hold every event listener,
    // every timer callback and every element wrapper in C++ containers, and
    // nothing in the register file, the globals table or a call frame refers to
    // them. A collection without this frees a page's listeners while the page
    // is still using them - which is why collection never ran at all.
    using root_visitor = std::function<void(value)>;
    void set_external_roots(std::function<void(const root_visitor &)> enumerate) {
        external_roots_ = std::move(enumerate);
    }

    // Collect if the heap has grown enough to be worth it. Called once per
    // tick, so a long-running page's garbage is bounded instead of accumulating
    // for the life of the document.
    std::size_t collect_if_due() {
        if (live_objects_ < collect_threshold_) { return 0; }
        const std::size_t freed = collect();
        // The next collection waits for the heap to grow again, so a page whose
        // live set is genuinely large does not collect on every tick.
        collect_threshold_ = std::max(minimum_collect_threshold, live_objects_ * 2);
        return freed;
    }
    [[nodiscard]] std::size_t live_objects() const noexcept { return live_objects_; }

    // WHERE A THROW LANDS. One entry per open `try`, so unwinding can pop back
    // to the frame that installed it - a handler in a caller must not be caught
    // by a callee.
    struct handler {
        std::size_t frame = 0;   // index into frames_
        std::size_t address = 0; // the catch block
        std::size_t reg_top = 0; // registers_ size on entry
        std::uint16_t slot = 0;  // where to put the thrown value
    };

    // A SUSPENDED FRAME, saved whole.
    //
    // `await` on a pending promise cannot block - there is one stack and the
    // event loop is above it - so the frame is lifted out of the register stack
    // and put here, the caller is handed a promise, and the frame goes back when
    // the awaited promise settles.
    //
    // Only the TOP frame can suspend, which is sufficient: `registers_` is one
    // flat vector indexed by base, so lifting a frame out from the middle would
    // move every frame above it. And there is never a reason to - every frame
    // below is either already suspended or a synchronous caller that must
    // itself unwind before it can wait for anything.
    //
    // The register window is COPIED rather than referenced. It has to be: the
    // stack is truncated the moment the frame leaves, and whatever runs next
    // reuses those slots.
    struct coroutine_object final : heap_object {
        const function_proto * proto = nullptr;
        std::size_t ip = 0;
        // Where the awaited value lands when the frame comes back - the
        // destination register of the `await` that suspended it.
        std::uint16_t await_reg = 0;
        std::uint16_t argc = 0;
        closure_object * closure = nullptr;
        value receiver = value::undefined();
        bool constructing = false;
        std::vector<value> window;
        // This frame's own handlers, with `reg_top` made RELATIVE to the frame's
        // base: the frame comes back at a different place in the register stack,
        // and an absolute mark would point into whatever is there now.
        std::vector<handler> handlers;
        // The promise the caller was given, settled when the body finally
        // returns. One per suspended function however many times it awaits.
        value promise;
        coroutine_object() : heap_object(heap_kind::coroutine) {}
    };

    // Put a suspended frame back and run it. `with` is what the await
    // evaluates to; `rejected` throws it at the await instead.
    void resume(value coroutine, value with, bool rejected);
    // Whether this value is a promise that has NOT settled - the one case
    // `await` cannot answer by reading. The shape is the standard library's:
    // `__settled` present and false.
    [[nodiscard]] static bool is_pending_promise(value v) {
        if (!v.is_object()) { return false; }
        value * settled = static_cast<object_object *>(v.as_heap())->find("__settled");
        return settled != nullptr && !truthy(*settled);
    }
    // Ask a pending promise to put this coroutine back when it settles. The
    // record goes on the promise's own handler list, so a resumption is queued
    // and ordered exactly like a `then` - because that is what it is.
    void attach_resume(value promise, value coroutine) {
        if (!promise.is_object()) { return; }
        value * handlers = static_cast<object_object *>(promise.as_heap())->find("__handlers");
        if (handlers == nullptr || !handlers->is_array()) { return; }
        auto * record = allocate<object_object>();
        record->set("co", coroutine);
        static_cast<array_object *>(handlers->as_heap())->items.push_back(value::object(record));
    }

private:
    struct call_frame {
        const function_proto * proto = nullptr;
        std::size_t ip = 0;
        std::size_t base = 0; // index into registers_ of this frame's r0
        std::uint16_t result_reg = 0;
        // How many arguments ACTUALLY arrived, which is not param_count: a rest
        // parameter binds the ones past the declared list, and nothing else in
        // the frame records that they were passed.
        std::uint16_t argc = 0;
        closure_object * closure = nullptr; // whose upvalues this body sees
        // The receiver. A JS body reads it through `this`; before this existed
        // `this` compiled to undefined unconditionally, so no method could see
        // the object it was called on.
        value receiver = value::undefined();
        // How many exception handlers this frame had on entry. Unwinding pops
        // back to it, so a handler in a caller cannot be caught by a callee.
        std::size_t handler_base = 0;

        // `new C()` evaluates to the new object, NOT to whatever the
        // constructor body happens to return - unless it returns an object,
        // which is the one case the spec lets override it.
        bool constructing = false;

        // The promise this frame's eventual return settles, once it has
        // suspended at least once. Undefined on a frame that has not - a
        // function that never awaits anything pending returns normally and
        // needs no promise of its own beyond the one `wrap_promise` makes.
        value async_promise = value::undefined();
    };

    // The `this` a frame actually sees. For an ordinary function that is its
    // own receiver; for an arrow it is the one captured where the arrow was
    // written, because an arrow never gets a receiver of its own. Both
    // `load_this` and the closure builder go through here, which is what makes
    // an arrow nested inside an arrow inside a method still resolve correctly.
    [[nodiscard]] static value effective_this(const call_frame & f) {
        if (f.closure != nullptr && f.closure->proto != nullptr && f.closure->proto->is_arrow) {
            return f.closure->captured_this;
        }
        return f.receiver;
    }

    // A live try block: where to jump, and where the state was when it started.

    // Run the `__fields` initialiser of `constructor` and of every class it
    // extends, BASE FIRST, against a freshly made instance. The chain is walked
    // here rather than threaded through the compiler because the compiler does
    // not know what `extends` will evaluate to.
    void run_field_initialisers(value constructor, value self);

    // The fresh object `new` builds, with its prototype taken from the
    // constructor's own `prototype` property - which is what makes a method
    // defined on the class reachable from every instance.
    [[nodiscard]] static std::string callee_origin(const function_proto & fn, std::size_t ip,
                                                   std::uint16_t reg_index);
    [[nodiscard]] std::string describe_callee(const function_proto & fn, std::string_view name,
                                              value callee);
    // The handler's trap of this name, or undefined when it has none.
    [[nodiscard]] value proxy_trap(value proxy, const std::string & name);
    [[nodiscard]] std::string describe_thrown(value thrown);
    // A function's `prototype`, made on first use. See the definition.
    [[nodiscard]] value ensure_prototype(value fn);
    [[nodiscard]] value make_instance(value callee);

    [[nodiscard]] value execute(const program & prog, const function_proto & entry);
    [[nodiscard]] value run_loop(std::size_t stop_depth);
    // A FAILURE COMES WITH THE STACK IT HAPPENED ON.
    //
    // "the value is undefined, not a function" is a fact about one instruction;
    // which functions were running when it happened is what says where to look.
    // Costs nothing until something fails, and in a 4.5 MB bundle it is the
    // difference between a diagnostic and a shrug.
    void raise(std::string message) {
        if (failed_) { return; }
        failed_ = true;
        error_ = std::move(message);
        std::string trace;
        int shown = 0;
        for (std::size_t i = frames_.size(); i-- > 0 && shown < 12; ++shown) {
            const function_proto * fp = frames_[i].proto;
            if (fp == nullptr) { continue; }
            // The function's INDEX as well as its name: most of a bundle's
            // functions are anonymous, and the index is what lets a
            // disassembler be pointed straight at the failing instruction.
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
            trace += "\n        at " + (fp->name.empty() ? std::string{"<anonymous>"} : fp->name) +
                     " (fn#" + std::to_string(which) + " +" + std::to_string(frames_[i].ip) + ")";
        }
        if (frames_.size() > 12) {
            trace += "\n        ... " + std::to_string(frames_.size() - 12) + " more";
        }
        error_ += trace;
    }

    // Find the innermost live handler and jump to it, discarding every call
    // frame between here and the one that owns it. Returning false means
    // nothing caught it, which is an uncaught exception.
    //
    // This is why exceptions are a VM change and not a compiler one: a `throw`
    // several frames deep has to reach a `try` in a caller, and only the VM
    // knows where those frames are.
    [[nodiscard]] bool unwind_to_handler() {
        while (!handlers_.empty()) {
            const handler h = handlers_.back();
            handlers_.pop_back();
            if (h.frame >= frames_.size()) { continue; } // its frame already returned
            frames_.resize(h.frame + 1);
            call_frame & target = frames_.back();
            target.ip = h.address;
            if (registers_.size() < h.reg_top) { registers_.resize(h.reg_top, value::undefined()); }
            registers_[target.base + h.slot] = thrown_;
            thrown_ = value::undefined();
            return true;
        }
        return false;
    }

    void mark(value v);
    void mark_object(heap_object * o);
    void sweep_all();

    // Set while a native runs, so it can see its receiver.
    value current_this_ = value::undefined();
    // The program being executed, so a call from C++ can find the string
    // tables a nested frame needs.
    const program * program_ = nullptr;
    // Keyed by the FUNCTION rather than by its index in the current program.
    // An index is only meaningful within one program, and a context can run
    // more than one: a devtools-style eval calls a function the page defined,
    // so the running frame's proto belongs to a DIFFERENT program from the one
    // being executed. Subtracting its address from the wrong program's
    // functions vector gave a garbage index and read off the end of the cache.
    flat_map<const function_proto *, flat_map<std::uint32_t, value>> string_cache_;
    // Live try blocks, innermost last. Not per-frame, because a throw has to be
    // able to find a handler several frames up.
    std::vector<handler> handlers_;
    std::array<object_object *, static_cast<std::size_t>(proto_kind::count_)> prototypes_{};
    std::function<value(context &, value, bool)> promise_factory_;
    // Making a PENDING promise and settling one. The VM can read a promise's
    // state - `await` already did - but creating and settling run the standard
    // library's own logic, including queueing the handlers. Two hooks rather
    // than reaching into the object's properties, so there is one definition of
    // what settling means.
    std::function<value(context &)> pending_promise_factory_;
    std::function<void(context &, value, value, bool)> promise_settler_;
    // Set by a frame that suspended, so `resume` can tell "awaited again" from
    // "returned" - both leave run_loop the same way.
    bool suspended_ = false;
    // The MICROTASK QUEUE. A promise handler runs at the end of the turn, not
    // the moment the promise settles.
    //
    // The difference is observable and pages are written against it:
    // `p.then(f); after();` must run `after` FIRST. Running the handler on
    // settle also lets a chain reenter code that is halfway through its own
    // work, which is the class of bug the queue exists to prevent.
    //
    // A job is a callable plus its arguments, held as values so the collector
    // traces them - a std::function capturing a value would be a root nothing
    // knows about, which is how a queued handler's argument gets freed before
    // it runs.
    struct microtask {
        value fn;
        std::vector<value> args;
    };
    std::deque<microtask> microtasks_;
    value thrown_ = value::undefined();

    flat_map<std::string, value> globals_;
    std::vector<value> registers_;
    std::vector<call_frame> frames_;
    heap_object * heap_ = nullptr;
    std::size_t live_objects_ = 0;
    // TOTAL allocations, never reset: the cap is about a loop that does not
    // terminate, and a collector that keeps the live set small hides exactly
    // that if the count is reset.
    std::size_t allocations_ = 0;
    static constexpr std::size_t minimum_collect_threshold = 4096;
    std::size_t collect_threshold_ = minimum_collect_threshold;
    std::function<void(const root_visitor &)> external_roots_;
    bool failed_ = false;
    std::string error_;
};

// ===================== conversions ======================================

} // namespace ctbrowser::script
