#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
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
    // AN OBJECT CONVERTS THROUGH ITS OWN `toString`, then `valueOf`.
    //
    // That is ToPrimitive, and it is not a nicety: a class that defines
    // toString does so precisely because it expects `'' + x`, a template hole
    // and String(x) to use it. Returning the tag regardless turned every such
    // object into "[object Object]" - which for colorjs, whose colour spaces
    // print as "HSB (hsb)", made an error message name the wrong thing and a
    // page's own labels come out as tags.
    return to_primitive_string(v);
}

// `toString` then `valueOf`, either of which may be inherited. Bounded: a
// method that hands back another object falls through to the tag rather than
// recursing, which is what the spec does after trying both.
// ToPrimitive with the DEFAULT hint: `valueOf`, then `toString`, and whatever
// comes back is a primitive the operator can work on. `+` needs this before it
// can even decide what it means - `{valueOf: () => 42} + 1` is 43 and
// `{toString: () => 'x'} + 1` is "x1", and which one it is depends on what the
// object hands back rather than on the object being an object.
value context::to_primitive(value v) {
    if (!v.is_heap() || v.is_string()) { return v; }
    for (const char * name : {"valueOf", "toString"}) {
        const value fn = lookup_property(v, name);
        if (!fn.is_callable()) { continue; }
        const value produced = call(fn, std::span<const value>{}, v);
        if (produced.is_heap() && !produced.is_string()) { continue; }
        return produced;
    }
    return v;
}

// The numeric half of the same rule: `valueOf` then `toString`. An object that
// defines valueOf means it to be used in arithmetic - that is the only reason to
// define one - and without this every such object was NaN.
double context::to_number_value(value v) {
    if (!v.is_heap() || v.is_string()) { return to_number(v); }
    for (const char * name : {"valueOf", "toString"}) {
        const value fn = lookup_property(v, name);
        if (!fn.is_callable()) { continue; }
        const value produced = call(fn, std::span<const value>{}, v);
        if (produced.is_heap() && !produced.is_string()) { continue; }
        return to_number(produced);
    }
    return to_number(v);
}

