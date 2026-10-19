#pragma once
//===- vm.hpp - The script VM's execution context ---------------*- C++ -*-===//
//
// Declares ctbrowser::script::context, the bytecode interpreter's state and
// API: allocation and GC, globals and realms, modules, calls and throws,
// promises and generators, and the private hooks the AOT bridge uses. Inline
// member definitions are in vm/*_inline.hpp, included at the end.
//
//===----------------------------------------------------------------------===//

#include <optional>
#include <unordered_map>

#include <ctbrowser/aot/aot.hpp>

#include "vm/objects.hpp"

namespace ctbrowser::script {

/// The script VM: one JavaScript agent's heap, realms, call frames, register
/// file, exception handlers, module registry and microtask queue. Detailed
/// contracts are in docs/reference/header-contracts/script-vm-*.md.
class context {
public:
    context();

    ~context();

    context(const context &) = delete;
    context & operator=(const context &) = delete;

    /// Total allocations (never reset by collection) after which the context
    /// fails the run, so a non-terminating allocating loop ends instead of
    /// exhausting memory.
    static constexpr std::size_t allocation_ceiling = 40'000'000;

    template <typename T, typename... Args> [[nodiscard]] T * allocate(Args &&... args);

    [[nodiscard]] value string(std::string s);

    [[nodiscard]] value make_object();

    [[nodiscard]] value make_array();

    [[nodiscard]] value iter_result(value v, bool done);

    void define_global(std::string name, value v);
    /// An assignment to a global name: op::set_global, and the AOT helper
    /// ct_aot_global_set, which is this with `strict` false. One store, so the
    /// interpreter and compiled code cannot disagree about where a global's
    /// value went (see the definition). An uninitialized lexical throws in
    /// either mode; a strict store to a read-only global throws too.
    void assign_global(realm_record & realm, const std::string & name, value v, bool strict);
    /// InitializeBinding after a global lexical declaration's initializer completes.
    void initialize_global(realm_record & realm, const std::string & name, value v);
    /// Defines a platform name (an interface object, a namespace, an alias of
    /// one): { writable, !enumerable, configurable } (realm_record::builtin).
    void define_builtin_global(std::string name, value v);
    /// Makes every name the current realm has a platform name: the last step
    /// of install_builtins, whose intrinsics are all ECMA-262 clause 19's. The
    /// names are remembered as the intrinsics, so a builtin global marked after
    /// them is the host's, a platform interface (is_intrinsic_name).
    void mark_globals_builtin();
    [[nodiscard]] bool is_intrinsic_name(std::string_view name) const {
        return intrinsic_names_.contains(name);
    }
    /// Registers a serializable interface of the host's (HTML 2.7.2
    /// [Serializable]): structuredClone copies an instance of one onto a fresh
    /// object of the same interface prototype, and what it copies is `state`,
    /// the own properties the binding keeps an instance's state in (internal
    /// ones included), which are its serialization steps. Nothing else: a
    /// page's expando on a Blob is not the Blob's, and is neither copied nor
    /// able to make the clone fail. An instance of any other platform interface
    /// (a builtin global's prototype that is not an intrinsic's) is a
    /// DataCloneError.
    void set_serializable_interface(std::string name, std::vector<std::string> state) {
        serializable_interfaces_[std::move(name)] = std::move(state);
    }
    /// Its state keys, or null when `name` is not a serializable interface.
    [[nodiscard]] const std::vector<std::string> * serializable_interface(
        std::string_view name) const {
        const auto it = serializable_interfaces_.find(name);
        return it == serializable_interfaces_.end() ? nullptr : &it->second;
    }

    /// CreateGlobalVarBinding (9.1.1.4.17): a script's or an eval's `var`. A
    /// new binding is undefined, and a name the global object already owns, in
    /// the binding table or as a property defined on the object itself, keeps
    /// its value. A script's `var` (`configurable` false) is also enumerable
    /// and cannot be deleted; an eval's (true) can.
    void declare_global_var(std::string name, bool configurable = true);
    /// CreateGlobalFunctionBinding (9.1.1.4.18) for a script's top-level
    /// function: { writable, enumerable, !configurable }, whatever a
    /// configurable binding of that name said before; a name the global
    /// object's target owns keeps its own storage. An eval's (`configurable`,
    /// D true) is a table binding with all three, and a configurable property
    /// the target owned gives way to it.
    void declare_global_function(std::string name, bool configurable = false);
    /// EvalDeclarationInstantiation (19.2.1.3) for an eval whose variable
    /// environment is the global one (program::eval_global): a `var` or
    /// function colliding with a global lexical is the SyntaxError of step
    /// 3.a, and one CanDeclareGlobalFunction / CanDeclareGlobalVar refuses is
    /// the TypeError of steps 8-10, both before anything exists; then each
    /// binding is made (steps 15-17). False with the error thrown.
    bool eval_declaration_instantiation(const program & prog);
    /// GlobalDeclarationInstantiation's refusals (16.1.7 steps 3-6): a classic
    /// script whose lexical name is already a var, a lexical or a
    /// non-configurable global property, or whose var or function name is
    /// already lexical, is a SyntaxError and creates no binding. False with
    /// the SyntaxError thrown; true once the script's names are recorded.
    bool global_declaration_instantiation(const program & prog);
    /// True when `name` is one of the program's Annex B block-function names
    /// (program::annex_b_names) that the global environment cannot take as a
    /// var (a global lexical of it, or CanDeclareGlobalVar false) and so is
    /// not created (B.3.2.2, B.3.2.3). Whoever declares hoisted_vars asks.
    [[nodiscard]] bool annex_b_skipped(const program & prog, const std::string & name);

    /// The three value properties of the global object (19.1): NaN, Infinity
    /// and undefined are { writable: false, enumerable: false, configurable:
    /// false } on every global object. A store to one is refused (silently in
    /// sloppy code, a TypeError in strict), and none can be deleted or
    /// redefined. Tested by name, so the store path pays three length compares
    /// rather than a table lookup.
    [[nodiscard]] static constexpr bool immutable_global(std::string_view name) noexcept {
        return name == "undefined" || name == "NaN" || name == "Infinity";
    }
    /// Defines NaN, Infinity and undefined as non-writable, non-configurable
    /// own properties of a global proxy's target. A proxy may report a property
    /// as non-configurable only if its target has it so (10.5.5 step 22);
    /// without these, getOwnPropertyDescriptor(globalThis, 'NaN') would throw.
    static void define_immutable_globals(object_object & global);

