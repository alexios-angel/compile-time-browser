#pragma once
//===- objects.hpp - Realms, functions and modules of the VM ----*- C++ -*-===//
//
// The VM's realm record and intrinsics, the native and closure function
// objects, captured-variable cells, module records, and the names of the
// hidden natives the compiler emits calls to.
//
// The interpreter runs a flat array of 4-byte instructions with registers
// already assigned. GC is mark-and-sweep over precise roots (the register
// stack, the globals table, the call frames and the inventory in each_root),
// so there is no conservative stack scanning and no pointer-shaped integer
// can keep an object alive.
//
// One agent per thread, like a real JS agent: a context is not thread-safe
// and is not meant to be. Workers get their own context; what they share is
// the DOM, which has its own concurrency control.
//
//===----------------------------------------------------------------------===//

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
// Included rather than forward-declared because each_root hands its visitor
// the `root_label` vocabulary declared there.
#include <ctbrowser/script/type_record.hpp>
#include <ctbrowser/script/value.hpp>

namespace ctbrowser::script {

struct closure_object;

/// The mark of a module namespace object (context::module_namespace): its
/// exports are getters over the cells, which [[GetOwnProperty]] and
/// [[DefineOwnProperty]] answer as the data properties 10.4.6 says they are.
inline constexpr std::string_view module_namespace_key = CTBROWSER_PRIVATE_KEY("ModuleNamespace");

/// How many value kinds have a prototype: context::proto_kind::count_, which
/// vm.hpp checks against this.
inline constexpr std::size_t realm_prototype_count = 19;

/// [[Intrinsics]] (9.3), the part the VM itself reaches for: the prototype of
/// each value kind (context::prototype, indexed by proto_kind), the
/// NativeError prototypes a throw site names (context::error_prototype) and
/// %ThrowTypeError%. They are GC roots through every realm that holds them
/// (each_root).
struct realm_intrinsics {
    std::array<object_object *, realm_prototype_count> prototypes{};
    std::vector<std::pair<std::string, object_object *>> error_prototypes;
    value throw_type_error = value::undefined();
};

/// A realm (ECMA-262 9.3): the global environment a script's free names
/// resolve in, that is, the binding table, the global object and the
/// embedder's answer for a name that is in neither. A page is one; every
/// frame that runs its own scripts is another, so a function a frame declared
/// still reads the frame's `document` when the page calls it (10.2.1.1
/// PrepareForOrdinaryCall step 6: the callee's realm becomes the current
/// one). Owned by the context for its whole life, since a closure points at
/// the realm it was made in.
///
/// A frame's intrinsics are still shared: `frame.contentWindow.Array ===
/// Array` here, where a browser has two. The binding table starts as a copy
/// of the page's template and the intrinsics are the page's (see
/// context::create_realm), so a frame sees the same constructors; what it
/// does not see is the page's own `var`s. A ShadowRealm's intrinsics are its
/// own: the builtins are installed again inside it (builtins'
/// create_intrinsic_realm).
/// Whether `name` spells an engine-internal helper the compiler calls (the
/// class helpers: `%class_defined` and the rest). No JavaScript identifier
/// starts with `%`, so no script can name one with get_global or set_global,
/// and the helpers live in realm_record::internals, which no write a script
/// makes reaches. A page assigning `__ctbrowser_class_defined` or writing
/// `globalThis['%class_defined']` creates an ordinary global, as in any other
/// engine, and its classes behave exactly as before.
[[nodiscard]] constexpr bool internal_global(std::string_view name) noexcept {
    return !name.empty() && name.front() == '%';
}

struct realm_record {
    string_flat_map<value> globals;
    /// The engine-internal helpers (internal_global names), apart from the
    /// globals table so a script cannot replace or read them.
    string_flat_map<value> internals;
    /// The names a script declared (`var`, and a block function's Annex B var):
    /// enumerable and not configurable, so `delete` refuses them (16.1.7
    /// CreateGlobalVarBinding).
    string_flat_map<bool> declared;
    /// The global environment's two name lists (9.1.1.4): every `var` and
    /// function a script declared ([[VarNames]]) and every top-level `let`,
    /// `const` and `class` ([[DeclarativeRecord]]'s names). A later script
    /// colliding with either is refused before it runs
    /// (context::global_declaration_instantiation), and a lexical name is not a
    /// property of the global object, though its value lives in `globals`.
    string_flat_map<bool> var_names;
    string_flat_map<bool> lexical;
    /// The platform's names: every intrinsic (clause 19) and every interface
    /// object or namespace a binding installed (WebIDL 3.7.1), which are
    /// { writable, !enumerable, configurable }. Any other binding is a
    /// script's plain assignment or a member of the Window itself, { writable,
    /// enumerable, configurable }. A `delete` forgets the mark with the
    /// binding.
    string_flat_map<bool> builtin;
    /// Host interface identity survives deleting or replacing its global.
    /// structuredClone reads these registered prototypes, never page bindings.
    string_flat_map<value> platform_prototypes;
    value global_this = value::undefined();
    /// See context::set_undeclared_name_hook.
    std::function<value(std::string_view)> undeclared_name;
    /// Its intrinsics, read through the current realm, so code running in a
    /// ShadowRealm finds that realm's `[].push`, and an object with no
    /// prototype of its own inherits the current realm's (implicit_prototypes).
    /// A frame's are the page's, shared (context::create_realm).
    std::shared_ptr<realm_intrinsics> intrinsics = std::make_shared<realm_intrinsics>();
};

/// The C++ signature of a native function: the context and the arguments.
using native_fn = std::function<value(class context &, std::span<value>)>;

// The linear scans on `native_object` and `closure_object` below are
// deliberate, and measured. Callgrind on `babylon_ratchet` (46.577 G
// instructions):
//
//   closure_object::find    64,466,020   0.14%
//   native_object::find      2,666,456   0.01%
//
// A `string_flat_map` index built past a threshold took the pair from 5.14 M
// to 9.54 M instructions: the scan-or-index branch touches the map's header,
// and two inlined bodies became one out-of-line call. `ensure_prototype` is
// lazy, so most closures hold nothing at all and the loop exits on an empty
// vector. Do not re-propose a hash table here without a profile that puts
// these two functions somewhere near the top.

/// A function implemented in C++: a callable heap object with its own property
/// table.
struct native_object final : heap_object {
    std::string name;
    native_fn fn;
    /// Own properties: a native is also an object. `Symbol` has to be both:
    /// `Symbol('x')` calls it and `Symbol.iterator` reads a property of it.
    std::vector<std::pair<std::string, value>> props;
    /// Parallel to `props`, grown lazily; see the long note on
    /// object_object::attrs. A built-in constructor's statics are here, and the
    /// specification says none of them is enumerable.
    std::vector<std::uint8_t> attrs;
    bool extensible = true;
    /// Accessor keys retain their position in props with an undefined payload.
    /// That keeps data/accessor replacement in the same property order; find()
    /// only exposes data slots, while the VM invokes the separate descriptor.
    accessor_table accessors;

