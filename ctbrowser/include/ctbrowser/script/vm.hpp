#pragma once
#include <array>
#include <charconv>
#include <cmath>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <ctbrowser/core/core.hpp>

#include <ctbrowser/script/bytecode.hpp>
#include <ctbrowser/script/dispatch.hpp>
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

// THE LINEAR SCAN ON `native_object` AND `closure_object` BELOW IS DELIBERATE,
// AND IT WAS MEASURED (2026-08-08).
//
// It looks exactly like the bug this engine has found three times - a linear
// scan comparing std::strings, once per property MENTION - and a class compiles
// to a `closure_object`, so every static, every `C.prototype` and every Babel
// `_inherits` hop comes through here. Babylon is 181,222 lines of class-heavy
// code and was the corpus expected to show it.
//
// It does not. Callgrind, `babylon_ratchet` at 46.577 G instructions:
//
//   closure_object::find    64,466,020   0.14%
//   native_object::find      2,666,456   0.01%
//
// 0.15% together, and 0.04% of a `phaser_invaders` frame. A perfect fix cannot
// win more than that, and the fix that was tried LOST: folding both into one
// table with a `string_flat_map` index built past a threshold took the pair
// from 5.14 M to 9.54 M instructions - nearly 2x - because the branch selecting
// scan-or-index touches the map's header before the scan, and because two
// bodies the compiler had been inlining into their callers became one
// out-of-line call. `phaser_invaders` went 13.646 G -> 13.699 G, +0.39%.
//
// The reason the scan is already right: `ensure_prototype` is lazy on the
// stated grounds that "a program allocates far more functions than it
// constructs", so MOST CLOSURES HOLD NOTHING AT ALL and the loop exits on an
// empty vector. Do not re-propose a hash table here without a corpus that
// puts these two functions somewhere near the top of a profile.
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
    //
    // Linear on purpose - see the note above `native_object`.
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

