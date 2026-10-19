//===- ctbrowser/lib/Script/vm/call/invoke.cpp - Calling into JavaScript --===//
//
// Every C++ entry into JavaScript, the diagnostics a failed call is described
// with, spread and rest arguments, and what an iterable yields. All functions
// here are members of `context`, declared in include/ctbrowser/script/vm.hpp.
//
//===----------------------------------------------------------------------===//

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

/// Calls a JS function from C++.
///
/// The two cases are genuinely different: a native is just a C++ call, while a
/// closure needs a frame on the interpreter's own stack. Giving the closure a
/// region above everything currently live is what lets this be re-entrant: the
/// caller's registers are untouched, so a listener that triggers another
/// listener works rather than corrupting the frame that dispatched it.
value context::call(value callable, std::span<const value> args, value this_value) {
    // Not called directly by a native (a getter reached from op::get_prop,
    // an event handler from the browser's tick, a setter hit by interpreted
    // code running under `apply`): the throw unwinds to the JavaScript handler
    // below at once; there is no native to return through before that
    // handler.
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

/// Calls \p callable under a fence handler, so a throw comes back through
/// \p threw and \p thrown instead of unwinding past the caller.
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
    // A re-entry refused after its episode's one RangeError returns undefined
    // with nothing thrown; every fenced caller would take that for the
    // callee's answer (an async-from-sync step casting it to its pair array).
    // Report it as the throw it stands for.
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

/// Every C++ entry into JavaScript ends up here: a DOM event, a timer, a
/// promise job, an animation frame, `Function.prototype.apply`, a getter, a
/// class field initialiser, `super()`. It handles natives and proxies, then
/// runs a closure's compiled body if it has one (enter_compiled) and falls
/// back to the interpreter otherwise.
value context::invoke(value callable, std::span<const value> args, value this_value,
                      bool constructing) {
    const bool super_call = std::exchange(super_call_, false);
    if (refuse_class_call(callable, constructing || super_call)) { return value::undefined(); }
    if (callable.is_kind(heap_kind::native)) {
        // A native pushes no frame, so the 512-frame ceiling below cannot see
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
        // Root the arguments for the call. From C++ they live only in the
        // caller's span (drain_microtasks pops a job's arguments before
        // invoking it), and a native that calls back into script can trigger a
        // collection. An interpreted callee has its arguments in registers; a
        // native has them in this vector.
        const rooted_values keep_args{*this, copy};
        const rooted keep_callee{*this, callable};
        const rooted keep_this{*this, this_value};
        // Everything is rooted now, so a heap past its threshold may collect
        // before the native runs. This is the same rule as the interpreter's
        // native call site (run_loop.cpp), and not a stress point for the same
        // reason.
        if (!gc_stress_ && live_objects_ >= collect_threshold_) [[unlikely]] {
            (void)collect_if_due();
        }
        const value saved = current_this_;
        current_this_ = this_value;
        note_transition_into_cxx(*this);
        // A native called by a native hands its result to a C++ local, as a
        // script callee does through callout_scope::returned. That result may
        // be outside the caller's pin range (a value read off a data property
        // the page can clear), so it is pinned the same way, for the rest of
        // the calling native's life.
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
    // A proxy's [[Call]] (10.5.12): the `apply` trap with (target, this,
    // argsArray) and the handler as `this`, or, with no trap, Call(target, ...),
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
    // Calling a generator function runs nothing; it only creates the
    // generator, as op::call does. This path matters because
    // Function.prototype.apply and .call land here, e.g. TypeScript's
    // __awaiter does `(generator = generator.apply(thisArg, args)).next()`.
    if (target.is_generator) { return make_generator(fnobj, this_value, args); }
    callout_scope callout{*this};

    const std::size_t new_base = registers_.size();
    // Every argument lands in a register, not just the declared ones, for two
    // reasons: a rest parameter reads the extra ones straight out of the
    // frame, and a value that is only in the caller's std::vector is not a GC
    // root, so a collection during the call would free an argument that is
    // about to be used.
    const std::size_t window = std::max<std::size_t>(target.frame_size, args.size());
    registers_.resize(new_base + window + 8u, value::undefined());
    for (std::size_t i = 0; i < std::max<std::size_t>(target.param_count, args.size()); ++i) {
        registers_[new_base + i] = i < args.size() ? args[i] : value::undefined();
    }
    // Safepoint: after the arguments are copied into the register window (the
    // caller's span is owned by the embedder and is not a GC root, so
    // collecting earlier would free them) and before anything runs. The
    // callee and receiver are rooted too, since their frame is not pushed
    // yet: a drained microtask's reaction or a JSON.parse reviver's wrapper
    // may be reachable from nothing else.
    const rooted keep_callee{*this, callable};
    const rooted keep_this{*this, this_value};
    safepoint();
    // Run the compiled body if this function has one. This comes after the
    // argument fill because the entry signature in aot/aot_entry.h expects
    // that register window, and before the depth check because ct_aot_enter
    // does its own depth check for compiled frames.
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
    // super() compiles to `op::apply`, which lands here, so the new.target a
    // super() call passed along is consumed here or nowhere.
    entered.new_target = pending_new_target_;
    pending_new_target_ = value::undefined();
    frames_.push_back(entered);
    const value out = run_loop(depth);
    current_this_ = saved;
    // Only shrink back if nothing below is still using the space; a nested
    // call that grew the stack further has already returned by now.
    if (registers_.size() >= new_base) { registers_.resize(new_base); }
    return callout.returned(out);
}

/// Describes where a plain call's callee came from. A method call knows its
/// name outright; `f(...)` only has a register, so this walks back through the
/// emitted code for the instruction that last wrote it. Costs nothing until
/// something fails, and turns "the value is undefined" into "`Symbol` is
/// undefined". \p ip is the index of the failing instruction itself (not the
/// post-incremented ip); the scan starts strictly before it, so the call
/// cannot match itself.
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
        // `f()(...)`: what was called is what the inner call returned, so name
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

/// Describes what was called, in the terms the source used. "attempted to
/// call a non-function" is true and useless; `o.foo is undefined, not a
/// function` says which line to look at. The method and computed forms know
/// the name outright; a plain call only knows what it found, which is still the
/// difference between "undefined" and "a number".
std::string context::describe_callee(const function_proto & fn, std::string_view name,
                                     value callee) {
    const std::string what =
        name.empty() ? std::string{"the value"} : "`" + std::string{name} + "`";
    // A primitive is spelled out; an object is not. ToString of an object
    // runs its own toString, which is page code, and page code must not run
    // while a TypeError is being built: it could throw a second TypeError
    // under the first (e.g. an object inheriting Function.prototype.toString),
    // which would consume the page's own catch and leave the first uncaught.
    const bool spell = !callee.is_undefined() && !callee.is_null() && !callee.is_object_like();
    return what + " is " + std::string{type_of(callee)} +
           (spell ? " (" + to_string(callee) + ")" : "") + ", not a function - in " +
           (fn.display_name().empty() ? std::string{"<anonymous>"} : "`" + fn.display_name() + "`");
}

/// Describes what was thrown, in the terms the thrower used. `to_string` on an
/// object is "[object Object]", which is the least useful thing a diagnostic
/// can say, and an uncaught throw is almost always an Error, whose name and
/// message are right there.
std::string context::describe_thrown(value thrown) {
    // Diagnostic getters may throw, catch, collect, or enter another script.
    // Keep the original exception independent of the mutable VM throw slots.
    const value saved[] = {thrown, thrown_, pending_throw_, fence_thrown_};
    const rooted_values keep{*this, saved};
    const throw_site site = throw_site_;
    const std::uint64_t site_value = throw_site_value_;
    const bool failed = std::exchange(failed_, false);
    std::string error = std::move(error_);
    error_.clear();
    const bool pending = std::exchange(has_pending_throw_, false);
    const bool fenced = std::exchange(fence_hit_, false);
    const std::size_t depth = frames_.size();
    std::string description;
    const value format = value::object(allocate<native_object>(
        "<exception diagnostic>", [thrown, &description](context & cx, std::span<value>) {
            if (thrown.is_object()) {
                auto * obj = static_cast<object_object *>(thrown.as_heap());
                const value name = cx.lookup_property(thrown, "name");
                const rooted keep_name{cx, name};
                if (cx.throw_pending()) { return value::undefined(); }
                const value message = cx.lookup_property(thrown, "message");
                const rooted keep_message{cx, message};
                if (cx.throw_pending()) { return value::undefined(); }
                if (!name.is_undefined() || !message.is_undefined()) {
                    const std::string label = name.is_undefined() ? "Error" : cx.to_string(name);
                    if (cx.throw_pending()) { return value::undefined(); }
                    const std::string text = cx.to_string(message);
                    if (!cx.throw_pending()) { description = label + ": " + text; }
                    return value::undefined();
                }
                std::string keys;
                obj->each_own_key([&](const std::string & key) {
                    if (keys.size() < 120) { keys += (keys.empty() ? "" : ", ") + key; }
                });
                description = "exception: an object {" + keys + "}";
            } else {
                const std::string text = cx.to_string(thrown);
                if (!cx.throw_pending()) { description = "exception: " + text; }
            }
            return value::undefined();
        }));
    bool threw = false;
    value ignored = value::undefined();
    (void)call_fenced(format, {}, value::undefined(), threw, ignored);
    // A VM fault, unlike a JavaScript throw, can leave diagnostic frames live.
    if (frames_.size() > depth) { frames_.resize(depth); }
    thrown_ = saved[1];
    pending_throw_ = saved[2];
    fence_thrown_ = saved[3];
    has_pending_throw_ = pending;
    fence_hit_ = fenced;
    throw_site_ = site;
    throw_site_value_ = site_value;
    failed_ = failed;
    error_ = std::move(error);
    return description.empty() ? "exception: <description unavailable>" : description;
}

/// GetMethod(handler, name) (7.3.10) with 10.5's steps 1-3 around it: a
/// revoked proxy (null handler) is the TypeError; the trap is read through
/// [[Get]], so one inherited from the handler's prototype or answered by a
/// getter counts (test262 drives most traps that way); null/undefined is "no
/// trap" and anything else that is not callable is the TypeError. The caller
/// must check throw_pending() before forwarding to the target: undefined here
/// means either "no trap" or "a throw is in flight".
value context::proxy_trap(value proxy, const std::string & name, bool * failed) {
    // `failed` is how a caller learns of a throw this function raised: a
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

/// Materialises \p v for a spread (`...v`), throwing a TypeError when it is
/// not iterable.
value context::spread_values(value v) {
    if (v.is_nullish() || (!v.is_heap() && !v.is_string())) {
        throw_error("TypeError", std::string{v.is_null()        ? "null"
                                             : v.is_undefined() ? "undefined"
                                                                : type_of(v)} +
                                     " is not iterable");
        return make_array();
    }
    return iterable_values(v, true);
}

/// Collects every value \p v yields into an array. Eager: an infinite source
/// stops at a step bound. \p spread makes a non-iterable a TypeError rather
/// than an empty list.
value context::iterable_values(value v, bool spread) {
    // Already an array: hand it straight back, so the common case allocates
    // nothing. Callers must not mutate what they get.
    if (v.is_array()) { return v; }
    if (v.is_string()) {
        // A string iterates by code point (22.1.5.1): a surrogate pair is one
        // element and a lone surrogate one of its own, exactly the WTF-8
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
    // A proxy is array-like through its traps. Every live DOM collection
    // (`el.children`, `getElementsByTagName`, `getElementsByClassName`,
    // `document.images`) is a proxy, because a collection has no fixed set of
    // properties and its `length` is a walk of the document rather than a
    // stored number. A proxy is not `is_object()`, so without this branch
    // `for (const x of el.children)` would read an empty list. `length` and
    // each index come from the `get` trap, which is where the walk lives.
    if (v.is_kind(heap_kind::proxy)) {
        const value length = lookup_property(v, "length");
        if (!length.is_number()) { return make_array(); }
        value out = make_array();
        const rooted keep{*this, out}; // Each `get` trap is page code and can collect.
        auto * items = static_cast<array_object *>(out.as_heap());
        const auto count = static_cast<std::size_t>(std::max(0.0, to_number(length)));
        for (std::size_t i = 0; i < count && i < 1u << 24; ++i) {
            items->items.push_back(lookup_property(v, std::to_string(i)));
        }
        return out;
    }
    if (!v.is_object()) { return make_array(); }
    auto * obj = static_cast<object_object *>(v.as_heap());
    // A generator is drained by running it. There is no `length` to loop over
    // and no array behind it; the values do not exist until the body is
    // resumed, once per value.
    //
    // This materialises, so for-of over an infinite generator stops at the
    // bound below rather than looping lazily. `op::iterable` hands back an
    // array by construction, so laziness would need a different opcode and a
    // real iterator protocol in the loop (see docs/script.md).
    if (value * co = obj->find(generator_slot_key);
        co != nullptr && co->is_kind(heap_kind::coroutine)) {
        value out = make_array();
        // Rooted across the resumes: the body can allocate, allocation can
        // collect, and the list under construction is reachable from nothing
        // else.
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
    // how their entries slot already holds them, so this is a copy and not a
    // rebuild.
    if (value * entries = obj->find(entries_slot_key); entries != nullptr && entries->is_array()) {
        value out = make_array();
        static_cast<array_object *>(out.as_heap())->items =
            static_cast<array_object *>(entries->as_heap())->items;
        return out;
    }
    // The views `keys()`, `values()` and `entries()` hand back are arrays
    // already, so this is for anything that carries its items under another
    // name.
    if (value * items = obj->find(iterated_list_key); items != nullptr && items->is_array()) {
        value out = make_array();
        static_cast<array_object *>(out.as_heap())->items =
            static_cast<array_object *>(items->as_heap())->items;
        return out;
    }
    // An iterable of the page's own (`[Symbol.iterator]() { ... }` on a class
    // or a literal), run through the protocol (7.4.3-7.4.8) and drained.
    // Everything above is the standard library's fast path for values whose
    // iterator is known; this is everything else, and it is eager like the
    // rest of this function: an infinite iterator here hits the bound below.
    // Not while this very value is being materialised through its own
    // @@iterator: the shell's collections answer `[Symbol.iterator]()` with
    // an iterator built from iterable_values(this), which would come straight
    // back here and recurse until the stack overflowed. Those fall through to
    // the array-like walk below.
    const bool materialising =
        std::find_if(materialising_.begin(), materialising_.end(),
                     [&](value held) { return held.bits() == v.bits(); }) != materialising_.end();
    // Every step below can throw. Called from the interpreter, a throw lands
    // on the page's handler immediately, so throw_pending() never sees it;
    // only a change in unwinds_ does (see context::unwinds() in vm.hpp). Stop
    // at the first throw: one more step, or a TypeError of our own on top,
    // would be a second throw that unwinds past the handler the first reached.
    const std::size_t unwound = unwinds_;
    const auto threw = [&] { return unwinds_ != unwound || throw_pending(); };
    // GetMethod once (7.3.10): a getter runs one time, and a method that is
    // there but not callable (`null` included) is the TypeError of
    // GetIterator. Only an absent one falls through to the array-like walk.
    const value method = lookup_property(v, CTBROWSER_SYMBOL_KEY("iterator"));
    if (threw()) { return make_array(); }
    if (!method.is_undefined() && !method.is_callable()) {
        throw_error("TypeError", std::string{type_of(v)} + " is not iterable");
        return make_array();
    }
    if (method.is_callable() && !materialising) {
        materialising_.push_back(v);
        value out = make_array();
        const rooted keep{*this, out}; // See the generator branch above.
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
                if (done) { break; } // The end, or a throw: iterator_step says done for both.
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
    // An array-like: anything with a numeric length and indexed properties, which
    // is what a NodeList, `arguments` and a page's own collection look like.
    if (value * length = obj->find("length"); length != nullptr && length->is_number()) {
        value out = make_array();
        const rooted keep{*this, out}; // An index can be a getter.
        auto * items = static_cast<array_object *>(out.as_heap());
        const auto count = static_cast<std::size_t>(std::max(0.0, to_number(*length)));
        for (std::size_t i = 0; i < count && i < 1u << 24; ++i) {
            items->items.push_back(lookup_property(v, std::to_string(i)));
        }
        return out;
    }
    // Nothing to iterate: no library slot, no @@iterator, no `length`. A
    // library caller takes that as an empty list; a spread is GetIterator's
    // TypeError (7.4.3), the same one for-of throws for the object, so
    // `[...{}]` throws.
    if (spread) { throw_error("TypeError", std::string{type_of(v)} + " is not iterable"); }
    return make_array();
}

/// GetIterator(v, sync) (7.4.3): returns \p v's iterator, or undefined after
/// a throw.
value context::get_iterator(value v) {
    // null and undefined first: lookup_property throws its own TypeError for
    // them, and the "not iterable" throw below would then be a second throw
    // after the first had already landed on the handler, which unwinds past
    // it and reports the pattern's TypeError as uncaught. One throw, the
    // right one (7.4.3 GetIterator -> GetMethod -> GetV -> ToObject).
    if (v.is_nullish()) {
        throw_error("TypeError",
                    std::string{v.is_null() ? "null" : "undefined"} + " is not iterable");
        return value::undefined();
    }
    const std::size_t unwound = unwinds_;
    const value method = lookup_property(v, CTBROWSER_SYMBOL_KEY("iterator"));
    if (unwinds_ != unwound || throw_pending()) { return value::undefined(); } // A getter threw.
    if (!method.is_callable()) {
        // A typed array, a Map, a Set or an array-like the library owns has
        // no @@iterator of its own here but is iterable all the same: its
        // values come from iterable_values, and the iterator is a native over
        // that list. An ordinary array and a string are not in this set:
        // both prototypes carry a real @@iterator, so reaching here
        // means a page deleted or replaced it, and `[x] = []` is then the
        // TypeError every engine throws (7.4.3). A number, a boolean or a
        // plain object is the TypeError too.
        const bool typed = v.is_array() &&
                           static_cast<array_object *>(v.as_heap())->elements != element_kind::none;
        const bool known = typed || v.is_kind(heap_kind::proxy) ||
                           (v.is_object() &&
                            (static_cast<object_object *>(v.as_heap())->find(entries_slot_key) ||
                             static_cast<object_object *>(v.as_heap())->find(iterated_list_key) ||
                             static_cast<object_object *>(v.as_heap())->find(generator_slot_key)));
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
        it->set("state", value::object(state)); // Keeps the list reachable.
        return iterator;
    }
    // When the interpreter called this, a throw from the call has already
    // landed on its handler (visible only through unwinds_); raising a
    // TypeError on top would be a second throw that unwinds past that handler.
    const value iterator = call(method, {}, v);
    if (unwinds_ != unwound || throw_pending()) { return value::undefined(); }
    // Any object (a proxy, a function, an array), not only an ordinary one
    // (7.4.2 GetIteratorFromMethod step 3: "is not an Object").
    if (!iterator.is_object_like()) {
        throw_error("TypeError", "Result of the Symbol.iterator method is not an object");
        return value::undefined();
    }
    return iterator;
}

/// IteratorStep plus IteratorValue: returns the next value, or sets \p done.
value context::iterator_step(value iterator, value next, bool & done) {
    // Done on a throw too, as IteratorStep leaves the record's [[Done]], so a
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
    // A non-array means no arguments, not one argument. That is what the
    // interpreter does and callers rely on it: `f(...undefined)` calls f with
    // nothing rather than with undefined.
    if (!arg_array.is_array()) { return {}; }
    return static_cast<array_object *>(arg_array.as_heap())->items;
}

/// Throws the TypeError for calling a class constructor without `new` and
/// returns true; returns false when the call is allowed.
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
        // Returns the callee so the destination register is left unchanged,
        // which matches VM_CASE(apply) in run_loop.cpp (it writes reg(in.a)
        // only on success, and reg(in.a) is where the callee came from).
        // raise() is an uncatchable engine fault, so nothing observes the
        // register; matching anyway keeps the interpreter and the compiled
        // code visibly identical.
        raise("attempted to call a non-function");
        return callee;
    }
    return call(callee, args, receiver);
}

/// Returns the realm's %ThrowTypeError% intrinsic (10.2.4.1), creating it on
/// first use.
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

/// Builds the `arguments` object for frame \p fr from its \p argc arguments.
value context::make_arguments_object(call_frame & fr, const value * slots, std::uint32_t argc) {
    // The frame knows how many arguments arrived; the proto only knows how many
    // were declared, and those are different numbers whenever `arguments` is
    // worth reading at all.
    value list = make_array();
    auto * items = static_cast<array_object *>(list.as_heap());
    items->items.reserve(argc);
    for (std::uint32_t i = 0; i < argc; ++i) { items->items.push_back(slots[i]); }
    // An arguments object is not an array (10.4.4). It is an array_object
    // here for its indices and `length` (type_record pins the kind), on
    // Object.prototype rather than Array.prototype, so `arguments.map` is
    // undefined as it is everywhere. It has @@iterator = Array.prototype.values
    // (10.4.4.6 steps 6/7), `callee` (the function when mapped; the
    // %ThrowTypeError% accessor when strict or the parameters are not simple,
    // 10.4.4.7 step 8), and a private marker Array.isArray and
    // Object.prototype.toString read. The parameter map of 10.4.4.6 is not
    // made: a write to `arguments[0]` does not reach the parameter.
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
    // Stored on the frame too, and inside this member rather than at the
    // call site: this claims a register an extra argument may be in, so
    // whatever still needs the raw ones reads them from here.
    fr.arguments_object = list;
    return list;
}

/// Builds the rest parameter's array from arguments \p from onward.
value context::gather_rest_values(const call_frame & fr, const value * slots, std::uint32_t argc,
                                  std::uint32_t from) {
    value out = make_array();
    auto * rest = static_cast<array_object *>(out.as_heap());
    // The arguments normally come from the slots, unless the body also built
    // an `arguments` object, which happens before this and claims a register
    // an extra argument may be in. Then the frame's copy is the one that
    // still has them.
    if (fr.arguments_object.is_array()) {
        const auto & held = static_cast<array_object *>(fr.arguments_object.as_heap())->items;
        for (std::size_t i = from; i < held.size(); ++i) { rest->items.push_back(held[i]); }
    } else {
        for (std::uint32_t i = from; i < argc; ++i) { rest->items.push_back(slots[i]); }
    }
    return out;
}

} // namespace ctbrowser::script
