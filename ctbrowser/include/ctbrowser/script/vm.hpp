#pragma once

#include <optional>

#include "vm/objects.hpp"

namespace ctbrowser::script {

// Detailed contracts: docs/reference/header-contracts/script-vm-*.md
class context {
public:
    context();

    ~context();

    context(const context &) = delete;
    context & operator=(const context &) = delete;

    static constexpr std::size_t allocation_ceiling = 40'000'000;

    template <typename T, typename... Args> [[nodiscard]] T * allocate(Args &&... args);

    [[nodiscard]] value string(std::string s);

    [[nodiscard]] value make_object();

    [[nodiscard]] value make_array();

    [[nodiscard]] value iter_result(value v, bool done);

    void define_global(std::string name, value v);

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

    run_result run(const program & prog);

    void instantiate_module(const program & prog, module_record & into);
    // THE NAMESPACE OBJECT for a module, live and cached. See the definition.
    [[nodiscard]] value module_namespace(module_record & of);

    void set_module_loader(
        std::function<value(context &, const std::string &, const std::string &)> loader);
    run_result run_module(const program & prog, module_record & into);

    [[nodiscard]] flat_map<std::string, module_record> & modules() noexcept;

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

    static constexpr std::uint32_t reentry_ceiling = 512;

    // RAII, one level. `overflowed()` says the ceiling was reached, in which
    // case the RangeError HAS ALREADY BEEN THROWN and the caller must answer
    // with something harmless instead of recursing again.
    class reentry_scope {
    public:
        explicit reentry_scope(context & cx) : cx_(&cx) {
            over_ = ++cx_->reentry_depth_ > reentry_ceiling;
            // ONCE PER EPISODE. Throwing again on the way out would pop a
            // second handler off `handlers_` for one overflow, which loses the
            // `try` a page actually wrote.
            if (over_ && !cx_->reentry_reported_) {
                cx_->reentry_reported_ = true;
                cx_->throw_error("RangeError", "Maximum call stack size exceeded");
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
    value call_fenced(value callable, std::span<const value> args, value this_value, bool & threw,
                      value & thrown);

    void queue_microtask(value fn, std::vector<value> args = {});

    void drain_microtasks();

    [[nodiscard]] std::string current_stack(std::size_t skip = 0) const;

    [[nodiscard]] bool failed() const noexcept;

    [[nodiscard]] const std::string & error() const noexcept;

    void clear_store_rejected() noexcept;

    [[nodiscard]] bool store_rejected() const noexcept;

    void strict_store_check(std::string_view name);

    [[nodiscard]] bool throw_pending() const noexcept;

    [[nodiscard]] std::size_t unwinds() const noexcept;

    [[nodiscard]] value last_thrown() const noexcept;

    [[nodiscard]] std::string take_error();

    // WHATEVER A PAGE CAN ITERATE, AS AN ARRAY OF VALUES - the one answer
    // for-of, spread and Array.from share. See the definition for what it
    // covers.
    [[nodiscard]] value iterable_values(value v);
    [[nodiscard]] value spread_values(value v);
    // GetIterator(v, sync) (7.4.3): `v[Symbol.iterator]()`, checked to be an
    // object. A TypeError (thrown, catchable) and undefined when it is not
    // iterable or the method answers a non-object.
    [[nodiscard]] value get_iterator(value v);
    // IteratorStep + IteratorValue (7.4.8): `next()` on the iterator; `done`
    // says whether the result was the end. Throws (catchable) when the
    // result is not an object.
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
    // IsLooselyEqual, 7.2.15. NOT static: an object compared against a
    // primitive has to go through ToPrimitive, which re-enters the VM.
    [[nodiscard]] bool loose_equals(value a, value b);
    [[nodiscard]] std::partial_ordering compare_relational(value a, value b);

    [[nodiscard]] bool bigint_binary(op kind, value a, value b, value & out);

    [[nodiscard]] value binary_op_static(op kind, value lhs, value rhs);

    [[nodiscard]] value binary_op(op kind, value lhs, value rhs);

    [[nodiscard]] value negate_value(value v);

    [[nodiscard]] value bit_not_value(value v);

    [[nodiscard]] value numeric_operand(value v);

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

    [[nodiscard]] static proto_kind function_proto_kind(value v) noexcept;

    [[nodiscard]] std::array<object_object *, 3> implicit_prototypes(value v) const;

    void set_prototype(proto_kind kind, object_object * table);

    [[nodiscard]] object_object * prototype(proto_kind kind) const;

    static constexpr double fixed_epoch_base = 1767225600000.0;

    void set_clock(std::function<double()> clock);

    [[nodiscard]] double clock_ms() const;

    void set_pending_promise_factory(std::function<value(context &)> make);

    void set_promise_settler(std::function<void(context &, value, value, bool)> settle);

    void set_promise_factory(std::function<value(context &, value, bool)> make);

    void set_rejection_tracker(std::function<void(value promise, bool handled)> track);

    void track_promise_rejection(value promise, bool handled);

    [[nodiscard]] static bool promise_is_handled(value promise);

    void mark_promise_handled(value promise);

    [[nodiscard]] value make_pending_promise();

    void settle_promise(value promise, value with, bool rejected);

    [[nodiscard]] value make_promise(value v, bool rejected);

    [[nodiscard]] value wrap_in_promise(value v);

    [[nodiscard]] value lookup_property(value target, const std::string & name);
    [[nodiscard]] value key_value(const std::string & key);
    // Assign through the chain, honouring a setter. Returns false when nothing
    // took the write, so the caller can fall back to defining an own property.
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

    // What the implicit Object.prototype answers for `name` on `receiver` - a
    // data member or an accessor called with that receiver. The fallback every
    // arm of lookup_property ends in; see the definition for why.
    [[nodiscard]] value from_object_prototype(value receiver, const std::string & name);
    [[nodiscard]] value lookup_along(object_object * from, value receiver,
                                     const std::string & name);

    void delete_named(value target, const std::string & name);

    [[nodiscard]] value own_keys(value source);

    [[nodiscard]] value get_prototype(value target);
    void set_prototype(value target, value proto);

    [[nodiscard]] bool has_property(value target, value key);
    // The same walk for a name already a string - no key object made.
    [[nodiscard]] bool has_property(value target, const std::string & name);
    [[nodiscard]] bool instance_of(value target, value ctor);
    void delete_index(value target, value key);

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
        // `a` is the entry's attrs, or for an array element the element's -
        // freeze/seal on an array flips the element bits and never rewrites
        // the accessor table.
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

    // [[GetOwnProperty]]. False when the property is not an OWN one - the
    // prototype chain is not consulted, which is the point.
    [[nodiscard]] bool own_property(value target, const std::string & name,
                                    property_descriptor & out);
    // Does `target` have an own property `name` at all? The question
    // hasOwnProperty, Object.hasOwn and verifyProperty all ask.
    [[nodiscard]] bool has_own_property(value target, const std::string & name);
    [[nodiscard]] bool private_element_present(value target, const std::string & key);
    // [[Get]] with an explicit receiver (10.1.8.1 OrdinaryGet): `base`'s own
    // property or the first one up its chain, a getter called on `receiver`.
    [[nodiscard]] value get_with_receiver(value base, const std::string & name, value receiver);

    // [[DefineOwnProperty]], with 10.1.6.3's validation. False means REJECTED -
    // the caller decides whether that is a TypeError (Object.defineProperty) or
    // silence (Reflect.defineProperty answers false).
    [[nodiscard]] bool define_own_property(value target, const std::string & name,
                                           const property_descriptor & wanted);

    // [[Delete]]. False when the property exists and is not configurable, which
    // is what makes Object.freeze and Object.seal observable. Sloppy-mode
    // `delete` discards the answer; a strict-mode one would throw on false.
    bool delete_own_property(value target, const std::string & name);

    // [[PreventExtensions]] / [[IsExtensible]], across all four table kinds.
    void prevent_extensions(value target);
    [[nodiscard]] bool is_extensible(value target);

    void array_append(value target, value v);

    // --- gc ----------------------------------------------------------------
    std::size_t collect();

    using root_visitor = std::function<void(value)>;

    void set_external_roots(std::function<void(const root_visitor &)> enumerate);

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

    class native_scope {
    public:
        explicit native_scope(context & cx) : cx_(&cx), saved_frames_(cx.native_frames_) {
            if (cx.native_depth_++ == 0) { cx.native_epoch_ = cx.heap_; }
            // How deep the interpreter was when THIS native began: `call`
            // parks a throw only while no interpreted frame has been pushed
            // since (see `call`).
            cx.native_frames_ = cx.frames_.size();
        }
        ~native_scope() {
            cx_->native_frames_ = saved_frames_;
            if (--cx_->native_depth_ == 0) { cx_->native_epoch_ = nullptr; }
        }
        native_scope(const native_scope &) = delete;
        native_scope & operator=(const native_scope &) = delete;

    private:
        context * cx_;
        std::size_t saved_frames_;
    };

    void set_gc_stress(bool on) noexcept;

    [[nodiscard]] bool gc_stress() const noexcept;

    void record_step(instruction in);

    void safepoint();

    [[nodiscard]] std::size_t collections() const noexcept;

    std::size_t collect_if_due();

    [[nodiscard]] std::size_t live_objects() const noexcept;

    // WHERE A THROW LANDS. One entry per open `try`, so unwinding can pop back
    // to the frame that installed it - a handler in a caller must not be caught
    // by a callee.
    struct handler {
        std::size_t frame = 0;   // index into frames_
        std::size_t address = 0; // the catch block
        std::size_t reg_top = 0; // registers_ size on entry
        std::uint16_t slot = 0;  // where to put the thrown value
        // A C++ caller's catch (call_fenced): `frame` is the depth to unwind
        // to, and the throw lands in fence_thrown_ rather than a register.
        bool fence = false;
    };

    enum class resume_mode {
        next,
        thrown,
        returned
    };

    struct coroutine_object final : heap_object {
        const function_proto * proto = nullptr;
        std::size_t ip = 0;
        // Where the awaited value lands when the frame comes back - the
        // destination register of the `await` that suspended it.
        std::uint16_t await_reg = 0;
        std::uint16_t argc = 0;
        closure_object * closure = nullptr;
        value receiver = value::undefined();
        bool constructing = false;
        std::vector<value> window;
        // This frame's own handlers, with `reg_top` made RELATIVE to the frame's
        // base: the frame comes back at a different place in the register stack,
        // and an absolute mark would point into whatever is there now.
        std::vector<handler> handlers;
        // The promise the caller was given, settled when the body finally
        // returns. One per suspended function however many times it awaits.
        value promise;
        bool generator = false;
        bool started = false;
        bool done = false;
        // Set while the body is running, so a `.next()` from inside itself is
        // refused rather than corrupting the register stack.
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

    // The generator whose frame is running - a native called from a
    // generator body sees that frame on top, since natives push none.
    [[nodiscard]] coroutine_object * current_generator() const noexcept;
    // See return_marker_key: a `.return(v)` in flight through the body's
    // finally blocks, and how to tell one from a page's own throw.
    [[nodiscard]] value make_return_marker(value v);
    [[nodiscard]] bool is_return_marker(value v) const;
    [[nodiscard]] value return_marker_value(value marker) const;
    // Put a suspended frame back and run it. `with` is what the await
    // evaluates to; `rejected` throws it at the await instead.
    void resume(value coroutine, value with, bool rejected);
    void await_for(coroutine_object * saved, value v);
    // What an async generator's `.throw(e)` / `.return(v)` becomes at a
    // `yield*`: the value handed to the delegate loop, which forwards it to
    // the inner iterator's own method (14.4.14 step 7.b / 7.c).
    [[nodiscard]] value make_resume_record(std::string_view how, value v);
    void suspend_frame(coroutine_object * saved, std::uint16_t await_reg);
    // The mirror: the window back on the register stack with slack above it,
    // a frame rebuilt from the coroutine and pushed, the handlers absolute
    // again. Returns the frame's base.
    std::size_t restore_frame(coroutine_object * saved);

    [[nodiscard]] value generator_resume(value generator, value sent, resume_mode how);
    // The async generator's `.next(v)` / `.throw(e)` / `.return(v)`: a promise
    // of the record, queued behind whatever the body is doing.
    [[nodiscard]] value async_generator_request(value generator, value sent, resume_mode how);
    // Run queued requests while the body is neither running nor awaiting.
    void async_generator_drain(coroutine_object * saved);
    void settle_async_generator(coroutine_object * saved, value outcome, bool raw_return);
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

    // The object a generator function call hands back.
    [[nodiscard]] value make_generator(closure_object * closure, value receiver,
                                       std::span<const value> args);

    [[nodiscard]] static bool is_pending_promise(value v);
    // %ThrowTypeError% (10.2.4.1): ONE per realm, anonymous, arity 0, frozen -
    // the getter and setter of Function.prototype's `caller` and `arguments`
    // and of an unmapped arguments object's `callee`. Made on first use.
    [[nodiscard]] value throw_type_error();

    [[nodiscard]] value await_job();

    void attach_resume(value promise, value coroutine);

private:
    value invoke(value callable, std::span<const value> args, value this_value, bool constructing);

    friend struct aot_bridge;
    friend class executing_as;
    // THE SIGNATURE MUST MATCH EXACTLY or this friends a different overload and
    // every private access inside dispatch.cpp fails at once - which is how it
    // reports a widened parameter list.
    friend bool enter_compiled_body(context & ctx, const function_proto & target, value closure,
                                    const value * argv, std::size_t argv_base, std::uint32_t argc,
                                    value receiver, bool constructing, value & out);
    friend void note_transition_into_vm(const context & ctx) noexcept;
    friend void note_transition_into_cxx(const context & ctx) noexcept;

    struct call_frame {
        const function_proto * proto = nullptr;
        std::size_t ip = 0;
        std::size_t base = 0; // index into registers_ of this frame's r0
        std::uint16_t result_reg = 0;
        // How many arguments ACTUALLY arrived, which is not param_count: a rest
        // parameter binds the ones past the declared list, and nothing else in
        // the frame records that they were passed.
        std::uint16_t argc = 0;
        closure_object * closure = nullptr; // whose upvalues this body sees
        // The receiver a JS body reads through `this`.
        value receiver = value::undefined();
        // How many exception handlers this frame had on entry. Unwinding pops
        // back to it, so a handler in a caller cannot be caught by a callee.
        std::size_t handler_base = 0;

        std::uint16_t landed_slot = 0;

        value new_target = value::undefined();

        coroutine_object * generator = nullptr;

        // `new C()` evaluates to the new object, NOT to whatever the
        // constructor body happens to return - unless it returns an object,
        // which is the one case the spec lets override it.
        bool constructing = false;

        value arguments_object = value::undefined();

        value async_promise = value::undefined();

        std::uint64_t serial = 0;
    };

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
    // ToPrimitive for the string case: an object's own toString, then valueOf.
    [[nodiscard]] std::string to_primitive_string(value v);
    // ToPrimitive with the default hint, for `+`.
    [[nodiscard]] value to_primitive(value v);
    // A function's `prototype`, made on first use. See the definition.
    [[nodiscard]] value ensure_prototype(value fn);
    // The fresh object `new` builds, with its prototype taken from the
    // constructor's own `prototype` property.
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

    void mark(value v);
    // Marks `o` and everything reachable from it. Iterative - see
    // `mark_worklist_`; the depth of the object graph is not a stack cost.
    void mark_object(heap_object * o);
    void sweep_all();

    template <class Visit>
    void each_root(std::size_t register_limit, std::size_t frame_limit, Visit && visit);
    // collect(), in its three parts - vm/objects/gc.cpp. `mark_roots` marks through
    // each_root; `sweep` frees the unmarked and clears the marked; `unmark_all`
    // clears every mark and frees NOTHING, which is the escape oracle's exit.
    void mark_roots(std::size_t register_limit);
    [[nodiscard]] std::size_t sweep();
    void unmark_all();

    std::vector<heap_object *> mark_worklist_;

    void push_mark(heap_object * o);
    // Blacken one object: grey everything it points at. THE ONLY PLACE THAT
    // KNOWS THE PER-KIND EDGES.
    void trace_object(heap_object * o);

    void note_allocation(heap_object * p);
    void note_freed(heap_object * o);
    void record_frame_pop(const call_frame & popped, value carried, bool compiled_return = false);
    void record_frames_unwound(std::size_t first);
    void adjudicate(std::size_t register_limit, std::size_t frame_limit,
                    std::vector<type_recorder::escape_record> & records);

    executing_kind executing_ = executing_kind::cxx;

    // Set while a native runs, so it can see its receiver.
    value current_this_ = value::undefined();
    // The program being executed, so a call from C++ can find the string
    // tables a nested frame needs.
    const program * program_ = nullptr;
    std::vector<std::unique_ptr<program>> owned_programs_;
    flat_map<const void *, flat_map<std::uint32_t, value>> string_cache_;
    // The same for BigInt literals, keyed and swept the same way.
    flat_map<const void *, flat_map<std::uint32_t, value>> bigint_cache_;
    // Live try blocks, innermost last. Not per-frame, because a throw has to be
    // able to find a handler several frames up.
    std::vector<handler> handlers_;
    std::array<object_object *, static_cast<std::size_t>(proto_kind::count_)> prototypes_{};
    // Seven entries, scanned linearly: see register_error_prototype.
    std::vector<std::pair<std::string, object_object *>> error_prototypes_;
    std::function<value(context &, value, bool)> promise_factory_;
    std::function<double()> clock_;
    std::function<value(context &)> pending_promise_factory_;
    std::function<void(context &, value, value, bool)> promise_settler_;
    std::function<void(value, bool)> rejection_tracker_; // see set_rejection_tracker
    // Set by `op::pass_new_target` and consumed by the very next frame push, so
    // a super() call hands its own new.target to the base constructor.
    value pending_new_target_ = value::undefined();
    value pending_closure_ = value::undefined();

    std::size_t pending_argv_base_ = 0;
    std::uint32_t pending_argc_ = 0;
    flat_map<std::string, module_record> modules_;
    // The module being evaluated, so `bind_export` knows whose cells to adopt and
    // `load_import` knows who is asking. Null while a classic script runs.
    module_record * current_module_ = nullptr;
    // See the definition: `run` cannot be re-entered, and a dynamic import
    // needs a program evaluated from inside the interpreter.
    run_result run_reentrant(const program & prog);
    std::function<value(context &, const std::string &, const std::string &)> module_loader_;
    std::function<bool(context &, module_record &)> module_evaluator_;
    // Set by a frame that suspended, so `resume` can tell "awaited again" from
    // "returned" - both leave run_loop the same way.
    bool suspended_ = false;
    // Set by `op::yield_value` so generator_resume can tell a body that YIELDED
    // from one that RETURNED - run_loop hands back a value either way, and the
    // difference is the whole of `done`.
    bool yielded_ = false;
    struct microtask {
        value fn;
        std::vector<value> args;
    };
    std::deque<microtask> microtasks_;
    value thrown_ = value::undefined();
    // How deep the C++ stack currently is inside a conversion or a native, and
    // whether this episode has already reported its overflow. See
    // reentry_scope, which is the only thing that touches either.
    std::uint32_t reentry_depth_ = 0;
    bool reentry_reported_ = false;

    // string_flat_map, NOT flat_map<std::string, value>: the plain one's hasher
    // and equality are not transparent, so `find(string_view)` cannot exist
    // (docs/performance.md: -47% on object_object::find).
    string_flat_map<value> globals_;
    // See set_undeclared_name_hook. Empty in a bare VM, which is what
    // `unittests/js` and `ct262` run.
    std::function<value(std::string_view)> undeclared_name_;
    value global_this_ = value::undefined();
    std::vector<value> registers_;
    std::vector<call_frame> frames_;
    // WHERE THE TYPE ORACLE'S OBSERVATIONS GO, or null - and null selects the
    // run_loop instantiation with no hook in it (run_loop.cpp).
    type_recorder * recorder_ = active_type_recorder();
    heap_object * heap_ = nullptr;
    std::size_t live_objects_ = 0;
    // See native_scope: the head of the heap when the outermost native in
    // progress was entered, and how many natives are in progress.
    heap_object * native_epoch_ = nullptr;
    std::size_t native_depth_ = 0;
    std::size_t native_frames_ = 0; // frames_.size() when the innermost native began
    // Values a C++ scope is holding across something that can collect. See
    // `rooted`; marked in collect() like any other root.
    std::vector<value> temporaries_;
    // The values iterable_values is materialising through their own
    // @@iterator right now - see the re-entrancy note there. Held by the
    // caller's register too, so not a root of its own.
    std::vector<value> materialising_;
    // What the innermost call_fenced caught, consumed by it on return.
    bool fence_hit_ = false;
    value fence_thrown_;
    bool store_rejected_ = false;
    value await_job_ = value::undefined();        // see await_job(); a root in each_root
    value throw_type_error_ = value::undefined(); // see throw_type_error(); a root too
    // What `call` parked for rethrow_pending - see `call`.
    bool has_pending_throw_ = false;
    value pending_throw_;
    std::size_t unwinds_ = 0;
    std::size_t collections_ = 0;
    bool gc_stress_ = false;
    // TOTAL allocations, never reset: the cap is about a loop that does not
    // terminate, and a collector that keeps the live set small hides exactly
    // that if the count is reset.
    std::size_t allocations_ = 0;
    static constexpr std::size_t minimum_collect_threshold = 4096;
    std::size_t collect_threshold_ = minimum_collect_threshold;
    std::function<void(const root_visitor &)> external_roots_;
    bool failed_ = false;
    std::string error_;
};

// IsConstructor, 7.2.4: a native with [[Construct]], a non-arrow, non-generator
// closure, or a proxy whose target is one. Defined in vm/call/construct.cpp.
[[nodiscard]] bool is_constructor(value v);

} // namespace ctbrowser::script

#include "vm/execution_inline.hpp"
#include "vm/frames_inline.hpp"
#include "vm/gc_inline.hpp"
#include "vm/properties_inline.hpp"