    bool erase_global(std::string_view name);

    void define_native(std::string name, native_fn fn);

    [[nodiscard]] value global(std::string_view name) const;

    [[nodiscard]] bool has_global(std::string_view name) const;

    [[nodiscard]] const string_flat_map<value> & globals() const noexcept;

    [[nodiscard]] bool in_native() const noexcept;

    void set_undeclared_name_hook(std::function<value(std::string_view)> hook);

    [[nodiscard]] value global_or_named(std::string_view name, bool silent = false);

    [[nodiscard]] value global_this() const noexcept;

    void set_global_this(value receiver);

    // --- realms ---------------------------------------------------------------
    //
    // The current realm is the realm of the innermost running JavaScript frame
    // (a frame takes its closure's realm when it is pushed) or, below the
    // first frame a realm_scope was opened over, that scope's. Every accessor
    // above (define_global, global, global_or_named, global_this, the
    // undeclared-name hook) reads and writes the current realm's, so a page
    // with one realm behaves as if realms did not exist.

    /// The realm of the innermost running JavaScript frame; see above.
    [[nodiscard]] realm_record & current_realm() const noexcept;
    /// HTML's entry realm, as far as this VM keeps one: the realm of the
    /// outermost frame of the running script, the one a task started with,
    /// which a call into another realm's function does not change.
    [[nodiscard]] realm_record & entry_realm() const noexcept;
    [[nodiscard]] realm_record & main_realm() const noexcept;
    /// A new realm whose binding table is a copy of the template (see
    /// set_realm_template), whose intrinsics are the page's own (shared), and
    /// whose global object is the caller's to set.
    [[nodiscard]] realm_record & create_realm();
    /// A new realm with nothing in it but the two natives compiled code calls
    /// whatever the embedding installs (define_own_name, to_property_key_name):
    /// no intrinsic, no global object. This is 9.3.1 CreateRealm, which
    /// builtins' create_intrinsic_realm then fills the way install_builtins
    /// filled the first.
    [[nodiscard]] realm_record & create_empty_realm();
    /// The realm whose global object is `global`, or null. What a ShadowRealm
    /// keeps in its [[ShadowRealm]] slot is that object.
    [[nodiscard]] realm_record * realm_of(value global) const noexcept;
    /// What a new realm's binding table starts as: the page's globals as they
    /// stood before any page script ran (the builtins and the platform's
    /// interface objects, none of the page's own `var`s). The current realm's
    /// platform names (realm_record::builtin) go with it.
    void set_realm_template(const string_flat_map<value> & globals);
    /// A bare VM's global object for `r`: a proxy over r's binding table,
    /// which is what `globalThis` is in ct262 and in unittests/js, and what
    /// $262.createRealm hands back for a second one.
    [[nodiscard]] value make_global_object(realm_record & r);

    /// RAII: `r` is the current realm until the scope closes, for the code the
    /// embedder runs at this depth (defining a frame's globals, compiling its
    /// inline handlers, running its <script>s). A JS frame pushed inside the
    /// scope still runs in its own closure's realm.
    class realm_scope {
    public:
        realm_scope(context & cx, realm_record & r)
            : cx_(&cx), saved_realm_(cx.scope_realm_), saved_depth_(cx.scope_depth_) {
            cx.scope_realm_ = &r;
            cx.scope_depth_ = cx.frames_.size();
        }
        ~realm_scope() {
            cx_->scope_realm_ = saved_realm_;
            cx_->scope_depth_ = saved_depth_;
        }
        realm_scope(const realm_scope &) = delete;
        realm_scope & operator=(const realm_scope &) = delete;

    private:
        context * cx_;
        realm_record * saved_realm_;
        std::size_t saved_depth_;
    };

    run_result run(const program & prog);
    /// Runs a nested classic script with its own exception boundary, including
    /// declaration instantiation, without resetting the caller's live frames.
    run_result run_script_fenced(const program & prog, value & thrown);

    void instantiate_module(const program & prog, module_record & into);
    /// The namespace object for a module, live and cached. See the definition.
    [[nodiscard]] value module_namespace(module_record & of);

    void set_module_loader(
        std::function<value(context &, const std::string &, const std::string &)> loader);
    run_result run_module(const program & prog, module_record & into);

    [[nodiscard]] std::unordered_map<std::string, module_record> & modules() noexcept;

    [[nodiscard]] value module_import_cell(const std::string & specifier,
                                           const std::string & export_name);

    [[nodiscard]] value module_export_cell(const std::string & name, value current);

    [[nodiscard]] value module_namespace_for(const std::string & specifier);
    [[nodiscard]] value deferred_module_namespace(module_record & of);
    [[nodiscard]] value deferred_module_namespace_for(const std::string & specifier);

    void set_module_evaluator(std::function<bool(context &, module_record &)> evaluator);

    [[nodiscard]] value dynamic_import(value specifier, const std::string & referrer);

    [[nodiscard]] value current_this() const noexcept;

    [[nodiscard]] value run_nested(const program & prog);

    const program & own_program(program compiled);

    void refuse(std::string_view what, std::string why);

    [[nodiscard]] value make_error(std::string_view kind, std::string message);

    void register_error_prototype(std::string kind, object_object * table);

    [[nodiscard]] object_object * error_prototype(std::string_view kind) const;

    void throw_error(std::string_view kind, std::string message);

    void rebind_receiver(value v);

    [[nodiscard]] value take_pending_throw();

    bool rethrow_pending();

    void throw_value(value thrown);

    /// How deep C++ may re-enter the VM (conversions, natives calling script)
    /// before reentry_scope throws a RangeError.
    static constexpr std::uint32_t reentry_ceiling = 512;

    /// RAII, one level of C++ re-entry. `overflowed()` says the ceiling was
    /// reached, in which case the RangeError has already been thrown and the
    /// caller must answer with something harmless instead of recursing again.
    class reentry_scope {
    public:
        explicit reentry_scope(context & cx) : cx_(&cx) {
            over_ = ++cx_->reentry_depth_ > reentry_ceiling;
            // Throw once per episode. Throwing again on the way out would pop
            // a second handler off `handlers_` for one overflow, which loses
            // the `try` a page actually wrote.
            if (over_ && !cx_->reentry_reported_) {
                cx_->reentry_reported_ = true;
                cx_->throw_error("RangeError", "Maximum call stack size exceeded");
            } else if (over_) {
                // Refused without a throw: the caller gets undefined, which a
                // fenced caller must not mistake for a result (call_fenced).
                cx_->reentry_refused_ = true;
            }
        }
        ~reentry_scope() {
            if (--cx_->reentry_depth_ == 0) { cx_->reentry_reported_ = false; }
        }
        reentry_scope(const reentry_scope &) = delete;
        reentry_scope & operator=(const reentry_scope &) = delete;
        reentry_scope(reentry_scope &&) = delete;
        reentry_scope & operator=(reentry_scope &&) = delete;
        [[nodiscard]] bool overflowed() const noexcept { return over_; }