    /// What this native's C++ lambda is holding, the one root the collector
    /// could not otherwise see. A `value` captured by a C++ lambda is invisible
    /// to a precise collector: the captures live inside a std::function's
    /// erased storage, and `each_root` has no inventory of them. Anything that
    /// captures a value in a native lambda belongs here. A growing set is
    /// better made a heap object and retained as one handle, which is what
    /// `Symbol.for`'s registry does.
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

    /// Whether the own `name` was deleted. `context::own_property` synthesises a
    /// native's `name` out of the C++ object when the table has none, which is
    /// right for a native that never had one installed and wrong after a
    /// `delete`: the synthesised slot would reappear, `hasOwnProperty("name")`
    /// would stay true, and that is exactly what test262's `verifyProperty`
    /// asks when it checks the descriptor is configurable. A flag rather than
    /// deleting the fallback, because every native `define_native` makes (the
    /// DOM bindings, setTimeout and 400 others) has no own entry and still has
    /// to answer.
    bool name_erased = false;
    /// The function's own [[Prototype]]. A NativeError constructor's is %Error%
    /// (20.5.6.2) rather than Function.prototype, so
    /// `Object.getPrototypeOf(TypeError)` is `Error` and a static on Error is
    /// inherited by all six.
    value proto_link = value::null();
    /// IsConstructor (7.2.4) for a native: a built-in method, a getter and a
    /// promise reaction have no [[Construct]], so `new Math.abs()` is a
    /// TypeError. True by default because every native an embedder defines
    /// (`Image`, `DOMParser`, ...) is constructed and has no other way to say so.
    bool is_constructor = true;

