#pragma once
#include <algorithm>
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

#include <ctbrowser/core/algorithms.hpp>
#include <ctbrowser/core/core.hpp>

#include <ctbrowser/script/bytecode.hpp>
#include <ctbrowser/script/dispatch.hpp>
#include <ctbrowser/script/type_record.hpp>
#include <ctbrowser/script/value.hpp>

// The interpreter: a flat array of 4-byte instructions with registers already
// assigned.
//
// GC is mark-and-sweep over precise roots - the register stack, the globals
// table, the call frames and the inventory in each_root - so there is no
// conservative stack scanning and no pointer-shaped integer can keep an object
// alive.
//
// One agent per thread, like a real JS agent: a context is NOT thread-safe
// and is not meant to be. Workers get their own context; what they share is
// the DOM, which has its own concurrency control.

namespace ctbrowser::script {

struct closure_object;

// type_record.hpp is included rather than forward-declared because each_root
// hands its visitor the `root_label` vocabulary declared there.

using native_fn = std::function<value(class context &, std::span<value>)>;

// THE LINEAR SCAN ON `native_object` AND `closure_object` BELOW IS DELIBERATE,
// AND IT WAS MEASURED. Callgrind on `babylon_ratchet` (46.577 G instructions):
//
//   closure_object::find    64,466,020   0.14%
//   native_object::find      2,666,456   0.01%
//
// A `string_flat_map` index built past a threshold took the pair from 5.14 M to
// 9.54 M instructions - the scan-or-index branch touches the map's header, and
// two inlined bodies became one out-of-line call. `ensure_prototype` is lazy,
// so MOST CLOSURES HOLD NOTHING AT ALL and the loop exits on an empty vector.
// Do not re-propose a hash table here without a profile that puts these two
// functions somewhere near the top.
struct native_object final : heap_object {
    std::string name;
    native_fn fn;
    // A NATIVE IS A FUNCTION THAT IS ALSO AN OBJECT. `Symbol` has to be both -
    // `Symbol('x')` calls it and `Symbol.iterator` reads a property of it - and
    // without a table here the two were mutually exclusive. Closures have had
    // one since classes needed somewhere for their statics; this is the same
    // need arriving for the built-ins.
    std::vector<std::pair<std::string, value>> props;
    // Parallel to `props`, grown lazily - see the long note on
    // object_object::attrs. A built-in constructor's statics are here, and the
    // specification says none of them is enumerable.
    std::vector<std::uint8_t> attrs;
    bool extensible = true;
    // Accessor keys retain their position in props with an undefined payload.
    // That keeps data/accessor replacement in the same property order; find()
    // only exposes data slots, while the VM invokes the separate descriptor.
    accessor_table accessors;

    // WHAT THIS NATIVE'S C++ LAMBDA IS HOLDING - the one root the collector
    // could not otherwise have. A `value` captured by a C++ lambda is invisible
    // to a precise collector: the captures live inside a std::function's erased
    // storage, and `each_root` has no inventory of them. Anything that captures
    // a value in a native lambda belongs here. A growing set is better made a
    // heap object and retained as one handle, which is what `Symbol.for`'s
    // registry does.
    std::vector<value> retained;

