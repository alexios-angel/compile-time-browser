// ctbrowser.script context - property lookup and storage, and the mark-sweep collector.
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

// ===================== gc ================================================

void context::mark_object(heap_object * o) {
    if (o == nullptr || o->marked) { return; }
    o->marked = true;
    switch (o->kind) {
    case heap_kind::array: {
        auto * arr = static_cast<array_object *>(o);
        for (const value & v : arr->items) { mark(v); }
        mark(arr->viewed);
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
    case heap_kind::coroutine: {
        // A SUSPENDED FRAME IS A ROOT LIKE ANY OTHER. Its registers are the
        // only reference to everything the function had in hand, and they are
        // out of the register stack the collector normally walks - so without
        // this every local of every waiting function is freed.
        auto * saved = static_cast<coroutine_object *>(o);
        for (const value & v : saved->window) { mark(v); }
        mark(saved->receiver);
        mark(saved->promise);
        if (saved->closure != nullptr) { mark_object(saved->closure); }
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
        if (arr->is_view()) {
            return i >= 0 && static_cast<std::size_t>(i) < arr->length()
                       ? value::number(view_get(*arr, static_cast<std::size_t>(i)))
                       : value::undefined();
        }
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
    // `a.length = n` RESIZES THE ARRAY, and dropping the write silently is not
    // a small gap: `a.length = 0` is how a great deal of code empties one -
    // Phaser's scene manager ends its boot with `this._pending.length = 0`, and
    // with the write ignored the queue it had just drained was still full, so
    // the next frame added the same scene a second time and threw "Cannot add
    // Scene with duplicate key". An engine that reads `length` but will not
    // write it looks like it supports arrays right up until it does not.
    //
    // Growing pads with undefined, which is what the spec says and what
    // `a.length = 10` is occasionally used for.
    if (target.is_array()) {
        if (name != "length") { return; } // arrays have no property table here
        auto * arr = static_cast<array_object *>(target.as_heap());
        // A TYPED array's length is fixed - it is a view over bytes that were
        // sized once, and resizing it here would leave the view and its buffer
        // disagreeing. The spec makes the write a no-op, not an error.
        if (arr->elements != element_kind::none) { return; }
        const double wanted = to_number(v);
        // The spec throws RangeError for a non-integer or negative length. This
        // engine is lenient with pages elsewhere for the same reason it is
        // here: a dropped nonsense write leaves the array as it was, which is
        // strictly better than the page dying.
        if (!std::isfinite(wanted) || wanted < 0) { return; }
        constexpr double length_limit = 4294967295.0; // 2^32 - 1, the spec's cap
        if (wanted > length_limit) { return; }
        arr->items.resize(static_cast<std::size_t>(wanted));
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
        // HASHED ONCE FOR THE WHOLE CHAIN. Every level below is asked for the
        // same name, and each `find` used to hash it again.
        const prehashed_name key{name, hash_name(name)};
        for (int depth = 0; obj != nullptr && depth < 64; ++depth) {
            if (value * found = obj->find(key)) { return *found; }
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
            if (value * found = table->find(key)) { return *found; }
        }
        return value::undefined();
    }
    if (target.is_array()) {
        auto * arr = static_cast<array_object *>(target.as_heap());
        if (name == "length") { return value::number(static_cast<double>(arr->length())); }
        // WHAT A VIEW KNOWS ABOUT ITS BUFFER. `new Uint8Array(f32.buffer)` is
        // how a page makes a second view of a different width over storage it
        // already has - Phaser does exactly that - and it needs `buffer` to
        // hand back something the constructor recognises as one.
        if (arr->is_view()) {
            const auto width = bytes_per_element(arr->elements);
            if (name == "byteLength") {
                return value::number(static_cast<double>(arr->view_length * width));
            }
            if (name == "byteOffset") { return value::number(arr->byte_offset); }
            if (name == "BYTES_PER_ELEMENT") { return value::number(static_cast<double>(width)); }
            if (name == "buffer") {
                value made = make_object();
                auto * buffer = static_cast<object_object *>(made.as_heap());
                const auto * bytes = static_cast<const array_object *>(arr->viewed.as_heap());
                buffer->set("byteLength", value::number(static_cast<double>(bytes->items.size())));
                buffer->set("length", value::number(static_cast<double>(bytes->items.size())));
                buffer->set("__bytes", arr->viewed);
                return made;
            }
        }
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
        if (object_object * table = prototype(proto_kind::object)) {
            if (value * found = table->find(name)) { return *found; }
        }
        return value::undefined();
    }
    // A BigInt is a primitive with methods, exactly as a number is - and it
    // reaches its prototype the same way, because it has no own properties for
    // a lookup to find first. Without this `(255n).toString(16)` read
    // undefined and called it.
    if (target.is_kind(heap_kind::bigint)) {
        if (object_object * table = prototype(proto_kind::bigint)) {
            if (value * found = table->find(name)) { return *found; }
        }
        if (object_object * table = prototype(proto_kind::object)) {
            if (value * found = table->find(name)) { return *found; }
        }
        return value::undefined();
    }
    if (target.is_number()) {
        if (object_object * table = prototype(proto_kind::number)) {
            if (value * found = table->find(name)) { return *found; }
        }
        // ...THEN Object.prototype, because `Number.prototype`'s own prototype
        // IS Object.prototype and a primitive boxes on property access. Without
        // this `(5).hasOwnProperty(...)` read undefined - and library code does
        // exactly that on values whose type it has not checked. Phaser's tween
        // manager asks `hasOwnProperty` of a number while working out which
        // properties of a target to animate.
        //
        // NUMBERS, BOOLEANS AND STRINGS ALL LACKED THIS. Only arrays chained to
        // Object.prototype, and the comment there says it is "the chain
        // JavaScript actually has" - which was true of arrays and of nothing
        // else. The regression test asserted the string case on the assumption
        // it already worked, and it did not.
        if (object_object * table = prototype(proto_kind::object)) {
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
        if (object_object * table = prototype(proto_kind::object)) {
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
    // COUNTED, because a stress mode that silently stops collecting looks
    // exactly like one that works: every answer is the same either way. A test
    // that forces GC asserts this number, not the answer.
    ++collections_;

    // AND THE FRAME CHAIN CHECKED, under stress only, which is where the master
    // plan puts it: "have the GC validate the whole frame chain on every
    // collection - reachable, terminated, and with plausible slot counts".
    //
    // A compiled frame's slots live in `registers_` and are addressed as
    // `registers_.data() + base`, so a base past the end of that vector is a
    // body about to read and write somewhere else entirely - the kind of
    // corruption that surfaces a long way from its cause.
    //
    // SAID PLAINLY: no test exercises this. Reaching it needs a corrupted
    // frame stack, which nothing can produce from outside the class, and
    // writing a test that reaches in to corrupt it would be testing the
    // corruption rather than the guard. It is here because it costs one
    // comparison per frame in a mode that already collects the whole heap.
    if (gc_stress_) {
        for (const call_frame & f : frames_) {
            if (f.proto == nullptr || f.base > registers_.size()) {
                raise("a call frame's register span is outside the register file");
                break;
            }
        }
    }

    // Precise roots: everything reachable is reachable from exactly these.
    for (const auto & [name, v] : globals_) { mark(v); }
    for (const value & v : registers_) { mark(v); }
    // The receiver of a native call in progress. It is held in a C++ local, not
    // in a register, so nothing else would keep it alive - and collecting the
    // object a method is running on is about as bad as it gets.
    mark(current_this_);
    // The constructor a super() call is in the middle of handing on. It lives
    // only in this slot between the two instructions, which is exactly the
    // window a collection can fall in.
    mark(pending_new_target_);
    mark(pending_closure_);
    // And the closure each live frame is executing. A function called from C++
    // via call() is likewise only referenced from a C++ local; without this its
    // upvalues can be freed while its body is still running.
    for (const call_frame & f : frames_) {
        if (f.closure != nullptr) { mark_object(f.closure); }
        mark(f.receiver);
        mark(f.arguments_object);
        mark(f.async_promise);
        mark(f.new_target);
    }
    // A QUEUED JOB AND ITS ARGUMENTS. Nothing else refers to them between the
    // moment they are queued and the moment they run, which is precisely the
    // window a collection can fall in.
    for (const microtask & job : microtasks_) {
        mark(job.fn);
        for (const value & arg : job.args) { mark(arg); }
    }
    // EVERY MODULE'S EXPORT CELLS. They live in `modules_` and in no register
    // once the module has finished evaluating, so without this a collection
    // between two modules frees the bindings the second one is about to import
    // - and it frees them while a closure inside the first still refers to the
    // same cell.
    for (auto & [specifier, mod] : modules_) {
        for (auto & [name, cell] : mod.exports) { mark(cell); }
        mark(mod.namespace_object);
    }
    // A thrown value in flight is reachable from nothing else.
    mark(thrown_);
    // AND WHAT A C++ SCOPE IS HOLDING ACROSS A CALL. `construct` allocates the
    // instance and then runs field initialisers and the constructor body with
    // it in a local; without this the object being constructed is freed by any
    // collection inside either. See context::rooted.
    for (const value & v : temporaries_) { mark(v); }
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
    // The BigInt literal cache is a root for exactly the same reason: the
    // context is the only thing holding those values, so a sweep without this
    // frees a literal a running loop is about to read again.
    for (auto & [proto, cache] : bigint_cache_) {
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

} // namespace ctbrowser::script