    private:
        context * cx_;
        bool over_ = false;
    };

    value call(value callable, std::span<const value> args, value this_value = value::undefined());
    /// `constructing` permits a host to initialize an existing receiver, as
    /// custom-element upgrades require, without exposing that mode to Call.
    value call_fenced(value callable, std::span<const value> args, value this_value, bool & threw,
                      value & thrown, bool constructing = false);

    void queue_microtask(value fn, std::vector<value> args = {});

    void drain_microtasks();

    [[nodiscard]] std::string current_stack(std::size_t skip = 0) const;
    /// Where the interpreter is: the frame depth and the innermost frame's
    /// instruction. A throw that lands moves one of them (it pops frames, or
    /// moves the catching frame's ip to its handler), and a callee that threw
    /// and caught inside itself moves neither. A native snapshots this before
    /// running page code, to ask afterwards whether a throw landed
    /// (detail::threw_since in lib/Script/builtins/internal.hpp), without
    /// building the string current_stack would.
    struct stack_mark {
        std::size_t depth;
        std::size_t ip;
        bool operator==(const stack_mark &) const = default;
    };
    [[nodiscard]] stack_mark mark_stack() const noexcept {
        return {frames_.size(), frames_.empty() ? 0 : static_cast<std::size_t>(frames_.back().ip)};
    }

    [[nodiscard]] bool failed() const noexcept;

    [[nodiscard]] const std::string & error() const noexcept;

    void clear_store_rejected() noexcept;

    [[nodiscard]] bool store_rejected() const noexcept;

    void strict_store_check(std::string_view name);

    [[nodiscard]] bool throw_pending() const noexcept;

    [[nodiscard]] std::size_t unwinds() const noexcept;

    /// Where the last throw happened: the program whose `source` the byte
    /// `offset` indexes, taken from the innermost interpreted frame's
    /// instruction (function_proto::code_offsets) as the throw began to
    /// unwind. "Report an exception" needs it for an ErrorEvent's lineno and
    /// colno (HTML 8.1.4.7); line_table turns it into both. `source` is null
    /// when nothing was recorded: a compiled frame, a build without the offset
    /// table, a throw with no frame at all. A parked throw rethrown at a
    /// native's call site keeps the site it was thrown at.
    struct throw_site {
        const program * source = nullptr;
        std::uint32_t offset = 0;
    };
    [[nodiscard]] throw_site last_throw_site() const noexcept { return throw_site_; }

    [[nodiscard]] value last_thrown() const noexcept;

    [[nodiscard]] std::string take_error();

    /// Whatever a page can iterate, as an array of values: the one answer
    /// for-of, spread and Array.from share. See the definition for what it
    /// covers. An object with nothing to iterate is an empty list, or with
    /// `spread` (spread_values) the TypeError.
    [[nodiscard]] value iterable_values(value v, bool spread = false);
    [[nodiscard]] value spread_values(value v);
    /// GetIterator(v, sync) (7.4.3): `v[Symbol.iterator]()`, checked to be an
    /// object. A TypeError (thrown, catchable) and undefined when it is not
    /// iterable or the method answers a non-object.
    [[nodiscard]] value get_iterator(value v);
    /// IteratorStep + IteratorValue (7.4.8): `next()` on the iterator; `done`
    /// says whether the result was the end. Throws (catchable) when the result
    /// is not an object.
    [[nodiscard]] value iterator_step(value iterator, value next, bool & done);

    [[nodiscard]] value construct(value callee, std::span<const value> args,
                                  value new_target = value::undefined());

    bool to_primitive_hint(value v, const char * hint, value & out);
    [[nodiscard]] static bool truthy(value v);

    [[nodiscard]] value proxy_trap(value proxy, const std::string & name, bool * failed = nullptr);

    [[nodiscard]] static std::int32_t to_int32(value v);

    [[nodiscard]] static std::uint32_t to_uint32(value v);
    [[nodiscard]] static double to_number(value v);
    [[nodiscard]] static double exponentiate(double base, double exponent);
    [[nodiscard]] std::string to_string(value v);
    [[nodiscard]] static std::string_view type_of(value v);
    [[nodiscard]] double to_number_value(value v);
    /// IsLooselyEqual, 7.2.15. Not static: an object compared against a
    /// primitive has to go through ToPrimitive, which re-enters the VM.
    [[nodiscard]] bool loose_equals(value a, value b);
    [[nodiscard]] std::partial_ordering compare_relational(value a, value b);

    [[nodiscard]] bool bigint_binary(op kind, value a, value b, value & out);

    [[nodiscard]] value binary_op_static(op kind, value lhs, value rhs);

    [[nodiscard]] value binary_op(op kind, value lhs, value rhs);

    [[nodiscard]] value negate_value(value v);

    [[nodiscard]] value bit_not_value(value v);

    [[nodiscard]] value numeric_operand(value v);
    /// ToNumeric of both operands of a numeric binary operator, left first
    /// (13.15.3 steps 3-4): the left's conversion (its valueOf, a Symbol's
    /// TypeError) finishes before the right's starts, and a throw from it is
    /// the operator's. False with that throw landed or in flight.
    [[nodiscard]] bool numeric_operands(value lhs, value rhs, value & l, value & r);

    /// The value kinds with an implicit prototype, indexing each realm's
    /// realm_intrinsics::prototypes.
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
        promise,
        generator,
        async_generator,
        generator_function,
        async_generator_function,
        async_function,
        count_
    };

    static_assert(static_cast<std::size_t>(proto_kind::count_) == realm_prototype_count);

    [[nodiscard]] static proto_kind function_proto_kind(value v) noexcept;

    [[nodiscard]] std::array<object_object *, 3> implicit_prototypes(value v) const;

    void set_prototype(proto_kind kind, object_object * table);

    [[nodiscard]] object_object * prototype(proto_kind kind) const;

