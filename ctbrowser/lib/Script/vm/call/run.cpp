// ctbrowser.script context - running a program: the top-level entry, and the
// re-entrant one a dynamic import needs.
//
// One of five files carved out of a 1,171-line vm/call.cpp on 2026-09-08 -
// which was itself one of four carved out of a 3,232-line vm.cpp on
// 2026-08-09. All members of `context`, declared in
// include/ctbrowser/script/vm.hpp - so they split across translation units
// with nothing to declare.

#include <cstddef>
#include <string>

#include <ctbrowser/script/vm.hpp>

namespace ctbrowser::script {

// RUNNING A PROGRAM WHILE ONE IS ALREADY RUNNING, which `run` cannot do and
// must not pretend to: `execute` CLEARS `frames_` and reassigns `registers_`,
// because it is the entry point for a whole turn. A dynamic import is the first
// thing that ever needed a program evaluated from inside the interpreter, and
// calling `run` there wiped the importing module's own frame - so the module
// stopped dead at the `import(...)` and every statement after it silently never
// ran. Nothing threw; the loop simply found no frames left and finished.
//
// This is `call`'s shape rather than `run`'s: push a frame on top of what is
// already there and run down to the depth it started at.
run_result context::run_reentrant(const program & prog) {
    run_result result;
    if (!prog.ok) {
        result.ok = false;
        result.error = prog.error;
        return result;
    }
    const function_proto & entry = prog.functions[0];
    const value receiver =
        prog.kind == script_kind::classic ? current_realm().global_this : value::undefined();
    const std::size_t new_base = registers_.size();
    registers_.resize(new_base + entry.frame_size + 8u, value::undefined());
    const std::size_t depth = frames_.size();
    // THE PROGRAM POINTER IS SAVED AND RESTORED. `call` reads it to decide
    // whether a closure can be entered at all, and leaving it pointing at the
    // imported module would make every later call in the IMPORTING one look up
    // its function protos in the wrong program.
    const program * const outer_program = program_;
    program_ = &prog;
    // AND THE SAME QUESTION FOR A MODULE EVALUATING INSIDE ITS IMPORTER. This
    // is the third place that entered a program's top level by pushing a frame
    // of its own; leaving it out would mean an imported module's compiled body
    // was interpreted purely because of who imported it.
    if (value produced = value::undefined();
        enter_compiled(*this, entry, value::undefined(), registers_.data() + new_base, new_base, 0u,
                       receiver, /*constructing*/ false, produced)) {
        program_ = outer_program;
        if (registers_.size() >= new_base) { registers_.resize(new_base); }
        return result;
    }
    call_frame entered{&entry, 0, new_base, 0, 0, nullptr, receiver, handlers_.size()};
    entered.realm = &current_realm();
    frames_.push_back(entered);
    (void)run_loop(depth);
    program_ = outer_program;
    if (registers_.size() >= new_base) { registers_.resize(new_base); }
    // NO drain_microtasks HERE. The checkpoint belongs to the end of a turn,
    // and this is the middle of one - draining now would run the importer's own
    // pending handlers before its next statement.
    result.ok = !failed_;
    result.error = error_;
    return result;
}

bool context::global_declaration_instantiation(const program & prog) {
    if (prog.eval_code) { return !prog.eval_global || eval_declaration_instantiation(prog); }
    if (prog.kind != script_kind::classic ||
        (prog.lexical_names.empty() && prog.function_names.empty() && prog.hoisted_vars.empty())) {
        return true;
    }
    realm_record & realm = current_realm();
    const auto refuse = [&](const std::string & name) {
        throw_error("SyntaxError", "Identifier '" + name + "' has already been declared");
        return false;
    };
    // HasRestrictedGlobalProperty (9.1.1.4.14): a non-configurable own
    // property of the global object - `undefined`, `NaN`, one a page defined
    // so - may not be shadowed by a `let`. Asked of the proxy's target, as
    // declare_global_var asks it.
    value holder = realm.global_this;
    if (holder.is_kind(heap_kind::proxy)) {
        holder = static_cast<proxy_object *>(holder.as_heap())->target;
    }
    for (const std::string & name : prog.lexical_names) {
        if (realm.var_names.contains(name) || realm.lexical.contains(name)) { return refuse(name); }
        property_descriptor own;
        if (holder.is_object() && own_property(holder, name, own) && own.has_configurable &&
            !own.configurable) {
            return refuse(name);
        }
    }
    for (const std::string & name : prog.hoisted_vars) {
        if (realm.lexical.contains(name)) { return refuse(name); }
    }
    for (const std::string & name : prog.function_names) {
        if (realm.lexical.contains(name)) { return refuse(name); }
    }
    // CanDeclareGlobalFunction and CanDeclareGlobalVar (9.1.1.4.15-16, 16.1.7
    // steps 8-12): a function may replace a configurable property or a
    // writable, enumerable data one; any other name needs the global object
    // extensible unless it already owns one. A TypeError otherwise, before a
    // single binding exists. Read from the two stores behind the traps - the
    // target, then the table - because the traps' own invariant check refuses
    // a table name once the target is not extensible.
    const bool extensible = !holder.is_heap() || is_extensible(holder);
    const auto global_own = [&](const std::string & name, property_descriptor & out) {
        if (holder.is_object() && own_property(holder, name, out)) { return true; }
        const auto it = realm.globals.find(name);
        if (it == realm.globals.end() || realm.lexical.contains(name)) { return false; }
        out = property_descriptor::data(it->second, realm.builtin.contains(name)
                                                        ? attr_writable | attr_configurable
                                                        : attr_default);
        return true;
    };
    const auto cannot = [&](const std::string & name) {
        throw_error("TypeError", "Cannot declare global '" + name + "'");
        return false;
    };
    for (const std::string & name : prog.function_names) {
        property_descriptor existing;
        if (!global_own(name, existing)) {
            if (!extensible) { return cannot(name); }
        } else if (!existing.configurable &&
                   !(existing.is_data() && existing.writable && existing.enumerable)) {
            return cannot(name);
        }
    }
    for (const std::string & name : prog.hoisted_vars) {
        if (property_descriptor existing; !extensible && !global_own(name, existing)) {
            return cannot(name);
        }
    }
    for (const std::string & name : prog.lexical_names) { realm.lexical[name] = true; }
    for (const std::string & name : prog.hoisted_vars) { realm.var_names[name] = true; }
    for (const std::string & name : prog.function_names) { realm.var_names[name] = true; }
    return true;
}

bool context::eval_declaration_instantiation(const program & prog) {
    realm_record & realm = current_realm();
    // Step 3.a: a var or function name the global environment has as a
    // lexical binding - a `let x` of a script, and then `eval('var x')`.
    for (const auto * names : {&prog.hoisted_vars, &prog.function_names}) {
        for (const std::string & name : *names) {
            if (realm.lexical.contains(name)) {
                throw_error("SyntaxError", "Identifier '" + name + "' has already been declared");
                return false;
            }
        }
    }
    // Steps 8-10, as GlobalDeclarationInstantiation asks them (above).
    value holder = realm.global_this;
    if (holder.is_kind(heap_kind::proxy)) {
        holder = static_cast<proxy_object *>(holder.as_heap())->target;
    }
    const bool extensible = !holder.is_heap() || is_extensible(holder);
    const auto global_own = [&](const std::string & name, property_descriptor & out) {
        if (holder.is_object() && own_property(holder, name, out)) { return true; }
        const auto it = realm.globals.find(name);
        if (it == realm.globals.end() || realm.lexical.contains(name)) { return false; }
        out = property_descriptor::data(it->second, realm.builtin.contains(name)
                                                        ? attr_writable | attr_configurable
                                                        : attr_default);
        return true;
    };
    for (const std::string & name : prog.function_names) {
        property_descriptor existing;
        const bool definable =
            global_own(name, existing)
                ? existing.configurable ||
                      (existing.is_data() && existing.writable && existing.enumerable)
                : extensible;
        if (!definable) {
            throw_error("TypeError", "Cannot declare global function '" + name + "'");
            return false;
        }
    }
    for (const std::string & name : prog.hoisted_vars) {
        if (property_descriptor existing; !extensible && !global_own(name, existing)) {
            throw_error("TypeError", "Cannot declare global variable '" + name + "'");
            return false;
        }
    }
    for (const std::string & name : prog.hoisted_vars) { realm.var_names[name] = true; }
    for (const std::string & name : prog.function_names) { realm.var_names[name] = true; }
    return true;
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
    // See program::hoisted_vars: the script's `var`s exist before it starts
    // - once GlobalDeclarationInstantiation has found nothing to refuse. Its
    // refusal is thrown with no frame and no handler, as execute() would
    // leave them, so a dead program's `try` cannot catch it.
    frames_.clear();
    handlers_.clear();
    if (global_declaration_instantiation(prog)) {
        for (const std::string & name : prog.hoisted_vars) {
            declare_global_var(name, prog.eval_code);
        }
        for (const std::string & name : prog.function_names) {
            declare_global_function(name, prog.eval_code);
        }
        result.returned = execute(prog, prog.functions[0]);
    }
    // THE END OF THE TURN. A script's promise handlers run after its last
    // statement, not between two of them - so the checkpoint is here, once the
    // top level has finished.
    drain_microtasks();
    // AND A FAILED SCRIPT'S QUEUE DIES WITH IT. `drain_microtasks` stops on
    // `failed_`, so a script that threw left its already-queued jobs in the
    // queue - and once a page's classic scripts became separate programs, the
    // NEXT script's checkpoint ran them, after that script's synchronous code.
    // A handler belonging to a script that is over should not surface in the
    // middle of the following one.
    //
    // CHROME WOULD RUN THEM, and that difference is real: an uncaught throw
    // does not cancel a microtask checkpoint there. It is not matched here
    // because `raise` and an uncaught throw set the same `failed_` - the
    // call-stack ceiling and the allocation ceiling look exactly like a page
    // exception from this function - and draining after a resource ceiling
    // would run more JavaScript precisely where the ceiling exists to stop it.
    // Telling the two apart is the fix; dropping the queue is the safe half of
    // it, and this comment is the record of which half was taken.
    if (failed_) { microtasks_.clear(); }
    result.ok = !failed_;
    result.error = error_;
    return result;
}

value context::execute(const program & prog, const function_proto & entry) {
    registers_.assign(entry.frame_size + 8u, value::undefined());
    frames_.clear();
    // AND THE HANDLER STACK, WHICH FRAMES_.CLEAR() DOES NOT IMPLY. A VM-level
    // `raise` - the call-stack ceiling, the allocation ceiling - returns out of
    // run_loop WITHOUT unwinding, because the loop's condition is
    // `frames_.size() > stop_depth && !failed_` and it simply stops. Any `try`
    // that was live at that moment stays on `handlers_` recording frame 0.
    //
    // A SECOND TOP-LEVEL PROGRAM ON THIS CONTEXT THEN HAS A FRAME 0 TOO, so
    // `unwind_to_handler` accepts the dead program's handler: it only rejects
    // one whose frame has returned. It writes the thrown value into the wrong
    // frame's register and sets `ip` to an ADDRESS OUT OF THE OTHER PROGRAM'S
    // BYTECODE, and the interpreter carries on from there. Measured: a page
    // whose first script exhausts the stack inside a `try` and whose second
    // alerts 1..8 then throws produced 1,2,3,4,5,6,7,8,3,4,5,6,7,8 - the throw
    // swallowed, six statements run twice - and padding the first script moved
    // where the second one resumed.
    //
    // It became reachable when a page's classic scripts stopped being one
    // program: before that, a raise in the first script ended the only top
    // level and nothing else ran on the dirtied context.
    handlers_.clear();
    thrown_ = value::undefined();
    program_ = &prog;
    // Script this belongs to the realm, including in a strict classic script.
    // Modules have no top-level receiver. Never reload the writable globalThis
    // binding here: an earlier script may have replaced or cleared it.
    const value receiver =
        prog.kind == script_kind::classic ? current_realm().global_this : value::undefined();
    // A COMPILED TOP LEVEL, IF THIS PROGRAM HAS ONE, and for ctcompile that is
    // the ordinary case rather than an exotic one: a page's `<script>` IS a top
    // level, so a backend that compiles anything compiles this. Asked after the
    // reset above, so a compiled body enters a context in the state it expects,
    // and before the frame push, because ct_aot_enter pushes its own.
    if (value produced = value::undefined();
        enter_compiled(*this, entry, value::undefined(), registers_.data(), 0u, 0u, receiver,
                       /*constructing*/ false, produced)) {
        return produced;
    }
    call_frame entered{&entry, 0, 0, 0, 0, nullptr, receiver, 0};
    entered.realm = &current_realm();
    frames_.push_back(entered);
    // Per-frame string interning: a literal in a loop should allocate once,
    // not once per iteration.
    string_cache_.clear();
    bigint_cache_.clear();
    // A TOP-LEVEL PROGRAM IS A C++ ENTRY LIKE ANY OTHER. Marking it is what
    // makes the transition INSIDE it attributable: without this, `executing_`
    // is still `cxx` for the whole run, so an interpreted script calling a
    // compiled function would be counted as C++ reaching it directly. The
    // interpreter is what is running; say so.
    note_transition_into_vm(*this);
    const executing_as running{*this, executing_kind::vm};
    return run_loop(0);
}

} // namespace ctbrowser::script