    native_object(std::string n, native_fn f)
        : heap_object(heap_kind::native), name(std::move(n)), fn(std::move(f)) {}
};

/// A captured variable's box. Sharing the cell rather than the value is what
/// makes a mutation through a closure visible to everyone else holding it.
struct cell_object final : heap_object {
    value slot;
    explicit cell_object(value v) : heap_object(heap_kind::cell), slot(v) {}
};

/// A JavaScript function made from compiled bytecode.
struct closure_object final : heap_object {
    /// Own properties. A function is an object in JavaScript, and a class
    /// compiles to one: its statics and its `prototype` live here. Linear on
    /// purpose; see the note above `native_object`.
    std::vector<std::pair<std::string, value>> props;
    /// Parallel to `props`, grown lazily; see object_object::attrs. A class's
    /// statics live here, and `C.prototype` is { writable: false, enumerable:
    /// false, configurable: false } on one.
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

    /// Static accessors. A class is a closure, so `static get w()` has nowhere
    /// else to go. Same table as an object's, and empty on every function that
    /// is not a class with a static accessor.
    accessor_table accessors;
    [[nodiscard]] accessor_entry * find_accessor(std::string_view name) {
        return accessors.find(name);
    }
    void define_accessor(std::string_view name, value getter, value setter,
                         std::uint8_t a = attr_enumerable | attr_configurable) {
        accessors.define(name, getter, setter, 0, a);
    }

    /// The function's own [[Prototype]], what `Object.getPrototypeOf(F)`
    /// returns, which is a different thing from the `prototype` property its
    /// instances get. Babel's `_inherits` sets both: the subclass's prototype
    /// property chains to the superclass's for instance methods, and the
    /// subclass function chains to the superclass function for static ones.
    value proto_link = value::null();

    /// Whether the synthesised `name` / `length` were deleted. Both are answered
    /// off the compiled function rather than stored (see context::own_property),
    /// so without this flag a `delete f.name` would uncover the same answer
    /// again and `hasOwnProperty("name")` would stay true, which is the question
    /// test262's verifyProperty asks to decide `name` is configurable. Same as
    /// native_object::name_erased.
    bool name_erased = false;
    bool length_erased = false;
    /// Set on a derived class's constructor while its body is being evaluated,
    /// so a static method is defined on it rather than [[Set]] through the
    /// parent class's statics; see object_object::defining_class.
    bool defining_class = false;
    /// Whether this is a class's constructor (15.7.14 step 12:
    /// [[IsClassConstructor]]), set by the class_defined native once the class
    /// exists. It has no [[Call]] (10.2.1 step 2), which
    /// context::refuse_class_call enforces.
    bool class_constructor = false;
    /// [[ConstructorKind]] stays derived even if its prototype link changes.
    bool derived_constructor = false;