    [[nodiscard]] value * find(std::string_view key) {
        if (accessors.find(key) != nullptr) { return nullptr; }
        for (auto & [k, item] : props) {
            if (k == key) { return &item; }
        }
        return nullptr;
    }
    [[nodiscard]] std::size_t position(std::string_view key) const {
        for (std::size_t i = 0; i < props.size(); ++i) {
            if (props[i].first == key) { return i; }
        }
        return props.size();
    }
    [[nodiscard]] std::uint8_t attrs_of(std::string_view key) const {
        const std::size_t at = position(key);
        return at < attrs.size() ? attrs[at] : attr_default;
    }
    void set_attrs(std::string_view key, std::uint8_t a) {
        const std::size_t at = position(key);
        if (at >= props.size()) { return; }
        if (a == attr_default && attrs.empty()) { return; }
        if (attrs.size() < props.size()) { attrs.resize(props.size(), attr_default); }
        attrs[at] = a;
    }
    void set(std::string_view key, value v) {
        accessors.erase(key);
        if (!attrs.empty() && attrs.size() != props.size()) {
            attrs.resize(props.size(), attr_default);
        }
        if (value * existing = find(key)) {
            *existing = v;
            return;
        }
        props.emplace_back(std::string{key}, v);
        if (!attrs.empty()) { attrs.push_back(attr_default); }
    }
    void define(std::string_view key, value v, std::uint8_t a) {
        set(key, v);
        set_attrs(key, a);
    }
    [[nodiscard]] accessor_entry * find_accessor(std::string_view key) {
        return accessors.find(key);
    }
    void define_accessor(std::string_view key, value getter, value setter,
                         std::uint8_t a = attr_enumerable | attr_configurable) {
        set(key, value::undefined());
        set_attrs(key, a);
        accessors.define(key, getter, setter, 0, a);
    }
    bool erase(std::string_view key) {
        const std::size_t at = position(key);
        if (at >= props.size()) { return false; }
        if (key == "name") { name_erased = true; }
        accessors.erase(key);
        props.erase(props.begin() + static_cast<std::ptrdiff_t>(at));
        if (at < attrs.size()) { attrs.erase(attrs.begin() + static_cast<std::ptrdiff_t>(at)); }
        return true;
    }

    // THE FUNCTION'S OWN [[Prototype]], which a closure has had since Babel's
    // `_inherits` needed one. A NativeError constructor's is %Error% (20.5.6.2)
    // rather than Function.prototype, so `Object.getPrototypeOf(TypeError)` is
    // `Error` and a static on Error is inherited by all six.
    // WAS THE OWN `name` DELETED? `context::own_property` synthesises a native's
    // `name` out of the C++ object when the table has none, which is right for a
    // native that never had one installed and wrong after a `delete`: the
    // synthesised slot uncovers, `hasOwnProperty("name")` stays true, and that
    // is precisely what test262's `verifyProperty` asks when it checks the
    // descriptor is configurable. A flag rather than deleting the fallback,
    // because every native `define_native` makes - the DOM bindings, setTimeout,
    // 400 others - has no own entry and still has to answer.
    bool name_erased = false;
    value proto_link = value::null();
    // IsConstructor (7.2.4) for a native: a built-in METHOD, a getter and a
    // promise reaction have no [[Construct]], so `new Math.abs()` is a
    // TypeError. True by default because every native an embedder defines
    // (`Image`, `DOMParser`, ...) is constructed and has no other way to say so.
    bool is_constructor = true;

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
    // statics and its `prototype` live here. Linear on purpose - see the note
    // above `native_object`.
    std::vector<std::pair<std::string, value>> props;
    // Parallel to `props`, grown lazily - see object_object::attrs. A class's
    // statics live here, and `C.prototype` is { writable: false, enumerable:
    // false, configurable: false } on one.
    std::vector<std::uint8_t> attrs;
    bool extensible = true;

    [[nodiscard]] value * find(std::string_view name) {
        for (auto & [key, item] : props) {
            if (key == name) { return &item; }
        }
        return nullptr;
    }
    [[nodiscard]] std::size_t position(std::string_view name) const {
        for (std::size_t i = 0; i < props.size(); ++i) {
            if (props[i].first == name) { return i; }
        }
        return props.size();
    }
    [[nodiscard]] std::uint8_t attrs_of(std::string_view name) const {
        const std::size_t at = position(name);
        return at < attrs.size() ? attrs[at] : attr_default;
    }
    void set_attrs(std::string_view name, std::uint8_t a) {
        const std::size_t at = position(name);
        if (at >= props.size()) { return; }
        if (a == attr_default && attrs.empty()) { return; }
        if (attrs.size() < props.size()) { attrs.resize(props.size(), attr_default); }
        attrs[at] = a;
    }
    void set(std::string_view name, value v) {
        if (!attrs.empty() && attrs.size() != props.size()) {
            attrs.resize(props.size(), attr_default);
        }
        if (value * existing = find(name)) {
            *existing = v;
            return;
        }
        props.emplace_back(std::string{name}, v);
        if (!attrs.empty()) { attrs.push_back(attr_default); }
    }
    void define(std::string_view name, value v, std::uint8_t a) {
        set(name, v);
        set_attrs(name, a);
    }
    bool erase(std::string_view name) {
        const std::size_t at = position(name);
        if (at >= props.size()) { return false; }
        props.erase(props.begin() + static_cast<std::ptrdiff_t>(at));
        if (at < attrs.size()) { attrs.erase(attrs.begin() + static_cast<std::ptrdiff_t>(at)); }
        return true;
    }

