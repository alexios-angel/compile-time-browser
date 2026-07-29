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

#include <ctbrowser/script/vm.hpp>

// The VM's implementation.
//
// `run_loop` alone is 15 KB of object code - the whole instruction dispatch -
// and while it lived in the interface every translation unit that imported the
// module emitted its own copy and optimised it again. The class declaration
// stays in :vm; the bodies live here and are compiled once.

namespace ctbrowser::script {

bool context::truthy(value v) {
    if (v.is_boolean()) { return v.as_boolean(); }
    if (v.is_nullish()) { return false; }
    if (v.is_number()) {
        const double d = v.as_number();
        return d != 0 && !std::isnan(d);
    }
    if (v.is_string()) { return !static_cast<string_object *>(v.as_heap())->text.empty(); }
    return true; // every other object is truthy
}

double context::to_number(value v) {
    if (v.is_number()) { return v.as_number(); }
    if (v.is_boolean()) { return v.as_boolean() ? 1 : 0; }
    if (v.is_null()) { return 0; }
    if (v.is_undefined()) { return std::nan(""); }
    if (v.is_string()) {
        const std::string & s = static_cast<string_object *>(v.as_heap())->text;
        try {
            std::size_t consumed = 0;
            const double d = std::stod(s, &consumed);
            while (consumed < s.size() && (s[consumed] == ' ' || s[consumed] == '\t')) {
                ++consumed;
            }
            return consumed == s.size() ? d : std::nan("");
        } catch (...) {
            return s.find_first_not_of(" \t\n\r") == std::string::npos ? 0.0 : std::nan("");
        }
    }
    return std::nan("");
}

std::string context::to_string(value v) {
    if (v.is_undefined()) { return "undefined"; }
    if (v.is_null()) { return "null"; }
    if (v.is_boolean()) { return v.as_boolean() ? "true" : "false"; }
    if (v.is_number()) {
        const double d = v.as_number();
        if (std::isnan(d)) { return "NaN"; }
        if (std::isinf(d)) { return d > 0 ? "Infinity" : "-Infinity"; }
        // integral doubles print without a decimal point, as JS does
        if (d == static_cast<double>(static_cast<std::int64_t>(d)) && std::abs(d) < 1e15) {
            return std::to_string(static_cast<std::int64_t>(d));
        }
        std::string out = std::to_string(d);
        while (out.size() > 1 && out.back() == '0') { out.pop_back(); }
        if (!out.empty() && out.back() == '.') { out.pop_back(); }
        return out;
    }
    if (v.is_string()) { return static_cast<string_object *>(v.as_heap())->text; }
    // The KEY, not the description: this is what makes `o[sym]` reach a slot
    // no literal can name, since a computed property goes through here.
    if (v.is_kind(heap_kind::symbol)) { return static_cast<symbol_object *>(v.as_heap())->key; }
    if (v.is_array()) {
        auto * arr = static_cast<array_object *>(v.as_heap());
        std::string out;
        for (std::size_t i = 0; i < arr->items.size(); ++i) {
            if (i != 0) { out += ','; }
            if (!arr->items[i].is_nullish()) { out += to_string(arr->items[i]); }
        }
        return out;
    }
    if (v.is_callable()) { return "function"; }
    return "[object Object]";
}

std::string_view context::type_of(value v) {
    if (v.is_kind(heap_kind::symbol)) { return "symbol"; }
    if (v.is_undefined()) { return "undefined"; }
    if (v.is_null()) { return "object"; } // the famous wart, preserved
    if (v.is_boolean()) { return "boolean"; }
    if (v.is_number()) { return "number"; }
    if (v.is_string()) { return "string"; }
    if (v.is_callable()) { return "function"; }
    return "object";
}

bool context::loose_equals(value a, value b) {
    if (a.is_nullish() && b.is_nullish()) { return true; }
    if (a.is_nullish() || b.is_nullish()) { return false; }
    if (a.is_number() && b.is_number()) { return a.as_number() == b.as_number(); }
    if (a.is_string() && b.is_string()) {
        return static_cast<string_object *>(a.as_heap())->text ==
               static_cast<string_object *>(b.as_heap())->text;
    }
    if (a.is_heap() && b.is_heap()) { return a == b; }
    return to_number(a) == to_number(b); // the coercing cases
}

// ===================== gc ================================================

void context::mark_object(heap_object * o) {
    if (o == nullptr || o->marked) { return; }
    o->marked = true;
    switch (o->kind) {
    case heap_kind::array: {
        auto * arr = static_cast<array_object *>(o);
        for (const value & v : arr->items) { mark(v); }
        mark(arr->index);
        mark(arr->input);
        mark(arr->groups);
        break;
    }
    case heap_kind::object: {
        auto * obj = static_cast<object_object *>(o);
        for (const auto & [name, v] : obj->props) { mark(v); }
        for (const accessor_entry & entry : obj->accessors.entries) {
            mark(entry.getter);
            mark(entry.setter);
        }
        mark(obj->prototype);
        break;
    }
    case heap_kind::cell: mark(static_cast<cell_object *>(o)->slot); break;
    case heap_kind::function: {
        auto * closure = static_cast<closure_object *>(o);
        // A closure OWNS its upvalue cells. Missing this frees a captured
        // variable while the closure that captured it is still reachable.
        for (const value & up : closure->upvalues) { mark(up); }
        // ...and its own properties, which is where a class keeps its statics
        // and its prototype.
        for (const auto & [name, v] : closure->props) { mark(v); }
        for (const accessor_entry & entry : closure->accessors.entries) {
            mark(entry.getter);
            mark(entry.setter);
        }
        // ...and an arrow's captured `this`, which nothing else can reach.
        mark(closure->captured_this);
        mark(closure->proto_link);
        break;
    }
    case heap_kind::native:
        for (const auto & [name, v] : static_cast<native_object *>(o)->props) { mark(v); }
        break;
    case heap_kind::proxy: {
        auto * proxy = static_cast<proxy_object *>(o);
        mark(proxy->target);
        mark(proxy->handler);
        break;
    }
    default: break; // strings and symbols own no values
    }
}

void context::mark(value v) {
    if (v.is_heap()) { mark_object(v.as_heap()); }
}

value context::lookup_index(value target, value key) {
    if (target.is_array() && key.is_number()) {
        auto * arr = static_cast<array_object *>(target.as_heap());
        const auto i = static_cast<std::ptrdiff_t>(key.as_number());
        if (i >= 0 && static_cast<std::size_t>(i) < arr->items.size()) {
            return arr->items[static_cast<std::size_t>(i)];
        }
        return value::undefined();
    }
    if (target.is_string() && key.is_number()) {
        const std::string & text = static_cast<string_object *>(target.as_heap())->text;
        const auto i = static_cast<std::size_t>(key.as_number());
        return i < text.size() ? string(std::string{text[i]}) : value::undefined();
    }
    return lookup_property(target, to_string(key));
}

void context::store_property(value target, const std::string & name, value v) {
    // A proxy's `set` trap first: it is the only thing that can decide the
    // write does not land on the target at all, which is the point of it.
    if (target.is_kind(heap_kind::proxy)) {
        auto * p = static_cast<proxy_object *>(target.as_heap());
        const value trap = proxy_trap(target, "set");
        if (trap.is_callable()) {
            const value args[4] = {p->target, string(name), v, target};
            (void)call(trap, args, p->handler);
            return;
        }
        store_property(p->target, name, v);
        return;
    }
    if (target.is_object()) {
        if (!assign_through_accessor(target, name, v)) {
            static_cast<object_object *>(target.as_heap())->set(name, v);
        }
        return;
    }
    if (target.is_kind(heap_kind::native)) {
        static_cast<native_object *>(target.as_heap())->set(name, v);
        return;
    }
    if (target.is_kind(heap_kind::function)) {
        auto * closure = static_cast<closure_object *>(target.as_heap());
        if (accessor_entry * entry = closure->find_accessor(name);
            entry != nullptr && entry->setter.is_callable()) {
            const value args[1] = {v};
            (void)call(entry->setter, args, target);
        } else {
            closure->set(name, v);
        }
    }
    // A write to a number, a string or undefined is silently dropped, which is
    // what non-strict JavaScript does.
}

bool context::assign_through_accessor(value target, const std::string & name, value v) {
    if (!target.is_object()) { return false; }
    auto * obj = static_cast<object_object *>(target.as_heap());
    for (int depth = 0; obj != nullptr && depth < 64; ++depth) {
        // An own DATA property wins outright: it is the same property, and it
        // is not an accessor, so nothing on the prototype gets a say.
        if (obj->find(name) != nullptr) { return false; }
        if (accessor_entry * entry = obj->find_accessor(name)) {
            if (entry->setter.is_callable()) {
                const value args[1] = {v};
                (void)call(entry->setter, args, target);
                return true;
            }
            // Getter with no setter: the write is DISCARDED, as in strict-mode
            // JavaScript minus the throw. Silently defining a data property
            // over it would shadow the getter forever.
            return true;
        }
        obj = obj->prototype.is_object() ? static_cast<object_object *>(obj->prototype.as_heap())
                                         : nullptr;
    }
    return false;
}

value context::lookup_property(value target, const std::string & name) {
    // A PROXY ANSWERS FIRST, or hands the question to its target. This sits at
    // the top because a proxy's whole purpose is to be asked before anything
    // else looks at the object underneath it.
    if (target.is_kind(heap_kind::proxy)) {
        auto * p = static_cast<proxy_object *>(target.as_heap());
        const value trap = proxy_trap(target, "get");
        if (trap.is_callable()) {
            const value args[3] = {p->target, string(name), target};
            return call(trap, args, p->handler);
        }
        return lookup_property(p->target, name);
    }
    // Own properties first: a page that writes `arr.length = 0` or shadows a
    // method on one object must not be overridden by the prototype.
    if (target.is_object()) {
        // Own properties, then the object's OWN prototype chain (what a class
        // instance uses to find its methods), then the shared table. A depth
        // cap because a page can make the chain cyclic and a lookup must not
        // hang because of it.
        auto * obj = static_cast<object_object *>(target.as_heap());
        for (int depth = 0; obj != nullptr && depth < 64; ++depth) {
            if (value * found = obj->find(name)) { return *found; }
            // An accessor found anywhere on the chain is CALLED, with the
            // original target as its receiver - a getter defined on a prototype
            // reads the instance, which is the entire point of putting one
            // there. The has_accessors test is why this costs nothing on the
            // objects that have none, which is nearly all of them.
            if (accessor_entry * entry = obj->find_accessor(name)) {
                if (entry->getter.is_callable()) {
                    return call(entry->getter, std::span<const value>{}, target);
                }
                return value::undefined(); // set-only: reading gives undefined
            }
            obj = obj->prototype.is_object()
                      ? static_cast<object_object *>(obj->prototype.as_heap())
                      : nullptr;
        }
        if (object_object * table = prototype(proto_kind::object)) {
            if (value * found = table->find(name)) { return *found; }
        }
        return value::undefined();
    }
    if (target.is_array()) {
        auto * arr = static_cast<array_object *>(target.as_heap());
        if (name == "length") { return value::number(static_cast<double>(arr->items.size())); }
        if (arr->is_match) { // an exec() result carries index/input/groups
            if (name == "index") { return arr->index; }
            if (name == "input") { return arr->input; }
            if (name == "groups") { return arr->groups; }
        }
        // A TYPED array's own methods first, then every array's, then every
        // object's - which is the chain JavaScript actually has, and the reason
        // `[1,2].hasOwnProperty(...)` and `bytes.subarray(...)` both work.
        if (arr->elements != element_kind::none) {
            if (object_object * table = prototype(proto_kind::typed_array)) {
                if (value * found = table->find(name)) { return *found; }
            }
        }
        if (object_object * table = prototype(proto_kind::array)) {
            if (value * found = table->find(name)) { return *found; }
        }
        if (object_object * table = prototype(proto_kind::object)) {
            if (value * found = table->find(name)) { return *found; }
        }
        return value::undefined();
    }
    if (target.is_string()) {
        auto * str = static_cast<string_object *>(target.as_heap());
        if (name == "length") { return value::number(static_cast<double>(str->text.size())); }
        if (object_object * table = prototype(proto_kind::string)) {
            if (value * found = table->find(name)) { return *found; }
        }
        return value::undefined();
    }
    if (target.is_number()) {
        if (object_object * table = prototype(proto_kind::number)) {
            if (value * found = table->find(name)) { return *found; }
        }
        return value::undefined();
    }
    // A boolean is a value with methods too. `flag.toString()` is what a
    // template literal and a string concatenation both do underneath, and code
    // that calls it explicitly - to build a cache key, say - found nothing.
    if (target.is_boolean()) {
        if (object_object * table = prototype(proto_kind::boolean)) {
            if (value * found = table->find(name)) { return *found; }
        }
        return value::undefined();
    }
    if (target.is_kind(heap_kind::native)) {
        if (value * found = static_cast<native_object *>(target.as_heap())->find(name)) {
            return *found;
        }
        // ...then Function.prototype, so `nativeFn.call(...)` works too.
        if (object_object * table = prototype(proto_kind::function)) {
            if (value * found = table->find(name)) { return *found; }
        }
        return value::undefined();
    }
    if (target.is_kind(heap_kind::symbol)) {
        auto * sym = static_cast<symbol_object *>(target.as_heap());
        if (name == "description") { return string(sym->description); }
        if (object_object * table = prototype(proto_kind::symbol)) {
            if (value * found = table->find(name)) { return *found; }
        }
        return value::undefined();
    }
    if (target.is_kind(heap_kind::function)) {
        auto * closure = static_cast<closure_object *>(target.as_heap());
        if (value * found = closure->find(name)) { return *found; }
        if (name == "prototype") { return ensure_prototype(target); }
        // `f.name` and `f.length`, read off the compiled function rather than
        // stored on every closure - most functions are never asked and an extra
        // two properties each is a real cost on a 4,754-function bundle.
        //
        // `name` is not cosmetic: identifying a value by
        // `Object.getPrototypeOf(x).constructor.name` is the standard walk that
        // works where instanceof does not, and an undefined name compares equal
        // to the other undefined it is being tested against - so a nameless
        // class reported a MATCH against anything else with no name.
        if (closure->proto != nullptr) {
            if (name == "name") { return string(closure->proto->name); }
            if (name == "length") { return value::number(closure->proto->param_count); }
        }
        // `static get w()` on a class - the constructor IS the closure, so its
        // accessors live here rather than on any object.
        if (accessor_entry * entry = closure->find_accessor(name)) {
            if (entry->getter.is_callable()) {
                return call(entry->getter, std::span<const value>{}, target);
            }
            return value::undefined();
        }
        // STATIC INHERITANCE: `class D extends B` makes D's own [[Prototype]]
        // B, so `D.staticMethod` finds B's. Babel wires this by hand with
        // _setPrototypeOf, and a real `extends` should do the same.
        for (value up = closure->proto_link; up.is_callable();) {
            if (up.is_kind(heap_kind::function)) {
                auto * parent = static_cast<closure_object *>(up.as_heap());
                if (value * found = parent->find(name)) { return *found; }
                up = parent->proto_link;
                continue;
            }
            if (value * found = static_cast<native_object *>(up.as_heap())->find(name)) {
                return *found;
            }
            break;
        }
        // A FUNCTION IS AN OBJECT WITH A PROTOTYPE OF ITS OWN. `call`, `apply`
        // and `bind` live there, and p5.js cannot install a single event
        // listener without bind.
        if (object_object * table = prototype(proto_kind::function)) {
            if (value * found = table->find(name)) { return *found; }
        }
    }
    return value::undefined();
}

std::size_t context::collect() {
    // Precise roots: everything reachable is reachable from exactly these.
    for (const auto & [name, v] : globals_) { mark(v); }
    for (const value & v : registers_) { mark(v); }
    // The receiver of a native call in progress. It is held in a C++ local, not
    // in a register, so nothing else would keep it alive - and collecting the
    // object a method is running on is about as bad as it gets.
    mark(current_this_);
    // And the closure each live frame is executing. A function called from C++
    // via call() is likewise only referenced from a C++ local; without this its
    // upvalues can be freed while its body is still running.
    for (const call_frame & f : frames_) {
        if (f.closure != nullptr) { mark_object(f.closure); }
        mark(f.receiver);
    }
    // A QUEUED JOB AND ITS ARGUMENTS. Nothing else refers to them between the
    // moment they are queued and the moment they run, which is precisely the
    // window a collection can fall in.
    for (const microtask & job : microtasks_) {
        mark(job.fn);
        for (const value & arg : job.args) { mark(arg); }
    }
    // A thrown value in flight is reachable from nothing else.
    mark(thrown_);
    // The prototype tables hold every builtin method. Nothing else references
    // them, so without this the standard library is collected on the first gc.
    for (object_object * table : prototypes_) {
        if (table != nullptr) { mark_object(table); }
    }
    // The per-function string cache. These are live `value`s held by the
    // context itself and referenced from nowhere else - a sweep without them
    // frees a string literal that a running loop is about to read again.
    for (auto & [proto, cache] : string_cache_) {
        for (auto & [index, v] : cache) { mark(v); }
    }
    // And whatever the embedder holds: every DOM listener, timer callback and
    // element wrapper lives in the bindings, not in any VM structure.
    if (external_roots_) {
        external_roots_([this](value v) { mark(v); });
    }

    std::size_t freed = 0;
    heap_object ** link = &heap_;
    while (*link != nullptr) {
        heap_object * o = *link;
        if (o->marked) {
            o->marked = false; // clear for the next cycle
            link = &o->next;
        } else {
            *link = o->next;
            delete o;
            ++freed;
            --live_objects_;
        }
    }
    return freed;
}

void context::sweep_all() {
    while (heap_ != nullptr) {
        heap_object * next = heap_->next;
        delete heap_;
        heap_ = next;
    }
    live_objects_ = 0;
}

// ===================== the dispatch loop =================================

// Call a JS function from C++.
//
// The two cases are genuinely different: a native is just a C++ call, while a
// closure needs a frame on the interpreter's own stack. Giving the closure a
// region ABOVE everything currently live is what lets this be re-entrant - the
// caller's registers are untouched, so a listener that triggers another
// listener works rather than corrupting the frame that dispatched it.
value context::call(value callable, std::span<const value> args, value this_value) {
    if (callable.is_kind(heap_kind::native)) {
        auto * nat = static_cast<native_object *>(callable.as_heap());
        std::vector<value> copy{args.begin(), args.end()};
        const value saved = current_this_;
        current_this_ = this_value;
        const value out = nat->fn(*this, copy);
        current_this_ = saved;
        return out;
    }
    if (!callable.is_kind(heap_kind::function) || program_ == nullptr) {
        return value::undefined();
    }
    auto * fnobj = static_cast<closure_object *>(callable.as_heap());
    const function_proto & target = *fnobj->proto;

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
    if (frames_.size() > 512) {
        raise("call stack exhausted");
        return value::undefined();
    }
    const std::size_t depth = frames_.size();
    const value saved = current_this_;
    current_this_ = this_value;
    frames_.push_back(call_frame{&target, 0, new_base, 0, static_cast<std::uint16_t>(args.size()),
                                 fnobj, this_value, handlers_.size()});
    const value out = run_loop(depth);
    current_this_ = saved;
    // Only shrink back if nothing below is still using the space - a nested
    // call that grew the stack further has already returned by now.
    if (registers_.size() >= new_base) { registers_.resize(new_base); }
    return out;
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
    result.ok = !failed_;
    result.error = error_;
    return result;
}

value context::execute(const program & prog, const function_proto & entry) {
    registers_.assign(entry.frame_size + 8u, value::undefined());
    frames_.clear();
    frames_.push_back(call_frame{&entry, 0, 0, 0, 0, nullptr, value::undefined(), 0});
    program_ = &prog;
    // Per-frame string interning: a literal in a loop should allocate once,
    // not once per iteration.
    string_cache_.clear();
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
    run_field_initialisers(callee, self);
    if (callee.is_kind(heap_kind::native)) {
        auto * nat = static_cast<native_object *>(callee.as_heap());
        std::vector<value> copy{args.begin(), args.end()};
        const value saved = current_this_;
        current_this_ = self;
        const value produced = nat->fn(*this, copy);
        current_this_ = saved;
        return produced.is_object_like() ? produced : self;
    }
    // `new C()` evaluates to the new object unless the body returned one of its
    // own - the single case the spec lets override it.
    const value produced = call(callee, args, self);
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

value context::run_loop(std::size_t stop_depth) {
    auto & string_cache = string_cache_;

    while (frames_.size() > stop_depth && !failed_) {
        call_frame & frame = frames_.back();
        // PER FRAME, not per loop: a context can be running functions from more
        // than one program at a time - a page's script calling something the
        // previous script defined - and a function index means nothing outside
        // the program it was compiled in.
        const program & prog = frame.closure != nullptr && frame.closure->owner != nullptr
                                   ? *frame.closure->owner
                                   : *program_;
        const function_proto & fn = *frame.proto;
        if (frame.ip >= fn.code.size()) { break; }
        const instruction in = fn.code[frame.ip++];
        const std::size_t base = frame.base;
        const auto reg = [&](std::uint16_t r) -> value & { return registers_[base + r]; };

        switch (in.code) {
        case op::load_const: reg(in.a) = fn.constants[in.bx()]; break;
        case op::load_string: {
            auto & cache = string_cache[&fn];
            const auto it = cache.find(in.bx());
            if (it != cache.end()) {
                reg(in.a) = it->second;
            } else {
                const value v = string(fn.strings[in.bx()]);
                cache.emplace(in.bx(), v);
                reg(in.a) = v;
            }
            break;
        }
        case op::load_undef: reg(in.a) = value::undefined(); break;
        case op::load_null: reg(in.a) = value::null(); break;
        case op::load_true: reg(in.a) = value::boolean(true); break;
        case op::load_false: reg(in.a) = value::boolean(false); break;
        case op::move: reg(in.a) = reg(in.b); break;

        case op::get_global: {
            const auto it = globals_.find(fn.names[in.bx()]);
            reg(in.a) = it == globals_.end() ? value::undefined() : it->second;
            break;
        }
        case op::set_global: globals_[fn.names[in.bx()]] = reg(in.a); break;

        case op::add: reg(in.a) = value::number(to_number(reg(in.b)) + to_number(reg(in.c))); break;
        case op::sub: reg(in.a) = value::number(to_number(reg(in.b)) - to_number(reg(in.c))); break;
        case op::mul: reg(in.a) = value::number(to_number(reg(in.b)) * to_number(reg(in.c))); break;
        case op::div: reg(in.a) = value::number(to_number(reg(in.b)) / to_number(reg(in.c))); break;
        case op::mod:
            reg(in.a) = value::number(std::fmod(to_number(reg(in.b)), to_number(reg(in.c))));
            break;
        case op::pow:
            reg(in.a) = value::number(std::pow(to_number(reg(in.b)), to_number(reg(in.c))));
            break;
        case op::add_generic: {
            // JS `+`: string concatenation if EITHER side is a string, numeric
            // addition otherwise. The one operator whose meaning is decided by
            // its operands, which is why it is not folded into `add`.
            const value l = reg(in.b);
            const value r = reg(in.c);
            if (l.is_string() || r.is_string()) {
                reg(in.a) = string(to_string(l) + to_string(r));
            } else {
                reg(in.a) = value::number(to_number(l) + to_number(r));
            }
            break;
        }
        case op::concat: reg(in.a) = string(to_string(reg(in.b)) + to_string(reg(in.c))); break;
        case op::negate: reg(in.a) = value::number(-to_number(reg(in.b))); break;
        case op::logical_not: reg(in.a) = value::boolean(!truthy(reg(in.b))); break;

        case op::equal: reg(in.a) = value::boolean(reg(in.b).strict_equals(reg(in.c))); break;
        case op::not_equal: reg(in.a) = value::boolean(!reg(in.b).strict_equals(reg(in.c))); break;
        case op::loose_equal: reg(in.a) = value::boolean(loose_equals(reg(in.b), reg(in.c))); break;
        case op::loose_not_equal:
            reg(in.a) = value::boolean(!loose_equals(reg(in.b), reg(in.c)));
            break;

        // `x instanceof C` is true when C.prototype appears anywhere in x's
        // prototype chain - the same chain lookup_property walks.
        case op::instance_of: {
            reg(in.a) = value::boolean(false);
            value target = reg(in.b);
            const value ctor = reg(in.c);
            value wanted = value::undefined();
            if (ctor.is_kind(heap_kind::function)) {
                wanted = ensure_prototype(ctor);
            } else if (ctor.is_kind(heap_kind::native)) {
                // A BUILT-IN constructor is a native, and every one of them is
                // now something a page can extend - `class E extends Error`.
                // Without this, `e instanceof Error` was false for every one.
                if (value * p = static_cast<native_object *>(ctor.as_heap())->find("prototype")) {
                    wanted = *p;
                }
            } else if (ctor.is_object()) {
                if (value * p = static_cast<object_object *>(ctor.as_heap())->find("prototype")) {
                    wanted = *p;
                }
            }
            for (int depth = 0; depth < 64 && target.is_object(); ++depth) {
                target = static_cast<object_object *>(target.as_heap())->prototype;
                if (target.is_object() && wanted.is_object() &&
                    target.as_heap() == wanted.as_heap()) {
                    reg(in.a) = value::boolean(true);
                    break;
                }
            }
            break;
        }
        case op::has_property: {
            // A PROXY ANSWERS `in` ITSELF, or hands it to the target.
            if (reg(in.c).is_kind(heap_kind::proxy)) {
                auto * p = static_cast<proxy_object *>(reg(in.c).as_heap());
                const value trap = proxy_trap(reg(in.c), "has");
                if (trap.is_callable()) {
                    const value args[2] = {p->target, reg(in.b)};
                    reg(in.a) = value::boolean(truthy(call(trap, args, p->handler)));
                } else {
                    reg(in.a) = value::boolean(
                        !lookup_property(p->target, to_string(reg(in.b))).is_undefined());
                }
                break;
            }
            const std::string key = to_string(reg(in.b));
            const value target = reg(in.c);
            bool present = false;
            if (target.is_object()) {
                present = static_cast<object_object *>(target.as_heap())->find(key) != nullptr;
            } else if (target.is_array()) {
                // `0 in [7, 8]` asks about an INDEX, so the key has to be a
                // whole number and the whole key - "1x" is not index 1.
                std::size_t index = 0;
                const char * first = key.data();
                const char * last = first + key.size();
                const auto [stopped, failed] = std::from_chars(first, last, index);
                present = failed == std::errc{} && stopped == last &&
                          index < static_cast<array_object *>(target.as_heap())->items.size();
            }
            reg(in.a) = value::boolean(present);
            break;
        }

        // ToInt32 / ToUint32 first: `-1 >>> 0` is 4294967295, not -1, and
        // `2.7 | 0` is 2. Doing this on the raw double gets both wrong.
        case op::bit_and:
            reg(in.a) = value::number(to_int32(reg(in.b)) & to_int32(reg(in.c)));
            break;
        case op::bit_or:
            reg(in.a) = value::number(to_int32(reg(in.b)) | to_int32(reg(in.c)));
            break;
        case op::bit_xor:
            reg(in.a) = value::number(to_int32(reg(in.b)) ^ to_int32(reg(in.c)));
            break;
        case op::shl:
            reg(in.a) = value::number(static_cast<std::int32_t>(
                static_cast<std::uint32_t>(to_int32(reg(in.b))) << (to_uint32(reg(in.c)) & 31U)));
            break;
        case op::shr:
            reg(in.a) = value::number(to_int32(reg(in.b)) >> (to_uint32(reg(in.c)) & 31U));
            break;
        case op::ushr:
            reg(in.a) = value::number(
                static_cast<double>(to_uint32(reg(in.b)) >> (to_uint32(reg(in.c)) & 31U)));
            break;
        case op::bit_not: reg(in.a) = value::number(~to_int32(reg(in.b))); break;

        case op::copy_props: {
            if (!reg(in.a).is_object()) { break; }
            auto * target = static_cast<object_object *>(reg(in.a).as_heap());
            if (reg(in.b).is_object()) {
                // A copy of the source's entries first: `set` can reallocate
                // the target's storage, and target and source may be the same
                // object.
                const std::vector<std::pair<std::string, value>> entries =
                    static_cast<object_object *>(reg(in.b).as_heap())->props;
                for (const auto & [name, item] : entries) { target->set(name, item); }
            } else if (reg(in.b).is_array()) {
                const std::vector<value> items =
                    static_cast<array_object *>(reg(in.b).as_heap())->items;
                for (std::size_t i = 0; i < items.size(); ++i) {
                    target->set(std::to_string(i), items[i]);
                }
            }
            break;
        }
        case op::delete_prop:
            if (reg(in.a).is_object()) {
                (void)static_cast<object_object *>(reg(in.a).as_heap())->erase(fn.names[in.b]);
            }
            break;
        case op::delete_index:
            if (reg(in.a).is_object()) {
                (void)static_cast<object_object *>(reg(in.a).as_heap())
                    ->erase(to_string(reg(in.b)));
            }
            break;

        case op::less:
            reg(in.a) = value::boolean(to_number(reg(in.b)) < to_number(reg(in.c)));
            break;
        case op::less_equal:
            reg(in.a) = value::boolean(to_number(reg(in.b)) <= to_number(reg(in.c)));
            break;
        case op::greater:
            reg(in.a) = value::boolean(to_number(reg(in.b)) > to_number(reg(in.c)));
            break;
        case op::greater_equal:
            reg(in.a) = value::boolean(to_number(reg(in.b)) >= to_number(reg(in.c)));
            break;

        case op::jump:
            frame.ip = static_cast<std::size_t>(static_cast<std::int64_t>(frame.ip) + in.sbx());
            break;
        case op::jump_if_false:
            if (!truthy(reg(in.a))) {
                frame.ip = static_cast<std::size_t>(static_cast<std::int64_t>(frame.ip) + in.sbx());
            }
            break;
        case op::jump_if_true:
            if (truthy(reg(in.a))) {
                frame.ip = static_cast<std::size_t>(static_cast<std::int64_t>(frame.ip) + in.sbx());
            }
            break;
        case op::jump_if_not_nullish:
            if (!reg(in.a).is_nullish()) {
                frame.ip = static_cast<std::size_t>(static_cast<std::int64_t>(frame.ip) + in.sbx());
            }
            break;
        case op::jump_if_defined:
            if (!reg(in.a).is_undefined()) {
                frame.ip = static_cast<std::size_t>(static_cast<std::int64_t>(frame.ip) + in.sbx());
            }
            break;

        case op::define_getter:
        case op::define_setter: {
            const bool getter = in.code == op::define_getter;
            const value g = getter ? reg(in.c) : value::undefined();
            const value st = getter ? value::undefined() : reg(in.c);
            if (reg(in.a).is_object()) {
                static_cast<object_object *>(reg(in.a).as_heap())
                    ->define_accessor(fn.names[in.b], g, st);
            } else if (reg(in.a).is_kind(heap_kind::function)) {
                // a `static get` on a class, which IS the constructor closure
                static_cast<closure_object *>(reg(in.a).as_heap())
                    ->define_accessor(fn.names[in.b], g, st);
            }
        } break;

        case op::apply:
        case op::construct_apply: {
            // The arguments arrived as an ARRAY because their count was not
            // known until the spread was evaluated. Both go through the same
            // re-entrant call path a native callback uses, which is why one
            // opcode covers plain, method and computed calls: the receiver is
            // just a register the compiler already filled in.
            const value callee = reg(in.a);
            const value argv = reg(in.b);
            std::vector<value> args;
            if (argv.is_array()) { args = static_cast<array_object *>(argv.as_heap())->items; }
            if (in.code == op::construct_apply) {
                reg(in.a) = construct(callee, args);
            } else if (!callee.is_callable()) {
                raise("attempted to call a non-function");
            } else {
                reg(in.a) = call(callee, args, reg(in.c));
            }
            break;
        }

        case op::gather_rest: {
            // The arguments past the declared parameters. They are still in
            // this frame's registers - the caller wrote them there and the
            // callee's base IS the argument base - so this reads them in place.
            value out = make_array();
            auto * rest = static_cast<array_object *>(out.as_heap());
            for (std::size_t i = in.b; i < frame.argc; ++i) {
                rest->items.push_back(registers_[base + i]);
            }
            reg(in.a) = out;
            break;
        }

        case op::new_object: reg(in.a) = make_object(); break;
        case op::new_array: reg(in.a) = make_array(); break;
        case op::append:
            if (reg(in.a).is_array()) {
                static_cast<array_object *>(reg(in.a).as_heap())->items.push_back(reg(in.b));
            }
            break;
        case op::get_prop: reg(in.a) = lookup_property(reg(in.b), fn.names[in.c]); break;
        // A SETTER ON THE CHAIN TAKES THE WRITE, and only if none does is an
        // own data property defined. Getting that backwards is how a setter
        // silently stops running: the write lands on the instance and shadows
        // the accessor from then on. store_property has the whole rule.
        case op::set_prop: store_property(reg(in.a), fn.names[in.b], reg(in.c)); break;
        case op::get_index: reg(in.a) = lookup_index(reg(in.b), reg(in.c)); break;
        case op::set_index: {
            const value target = reg(in.a);
            const value key = reg(in.b);
            if (target.is_array() && key.is_number()) {
                auto * arr = static_cast<array_object *>(target.as_heap());
                const auto i = static_cast<std::ptrdiff_t>(key.as_number());
                // A TYPED ARRAY COERCES ON WRITE AND DOES NOT GROW. Both are
                // what makes it typed: `pixels[i] = 300` is 255 in a clamped
                // byte array, and a write past the end is DROPPED rather than
                // extending it.
                if (arr->elements != element_kind::none) {
                    if (i >= 0 && static_cast<std::size_t>(i) < arr->items.size()) {
                        arr->items[static_cast<std::size_t>(i)] =
                            value::number(coerce_element(arr->elements, to_number(reg(in.c))));
                    }
                    break;
                }
                if (i >= 0) {
                    if (static_cast<std::size_t>(i) >= arr->items.size()) {
                        arr->items.resize(static_cast<std::size_t>(i) + 1, value::undefined());
                    }
                    arr->items[static_cast<std::size_t>(i)] = reg(in.c);
                }
            } else {
                store_property(target, to_string(key), reg(in.c));
            }
            break;
        }

        case op::closure: {
            const function_proto & target = prog.functions[in.bx()];
            auto * made = allocate<closure_object>(&target);
            made->owner = &prog;
            // Walk the descriptors the compiler resolved: each upvalue is
            // either a cell sitting in THIS frame's register, or one this
            // frame's own closure already holds. The second case is what
            // carries a capture down through more than one level of nesting.
            made->upvalues.reserve(target.upvalues.size());
            for (const upvalue_desc & up : target.upvalues) {
                if (up.from_parent_local) {
                    made->upvalues.push_back(reg(up.index));
                } else if (frame.closure != nullptr && up.index < frame.closure->upvalues.size()) {
                    made->upvalues.push_back(frame.closure->upvalues[up.index]);
                } else {
                    made->upvalues.push_back(value::undefined());
                }
            }
            // An arrow's `this` is decided HERE, where it is written, not where
            // it is called. Reading the effective receiver rather than the raw
            // one is what makes an arrow inside an arrow inside a method still
            // see the method's object.
            if (target.is_arrow) { made->captured_this = effective_this(frame); }
            reg(in.a) = value::object(made);
            break;
        }

        case op::call:
        case op::call_method:
        case op::call_computed:
        case op::call_receiver: {
            value callee = reg(in.a);
            value receiver = value::undefined();
            if (in.code == op::call_receiver) {
                // The callee was resolved elsewhere (up the prototype chain, for
                // `super`) and the receiver is passed explicitly.
                receiver = reg(in.c);
            } else if (in.code == op::call_method) {
                receiver = reg(in.a);
                // Through the SAME lookup as get_prop, so `s.split(...)` and
                // `var f = s.split; f(...)` find the same function.
                callee = lookup_property(receiver, fn.names[in.c]);
            } else if (in.code == op::call_computed) {
                receiver = reg(in.a);
                callee = lookup_index(receiver, reg(in.c));
            }
            const std::size_t arg_base = base + in.a + 1;
            if (callee.is_kind(heap_kind::native)) {
                auto * nat = static_cast<native_object *>(callee.as_heap());
                // COPIED, not spanned into the register stack. A native may call
                // back into script - an event listener dispatching another
                // event - and that grows registers_, which would leave a span
                // into it dangling. One small vector per native call is the
                // price of natives being allowed to re-enter the VM at all.
                std::vector<value> args{registers_.begin() + static_cast<std::ptrdiff_t>(arg_base),
                                        registers_.begin() +
                                            static_cast<std::ptrdiff_t>(arg_base + in.b)};
                const value saved_this = current_this_;
                current_this_ = receiver;
                const value produced = nat->fn(*this, args);
                current_this_ = saved_this;
                reg(in.a) = produced;
                break;
            }
            if (!callee.is_kind(heap_kind::function)) {
                {
                    std::string what = describe_callee(fn,
                                                       in.code == op::call_method ? fn.names[in.c]
                                                       : in.code == op::call_computed
                                                           ? to_string(reg(in.c))
                                                           : callee_origin(fn, frame.ip - 1, in.a),
                                                       callee);
                    // WHAT IT WAS CALLED ON. "`replace` is undefined" reads the
                    // same whether the method is missing from a real object or
                    // the object itself is undefined, and those are different
                    // bugs in different places.
                    if (in.code == op::call_method || in.code == op::call_computed) {
                        what += ", on " + std::string{type_of(receiver)};
                        if (receiver.is_nullish()) { what += " (" + to_string(receiver) + ")"; }
                    }
                    throw_error("TypeError", std::move(what));
                }
                break;
            }
            auto * fnobj = static_cast<closure_object *>(callee.as_heap());
            const function_proto & target = *fnobj->proto;
            // The callee's frame starts where its arguments already are, so no
            // copying is needed to pass them.
            const std::size_t new_base = arg_base;
            const std::size_t needed = new_base + target.frame_size + 8u;
            if (registers_.size() < needed) { registers_.resize(needed, value::undefined()); }
            for (std::size_t i = in.b; i < target.param_count; ++i) {
                registers_[new_base + i] = value::undefined(); // missing args
            }
            if (frames_.size() > 512) {
                raise("call stack exhausted");
                break;
            }
            frames_.push_back(
                call_frame{&target, 0, new_base, in.a, in.b, fnobj, receiver, handlers_.size()});
            break;
        }

        case op::ret:
        case op::ret_undef: {
            value returned = in.code == op::ret ? reg(in.a) : value::undefined();
            if (frame.constructing && !returned.is_object_like()) { returned = frame.receiver; }
            const std::uint16_t slot = frame.result_reg;
            // Handlers this frame installed die with it: a `return` out of a
            // try block must not leave its catch reachable from the caller.
            if (handlers_.size() > frame.handler_base) { handlers_.resize(frame.handler_base); }
            frames_.pop_back();
            if (frames_.size() <= stop_depth) { return returned; }
            registers_[frames_.back().base + slot] = returned;
            break;
        }

        case op::type_of: reg(in.a) = string(std::string{type_of(reg(in.b))}); break;

        case op::load_this: reg(in.a) = effective_this(frame); break;
        case op::make_arguments: {
            // The frame knows how many arguments ARRIVED; the proto only knows
            // how many were declared, and those are different numbers whenever
            // `arguments` is worth reading at all.
            value list = make_array();
            auto * items = static_cast<array_object *>(list.as_heap());
            items->items.reserve(frame.argc);
            for (std::uint16_t i = 0; i < frame.argc; ++i) { items->items.push_back(reg(i)); }
            reg(in.a) = list;
            break;
        }
        case op::load_callee:
            reg(in.a) =
                frame.closure != nullptr ? value::object(frame.closure) : value::undefined();
            break;

        case op::own_keys: {
            // The own property names of an object, as an array. `for (k in o)`
            // compiles to a for-of over this, which keeps one iteration
            // mechanism instead of two.
            value out = make_array();
            auto * keys = static_cast<array_object *>(out.as_heap());
            if (reg(in.b).is_object()) {
                // In DEFINITION ORDER, data and accessors interleaved - which
                // is what a page sees from for-in and has to match Object.keys.
                static_cast<object_object *>(reg(in.b).as_heap())
                    ->each_own_key(
                        [&](const std::string & name) { keys->items.push_back(string(name)); });
            } else if (reg(in.b).is_array()) {
                const std::size_t n =
                    static_cast<array_object *>(reg(in.b).as_heap())->items.size();
                for (std::size_t i = 0; i < n; ++i) {
                    keys->items.push_back(string(std::to_string(i)));
                }
            }
            reg(in.a) = out;
            break;
        }

        case op::wrap_promise:
            // Already a promise (`return somePromise` inside an async function)
            // stays as it is rather than nesting.
            if (!(reg(in.a).is_object() &&
                  static_cast<object_object *>(reg(in.a).as_heap())->find("__value") != nullptr)) {
                reg(in.a) = make_promise(reg(in.a), false);
            }
            break;
        case op::await_value: {
            // A settled promise carries its value in `__value`; anything else
            // awaits to itself. A REJECTED promise throws, which is what makes
            // `try { await f() } catch` work.
            const value awaited = reg(in.b);
            reg(in.a) = awaited;
            if (awaited.is_object()) {
                auto * obj = static_cast<object_object *>(awaited.as_heap());
                if (value * state = obj->find("__rejected"); state != nullptr && truthy(*state)) {
                    thrown_ = obj->find("__value") != nullptr ? *obj->find("__value")
                                                              : value::undefined();
                    if (!unwind_to_handler()) { raise("uncaught rejection"); }
                    break;
                }
                if (value * settled = obj->find("__value")) { reg(in.a) = *settled; }
            }
            break;
        }

        case op::set_proto:
            if (reg(in.a).is_object()) {
                static_cast<object_object *>(reg(in.a).as_heap())->prototype = reg(in.b);
            }
            break;

        case op::get_proto:
            reg(in.a) = reg(in.b).is_object()
                            ? static_cast<object_object *>(reg(in.b).as_heap())->prototype
                            : value::undefined();
            break;

        // `super` has to start its lookup at the prototype ABOVE the class the
        // running method was written in - not above `this`, which in a
        // three-deep hierarchy is a different object and would call the method
        // again forever. So each method carries its home object.
        case op::load_home: {
            closure_object * running = frames_.empty() ? nullptr : frames_.back().closure;
            reg(in.a) = value::undefined();
            if (running != nullptr) {
                if (value * home = running->find("__home")) { reg(in.a) = *home; }
            }
            break;
        }

        case op::construct: {
            const value callee = reg(in.a);
            // A PROXY GOES THE LONG WAY ROUND. The inline path exists to avoid
            // a nested interpreter loop, and a construct trap needs one - so
            // this hands over to the general form rather than duplicating it.
            if (callee.is_kind(heap_kind::proxy)) {
                const std::size_t arg_base = base + in.a + 1;
                std::vector<value> args{registers_.begin() + static_cast<std::ptrdiff_t>(arg_base),
                                        registers_.begin() +
                                            static_cast<std::ptrdiff_t>(arg_base + in.b)};
                reg(in.a) = construct(callee, args);
                break;
            }
            // The instance's prototype comes from the constructor's own
            // `prototype` property, which is what makes a method defined on the
            // class reachable from every instance.
            auto * instance = allocate<object_object>();
            if (callee.is_object()) {
                if (value * proto =
                        static_cast<object_object *>(callee.as_heap())->find("prototype")) {
                    instance->prototype = *proto;
                }
            } else if (callee.is_kind(heap_kind::function)) {
                instance->prototype = ensure_prototype(callee);
            }
            const value self = value::object(instance);
            run_field_initialisers(callee, self);
            const std::size_t arg_base = base + in.a + 1;

            if (callee.is_kind(heap_kind::native)) {
                auto * nat = static_cast<native_object *>(callee.as_heap());
                std::vector<value> args{registers_.begin() + static_cast<std::ptrdiff_t>(arg_base),
                                        registers_.begin() +
                                            static_cast<std::ptrdiff_t>(arg_base + in.b)};
                const value saved = current_this_;
                current_this_ = self;
                const value produced = nat->fn(*this, args);
                current_this_ = saved;
                reg(in.a) = produced.is_object_like() ? produced : self;
                break;
            }
            if (!callee.is_kind(heap_kind::function)) {
                throw_error("TypeError",
                            "`new` on " +
                                describe_callee(fn, callee_origin(fn, frame.ip - 1, in.a), callee));
                break;
            }
            auto * fnobj = static_cast<closure_object *>(callee.as_heap());
            const function_proto & target = *fnobj->proto;
            const std::size_t new_base = arg_base;
            const std::size_t needed = new_base + target.frame_size + 8u;
            if (registers_.size() < needed) { registers_.resize(needed, value::undefined()); }
            for (std::size_t i = in.b; i < target.param_count; ++i) {
                registers_[new_base + i] = value::undefined();
            }
            if (frames_.size() > 512) {
                raise("call stack exhausted");
                break;
            }
            call_frame fresh{&target, 0, new_base, in.a, in.b, fnobj, self, handlers_.size()};
            fresh.constructing = true;
            frames_.push_back(fresh);
            break;
        }

        case op::push_handler:
            handlers_.push_back(
                handler{frames_.size() - 1,
                        static_cast<std::size_t>(frame.ip) + static_cast<std::size_t>(in.sbx()),
                        registers_.size(), in.a});
            break;
        case op::pop_handler:
            if (!handlers_.empty()) { handlers_.pop_back(); }
            break;
        case op::throw_value: {
            thrown_ = reg(in.a);
            if (!unwind_to_handler()) { raise("uncaught " + describe_thrown(thrown_)); }
            break;
        }

        case op::new_cell: reg(in.a) = value::object(allocate<cell_object>(reg(in.a))); break;
        case op::cell_get:
            reg(in.a) = reg(in.b).is_kind(heap_kind::cell)
                            ? static_cast<cell_object *>(reg(in.b).as_heap())->slot
                            : value::undefined();
            break;
        case op::cell_set:
            if (reg(in.a).is_kind(heap_kind::cell)) {
                static_cast<cell_object *>(reg(in.a).as_heap())->slot = reg(in.b);
            }
            break;
        case op::get_upvalue: {
            reg(in.a) = value::undefined();
            if (frame.closure != nullptr && in.b < frame.closure->upvalues.size()) {
                const value cell = frame.closure->upvalues[in.b];
                if (cell.is_kind(heap_kind::cell)) {
                    reg(in.a) = static_cast<cell_object *>(cell.as_heap())->slot;
                }
            }
            break;
        }
        case op::set_upvalue: {
            if (frame.closure != nullptr && in.a < frame.closure->upvalues.size()) {
                const value cell = frame.closure->upvalues[in.a];
                if (cell.is_kind(heap_kind::cell)) {
                    static_cast<cell_object *>(cell.as_heap())->slot = reg(in.b);
                }
            }
            break;
        }
        case op::halt: return value::undefined();
        }
    }
    return value::undefined();
}

} // namespace ctbrowser::script