    /// What Date.now() returns when no clock is installed: 1767225600000 ms is
    /// 2026-01-01T00:00:00Z, so renders and goldens do not depend on the wall
    /// clock.
    static constexpr double fixed_epoch_base = 1767225600000.0;

    void set_clock(std::function<double()> clock);

    [[nodiscard]] double clock_ms() const;

    void set_pending_promise_factory(std::function<value(context &)> make);

    void set_promise_settler(std::function<void(context &, value, value, bool)> settle);

    void set_promise_factory(std::function<value(context &, value, bool)> make);

    void set_rejection_tracker(std::function<void(value promise, bool handled)> track);

    /// Called at the end of every microtask checkpoint that drained cleanly,
    /// where HTML's "perform a microtask checkpoint" step 5 notifies about
    /// rejected promises. A host hook; it must not run script.
    void set_checkpoint_hook(std::function<void()> hook);

    void track_promise_rejection(value promise, bool handled);

    [[nodiscard]] static bool promise_is_handled(value promise);

    void mark_promise_handled(value promise);

    [[nodiscard]] value make_pending_promise();

    void settle_promise(value promise, value with, bool rejected);

    [[nodiscard]] value make_promise(value v, bool rejected);

    [[nodiscard]] value wrap_in_promise(value v);

    [[nodiscard]] value lookup_property(value target, const std::string & name);
    [[nodiscard]] value key_value(const std::string & key);
    /// 10.2.1 [[Call]] step 2: a class constructor called without `new` is a
    /// TypeError, thrown here, and the answer is true. A `super()` call is the
    /// one that reaches it with a pending new.target, and is let through.
    /// Asked by the interpreter's inline call path and invoke, including C++
    /// callbacks. Embedders constructing a class use construct or
    /// call_fenced's explicit construction mode for an existing receiver.
    bool refuse_class_call(value callee, bool super_call = false);
    /// Assigns through the chain, honouring a setter. Returns false when
    /// nothing took the write, so the caller can fall back to defining an own
    /// property.
    bool assign_through_accessor(value target, const std::string & name, value v);
    void store_property(value target, const std::string & name, value v);
    [[nodiscard]] value interned_string(const void * site, std::uint32_t slot,
                                        std::string_view text);

    [[nodiscard]] value interned_bigint_literal(const void * site, std::uint32_t slot,
                                                std::string_view text);

    [[nodiscard]] value lookup_index(value target, value key);

    void store_index(value target, value key, value v);

    void pass_new_target(value from);

    void copy_own_properties(value target, value source);

    void define_accessor(value target, const std::string & name, value getter, value setter);

    /// What the implicit Object.prototype answers for `name` on `receiver`: a
    /// data member or an accessor called with that receiver. The fallback every
    /// arm of lookup_property ends in; see the definition for why.
    [[nodiscard]] value from_object_prototype(value receiver, const std::string & name);
    [[nodiscard]] value lookup_along(object_object * from, value receiver,
                                     const std::string & name);

    void delete_named(value target, const std::string & name);

    [[nodiscard]] value own_keys(value source);

    [[nodiscard]] value get_prototype(value target);
    /// GetSuperBase (9.1.1.3.5): the home object's [[GetPrototypeOf]], what
    /// op::get_proto answers. get_prototype's null (an object whose link was
    /// never set) is the implicit Object.prototype here, so `super.m()` in a
    /// class with no `extends` finds Object.prototype's; an explicit null
    /// (undefined in the field) stays nothing.
    [[nodiscard]] value super_base(value home);
    void set_prototype(value target, value proto);

    [[nodiscard]] bool has_property(value target, value key);
    /// The same walk for a name that is already a string; no key object made.
    [[nodiscard]] bool has_property(value target, const std::string & name);
    [[nodiscard]] bool instance_of(value target, value ctor);
    void delete_index(value target, value key);

    /// A property descriptor (6.2.6) with a has_ flag for every field, as
    /// ToPropertyDescriptor produces and [[DefineOwnProperty]] consumes.
    struct property_descriptor {
        bool has_value = false;
        bool has_get = false;
        bool has_set = false;
        bool has_writable = false;
        bool has_enumerable = false;
        bool has_configurable = false;
        value held = value::undefined();
        value getter = value::undefined();
        value setter = value::undefined();
        bool writable = false;
        bool enumerable = false;
        bool configurable = false;
        bool virtual_slot = false;

        [[nodiscard]] bool is_accessor() const noexcept { return has_get || has_set; }
        [[nodiscard]] bool is_data() const noexcept { return has_value || has_writable; }
        static property_descriptor data(value v, std::uint8_t a) {
            property_descriptor d;
            d.has_value = d.has_writable = d.has_enumerable = d.has_configurable = true;
            d.held = v;
            d.writable = (a & attr_writable) != 0;
            d.enumerable = (a & attr_enumerable) != 0;
            d.configurable = (a & attr_configurable) != 0;
            return d;
        }
        /// `a` is the entry's attrs, or for an array element the element's:
        /// freeze/seal on an array flips the element bits and never rewrites
        /// the accessor table.
        static property_descriptor accessor(value get, value set, std::uint8_t a) {
            property_descriptor d;
            d.has_get = d.has_set = d.has_enumerable = d.has_configurable = true;
            d.getter = get;
            d.setter = set;
            d.enumerable = (a & attr_enumerable) != 0;
            d.configurable = (a & attr_configurable) != 0;
            return d;
        }
    };

    [[nodiscard]] value from_property_descriptor(const property_descriptor & from);
    [[nodiscard]] property_descriptor to_property_descriptor(value from);

    /// [[GetOwnProperty]]. False when the property is not an own one; the
    /// prototype chain is not consulted, which is the point.
    [[nodiscard]] bool own_property(value target, const std::string & name,
                                    property_descriptor & out);
    /// Whether `target` has an own property `name` at all: the question
    /// hasOwnProperty, Object.hasOwn and verifyProperty all ask.
    [[nodiscard]] bool has_own_property(value target, const std::string & name);
    [[nodiscard]] std::optional<property_descriptor> find_private_element(value target,
                                                                          const std::string & key);
    /// [[Get]] with an explicit receiver (10.1.8.1 OrdinaryGet): `base`'s own
    /// property or the first one up its chain, a getter called on `receiver`.
    [[nodiscard]] value get_with_receiver(value base, const std::string & name, value receiver);

