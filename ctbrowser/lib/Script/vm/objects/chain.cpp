// ctbrowser.script context - the prototype chain and the object-shape
// operations the opcodes call: own keys, accessors, spread, `in`, `instanceof`.
//
// One of six files carved out of a 1,396-line vm/objects.cpp on 2026-09-08 -
// which was itself one of four carved out of a 3,232-line vm.cpp on
// 2026-08-09. All members of `context`, declared in
// include/ctbrowser/script/vm.hpp - so they split across translation units
// with nothing to declare.

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

#include <ctbrowser/script/vm.hpp>

namespace ctbrowser::script {

// Defined in lookup.cpp beside the read that needs it most.
object_object * typed_array_prototype(context & cx, element_kind kind);

value context::own_keys(value source) {
    // A PROXY ENUMERATES ITS TARGET: no handler here defines ownKeys or
    // getOwnPropertyDescriptor, and an absent trap is the target's own
    // [[OwnPropertyKeys]] (10.5.11 step 6) - the same fall-through
    // own_property and Object.keys already take. `for (k in el.dataset)`
    // walked nothing while `Object.keys(el.dataset)` walked the store.
    if (source.is_kind(heap_kind::proxy)) {
        const value keys = own_keys(static_cast<proxy_object *>(source.as_heap())->target);
        // A REALM'S GLOBAL OBJECT OWNS ITS BINDINGS TOO, which live in the
        // table rather than on the target: `for (k in globalThis)` names an
        // eval's `var` and a plain assignment's global as its traps describe
        // them - enumerable unless an intrinsic or an interface object, and
        // never a `let`. A declared name has a placeholder on the target
        // already, and one the target owns was answered above.
        for (const auto & realm : realms_) {
            if (realm->global_this.bits() != source.bits()) { continue; }
            const value target = static_cast<proxy_object *>(source.as_heap())->target;
            auto * held =
                target.is_object() ? static_cast<object_object *>(target.as_heap()) : nullptr;
            auto * listed = static_cast<array_object *>(keys.as_heap());
            string_flat_map<bool> seen;
            for (const value & key : listed->items) {
                if (key.is_string()) { seen[to_string(key)] = true; }
            }
            for (const auto & [name, bound] : realm->globals) {
                if (realm->lexical.contains(name) || realm->builtin.contains(name) ||
                    realm->declared.contains(name) || seen.contains(name) ||
                    (!name.empty() && name.front() == internal_key_lead) ||
                    (held != nullptr &&
                     (held->find(name) != nullptr || held->find_accessor(name) != nullptr))) {
                    continue;
                }
                listed->items.push_back(string(name));
            }
            break;
        }
        return keys;
    }
    value out = make_array();
    auto * keys = static_cast<array_object *>(out.as_heap());
    if (source.is_object()) {
        // In DEFINITION ORDER, data and accessors interleaved - which is what a
        // page sees from for-in and has to match Object.keys. for-in
        // enumerates STRING keys only, and ENUMERABLE ones only: 13.7.5.15
        // filters on [[Enumerable]], which is why a built-in method never turns
        // up in a `for (k in Math)`. A String wrapper's indices are own and
        // enumerable (10.4.3.1), and come first.
        auto * obj = static_cast<object_object *>(source.as_heap());
        if (const value * slot = primitive_slot(source); slot != nullptr && slot->is_string()) {
            const std::size_t n = static_cast<string_object *>(slot->as_heap())->length();
            for (std::size_t i = 0; i < n; ++i) {
                keys->items.push_back(string(std::to_string(i)));
            }
        }
        obj->each_own_enumerable_key(
            [&](const std::string & name) { keys->items.push_back(string(name)); });
    } else if (source.is_array()) {
        auto * arr = static_cast<array_object *>(source.as_heap());
        const std::size_t n = arr->items.size();
        for (std::size_t i = 0; i < n; ++i) {
            // A hole is not a property and a non-enumerable element is not
            // enumerated - see array_object::element_attrs.
            if (!arr->element_attrs.empty() || arr->has_hole_run()) {
                const std::uint8_t a = arr->element_attrs_at(static_cast<std::uint32_t>(i));
                if ((a & array_object::elem_hole) != 0 || (a & attr_enumerable) == 0) { continue; }
            }
            keys->items.push_back(string(std::to_string(i)));
        }
        // Then the named own properties, in definition order (10.4.2.1: the
        // integer keys first, ascending, then the strings).
        if (arr->named) {
            arr->named->each_own_enumerable_key([&](const std::string & name) {
                // An accessor ELEMENT's pair also lives here, under its
                // index; it was reported above.
                if (std::uint32_t at = 0; !object_object::array_index_key(name, at)) {
                    keys->items.push_back(string(name));
                }
            });
        }
    } else if (source.is_kind(heap_kind::function)) {
        // `for (k in fn)`: a class's enumerable statics (a plain function's
        // `length`/`name`/`prototype` are not enumerable and have no entry).
        auto * closure = static_cast<closure_object *>(source.as_heap());
        for (std::size_t i = 0; i < closure->props.size(); ++i) {
            const std::string & name = closure->props[i].first;
            // `__home` is the compiler's [[HomeObject]] slot, not a static.
            if ((closure->attrs_of(name) & attr_enumerable) != 0 &&
                !name.starts_with(symbol_key_mark) && name != "__home") {
                keys->items.push_back(string(name));
            }
        }
    } else if (source.is_kind(heap_kind::native)) {
        auto * fn = static_cast<native_object *>(source.as_heap());
        for (std::size_t i = 0; i < fn->props.size(); ++i) {
            const std::string & name = fn->props[i].first;
            if ((fn->attrs_of(name) & attr_enumerable) != 0 && !name.starts_with(symbol_key_mark)) {
                keys->items.push_back(string(name));
            }
        }
    }
    // ...AND THE PROTOTYPE CHAIN (14.7.5.9 EnumerateObjectProperties), for
    // every kind of object: an inherited enumerable key is visited once,
    // unless an own or nearer property of the same name shadows it - a
    // non-enumerable one shadows too. `for (k in body)` reaching the
    // Window-forwarded handlers on HTMLBodyElement.prototype is the case a page
    // notices; an enumerable property a page put on Object.prototype,
    // Array.prototype or Function.prototype is the one test262 asks about.
    // The chain is the explicit object links, then the implicit tables behind
    // them (implicit_prototypes).
    std::vector<object_object *> chain;
    value up = source.is_object()  ? static_cast<object_object *>(source.as_heap())->prototype
               : source.is_array() ? static_cast<array_object *>(source.as_heap())->prototype
                                   : value::null();
    for (int depth = 0; depth < 4096 && up.is_object(); ++depth) {
        auto * parent = static_cast<object_object *>(up.as_heap());
        chain.push_back(parent);
        up = parent->prototype;
    }
    if (up.is_null()) {
        if (chain.empty()) {
            for (object_object * table : implicit_prototypes(source)) {
                if (table != nullptr) { chain.push_back(table); }
            }
        } else if (object_object * table = prototype(proto_kind::object)) {
            chain.push_back(table);
        }
    }
    // Nothing inherited is enumerable - the usual case, every built-in
    // prototype - and nothing needs a shadow set.
    bool inherits = false;
    for (object_object * table : chain) {
        table->each_own_enumerable_key([&](const std::string &) { inherits = true; });
        if (inherits) { break; }
    }
    if (!inherits) { return out; }
    std::unordered_set<std::string> seen;
    for (const value & key : keys->items) { seen.insert(to_string(key)); }
    const auto shadow = [&](object_object * table) {
        table->each_own_entry([&](const std::string & name, std::uint8_t) { seen.insert(name); });
    };
    if (source.is_object()) { shadow(static_cast<object_object *>(source.as_heap())); }
    if (source.is_array() && static_cast<array_object *>(source.as_heap())->named) {
        shadow(static_cast<array_object *>(source.as_heap())->named.get());
    }
    for (object_object * parent : chain) {
        std::vector<std::string> fresh;
        parent->each_own_enumerable_key([&](const std::string & name) {
            if (seen.find(name) == seen.end()) { fresh.push_back(name); }
        });
        for (const std::string & name : fresh) { keys->items.push_back(string(name)); }
        shadow(parent);
    }
    return out;
}

void context::define_accessor(value target, const std::string & name, value getter, value setter) {
    // DefinePropertyOrThrow (7.3.8) - a class's `static get prototype()`
    // meets the constructor's non-configurable `prototype`, and
    // ValidateAndApplyPropertyDescriptor refuses it: the TypeError.
    if (target.is_kind(heap_kind::function)) {
        property_descriptor held;
        if (own_property(target, name, held) && held.has_configurable && !held.configurable) {
            throw_error("TypeError", "Cannot redefine property: " + name);
            return;
        }
    }
    if (target.is_object()) {
        static_cast<object_object *>(target.as_heap())->define_accessor(name, getter, setter);
    } else if (target.is_kind(heap_kind::function)) {
        // a `static get` on a class, which IS the constructor closure
        static_cast<closure_object *>(target.as_heap())->define_accessor(name, getter, setter);
    }
}

void context::copy_own_properties(value target, value source) {
    if (!target.is_object()) { return; }
    auto * into = static_cast<object_object *>(target.as_heap());
    if (source.is_object()) {
        // A COPY OF THE SOURCE'S ENTRIES FIRST: `set` can reallocate the
        // target's storage, and target and source may be the same object.
        //
        // ENUMERABLE OWN PROPERTIES ONLY. `{...o}` is CopyDataProperties
        // (7.3.25), which skips a non-enumerable one - so spreading a class
        // instance no longer drags its prototype's plumbing along.
        //
        // A SYMBOL KEY IS COPIED, unlike in Object.keys or for-in:
        // CopyDataProperties takes OwnPropertyKeys, which reports both. That is
        // the one place the enumerable walk must NOT filter symbols, and
        // filtering them cost four tests (…/spread-obj-symbol-property.js).
        //
        // ...and an accessor is READ rather than copied: the spec does a Get,
        // so what lands on the target is the getter's answer as a data property.
        std::vector<std::pair<std::string, value>> entries;
        auto * from = static_cast<object_object *>(source.as_heap());
        from->each_own_entry([&](const std::string & name, std::uint8_t attrs) {
            if ((attrs & attr_enumerable) != 0) {
                entries.emplace_back(name, lookup_property(source, name));
            }
        });
        for (const auto & [name, item] : entries) { into->set(name, item); }
    } else if (source.is_array()) {
        auto * arr = static_cast<array_object *>(source.as_heap());
        const std::vector<value> items = arr->items;
        for (std::size_t i = 0; i < items.size(); ++i) {
            if (!arr->element_attrs.empty() || arr->has_hole_run()) {
                const std::uint8_t a = arr->element_attrs_at(static_cast<std::uint32_t>(i));
                if ((a & array_object::elem_hole) != 0 || (a & attr_enumerable) == 0) { continue; }
                if ((a & array_object::elem_accessor) != 0) {
                    into->set(std::to_string(i),
                              lookup_index(source, value::number(static_cast<double>(i))));
                    continue;
                }
            }
            into->set(std::to_string(i), items[i]);
        }
        if (arr->named) { copy_own_properties(target, value::object(arr->named.get())); }
    } else if (source.is_kind(heap_kind::proxy)) {
        // A PROXY IS ASKED, trap by trap, as CopyDataProperties (7.3.25) has
        // it: OwnPropertyKeys through `ownKeys`, then for each key
        // [[GetOwnProperty]] and - an enumerable one only - [[Get]]. It
        // copied nothing at all. With no `ownKeys` trap its target's keys
        // are the list, read off the target itself.
        auto * p = static_cast<proxy_object *>(source.as_heap());
        const std::size_t before = unwinds();
        const auto threw = [&] { return throw_pending() || unwinds() != before; };
        bool failed = false;
        const value trap = proxy_trap(source, "ownKeys", &failed);
        if (failed || threw()) { return; }
        if (!trap.is_callable()) {
            copy_own_properties(target, p->target);
            return;
        }
        const value args[1] = {p->target};
        const value list = call(trap, args, p->handler);
        if (threw()) { return; }
        if (!list.is_array()) {
            throw_error("TypeError", "'ownKeys' on proxy: trap result is not an array");
            return;
        }
        const rooted keep_list{*this, list};
        std::vector<std::string> keys;
        for (const value & key : static_cast<array_object *>(list.as_heap())->items) {
            if (key.is_string()) {
                keys.push_back(static_cast<string_object *>(key.as_heap())->text);
            } else if (key.is_kind(heap_kind::symbol)) {
                keys.push_back(static_cast<symbol_object *>(key.as_heap())->key);
            } else {
                throw_error("TypeError", "'ownKeys' on proxy: a key is neither a string nor "
                                         "a symbol");
                return;
            }
        }
        for (const std::string & key : keys) {
            property_descriptor found;
            const bool present = own_property(source, key, found);
            if (threw()) { return; }
            if (!present || !found.enumerable) { continue; }
            const value item = lookup_property(source, key);
            if (threw()) { return; }
            into->set(key, item);
        }
    } else if (source.is_string()) {
        // ToObject(source) (7.3.25 CopyDataProperties step 3): a String
        // object, whose own enumerable properties are its indices - `{...
        // "ab"}` is {0: "a", 1: "b"} - counted in the unit `length` and
        // charAt count here (lookup_index answers the same one-unit strings).
        const string_object text{static_cast<string_object *>(source.as_heap())->text};
        for (std::size_t i = 0; i < text.length(); ++i) {
            into->set(std::to_string(i), string(text.unit_text(i)));
        }
    }
}

value context::get_prototype(value target) {
    if (target.is_object()) { return static_cast<object_object *>(target.as_heap())->prototype; }
    if (target.is_array()) { return static_cast<array_object *>(target.as_heap())->prototype; }
    // A closure's own [[Prototype]]: the parent class `extends` chained it
    // to, else Function.prototype (10.2.5 / OrdinaryGetPrototypeOf) - which
    // is what `super.x` inside a static method starts from.
    if (target.is_kind(heap_kind::function)) {
        const value link = static_cast<closure_object *>(target.as_heap())->proto_link;
        if (!link.is_null()) { return link; }
        // ...else the intrinsic its shape names: %GeneratorFunction.prototype%
        // for a `function*`, and so on (function_proto_kind).
        object_object * table = prototype(function_proto_kind(target));
        if (table == nullptr) { table = prototype(proto_kind::function); }
        return table != nullptr ? value::object(table) : value::undefined();
    }
    return value::undefined();
}

value context::super_base(value home) {
    const value up = get_prototype(home);
    if (up.is_null() && home.is_object()) {
        if (object_object * table = prototype(proto_kind::object)) { return value::object(table); }
    }
    return up;
}

void context::set_prototype(value target, value proto) {
    if (target.is_object()) { static_cast<object_object *>(target.as_heap())->prototype = proto; }
    if (target.is_array()) { static_cast<array_object *>(target.as_heap())->prototype = proto; }
}

bool context::has_property(value target, value key) {
    if (key.is_string()) {
        const std::string & name = static_cast<string_object *>(key.as_heap())->text;
        if (is_private_key(name)) [[unlikely]] { return has_property(target, name); }
    }
    // A PROXY ANSWERS `in` ITSELF, or hands it to the target.
    if (target.is_kind(heap_kind::proxy)) {
        auto * p = static_cast<proxy_object *>(target.as_heap());
        bool failed = false;
        const value trap = proxy_trap(target, "has", &failed);
        if (failed || throw_pending()) { return false; }
        if (trap.is_callable()) {
            // The trap gets a PROPERTY KEY (ToPropertyKey): the index a
            // generic walk asks about arrives as "3", never as 3.
            const value property = key.is_number() ? string(to_string(key)) : key;
            const value args[2] = {p->target, property};
            const std::size_t before = unwinds();
            const bool answered = truthy(call(trap, args, p->handler));
            if (throw_pending() || unwinds() != before) { return false; }
            // 10.5.7 step 9, the invariant: `false` over a target property that
            // is non-configurable, or that a non-extensible target has at all,
            // is the TypeError.
            if (!answered) {
                const std::string name = to_string(key);
                property_descriptor held;
                if (own_property(p->target, name, held)) {
                    if ((held.has_configurable && !held.configurable) ||
                        !is_extensible(p->target)) {
                        throw_error("TypeError", "'has' on proxy: trap returned falsish for "
                                                 "property '" +
                                                     name +
                                                     "' which exists in the proxy target as "
                                                     "non-configurable, or the target is not "
                                                     "extensible");
                        return false;
                    }
                }
            }
            return answered;
        }
        // 10.5.7 step 7: target.[[HasProperty]] - not a [[Get]], so no
        // getter on the target runs.
        return has_property(p->target, to_string(key));
    }
    return has_property(target, to_string(key));
}

bool context::has_property(value target, const std::string & name) {
    // `#x in o` (13.10.1): the brand check, not the chain walk.
    if (is_private_key(name)) [[unlikely]] {
        if (!target.is_object_like()) {
            throw_error("TypeError", "Cannot check a private member on a non-object");
            return false;
        }
        return find_private_element(target, name).has_value();
    }
    if (target.is_kind(heap_kind::proxy)) { return has_property(target, key_value(name)); }
    // --- HasProperty, 7.3.11: THE WHOLE CHAIN, not the own table ----------
    //
    // `in` used to answer about own DATA properties of an object_object and
    // about array indices, and about nothing else - so `'toString' in {}` was
    // false, `'x' in obj` was false for an accessor, `'length' in [1]` was
    // false, and an inherited property was invisible to the operator whose
    // entire job is to see one. It is also what ToPropertyDescriptor asks with,
    // which is how a descriptor object built by `new Con()` over a prototype
    // carrying a `writable` getter described nothing at all.
    //
    // own_property is the shared [[GetOwnProperty]] over all four tables, so
    // this is that walked up the chain: the explicit prototype links first,
    // then the implicit tables property lookup falls back to.
    property_descriptor found;
    if (own_property(target, name, found)) { return true; }
    // 10.4.5.2: a typed array answers a numeric key itself - an element it
    // does not have is absent, whatever its prototypes hold.
    if (target.is_array() &&
        static_cast<array_object *>(target.as_heap())->elements != element_kind::none &&
        canonical_numeric_key(name)) {
        return false;
    }
    value link = target.is_object()  ? static_cast<object_object *>(target.as_heap())->prototype
                 : target.is_array() ? static_cast<array_object *>(target.as_heap())->prototype
                                     : value::null();
    const bool explicit_chain = target.is_object() || (target.is_array() && !link.is_null());
    // A depth cap because a page can make the chain cyclic, exactly as
    // lookup_property does.
    for (int depth = 0; depth < 64 && link.is_object(); ++depth) {
        if (own_property(link, name, found)) { return true; }
        link = static_cast<object_object *>(link.as_heap())->prototype;
    }
    // A prototype that is not a plain object (`foo.prototype = [1]`) answers
    // for the rest of the chain - see lookup_property.
    if (link.is_heap() && !link.is_object() && !link.is_string()) {
        return has_property(link, name);
    }
    // An explicit null [[Prototype]] (object_object::prototype) ends the chain
    // without the implicit Object.prototype.
    if (explicit_chain && link.is_undefined()) { return false; }
    if (target.is_array() && explicit_chain) { // see instance_of
        object_object * table = prototype(proto_kind::object);
        return table != nullptr &&
               (table->find(name) != nullptr || table->find_accessor(name) != nullptr);
    }
    // A TYPED array's implicit chain starts at its KIND's prototype -
    // Float64Array.prototype, which the lookup reads first too - and only
    // then reaches %TypedArray%.prototype and the tables below.
    if (target.is_array() &&
        static_cast<array_object *>(target.as_heap())->elements != element_kind::none) {
        for (object_object * table = typed_array_prototype(
                 *this, static_cast<array_object *>(target.as_heap())->elements);
             table != nullptr;
             table = table->prototype.is_object()
                         ? static_cast<object_object *>(table->prototype.as_heap())
                         : nullptr) {
            if (table->find(name) != nullptr || table->find_accessor(name) != nullptr) {
                return true;
            }
        }
    }
    for (object_object * table : implicit_prototypes(target)) {
        if (table != nullptr &&
            (table->find(name) != nullptr || table->find_accessor(name) != nullptr)) {
            return true;
        }
    }
    // ...and a function's STATIC chain, which is a third kind of link again.
    for (value up = target; up.is_callable();) {
        if (up.is_kind(heap_kind::function)) {
            auto * fn = static_cast<closure_object *>(up.as_heap());
            up = fn->proto_link;
        } else if (up.is_kind(heap_kind::native)) {
            up = value::null();
        } else {
            break;
        }
        if (!up.is_callable()) { break; }
        if (own_property(up, name, found)) { return true; }
    }
    return false;
}

// 13.10.2 InstanceofOperator, and 7.3.22 OrdinaryHasInstance after it.
//
// The right-hand side must be an object; its @@hasInstance, when it has one,
// answers (ToBoolean of a call); otherwise it must be callable. The intrinsic
// %Function.prototype[@@hasInstance]% IS OrdinaryHasInstance, so finding it -
// which is what every ordinary `x instanceof F` does - skips the call and
// walks the chain directly (builtins/objects/function.cpp has the method
// itself, written over the public operations).
bool context::instance_of(value target, value ctor) {
    if (!ctor.is_object_like()) {
        throw_error("TypeError", "Right-hand side of 'instanceof' is not an object");
        return false;
    }
    const std::size_t before = unwinds();
    const value handler = lookup_property(ctor, CTBROWSER_SYMBOL_KEY("hasInstance"));
    if (throw_pending() || unwinds() != before) { return false; }
    if (!handler.is_nullish()) {
        object_object * fn_proto = prototype(proto_kind::function);
        const value * intrinsic =
            fn_proto == nullptr ? nullptr : fn_proto->find(CTBROWSER_SYMBOL_KEY("hasInstance"));
        if (intrinsic == nullptr || !handler.same_value(*intrinsic)) {
            if (!handler.is_callable()) {
                throw_error("TypeError", "Symbol.hasInstance of the right-hand side of "
                                         "'instanceof' is not callable");
                return false;
            }
            const value args[1] = {target};
            const value answered = call(handler, args, ctor);
            if (throw_pending() || unwinds() != before) { return false; }
            return truthy(answered);
        }
    }
    if (!ctor.is_callable()) {
        throw_error("TypeError", "Right-hand side of 'instanceof' is not callable");
        return false;
    }
    // OrdinaryHasInstance step 2: a bound function asks about its target,
    // with the whole operator.
    if (ctor.is_kind(heap_kind::native)) {
        if (const value * bound = static_cast<native_object *>(ctor.as_heap())
                                      ->find(CTBROWSER_PRIVATE_KEY("BoundTargetFunction"))) {
            return instance_of(target, *bound);
        }
    }
    // Step 3: a primitive is an instance of nothing - `5 instanceof Number`
    // is false however many methods a number resolves.
    if (!target.is_object_like()) { return false; }
    value wanted = value::undefined();
    if (ctor.is_kind(heap_kind::function)) {
        wanted = ensure_prototype(ctor);
    } else if (ctor.is_kind(heap_kind::proxy)) {
        wanted = lookup_property(ctor, "prototype");
        if (throw_pending() || unwinds() != before) { return false; }
    } else if (ctor.is_kind(heap_kind::native)) {
        // A BUILT-IN constructor is a native, and every one of them is now
        // something a page can extend - `class E extends Error`. Without this,
        // `e instanceof Error` was false for every one.
        if (value * p = static_cast<native_object *>(ctor.as_heap())->find("prototype")) {
            wanted = *p;
        }
    }
    // Step 5: Get(C, "prototype") must be an object.
    if (!wanted.is_object_like()) {
        throw_error("TypeError", "Function has non-object prototype in instanceof check");
        return false;
    }
    if (!wanted.is_object()) { return false; }
    // A PROXY IS ASKED THROUGH TO ITS TARGET. 7.3.21 OrdinaryHasInstance calls
    // `[[GetPrototypeOf]]`, and a proxy's forwards to the object behind it - so
    // `x instanceof C` for a proxy is a question about that object's chain and
    // not about the proxy, which has no prototype field at all. `window` is a
    // proxy and so is every live DOM collection, and `document.images
    // instanceof HTMLCollection` was false for exactly this reason while the
    // target carried the right prototype the whole time. Bounded, because a
    // proxy of a proxy is legal and a cycle must not be.
    value subject = target;
    for (int hops = 0; hops < 8 && subject.is_kind(heap_kind::proxy); ++hops) {
        auto * p = static_cast<proxy_object *>(subject.as_heap());
        // ...UNLESS IT ANSWERS ITSELF: a `getPrototypeOf` trap is
        // [[GetPrototypeOf]] (10.5.1), and its answer is where the walk goes
        // on - held to the target when the target is not extensible (steps
        // 8-11), which is a TypeError otherwise.
        bool failed = false;
        const value trap = proxy_trap(subject, "getPrototypeOf", &failed);
        if (failed || throw_pending() || unwinds() != before) { return false; }
        if (!trap.is_callable()) {
            subject = p->target;
            continue;
        }
        const value args[1] = {p->target};
        const value answered = call(trap, args, p->handler);
        if (throw_pending() || unwinds() != before) { return false; }
        if (!answered.is_object_like() && !answered.is_null()) {
            throw_error("TypeError", "'getPrototypeOf' on proxy: trap returned neither object "
                                     "nor null");
            return false;
        }
        const bool extensible = is_extensible(p->target);
        if (throw_pending() || unwinds() != before) { return false; }
        if (!extensible) {
            value actual = get_prototype(p->target);
            if (actual.is_null()) {
                object_object * table = implicit_prototypes(p->target)[0];
                actual = table != nullptr ? value::object(table) : value::null();
            }
            if (actual.is_undefined()) { actual = value::null(); } // an explicit null
            if (!actual.same_value(answered)) {
                throw_error("TypeError", "'getPrototypeOf' on proxy: proxy target is "
                                         "non-extensible but the trap did not return its "
                                         "actual prototype");
                return false;
            }
        }
        if (!answered.is_object_like()) { return false; }
        if (answered.as_heap() == wanted.as_heap()) { return true; }
        return instance_of(answered, ctor); // the walk goes on from `answered`
    }
    // The EXPLICIT chain first - a page's own classes, and every builtin whose
    // instances carry a prototype (Error, Map, Blob).
    // ...and an array's own, when it has one (array_object::prototype), and a
    // function's proto_link - `class B extends A` for B itself, and a dynamic
    // function made under `class F extends Function` (`f instanceof F`).
    value link = subject.is_object()  ? static_cast<object_object *>(subject.as_heap())->prototype
                 : subject.is_array() ? static_cast<array_object *>(subject.as_heap())->prototype
                 : subject.is_kind(heap_kind::function)
                     ? static_cast<closure_object *>(subject.as_heap())->proto_link
                 : subject.is_kind(heap_kind::native)
                     ? static_cast<native_object *>(subject.as_heap())->proto_link
                     : value::null();
    const bool explicit_chain = subject.is_object() || !link.is_null();
    for (int depth = 0; depth < 64 && link.is_object(); ++depth) {
        if (link.as_heap() == wanted.as_heap()) { return true; }
        link = static_cast<object_object *>(link.as_heap())->prototype;
    }
    // A prototype that is not a plain object (`foo.prototype = [1]`) carries
    // the rest of the chain - see lookup_property.
    if (link.is_heap() && !link.is_object() && !link.is_string()) {
        if (link.as_heap() == wanted.as_heap()) { return true; }
        return instance_of(link, ctor); // ctor's own @@hasInstance was the intrinsic
    }
    if (explicit_chain && link.is_undefined()) { return false; } // an explicit null
    // An array's explicit chain ended at the implicit Object.prototype: the
    // kind's tables are not behind it (Object.setPrototypeOf(arr, o) took
    // Array.prototype out of the chain).
    if (!subject.is_object() && explicit_chain) {
        return prototype(proto_kind::object) == wanted.as_heap();
    }
    // Then the IMPLICIT one. An array, a function, a string and a plain object
    // have no prototype field to walk - their chain is the tables property
    // lookup falls back to - so instanceof answered false for every builtin
    // while answering correctly for a page's own classes.
    //
    // OBJECT-LIKE ONLY. `5 instanceof Number` and `'x' instanceof String` are
    // FALSE in JavaScript however many methods a primitive resolves -
    // instanceof asks about a prototype chain and a primitive does not have
    // one. Applying the fallback to everything made both of those true, which
    // is the mirror image of the bug being fixed.
    if (subject.is_object_like()) {
        for (object_object * table : implicit_prototypes(subject)) {
            if (table != nullptr && table == wanted.as_heap()) { return true; }
        }
    }
    // A TYPED ARRAY's chain starts at its kind's own prototype (23.2.7),
    // which is not one of the implicit tables.
    if (subject.is_array()) {
        auto * arr = static_cast<array_object *>(subject.as_heap());
        for (object_object * table = typed_array_prototype(*this, arr->elements);
             table != nullptr;) {
            if (table == wanted.as_heap()) { return true; }
            table = table->prototype.is_object()
                        ? static_cast<object_object *>(table->prototype.as_heap())
                        : nullptr;
        }
    }
    return false;
}

} // namespace ctbrowser::script
