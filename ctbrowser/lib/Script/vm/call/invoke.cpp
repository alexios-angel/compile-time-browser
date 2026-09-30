// ctbrowser.script context - calling: every C++ entry into JavaScript, the
// diagnostics a failed call is described with, spread and rest arguments, and
// what an iterable yields.
//
// One of five files carved out of a 1,171-line vm/call.cpp on 2026-09-08 -
// which was itself one of four carved out of a 3,232-line vm.cpp on
// 2026-08-09. All members of `context`, declared in
// include/ctbrowser/script/vm.hpp - so they split across translation units
// with nothing to declare.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <ctbrowser/script/vm.hpp>

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
    // NOT CALLED DIRECTLY BY A NATIVE - a getter reached from op::get_prop,
    // an event handler from the browser's tick, a setter hit by interpreted
    // code running under `apply`: the throw unwinds to the JavaScript handler
    // below at once, as it always did; there is no native to return through
    // before that handler.
    if (native_depth_ == 0 || frames_.size() != native_frames_) {
        return invoke(callable, args, this_value, /*constructing*/ false);
    }
    if (has_pending_throw_) { return value::undefined(); } // see the declaration
    bool threw = false;
    value thrown = value::undefined();
    const value out = call_fenced(callable, args, this_value, threw, thrown);
    if (threw) {
        has_pending_throw_ = true;
        pending_throw_ = thrown;
        return value::undefined();
    }
    return out;
}