    /// [[DefineOwnProperty]], with 10.1.6.3's validation. False means rejected:
    /// the caller decides whether that is a TypeError (Object.defineProperty)
    /// or silence (Reflect.defineProperty answers false).
    [[nodiscard]] bool define_own_property(value target, const std::string & name,
                                           const property_descriptor & wanted);
    /// CreateDataPropertyOrThrow (7.3.7): a writable, configurable data
    /// property, defined whatever the chain holds; a rejection is the TypeError.
    void define_data_property(value target, value key, value v, bool enumerable);

    /// [[Delete]]. False when the property exists and is not configurable,
    /// which is what makes Object.freeze and Object.seal observable.
    /// Sloppy-mode `delete` discards the answer; a strict-mode one would throw
    /// on false.
    bool delete_own_property(value target, const std::string & name);

    /// [[PreventExtensions]] / [[IsExtensible]], across all four table kinds.
    void prevent_extensions(value target);
    [[nodiscard]] bool is_extensible(value target);

    void array_append(value target, value v);

    // --- gc ----------------------------------------------------------------

    /// Runs a full mark-and-sweep collection; returns how many objects it freed.
    std::size_t collect();

    using root_visitor = std::function<void(value)>;

    void set_external_roots(std::function<void(const root_visitor &)> enumerate);
    /// The host's weak lists, pruned by every collection after it has marked
    /// and before it sweeps: `prune` is handed whether a value survived and
    /// drops the ones that did not, so a list the host must keep current while
    /// script can see its members (a document's live Ranges) never keeps one
    /// alive and never reads a freed one. One callback per realm, like the
    /// external roots.
    using survives_fn = std::function<bool(value)>;
    void set_weak_prune(std::function<void(const survives_fn &)> prune);

    /// RAII: keeps one value alive across code that can collect, by pushing
    /// it onto temporaries_ until the scope closes.
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

    /// RAII: keeps every value in a span alive until the scope closes.
    class rooted_values {
    public:
        rooted_values(context & cx, std::span<const value> vs) : cx_(&cx), count_(vs.size()) {
            cx.temporaries_.insert(cx.temporaries_.end(), vs.begin(), vs.end());
        }
        ~rooted_values() {
            // Defensive in the same way `rooted`'s pop is: the stack discipline
            // says these are exactly the top `count_`, and unwinding out of an
            // engine fault is not the moment to trust that.
            const std::size_t n = std::min(count_, cx_->temporaries_.size());
            cx_->temporaries_.erase(cx_->temporaries_.end() - static_cast<std::ptrdiff_t>(n),
                                    cx_->temporaries_.end());
        }
        rooted_values(const rooted_values &) = delete;
        rooted_values & operator=(const rooted_values &) = delete;

    private:
        context * cx_;
        std::size_t count_;
    };

    /// RAII for a native function in progress: counts it in native_depth_ and,
    /// when called from script, opens a pin range so what it allocates
    /// survives a collection while it holds the objects in C++ locals.
    class native_scope {
    public:
        explicit native_scope(context & cx)
            : cx_(&cx), saved_frames_(cx.native_frames_),
              from_script_(cx.native_depth_ == 0 || cx.frames_.size() != cx.native_frames_),
              saved_pins_(cx.pins_.size()), saved_results_(cx.pinned_results_.size()) {
            ++cx.native_depth_;
            // A native called by a native allocates into its caller's open
            // range; one called from script opens its own.
            if (from_script_) { cx.pins_.push_back({nullptr, cx.heap_}); }
            // How deep the interpreter was when this native began: `call`
            // parks a throw only while no interpreted frame has been pushed
            // since (see `call`).
            cx.native_frames_ = cx.frames_.size();
        }
        ~native_scope() {
            cx_->native_frames_ = saved_frames_;
            --cx_->native_depth_;
            if (from_script_) {
                cx_->pins_.resize(saved_pins_);
                cx_->pinned_results_.resize(saved_results_);
            }
        }
        native_scope(const native_scope &) = delete;
        native_scope & operator=(const native_scope &) = delete;

    private:
        context * cx_;
        std::size_t saved_frames_;
        bool from_script_;
        std::size_t saved_pins_;
        std::size_t saved_results_;
    };

    /// A native calling into script (context::invoke's closure path). What the
    /// native allocated so far stays pinned, what the callee allocates does
    /// not, and what the callee returns is pinned instead, because the native
    /// holds it in a C++ local from here on. See native_scope's contract.
    class callout_scope {
    public:
        explicit callout_scope(context & cx)
            : cx_(&cx),
              from_native_(cx.native_depth_ > 0 && cx.frames_.size() == cx.native_frames_) {
            if (!from_native_ || cx.pins_.empty() || cx.pins_.back().top != nullptr) { return; }
            // Close the open range at the head; an empty one is dropped, so
            // a native that allocates nothing between calls (forEach over
            // integer indices) does not grow the list.
            if (cx.heap_ == cx.pins_.back().bottom) {
                cx.pins_.pop_back();
            } else {
                cx.pins_.back().top = cx.heap_;
            }
        }
        ~callout_scope() {
            if (from_native_) { cx_->pins_.push_back({nullptr, cx_->heap_}); }
        }
        [[nodiscard]] value returned(value v) {
            if (from_native_ && v.is_heap()) { cx_->pinned_results_.push_back(v); }
            return v;
        }
        callout_scope(const callout_scope &) = delete;
        callout_scope & operator=(const callout_scope &) = delete;

    private:
        context * cx_;
        bool from_native_;
    };

    void set_gc_stress(bool on) noexcept;

    [[nodiscard]] bool gc_stress() const noexcept;

    void record_step(instruction in);

    void safepoint();

    [[nodiscard]] std::size_t collections() const noexcept;

    std::size_t collect_if_due();

    [[nodiscard]] std::size_t live_objects() const noexcept;

    /// Where a throw lands. One entry per open `try`, so unwinding can pop back
    /// to the frame that installed it: a handler in a caller must not be caught
    /// by a callee.
    struct handler {
        std::size_t frame = 0;   // Index into frames_.
        std::size_t address = 0; // The catch block.
        std::size_t reg_top = 0; // registers_ size on entry.
        std::uint16_t slot = 0;  // Where to put the thrown value.
        /// A C++ caller's catch (call_fenced): `frame` is the depth to unwind
        /// to, and the throw lands in fence_thrown_ rather than a register.
        bool fence = false;
    };

    /// How a suspended generator is resumed: `.next()`, `.throw()` or
    /// `.return()`.
    enum class resume_mode {
        next,
        thrown,
        returned
    };