// ONE MODULE, AS THE RUNTIME SEES IT.
//
// `exports` maps an exported name to the CELL holding it, not to a value. That
// is what makes an imported binding live: the importer is handed the same box,
// so a later write by the exporter is a write the importer reads. Handing over
// the value instead passes a test that two modules can see each other and fails
// the one that matters, which is why docs/plans/modules.md names it as the
// shortcut to refuse.
struct module_record {
    std::string specifier;
    const program * compiled = nullptr;
    flat_map<std::string, value> exports;
    // THE SPECIFIER AS WRITTEN -> THE ONE THE REGISTRY IS KEYED BY. `./dep.js`
    // means a different file depending on WHICH module wrote it, and the
    // bytecode can only carry what was written. Resolving is the loader's job -
    // it is the half that knows about paths, and eventually about URLs and
    // import maps - so it leaves the answer here and `op::load_import` looks it
    // up rather than doing any path arithmetic of its own.
    flat_map<std::string, std::string> resolved;
    // ONE PER MODULE, made on demand: two `import * as` of the same module must
    // give the same object. See context::module_namespace.
    value namespace_object = value::undefined();
    // Evaluated ONCE, however many modules import it. The flag is the whole of
    // "a module is a singleton".
    bool evaluated = false;
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
        const auto it = globals_.find(name);
        return it == globals_.end() ? value::undefined() : it->second;
    }
    // Whether the name is DEFINED, which is not whether it is truthy or even
    // defined-and-undefined: `window.foo` on a global explicitly set to
    // undefined must still report the global as present, or `'foo' in window`
    // and a window that proxies to the globals disagree with `typeof foo`.
    [[nodiscard]] bool has_global(std::string_view name) const {
        return globals_.find(name) != globals_.end();
    }
    // Every global, for a window that enumerates itself.
    [[nodiscard]] const string_flat_map<value> & globals() const noexcept { return globals_; }

    // --- execution ---------------------------------------------------------
    //
    // THE PROGRAM MUST OUTLIVE THE CONTEXT. Closures hold `const function_proto *`
    // into it, and anything that calls back in later - a timer, an event
    // listener, requestAnimationFrame - dereferences them long after run()
    // returned. Passing a temporary works exactly until the first callback.
    run_result run(const program & prog);

    // --- ES modules --------------------------------------------------------
    // Run `prog` AS a module: its exports are published into `into`, and its
    // imports are resolved through the registry the loader filled in.
    // BEFORE ANY OF THE GRAPH RUNS: creates this module's export cells without
    // evaluating it, so a cyclic importer finds an empty binding rather than a
    // missing one. See the definition.
    void instantiate_module(const program & prog, module_record & into);
    // THE NAMESPACE OBJECT for a module, live and cached. See the definition.
    [[nodiscard]] value module_namespace(module_record & of);
    // WHAT `import(specifier)` CALLS. The VM knows nothing about paths, URLs or
    // fetching, so a dynamic import hands the specifier and the module that
    // wrote it to the embedder and expects a promise back.
    void set_module_loader(
        std::function<value(context &, const std::string &, const std::string &)> loader) {
        module_loader_ = std::move(loader);
    }
    run_result run_module(const program & prog, module_record & into);
    // Where a module is found by specifier. The loader owns the graph; the VM
    // only needs to look a name up when `load_import` runs.
    [[nodiscard]] flat_map<std::string, module_record> & modules() noexcept { return modules_; }

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
    // Compile a program and KEEP it, returning a reference that stays valid
    // for the life of the context. The compiler is passed in by the caller
    // because the VM does not depend on it - `compile.hpp` includes `vm.hpp`,
    // not the other way round.
    // Run a program NESTED inside another - `new Function(...)` evaluating its
    // own body while a script is halfway through a statement.
    //
    // Not `run()`, which clears the failure flag and drains the microtask
    // queue - both belong to the TURN rather than to the program, and draining
    // here would run a page's pending promise handlers in the middle of the
    // statement that happened to build a function.
    //
    // And not `execute()`, which is the TOP-LEVEL entry: it clears `frames_`
    // and `registers_` and points `program_` at its argument. Called nested,
    // that throws away the stack of whoever was running and leaves the outer
    // program's top-level frame - which has no closure, so it falls back to
    // `program_` - reading a function table that is not its own.
    //
    // A closure over the program's entry function, called normally. Its `owner`
    // is what makes every `op::closure` inside it index the right table, which
    // is the whole reason that field exists.
    [[nodiscard]] value run_nested(const program & prog) {
        if (!prog.ok || prog.functions.empty()) { return value::undefined(); }
        auto * entry = allocate<closure_object>(&prog.functions[0]);
        entry->owner = &prog;
        return call(value::object(entry), std::span<const value>{});
    }

    const program & own_program(program compiled) {
        owned_programs_.push_back(std::make_unique<program>(std::move(compiled)));
        return *owned_programs_.back();
    }

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
    // AN ERROR OBJECT WITHOUT THROWING IT. A rejected promise carries one and
    // is not a throw, so building and throwing had to come apart.
    [[nodiscard]] value make_error(std::string_view kind, std::string message) {
        value made = make_object();
        auto * o = static_cast<object_object *>(made.as_heap());
        o->set("name", string(std::string{kind}));
        o->set("message", string(message));
        // The frames it happened on, exactly as a constructed Error gets them -
        // a page catching a TypeError the VM raised should be able to report
        // where as easily as one it threw itself.
        o->set("stack", string(std::string{kind} + ": " + message + current_stack()));
        // On the Error prototype, so `e instanceof Error` and `e.toString()`
        // work on a thrown one exactly as on `new TypeError(...)`.
        if (object_object * table = prototype(proto_kind::error)) {
            o->prototype = value::object(table);
        }
        return made;
    }

    void throw_error(std::string_view kind, std::string message) {
        thrown_ = make_error(kind, std::move(message));
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

    // THE STACK AS IT IS RIGHT NOW.
    //
    // "the value is undefined, not a function" is a fact about one instruction;
    // which functions were running when it happened is what says where to look.
    // In a 4.5 MB bundle that is the difference between a diagnostic and a
    // shrug.
    //
    // Split out of raise() because a CONSTRUCTED error wants it too: `new
    // Error()` had an empty `stack`, so a library that reports where it went
    // wrong - p5's Friendly Error System does, and so does any page catching an
    // exception - had nothing to report. Costs nothing until something asks.
    //
    // The function's INDEX as well as its name: most of a bundle's functions
    // are anonymous, and the index is what lets `p5-ratchet.py --source N` be
    // pointed straight at the failing code.
    [[nodiscard]] std::string current_stack(std::size_t skip = 0) const {
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
            // A COMPILED FRAME HAS NO BYTECODE OFFSET, so it does not get one.
            // Phase 6 asks for "a single coherent JavaScript stack trace" over a
            // mixed stack, and printing "+0" for a native body is not coherent -
            // it is a number that looks like a position and is not one. Worse,
            // `ip` on a compiled frame is where the unwinder puts the LANDING
            // PAD, with CT_AOT_PAD_BIT set, so a trace captured between a catch
            // firing and ct_aot_catch_land reading it would print
            // 9223372036854775815 as an offset.
            const bool compiled = fp->aot_entry != nullptr;
            trace += "\n        at " + (fp->name.empty() ? std::string{"<anonymous>"} : fp->name) +
                     " (fn#" + std::to_string(which) +
                     (compiled ? std::string{", compiled)"}
                               : " +" + std::to_string(frames_[i].ip) + ")");
        }
        if (depth > 12) { trace += "\n        ... " + std::to_string(depth - 12) + " more"; }
        return trace;
    }

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
    // WHATEVER A PAGE CAN ITERATE, AS AN ARRAY OF VALUES.
    //
    // for-of, spread and Array.from all need the same answer, and they used to
    // each assume an array. A Map and a Set have no `length`, so `for (const x of
    // set)` ran zero times and `[...new Set(v)]` was empty - silently, which cost
    // every colour string in p5.js: its colour-space registry is
    // `[...new Set(Object.values(registry))]`, so nothing was ever registered and
    // `color('#ff0000')` threw "Invalid color string".
    //
    // NOT the real iterator protocol - there is no Symbol.iterator dispatch - but
    // it covers arrays, strings, Maps, Sets and the key/value/entry views they
    // hand out, which is what a page iterates. An object with none of those
    // yields nothing, as before, and that limit is written down in docs/script.md.
    [[nodiscard]] value iterable_values(value v);

    [[nodiscard]] value construct(value callee, std::span<const value> args);

    // --- conversions (ECMA-262 shaped, and shared with the bindings) -------
    [[nodiscard]] static bool truthy(value v);

    // The handler's trap of this name, or undefined when it has none. Public
    // for the standard library: `hasOwnProperty` must ask a proxy's handler the
    // same question `in` asks it, and `window` is a proxy.
    [[nodiscard]] value proxy_trap(value proxy, const std::string & name);
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
    // Number::exponentiate (6.1.6.1.3), which is NOT C's `pow`. C99 F.10.4.4
    // defines pow(+-1, y) as 1 for EVERY y - including NaN and both infinities -
    // where the specification requires NaN for exactly those. Shared by
    // `Math.pow` and the `**` opcode, which had the same bug in two places.
    [[nodiscard]] static double exponentiate(double base, double exponent);
    [[nodiscard]] std::string to_string(value v);
    [[nodiscard]] static std::string_view type_of(value v);
    // ToNumber, 7.1.4, INCLUDING the object case - an object coerces through
    // its own valueOf and then toString, which the static `to_number` cannot do
    // because it cannot call back into the VM. PUBLIC because that is the one
    // every built-in whose spec text reads `? ToNumber(x)` must use: `Number([])`
    // is 0 and `Math.abs([])` is 0 precisely because of it, and reaching for the
    // static form instead is what made both NaN.
    [[nodiscard]] double to_number_value(value v);
    // IsLooselyEqual, 7.2.15. NOT static: an object compared against a
    // primitive has to go through ToPrimitive, which re-enters the VM.
    [[nodiscard]] bool loose_equals(value a, value b);
    // Abstract Relational Comparison, 7.2.13 - the ONE comparison that `<`,
    // `>`, `<=` and `>=` each ask a different question of.
    //
    // It is not a numeric comparison. Two STRINGS compare as text, and coercing
    // them to numbers instead makes `"a" < "b"` false - along with `>`, `<=`
    // and `>=`, because ToNumber("a") is NaN and every comparison against NaN
    // is false. `unordered` is the specification's `undefined` result, which is
    // what makes all four operators false on a NaN.
    [[nodiscard]] std::partial_ordering compare_relational(value a, value b);

    // BIGINT ARITHMETIC, and the rule that makes it safe.
    //
    // A BigInt and a Number CANNOT be mixed in arithmetic - `1n + 1` is a
    // TypeError - and that is the feature rather than an omission: an engine
    // that quietly coerced would round exactly where the type exists to stay
    // exact. So the dispatch is: both bigint, do it; one bigint and one
    // anything-else, throw.
    //
    // Returns true when it HANDLED the operation (including by throwing), so
    // the caller falls through to the Number path only when neither side is a
    // bigint. `kind` is the opcode being executed.
    [[nodiscard]] bool bigint_binary(op kind, value a, value b, value & out);

    // THE SEVEN NON-RE-ENTERING BINARY OPERATIONS, in one place. Phase 5.
    //
    // add, bit_and, bit_or, bit_xor, shl, shr and ushr. They are together
    // because they are the same function: try the BigInt arm, and otherwise
    // apply a STATIC conversion - to_number for add, to_int32/to_uint32 for the
    // six bitwise - which is what makes them non-re-entering. None of them can
    // run a user valueOf or toString, so a backend that has proven both
    // operands are Numbers may drop the call, the exception edge AND the
    // safepoint.
    //
    // `add` BELONGS HERE AND NOT WITH THE RE-ENTERING FAMILY, which looks wrong
    // and is not: compile_binary maps source `+` to add_generic, and op::add
    // comes only from `++` and three internal counters. So `x++` on
    // {valueOf: () => 3} is NaN and never runs user code. That is a deviation
    // from the specification rather than an optimisation, and aot_helpers.def
    // records it as one - if it is ever fixed, these seven move.
    //
    // Extracted rather than duplicated because the AOT backend must run the
    // SAME semantics as the interpreter, not a second implementation of them:
    // ct_aot_binary_op_static calls exactly this.
    [[nodiscard]] value binary_op_static(op kind, value lhs, value rhs);

    // AND THE SEVEN THAT CAN RUN PAGE JAVASCRIPT. Phase 5.
    //
    // sub, mul, div, mod, pow, add_generic and concat. They are the same seven
    // shapes as `binary_op_static` above except that their conversions are the
    // RE-ENTERING ones - to_primitive, to_number_value, to_string - each of
    // which runs a user `valueOf` or `toString` when handed an object. So every
    // caller must have its live values reachable from a root for the duration,
    // which is what is_safepoint means on these rows.
    //
    // THE THREE `+` OPCODES STAY THREE, and that is the trap in this
    // extraction. `add` is the static-family immediate above; `add_generic`
    // makes both sides primitive and then lets the operands decide, with the
    // string test taking first refusal so `1n + "a"` concatenates while
    // `1n + 1` is a TypeError; `concat` unconditionally ToStrings both and is
    // emitted only by template literals. Routing concat through bigint_binary
    // would send `${1n}` to a switch that has no case for it and throw "BigInts
    // have no unsigned right shift".
    [[nodiscard]] value binary_op(op kind, value lhs, value rhs);

    // UNARY MINUS, AND ITS TWO ARMS ARE NOT ONE OPERATION.
    //
    // It is NOT to_number_value plus a negation, which is the lowering the
    // shape invites: a BigInt operand is negated as an unbounded integer and
    // ALLOCATES, so `-0n` is `0n` - a BigInt has one zero - and the result may
    // be an unrooted heap value. That is why this answers with a `value` where
    // ct_aot_to_number answers with a double, and it is the whole reason the
    // ABI gives the two separate rows.
    //
    // THE BIGINT TEST COMES FIRST AND THAT ORDERING IS LOAD-BEARING: it is what
    // makes to_number_value's own "Cannot convert a BigInt value to a number"
    // TypeError unreachable from `-x`. `+1n` throws; `-1n` does not.
    [[nodiscard]] value negate_value(value v);

    // BITWISE NOT, and the same split for a different reason.
    //
    // `~1n` is `-2n` on the unbounded two's-complement value: there is no
    // ToInt32 step, because a BigInt has no width to truncate to. The Number
    // arm does have one, so the two arms genuinely differ rather than one being
    // the other's fast path.
    [[nodiscard]] value bit_not_value(value v);

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
        bigint,
        map,
        set,
        error,
        function,
        typed_array,
        // A promise's then/catch/finally used to be three natives PER PROMISE.
        // On a prototype they are three for the whole program, and `p instanceof
        // Promise` - which was false - is a pointer compare.
        promise,
        // `.next` / `.throw` / `.return` for every generator object, and
        // Symbol.iterator so `for (x of gen())` works. On a prototype for the
        // same reason promise's are: three natives for the program rather than
        // three per generator, and `gen()` objects compare alike.
        generator,
        count_
    };

    // THE IMPLICIT PROTOTYPES FOR A VALUE'S KIND, most derived first.
    //
    // Property lookup falls back to these tables (see lookup_property), and
    // anything else that asks "what is this value's prototype chain" has to see
    // the SAME ones or it disagrees with `.` for no visible reason.
    //
    // `instanceof` did disagree. It walked only the explicit `prototype` field,
    // which a builtin does not have - so `f instanceof Function`, `[] instanceof
    // Array` and `({}) instanceof Object` were all FALSE while `new B()
    // instanceof A` was true for a page's own classes. p5.js type-tests with
    // `val.array instanceof Function` before serialising a vector and threw
    // "Can't convert vector[2, 20, 0] to array!" on a perfectly good vector.
    //
    // Three entries because that is the deepest chain there is here - a typed
    // array is TypedArray.prototype, then Array.prototype, then
    // Object.prototype - and nullptr pads the rest.
    [[nodiscard]] std::array<object_object *, 3> implicit_prototypes(value v) const {
        const auto table = [this](proto_kind kind) { return prototype(kind); };
        if (v.is_array()) {
            auto * arr = static_cast<array_object *>(v.as_heap());
            if (arr->elements != element_kind::none) {
                return {table(proto_kind::typed_array), table(proto_kind::array),
                        table(proto_kind::object)};
            }
            return {table(proto_kind::array), table(proto_kind::object), nullptr};
        }
        if (v.is_string()) {
            return {table(proto_kind::string), table(proto_kind::object), nullptr};
        }
        if (v.is_number()) {
            return {table(proto_kind::number), table(proto_kind::object), nullptr};
        }
        // So `(1n).toString(16)` finds a method at all - a bigint is a
        // primitive, so it has no own properties and the prototype is the only
        // place a method can live.
        if (v.is_kind(heap_kind::bigint)) {
            return {table(proto_kind::bigint), table(proto_kind::object), nullptr};
        }
        if (v.is_boolean()) {
            return {table(proto_kind::boolean), table(proto_kind::object), nullptr};
        }
        if (v.is_kind(heap_kind::symbol)) {
            return {table(proto_kind::symbol), table(proto_kind::object), nullptr};
        }
        if (v.is_kind(heap_kind::function) || v.is_kind(heap_kind::native)) {
            return {table(proto_kind::function), table(proto_kind::object), nullptr};
        }
        if (v.is_object()) { return {table(proto_kind::object), nullptr, nullptr}; }
        return {nullptr, nullptr, nullptr};
    }

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
    // WHAT TIME A PAGE THINKS IT IS.
    //
    // `Date.now()` returned a literal 0, so every page here believed it was
    // 1 January 1970 - a copyright line, a date picker and an age calculation all
    // silently wrong - and `Date.now() - start` was always 0, so anything pacing
    // itself by wall clock saw no time pass at all.
    //
    // The frozen clock was deliberate, for the reason Math.random is seeded: a
    // page that draws from either cannot have a byte-comparable golden otherwise.
    // That reasoning is kept and the two failures are not: the default is a FIXED
    // BASE plus the page's own monotonic time, so it is deterministic under
    // `tick()` (16 ms a frame), it ADVANCES, and it reads as a plausible instant
    // rather than the epoch.
    //
    // An embedder that wants real time installs one - `browser::set_clock`, which
    // the SDL app does, because an application showing the wrong date is a bug no
    // golden cares about.
    static constexpr double fixed_epoch_base = 1767225600000.0; // 2026-01-01T00:00:00Z

    void set_clock(std::function<double()> clock) { clock_ = std::move(clock); }
    [[nodiscard]] double clock_ms() const { return clock_ ? clock_() : fixed_epoch_base; }

    void set_pending_promise_factory(std::function<value(context &)> make) {
        pending_promise_factory_ = std::move(make);
    }
    void set_promise_settler(std::function<void(context &, value, value, bool)> settle) {
        promise_settler_ = std::move(settle);
    }
    void set_promise_factory(std::function<value(context &, value, bool)> make) {
        promise_factory_ = std::move(make);
    }
    // A PENDING promise, and settling one. What a host needs to model work that
    // finishes later - a fetch off the event loop, a decode, a file read - now
    // that `await` can actually suspend on one.
    //
    // The standard library owns what settling MEANS, including queueing the
    // handlers, so both go through the hooks it installed rather than reaching
    // into the object's properties.
    [[nodiscard]] value make_pending_promise() {
        return pending_promise_factory_ ? pending_promise_factory_(*this) : value::undefined();
    }
    void settle_promise(value promise, value with, bool rejected) {
        if (promise_settler_) { promise_settler_(*this, promise, with, rejected); }
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
    // A STRING LITERAL, MEMOISED PER SITE - AND THE MEMO IS PART OF THE ABI
    // RATHER THAN AN OPTIMISATION.
    //
    // `allocations_` counts TOTAL allocations for the process lifetime and is
    // never reset, so the 40,000,000 ceiling is a lifetime budget. Interpreted,
    // a string literal in a per-pixel loop allocates ONE object for the whole
    // run because the handler memoises it; the same loop compiled without the
    // memo allocates one per iteration, and at 480k pixels a frame it reaches
    // the ceiling in about a second - raising an UNCATCHABLE failure on a
    // program the interpreter runs forever. That is a divergence in the raise
    // tier introduced by an optimisation, which is why it is mandatory.
    //
    // site == nullptr MEANS DO NOT MEMOISE, which is what lets a companion
    // allocation with no cache of its own stay at parity.
    //
    // POINTER AND LENGTH, not a C string: a JavaScript string may contain an
    // embedded NUL, and `a\0b` would silently truncate. String IDENTITY is
    // unobservable - strict equality compares text - so sharing one object is
    // safe; it is the ceiling, not identity, that makes it required.
    [[nodiscard]] value interned_string(const void * site, std::uint32_t slot,
                                        std::string_view text);

    [[nodiscard]] value lookup_index(value target, value key);

    // `target[key] = v` for an arbitrary key value - the write twin of
    // lookup_index, and the same shape: array-with-a-NUMBER is the fast path,
    // with the typed-array and owning arms inside it, and everything else is a
    // named write through store_property with to_string of the key.
    //
    // THE SLOW PATH IS NOT "THE NON-ARRAY CASE". The guard is is_array() AND
    // is_number(), so `a['foo'] = 1` arrives there on an array and hits
    // store_property's drop-everything-but-length arm.
    void store_index(value target, value key, value v);

    // THREE OPCODE BODIES LIFTED OUT OF run_loop VERBATIM, for the reason all
    // the others were: a compiled `key in obj` and an interpreted one must not
    // be able to disagree, and the only way to guarantee that is one copy.
    //
    // Each keeps its opcode's own quirks rather than tidying them. `in` on an
    // ARRAY asks about an index, so the key must parse as a whole number and
    // consume the whole string - "1x" is not index 1. `instanceof` walks the
    // explicit prototype chain and THEN the implicit tables, but the second
    // pass is object-like only, because `5 instanceof Number` is false in
    // JavaScript however many methods a primitive resolves. `delete` on
    // anything that is not an object is a silent no-op.
    // THE PROTOTYPE LINK, READ AND WRITTEN - what `super` walks.
    //
    // is_object() is heap_kind::object EXACTLY, so both report or ignore an
    // array, a string, a proxy, a native and a CLOSURE, whose chain is
    // closure_object::proto_link and is never this field. And a fresh object's
    // prototype is value::null() while `extends` is what sets it, so
    // `super.m()` in a BASE-class method reads null - which a backend that
    // folded super-dispatch would get wrong.
    // WHAT super(...) HANDS THE BASE CONSTRUCTOR. The next frame pushed gets
    // THIS frame's new.target instead of undefined.
    //
    // IT MUST REPRODUCE THE LEAK, NOT REPAIR IT. Only two places clear
    // pending_new_target_ and both are JS-closure frame pushes, so a native or
    // generator callee leaves the flag set and the next ordinary call anywhere
    // sees a truthy new.target. That is the interpreter's behaviour and the two
    // tiers have to agree on it.
    //
    // IT TAKES THE VALUE rather than the frame only because call_frame is
    // declared further down this class. The ABI helper above it is
    // zero-operand on purpose - reading THIS frame's field is the point, and an
    // explicit ABI parameter would let a backend hand over a stale one.
    void pass_new_target(value from);

    // `{...o}` AND `{a, ...rest}` - object spread, both directions.
    //
    // THE ROW HAS CITED THIS BY NAME SINCE BEFORE IT EXISTED. Its DELEGATES TO
    // read "context::copy_own_properties" while nothing in the tree defined
    // one, which is the second fictional delegate found here - the first was
    // context::callee_type_error, cited by two rows and defined nowhere. The
    // row is now true.
    //
    // THE SOURCE'S ENTRIES ARE COPIED FIRST, and that is not a micro-optimisation
    // to undo: set() can reallocate the target's storage, and target and source
    // may be the SAME object.
    void copy_own_properties(value target, value source);

    // `get x()` AND `set x(v)`, in a class or an object literal.
    //
    // BOTH OPCODES ARE THIS ONE MEMBER, because the runtime primitive is
    // already fused: accessor_table::define SKIPS undefined halves, which is
    // exactly what merges a get/set pair into one entry. The discriminator is
    // the OPCODE, read out of in.code by the interpreter and known statically
    // at a compiled site - so it never reaches here; the caller passes
    // undefined for the half it does not have.
    //
    // ONE ASYMMETRY IS PRESERVED RATHER THAN TIDIED. object_object's arm
    // erase()s a shadowing data property first and closure_object's does NOT,
    // and lookup_property checks find() before find_accessor - so a
    // `static get x()` on a class that already has a static data property `x`
    // never runs. Harmonising the two would change observable behaviour.
    //
    // AND THE CLOSURE ARM MUST STAY: a `static get` installs onto the
    // CONSTRUCTOR closure, so testing is_object() alone would silently drop
    // every static accessor.
    void define_accessor(value target, const std::string & name, value getter, value setter);

    // `delete o.k` - the NAMED form. delete_index is the computed one and they
    // are separate opcodes because the key arrives differently: a name is a
    // constant-pool index here and a VALUE there, and converting a value key
    // runs to_string, which for an object runs user JavaScript.
    void delete_named(value target, const std::string & name);

    // THE OWN STRING KEYS OF AN OBJECT, AS AN ARRAY - what `for (k in o)`
    // iterates. for-in compiles to a for-of over this array, which is how the
    // runtime keeps ONE iteration mechanism instead of two.
    //
    // IN DEFINITION ORDER, data and accessors interleaved, because that is what
    // a page sees and what Object.keys has to match. An ARRAY source enumerates
    // its indices as strings; anything else yields an empty array rather than
    // throwing.
    [[nodiscard]] value own_keys(value source);

    [[nodiscard]] value get_prototype(value target);
    void set_prototype(value target, value proto);

    [[nodiscard]] bool has_property(value target, value key);
    [[nodiscard]] bool instance_of(value target, value ctor);
    void delete_index(value target, value key);

    // PUSH ONE ELEMENT ONTO AN ARRAY LITERAL UNDER CONSTRUCTION.
    //
    // SILENT ON A NON-ARRAY, which is the row's (0, 0, 0) rather than an
    // oversight: the bytecode only ever emits this against an array it has just
    // built, so the guard is a belt on a thing that cannot happen and answering
    // rather than faulting is what keeps the row free of an exception edge.
    void array_append(value target, value v);

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

    // A VALUE THE COLLECTOR CAN SEE, FOR AS LONG AS A C++ SCOPE HOLDS IT.
    //
    // The precise collector walks exactly the roots in GCRoots.def, and a value
    // in a C++ local is in none of them. Most of the engine gets away with that
    // because nothing collects while script is running - but `construct` does
    // not: it allocates the instance, then runs field initialisers, then calls
    // the constructor body, and the instance is in a C++ local across both.
    // Under gc_stress that is a use-after-free, and it was a real one - found
    // by turning the mode on for the first time.
    //
    // A STACK OF TEMPORARIES rather than a second special-cased field like
    // `current_this_` and `pending_new_target_`, which are exactly this problem
    // solved twice. Anything that must survive a call it makes can say so in
    // one line and stop being a hazard.
    class rooted {
    public:
        rooted(context & cx, value v) : cx_(&cx) { cx.temporaries_.push_back(v); }
        ~rooted() {
            if (!cx_->temporaries_.empty()) { cx_->temporaries_.pop_back(); }
        }
        rooted(const rooted &) = delete;
        rooted & operator=(const rooted &) = delete;

    private:
        context * cx_;
    };

    // COLLECT AT EVERY SAFEPOINT, FOR TESTS. Phase 4.
    //
    // The only thing that collects in an ordinary run is `collect_if_due`, once
    // per tick, from the browser's frame loop - so a collection NEVER happens
    // while script is running. That makes every `is_safepoint` flag in
    // aot_helpers.def a claim about a collector that does not yet run there,
    // and it makes a rooting bug in a compiled body impossible to reach: the
    // value in the C++ local the collector cannot see is never given a chance
    // to be freed.
    //
    // Which is why the master plan calls a forced-GC mode the highest-value
    // test in this phase. It is a TEST MODE and says so - it collects the whole
    // heap at every safepoint, which is enormously slow - and it is the only
    // way the rooting discipline the ABI demands can be exercised at all.
    void set_gc_stress(bool on) noexcept { gc_stress_ = on; }
    [[nodiscard]] bool gc_stress() const noexcept { return gc_stress_; }

    // A point where the ABI says a collection may happen. Does nothing unless
    // stress is on, so this is one predictable not-taken branch on the paths
    // that call it.
    void safepoint() {
        if (gc_stress_) { (void)collect(); }
    }

    // HOW MANY COLLECTIONS HAVE RUN. A test that forces GC and asserts an
    // answer proves nothing on its own - the answer is the same if no
    // collection happened at all, which is exactly how a stress mode that
    // silently does nothing looks.
    [[nodiscard]] std::size_t collections() const noexcept { return collections_; }

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
        // --- generators ------------------------------------------------
        // A generator is the SAME suspended frame with a different resumer: an
        // explicit `.next()` rather than a settling promise. `started` exists
        // because the first `.next(v)` must NOT deliver v anywhere - there is
        // no yield waiting for it yet - and `done` because a finished generator
        // must keep answering `{value: undefined, done: true}` for ever rather
        // than running its body again.
        bool generator = false;
        bool started = false;
        bool done = false;
        // Set while the body is running, so a `.next()` from inside itself is
        // refused rather than corrupting the register stack.
        bool running = false;
        coroutine_object() : heap_object(heap_kind::coroutine) {}
    };

    // Put a suspended frame back and run it. `with` is what the await
    // evaluates to; `rejected` throws it at the await instead.
    void resume(value coroutine, value with, bool rejected);

    // What `.next(v)` / `.throw(e)` / `.return(v)` do. Runs the body until it
    // yields or finishes, and answers the `{value, done}` record the iterator
    // protocol is made of.
    enum class resume_mode {
        next,
        thrown,
        returned
    };
    [[nodiscard]] value generator_resume(value generator, value sent, resume_mode how);
    // The object a generator function call hands back.
    // WHERE THE PARENT'S HALF OF AN UPVALUE COMES FROM, and the two tiers
    // genuinely differ - which is why this is a parameter and not an
    // assumption.
    //
    // A descriptor marked from_parent_local names a REGISTER of the enclosing
    // frame. The interpreter has that window and indexes it directly. A
    // compiled body does not: its registers are the backend's own slots and its
    // numbering is not the bytecode's, so the ABI has it pass an array indexed
    // IN PARALLEL with the descriptors instead - ct_aot_make_closure's row
    // spells that out, and packed is the plausible wrong reading.
    //
    // Exactly one pointer is set. Passing both, or neither, is a caller bug.
    struct upvalue_source {
        // RAW BITS, because that is what the ABI passes and a value is not
        // layout-punnable: script::value keeps its bits private and offers
        // from_bits/bits() as the only route in and out. Reinterpreting an
        // array of one as an array of the other would be exactly the type
        // punning this project refuses, and copying it would allocate once per
        // closure a compiled body builds.
        const std::uint64_t * by_descriptor = nullptr;
        // HOW LONG by_descriptor IS, and only that form has a length to state.
        // The register window is the enclosing frame's and is as long as that
        // frame; the parallel array is the CALLER's, and reading past its end
        // is a closure that captured whatever was next in memory.
        std::uint32_t descriptor_count = 0;
        const value * by_register = nullptr;

        [[nodiscard]] value at(std::size_t which, const upvalue_desc & up) const {
            return by_descriptor != nullptr ? value::from_bits(by_descriptor[which])
                                            : by_register[up.index];
        }
    };

    // ONE COPY OF op::closure's BODY, shared with the compiled tier.
    //
    // Factored so run_loop and ct_aot_make_closure cannot drift, which is the
    // .def's instruction for this row. It also GUARDS three things the inline
    // version dereferences unguarded, because a compiled caller can reach it in
    // configurations the interpreter cannot: no program with no enclosing
    // closure, a function index past the end, and an upvalue count that
    // disagrees with the target's. Each raises; the row is raise-tier only.
    [[nodiscard]] value make_closure(closure_object * enclosing, std::uint32_t function_index,
                                     upvalue_source parent, value enclosing_this);

    [[nodiscard]] value make_generator(closure_object * closure, value receiver,
                                       std::span<const value> args);
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
    // THE AOT BRIDGE REACHES IN HERE, and it is one line rather than nine
    // declarations because the helper bodies are the ABI's, not the VM's.
    // ct_aot_enter pushes a real call_frame, ct_aot_leave truncates handlers_
    // exactly as op::ret does, and ct_aot_check classifies against frames_ and
    // failed_ - all of which are this class's private state and none of which
    // should become public API for a rung. See lib/Script/aot_bridge.cpp.
    // The one implementation of "enter this callable", which `call` and
    // `construct` are the two public spellings of. Private because
    // `constructing` is not a thing an embedder should be choosing.
    value invoke(value callable, std::span<const value> args, value this_value, bool constructing);

    friend struct aot_bridge;
    // The dispatch layer maintains `executing_` and reads it to attribute a
    // transition. It is not a second implementation of anything - it is the
    // ONE place that decides interpreted or native, and these are the two
    // members it needs.
    friend class executing_as;
    friend bool enter_compiled_body(context & ctx, const function_proto & target, value closure,
                                    const value * argv, std::uint32_t argc, value receiver,
                                    bool constructing, value & out);
    friend void note_transition_into_vm(const context & ctx) noexcept;
    friend void note_transition_into_cxx(const context & ctx) noexcept;

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

        // WHERE THE THROWN VALUE WAS JUST PUT, recorded by unwind_to_handler at
        // the moment it writes. Phase 6, and it exists for compiled frames.
        //
        // The interpreter never needs it: its catch block was compiled with the
        // register baked into the instruction at `address`, so resuming at that
        // address is enough. A compiled body has no bytecode to resume into -
        // it asks ct_aot_catch_land where the value went, and the handler that
        // knew has already been POPPED by the search. Two bytes on the frame is
        // the whole of the fix, and it keeps ct_aot_catch_land's signature the
        // one the ABI table declares, which two code generators are written
        // against.
        std::uint16_t landed_slot = 0;

        // `new.target`: the constructor this frame was entered with, or
        // undefined for an ordinary call. It is a VALUE rather than a flag
        // because it PROPAGATES - a base constructor reached through super()
        // reports the derived class `new` was written against, not itself.
        value new_target = value::undefined();

        // WHICH GENERATOR THIS FRAME IS THE BODY OF, or null for an ordinary
        // call. `yield` needs it to know where to save itself, and it cannot be
        // found any other way: the coroutine is reached from the generator
        // object, not from the frame.
        coroutine_object * generator = nullptr;

        // `new C()` evaluates to the new object, NOT to whatever the
        // constructor body happens to return - unless it returns an object,
        // which is the one case the spec lets override it.
        bool constructing = false;

        // The `arguments` object, when this body built one.
        //
        // Kept on the FRAME as well as in a register because it is built before
        // the parameter prologue - it has to be, or a default or a destructured
        // pattern has already overwritten the register it would read - and
        // building it claims a register that an EXTRA argument may be sitting
        // in. Anything after that point which still needs the raw arguments,
        // which is the rest parameter, reads them from here instead.
        value arguments_object = value::undefined();

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

    [[nodiscard]] std::string describe_thrown(value thrown);
    // ToPrimitive for the string case: an object's own toString, then valueOf.
    [[nodiscard]] std::string to_primitive_string(value v);
    // ToPrimitive with the default hint, for `+`.
    [[nodiscard]] value to_primitive(value v);
    // A function's `prototype`, made on first use. See the definition.
    [[nodiscard]] value ensure_prototype(value fn);
    [[nodiscard]] value make_instance(value callee);

    // THE `new` FORM OF THE CALLEE TYPE ERROR, factored so the two tiers
    // cannot spell it differently. ct_aot_call's row asks for a general
    // callee_type_error(form, callee, receiver, site); THAT FUNCTION HAS NEVER
    // EXISTED - it is cited by two rows and defined nowhere - and this is its
    // `new` half, which is the only half with a caller.
    //
    // `origin` IS THE BACKWARDS SCAN, PASSED IN. callee_origin walks emitted
    // bytecode from an ip and a register index, and an AOT frame has neither,
    // so the interpreter supplies the scan's result and a compiled frame
    // supplies nothing - which describe_callee renders as "the value".
    void new_callee_type_error(const function_proto & fn, std::string_view origin, value callee);

    // op::construct's OWN DISPATCH, AS A VALUE - a different function from
    // `construct` above rather than a wrapper round it, because VM_CASE
    // (construct) ends in a frame PUSH and computes nothing, so a helper that
    // must answer with a value cannot share it. What it CAN share is every
    // branch, and each one is the same member the interpreter calls.
    //
    // `from` is the function the `new` was written in, used only to name it in
    // the TypeError.
    // THE SPREAD FORMS OF A CALL AND A `new`, which are one VM_CASE because
    // they differ only in which member they end in.
    //
    // THE ARGUMENTS ARRIVED AS AN ARRAY because their count was not known until
    // the spread was evaluated - so there is no argc and no contiguous window,
    // and a compiled caller needs none of the window machinery ct_aot_call
    // needs. A non-array yields NO arguments rather than one, which is the
    // interpreter's behaviour and is why the unpack is shared rather than
    // written twice.
    [[nodiscard]] std::vector<value> spread_arguments(value arg_array);
    [[nodiscard]] value call_spread(value callee, value arg_array, value receiver);
    [[nodiscard]] value construct_spread(value callee, value arg_array);

    [[nodiscard]] value construct_new(value callee, std::span<const value> args,
                                      const function_proto & from);

    [[nodiscard]] value execute(const program & prog, const function_proto & entry);
    [[nodiscard]] value run_loop(std::size_t stop_depth);
    // A FAILURE COMES WITH THE STACK IT HAPPENED ON.
    void raise(std::string message) {
        if (failed_) { return; }
        failed_ = true;
        error_ = std::move(message) + current_stack();
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
            // AND WHICH SLOT THAT WAS. The handler carrying it has already been
            // popped above, so this is the last moment anything knows.
            target.landed_slot = h.slot;
            thrown_ = value::undefined();
            return true;
        }
        return false;
    }

    void mark(value v);
    void mark_object(heap_object * o);
    void sweep_all();

    // WHAT IS RUNNING RIGHT NOW - the interpreter, a compiled body, or C++.
    //
    // The half of a mixed-mode transition that cannot be read off the call
    // site: `enter_compiled` is reached from `op::call` and from `context::call`
    // alike, and only this says whether the code doing the calling was itself
    // compiled. Maintained by `executing_as`, which is one byte saved and
    // restored on the C++ stack of whatever entered a body.
    executing_kind executing_ = executing_kind::cxx;

    // Set while a native runs, so it can see its receiver.
    value current_this_ = value::undefined();
    // The program being executed, so a call from C++ can find the string
    // tables a nested frame needs.
    const program * program_ = nullptr;
    // PROGRAMS THE CONTEXT COMPILED ITSELF, for `new Function(body)`.
    //
    // A closure holds a `const function_proto *` into the program it came from
    // and `closure_object::owner` records which program that is, so a function
    // compiled at run time works everywhere - as long as the program OUTLIVES
    // it. Nothing else owns one, so the context does. Same reasoning as
    // browser::run_script keeping its own.
    std::vector<std::unique_ptr<program>> owned_programs_;
    // Keyed by the FUNCTION rather than by its index in the current program.
    // An index is only meaningful within one program, and a context can run
    // more than one: a devtools-style eval calls a function the page defined,
    // so the running frame's proto belongs to a DIFFERENT program from the one
    // being executed. Subtracting its address from the wrong program's
    // functions vector gave a garbage index and read off the end of the cache.
    // KEYED BY const void *, NOT const function_proto *, so both tiers share
    // one cache. The interpreter keys it by the proto it is running; a compiled
    // body keys it by the `site` its entry was handed, which IS that proto -
    // but the ABI hands it as an opaque const ct_aot_site *, and widening the
    // key here is cheaper than a cast at every use.
    flat_map<const void *, flat_map<std::uint32_t, value>> string_cache_;
    // The same idea for BigInt literals: the digits never change, so a site in
    // a loop parses them once. Keyed the same way and swept the same way.
    flat_map<const function_proto *, flat_map<std::uint32_t, value>> bigint_cache_;
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
    std::function<double()> clock_;
    std::function<value(context &)> pending_promise_factory_;
    std::function<void(context &, value, value, bool)> promise_settler_;
    // Set by a frame that suspended, so `resume` can tell "awaited again" from
    // "returned" - both leave run_loop the same way.
    // Set by `op::pass_new_target` and consumed by the very next frame push, so
    // a super() call hands its own new.target to the base constructor.
    value pending_new_target_ = value::undefined();
    // THE CLOSURE A COMPILED BODY IS ABOUT TO BE ENTERED WITH.
    //
    // The entry ABI delivers `site` - the function_proto - and not the closure,
    // and upvalues live on the closure INSTANCE: two closures over the same
    // function share a proto and have different upvalues. So a compiled body
    // could reach nothing it captured, and ct_aot_upvalue_cell, ct_aot_callee
    // and ct_aot_home were all blocked at that one point.
    //
    // IT TRAVELS THE SAME WAY new.target DOES rather than through the ABI's
    // signature, which aot_entry.h calls "the thing two code generators are
    // written against". Set by enter_compiled_body, consumed and cleared by
    // ct_aot_enter, and a GC root in the window between - the same window the
    // comment on pending_new_target_ describes.
    value pending_closure_ = value::undefined();
    flat_map<std::string, module_record> modules_;
    // The module being evaluated, so `bind_export` knows whose cells to adopt and
    // `load_import` knows who is asking. Null while a classic script runs.
    module_record * current_module_ = nullptr;
    // See the definition: `run` cannot be re-entered, and a dynamic import
    // needs a program evaluated from inside the interpreter.
    run_result run_reentrant(const program & prog);
    std::function<value(context &, const std::string &, const std::string &)> module_loader_;
    bool suspended_ = false;
    // Set by `op::yield_value` so generator_resume can tell a body that YIELDED
    // from one that RETURNED - run_loop hands back a value either way, and the
    // difference is the whole of `done`.
    bool yielded_ = false;
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

    // string_flat_map, NOT flat_map<std::string, value>: the plain one's hasher
    // and equality are not transparent, so `find(string_view)` cannot exist and
    // every caller had to build a std::string to throw away. That is the bug
    // docs/performance.md measured at -47% on object_object::find, and this map
    // is the one the whole shell binding layer reads through.
    string_flat_map<value> globals_;
    std::vector<value> registers_;
    std::vector<call_frame> frames_;
    heap_object * heap_ = nullptr;
    std::size_t live_objects_ = 0;
    // Values a C++ scope is holding across something that can collect. See
    // `rooted`; marked in collect() like any other root.
    std::vector<value> temporaries_;
    std::size_t collections_ = 0;
    bool gc_stress_ = false;
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