    // A CLASS IS A CLOSURE, so `static get w()` has nowhere else to go. Same
    // table as an object's, and empty on every function that is not a class
    // with a static accessor.
    accessor_table accessors;
    [[nodiscard]] accessor_entry * find_accessor(std::string_view name) {
        return accessors.find(name);
    }
    void define_accessor(std::string_view name, value getter, value setter,
                         std::uint8_t a = attr_enumerable | attr_configurable) {
        accessors.define(name, getter, setter, 0, a);
    }

    // The function's OWN [[Prototype]] - what `Object.getPrototypeOf(F)`
    // returns - which is a different thing from the `prototype` PROPERTY that
    // its instances get. Babel's `_inherits` sets both: the subclass's
    // prototype property chains to the superclass's for instance methods, and
    // the subclass FUNCTION chains to the superclass function for static ones.
    value proto_link = value::null();

    // WERE THE SYNTHESISED `name` / `length` DELETED? Both are answered off the
    // compiled function rather than stored (see context::own_property), so
    // without a memory a `delete f.name` uncovered the same answer again and
    // `hasOwnProperty("name")` stayed true - the question test262's
    // verifyProperty asks to decide `name` is configurable. The native_object
    // flag above has the same story.
    bool name_erased = false;
    bool length_erased = false;