    /// A suspended generator or async function: its saved frame, register
    /// window and handlers, plus the generator protocol's state.
    struct coroutine_object final : heap_object {
        const function_proto * proto = nullptr;
        std::size_t ip = 0;
        /// Where the awaited value lands when the frame comes back: the
        /// destination register of the `await` that suspended it.
        std::uint16_t await_reg = 0;
        std::uint16_t argc = 0;
        closure_object * closure = nullptr;
        /// The suspended frame's realm, put back with it.
        realm_record * realm = nullptr;
        value receiver = value::undefined();
        bool constructing = false;
        std::vector<value> window;
        /// This frame's own handlers, with `reg_top` made relative to the
        /// frame's base: the frame comes back at a different place in the
        /// register stack, and an absolute mark would point into whatever is
        /// there now.
        std::vector<handler> handlers;
        /// The promise the caller was given, settled when the body finally
        /// returns. One per suspended function however many times it awaits.
        value promise;
        bool generator = false;
        bool started = false;
        bool done = false;
        /// Set while the body is running, so a `.next()` from inside itself is
        /// refused rather than corrupting the register stack.
        bool running = false;
        bool async_gen = false;
        bool awaiting = false;
        std::uint8_t return_pending = 0;
        value self;
        value delegate;
        struct async_request {
            resume_mode how;
            value sent;
            value promise;
        };
        std::vector<async_request> queue;
        coroutine_object() : heap_object(heap_kind::coroutine) {}
    };

    /// The generator whose frame is running. A native called from a generator
    /// body sees that frame on top, since natives push none.
    [[nodiscard]] coroutine_object * current_generator() const noexcept;
    /// See return_marker_key: a `.return(v)` in flight through the body's
    /// finally blocks, and how to tell one from a page's own throw.
    [[nodiscard]] value make_return_marker(value v);
    [[nodiscard]] bool is_return_marker(value v) const;
    [[nodiscard]] value return_marker_value(value marker) const;
    /// Puts a suspended frame back and runs it. `with` is what the await
    /// evaluates to; `rejected` throws it at the await instead.
    void resume(value coroutine, value with, bool rejected);
    void await_for(coroutine_object * saved, value v);
    /// What an async generator's `.throw(e)` / `.return(v)` becomes at a
    /// `yield*`: the value handed to the delegate loop, which forwards it to
    /// the inner iterator's own method (14.4.14 step 7.b / 7.c).
    [[nodiscard]] value make_resume_record(std::string_view how, value v);
    void suspend_frame(coroutine_object * saved, std::uint16_t await_reg);
    /// The mirror of suspend_frame: the window back on the register stack with
    /// slack above it, a frame rebuilt from the coroutine and pushed, the
    /// handlers absolute again. Returns the frame's base.
    std::size_t restore_frame(coroutine_object * saved);

    [[nodiscard]] value generator_resume(value generator, value sent, resume_mode how);
    /// The async generator's `.next(v)` / `.throw(e)` / `.return(v)`: a promise
    /// of the record, queued behind whatever the body is doing.
    [[nodiscard]] value async_generator_request(value generator, value sent, resume_mode how);
    /// Runs queued requests while the body is neither running nor awaiting.
    void async_generator_drain(coroutine_object * saved);
    void settle_async_generator(coroutine_object * saved, value outcome, bool raw_return);
    /// Where a new closure's upvalues come from: a compiled body's descriptor
    /// array (by_descriptor), or the interpreter's registers (by_register).
    struct upvalue_source {
        const std::uint64_t * by_descriptor = nullptr;
        std::uint32_t descriptor_count = 0;
        const value * by_register = nullptr;

        [[nodiscard]] value at(std::size_t which, const upvalue_desc & up) const {
            return by_descriptor != nullptr ? value::from_bits(by_descriptor[which])
                                            : by_register[up.index];
        }
    };

    [[nodiscard]] value make_closure(closure_object * enclosing, std::uint32_t function_index,
                                     upvalue_source parent, value enclosing_this);

    /// The object a generator function call hands back.
    [[nodiscard]] value make_generator(closure_object * closure, value receiver,
                                       std::span<const value> args);

    [[nodiscard]] static bool is_pending_promise(value v);
    /// %ThrowTypeError% (10.2.4.1): one per realm, anonymous, arity 0, frozen;
    /// the getter and setter of Function.prototype's `caller` and `arguments`
    /// and of an unmapped arguments object's `callee`. Made on first use.
    [[nodiscard]] value throw_type_error();

    [[nodiscard]] value await_job();

    void attach_resume(value promise, value coroutine);

private:
    value invoke(value callable, std::span<const value> args, value this_value, bool constructing);
    /// context::string without the lead-byte guard, for a string the VM minted
    /// from its own text (a compiled constant, a property key handed back by
    /// key_value), which may be a private name's key and must stay one.
    [[nodiscard]] value minted_string(std::string s);

    // The AOT ABI's helpers (one per row of aot_helpers.def), each defined in
    // its extern "C" body under lib/Script/aot_bridge/, and aot_bridge, the
    // reads they share.
#define CT_AOT_HELPER(name, ret, params, may_throw, may_reenter, is_safepoint)                     \
    friend decltype(aot::name) aot::name;
#include <ctbrowser/aot/aot_helpers.def>
    friend struct aot_bridge;
    friend class executing_as;
    // The signature must match exactly, or this befriends a different
    // overload and every private access inside dispatch.cpp fails at once;
    // that is how it reports a widened parameter list.
    friend bool enter_compiled_body(context & ctx, const function_proto & target, value closure,
                                    const value * argv, std::size_t argv_base, std::uint32_t argc,
                                    value receiver, bool constructing, value & out);
    friend void note_transition_into_vm(const context & ctx) noexcept;
    friend void note_transition_into_cxx(const context & ctx) noexcept;

    /// One activation of an interpreted or compiled function on frames_.
    struct call_frame {
        const function_proto * proto = nullptr;
        std::size_t ip = 0;
        std::size_t base = 0; // Index into registers_ of this frame's r0.
        std::uint16_t result_reg = 0;
        /// How many arguments actually arrived, which is not param_count: a
        /// rest parameter binds the ones past the declared list, and nothing
        /// else in the frame records that they were passed.
        std::uint16_t argc = 0;
        closure_object * closure = nullptr; // Whose upvalues this body sees.
        /// The receiver a JS body reads through `this`.
        value receiver = value::undefined();
        /// How many exception handlers this frame had on entry. Unwinding pops
        /// back to it, so a handler in a caller cannot be caught by a callee.
        std::size_t handler_base = 0;

