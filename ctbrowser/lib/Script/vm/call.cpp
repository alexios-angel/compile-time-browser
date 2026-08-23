// ctbrowser.script context - calls, construction, modules, generators and the error reporting.
//
// One of four files carved out of a 3,232-line vm.cpp on 2026-08-09. All
// members of `context`, declared in include/ctbrowser/script/vm.hpp - so
// they split across translation units with nothing to declare.

#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <ctbrowser/script/bigint.hpp>
#include <ctbrowser/script/number_format.hpp>
#include <ctbrowser/script/vm.hpp>

// The VM's implementation.
//
// `run_loop` alone is 15 KB of object code - the whole instruction dispatch -
// and while it lived in the interface every translation unit that imported the
// module emitted its own copy and optimised it again. The class declaration
// stays in :vm; the bodies live here and are compiled once.

namespace ctbrowser::script {

// ===================== the dispatch loop =================================

// Call a JS function from C++.
//
// The two cases are genuinely different: a native is just a C++ call, while a
// closure needs a frame on the interpreter's own stack. Giving the closure a
// region ABOVE everything currently live is what lets this be re-entrant - the
// caller's registers are untouched, so a listener that triggers another
// listener works rather than corrupting the frame that dispatched it.
value context::call(value callable, std::span<const value> args, value this_value) {
    return invoke(callable, args, this_value, /*constructing*/ false);
}

// EVERY C++ ENTRY INTO JAVASCRIPT ENDS UP HERE - a DOM event, a timer, a
// promise job, an animation frame, `Function.prototype.apply`, a getter, a
// class field initialiser, `super()` - and until Phase 3 it could not reach a
// compiled body, because it pushed a frame and ran the loop without ever asking
// whether the function had one. That is not a failure: it returns the right
// answer, interpreted, which is why the plan calls it "a performance cliff
// rather than a bug" and makes centralising this a phase of its own.
value context::invoke(value callable, std::span<const value> args, value this_value,
                      bool constructing) {
    if (callable.is_kind(heap_kind::native)) {
        auto * nat = static_cast<native_object *>(callable.as_heap());
        std::vector<value> copy{args.begin(), args.end()};
        const value saved = current_this_;
        current_this_ = this_value;
        note_transition_into_cxx(*this);
        const value out = [&] {
            const executing_as running{*this, executing_kind::cxx};
            return nat->fn(*this, copy);
        }();
        current_this_ = saved;
        return out;
    }
    if (!callable.is_kind(heap_kind::function) || program_ == nullptr) {
        return value::undefined();
    }
    auto * fnobj = static_cast<closure_object *>(callable.as_heap());
    const function_proto & target = *fnobj->proto;
    // CALLING A GENERATOR RUNS NOTHING, here as much as in `op::call`. This
    // path is the one `Function.prototype.apply` and `.call` take, and it is
    // how a generator is actually started in practice: TypeScript's __awaiter
    // does `(generator = generator.apply(thisArg, args)).next()`. Missing it
    // meant the body was entered as an ordinary frame and its first `yield` had
    // no generator to suspend into.
    if (target.is_generator) { return make_generator(fnobj, this_value, args); }

    const std::size_t new_base = registers_.size();
    // EVERY argument lands in a register, not just the declared ones. Two
    // reasons, and both bite: a rest parameter reads the extra ones straight
    // out of the frame, and a value that is only in the caller's std::vector is
    // not a GC root - so a collection during the call would free an argument
    // that is about to be used.
    const std::size_t window = std::max<std::size_t>(target.frame_size, args.size());
    registers_.resize(new_base + window + 8u, value::undefined());
    for (std::size_t i = 0; i < std::max<std::size_t>(target.param_count, args.size()); ++i) {
        registers_[new_base + i] = i < args.size() ? args[i] : value::undefined();
    }
    // A SAFEPOINT, AND IT BELONGS EXACTLY HERE - after the arguments are in the
    // register window and before anything runs.
    //
    // It was at the top of this function, which made the forced-GC mode
    // unusable by its own callers: `ctx.call(fn, args, this)` takes a span the
    // EMBEDDER owns, so collecting before the copy above frees any heap
    // argument that is not separately rooted. The first thing written against
    // it - a test calling an interpreted function with an array it held in a
    // C++ local - was a heap-use-after-free on that array, and the test was
    // right.
    //
    // Below the copy, `registers_` holds every argument and the collector
    // traces it in full, which is also where a real collector would run: at the
    // point where the frame it is about to enter is describable.
    safepoint();
    // A COMPILED BODY, IF THIS FUNCTION HAS ONE, asked in the same place the
    // interpreter asks. AFTER the argument fill, because that window is what
    // the ABI's entry row promises, and BEFORE the depth guard, because
    // ct_aot_enter owns that guard for a compiled frame.
    if (value produced = value::undefined(); enter_compiled(
            *this, target, registers_.data() + new_base, static_cast<std::uint32_t>(args.size()),
            this_value, constructing, produced)) {
        if (registers_.size() >= new_base) { registers_.resize(new_base); }
        return produced;
    }
    if (frames_.size() > 512) {
        raise("call stack exhausted");
        return value::undefined();
    }
    const std::size_t depth = frames_.size();
    const value saved = current_this_;
    current_this_ = this_value;
    note_transition_into_vm(*this);
    const executing_as running{*this, executing_kind::vm};
    call_frame entered{
        &target, 0,          new_base,        0, static_cast<std::uint16_t>(args.size()),
        fnobj,   this_value, handlers_.size()};
    // THIS IS THE PATH super() TAKES - it compiles to `op::apply`, which lands
    // here - so the new.target a super() call passed along is consumed here or
    // nowhere.
    entered.new_target = pending_new_target_;
    pending_new_target_ = value::undefined();
    frames_.push_back(entered);
    const value out = run_loop(depth);
    current_this_ = saved;
    // Only shrink back if nothing below is still using the space - a nested
    // call that grew the stack further has already returned by now.
    if (registers_.size() >= new_base) { registers_.resize(new_base); }
    return out;
}

// CREATE THE BINDINGS WITHOUT RUNNING ANYTHING. This is the instantiate half of
// the specification's two-phase module loading, and a cycle is the reason it
// exists: A imports B imports A, so B evaluates first and asks A for a binding
// A has not reached the declaration of. With every cell in the graph created up
// front, B gets a real box that is merely EMPTY, and the write A makes later is
// a write B reads - which is a cycle resolving rather than a missing export.
//
// It also has to happen before evaluation for a reason unrelated to cycles: the
// cell an importer takes and the cell the exporter writes must be the SAME
// object, and the only way to guarantee that is for one of them not to make it.
void context::instantiate_module(const program & prog, module_record & into) {
    into.compiled = &prog;
    for (const std::string & name : prog.exports) {
        value & slot = into.exports[name];
        if (!slot.is_kind(heap_kind::cell)) {
            slot = value::object(allocate<cell_object>(value::undefined()));
        }
    }
}

// THE NAMESPACE OBJECT, and its properties are ACCESSORS rather than values.
//
// A namespace is live exactly as a named import is - `ns.count` after the
// exporter reassigns `count` must read the new value - so copying the cells'
// contents into an ordinary object here would be the same shortcut in a
// different shape. Each property is a getter over the cell instead, which is
// what the cell was for.
//
// ONE OBJECT PER MODULE, cached in the record: `import * as a` and `import * as
// b` of the same module are required to be the SAME object, and code compares
// namespaces by identity.
value context::module_namespace(module_record & of) {
    if (of.namespace_object.is_kind(heap_kind::object)) { return of.namespace_object; }
    object_object * const ns = allocate<object_object>();
    of.namespace_object = value::object(ns);
    for (auto & [name, cell] : of.exports) {
        const value box = cell;
        ns->define_accessor(name,
                            value::object(allocate<native_object>(
                                "get " + name,
                                [box](context &, std::span<value>) {
                                    return box.is_kind(heap_kind::cell)
                                               ? static_cast<cell_object *>(box.as_heap())->slot
                                               : value::undefined();
                                })),
                            value::undefined());
    }
    return of.namespace_object;
}

// A MODULE IS RUN LIKE ANY OTHER PROGRAM, with two differences: it knows which
// record it is filling in, so `bind_export` knows which cells to adopt, and its
// exports outlive the call.
run_result context::run_module(const program & prog, module_record & into) {
    module_record * const outer = current_module_;
    current_module_ = &into;
    into.compiled = &prog;
    const run_result result = frames_.empty() ? run(prog) : run_reentrant(prog);
    into.evaluated = true;
    current_module_ = outer;
    return result;
}

// RUNNING A PROGRAM WHILE ONE IS ALREADY RUNNING, which `run` cannot do and
// must not pretend to: `execute` CLEARS `frames_` and reassigns `registers_`,
// because it is the entry point for a whole turn. A dynamic import is the first
// thing that ever needed a program evaluated from inside the interpreter, and
// calling `run` there wiped the importing module's own frame - so the module
// stopped dead at the `import(...)` and every statement after it silently never
// ran. Nothing threw; the loop simply found no frames left and finished.
//
// This is `call`'s shape rather than `run`'s: push a frame on top of what is
// already there and run down to the depth it started at.
run_result context::run_reentrant(const program & prog) {
    run_result result;
    if (!prog.ok) {
        result.ok = false;
        result.error = prog.error;
        return result;
    }
    const function_proto & entry = prog.functions[0];
    const std::size_t new_base = registers_.size();
    registers_.resize(new_base + entry.frame_size + 8u, value::undefined());
    const std::size_t depth = frames_.size();
    // THE PROGRAM POINTER IS SAVED AND RESTORED. `call` reads it to decide
    // whether a closure can be entered at all, and leaving it pointing at the
    // imported module would make every later call in the IMPORTING one look up
    // its function protos in the wrong program.
    const program * const outer_program = program_;
    program_ = &prog;
    // AND THE SAME QUESTION FOR A MODULE EVALUATING INSIDE ITS IMPORTER. This
    // is the third place that entered a program's top level by pushing a frame
    // of its own; leaving it out would mean an imported module's compiled body
    // was interpreted purely because of who imported it.
    if (value produced = value::undefined();
        enter_compiled(*this, entry, registers_.data() + new_base, 0u, value::undefined(),
                       /*constructing*/ false, produced)) {
        program_ = outer_program;
        if (registers_.size() >= new_base) { registers_.resize(new_base); }
        return result;
    }
    frames_.push_back(
        call_frame{&entry, 0, new_base, 0, 0, nullptr, value::undefined(), handlers_.size()});
    (void)run_loop(depth);
    program_ = outer_program;
    if (registers_.size() >= new_base) { registers_.resize(new_base); }
    // NO drain_microtasks HERE. The checkpoint belongs to the end of a turn,
    // and this is the middle of one - draining now would run the importer's own
    // pending handlers before its next statement.
    result.ok = !failed_;
    result.error = error_;
    return result;
}

run_result context::run(const program & prog) {
    run_result result;
    if (!prog.ok) {
        result.ok = false;
        result.error = prog.error;
        return result;
    }
    failed_ = false;
    error_.clear();
    result.returned = execute(prog, prog.functions[0]);
    // THE END OF THE TURN. A script's promise handlers run after its last
    // statement, not between two of them - so the checkpoint is here, once the
    // top level has finished.
    drain_microtasks();
    // AND A FAILED SCRIPT'S QUEUE DIES WITH IT. `drain_microtasks` stops on
    // `failed_`, so a script that threw left its already-queued jobs in the
    // queue - and once a page's classic scripts became separate programs, the
    // NEXT script's checkpoint ran them, after that script's synchronous code.
    // A handler belonging to a script that is over should not surface in the
    // middle of the following one.
    //
    // CHROME WOULD RUN THEM, and that difference is real: an uncaught throw
    // does not cancel a microtask checkpoint there. It is not matched here
    // because `raise` and an uncaught throw set the same `failed_` - the
    // call-stack ceiling and the allocation ceiling look exactly like a page
    // exception from this function - and draining after a resource ceiling
    // would run more JavaScript precisely where the ceiling exists to stop it.
    // Telling the two apart is the fix; dropping the queue is the safe half of
    // it, and this comment is the record of which half was taken.
    if (failed_) { microtasks_.clear(); }
    result.ok = !failed_;
    result.error = error_;
    return result;
}

value context::execute(const program & prog, const function_proto & entry) {
    registers_.assign(entry.frame_size + 8u, value::undefined());
    frames_.clear();
    // AND THE HANDLER STACK, WHICH FRAMES_.CLEAR() DOES NOT IMPLY. A VM-level
    // `raise` - the call-stack ceiling, the allocation ceiling - returns out of
    // run_loop WITHOUT unwinding, because the loop's condition is
    // `frames_.size() > stop_depth && !failed_` and it simply stops. Any `try`
    // that was live at that moment stays on `handlers_` recording frame 0.
    //
    // A SECOND TOP-LEVEL PROGRAM ON THIS CONTEXT THEN HAS A FRAME 0 TOO, so
    // `unwind_to_handler` accepts the dead program's handler: it only rejects
    // one whose frame has returned. It writes the thrown value into the wrong
    // frame's register and sets `ip` to an ADDRESS OUT OF THE OTHER PROGRAM'S
    // BYTECODE, and the interpreter carries on from there. Measured: a page
    // whose first script exhausts the stack inside a `try` and whose second
    // alerts 1..8 then throws produced 1,2,3,4,5,6,7,8,3,4,5,6,7,8 - the throw
    // swallowed, six statements run twice - and padding the first script moved
    // where the second one resumed.
    //
    // It became reachable when a page's classic scripts stopped being one
    // program: before that, a raise in the first script ended the only top
    // level and nothing else ran on the dirtied context.
    handlers_.clear();
    thrown_ = value::undefined();
    program_ = &prog;
    // A COMPILED TOP LEVEL, IF THIS PROGRAM HAS ONE, and for ctcompile that is
    // the ordinary case rather than an exotic one: a page's `<script>` IS a top
    // level, so a backend that compiles anything compiles this. Asked after the
    // reset above, so a compiled body enters a context in the state it expects,
    // and before the frame push, because ct_aot_enter pushes its own.
    if (value produced = value::undefined();
        enter_compiled(*this, entry, registers_.data(), 0u, value::undefined(),
                       /*constructing*/ false, produced)) {
        return produced;
    }
    frames_.push_back(call_frame{&entry, 0, 0, 0, 0, nullptr, value::undefined(), 0});
    // Per-frame string interning: a literal in a loop should allocate once,
    // not once per iteration.
    string_cache_.clear();
    bigint_cache_.clear();
    // A TOP-LEVEL PROGRAM IS A C++ ENTRY LIKE ANY OTHER. Marking it is what
    // makes the transition INSIDE it attributable: without this, `executing_`
    // is still `cxx` for the whole run, so an interpreted script calling a
    // compiled function would be counted as C++ reaching it directly. The
    // interpreter is what is running; say so.
    note_transition_into_vm(*this);
    const executing_as running{*this, executing_kind::vm};
    return run_loop(0);
}

// The interpreter loop, entered at a frame depth and running until it unwinds
// back to it. `stop_depth` is 0 for the top-level program and the caller's
// depth for a call from C++ - which is what makes call() re-entrant instead of
// a second interpreter.
// WHAT was called, in the terms the source used. "attempted to call a
// non-function" is true and useless; `o.foo is undefined, not a function` says
// which line to look at. The method and computed forms know the name outright;
// a plain call only knows what it found, which is still the difference between
// "undefined" and "a number".
// Where a plain call's callee CAME FROM. A method call knows its name outright;
// `f(...)` only has a register, so this walks back through the emitted code for
// the instruction that last wrote it. Costs nothing until something fails, and
// turns "the value is undefined" into "`Symbol` is undefined".
// `ip` is the index of the failing instruction ITSELF, and the scan starts
// strictly before it - passing the post-incremented ip made the call match
// itself and report "what `x()` returned" about the very call that failed.
std::string context::callee_origin(const function_proto & fn, std::size_t ip,
                                   std::uint16_t reg_index) {
    std::uint16_t want = reg_index;
    for (std::size_t i = ip; i-- > 0;) {
        const instruction & prior = fn.code[i];
        if (prior.a != want) { continue; }
        switch (prior.code) {
        case op::get_global: return fn.names[prior.bx()];
        case op::get_prop: return fn.names[prior.c];
        // A `move` only relays: keep looking for whatever filled its source.
        case op::move: want = prior.b; continue;
        case op::get_index: return "a computed member";
        case op::get_upvalue: return "a captured variable";
        case op::cell_get: return "a boxed local";
        // `f()(...)` - what was called is what the INNER call returned, so name
        // that instead. Bounded because each step moves strictly earlier.
        case op::call:
        case op::call_method:
        case op::construct: {
            const std::string inner =
                prior.code == op::call_method ? fn.names[prior.c] : callee_origin(fn, i, prior.a);
            return inner.empty() ? std::string{"the result of a call"}
                                 : "what `" + inner + "()` returned";
        }
        default: return "the result of opcode " + std::to_string(static_cast<int>(prior.code));
        }
    }
    return {};
}

std::string context::describe_callee(const function_proto & fn, std::string_view name,
                                     value callee) {
    const std::string what =
        name.empty() ? std::string{"the value"} : "`" + std::string{name} + "`";
    return what + " is " + std::string{type_of(callee)} +
           (callee.is_undefined() || callee.is_null() ? "" : " (" + to_string(callee) + ")") +
           ", not a function - in " +
           (fn.name.empty() ? std::string{"<anonymous>"} : "`" + fn.name + "`");
}

// EVERY FUNCTION HAS A `prototype`, and JavaScript relies on it far beyond
// classes: `function F() {}; new F() instanceof F` is the constructor-function
// pattern every transpiler emits, and Babel's own `_classCallCheck` guard is
// exactly that test. A class got one from the compiler; a plain function got
// nothing, so `new F()` produced an object with no prototype and `instanceof`
// was false for it.
//
// Made on demand rather than at closure creation: a program allocates far more
// functions than it constructs, and an object per closure is a real cost for
// something most of them never use.
value context::ensure_prototype(value fn) {
    if (!fn.is_kind(heap_kind::function)) { return value::undefined(); }
    auto * closure = static_cast<closure_object *>(fn.as_heap());
    if (value * existing = closure->find("prototype")) { return *existing; }
    // An arrow is not a constructor and never needs one.
    if (closure->proto != nullptr && closure->proto->is_arrow) { return value::undefined(); }
    value made = make_object();
    static_cast<object_object *>(made.as_heap())->set("constructor", fn);
    closure->set("prototype", made);
    return made;
}

value context::make_instance(value callee) {
    auto * instance = allocate<object_object>();
    if (callee.is_object()) {
        if (value * proto = static_cast<object_object *>(callee.as_heap())->find("prototype")) {
            instance->prototype = *proto;
        }
    } else if (callee.is_kind(heap_kind::function)) {
        instance->prototype = ensure_prototype(callee);
    }
    return value::object(instance);
}

// The handler's trap of this name, if it has one. An ABSENT trap is not an
// error and not a silent skip: the operation falls through to the target,
// which is exactly what absent means in the spec.
// WHAT WAS THROWN, in the terms the thrower used. `to_string` on an object is
// "[object Object]", which is the least useful thing a diagnostic can say -
// and an uncaught throw is almost always an Error, whose name and message are
// right there.
std::string context::describe_thrown(value thrown) {
    if (thrown.is_object()) {
        auto * obj = static_cast<object_object *>(thrown.as_heap());
        const value name = lookup_property(thrown, "name");
        const value message = lookup_property(thrown, "message");
        if (!name.is_undefined() || !message.is_undefined()) {
            return to_string(name.is_undefined() ? string("Error") : name) + ": " +
                   to_string(message);
        }
        // Not an Error: say what it HAS, which is usually enough to recognise.
        std::string keys;
        obj->each_own_key([&](const std::string & key) {
            if (keys.size() < 120) { keys += (keys.empty() ? "" : ", ") + key; }
        });
        return "exception: an object {" + keys + "}";
    }
    return "exception: " + to_string(thrown);
}

value context::proxy_trap(value proxy, const std::string & name) {
    auto * p = static_cast<proxy_object *>(proxy.as_heap());
    if (!p->handler.is_object()) { return value::undefined(); }
    value * found = static_cast<object_object *>(p->handler.as_heap())->find(name);
    return found == nullptr ? value::undefined() : *found;
}

value context::iterable_values(value v) {
    // Already an array: hand it straight back, so the common case allocates
    // nothing. Callers must not mutate what they get.
    if (v.is_array()) { return v; }
    if (v.is_string()) {
        // A string iterates by CHARACTER. The index loop already did this through
        // `length`, and doing it here too means spread and for-of agree.
        value out = make_array();
        auto * items = static_cast<array_object *>(out.as_heap());
        for (const char c : static_cast<string_object *>(v.as_heap())->text) {
            items->items.push_back(string(std::string{c}));
        }
        return out;
    }
    if (!v.is_object()) { return make_array(); }
    auto * obj = static_cast<object_object *>(v.as_heap());
    // A GENERATOR IS DRAINED BY RUNNING IT. There is no `length` to loop over
    // and no array behind it - the values do not exist until the body is
    // resumed, once per value.
    //
    // THIS MATERIALIZES, which for-of over an INFINITE generator turns into a
    // hang rather than a lazy loop. `op::iterable` hands back an array by
    // construction, so laziness would mean a different opcode and a real
    // iterator protocol in the loop. Recorded in docs/script.md rather than
    // left to be discovered: the bound is there to make the failure a
    // diagnosable one instead of a silent freeze.
    if (value * co = obj->find("__co"); co != nullptr && co->is_kind(heap_kind::coroutine)) {
        value out = make_array();
        auto * items = static_cast<array_object *>(out.as_heap());
        for (std::size_t guard = 0; guard < 1u << 20; ++guard) {
            const value step = generator_resume(v, value::undefined(), resume_mode::next);
            if (!step.is_object()) { break; }
            auto * record = static_cast<object_object *>(step.as_heap());
            if (value * done = record->find("done"); done != nullptr && truthy(*done)) { break; }
            if (value * each = record->find("value")) { items->items.push_back(*each); }
            if (failed_) { break; }
        }
        return out;
    }
    // A Map or a Set, by the storage the standard library gives them. A Map
    // iterates as [key, value] pairs and a Set as bare values, which is exactly
    // how `__entries` already holds them - so this is a copy and not a rebuild.
    if (value * entries = obj->find("__entries"); entries != nullptr && entries->is_array()) {
        value out = make_array();
        static_cast<array_object *>(out.as_heap())->items =
            static_cast<array_object *>(entries->as_heap())->items;
        return out;
    }
    // The views `keys()`, `values()` and `entries()` hand back - arrays already,
    // so this is for anything that carries its items under another name.
    if (value * items = obj->find("__items"); items != nullptr && items->is_array()) {
        value out = make_array();
        static_cast<array_object *>(out.as_heap())->items =
            static_cast<array_object *>(items->as_heap())->items;
        return out;
    }
    // An ARRAY-LIKE: anything with a numeric length and indexed properties, which
    // is what a NodeList, `arguments` and a page's own collection look like.
    if (value * length = obj->find("length"); length != nullptr && length->is_number()) {
        value out = make_array();
        auto * items = static_cast<array_object *>(out.as_heap());
        const auto count = static_cast<std::size_t>(std::max(0.0, to_number(*length)));
        for (std::size_t i = 0; i < count && i < 1u << 24; ++i) {
            items->items.push_back(lookup_property(v, std::to_string(i)));
        }
        return out;
    }
    return make_array();
}

value context::construct(value callee, std::span<const value> args) {
    // `new proxy(...)` runs the construct trap with (target, argsArray). p5.js
    // has exactly one of these and it runs at the bundle's top level:
    // `p5.renderers['p2d-p3'] = new Proxy(Renderer2D, {construct(...) {...}})`.
    if (callee.is_kind(heap_kind::proxy)) {
        auto * p = static_cast<proxy_object *>(callee.as_heap());
        const value trap = proxy_trap(callee, "construct");
        if (trap.is_callable()) {
            value list = make_array();
            static_cast<array_object *>(list.as_heap())->items.assign(args.begin(), args.end());
            const value trap_args[2] = {p->target, list};
            return call(trap, trap_args, p->handler);
        }
        return construct(p->target, args);
    }
    if (!callee.is_callable()) {
        raise("attempted to construct a non-function");
        return value::undefined();
    }
    const value self = make_instance(callee);
    // THE INSTANCE IS IN A C++ LOCAL FOR THE REST OF THIS FUNCTION, across a
    // field-initialiser run and a constructor body - both of which run user
    // JavaScript and can collect. Nothing rooted it, and under gc_stress that
    // is a heap-use-after-free on the object `new` is building.
    const rooted keep_instance{*this, self};
    run_field_initialisers(callee, self);
    if (callee.is_kind(heap_kind::native)) {
        auto * nat = static_cast<native_object *>(callee.as_heap());
        std::vector<value> copy{args.begin(), args.end()};
        const value saved = current_this_;
        current_this_ = self;
        const value produced = nat->fn(*this, copy);
        current_this_ = saved;
        // A CONVERSION UNDER `new` KEEPS ITS VALUE. `new Number(5)` used to
        // evaluate to the fresh empty instance, because a native returning a
        // primitive looks exactly like a constructor that returned nothing - so
        // the 5 was thrown away and `n + 1` was "[object Object]1". Silently.
        //
        // The DEVIATION, said plainly: the spec builds a wrapper OBJECT here, so
        // `typeof new Number(5)` is "object" in a browser and "number" here.
        // Every operation on it is right, which is the opposite of what happened
        // before, and no page relies on the wrapper - every style guide in
        // existence tells you not to write this. The flag is set only on the
        // three conversions in install_globals, so a page's own constructor
        // returning a primitive still evaluates to its instance per spec.
        if (nat->find("__conversion") != nullptr) { return produced; }
        return produced.is_object_like() ? produced : self;
    }
    // `new C()` evaluates to the new object unless the body returned one of its
    // own - the single case the spec lets override it.
    //
    // `invoke` rather than `call`, so that a constructor with a COMPILED body
    // is told it is constructing. The ABI hands that decision to
    // ct_aot_return_value, and passing false would make `new C()` on a compiled
    // constructor evaluate to whatever the body happened to return.
    const value produced = invoke(callee, args, self, /*constructing*/ true);
    return produced.is_object_like() ? produced : self;
}

void context::run_field_initialisers(value constructor, value self) {
    // Most-derived first, walking `C.prototype`'s own prototype back to the
    // parent's `constructor`. Depth-capped for the same reason every other
    // chain walk here is: a page can make the chain cyclic.
    std::vector<value> chain;
    value current = constructor;
    for (int depth = 0; depth < 64 && current.is_kind(heap_kind::function); ++depth) {
        chain.push_back(current);
        value * prototype = static_cast<closure_object *>(current.as_heap())->find("prototype");
        if (prototype == nullptr || !prototype->is_object()) { break; }
        const value parent_prototype =
            static_cast<object_object *>(prototype->as_heap())->prototype;
        if (!parent_prototype.is_object()) { break; }
        value * parent =
            static_cast<object_object *>(parent_prototype.as_heap())->find("constructor");
        if (parent == nullptr) { break; }
        current = *parent;
    }
    // ...then run them BASE FIRST, so a derived field that reads one the base
    // set finds it there. The spec runs a derived class's fields after its
    // super() call returns; this runs the whole chain before the constructor
    // body instead, which agrees wherever a constructor does not overwrite a
    // field it also declares.
    for (std::size_t i = chain.size(); i-- > 0;) {
        auto * klass = static_cast<closure_object *>(chain[i].as_heap());
        if (value * fields = klass->find("__fields"); fields != nullptr && fields->is_callable()) {
            call(*fields, {}, self);
        }
    }
}

// PUT A SUSPENDED FRAME BACK AND RUN IT.
//
// The mirror of the suspension in `op::await_value`: the saved window goes on
// top of the register stack, the frame is rebuilt around it, the awaited value
// lands in the register the await was writing to, and the body carries on from
// the instruction after it.
//
// Called from a microtask, because a resumption IS a promise handler - it was
// registered on the awaited promise's own handler list, so it queues and orders
// with every other `then`.
// --- generators -------------------------------------------------------------
//
// A generator is the SAME suspended frame `await` uses, with a different
// resumer: an explicit `.next()` instead of a settling promise. That is why
// there is no second suspension mechanism here - `coroutine_object` already
// carried a proto, an ip, a register window and this frame's handlers, which
// is the whole of what a paused function is.
//
// WHAT THIS IS FOR. Babylon.js has 622 `function*` bodies and not one of them
// is an author writing a generator: TypeScript compiles every `async` function
// into a generator driven by an `__awaiter` helper, so `yield` there is what
// `await` became. That helper needs exactly `.next(v)`, `.throw(e)` and a
// `{value, done}` record back, which is what this implements.

value context::make_generator(closure_object * closure, value receiver,
                              std::span<const value> args) {
    auto * saved = allocate<coroutine_object>();
    saved->proto = closure->proto;
    saved->ip = 0;
    saved->argc = static_cast<std::uint16_t>(args.size());
    saved->closure = closure;
    saved->receiver = receiver;
    saved->generator = true;
    // THE ARGUMENTS ARE THE FRAME'S FIRST REGISTERS, which is how an ordinary
    // call passes them - the callee's frame starts where its arguments already
    // are. A generator has no such frame yet, so the window is built here and
    // the body runs out of it on the first `.next()`.
    saved->window.assign(args.begin(), args.end());
    saved->window.resize(closure->proto->frame_size, value::undefined());

    value out = make_object();
    auto * obj = static_cast<object_object *>(out.as_heap());
    if (object_object * table = prototype(proto_kind::generator)) {
        obj->prototype = value::object(table);
    }
    obj->set("__co", value::object(saved));
    return out;
}

value context::generator_resume(value generator, value sent, resume_mode how) {
    const auto record = [&](value v, bool done) {
        value out = make_object();
        auto * obj = static_cast<object_object *>(out.as_heap());
        obj->set("value", v);
        obj->set("done", value::boolean(done));
        return out;
    };
    if (!generator.is_object()) { return record(value::undefined(), true); }
    value * held = static_cast<object_object *>(generator.as_heap())->find("__co");
    if (held == nullptr || !held->is_kind(heap_kind::coroutine)) {
        return record(value::undefined(), true);
    }
    auto * saved = static_cast<coroutine_object *>(held->as_heap());

    // A GENERATOR CANNOT RESUME ITSELF. `.next()` from inside the body would
    // push a second frame over the same register window and both would write
    // each other's locals; the spec makes it a TypeError and so does this.
    if (saved->running) {
        throw_error("TypeError", "this generator is already running");
        return record(value::undefined(), true);
    }
    // A FINISHED GENERATOR KEEPS ANSWERING, for ever. `.next()` past the end is
    // not an error and must not run the body again.
    if (saved->done) {
        if (how == resume_mode::thrown) {
            thrown_ = sent;
            if (!unwind_to_handler()) { raise("uncaught exception from a finished generator"); }
            return record(value::undefined(), true);
        }
        return record(how == resume_mode::returned ? sent : value::undefined(), true);
    }
    // `.throw()` / `.return()` BEFORE THE BODY EVER RAN never enter it: there is
    // no `yield` to throw at, so the generator simply finishes.
    if (!saved->started && how != resume_mode::next) {
        saved->done = true;
        if (how == resume_mode::thrown) {
            thrown_ = sent;
            if (!unwind_to_handler()) { raise("uncaught exception from a generator"); }
        }
        return record(how == resume_mode::returned ? sent : value::undefined(), true);
    }
    // `.return(v)` at a yield finishes the generator without running any more of
    // it. Running the rest would be wrong - `return` means stop - and the
    // `finally` blocks the spec would run on the way out need the unwinder,
    // which is a bigger change than this corpus asks for. Recorded rather than
    // silent: docs/script.md says so by name.
    if (how == resume_mode::returned) {
        saved->done = true;
        return record(sent, true);
    }

    const std::size_t base = registers_.size();
    registers_.insert(registers_.end(), saved->window.begin(), saved->window.end());
    // Slack above the window, for the same reason a call reserves it: an
    // expression allocates scratch registers past the frame's declared size.
    registers_.resize(registers_.size() + 8u, value::undefined());

    call_frame frame;
    frame.proto = saved->proto;
    frame.ip = saved->ip;
    frame.base = base;
    frame.result_reg = 0;
    frame.argc = saved->argc;
    frame.closure = saved->closure;
    frame.receiver = saved->receiver;
    frame.handler_base = handlers_.size();
    frame.generator = saved;
    frames_.push_back(frame);
    const std::size_t index = frames_.size() - 1;
    for (handler restored : saved->handlers) {
        restored.frame = index;
        restored.reg_top += base; // relative while saved; absolute again here
        handlers_.push_back(restored);
    }
    saved->handlers.clear();

    // WHERE THE VALUE PASSED TO `.next(v)` LANDS: the destination register of
    // the `yield` that suspended, which is what makes `var x = yield y` see it.
    // NOT on the first resume - nothing is waiting for it there, and writing it
    // would clobber the frame's first local, which on a body with parameters is
    // an argument.
    if (saved->started) { registers_[base + saved->await_reg] = sent; }
    saved->started = true;

    const std::size_t stop = frames_.size() - 1;
    saved->running = true;
    yielded_ = false;

    if (how == resume_mode::thrown) {
        // THROW AT THE YIELD, so `try { yield x } catch` works across a real
        // suspension. __awaiter's rejection path is exactly this.
        thrown_ = sent;
        if (!unwind_to_handler()) {
            while (frames_.size() > stop) { frames_.pop_back(); }
            registers_.resize(base);
            saved->running = false;
            saved->done = true;
            return record(value::undefined(), true);
        }
    }

    const value produced = run_loop(stop);
    saved->running = false;

    if (yielded_) {
        yielded_ = false;
        return record(produced, false);
    }
    // The body returned, threw past its own handlers, or the VM failed. Any of
    // those finish the generator; a further `.next()` answers done for ever.
    saved->done = true;
    registers_.resize(base);
    return record(produced, true);
}

void context::resume(value coroutine, value with, bool rejected) {
    if (!coroutine.is_kind(heap_kind::coroutine) || failed_) { return; }
    auto * saved = static_cast<coroutine_object *>(coroutine.as_heap());

    const std::size_t base = registers_.size();
    registers_.insert(registers_.end(), saved->window.begin(), saved->window.end());
    // Slack above the window, for the same reason a call reserves it: an
    // expression allocates scratch registers past the frame's declared size.
    registers_.resize(registers_.size() + 8u, value::undefined());

    call_frame frame;
    frame.proto = saved->proto;
    frame.ip = saved->ip;
    frame.base = base;
    frame.result_reg = 0;
    frame.argc = saved->argc;
    frame.closure = saved->closure;
    frame.receiver = saved->receiver;
    frame.constructing = saved->constructing;
    frame.handler_base = handlers_.size();
    frame.async_promise = saved->promise;
    frames_.push_back(frame);
    const std::size_t index = frames_.size() - 1;
    for (handler restored : saved->handlers) {
        restored.frame = index;
        restored.reg_top += base; // relative while saved; absolute again here
        handlers_.push_back(restored);
    }

    registers_[base + saved->await_reg] = with;
    const std::size_t stop = frames_.size() - 1;
    suspended_ = false;

    // A REJECTED await THROWS AT THE AWAIT, so `try { await p } catch` works
    // across a real suspension and not only across a settled one.
    if (rejected) {
        thrown_ = with;
        if (!unwind_to_handler()) {
            // Nothing in this frame catches it: the function's own promise
            // rejects, which is what an async function does with an exception
            // it does not handle. The frame is already gone - unwind_to_handler
            // pops what it cannot satisfy - so there is nothing left to run.
            while (frames_.size() > stop) { frames_.pop_back(); }
            registers_.resize(base);
            thrown_ = value::undefined();
            promise_settler_(*this, saved->promise, with, true);
            drain_microtasks();
            return;
        }
    }

    const value returned = run_loop(stop);
    if (suspended_) {
        suspended_ = false;
        return; // it awaited again; its promise settles on some later resume
    }
    if (failed_) { return; }
    // The body finished. What it returns is already a settled promise, because
    // an async function's last act is `wrap_promise` - so its outcome is
    // ADOPTED rather than wrapped a second time.
    value outcome = returned;
    bool failed_outcome = false;
    if (returned.is_object()) {
        auto * obj = static_cast<object_object *>(returned.as_heap());
        if (obj->find("__settled") != nullptr) {
            value * held = obj->find("__value");
            value * state = obj->find("__rejected");
            outcome = held == nullptr ? value::undefined() : *held;
            failed_outcome = state != nullptr && truthy(*state);
        }
    }
    promise_settler_(*this, saved->promise, outcome, failed_outcome);
}

} // namespace ctbrowser::script
