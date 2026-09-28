// compiler_impl - classes.
//
// `class` declarations and expressions, their fields and their
// constructors. It lived in the expressions section, at 200 lines inside a
// file of 1,353.
//
// One of the files carved out of a 3,845-line compile.cpp on 2026-08-09.
// The class is declared whole in compiler_impl.hpp beside this.

#include "compiler_impl.hpp"

#include <boost/scope/scope_exit.hpp>

namespace ctbrowser::script::detail {

void compiler_impl::emit_computed_accessor(std::uint16_t target, std::int32_t key,
                                           std::int32_t fn_node, bool setter) {
    const std::uint32_t mark = reg_mark();
    const std::uint16_t callee = alloc_reg();
    proto().emit(instruction::with_bx(op::get_global, callee,
                                      intern_name(std::string{define_accessor_name})));
    const std::uint16_t object = alloc_reg();
    proto().emit(instruction{op::move, object, target});
    const std::uint16_t key_reg = alloc_reg();
    const std::uint16_t getter = alloc_reg();
    const std::uint16_t setter_reg = alloc_reg();
    // Reserve the contiguous call arguments before the key can allocate
    // temporaries (for example, the value sent by a bare yield).
    compile_expr(key, key_reg);
    proto().emit(instruction{op::load_undef, setter ? getter : setter_reg});
    method_pending_ = true; // see frame::has_home
    compile_expr(fn_node, setter ? setter_reg : getter);
    method_pending_ = false;
    // Its home object, as a named accessor gets one below.
    proto().emit(
        instruction{op::set_prop, setter ? setter_reg : getter, name_operand("__home"), target});
    proto().emit(instruction{op::call, callee, 4});
    release_to(mark);
}

void compiler_impl::emit_define_own(std::uint16_t target, std::string_view key, std::uint16_t v,
                                    bool enumerable) {
    const std::uint32_t mark = reg_mark();
    const std::uint16_t callee = alloc_reg();
    proto().emit(
        instruction::with_bx(op::get_global, callee, intern_name(std::string{define_own_name})));
    const std::uint16_t object = alloc_reg();
    proto().emit(instruction{op::move, object, target});
    const std::uint16_t key_reg = alloc_reg();
    proto().emit(instruction::with_bx(op::load_string, key_reg, intern_string(std::string{key})));
    const std::uint16_t held = alloc_reg();
    proto().emit(instruction{op::move, held, v});
    const std::uint16_t flag = alloc_reg();
    proto().emit(instruction{enumerable ? op::load_true : op::load_false, flag});
    proto().emit(instruction{op::call, callee, 4});
    release_to(mark);
}

void compiler_impl::emit_define_own_key(std::uint16_t target, std::uint16_t key, std::uint16_t v,
                                        bool enumerable) {
    const std::uint32_t mark = reg_mark();
    const std::uint16_t callee = alloc_reg();
    proto().emit(
        instruction::with_bx(op::get_global, callee, intern_name(std::string{define_own_name})));
    const std::uint16_t object = alloc_reg();
    proto().emit(instruction{op::move, object, target});
    const std::uint16_t key_reg = alloc_reg();
    proto().emit(instruction{op::move, key_reg, key});
    const std::uint16_t held = alloc_reg();
    proto().emit(instruction{op::move, held, v});
    const std::uint16_t flag = alloc_reg();
    proto().emit(instruction{enumerable ? op::load_true : op::load_false, flag});
    proto().emit(instruction{op::call, callee, 4});
    release_to(mark);
}

void compiler_impl::emit_private_add(std::uint16_t target, std::string_view key, std::uint16_t v) {
    const std::uint32_t mark = reg_mark();
    const std::uint16_t callee = alloc_reg();
    proto().emit(
        instruction::with_bx(op::get_global, callee, intern_name(std::string{private_add_name})));
    const std::uint16_t object = alloc_reg();
    proto().emit(instruction{op::move, object, target});
    const std::uint16_t key_reg = alloc_reg();
    proto().emit(instruction::with_bx(op::load_string, key_reg, intern_string(std::string{key})));
    const std::uint16_t held = alloc_reg();
    proto().emit(instruction{op::move, held, v});
    proto().emit(instruction{op::call, callee, 3});
    release_to(mark);
}

std::uint32_t compiler_impl::compile_field_initialiser(const std::vector<std::int32_t> & fields,
                                                       bool is_static, std::string brand) {
    const std::uint32_t index = new_proto(offset_of(fields.empty() ? -1 : fields.front()));
    out_.functions[index].name = is_static ? "<static>" : "<fields>";
    out_.functions[index].is_strict = true; // 15.7.1: all of a class body is strict

    frames_.emplace_back();
    frames_.back().proto = index;
    frames_.back().is_strict = true;
    frames_.back().has_home = true; // the prototype, or the class for `<static>`
    // A BODY IS NOT THE DECLARATION THAT HOLDS IT: `const f = function () {
    // typo = 1; }` writes `typo` as an assignment, whatever `f` is.
    const not_declaring body_is_not_a_write{*this};
    push_scope();
    // A static block's `break`, `continue`, `return` and `try` stop at this
    // boundary exactly as a function body's do - see compile_function_body.
    std::vector<finally_context> saved_finallies;
    saved_finallies.swap(finallies_);
    std::vector<loop_context> saved_loops;
    saved_loops.swap(loops_);
    const std::size_t saved_handler_depth = handler_depth_;
    handler_depth_ = 0;
    // A FUNCTION BODY IS NOT PART OF THE CHAIN THAT ENCLOSES IT.
    //
    // `a?.b(() => c?.d)` compiles the arrow while the outer chain is open,
    // so without this the arrow's own short-circuit would be recorded on
    // the outer chain's exit list - and patched into the ENCLOSING proto's
    // code array, at an index that means something else entirely there.
    const bool saved_in_chain = in_chain_;
    std::vector<std::size_t> saved_exits;
    saved_exits.swap(optional_exits_);
    in_chain_ = false;
    if (!brand.empty()) {
        // PrivateMethodOrAccessorAdd (7.3.29) for every private method and
        // accessor at once: the brand, before any field initialiser runs,
        // which is where 10.2.1.2 InitializeInstanceElements puts it - and the
        // TypeError when this object was already branded or is sealed.
        const std::uint32_t mark = reg_mark();
        const std::uint16_t self = alloc_reg();
        proto().emit(instruction{op::load_this, self});
        const std::uint16_t home = alloc_reg();
        proto().emit(instruction{op::load_callee, home});
        proto().emit(
            instruction{op::get_prop, home, home, name_operand(std::string{private_home_key})});
        emit_private_add(self, brand, home);
        release_to(mark);
    }
    for (const std::int32_t member : fields) {
        const vp::node & m = at(member);
        // A static block is a method's body, not a field's initialiser
        // (15.7.11: no [[ClassFieldInitializerName]]) - see eval_site.
        fn().field_initializer = m.c != 3;
        if (m.c == 3) {
            // `static { ... }`: a body of its own, with its own lexical scope
            // and its function declarations hoisted to its top (15.7.11
            // ClassStaticBlockDefinitionEvaluation - the block is a function
            // body, not a Block).
            fn().captures = range_of(m.b);
            push_scope();
            predeclare_locals(m.b);
            for (const std::int32_t st : kids(at(m.b))) {
                if (at(st).kind == vp::nk::func_decl) { compile_stmt(st); }
            }
            compile_statement_list(kids(at(m.b)), true);
            pop_scope();
            continue;
        }
        fn().captures = range_of(m.b);
        const std::uint32_t mark = reg_mark();
        const std::uint16_t self = alloc_reg();
        proto().emit(instruction{op::load_this, self});
        const std::uint16_t v = alloc_reg();
        const bool computed = (m.d & 2) != 0 && m.a >= 0;
        // `class A { x; }` declares x and gives it undefined - a field
        // without an initialiser is still a field.
        if (m.b >= 0) {
            compile_named_expr(m.b, v, computed ? "" : m.text);
        } else {
            proto().emit(instruction{op::load_undef, v});
        }
        if (computed) { // a computed key: `[expr] = init`
            // Evaluated when the class was defined (field_key_name), and
            // DEFINED like any public field - naming an anonymous function
            // it holds (15.7.10 step 3.b, NamedEvaluation with the key).
            const std::uint16_t key = alloc_reg();
            emit_plain_read(field_key_name(member), key);
            if (is_anonymous_function_definition(m.b)) { emit_set_function_name(v, key); }
            emit_define_own_key(self, key, v, true);
        } else if (m.text.starts_with('#')) {
            // A PRIVATE FIELD IS ADDED, NOT SET (7.3.28 PrivateFieldAdd): a
            // set_prop would be the brand check store_property makes, on an
            // instance that does not carry the element yet - and adding it
            // twice, or to a sealed object, is the TypeError.
            emit_private_add(self, member_key(m.text), v);
        } else {
            // A public field is DEFINED (7.3.5 CreateDataPropertyOrThrow via
            // DefineField): a setter of the same name on the prototype is not
            // called, a frozen `this` is the TypeError, and a static
            // `name`/`length` shadows the constructor's read-only one.
            emit_define_own(self, m.text, v, true);
        }
        release_to(mark);
    }
    proto().emit(instruction{op::ret_undef});
    finish_frame(index, 0);
    pop_scope();
    frames_.pop_back();
    in_chain_ = saved_in_chain;
    optional_exits_.swap(saved_exits);
    finallies_.swap(saved_finallies);
    loops_.swap(saved_loops);
    handler_depth_ = saved_handler_depth;
    return index;
}

void compiler_impl::declare_class_name(std::string name, bool force) {
    if ((force || frames_.size() > 1) && !was_predeclared(name) &&
        find_local_in_current_scope(name) == nullptr) {
        const std::uint16_t reg = declare_local(name);
        fn().locals.back().lexical = true;
        proto().emit(instruction{op::load_undef, reg});
        if (fn().locals.back().boxed) { proto().emit(instruction{op::new_cell, reg}); }
    }
}

std::string compiler_impl::field_key_name(std::int32_t member) const {
    return "@fieldkey:" + std::to_string(private_scopes_.back().klass) + ":" +
           std::to_string(member);
}

void compiler_impl::compile_class(const vp::node & n, std::uint16_t dst, bool as_declaration,
                                  std::string_view inferred_name) {
    // The name is DECLARED first, before any method body is compiled, so a
    // method that mentions it resolves to this binding rather than to an
    // outer one - capture is decided when the nested function is compiled,
    // not when it runs. The value is written further down, as soon as the
    // class exists.
    // A scope of its own for an expression's name, so the methods can capture
    // it and the code after the expression cannot see it.
    const bool own_scope = !as_declaration && !n.text.empty();
    if (own_scope) { push_scope(); }
    ++class_body_depth_; // everything in here is strict code (15.7.1)
    if (!n.text.empty()) { declare_class_name(std::string{n.text}, own_scope); }
    // AN EXPRESSION'S OWN NAME IS IN ITS DEAD ZONE until the class exists
    // (15.7.14 step 4.a creates it uninitialised; the heritage and the
    // computed keys run before step 27 initialises it): `(class x extends x
    // {})` is the ReferenceError, decided statically as a declaration's is
    // (capture.cpp hoists that one with the same offset). Only reads in this
    // frame see it - a method's body is a function of its own.
    if (own_scope && !n.text.empty() && n.end > 0) {
        if (local * self = find_local_in_current_scope(n.text)) { self->initialized_at = n.end; }
    }
    // Inside the body the name is the class's own immutable binding - see
    // class_bindings_. Wherever the declaration put the name, a write from
    // in here that resolves to the same binding is refused.
    if (!n.text.empty()) {
        const auto [level, position] = binding_position(n.text);
        class_bindings_.push_back(class_binding{std::string{n.text}, level, position});
    }
    const boost::scope::scope_exit close_binding{[&]() noexcept {
        if (!n.text.empty()) { class_bindings_.pop_back(); }
    }};
    const std::span<const std::int32_t> members = kids(n);
    // The body's private names, in scope for every member (a method may
    // read a field declared below it) and gone when the body closes.
    private_scopes_.push_back(private_scope{{}, ++private_classes_, {}});
    for (const std::int32_t member : members) {
        const vp::node & m = at(member);
        if ((m.d & 2) == 0 && m.text.starts_with('#')) {
            private_scopes_.back().names.push_back(m.text);
        }
    }
    const boost::scope::scope_exit closing{[&]() noexcept { private_scopes_.pop_back(); }};
    // The computed field names' cells - see field_key_name. Boxed whatever
    // the capture index says: only the initialisers, compiled below, read
    // them, and nothing in the source names them.
    for (const std::int32_t member : members) {
        const vp::node & m = at(member);
        if (m.c != 0 || (m.d & 2) == 0 || m.a < 0) { continue; }
        const std::uint16_t cell = declare_local(field_key_name(member));
        fn().locals.back().boxed = true;
        proto().emit(instruction{op::load_undef, cell});
        proto().emit(instruction{op::new_cell, cell});
    }
    const std::uint32_t mark = reg_mark();
    const std::uint16_t prototype_reg = alloc_reg();
    proto().emit(instruction{op::new_object, prototype_reg});

    // `extends`: the heritage is evaluated first (15.7.14 step 6), and wired
    // once the constructor exists - see class_heritage_name below.
    const std::uint16_t parent = n.a >= 0 ? alloc_reg() : 0;
    if (n.a >= 0) { compile_expr(n.a, parent); }

    std::int32_t constructor_body = -1;
    bool has_instance_elements = false;
    for (const std::int32_t member : members) {
        const vp::node & m = at(member);
        // THE constructor is the one non-static, non-computed method of the
        // name (15.7.3 ConstructorMethod); `static constructor() {}` is an
        // ordinary static method.
        if (m.c == 1 && m.text == "constructor" && (m.d & 3) == 0) { constructor_body = m.b; }
        // An instance field, or a private method/accessor (whose brand the
        // initialiser adds) - what `__fields` will hold, see below.
        if ((m.d & 1) == 0 &&
            (m.c == 0 || ((m.c == 1 || m.c == 2) && (m.d & 2) == 0 && m.text.starts_with('#')))) {
            has_instance_elements = true;
        }
    }
    // A BASE class runs its fields at the top of its constructor - whichever
    // way it is entered; a DERIVED class after its `super()` returns (see
    // emit_init_fields_after_super). The VM runs none at [[Construct]].
    base_fields_pending_ = n.a < 0 && has_instance_elements;

    if (constructor_body >= 0) {
        // Named after the CLASS. A constructor is a function expression, so
        // it had no name of its own and every stack trace through one said
        // `<anonymous>` - which in a 4.5 MB bundle is no answer at all.
        derived_ctor_pending_ = n.a >= 0; // see frame::derived_flag
        method_pending_ = true;
        const std::uint32_t index = compile_function_body(constructor_body, std::string{n.text});
        derived_ctor_pending_ = false;
        proto().emit(instruction::with_bx(op::closure, dst, index));
    } else if (n.a >= 0) {
        // A DERIVED class with no constructor gets `constructor(...args) {
        // super(...args); }`. Synthesising an EMPTY one instead meant the
        // parent constructor never ran, so `class MySet extends Set {}`
        // produced an object with none of Set's state and every method on
        // it failed - with nothing to say the constructor had been skipped.
        // `return this`, because super() may have rebound it to the object
        // the parent returned (bind_this_name) and [[Construct]] answers
        // what the body returns when that is an object.
        compile_foreign_expr("(function (...args) { super(...args); return this; })", dst);
    } else {
        // A base class with no constructor still needs a callable, or `new`
        // has nothing to invoke.
        compile_foreign_expr("(function () {})", dst);
    }
    base_fields_pending_ = false;
    // A SYNTHESISED CONSTRUCTOR IS STILL NAMED AFTER ITS CLASS. Both
    // branches above build one from source text, so it arrives anonymous -
    // and `Object.getPrototypeOf(x).constructor.name` is a standard way to
    // identify a value, where an undefined name compares equal to the other
    // undefined it is being tested against and reports a false match.
    // AND CARRIES THE CLASS'S SOURCE SPAN (20.2.3.5: `String(C)` is the class
    // text). The synthesised one was compiled from "(function () {})" and kept
    // THAT text's offsets, which read as fifteen bytes of whatever the program
    // starts with; an explicit constructor kept its own member span.
    if (!proto().code.empty() && proto().code.back().code == op::closure) {
        function_proto & ctor = out_.functions[proto().code.back().bx()];
        ctor.name = std::string{n.text};
        if (n.text.empty()) { ctor.inferred_name = std::string{inferred_name}; }
        if (n.end > n.begin) {
            ctor.source_begin = n.begin;
            ctor.source_end = n.end;
        }
    }
    if (n.a >= 0) {
        // The parent must be null or a constructor with an object (or null)
        // `prototype` - each a TypeError otherwise - and then C.prototype
        // chains to P.prototype and C to P (15.7.14 steps 6-9). One native
        // rather than four opcodes, and the checks with it.
        const std::uint32_t inner = reg_mark();
        const std::uint16_t callee = alloc_reg();
        proto().emit(instruction::with_bx(op::get_global, callee,
                                          intern_name(std::string{class_heritage_name})));
        const std::uint16_t ctor = alloc_reg();
        proto().emit(instruction{op::move, ctor, dst});
        const std::uint16_t heritage = alloc_reg();
        proto().emit(instruction{op::move, heritage, parent});
        const std::uint16_t table = alloc_reg();
        proto().emit(instruction{op::move, table, prototype_reg});
        proto().emit(instruction{op::call, callee, 3});
        release_to(inner);
    }
    proto().emit(instruction{op::set_prop, dst, name_operand("prototype"), prototype_reg});
    // `C.prototype.constructor === C`, which is both what pages expect and
    // how `super(...)` finds the parent constructor to call.
    proto().emit(instruction{op::set_prop, prototype_reg, name_operand("constructor"), dst});
    // The constructor's home is this class's prototype, so `super(...)`
    // inside it starts one level up.
    proto().emit(instruction{op::set_prop, dst, name_operand("__home"), prototype_reg});

    // INSTANCE FIELDS ARE PER INSTANCE, and they used to be evaluated once
    // at class-definition time and stored on the PROTOTYPE. So every
    // instance of `class A { items = [] }` shared one array: push to one and
    // it appeared in all of them. Nothing about that was an error, and it is
    // the nastiest shape of bug in this whole batch.
    //
    // They are compiled into a hidden initialiser instead, which `new` runs
    // against the fresh object before the constructor body. Static fields
    // are NOT this - evaluating those once and putting them on the
    // constructor is exactly right, and that is what still happens below.
    std::vector<std::int32_t> instance_fields;
    bool instance_brand = false;
    bool static_brand = false;
    for (const std::int32_t member : members) {
        const vp::node & m = at(member);
        if (m.c == 0 && (m.d & 1) == 0) { instance_fields.push_back(member); }
        // A private method or accessor brands its holder - see
        // context::find_private_element.
        if ((m.c == 1 || m.c == 2) && (m.d & 2) == 0 && m.text.starts_with('#')) {
            ((m.d & 1) != 0 ? static_brand : instance_brand) = true;
        }
    }
    const std::string brand =
        std::string{private_key_prefix} + ":" + std::to_string(private_scopes_.back().klass);
    if (!instance_fields.empty() || instance_brand) {
        const std::uint32_t fields =
            compile_field_initialiser(instance_fields, false, instance_brand ? brand : "");
        const std::uint16_t init = alloc_reg();
        proto().emit(instruction::with_bx(op::closure, init, fields));
        proto().emit(instruction{op::set_prop, dst, name_operand("__fields"), init});
        if (instance_brand) { emit_define_own(init, private_home_key, prototype_reg, false); }
        // AN INSTANCE FIELD'S HOME OBJECT IS THE PROTOTYPE (15.7.10
        // ClassFieldDefinitionEvaluation: MakeMethod(initializer, homeObject)),
        // so `x = super.y` reads the parent's - wired, as an object literal's
        // method is, only when an initialiser says `super` or `eval(`.
        if (std::ranges::any_of(instance_fields, [&](std::int32_t f) {
                return mentions_super(at(f).b, false) || contains_direct_eval(at(f).b);
            })) {
            proto().emit(instruction{op::set_prop, init, name_operand("__home"), prototype_reg});
        }
    }
    if (static_brand) { emit_private_add(dst, brand, dst); }

    // A NAMED CLASS EXPRESSION BINDS ITS OWN NAME, and its methods see it.
    //
    // `let p5$2 = class p5 { static register(a) { p5._seen.has(a); } }` is
    // how p5.js declares itself, and `p5` inside those methods is the class
    // - not any outer binding. Without this the methods read an undefined
    // global and failed at the first property they touched, three frames
    // deep and nowhere near the class.
    //
    // The binding is made before the methods are COMPILED so they capture
    // it, and written as soon as the class value exists.
    if (!n.text.empty()) {
        // The class's own binding, a declaration's first write (see declaring_).
        const bool outer_declaring = declaring_;
        declaring_ = true;
        emit_write(n.text, dst);
        declaring_ = outer_declaring;
    }

    // TWO PASSES, WHICH IS THE SPECIFICATION'S ORDER: every method and accessor
    // is defined first (ClassDefinitionEvaluation step 26), then the class's
    // own members are made non-enumerable in one native call, and only THEN
    // do the static field initialisers run (step 31) - so a static initialiser
    // that reads a static method finds it, and `Object.keys(C)` lists the
    // static fields and nothing else.
    const std::uint16_t slot = alloc_reg();
    for (const std::int32_t member : members) {
        const vp::node & m = at(member);
        if (m.text == "constructor" && m.c == 1 && (m.d & 3) == 0) { continue; }
        if (m.c == 0 && (m.d & 2) != 0 && m.a >= 0) {
            // A computed field name: evaluated and made a key HERE, in order
            // with the methods' - see field_key_name.
            const std::uint32_t at_key = reg_mark();
            const std::uint16_t key = alloc_reg();
            compile_expr(m.a, key);
            emit_iterator_native(to_property_key_name, key, key);
            const local * cell = find_local_entry(fn(), field_key_name(member));
            if (cell != nullptr) { proto().emit(instruction{op::cell_set, cell->reg, key}); }
            release_to(at_key);
            continue;
        }
        if (m.c == 0 || m.c == 3) { continue; } // fields and static blocks: below
        const bool computed = (m.d & 2) != 0 && m.a >= 0;
        if (m.c == 2) {
            // An accessor. It goes on the prototype like a method - or on
            // the constructor when static - and `d` bit2 says which half.
            // (Named below, with the methods.)
            // Installing it as a DATA property, which is what happened
            // before, made `obj.v` be the function rather than call it, and
            // a `set` of the same name overwrote the getter outright.
            const std::uint16_t target = (m.d & 1) != 0 ? dst : prototype_reg;
            if (computed) {
                // `get [k]() {}` - the key is a value, so the definition is
                // the native's (see define_accessor_name).
                emit_computed_accessor(target, m.a, m.b, (m.d & 4) != 0);
                continue;
            }
            method_pending_ = true;
            // An accessor is named for its key, "get id" (15.4.5 step 6).
            compile_named_expr(m.b, slot, ((m.d & 4) != 0 ? "set " : "get ") + std::string{m.text});
            method_pending_ = false;
            const std::uint16_t name = m.text.starts_with('#')
                                           ? name_operand(member_key(m.text) + ":method")
                                           : member_operand(m.text);
            proto().emit(instruction{(m.d & 4) != 0 ? op::define_setter : op::define_getter, target,
                                     name, slot});
            // An accessor has a home object like a method (15.4.5 step 3 /
            // 15.4.6 step 3): `super.x` inside a getter resolves through it.
            proto().emit(instruction{op::set_prop, slot, name_operand("__home"), target});
            continue;
        }
        if (m.b < 0) { continue; }
        // A METHOD KNOWS ITS OWN NAME, and JavaScript says so: `({m(){}}).m.name`
        // is "m". The name is on the MEMBER node; the function expression under
        // it is anonymous, so compiling it through compile_expr gave every
        // method a nameless proto - and every stack trace, every TypeError and
        // every `fn.name` through one said "<anonymous>". The constructor above
        // already goes through compile_function_body for exactly this reason.
        //
        // ONLY FOR c == 1. A STATIC FIELD reaches this line too and its `b` is
        // an arbitrary expression rather than a function.
        // A static member goes on the constructor; everything else on the
        // prototype, where instances find it.
        const std::uint16_t target = (m.d & 1) != 0 ? dst : prototype_reg;
        if (m.c == 1 && computed) {
            // `[k]() {}` (15.4.5 step 1: the key is evaluated before the
            // method is made): key first, closure second, set_index.
            const std::uint32_t mark = reg_mark();
            const std::uint16_t key = alloc_reg();
            compile_expr(m.a, key);
            // Made a key ONCE, and it names the method (15.4.4 DefineMethod,
            // then SetFunctionName) - known now when it is a literal.
            const std::optional<std::string> literal_key = literal_key_name(m.a);
            if (!literal_key) { emit_iterator_native(to_property_key_name, key, key); }
            method_pending_ = true;
            const std::uint32_t index = compile_function_body(m.b, "");
            if (literal_key) { out_.functions[index].inferred_name = *literal_key; }
            proto().emit(instruction::with_bx(op::closure, slot, index));
            if (!literal_key) { emit_set_function_name(slot, key); }
            if ((m.d & 1) != 0) {
                // A STATIC one is DEFINED (15.4.4 DefineMethodProperty), not
                // set: a [[Set]] on the constructor asks Function.prototype,
                // whose @@hasInstance is read-only, so `static
                // [Symbol.hasInstance]() {}` would be refused.
                emit_define_own_key(target, key, slot, false);
            } else {
                proto().emit(instruction{op::set_index, target, key, slot});
            }
            proto().emit(instruction{op::set_prop, slot, name_operand("__home"), target});
            release_to(mark);
            continue;
        }
        if (m.c == 1) {
            method_pending_ = true;
            const std::uint32_t index = compile_function_body(m.b, std::string{m.text});
            proto().emit(instruction::with_bx(op::closure, slot, index));
        } else {
            compile_expr(m.b, slot);
        }
        if ((m.d & 1) != 0 && (m.text == "name" || m.text == "length" || m.text == "caller" ||
                               m.text == "arguments")) {
            // A static `name`/`length` shadows the constructor's own
            // read-only one: DEFINED, not set (see define_own_name). So does
            // a static `caller`/`arguments`, which a [[Set]] would hand to
            // Function.prototype's %ThrowTypeError% accessor.
            emit_define_own(target, m.text, slot, false);
        } else if (m.text.starts_with('#')) {
            // The home holds the definition under a separate key: it is not
            // itself branded, and may later receive a private field or brand.
            emit_define_own(target, member_key(m.text) + ":method", slot, false);
        } else {
            const std::uint16_t name = member_operand(m.text);
            proto().emit(instruction{op::set_prop, target, name, slot});
        }
        // Each method remembers where it was WRITTEN. `super.m()` resolves
        // against that, not against `this` - in a three-deep hierarchy the
        // two differ and resolving against `this` calls the same method
        // forever.
        if (m.c == 1) {
            proto().emit(instruction{op::set_prop, slot, name_operand("__home"), target});
        }
    }
    {
        const std::uint16_t callee = alloc_reg();
        proto().emit(instruction::with_bx(op::get_global, callee,
                                          intern_name(std::string{class_defined_name})));
        const std::uint16_t klass = alloc_reg();
        proto().emit(instruction{op::move, klass, dst});
        proto().emit(instruction{op::call, callee, 1});
    }
    // THE STATIC FIELDS AND STATIC BLOCKS, in source order, as one `<static>`
    // function called once with the class as `this` - which is what makes
    // `static x = this.y` read the class and `static { }` a body of its own.
    // Its home object is the class, so `super.m()` in a static block finds
    // the parent constructor's methods, as it does from a static method.
    std::vector<std::int32_t> static_members;
    for (const std::int32_t member : members) {
        const vp::node & m = at(member);
        if (m.c == 3 || (m.c == 0 && (m.d & 1) != 0)) { static_members.push_back(member); }
    }
    if (!static_members.empty()) {
        const std::uint32_t init = compile_field_initialiser(static_members, true);
        proto().emit(instruction::with_bx(op::closure, slot, init));
        proto().emit(instruction{op::set_prop, slot, name_operand("__home"), dst});
        proto().emit(instruction{op::call_receiver, slot, 0, dst});
    }
    release_to(mark);
    --class_body_depth_;
    // Closed AFTER every method is compiled, so they capture the name, and
    // before anything else in the enclosing scope is - so nothing else sees
    // it. The register stays allocated, which is what a scope pop means here.
    if (own_scope) { pop_scope(); }
}

} // namespace ctbrowser::script::detail