        /// The register a catch's thrown value was written to; set by
        /// unwind_to_handler and read by ct_aot_catch_land.
        std::uint16_t landed_slot = 0;

        value new_target = value::undefined();

        coroutine_object * generator = nullptr;

        /// `new C()` evaluates to the new object, not to whatever the
        /// constructor body happens to return, unless it returns an object,
        /// which is the one case the spec lets override it.
        bool constructing = false;

        value arguments_object = value::undefined();

        value async_promise = value::undefined();

        std::uint64_t serial = 0;

        /// The realm this body runs in: its closure's, or for a top level the
        /// realm current when it was entered. Never null once pushed.
        realm_record * realm = nullptr;
        /// A derived constructor's mutable this binding, allocated only when
        /// an arrow captures it. Constructors cannot suspend into a coroutine.
        value receiver_cell = value::undefined();
    };

    /// The realm a frame for `callee` runs in; see call_frame::realm.
    [[nodiscard]] realm_record * realm_for(const closure_object * callee) const noexcept;

    [[nodiscard]] static value effective_this(const call_frame & f);

    [[nodiscard]] value make_arguments_object(call_frame & fr, const value * slots,
                                              std::uint32_t argc);

    [[nodiscard]] value gather_rest_values(const call_frame & fr, const value * slots,
                                           std::uint32_t argc, std::uint32_t from);

    void run_field_initialisers(value constructor, value self);

    [[nodiscard]] static std::string callee_origin(const function_proto & fn, std::size_t ip,
                                                   std::uint16_t reg_index);
    [[nodiscard]] std::string describe_callee(const function_proto & fn, std::string_view name,
                                              value callee);

    [[nodiscard]] std::string describe_thrown(value thrown);
    /// ToPrimitive for the string case: an object's own toString, then valueOf.
    [[nodiscard]] std::string to_primitive_string(value v);
    /// ToPrimitive with the default hint, for `+`.
    [[nodiscard]] value to_primitive(value v);
    /// A function's `prototype`, made on first use. See the definition.
    [[nodiscard]] value ensure_prototype(value fn);
    /// The fresh object `new` builds, with its prototype taken from the
    /// constructor's own `prototype` property.
    [[nodiscard]] value make_instance(value callee);

    void new_callee_type_error(const function_proto & fn, std::string_view origin, value callee);

    [[nodiscard]] std::vector<value> spread_arguments(value arg_array);
    [[nodiscard]] value call_spread(value callee, value arg_array, value receiver);
    [[nodiscard]] value construct_spread(value callee, value arg_array);

    [[nodiscard]] value construct_new(value callee, std::span<const value> args,
                                      const function_proto & from);

    [[nodiscard]] value execute(const program & prog, const function_proto & entry);
    [[nodiscard]] value run_loop(std::size_t stop_depth);
    template <bool Record> [[nodiscard]] value run_loop_impl(std::size_t stop_depth);
    void execute_call(instruction in, call_frame * vm_frame, const function_proto * vm_proto,
                      std::size_t base);
    void execute_construct(instruction in, call_frame * vm_frame, const function_proto * vm_proto,
                           std::size_t base);
    std::optional<value> execute_await(instruction in, call_frame *& vm_frame, std::size_t & base,
                                       std::size_t stop_depth);

    void raise(std::string message);

    [[nodiscard]] bool unwind_to_handler();
    /// Records throw_site_ for the throw unwind_to_handler is starting.
    void note_throw_site() noexcept;

    void mark(value v);
    /// Marks `o` and everything reachable from it. Iterative (see
    /// `mark_worklist_`), so the depth of the object graph costs no stack.
    void mark_object(heap_object * o);
    void sweep_all();

    template <class Visit>
    void each_root(std::size_t register_limit, std::size_t frame_limit, Visit && visit);
    // collect(), in its three parts (vm/objects/gc.cpp). `mark_roots` marks
    // through each_root; `sweep` frees the unmarked and clears the marked;
    // `unmark_all` clears every mark and frees nothing, which the type
    // recorder's escape check (type_record.cpp) uses to measure reachability
    // without collecting.
    void mark_roots(std::size_t register_limit);
    [[nodiscard]] std::size_t sweep();
    void unmark_all();

    std::vector<heap_object *> mark_worklist_;

    void push_mark(heap_object * o);
    /// Blackens one object: greys everything it points at. The only place that
    /// knows the per-kind edges.
    void trace_object(heap_object * o);

    void note_allocation(heap_object * p);
    void note_freed(heap_object * o);
    void record_frame_pop(const call_frame & popped, value carried, bool compiled_return = false);
    void record_frames_unwound(std::size_t first);
    void adjudicate(std::size_t register_limit, std::size_t frame_limit,
                    std::vector<type_recorder::escape_record> & records);

    executing_kind executing_ = executing_kind::cxx;

    /// Set while a native runs, so it can see its receiver.
    value current_this_ = value::undefined();
    /// The program being executed, so a call from C++ can find the string
    /// tables a nested frame needs.
    const program * program_ = nullptr;
    throw_site throw_site_;              // See last_throw_site.
    std::uint64_t throw_site_value_ = 0; // The bits of the value thrown there (note_throw_site).
    std::vector<std::unique_ptr<program>> owned_programs_;
    flat_map<const void *, flat_map<std::uint32_t, value>> string_cache_;
    /// The same for BigInt literals, keyed and swept the same way.
    flat_map<const void *, flat_map<std::uint32_t, value>> bigint_cache_;
    /// Live try blocks, innermost last. Not per-frame, because a throw has to
    /// be able to find a handler several frames up.
    std::vector<handler> handlers_;
    std::function<value(context &, value, bool)> promise_factory_;
    std::function<double()> clock_;
    std::function<value(context &)> pending_promise_factory_;
    std::function<void(context &, value, value, bool)> promise_settler_;
    std::function<void(value, bool)> rejection_tracker_; // See set_rejection_tracker.
    std::function<void()> checkpoint_hook_;              // See set_checkpoint_hook.
    /// Set by `op::pass_new_target` and consumed by the very next frame push,
    /// so a super() call hands its own new.target to the base constructor.
    value pending_new_target_ = value::undefined();
    /// The next call is a `super()`: set by op::pass_new_target, taken by the
    /// call that follows it. pending_new_target_ alone is not enough: from an
    /// arrow in a constructor that is the arrow frame's undefined, and the
    /// parent class's constructor would read as called without `new`.
    bool super_call_ = false;
    value pending_closure_ = value::undefined();

