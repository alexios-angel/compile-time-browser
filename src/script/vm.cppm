module;
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

export module ctbrowser.script:vm;

import ctbrowser.core;

import :bytecode;
import :value;

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

export namespace ctbrowser::script {

struct closure_object;

using native_fn = std::function<value(class context &, std::span<value>)>;

struct native_object final : heap_object {
    std::string name;
    native_fn fn;
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

    const function_proto * proto = nullptr;
    std::vector<value> upvalues; // each one is a cell_object
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
    template <typename T, typename... Args> [[nodiscard]] T * allocate(Args &&... args) {
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

    // Call a JS function FROM C++. This is what an event listener, a timer and
    // a requestAnimationFrame callback all need, and without it script can only
    // ever be entered at the top.
    //
    // Re-entrant: it runs a nested interpreter loop on the existing register
    // stack rather than resetting it, so a listener may itself call back into
    // script.
    value call(value callable, std::span<const value> args, value this_value = value::undefined());

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
    void set_promise_factory(std::function<value(context &, value, bool)> make) {
        promise_factory_ = std::move(make);
    }
    [[nodiscard]] value make_promise(value v, bool rejected) {
        return promise_factory_ ? promise_factory_(*this, v, rejected) : v;
    }

    // One property lookup, shared by get_prop, get_index-with-a-string-key and
    // call_method. Three copies of this is three chances for `a.length` and
    // `a["length"]` to disagree.
    [[nodiscard]] value lookup_property(value target, const std::string & name) const;
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

private:
    struct call_frame {
        const function_proto * proto = nullptr;
        std::size_t ip = 0;
        std::size_t base = 0; // index into registers_ of this frame's r0
        std::uint8_t result_reg = 0;
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
    };

    // A live try block: where to jump, and where the state was when it started.
    struct handler {
        std::size_t frame = 0;   // index into frames_
        std::size_t address = 0; // the catch block
        std::size_t reg_top = 0; // registers_ size on entry
        std::uint8_t slot = 0;   // where to put the thrown value
    };

    [[nodiscard]] value execute(const program & prog, const function_proto & entry);
    [[nodiscard]] value run_loop(std::size_t stop_depth);
    void raise(std::string message) {
        if (!failed_) {
            failed_ = true;
            error_ = std::move(message);
        }
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
    flat_map<const function_proto *, flat_map<std::uint16_t, value>> string_cache_;
    // Live try blocks, innermost last. Not per-frame, because a throw has to be
    // able to find a handler several frames up.
    std::vector<handler> handlers_;
    std::array<object_object *, static_cast<std::size_t>(proto_kind::count_)> prototypes_{};
    std::function<value(context &, value, bool)> promise_factory_;
    value thrown_ = value::undefined();

    flat_map<std::string, value> globals_;
    std::vector<value> registers_;
    std::vector<call_frame> frames_;
    heap_object * heap_ = nullptr;
    std::size_t live_objects_ = 0;
    static constexpr std::size_t minimum_collect_threshold = 4096;
    std::size_t collect_threshold_ = minimum_collect_threshold;
    std::function<void(const root_visitor &)> external_roots_;
    bool failed_ = false;
    std::string error_;
};

// ===================== conversions ======================================

} // namespace ctbrowser::script