    const function_proto * proto = nullptr;
    // WHICH PROGRAM ITS NESTED FUNCTIONS LIVE IN. `op::closure` names a
    // function by INDEX, and the index only means anything in the program it
    // was compiled against - a context runs more than one.
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
    // And the DEFERRED one (`import defer * as ns`, 16.2.2), likewise once.
    value deferred_namespace_object = value::undefined();
    // Evaluated ONCE, however many modules import it. The flag is the whole of
    // "a module is a singleton".
    bool evaluated = false;
};

struct run_result {
    value returned = value::undefined();
    bool ok = true;
    std::string error;
};

// `import.source(x)` (13.3.10.1.1 EvaluateImportCall, phase source), as a
// hidden native the compiler calls: ToString the specifier, then answer a
// promise REJECTED with the SyntaxError GetModuleSource of a source text
// module always is (16.2.1.7.2) - or with what the ToString threw. A name a
// page cannot shadow, like the ones in builtins.hpp; declared here because
// the compiler and the VM's own builtins share it and this is the header
// both include.
inline constexpr std::string_view import_source_name = "__ctbrowser_import_source";
// `using` / `await using` (explicit resource management, 9.13), as five
// hidden natives the compiler calls around a protected region - see
// compile/statements/using.cpp for the lowering and builtins/objects/
// function.cpp for the bodies. `stack()` makes the DisposeCapability;
// `add(stack, v, async)` is AddDisposableResource and answers v; `dispose
// (stack, kind, value)` is DisposeResources for a sync stack, handed the
// completion in flight (kind 1 = a throw of `value`) and throwing what comes
// out; `step(stack, kind, value)` disposes ONE resource of an async stack and
// answers what to await, or the stack itself when it is empty (throwing the
// folded completion then); `failed(stack, e)` folds an awaited rejection in.
// GetTemplateObject (13.2.8.4) for a tagged template: `(key, cooked, raw)`
// answers the frozen strings array - `raw` frozen and hung off it - cached
// per site under `key`, so the same site hands the same object to its tag
// on every evaluation. See compile_tagged.
inline constexpr std::string_view template_object_name = "__ctbrowser_template_object";
// PutValue on an unresolvable reference in STRICT code (6.2.5.6 step 3.a): the
// compiler calls this with the name before a set_global that is an
// ASSIGNMENT (never a declaration's own write), and it throws the
// ReferenceError when the name is neither a global nor on the global object.
// A call rather than a check inside op::set_global, whose contract says it
// cannot throw.
inline constexpr std::string_view strict_assign_check_name = "__ctbrowser_strict_assign";
// PrivateFieldAdd / PrivateMethodOrAccessorAdd (7.3.28, 7.3.29): `(obj, key,
// v)` defines the private element - a field's value, or the class BRAND (see
// context::private_element_present) as undefined - and it is the TypeError
// when the object already carries the key (a constructor that returns the
// same object twice) or is not extensible.
inline constexpr std::string_view private_add_name = "__ctbrowser_private_add";
// ClassDefinitionEvaluation steps 6-9 (15.7.14) for `class C extends P`:
// `(C, P, C.prototype)` checks P is null or a constructor and P.prototype an
// object or null - each a TypeError otherwise - then chains C.prototype to
// P.prototype and C itself to P (or to Function.prototype for null).
inline constexpr std::string_view class_heritage_name = "__ctbrowser_class_heritage";
// `super.x` / `super[k]` READ (13.3.7.3, 6.2.5.5 GetValue of a Super
// Reference): `(base, key, this)` is base.[[Get]](key, this) - a getter on
// the parent runs with the method's own receiver, not with the parent.
inline constexpr std::string_view super_get_name = "__ctbrowser_super_get";
// A direct `eval(src)` written in a function's PARAMETER EXPRESSIONS:
// `(src, name...)` is `eval` (the intrinsic - anything else the name is
// bound to is simply called) with the one rule of 19.2.1.3
// EvalDeclarationInstantiation step 3.d this engine can keep without a
// caller-scoped eval: a `var` the eval'd code declares may not be one of the
// names bound in that parameter scope - the parameters, and `arguments`
// unless the function is an arrow or names a parameter so - which is the
// SyntaxError the eval throws.
inline constexpr std::string_view param_eval_name = "__ctbrowser_param_eval";
// `delete o.k` / `delete o[k]` (13.5.1.2): `(obj, key, strict, super)` is
// ToObject(obj).[[Delete]](ToPropertyKey(key)) with its ANSWER - the opcodes
// delete_prop/delete_index produce none - a TypeError for a null or undefined
// base, a TypeError in strict code when the delete answers false, and a
// ReferenceError for `delete super.x` (`super` true; the key is still
// evaluated first).
inline constexpr std::string_view delete_ref_name = "__ctbrowser_delete";
// InitializeInstanceElements (7.3.34) for a DERIVED class, `(this, C)` - or
// `(this, C.prototype)`, the home object, whose own `constructor` is C: run C's
// `__fields` on the object `super()` just bound as `this` - which is where
// 10.2.1.3 / 13.3.7.1's super call runs them, and why a base constructor's
// `Object.preventExtensions(this)` or a returned object is what the derived
// fields meet. The compiler emits it after every `super(...)`; a base class's
// fields still run at construct entry (context::run_field_initialisers).
inline constexpr std::string_view init_fields_name = "__ctbrowser_init_fields";
// `super(...)` returned `(result)`: BindThisValue (10.2.1.3) with what the
// parent constructor answered - the object it returned (a "return
// override") replaces the instance as `this` for the rest of the derived
// constructor, and a derived constructor's implicit return hands back
// `this`, so `new` evaluates to it. The compiler emits it right after every
// super call, before the fields.
inline constexpr std::string_view bind_this_name = "__ctbrowser_bind_this";
inline constexpr std::string_view using_stack_name = "__ctbrowser_using_stack";
inline constexpr std::string_view using_add_name = "__ctbrowser_using_add";
inline constexpr std::string_view using_dispose_name = "__ctbrowser_using_dispose";
inline constexpr std::string_view using_step_name = "__ctbrowser_using_step";
inline constexpr std::string_view using_failed_name = "__ctbrowser_using_failed";

} // namespace ctbrowser::script