    std::size_t pending_argv_base_ = 0;
    std::uint32_t pending_argc_ = 0;
    /// Node-based, for reference stability: run_module holds `&into` as
    /// current_module_ while the module runs, and a dynamic import() inside it
    /// inserts here, which in a flat_map would move the record out from under
    /// it.
    std::unordered_map<std::string, module_record> modules_;
    /// The module being evaluated, so `bind_export` knows whose cells to adopt
    /// and `load_import` knows who is asking. Null while a classic script runs.
    module_record * current_module_ = nullptr;
    /// Runs a program from inside the interpreter, as a dynamic import needs;
    /// `run` itself cannot be re-entered. See the definition.
    run_result run_reentrant(const program & prog);
    std::function<value(context &, const std::string &, const std::string &)> module_loader_;
    std::function<bool(context &, module_record &)> module_evaluator_;
    /// Set by a frame that suspended, so `resume` can tell "awaited again" from
    /// "returned"; both leave run_loop the same way.
    bool suspended_ = false;
    /// Set by `op::yield_value` so generator_resume can tell a body that
    /// yielded from one that returned: run_loop hands back a value either way,
    /// and the difference is the whole of `done`.
    bool yielded_ = false;
    struct microtask {
        value fn;
        std::vector<value> args;
    };
    std::deque<microtask> microtasks_;
    value thrown_ = value::undefined();
    /// How deep the C++ stack currently is inside a conversion or a native, and
    /// whether this episode has already reported its overflow. See
    /// reentry_scope, which is the only thing that touches either.
    std::uint32_t reentry_depth_ = 0;
    bool reentry_reported_ = false;
    /// A silent refusal since the innermost call_fenced began; see reentry_scope.
    bool reentry_refused_ = false;

    /// Every realm this context has made, the page's first. unique_ptr because
    /// closures and frames point at them and the vector grows. A
    /// realm_record's table is a string_flat_map, not
    /// flat_map<std::string, value>: the plain one's hasher and equality are
    /// not transparent, so `find(string_view)` cannot exist
    /// (docs/performance.md: -47% on object_object::find).
    std::vector<std::unique_ptr<realm_record>> realms_;
    /// The innermost realm_scope: its realm, and frames_.size() when it opened.
    realm_record * scope_realm_ = nullptr;
    std::size_t scope_depth_ = 0;
    /// See set_realm_template.
    string_flat_map<value> realm_template_;
    string_flat_map<value> realm_template_internals_;
    string_flat_map<bool> realm_template_builtin_;
    string_flat_map<value> realm_template_platform_prototypes_;
    string_flat_map<bool> intrinsic_names_; // See mark_globals_builtin.
    /// See set_serializable_interface.
    string_flat_map<std::vector<std::string>> serializable_interfaces_;
    std::vector<value> registers_;
    std::vector<call_frame> frames_;
    /// Where the type oracle's observations go, or null; null selects the
    /// run_loop instantiation with no hook in it (run_loop.cpp).
    type_recorder * recorder_ = active_type_recorder();
    heap_object * heap_ = nullptr;
    std::size_t live_objects_ = 0;
    /// A range of the newest-first heap list allocated by a native in progress
    /// (see native_scope). `top` is the range's newest object, null while the
    /// range is open and runs to heap_; `bottom` is the head when it opened,
    /// not included.
    struct pin_range {
        heap_object * top;
        heap_object * bottom;
    };
    /// What the natives in progress allocated, oldest range first.
    std::vector<pin_range> pins_;
    /// What their script callees returned to them (callout_scope).
    std::vector<value> pinned_results_;
    /// How many natives are in progress.
    std::size_t native_depth_ = 0;
    std::size_t native_frames_ = 0; // frames_.size() when the innermost native began.
    /// Values a C++ scope is holding across something that can collect. See
    /// `rooted`; marked in collect() like any other root.
    std::vector<value> temporaries_;
    /// The values iterable_values is materialising through their own
    /// @@iterator right now; see the re-entrancy note there. Held by the
    /// caller's register too, so not a root of its own.
    std::vector<value> materialising_;
    /// What the innermost call_fenced caught, consumed by it on return.
    bool fence_hit_ = false;
    value fence_thrown_;
    bool store_rejected_ = false;
    value await_job_ = value::undefined(); // see await_job(); a root in each_root
    /// What `call` parked for rethrow_pending; see `call`.
    bool has_pending_throw_ = false;
    value pending_throw_;
    std::size_t unwinds_ = 0;
    std::size_t collections_ = 0;
    bool gc_stress_ = false;
    /// Total allocations, never reset: the cap is about a loop that does not
    /// terminate, and a collector that keeps the live set small would hide
    /// exactly that if the count were reset.
    std::size_t allocations_ = 0;
    static constexpr std::size_t minimum_collect_threshold = 4096;
    std::size_t collect_threshold_ = minimum_collect_threshold;
    /// String bytes made since the last collection, against a budget of what
    /// survived the last one (at least 64 MB). The object count alone let a
    /// loop of `s += c` (100,000 objects, five gigabytes) run into the
    /// address-space cap without ever being due. See collect_if_due and the
    /// interpreter's backward jump.
    static constexpr std::size_t minimum_string_budget = std::size_t{64} << 20;
    std::size_t string_bytes_ = 0;
    std::size_t string_budget_ = minimum_string_budget;
    std::function<void(const root_visitor &)> external_roots_;
    std::function<void(const survives_fn &)> weak_prune_;
    bool failed_ = false;
    std::string error_;
};

/// IsConstructor, 7.2.4: a native with [[Construct]], a non-arrow,
/// non-generator closure, or a proxy whose target is one. Defined in
/// vm/call/construct.cpp.
[[nodiscard]] bool is_constructor(value v);

/// OrdinarySet(o, name, v, receiver), 10.1.9.2, for a receiver that is not
/// `o`: a setter on the way runs with the receiver as `this`, a proxy is asked
/// with it, and a data property lands on the receiver. False is a refusal
/// (strict code's TypeError); a throw answers true. Defined in
/// vm/objects/store.cpp; the bindings' own [[Set]] traps (a collection written
/// through an object that inherits from it) end in it too.
bool set_for_receiver(context & cx, value o, const std::string & name, value v, value receiver);

} // namespace ctbrowser::script

#include "vm/execution_inline.hpp"
#include "vm/frames_inline.hpp"
#include "vm/gc_inline.hpp"
#include "vm/properties_inline.hpp"