    const function_proto * proto = nullptr;
    /// Which program its nested functions live in. `op::closure` names a
    /// function by index, and the index only means anything in the program it
    /// was compiled against; a context runs more than one.
    const program * owner = nullptr;
    std::vector<value> upvalues; // Each one is a cell_object.
    /// Only meaningful when proto->is_arrow: the `this` in scope where the arrow
    /// was written, captured when the closure was made. An arrow has no receiver
    /// of its own, so this is the only place its `this` can come from. In a
    /// derived constructor this is a shared cell, since super() binds it later.
    value captured_this = value::undefined();
    /// [[Realm]] (10.2.3 OrdinaryFunctionCreate step 4): the realm current when
    /// this closure was made, which is the one its body resolves free names in.
    /// Null only for a closure the embedder built by hand; a frame for one runs
    /// in whatever realm is current.
    realm_record * realm = nullptr;
    explicit closure_object(const function_proto * p)
        : heap_object(heap_kind::function), proto(p) {}
};

/// One module, as the runtime sees it.
///
/// `exports` maps an exported name to the cell holding it, not to a value.
/// That is what makes an imported binding live: the importer is handed the
/// same box, so a later write by the exporter is a write the importer reads.
/// Handing over the value instead would pass a test that two modules can see
/// each other and fail the one that matters, which is why docs/plans/modules.md
/// names it as the shortcut to refuse.
struct module_record {
    std::string specifier;
    const program * compiled = nullptr;
    flat_map<std::string, value> exports;
    /// Maps a specifier as written to the one the registry is keyed by.
    /// `./dep.js` means a different file depending on which module wrote it,
    /// and the bytecode can only carry what was written. Resolving is the
    /// loader's job (it knows about paths, and eventually URLs and import
    /// maps), so it leaves the answer here and `op::load_import` looks it up
    /// rather than doing any path arithmetic of its own.
    flat_map<std::string, std::string> resolved;
    /// The namespace object, one per module and made on demand: two
    /// `import * as` of the same module must give the same object. See
    /// context::module_namespace.
    value namespace_object = value::undefined();
    /// And the deferred one (`import defer * as ns`, 16.2.2), likewise made once.
    value deferred_namespace_object = value::undefined();
    /// Evaluated once, however many modules import it. This flag is the whole
    /// of "a module is a singleton".
    bool evaluated = false;
};

/// The outcome of running a script: its completion value, or ok false and the
/// failure message.
struct run_result {
    value returned = value::undefined();
    bool ok = true;
    std::string error;
};

/// `import.source(x)` (13.3.10.1.1 EvaluateImportCall, phase source), as a
/// hidden native the compiler calls: ToString the specifier, then answer a
/// promise rejected with the SyntaxError GetModuleSource of a source text
/// module always is (16.2.1.7.2), or with what the ToString threw. A name a
/// page cannot shadow, like the ones in builtins.hpp; declared here because
/// the compiler and the VM's own builtins share it and this is the header both
/// include.
inline constexpr std::string_view import_source_name = "__ctbrowser_import_source";
/// GetTemplateObject (13.2.8.4) for a tagged template: `(key, cooked, raw)`
/// answers the frozen strings array, with `raw` frozen and hung off it, cached
/// per site under `key`, so the same site hands the same object to its tag on
/// every evaluation. See compile_tagged.
inline constexpr std::string_view template_object_name = "__ctbrowser_template_object";
/// PutValue on an unresolvable reference in strict code (6.2.5.6 step 3.a): the
/// compiler calls this with the name before a set_global that is an
/// assignment (never a declaration's own write), and it throws the
/// ReferenceError when the name is neither a global nor on the global object.
/// A call rather than a check inside op::set_global, whose contract says it
/// cannot throw.
inline constexpr std::string_view strict_assign_check_name = "__ctbrowser_strict_assign";
/// PrivateFieldAdd / PrivateMethodOrAccessorAdd (7.3.28, 7.3.29):
/// `(obj, key, v)` defines the private element (a field's value, or the class
/// brand holding its declaration home; see context::find_private_element). It
/// throws the TypeError when the object already carries the key (a
/// constructor that returns the same object twice) or is not extensible. A
/// Proxy's private elements are independent of its target's extensibility and
/// never invoke its traps.
inline constexpr std::string_view private_add_name = "__ctbrowser_private_add";
/// The private key under which a field initializer keeps its class's home
/// object, independently of its mutable `__home`.
inline constexpr std::string_view private_home_key = CTBROWSER_PRIVATE_KEY("PrivateHome");
/// ClassDefinitionEvaluation steps 6-9 (15.7.14) for `class C extends P`:
/// `(C, P, C.prototype)` checks P is null or a constructor and P.prototype an
/// object or null (each a TypeError otherwise), then chains C.prototype to
/// P.prototype and C itself to P (or to Function.prototype for null).
inline constexpr std::string_view class_heritage_name = "%class_heritage";
/// `super.x` / `super[k]` read (13.3.7.3, 6.2.5.5 GetValue of a Super
/// Reference): `(base, key, this)` is base.[[Get]](key, this), so a getter on
/// the parent runs with the method's own receiver, not with the parent.
inline constexpr std::string_view super_get_name = "%super_get";
/// A direct `eval(src)` written in a function's parameter expressions:
/// `(src, name...)` is `eval` (the intrinsic; anything else the name is bound
/// to is simply called) with the one rule of 19.2.1.3
/// EvalDeclarationInstantiation step 3.d this engine can keep without a
/// caller-scoped eval: a `var` the eval'd code declares may not be one of the
/// names bound in that parameter scope (the parameters, and `arguments` unless
/// the function is an arrow or names a parameter so). Breaking it is the
/// SyntaxError the eval throws.
inline constexpr std::string_view param_eval_name = "__ctbrowser_param_eval";
/// A direct `eval(src)` in a function: `(eval, src, callee, this, descriptor,
/// cells...)`; see compiler_impl::compile_direct_eval. The source is compiled
/// with the caller's bindings as its entry function's upvalues.
inline constexpr std::string_view direct_eval_name = "__ctbrowser_direct_eval";
/// The hidden local a sloppy function whose own body calls `eval(` keeps its
/// evals' `var`s on (compiler_impl::with_scope::eval_vars) is named this
/// prefix plus how many such objects enclose it. It is handed to a direct eval
/// as a binding of that name, and to param_eval_name as a name and a cell.
inline constexpr std::string_view eval_vars_prefix = "@evalvars:";
/// `delete o.k` / `delete o[k]` (13.5.1.2): `(obj, key, strict, super)` is
/// ToObject(obj).[[Delete]](ToPropertyKey(key)) with its answer (the opcodes
/// delete_prop/delete_index produce none), a TypeError for a null or undefined
/// base, a TypeError in strict code when the delete answers false, and a
/// ReferenceError for `delete super.x` (`super` true; the key is still
/// evaluated first).
inline constexpr std::string_view delete_ref_name = "__ctbrowser_delete";
/// InitializeInstanceElements (7.3.34) for a derived class, `(this, C)` or
/// `(this, C.prototype)` (the home object, whose own `constructor` is C): runs
/// C's `__fields` on the object `super()` just bound as `this`. That is where
/// 10.2.1.3 / 13.3.7.1's super call runs them, and why a base constructor's
/// `Object.preventExtensions(this)` or a returned object is what the derived
/// fields meet. The compiler emits it after every `super(...)`; a base class's
/// fields still run at construct entry (context::run_field_initialisers).
inline constexpr std::string_view init_fields_name = "%init_fields";
/// `super(...)` returned `(result)`: BindThisValue (10.2.1.3) with what the
/// parent constructor answered. The object it returned (a "return override")
/// replaces the instance as `this` for the rest of the derived constructor,
/// and a derived constructor's implicit return hands back `this`, so `new`
/// evaluates to it. The compiler emits it right after every super call,
/// before the fields.
inline constexpr std::string_view bind_this_name = "%bind_this";
/// `using` / `await using` (explicit resource management, 9.13), as five
/// hidden natives the compiler calls around a protected region; see
/// compile/statements/using.cpp for the lowering and
/// builtins/objects/destructuring.cpp for the bodies. `stack()` makes the
/// DisposeCapability; `add(stack, v, async)` is AddDisposableResource and
/// answers v; `dispose(stack, kind, value)` is DisposeResources for a sync
/// stack, handed the completion in flight (kind 1 = a throw of `value`) and
/// throwing what comes out; `step(stack, kind, value)` disposes one resource
/// of an async stack and answers what to await, or the stack itself when it
/// is empty (throwing the folded completion then); `failed(stack, e)` folds
/// an awaited rejection in.
inline constexpr std::string_view using_stack_name = "__ctbrowser_using_stack";
inline constexpr std::string_view using_add_name = "__ctbrowser_using_add";
inline constexpr std::string_view using_dispose_name = "__ctbrowser_using_dispose";
inline constexpr std::string_view using_step_name = "__ctbrowser_using_step";
inline constexpr std::string_view using_failed_name = "__ctbrowser_using_failed";

} // namespace ctbrowser::script