std::string context::to_primitive_string(value v) {
    for (const char * name : {"toString", "valueOf"}) {
        const value fn = lookup_property(v, name);
        if (!fn.is_callable()) { continue; }
        const value produced = call(fn, std::span<const value>{}, v);
        if (produced.is_heap() && !produced.is_string()) { continue; }
        return to_string(produced);
    }
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
    if (frames_.size() > 512) {
        raise("call stack exhausted");
        return value::undefined();
    }
    const std::size_t depth = frames_.size();
    const value saved = current_this_;
    current_this_ = this_value;
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

// A MODULE IS RUN LIKE ANY OTHER PROGRAM, with two differences: it knows which
// record it is filling in, so `bind_export` knows which cells to adopt, and its
// exports outlive the call.
run_result context::run_module(const program & prog, module_record & into) {
    module_record * const outer = current_module_;
    current_module_ = &into;
    into.compiled = &prog;
    const run_result result = run(prog);
    into.evaluated = true;
    current_module_ = outer;
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

// EVERY OPCODE, ONCE, so the label table cannot silently lose an entry.
//
// `-Wswitch` makes a missing case a compile error today, and that guarantee had
// to survive: a table built with array designators has no such check, and a
// missing entry is a null pointer and a jump to address zero at run time. The
// static_assert in run_loop ties this list's length to the enum's, so an opcode
// added without a line here fails the BUILD rather than the process.
#define VM_OPCODES(X)                                                                              \
    X(load_const)                                                                                  \
    X(load_string)                                                                                 \
    X(load_undef)                                                                                  \
    X(load_null)                                                                                   \
    X(load_true)                                                                                   \
    X(load_false)                                                                                  \
    X(move)                                                                                        \
    X(get_global)                                                                                  \
    X(set_global)                                                                                  \
    X(new_cell)                                                                                    \
    X(cell_get)                                                                                    \
    X(cell_set)                                                                                    \
    X(get_upvalue)                                                                                 \
    X(set_upvalue)                                                                                 \
    X(add)                                                                                         \
    X(concat)                                                                                      \
    X(add_generic)                                                                                 \
    X(sub)                                                                                         \
    X(mul)                                                                                         \
    X(div)                                                                                         \
    X(mod)                                                                                         \
    X(pow)                                                                                         \
    X(negate)                                                                                      \
    X(to_number)                                                                                   \
    X(logical_not)                                                                                 \
    X(equal)                                                                                       \
    X(not_equal)                                                                                   \
    X(loose_equal)                                                                                 \
    X(loose_not_equal)                                                                             \
    X(less)                                                                                        \
    X(less_equal)                                                                                  \
    X(greater)                                                                                     \
    X(greater_equal)                                                                               \
    X(instance_of)                                                                                 \
    X(has_property)                                                                                \
    X(bit_and)                                                                                     \
    X(bit_or)                                                                                      \
    X(bit_xor)                                                                                     \
    X(shl)                                                                                         \
    X(shr)                                                                                         \
    X(ushr)                                                                                        \
    X(bit_not)                                                                                     \
    X(jump)                                                                                        \
    X(jump_if_false)                                                                               \
    X(jump_if_true)                                                                                \
    X(jump_if_not_nullish)                                                                         \
    X(jump_if_defined)                                                                             \
    X(gather_rest)                                                                                 \
    X(apply)                                                                                       \
    X(construct_apply)                                                                             \
    X(define_getter)                                                                               \
    X(define_setter)                                                                               \
    X(new_object)                                                                                  \
    X(new_array)                                                                                   \
    X(get_prop)                                                                                    \
    X(set_prop)                                                                                    \
    X(get_index)                                                                                   \
    X(set_index)                                                                                   \
    X(append)                                                                                      \
    X(closure)                                                                                     \
    X(call)                                                                                        \
    X(call_method)                                                                                 \
    X(call_computed)                                                                               \
    X(construct)                                                                                   \
    X(call_receiver)                                                                               \
    X(copy_props)                                                                                  \
    X(delete_prop)                                                                                 \
    X(delete_index)                                                                                \
    X(own_keys)                                                                                    \
    X(iterable)                                                                                    \
    X(set_proto)                                                                                   \
    X(get_proto)                                                                                   \
    X(load_home)                                                                                   \
    X(await_value)                                                                                 \
    X(yield_value)                                                                                 \
    X(wrap_promise)                                                                                \
    X(ret)                                                                                         \
    X(ret_undef)                                                                                   \
    X(type_of)                                                                                     \
    X(load_this)                                                                                   \
    X(load_new_target)                                                                             \
    X(pass_new_target)                                                                             \
    X(load_import)                                                                                 \
    X(bind_export)                                                                                 \
    X(load_callee)                                                                                 \
    X(make_arguments)                                                                              \
    X(push_handler)                                                                                \
    X(pop_handler)                                                                                 \
    X(throw_value)                                                                                 \
    X(halt)

// COUNTED, NOT SIZED, and the difference is the whole guarantee. The table below
// is built with array designators, so `std::size` on it reports the HIGHEST
// designated index plus one - which is `op::halt + 1` whether or not the middle
// of the list has holes in it. A hole is a null entry and a jump to address
// zero, and the assert written to catch exactly that could not see one: two
// opcodes (`load_import`, `bind_export`) were missing from the list for as long
// as modules have existed, and the only reason nothing jumped to zero is that
// computed gotos are off by default.
//
// Counting the list's ENTRIES catches a hole, and it holds in the switch build
// too, where there is no table to size.
#define VM_COUNT_OPCODE(name) +1
static_assert(0 VM_OPCODES(VM_COUNT_OPCODE) == static_cast<std::size_t>(op::halt) + 1,
              "VM_OPCODES(X) must list every opcode exactly once - a GAP is invisible to a "
              "std::size check on a designated-initializer table, and is a null entry and a "
              "jump to address zero in the computed-goto build");
#undef VM_COUNT_OPCODE

// --- INSTRUCTION DISPATCH: computed goto, or a switch ------------------------
//
// The dispatch loop is 15% of a Phaser frame (measured - callgrind on
// tests/phaser_invaders) and 77% of tests/bench_script. A `switch` compiles to
// ONE indirect branch that all 88 opcodes share, so the predictor sees a single
// site with 88 targets and mispredicts constantly. Replicating the jump into
// every handler gives it 88 sites, each of which can learn the PAIRS this
// bytecode actually emits - `less` then `jump_if_false`, `get_prop` then
// `call_method`. docs/computed-goto-plan.md has the measurement that justified
// trying it and the numbers it actually produced.
//
// GNU ONLY, and the switch is not a poor relation - it is the fallback that
// keeps this portable, and both paths must stay live. Any compiler without the
// address-of-label extension takes it, as does anyone defining
// CTBROWSER_NO_COMPUTED_GOTO to compare the two.
// OFF BY DEFAULT, BECAUSE IT MEASURED SLOWER - see the table in
// docs/performance.md. Opt in with -DCTBROWSER_COMPUTED_GOTO (or the CMake
// option of the same name) to measure it on your own hardware: the result is a
// property of the branch predictor, not of this code, and a different
// microarchitecture may well answer differently.
#if defined(__GNUC__) && defined(CTBROWSER_COMPUTED_GOTO)
#define VM_COMPUTED_GOTO 1
#else
#define VM_COMPUTED_GOTO 0
#endif

#if VM_COMPUTED_GOTO
// GNU extensions, suppressed HERE and nowhere else: the address-of-label and
// indirect-goto forms, and the C99 array designators that index the table by
// opcode rather than by position. Verified under
// -O2 -pedantic -Wall -Wextra -Werror -Wconversion on clang 24 and gcc 13.
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-label-as-value"
#pragma clang diagnostic ignored "-Wc99-designator"
#else
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#endif
#define VM_CASE(name) VM_LABEL_##name:
#define VM_DISPATCH_BEGIN goto * vm_table[static_cast<std::size_t>(in.code)];
#define VM_DISPATCH_END
// RE-DERIVES THE FRAME, exactly as the loop head does. It is tempting to keep
// `frame`, `fn` and `base` live across a dispatch and skip this - that is what
// a textbook computed-goto interpreter does - but 12 handlers here push, pop or
// unwind `frames_`, which is a std::vector: any of them can reallocate it and
// leave a cached reference dangling. Re-deriving keeps this change to DISPATCH
// alone, so it cannot alter behaviour, and the win being chased is the branch
// prediction rather than the loads.
#define VM_NEXT                                                                                    \
    do {                                                                                           \
        if (frames_.size() <= stop_depth || failed_) { goto vm_done; }                             \
        vm_frame = &frames_.back();                                                                \
        vm_proto = vm_frame->proto;                                                                \
        if (vm_frame->ip >= vm_proto->code.size()) { goto vm_done; }                               \
        in = vm_proto->code[vm_frame->ip++];                                                       \
        base = vm_frame->base;                                                                     \
        goto * vm_table[static_cast<std::size_t>(in.code)];                                        \
    } while (0)
#else
#define VM_CASE(name) case op::name:
#define VM_DISPATCH_BEGIN switch (in.code) {
#define VM_DISPATCH_END }
#define VM_NEXT break
#endif

value context::run_loop(std::size_t stop_depth) {
    auto & string_cache = string_cache_;

#if VM_COMPUTED_GOTO
    // Indexed BY OPCODE, which is what the array designators buy: the order of
    // this table cannot drift out of step with the enum. A label address is not
    // a constant expression, so it is a function-local `static` rather than
    // constexpr - checked in the emitted assembly to be plain .rodata with no
    // thread-safe-init guard, which on the hottest loop in the engine would
    // have cost an atomic load per dispatch.
#define X(name) [static_cast<std::size_t>(op::name)] = &&VM_LABEL_##name,
    static void * const vm_table[] = {VM_OPCODES(X)};
#undef X
    static_assert(std::size(vm_table) == static_cast<std::size_t>(op::halt) + 1,
                  "VM_OPCODES(X) must list every opcode - a gap here is a null table entry "
                  "and a jump to address zero at run time");
#endif

    // HOISTED, because in computed-goto mode the handlers are jumped to
    // directly and never fall back through the loop head - so anything they
    // read has to survive a goto that bypasses initialisation.
    call_frame * vm_frame = nullptr;
    const function_proto * vm_proto = nullptr;
    instruction in{};
    std::size_t base = 0;
    const auto reg = [&](std::uint16_t r) -> value & { return registers_[base + r]; };

    while (frames_.size() > stop_depth && !failed_) {
        vm_frame = &frames_.back();
        vm_proto = vm_frame->proto;
        if (vm_frame->ip >= vm_proto->code.size()) { break; }
        in = vm_proto->code[vm_frame->ip++];
        base = vm_frame->base;

        VM_DISPATCH_BEGIN
        VM_CASE(load_const) do {
            reg(in.a) = vm_proto->constants[in.bx()];
            break;
        }
        while (0);
        VM_NEXT;
        VM_CASE(load_string) do {
            {
                auto & cache = string_cache[&(*vm_proto)];
                const auto it = cache.find(in.bx());
                if (it != cache.end()) {
                    reg(in.a) = it->second;
                } else {
                    const value v = string(vm_proto->strings[in.bx()]);
                    cache.emplace(in.bx(), v);
                    reg(in.a) = v;
                }
                break;
            }
        }
        while (0);
        VM_NEXT;
        VM_CASE(load_undef) do {
            reg(in.a) = value::undefined();
            break;
        }
        while (0);
        VM_NEXT;
        VM_CASE(load_null) do {
            reg(in.a) = value::null();
            break;
        }
        while (0);
        VM_NEXT;
        VM_CASE(load_true) do {
            reg(in.a) = value::boolean(true);
            break;
        }
        while (0);
        VM_NEXT;
        VM_CASE(load_false) do {
            reg(in.a) = value::boolean(false);
            break;
        }
        while (0);
        VM_NEXT;
        VM_CASE(move) do {
            reg(in.a) = reg(in.b);
            break;
        }
        while (0);
        VM_NEXT;

        VM_CASE(get_global) do {
            {
                const auto it = globals_.find(vm_proto->names[in.bx()]);
                reg(in.a) = it == globals_.end() ? value::undefined() : it->second;
                break;
            }
        }
        while (0);
        VM_NEXT;
        VM_CASE(set_global) do {
            globals_[vm_proto->names[in.bx()]] = reg(in.a);
            break;
        }
        while (0);
        VM_NEXT;

        VM_CASE(add) do {
            reg(in.a) = value::number(to_number(reg(in.b)) + to_number(reg(in.c)));
            break;
        }
        while (0);
        VM_NEXT;
        VM_CASE(sub) do {
            reg(in.a) = value::number(to_number_value(reg(in.b)) - to_number_value(reg(in.c)));
            break;
        }
        while (0);
        VM_NEXT;
        VM_CASE(mul) do {
            reg(in.a) = value::number(to_number_value(reg(in.b)) * to_number_value(reg(in.c)));
            break;
        }
        while (0);
        VM_NEXT;
        VM_CASE(div) do {
            reg(in.a) = value::number(to_number_value(reg(in.b)) / to_number_value(reg(in.c)));
            break;
        }
        while (0);
        VM_NEXT;
        VM_CASE(mod) do {
            reg(in.a) =
                value::number(std::fmod(to_number_value(reg(in.b)), to_number_value(reg(in.c))));
            break;
        }
        while (0);
        VM_NEXT;
        VM_CASE(pow) do {
            reg(in.a) =
                value::number(std::pow(to_number_value(reg(in.b)), to_number_value(reg(in.c))));
            break;
        }
        while (0);
        VM_NEXT;
        VM_CASE(add_generic) do {
            {
                // JS `+`: string concatenation if EITHER side is a string, numeric
                // addition otherwise. The one operator whose meaning is decided by
                // its operands, which is why it is not folded into `add`.
                // BOTH SIDES ARE MADE PRIMITIVE FIRST, and only then does the
                // operator decide what it is. An object's own valueOf or toString
                // is what settles it, so `{valueOf: () => 42} + 1` is 43 while
                // `{toString: () => 'x'} + 1` is "x1".
                const value l = to_primitive(reg(in.b));
                const value r = to_primitive(reg(in.c));
                if (l.is_string() || r.is_string()) {
                    reg(in.a) = string(to_string(l) + to_string(r));
                } else {
                    reg(in.a) = value::number(to_number(l) + to_number(r));
                }
                break;
            }
        }
        while (0);
        VM_NEXT;
        VM_CASE(concat) do {
            reg(in.a) = string(to_string(reg(in.b)) + to_string(reg(in.c)));
            break;
            // `to_number_value`, NOT `to_number`, and for the same reason `-` and
            // `*` already use it: ToNumber on an object runs valueOf/toString
            // first. Without it `[] - 0` was 0 while `-[]` was NaN, which is one
            // conversion spelled two ways.
        }
        while (0);
        VM_NEXT;
        VM_CASE(negate) do {
            reg(in.a) = value::number(-to_number_value(reg(in.b)));
            break;
            // `+x` IS A CONVERSION. It compiled to a plain `move` for a long time,
            // so `+"2"` stayed the string "2" - and that is invisible in most of the
            // places it is written, because `+x + "/"` concatenates either way. It
            // shows up where the result is USED as a number: `d[(+y * 8 + +x) * 4]`
            // indexed with a string built by concatenation and read undefined.
        }
        while (0);
        VM_NEXT;
        VM_CASE(to_number) do {
            reg(in.a) = value::number(to_number_value(reg(in.b)));
            break;
        }
        while (0);
        VM_NEXT;
        VM_CASE(logical_not) do {
            reg(in.a) = value::boolean(!truthy(reg(in.b)));
            break;
        }
        while (0);
        VM_NEXT;

        VM_CASE(equal) do {
            reg(in.a) = value::boolean(reg(in.b).strict_equals(reg(in.c)));
            break;
        }
        while (0);
        VM_NEXT;
        VM_CASE(not_equal) do {
            reg(in.a) = value::boolean(!reg(in.b).strict_equals(reg(in.c)));
            break;
        }
        while (0);
        VM_NEXT;
        VM_CASE(loose_equal) do {
            reg(in.a) = value::boolean(loose_equals(reg(in.b), reg(in.c)));
            break;
        }
        while (0);
        VM_NEXT;
        VM_CASE(loose_not_equal) do {
            reg(in.a) = value::boolean(!loose_equals(reg(in.b), reg(in.c)));
            break;

            // `x instanceof C` is true when C.prototype appears anywhere in x's
            // prototype chain - the same chain lookup_property walks.
        }
        while (0);
        VM_NEXT;
        VM_CASE(instance_of) do {
            {
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
                    if (value * p =
                            static_cast<native_object *>(ctor.as_heap())->find("prototype")) {
                        wanted = *p;
                    }
                } else if (ctor.is_object()) {
                    if (value * p =
                            static_cast<object_object *>(ctor.as_heap())->find("prototype")) {
                        wanted = *p;
                    }
                }
                if (!wanted.is_object()) { break; }
                // The EXPLICIT chain first - a page's own classes, and every builtin
                // whose instances carry a prototype (Error, Map, Blob).
                value link = target.is_object()
                                 ? static_cast<object_object *>(target.as_heap())->prototype
                                 : value::undefined();
                bool found = false;
                for (int depth = 0; depth < 64 && link.is_object() && !found; ++depth) {
                    if (link.as_heap() == wanted.as_heap()) {
                        found = true;
                    } else {
                        link = static_cast<object_object *>(link.as_heap())->prototype;
                    }
                }
                // Then the IMPLICIT one. An array, a function, a string and a plain
                // object have no prototype field to walk - their chain is the tables
                // property lookup falls back to - so instanceof answered false for
                // every builtin while answering correctly for a page's own classes.
                // OBJECT-LIKE ONLY. `5 instanceof Number` and `'x' instanceof
                // String` are FALSE in JavaScript however many methods a primitive
                // resolves - instanceof asks about a prototype chain and a primitive
                // does not have one. Applying the fallback to everything made both
                // of those true, which is the mirror image of the bug being fixed.
                if (!found && target.is_object_like()) {
                    for (object_object * table : implicit_prototypes(target)) {
                        if (table != nullptr && table == wanted.as_heap()) {
                            found = true;
                            break;
                        }
                    }
                }
                reg(in.a) = value::boolean(found);
                break;
            }
        }
        while (0);
        VM_NEXT;
        VM_CASE(has_property) do {
            {
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
        }
        while (0);
        VM_NEXT;
        VM_CASE(bit_and) do {
            reg(in.a) = value::number(to_int32(reg(in.b)) & to_int32(reg(in.c)));
            break;
        }
        while (0);
        VM_NEXT;
        VM_CASE(bit_or) do {
            reg(in.a) = value::number(to_int32(reg(in.b)) | to_int32(reg(in.c)));
            break;
        }
        while (0);
        VM_NEXT;
        VM_CASE(bit_xor) do {
            reg(in.a) = value::number(to_int32(reg(in.b)) ^ to_int32(reg(in.c)));
            break;
        }
        while (0);
        VM_NEXT;
        VM_CASE(shl) do {
            reg(in.a) = value::number(static_cast<std::int32_t>(
                static_cast<std::uint32_t>(to_int32(reg(in.b))) << (to_uint32(reg(in.c)) & 31U)));
            break;
        }
        while (0);
        VM_NEXT;
        VM_CASE(shr) do {
            reg(in.a) = value::number(to_int32(reg(in.b)) >> (to_uint32(reg(in.c)) & 31U));
            break;
        }
        while (0);
        VM_NEXT;
        VM_CASE(ushr) do {
            reg(in.a) = value::number(
                static_cast<double>(to_uint32(reg(in.b)) >> (to_uint32(reg(in.c)) & 31U)));
            break;
        }
        while (0);
        VM_NEXT;
        VM_CASE(bit_not) do {
            reg(in.a) = value::number(~to_int32(reg(in.b)));
            break;
        }
        while (0);
        VM_NEXT;

        VM_CASE(copy_props) do {
            {
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
        }
        while (0);
        VM_NEXT;
        VM_CASE(delete_prop) do {
            if (reg(in.a).is_object()) {
                (void)static_cast<object_object *>(reg(in.a).as_heap())
                    ->erase(vm_proto->names[in.b]);
            }
            break;
        }
        while (0);
        VM_NEXT;
        VM_CASE(delete_index) do {
            if (reg(in.a).is_object()) {
                (void)static_cast<object_object *>(reg(in.a).as_heap())
                    ->erase(to_string(reg(in.b)));
            }
            break;
        }
        while (0);
        VM_NEXT;

        VM_CASE(less) do {
            reg(in.a) = value::boolean(to_number(reg(in.b)) < to_number(reg(in.c)));
            break;
        }
        while (0);
        VM_NEXT;
        VM_CASE(less_equal) do {
            reg(in.a) = value::boolean(to_number(reg(in.b)) <= to_number(reg(in.c)));
            break;
        }
        while (0);
        VM_NEXT;
        VM_CASE(greater) do {
            reg(in.a) = value::boolean(to_number(reg(in.b)) > to_number(reg(in.c)));
            break;
        }
        while (0);
        VM_NEXT;
        VM_CASE(greater_equal) do {
            reg(in.a) = value::boolean(to_number(reg(in.b)) >= to_number(reg(in.c)));
            break;
        }
        while (0);
        VM_NEXT;

        VM_CASE(jump) do {
            vm_frame->ip =
                static_cast<std::size_t>(static_cast<std::int64_t>(vm_frame->ip) + in.sbx());
            break;
        }
        while (0);
        VM_NEXT;
        VM_CASE(jump_if_false) do {
            if (!truthy(reg(in.a))) {
                vm_frame->ip =
                    static_cast<std::size_t>(static_cast<std::int64_t>(vm_frame->ip) + in.sbx());
            }
            break;
        }
        while (0);
        VM_NEXT;
        VM_CASE(jump_if_true) do {
            if (truthy(reg(in.a))) {
                vm_frame->ip =
                    static_cast<std::size_t>(static_cast<std::int64_t>(vm_frame->ip) + in.sbx());
            }
            break;
        }
        while (0);
        VM_NEXT;
        VM_CASE(jump_if_not_nullish) do {
            if (!reg(in.a).is_nullish()) {
                vm_frame->ip =
                    static_cast<std::size_t>(static_cast<std::int64_t>(vm_frame->ip) + in.sbx());
            }
            break;
        }
        while (0);
        VM_NEXT;
        VM_CASE(jump_if_defined) do {
            if (!reg(in.a).is_undefined()) {
                vm_frame->ip =
                    static_cast<std::size_t>(static_cast<std::int64_t>(vm_frame->ip) + in.sbx());
            }
            break;
        }
        while (0);
        VM_NEXT;

        VM_CASE(define_getter)
        VM_CASE(define_setter) do {
            {
                const bool getter = in.code == op::define_getter;
                const value g = getter ? reg(in.c) : value::undefined();
                const value st = getter ? value::undefined() : reg(in.c);
                if (reg(in.a).is_object()) {
                    static_cast<object_object *>(reg(in.a).as_heap())
                        ->define_accessor(vm_proto->names[in.b], g, st);
                } else if (reg(in.a).is_kind(heap_kind::function)) {
                    // a `static get` on a class, which IS the constructor closure
                    static_cast<closure_object *>(reg(in.a).as_heap())
                        ->define_accessor(vm_proto->names[in.b], g, st);
                }
            }
            break;
        }
        while (0);
        VM_NEXT;

        VM_CASE(apply)
        VM_CASE(construct_apply) do {
            {
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
        }
        while (0);
        VM_NEXT;

        VM_CASE(gather_rest) do {
            {
                // The arguments past the declared parameters. They are still in
                // this (*vm_frame)'s registers - the caller wrote them there and the
                // callee's base IS the argument base - so this reads them in place.
                //
                // UNLESS the body also built an `arguments` object, which happens
                // before this and claims a register an extra argument may be in.
                // Then the (*vm_frame)'s copy is the one that still has them.
                value out = make_array();
                auto * rest = static_cast<array_object *>(out.as_heap());
                if (vm_frame->arguments_object.is_array()) {
                    const auto & held =
                        static_cast<array_object *>(vm_frame->arguments_object.as_heap())->items;
                    for (std::size_t i = in.b; i < held.size(); ++i) {
                        rest->items.push_back(held[i]);
                    }
                } else {
                    for (std::size_t i = in.b; i < vm_frame->argc; ++i) {
                        rest->items.push_back(registers_[base + i]);
                    }
                }
                reg(in.a) = out;
                break;
            }
        }
        while (0);
        VM_NEXT;

        VM_CASE(new_object) do {
            reg(in.a) = make_object();
            break;
        }
        while (0);
        VM_NEXT;
        VM_CASE(new_array) do {
            reg(in.a) = make_array();
            break;
        }
        while (0);
        VM_NEXT;
        VM_CASE(append) do {
            if (reg(in.a).is_array()) {
                static_cast<array_object *>(reg(in.a).as_heap())->items.push_back(reg(in.b));
            }
            break;
        }
        while (0);
        VM_NEXT;
        VM_CASE(get_prop) do {
            reg(in.a) = lookup_property(reg(in.b), vm_proto->names[in.c]);
            break;
            // A SETTER ON THE CHAIN TAKES THE WRITE, and only if none does is an
            // own data property defined. Getting that backwards is how a setter
            // silently stops running: the write lands on the instance and shadows
            // the accessor from then on. store_property has the whole rule.
        }
        while (0);
        VM_NEXT;
        VM_CASE(set_prop) do {
            store_property(reg(in.a), vm_proto->names[in.b], reg(in.c));
            break;
        }
        while (0);
        VM_NEXT;
        VM_CASE(get_index) do {
            reg(in.a) = lookup_index(reg(in.b), reg(in.c));
            break;
        }
        while (0);
        VM_NEXT;
        VM_CASE(set_index) do {
            {
                const value target = reg(in.a);
                const value key = reg(in.b);
                if (target.is_array() && key.is_number()) {
                    auto * arr = static_cast<array_object *>(target.as_heap());
                    const auto i = static_cast<std::ptrdiff_t>(key.as_number());
                    // A TYPED ARRAY COERCES ON WRITE AND DOES NOT GROW. Both are
                    // what makes it typed: `pixels[i] = 300` is 255 in a clamped
                    // byte array, and a write past the end is DROPPED rather than
                    // extending it.
                    if (arr->is_view()) {
                        if (i >= 0 && static_cast<std::size_t>(i) < arr->length()) {
                            view_set(*arr, static_cast<std::size_t>(i), to_number(reg(in.c)));
                        }
                        break;
                    }
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
        }
        while (0);
        VM_NEXT;

        VM_CASE(closure) do {
            {
                // PER FRAME, not per loop: a context can be running functions from
                // more than one program at a time, and a function index means
                // nothing outside the program it was compiled in. Derived here
                // because this is the only opcode that needs it.
                const program & prog =
                    vm_frame->closure != nullptr && vm_frame->closure->owner != nullptr
                        ? *vm_frame->closure->owner
                        : *program_;
                const function_proto & target = prog.functions[in.bx()];
                auto * made = allocate<closure_object>(&target);
                made->owner = &prog;
                // Walk the descriptors the compiler resolved: each upvalue is
                // either a cell sitting in THIS (*vm_frame)'s register, or one this
                // (*vm_frame)'s own closure already holds. The second case is what
                // carries a capture down through more than one level of nesting.
                made->upvalues.reserve(target.upvalues.size());
                for (const upvalue_desc & up : target.upvalues) {
                    if (up.from_parent_local) {
                        made->upvalues.push_back(reg(up.index));
                    } else if (vm_frame->closure != nullptr &&
                               up.index < vm_frame->closure->upvalues.size()) {
                        made->upvalues.push_back(vm_frame->closure->upvalues[up.index]);
                    } else {
                        made->upvalues.push_back(value::undefined());
                    }
                }
                // An arrow's `this` is decided HERE, where it is written, not where
                // it is called. Reading the effective receiver rather than the raw
                // one is what makes an arrow inside an arrow inside a method still
                // see the method's object.
                if (target.is_arrow) { made->captured_this = effective_this((*vm_frame)); }
                reg(in.a) = value::object(made);
                break;
            }
        }
        while (0);
        VM_NEXT;

        VM_CASE(call)
        VM_CASE(call_method)
        VM_CASE(call_computed)
        VM_CASE(call_receiver) do {
            {
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
                    callee = lookup_property(receiver, vm_proto->names[in.c]);
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
                    std::vector<value> args{
                        registers_.begin() + static_cast<std::ptrdiff_t>(arg_base),
                        registers_.begin() + static_cast<std::ptrdiff_t>(arg_base + in.b)};
                    const value saved_this = current_this_;
                    current_this_ = receiver;
                    const value produced = nat->fn(*this, args);
                    current_this_ = saved_this;
                    reg(in.a) = produced;
                    break;
                }
                if (!callee.is_kind(heap_kind::function)) {
                    {
                        std::string what = describe_callee(
                            (*vm_proto),
                            in.code == op::call_method ? vm_proto->names[in.c]
                            : in.code == op::call_computed
                                ? to_string(reg(in.c))
                                : callee_origin((*vm_proto), vm_frame->ip - 1, in.a),
                            callee);
                        // WHAT IT WAS CALLED ON. "`replace` is undefined" reads the
                        // same whether the method is missing from a real object or
                        // the object itself is undefined, and those are different
                        // bugs in different places.
                        if (in.code == op::call_method || in.code == op::call_computed) {
                            what += ", on " + std::string{type_of(receiver)};
                            if (receiver.is_nullish()) {
                                what += " (" + to_string(receiver) + ")";
                                // WHICH undefined. "`get` is undefined, on
                                // undefined" names the method and says nothing
                                // about the object, and the object is the bug -
                                // `get` is fine, whatever should have had it is
                                // missing. A method call keeps its receiver in the
                                // callee's own register, so the walk that names a
                                // plain call's callee names the receiver too.
                                const std::string from =
                                    callee_origin((*vm_proto), vm_frame->ip - 1, in.a);
                                if (!from.empty()) { what += " from `" + from + "`"; }
                            }
                        }
                        throw_error("TypeError", std::move(what));
                    }
                    break;
                }
                auto * fnobj = static_cast<closure_object *>(callee.as_heap());
                const function_proto & target = *fnobj->proto;
                // CALLING A GENERATOR RUNS NOTHING. It hands back an object over a
                // (*vm_frame) that has not started; the first instruction runs on the
                // first `.next()`.
                if (target.is_generator) {
                    std::vector<value> args{
                        registers_.begin() + static_cast<std::ptrdiff_t>(arg_base),
                        registers_.begin() + static_cast<std::ptrdiff_t>(arg_base + in.b)};
                    reg(in.a) = make_generator(fnobj, receiver, args);
                    break;
                }
                // The callee's (*vm_frame) starts where its arguments already are, so no
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
                call_frame entered{&target, 0,     new_base, in.a,
                                   in.b,    fnobj, receiver, handlers_.size()};
                entered.new_target = pending_new_target_;
                pending_new_target_ = value::undefined();
                frames_.push_back(entered);
                break;
            }
        }
        while (0);
        VM_NEXT;

        VM_CASE(ret)
        VM_CASE(ret_undef) do {
            {
                value returned = in.code == op::ret ? reg(in.a) : value::undefined();
                if (vm_frame->constructing && !returned.is_object_like()) {
                    returned = vm_frame->receiver;
                }
                const std::uint16_t slot = vm_frame->result_reg;
                // Handlers this (*vm_frame) installed die with it: a `return` out of a
                // try block must not leave its catch reachable from the caller.
                if (handlers_.size() > vm_frame->handler_base) {
                    handlers_.resize(vm_frame->handler_base);
                }
                frames_.pop_back();
                if (frames_.size() <= stop_depth) { return returned; }
                registers_[frames_.back().base + slot] = returned;
                break;
            }
        }
        while (0);
        VM_NEXT;

        VM_CASE(type_of) do {
            reg(in.a) = string(std::string{type_of(reg(in.b))});
            break;
        }
        while (0);
        VM_NEXT;

        VM_CASE(load_this) do {
            reg(in.a) = effective_this((*vm_frame));
            break;
            // `new.target` IS THE CONSTRUCTOR, OR UNDEFINED. The (*vm_frame) already
            // carries both halves - `constructing`, so `new C()` can evaluate to
            // the new object rather than the body's return, and `closure`, which is
            // the function running - so this reads state that was there rather than
            // adding any.
            //
            // The closure is the function this (*vm_frame) is EXECUTING, which for a
            // direct `new C()` is C. The spec's new.target follows the originally
            // invoked constructor through a `super()` chain to the derived-most
            // class; that distinction only shows up in a hierarchy, and the pages
            // that use this - a transpiler's `_classCallCheck`, Babylon's decorator
            // metadata - ask whether it is undefined, not which constructor it is.
        }
        while (0);
        VM_NEXT;
        VM_CASE(load_new_target) do {
            reg(in.a) = vm_frame->new_target;
            break;
        }
        while (0);
        VM_NEXT;
        VM_CASE(make_arguments) do {
            {
                // The (*vm_frame) knows how many arguments ARRIVED; the proto only knows
                // how many were declared, and those are different numbers whenever
                // `arguments` is worth reading at all.
                value list = make_array();
                auto * items = static_cast<array_object *>(list.as_heap());
                items->items.reserve(vm_frame->argc);
                for (std::uint16_t i = 0; i < vm_frame->argc; ++i) {
                    items->items.push_back(reg(i));
                }
                // On the (*vm_frame) too: this claims a register that an extra argument
                // may be in, so whatever still needs the raw ones reads them here.
                vm_frame->arguments_object = list;
                reg(in.a) = list;
                break;
            }
        }
        while (0);
        VM_NEXT;
        VM_CASE(load_callee) do {
            reg(in.a) = vm_frame->closure != nullptr ? value::object(vm_frame->closure)
                                                     : value::undefined();
            break;
        }
        while (0);
        VM_NEXT;

        VM_CASE(iterable) do {
            reg(in.a) = iterable_values(reg(in.b));
            break;
        }
        while (0);
        VM_NEXT;

        VM_CASE(own_keys) do {
            {
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
        }
        while (0);
        VM_NEXT;

        VM_CASE(wrap_promise) do {
            // Already a promise (`return somePromise` inside an async function)
            // stays as it is rather than nesting.
            if (!(reg(in.a).is_object() &&
                  static_cast<object_object *>(reg(in.a).as_heap())->find("__value") != nullptr)) {
                reg(in.a) = make_promise(reg(in.a), false);
            }
            break;
        }
        while (0);
        VM_NEXT;
        VM_CASE(await_value) do {
            {
                // A settled promise carries its value in `__value`; anything else
                // awaits to itself. A REJECTED promise throws, which is what makes
                // `try { await f() } catch` work.
                const value awaited = reg(in.b);
                // A PENDING PROMISE SUSPENDS THE FRAME.
                //
                // There is one stack and the event loop is above it, so `await`
                // cannot block: the (*vm_frame) is lifted out, the caller is handed a
                // promise, and the (*vm_frame) comes back when the awaited one settles.
                //
                // It used to read `__value` off a promise that had none, so `await`
                // on anything genuinely asynchronous evaluated to UNDEFINED and ran
                // the rest of the function immediately - the single largest wrong
                // answer left in this engine, and silent.
                if (is_pending_promise(awaited) && pending_promise_factory_ && promise_settler_) {
                    if (vm_frame->async_promise.is_undefined()) {
                        vm_frame->async_promise = pending_promise_factory_(*this);
                    }
                    const value promise = vm_frame->async_promise;
                    auto * saved = allocate<coroutine_object>();
                    saved->proto = vm_frame->proto;
                    saved->ip = vm_frame->ip;
                    saved->await_reg = in.a;
                    saved->argc = vm_frame->argc;
                    saved->closure = vm_frame->closure;
                    saved->receiver = vm_frame->receiver;
                    saved->constructing = vm_frame->constructing;
                    saved->promise = promise;
                    saved->window.assign(registers_.begin() + static_cast<std::ptrdiff_t>(base),
                                         registers_.end());
                    // This (*vm_frame)'s handlers travel with it, with reg_top made
                    // RELATIVE - the (*vm_frame) comes back somewhere else in the stack,
                    // and an absolute mark would point at whatever is there then.
                    for (std::size_t i = vm_frame->handler_base; i < handlers_.size(); ++i) {
                        handler moved = handlers_[i];
                        moved.reg_top -= base;
                        saved->handlers.push_back(moved);
                    }
                    handlers_.resize(vm_frame->handler_base);
                    const std::uint16_t slot = vm_frame->result_reg;
                    registers_.resize(base);
                    frames_.pop_back();
                    attach_resume(awaited, value::object(saved));
                    suspended_ = true;
                    if (frames_.size() <= stop_depth) { return promise; }
                    registers_[frames_.back().base + slot] = promise;
                    break;
                }
                reg(in.a) = awaited;
                if (awaited.is_object()) {
                    auto * obj = static_cast<object_object *>(awaited.as_heap());
                    if (value * state = obj->find("__rejected");
                        state != nullptr && truthy(*state)) {
                        thrown_ = obj->find("__value") != nullptr ? *obj->find("__value")
                                                                  : value::undefined();
                        if (!unwind_to_handler()) { raise("uncaught rejection"); }
                        break;
                    }
                    if (value * settled = obj->find("__value")) { reg(in.a) = *settled; }
                }
                break;
            }
        }
        while (0);
        VM_NEXT;

        VM_CASE(yield_value) do {
            {
                // SUSPEND INTO THE GENERATOR AND HAND THE VALUE OUT. Everything
                // here is the (*vm_frame)-lifting `await` does a few cases up; what
                // differs is only who puts it back, and that a value goes to the
                // caller of `.next()` rather than to a promise.
                coroutine_object * saved = vm_frame->generator;
                if (saved == nullptr) {
                    // The compiler refuses `yield` outside a generator, so this is
                    // unreachable - and cheap insurance against it becoming
                    // reachable, since the alternative is a null dereference.
                    raise("`yield` outside a generator");
                    break;
                }
                const value produced = reg(in.b);
                saved->ip = vm_frame->ip;
                saved->await_reg = in.a;
                saved->receiver = vm_frame->receiver;
                saved->window.assign(registers_.begin() + static_cast<std::ptrdiff_t>(base),
                                     registers_.end());
                // This (*vm_frame)'s handlers travel with it, with reg_top made RELATIVE:
                // the (*vm_frame) comes back somewhere else in the register stack, and an
                // absolute mark would point at whatever is there then.
                saved->handlers.clear();
                for (std::size_t i = vm_frame->handler_base; i < handlers_.size(); ++i) {
                    handler moved = handlers_[i];
                    moved.reg_top -= base;
                    saved->handlers.push_back(moved);
                }
                handlers_.resize(vm_frame->handler_base);
                registers_.resize(base);
                frames_.pop_back();
                yielded_ = true;
                if (frames_.size() <= stop_depth) { return produced; }
                // A generator body is only ever entered by generator_resume, which
                // stops at its own depth - so reaching here would mean a yield ran
                // under some other caller's loop and there is nowhere to put the
                // value.
                raise("a generator yielded outside its own resume");
                break;
            }
        }
        while (0);
        VM_NEXT;

        VM_CASE(bind_export) do {
            // ADOPT THE RECORD'S CELL, do not publish this register's. The cell
            // is created before ANY module in the graph runs - see
            // instantiate_module - so by the time this executes it already
            // exists and something in a cycle may already be holding it.
            // Overwriting the record here would hand that importer a box
            // nobody ever writes to again.
            //
            // THE CELL, NOT THE VALUE, either way: an importer holds the box,
            // which is what makes the binding LIVE. Handing over the value
            // passes "an importer sees an export" and fails "an imported
            // binding is live" - the shortcut docs/modules-plan.md names in
            // advance.
            if (current_module_ != nullptr) {
                value & slot = current_module_->exports[vm_proto->names[in.bx()]];
                if (!slot.is_kind(heap_kind::cell)) {
                    slot = value::object(allocate<cell_object>(value::undefined()));
                }
                reg(in.a) = slot;
            }
            break;
        }
        while (0);
        VM_NEXT;

        VM_CASE(load_import) do {
            // The exporter has been evaluated already - the loader walks the
            // graph depth-first - so its cell is there to be taken.
            reg(in.a) = value::undefined();
            const std::string & from = vm_proto->names[in.c];
            const std::string & what = vm_proto->names[in.b];
            const auto found = modules_.find(from);
            if (found == modules_.end()) {
                raise("module `" + from + "` was not loaded");
                break;
            }
            const auto cell = found->second.exports.find(what);
            if (cell == found->second.exports.end()) {
                raise("`" + from + "` has no export named `" + what + "`");
                break;
            }
            reg(in.a) = cell->second;
            break;
        }
        while (0);
        VM_NEXT;

        VM_CASE(pass_new_target) do {
            // The NEXT (*vm_frame) pushed - the base constructor super() is about to
            // enter - gets this (*vm_frame)'s new.target rather than undefined.
            pending_new_target_ = vm_frame->new_target;
            break;
        }
        while (0);
        VM_NEXT;

        VM_CASE(set_proto) do {
            if (reg(in.a).is_object()) {
                static_cast<object_object *>(reg(in.a).as_heap())->prototype = reg(in.b);
            }
            break;
        }
        while (0);
        VM_NEXT;

        VM_CASE(get_proto) do {
            reg(in.a) = reg(in.b).is_object()
                            ? static_cast<object_object *>(reg(in.b).as_heap())->prototype
                            : value::undefined();
            break;

            // `super` has to start its lookup at the prototype ABOVE the class the
            // running method was written in - not above `this`, which in a
            // three-deep hierarchy is a different object and would call the method
            // again forever. So each method carries its home object.
        }
        while (0);
        VM_NEXT;
        VM_CASE(load_home) do {
            {
                closure_object * running = frames_.empty() ? nullptr : frames_.back().closure;
                reg(in.a) = value::undefined();
                if (running != nullptr) {
                    if (value * home = running->find("__home")) { reg(in.a) = *home; }
                }
                break;
            }
        }
        while (0);
        VM_NEXT;

        VM_CASE(construct) do {
            {
                const value callee = reg(in.a);
                // A PROXY GOES THE LONG WAY ROUND. The inline path exists to avoid
                // a nested interpreter loop, and a construct trap needs one - so
                // this hands over to the general form rather than duplicating it.
                if (callee.is_kind(heap_kind::proxy)) {
                    const std::size_t arg_base = base + in.a + 1;
                    std::vector<value> args{
                        registers_.begin() + static_cast<std::ptrdiff_t>(arg_base),
                        registers_.begin() + static_cast<std::ptrdiff_t>(arg_base + in.b)};
                    reg(in.a) = construct(callee, args);
                    break;
                }
                // A NATIVE GOES THE LONG WAY TOO, for the same reason as a proxy: the
                // inline path exists to avoid a nested interpreter loop, which only a
                // JavaScript body needs. Duplicating the native case here is what let
                // the two disagree - this copy neither set the instance's prototype
                // from a native's `prototype` property nor honoured the conversion
                // flag, so `new Number(5)` was an empty object down this path and a 5
                // down the other, depending only on whether a proxy was involved.
                if (callee.is_kind(heap_kind::native)) {
                    const std::size_t arg_base = base + in.a + 1;
                    std::vector<value> args{
                        registers_.begin() + static_cast<std::ptrdiff_t>(arg_base),
                        registers_.begin() + static_cast<std::ptrdiff_t>(arg_base + in.b)};
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

                if (!callee.is_kind(heap_kind::function)) {
                    throw_error("TypeError",
                                "`new` on " + describe_callee((*vm_proto),
                                                              callee_origin((*vm_proto),
                                                                            vm_frame->ip - 1, in.a),
                                                              callee));
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
                fresh.new_target = callee;
                frames_.push_back(fresh);
                break;
            }
        }
        while (0);
        VM_NEXT;

        VM_CASE(push_handler) do {
            handlers_.push_back(
                handler{frames_.size() - 1,
                        static_cast<std::size_t>(vm_frame->ip) + static_cast<std::size_t>(in.sbx()),
                        registers_.size(), in.a});
            break;
        }
        while (0);
        VM_NEXT;
        VM_CASE(pop_handler) do {
            if (!handlers_.empty()) { handlers_.pop_back(); }
            break;
        }
        while (0);
        VM_NEXT;
        VM_CASE(throw_value) do {
            {
                thrown_ = reg(in.a);
                if (!unwind_to_handler()) { raise("uncaught " + describe_thrown(thrown_)); }
                break;
            }
        }
        while (0);
        VM_NEXT;

        VM_CASE(new_cell) do {
            reg(in.a) = value::object(allocate<cell_object>(reg(in.a)));
            break;
        }
        while (0);
        VM_NEXT;
        VM_CASE(cell_get) do {
            reg(in.a) = reg(in.b).is_kind(heap_kind::cell)
                            ? static_cast<cell_object *>(reg(in.b).as_heap())->slot
                            : value::undefined();
            break;
        }
        while (0);
        VM_NEXT;
        VM_CASE(cell_set) do {
            if (reg(in.a).is_kind(heap_kind::cell)) {
                static_cast<cell_object *>(reg(in.a).as_heap())->slot = reg(in.b);
            }
            break;
        }
        while (0);
        VM_NEXT;
        VM_CASE(get_upvalue) do {
            {
                reg(in.a) = value::undefined();
                if (vm_frame->closure != nullptr && in.b < vm_frame->closure->upvalues.size()) {
                    const value cell = vm_frame->closure->upvalues[in.b];
                    if (cell.is_kind(heap_kind::cell)) {
                        reg(in.a) = static_cast<cell_object *>(cell.as_heap())->slot;
                    }
                }
                break;
            }
        }
        while (0);
        VM_NEXT;
        VM_CASE(set_upvalue) do {
            {
                if (vm_frame->closure != nullptr && in.a < vm_frame->closure->upvalues.size()) {
                    const value cell = vm_frame->closure->upvalues[in.a];
                    if (cell.is_kind(heap_kind::cell)) {
                        static_cast<cell_object *>(cell.as_heap())->slot = reg(in.b);
                    }
                }
                break;
            }
        }
        while (0);
        VM_NEXT;
        VM_CASE(halt) do {
            return value::undefined();
        }
        while (0);
        VM_NEXT;
        VM_DISPATCH_END
    }
#if VM_COMPUTED_GOTO
// Where VM_NEXT goes when the loop is over. In switch mode the while condition
// is the exit and this label would be unused - a -Wunused-label error - hence
// the guard.
vm_done:
#endif
    return value::undefined();
}
#if VM_COMPUTED_GOTO
#if defined(__clang__)
#pragma clang diagnostic pop
#else
#pragma GCC diagnostic pop
#endif
#endif

} // namespace ctbrowser::script