value context::call_fenced(value callable, std::span<const value> args, value this_value,
                           bool & threw, value & thrown, bool constructing) {
    threw = false;
    thrown = value::undefined();
    handler fence;
    fence.frame = frames_.size();
    fence.reg_top = registers_.size();
    fence.fence = true;
    handlers_.push_back(fence);
    const std::size_t mark = handlers_.size();
    const bool outer_refused = std::exchange(reentry_refused_, false);
    const value out = invoke(callable, args, this_value, constructing);
    // A RE-ENTRY REFUSED AFTER ITS EPISODE'S ONE RangeError returns undefined
    // with nothing thrown; every fenced caller would take that for the
    // callee's answer (an async-from-sync step casting it to its pair array).
    // Reported as the throw it stands for.
    if (std::exchange(reentry_refused_, outer_refused) && !fence_hit_) {
        if (handlers_.size() >= mark) { handlers_.resize(mark - 1); }
        threw = true;
        thrown = make_error("RangeError", "Maximum call stack size exceeded");
        return value::undefined();
    }
    if (fence_hit_) {
        // unwind_to_handler popped the fence and everything above it.
        fence_hit_ = false;
        threw = true;
        thrown = fence_thrown_;
        fence_thrown_ = value::undefined();
        return value::undefined();
    }
    // A normal return: the callee's own handlers died with its frame, so
    // ours is on top again.
    if (handlers_.size() >= mark) { handlers_.resize(mark - 1); }
    return out;
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
    const bool super_call = std::exchange(super_call_, false);
    if (refuse_class_call(callable, constructing || super_call)) { return value::undefined(); }
    if (callable.is_kind(heap_kind::native)) {
        // A NATIVE PUSHES NO FRAME, so the 512-frame ceiling below cannot see
        // it: a native that re-enters the VM recurses on the C++ stack alone.
        // See context::reentry_scope.
        const reentry_scope guard{*this};
        if (guard.overflowed()) { return value::undefined(); }
        auto * nat = static_cast<native_object *>(callable.as_heap());
        // Bound wrappers forward the constructor handoff; real natives
        // consume it so their callbacks remain ordinary Calls.
        if (nat->find(CTBROWSER_PRIVATE_KEY("BoundTargetFunction")) == nullptr) {
            pending_new_target_ = value::undefined();
        }
        std::vector<value> copy{args.begin(), args.end()};
        // THE ARGUMENTS ARE ROOTED FOR THE CALL. From C++ they live in the
        // caller's span and nowhere else - drain_microtasks pops a job's
        // arguments before invoking it - and a native that calls back into
        // script collects: `deliver` held its handler record only here, the
        // collection inside the handler freed it, and the settle through the
        // dangling pointer corrupted the heap. An interpreted callee has its
        // arguments in registers; a native has them in this vector.
        const rooted_values keep_args{*this, copy};
        const rooted keep_callee{*this, callable};
        const rooted keep_this{*this, this_value};
        // Everything is rooted now, so a heap past its threshold may collect
        // before the native runs - the same rule as the interpreter's native
        // call site (run_loop.cpp), and not a stress point for the same reason.
        if (!gc_stress_ && live_objects_ >= collect_threshold_) [[unlikely]] {
            (void)collect_if_due();
        }
        const value saved = current_this_;
        current_this_ = this_value;
        note_transition_into_cxx(*this);
        // A NATIVE CALLED BY A NATIVE hands its result to a C++ local, as a
        // script callee does through callout_scope::returned - and that
        // result may be nothing the caller's pin range covers (a value read
        // off a data property the page can clear), so it is pinned the same
        // way, for the rest of the calling native's life.
        const bool from_native = native_depth_ > 0 && frames_.size() == native_frames_;
        const value out = [&] {
            const executing_as running{*this, executing_kind::cxx};
            const native_scope pinned{*this};
            return nat->fn(*this, copy);
        }();
        current_this_ = saved;
        if (from_native && out.is_heap()) { pinned_results_.push_back(out); }
        // A throw the native's own `call` parked leaves here, from the
        // native's call site: the enclosing fence or handler, or a fault.
        if (rethrow_pending()) { return value::undefined(); }
        return out;
    }
    // A PROXY'S [[Call]] (10.5.12): the `apply` trap with (target, this,
    // argsArray) and the handler as `this`, or - no trap - Call(target, ...),
    // which is how a proxy over a proxy over a bound function reaches it. A
    // revoked one is proxy_trap's TypeError. `typeof` already said "function"
    // for any proxy over a callable (value::is_callable); this is what makes
    // calling one agree, from `.call`/`.apply` and from op::call alike.
    if (callable.is_kind(heap_kind::proxy)) {
        auto * p = static_cast<proxy_object *>(callable.as_heap());
        if (!p->callable) { return value::undefined(); }
        const rooted keep_callable{*this, callable};
        const value new_target = std::exchange(pending_new_target_, value::undefined());
        const rooted keep_new_target{*this, new_target};
        bool failed = false;
        const value trap = proxy_trap(callable, "apply", &failed);
        if (failed || throw_pending()) { return value::undefined(); }
        if (!trap.is_callable()) {
            pending_new_target_ = new_target;
            return constructing ? invoke(p->target, args, this_value, true)
                                : call(p->target, args, this_value);
        }
        const rooted keep_trap{*this, trap};
        const value list = make_array();
        static_cast<array_object *>(list.as_heap())->items.assign(args.begin(), args.end());
        const value trap_args[3] = {p->target, this_value, list};
        return call(trap, trap_args, p->handler);
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
    callout_scope callout{*this};

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
    //
    // THE CALLEE AND THE RECEIVER ARE ROOTED TOO, because the frame that will
    // carry them is not pushed yet and this is no longer a stress-only
    // collection point. `drain_microtasks` pops a job before calling it, so a
    // settled promise's reaction can be reachable from nothing but `callable`
    // here; JSON.parse's reviver gets a wrapper that exists only in a C++
    // local as `this_value`. Two pushes on `temporaries_` per entry.
    const rooted keep_callee{*this, callable};
    const rooted keep_this{*this, this_value};
    safepoint();
    // A COMPILED BODY, IF THIS FUNCTION HAS ONE, asked in the same place the
    // interpreter asks. AFTER the argument fill, because that window is what
    // the ABI's entry row promises, and BEFORE the depth guard, because
    // ct_aot_enter owns that guard for a compiled frame.
    if (value produced = value::undefined(); enter_compiled(
            *this, target, callable, registers_.data() + new_base, new_base,
            static_cast<std::uint32_t>(args.size()), this_value, constructing, produced)) {
        if (registers_.size() >= new_base) { registers_.resize(new_base); }
        return callout.returned(produced);
    }
    if (frames_.size() > 512) {
        throw_error("RangeError", "Maximum call stack size exceeded");
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
    entered.realm = realm_for(fnobj);
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
    return callout.returned(out);
}

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

// WHAT was called, in the terms the source used. "attempted to call a
// non-function" is true and useless; `o.foo is undefined, not a function` says
// which line to look at. The method and computed forms know the name outright;
// a plain call only knows what it found, which is still the difference between
// "undefined" and "a number".
std::string context::describe_callee(const function_proto & fn, std::string_view name,
                                     value callee) {
    const std::string what =
        name.empty() ? std::string{"the value"} : "`" + std::string{name} + "`";
    // A PRIMITIVE IS SPELLED OUT; an object is not - ToString of an object
    // runs its own toString, which is page code, and page code must not run
    // while a TypeError is being built: an object inheriting
    // Function.prototype.toString (`new F()` where `F.prototype` is a
    // function) had that native throw a SECOND TypeError under the first,
    // which consumed the page's own catch and left the first uncaught.
    const bool spell = !callee.is_undefined() && !callee.is_null() && !callee.is_object_like();
    return what + " is " + std::string{type_of(callee)} +
           (spell ? " (" + to_string(callee) + ")" : "") + ", not a function - in " +
           (fn.display_name().empty() ? std::string{"<anonymous>"} : "`" + fn.display_name() + "`");
}

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

// GetMethod(handler, name) (7.3.10) with 10.5's step 1-3 around it: a revoked
// proxy (null handler) is the TypeError; the trap is read through [[Get]], so
// one inherited from the handler's prototype or answered by a getter counts
// (test262 drives most traps that way); null/undefined is "no trap" and
// anything else that is not callable is the TypeError. THE CALLER CHECKS
// throw_pending() before forwarding to the target: undefined here means
// either "no trap" or "a throw is in flight".
value context::proxy_trap(value proxy, const std::string & name, bool * failed) {
    // `failed` is how a caller learns of a throw THIS function raised: a
    // throw_error here has landed on the page's handler by the time it
    // returns, and throw_pending cannot see it (context::unwinds). A throw a
    // getter raised across lookup_property's call is parked, and throw_pending
    // does see that one; `failed` reports both.
    const std::size_t before = unwinds();
    const auto fail = [&] {
        if (failed != nullptr) { *failed = true; }
        return value::undefined();
    };
    auto * p = static_cast<proxy_object *>(proxy.as_heap());
    if (!p->handler.is_object_like()) {
        throw_error("TypeError", "Cannot perform '" + name + "' on a proxy that has been revoked");
        return fail();
    }
    const value trap = lookup_property(p->handler, name);
    if (throw_pending() || unwinds() != before) { return fail(); }
    if (trap.is_nullish()) { return value::undefined(); }
    if (!trap.is_callable()) {
        throw_error("TypeError", "proxy trap '" + name + "' is not a function");
        return fail();
    }
    return trap;
}

value context::spread_values(value v) {
    if (v.is_nullish() || (!v.is_heap() && !v.is_string())) {
        throw_error("TypeError", std::string{v.is_null()        ? "null"
                                             : v.is_undefined() ? "undefined"
                                                                : type_of(v)} +
                                     " is not iterable");
        return make_array();
    }
    return iterable_values(v);
}

value context::iterable_values(value v) {
    // Already an array: hand it straight back, so the common case allocates
    // nothing. Callers must not mutate what they get.
    if (v.is_array()) { return v; }
    if (v.is_string()) {
        // A string iterates by CODE POINT (22.1.5.1): a surrogate pair is one
        // element and a lone surrogate one of its own - exactly the WTF-8
        // sequences the text is made of, one decode_utf8 step each. Spread and
        // for-of both come here, so they agree.
        value out = make_array();
        auto * items = static_cast<array_object *>(out.as_heap());
        const std::string text = static_cast<string_object *>(v.as_heap())->text;
        for (std::size_t at = 0; at < text.size();) {
            const std::size_t from = at;
            (void)decode_utf8(text, at);
            items->items.push_back(string(text.substr(from, at - from)));
        }
        return out;
    }
    // A PROXY IS ARRAY-LIKE THROUGH ITS TRAPS, and this is not a nicety. Every
    // LIVE DOM collection is a proxy - `el.children`,
    // `getElementsByTagName`, `getElementsByClassName`, `document.images` -
    // because a collection has no fixed set of properties and its `length` is
    // a walk of the document rather than a stored number. A proxy is not
    // `is_object()`, so it fell off the end of this function and
    // `for (const x of el.children)` and `[...el.children]` each read an EMPTY
    // list: not an error, a wrong answer. `length` and each index come from the
    // `get` trap, which is exactly where the walk lives.
    if (v.is_kind(heap_kind::proxy)) {
        const value length = lookup_property(v, "length");
        if (!length.is_number()) { return make_array(); }
        value out = make_array();
        const rooted keep{*this, out}; // each `get` trap is page code, and can collect
        auto * items = static_cast<array_object *>(out.as_heap());
        const auto count = static_cast<std::size_t>(std::max(0.0, to_number(length)));
        for (std::size_t i = 0; i < count && i < 1u << 24; ++i) {
            items->items.push_back(lookup_property(v, std::to_string(i)));
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
        // ROOTED ACROSS THE RESUMES: the body can allocate, allocation can
        // collect, and the list under construction is reachable from nothing
        // else. (It was freed under the loop and the pushes corrupted the
        // heap - a SIGSEGV in a later free, nowhere near here.)
        const rooted keep{*this, out};
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
    // AN ITERABLE OF THE PAGE'S OWN - `[Symbol.iterator]() { ... }` on a
    // class or a literal - run through the protocol (7.4.3-7.4.8) and drained.
    // Everything above is the standard library's fast path for values whose
    // iterator is known; this is everything else, and it is eager like the
    // rest of this function: an infinite iterator here is the bound below.
    // NOT WHILE THIS VERY VALUE IS BEING MATERIALISED THROUGH ITS OWN
    // @@iterator: the shell's collections answer `[Symbol.iterator]()` with
    // an iterator built from iterable_values(this), which would come straight
    // back here and recurse until the stack went. Those fall through to the
    // array-like walk below, as they always did.
    const bool materialising =
        std::find_if(materialising_.begin(), materialising_.end(),
                     [&](value held) { return held.bits() == v.bits(); }) != materialising_.end();
    // EVERY STEP BELOW CAN THROW, and from the interpreter a throw lands on
    // the page's handler at once - throw_pending never sees it, only
    // unwinds_ does (contract-17). Whatever landed is the answer: one more
    // step, or a TypeError of our own on top, would be a second throw that
    // unwinds past the handler the first one reached.
    const std::size_t unwound = unwinds_;
    const auto threw = [&] { return unwinds_ != unwound || throw_pending(); };
    // GetMethod once (7.3.10): a getter runs one time, and a method that is
    // there but not callable - `null` included - is the TypeError of
    // GetIterator. Only an ABSENT one falls through to the array-like walk.
    const value method = lookup_property(v, CTBROWSER_SYMBOL_KEY("iterator"));
    if (threw()) { return make_array(); }
    if (!method.is_undefined() && !method.is_callable()) {
        throw_error("TypeError", std::string{type_of(v)} + " is not iterable");
        return make_array();
    }
    if (method.is_callable() && !materialising) {
        materialising_.push_back(v);
        value out = make_array();
        const rooted keep{*this, out}; // see the generator branch above
        const value iterator = call(method, {}, v);
        const rooted keep_iterator{*this, iterator};
        // 7.4.2 GetIteratorFromMethod step 3: any Object, a proxy or a
        // function included.
        if (!threw() && !iterator.is_object_like()) {
            throw_error("TypeError", "Result of the Symbol.iterator method is not an object");
        }
        const value next = threw() ? value::undefined() : lookup_property(iterator, "next");
        const rooted keep_next{*this, next};
        if (!threw()) {
            auto * items = static_cast<array_object *>(out.as_heap());
            std::size_t guard = 0;
            for (; guard < 1u << 20; ++guard) {
                bool done = false;
                const value item = iterator_step(iterator, next, done);
                if (done) { break; } // the end, or a throw - iterator_step says done for both
                items->items.push_back(item);
            }
            // An iterator that never finishes is an infinite loop under an
            // eager materialisation; a diagnosable throw beats an
            // out-of-memory kill (test262's for-of IteratorClose tests are
            // exactly this shape: `next` never says done and the body breaks).
            if (guard == 1u << 20 && !threw()) {
                throw_error("RangeError", "iterator did not finish in 1,048,576 steps - "
                                          "for-of materialises its source (docs/script.md)");
            }
        }
        materialising_.pop_back();
        return out;
    }
    // An ARRAY-LIKE: anything with a numeric length and indexed properties, which
    // is what a NodeList, `arguments` and a page's own collection look like.
    if (value * length = obj->find("length"); length != nullptr && length->is_number()) {
        value out = make_array();
        const rooted keep{*this, out}; // an index can be a getter
        auto * items = static_cast<array_object *>(out.as_heap());
        const auto count = static_cast<std::size_t>(std::max(0.0, to_number(*length)));
        for (std::size_t i = 0; i < count && i < 1u << 24; ++i) {
            items->items.push_back(lookup_property(v, std::to_string(i)));
        }
        return out;
    }
    return make_array();
}

value context::get_iterator(value v) {
    // null AND undefined FIRST: lookup_property throws its own TypeError for
    // them, and the "not iterable" throw below would then be a SECOND throw
    // after the first had already landed on the handler - which unwinds past
    // it and reports the pattern's TypeError as uncaught. One throw, the
    // right one (7.4.3 GetIterator -> GetMethod -> GetV -> ToObject).
    if (v.is_nullish()) {
        throw_error("TypeError",
                    std::string{v.is_null() ? "null" : "undefined"} + " is not iterable");
        return value::undefined();
    }
    const std::size_t unwound = unwinds_;
    const value method = lookup_property(v, CTBROWSER_SYMBOL_KEY("iterator"));
    if (unwinds_ != unwound || throw_pending()) { return value::undefined(); } // a getter threw
    if (!method.is_callable()) {
        // A TYPED ARRAY, A Map, A Set OR AN ARRAY-LIKE THE LIBRARY OWNS has
        // no @@iterator of its own here but is iterable all the same: its
        // values come from iterable_values, and the iterator is a native over
        // that list. An ordinary array and a string are NOT in this set any
        // more: both prototypes carry a real @@iterator, so reaching here
        // means a page deleted or replaced it, and `[x] = []` is then the
        // TypeError every engine throws (7.4.3). A number, a boolean or a
        // plain object is the TypeError too.
        const bool typed = v.is_array() &&
                           static_cast<array_object *>(v.as_heap())->elements != element_kind::none;
        const bool known =
            typed || v.is_kind(heap_kind::proxy) ||
            (v.is_object() && (static_cast<object_object *>(v.as_heap())->find("__entries") ||
                               static_cast<object_object *>(v.as_heap())->find("__items") ||
                               static_cast<object_object *>(v.as_heap())->find("__co")));
        if (!known) {
            throw_error("TypeError", std::string{type_of(v)} + " is not iterable");
            return value::undefined();
        }
        const value items = iterable_values(v);
        if (unwinds_ != unwound || throw_pending()) { return value::undefined(); }
        const rooted keep{*this, items};
        auto * state = static_cast<object_object *>(make_object().as_heap());
        const rooted keep_state{*this, value::object(state)};
        state->set("items", items);
        state->set("at", value::number(0));
        const value iterator = make_object();
        auto * it = static_cast<object_object *>(iterator.as_heap());
        it->set(
            "next",
            value::object(allocate<native_object>("next", [state](context & c, std::span<value>) {
                const value list = *state->find("items");
                auto * arr = static_cast<array_object *>(list.as_heap());
                const auto at = static_cast<std::size_t>(state->find("at")->as_number());
                const bool done = at >= arr->items.size();
                const value out = c.iter_result(done ? value::undefined() : arr->items[at], done);
                if (!done) { state->set("at", value::number(static_cast<double>(at + 1))); }
                return out;
            })));
        it->set("state", value::object(state)); // keeps the list reachable
        return iterator;
    }
    // The call's throw has LANDED when the interpreter is what asked (the
    // run loop's handler took it, contract-17) - a TypeError on top of it
    // would be a second throw, unwinding past that handler.
    const value iterator = call(method, {}, v);
    if (unwinds_ != unwound || throw_pending()) { return value::undefined(); }
    // ANY OBJECT - a proxy, a function, an array - not only an ordinary one
    // (7.4.2 GetIteratorFromMethod step 3: "is not an Object").
    if (!iterator.is_object_like()) {
        throw_error("TypeError", "Result of the Symbol.iterator method is not an object");
        return value::undefined();
    }
    return iterator;
}

value context::iterator_step(value iterator, value next, bool & done) {
    // DONE ON A THROW TOO, as IteratorStep leaves the record's [[Done]] - so a
    // caller's loop stops whether the throw is pending or has already landed
    // on an interpreted handler, which throw_pending cannot see (the unwinds_
    // test below is what can).
    done = true;
    if (!next.is_callable()) {
        throw_error("TypeError", "iterator.next is not a function");
        return value::undefined();
    }
    const std::size_t unwound = unwinds_;
    const auto threw = [&] { return unwinds_ != unwound || throw_pending(); };
    const value result = call(next, {}, iterator);
    if (threw()) { return value::undefined(); }
    if (!result.is_object_like()) {
        throw_error("TypeError", "Iterator result is not an object");
        return value::undefined();
    }
    const value finished = lookup_property(result, "done");
    if (threw() || truthy(finished)) { return value::undefined(); }
    const value item = lookup_property(result, "value");
    if (threw()) { return value::undefined(); }
    done = false;
    return item;
}

std::vector<value> context::spread_arguments(value arg_array) {
    // A NON-ARRAY IS NO ARGUMENTS, NOT ONE ARGUMENT. That is what the
    // interpreter does and it is load-bearing: `f(...undefined)` calls f with
    // nothing rather than with undefined.
    if (!arg_array.is_array()) { return {}; }
    return static_cast<array_object *>(arg_array.as_heap())->items;
}

bool context::refuse_class_call(value callee, bool super_call) {
    if (super_call || !callee.is_kind(heap_kind::function) || !pending_new_target_.is_undefined()) {
        return false;
    }
    const auto * closure = static_cast<const closure_object *>(callee.as_heap());
    if (!closure->class_constructor) { return false; }
    const std::string name = closure->proto != nullptr ? closure->proto->display_name() : "";
    throw_error("TypeError", "Class constructor " +
                                 (name.empty() ? std::string{"(anonymous)"} : name) +
                                 " cannot be invoked without 'new'");
    return true;
}

value context::call_spread(value callee, value arg_array, value receiver) {
    const std::vector<value> args = spread_arguments(arg_array);
    if (!callee.is_callable()) {
        super_call_ = false;
        // AND THE DESTINATION IS LEFT ALONE, which is why this answers the
        // callee rather than undefined. VM_CASE(apply) writes reg(in.a) only in
        // its else branch, and reg(in.a) is where the callee came from - so
        // returning it reproduces "unchanged" exactly. The raise is the
        // uncatchable tier, so nothing observes the register either way; it is
        // written this way so the two tiers cannot be READ as differing.
        raise("attempted to call a non-function");
        return callee;
    }
    return call(callee, args, receiver);
}

value context::throw_type_error() {
    value & held = current_realm().intrinsics->throw_type_error;
    if (!held.is_undefined()) { return held; }
    auto * made = allocate<native_object>("", [](context & c, std::span<value>) {
        c.throw_error("TypeError", "'caller', 'callee', and 'arguments' properties may not be "
                                   "accessed on strict mode functions or the arguments objects "
                                   "for calls to them");
        return value::undefined();
    });
    made->is_constructor = false;
    // 10.2.4.1 steps 2-4: length 0 and name "" both { false, false, false },
    // in that order, and [[Extensible]] false.
    made->define("length", value::number(0), attr_none);
    made->define("name", string(""), attr_none);
    made->extensible = false;
    held = value::object(made);
    return held;
}

value context::make_arguments_object(call_frame & fr, const value * slots, std::uint32_t argc) {
    // The FRAME knows how many arguments ARRIVED; the proto only knows how many
    // were declared, and those are different numbers whenever `arguments` is
    // worth reading at all.
    value list = make_array();
    auto * items = static_cast<array_object *>(list.as_heap());
    items->items.reserve(argc);
    for (std::uint32_t i = 0; i < argc; ++i) { items->items.push_back(slots[i]); }
    // AN ARGUMENTS OBJECT IS NOT AN ARRAY (10.4.4): an array_object here for
    // its indices and `length` (type_record pins the kind), on Object.prototype
    // rather than Array.prototype - so `arguments.map` is undefined, as it is
    // everywhere - with @@iterator = Array.prototype.values (10.4.4.6 step
    // 6/7), `callee` (the function when mapped; the %ThrowTypeError% accessor
    // when strict or the parameters are not simple, 10.4.4.7 step 8), and a
    // private marker Array.isArray and Object.prototype.toString read. The
    // parameter MAP of 10.4.4.6 is not made: a write to `arguments[0]` does
    // not reach the parameter.
    if (object_object * table = prototype(proto_kind::object)) {
        items->prototype = value::object(table);
    }
    object_object & named = items->named_table();
    named.define(CTBROWSER_PRIVATE_KEY("Arguments"), value::boolean(true), attr_none);
    if (object_object * table = prototype(proto_kind::array)) {
        if (value * values = table->find("values")) {
            named.define(CTBROWSER_SYMBOL_KEY("iterator"), *values,
                         attr_writable | attr_configurable);
        }
    }
    const function_proto * proto = fr.proto;
    const bool unmapped =
        proto == nullptr || proto->is_strict || proto->length != proto->param_count;
    if (unmapped) {
        named.define_accessor("callee", throw_type_error(), throw_type_error(), attr_none);
    } else if (fr.closure != nullptr) {
        named.define("callee", value::object(fr.closure), attr_writable | attr_configurable);
    }
    // ON THE FRAME TOO, and inside this member rather than at the call: this
    // claims a register an extra argument may be in, so whatever still needs
    // the raw ones reads them from here.
    fr.arguments_object = list;
    return list;
}

value context::gather_rest_values(const call_frame & fr, const value * slots, std::uint32_t argc,
                                  std::uint32_t from) {
    value out = make_array();
    auto * rest = static_cast<array_object *>(out.as_heap());
    // UNLESS THE BODY ALSO BUILT AN `arguments` OBJECT, which happens before
    // this and claims a register an extra argument may be in. Then the frame's
    // copy is the one that still has them.
    if (fr.arguments_object.is_array()) {
        const auto & held = static_cast<array_object *>(fr.arguments_object.as_heap())->items;
        for (std::size_t i = from; i < held.size(); ++i) { rest->items.push_back(held[i]); }
    } else {
        for (std::uint32_t i = from; i < argc; ++i) { rest->items.push_back(slots[i]); }
    }
    return out;
}

} // namespace ctbrowser::script
