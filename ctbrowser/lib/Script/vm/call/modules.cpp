// ctbrowser.script context - ES modules: instantiation, the namespace object,
// the four module opcode bodies, and running a module.
//
// One of five files carved out of a 1,171-line vm/call.cpp on 2026-09-08 -
// which was itself one of four carved out of a 3,232-line vm.cpp on
// 2026-08-09. All members of `context`, declared in
// include/ctbrowser/script/vm.hpp - so they split across translation units
// with nothing to declare.

#include <algorithm>
#include <span>
#include <string>
#include <vector>

#include <ctbrowser/script/vm.hpp>

namespace ctbrowser::script {

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

// THE NAMESPACE OBJECT, and its properties are ACCESSORS rather than values.
//
// A namespace is live exactly as a named import is - `ns.count` after the
// exporter reassigns `count` must read the new value - so copying the cells'
// contents into an ordinary object here would be the same shortcut in a
// different shape. Each property is a getter over the cell instead, which is
// what the cell was for.
//
// ONE OBJECT PER MODULE, cached in the record: `import * as a` and `import * as
// b` of the same module are required to be the SAME object, and code compares
// namespaces by identity.
//
// AND AS MUCH OF 10.4.6's EXOTIC OBJECT AS AN ORDINARY ONE CAN BE: a null
// [[Prototype]], not extensible, `@@toStringTag` "Module" (non-writable,
// non-enumerable, non-configurable), the exports in code-unit order, each
// enumerable and NOT configurable - so a write, a `delete` and a new property
// are refused as the specification refuses them. Still getters rather than
// its data properties: a namespace read is a get_prop and an accessor
// re-entry, which is what the AOT bridge's lowering of one is written against
// (aot/helpers/calls-and-modules.def, ct_aot_module_namespace).
value context::module_namespace(module_record & of) {
    if (of.namespace_object.is_kind(heap_kind::object)) { return of.namespace_object; }
    object_object * const ns = allocate<object_object>();
    of.namespace_object = value::object(ns);
    ns->prototype = value::undefined(); // an explicit null (object_object::prototype)
    std::vector<std::string> names;
    names.reserve(of.exports.size());
    for (const auto & [name, cell] : of.exports) { names.push_back(name); }
    std::ranges::sort(names);
    for (const std::string & name : names) {
        const value box = of.exports.find(name)->second;
        ns->define_accessor(name,
                            value::object(allocate<native_object>(
                                "get " + name,
                                [box](context &, std::span<value>) {
                                    return box.is_kind(heap_kind::cell)
                                               ? static_cast<cell_object *>(box.as_heap())->slot
                                               : value::undefined();
                                })),
                            value::undefined(), attr_enumerable);
    }
    ns->define(CTBROWSER_SYMBOL_KEY("toStringTag"), string("Module"), attr_none);
    ns->define(module_namespace_key, value::boolean(true), attr_none);
    ns->extensible = false;
    return of.namespace_object;
}

// THE DEFERRED FORM (`import defer * as ns`, 16.2.2), and this one IS exotic:
// a proxy over the specification's namespace object. Every operation on a
// string key but `then` - [[Get]], [[GetOwnProperty]], [[DefineOwnProperty]],
// [[HasProperty]], [[Delete]] and [[OwnPropertyKeys]] - evaluates the module
// first (EnsureDeferredNamespaceEvaluation); a symbol key, `then`, [[Set]]
// and the prototype and extensibility questions do not
// (language/import/import-defer/evaluation-triggers). The target is what the
// traps must answer consistently with: no prototype, not extensible, each
// export a writable, enumerable, non-configurable property, and
// @@toStringTag "Deferred Module". Values come from the cells.
value context::deferred_module_namespace(module_record & of) {
    if (!of.deferred_namespace_object.is_undefined()) { return of.deferred_namespace_object; }
    const std::string key = of.specifier;
    std::vector<std::string> names;
    names.reserve(of.exports.size());
    for (const auto & [name, cell] : of.exports) { names.push_back(name); }
    std::ranges::sort(names);

    object_object * const target = allocate<object_object>();
    const rooted keep_target{*this, value::object(target)};
    target->prototype = value::undefined(); // an explicit null
    for (const std::string & name : names) {
        target->define(name, value::undefined(), attr_writable | attr_enumerable);
    }
    target->define(CTBROWSER_SYMBOL_KEY("toStringTag"), string("Deferred Module"), attr_none);
    target->extensible = false;
    object_object * const handler = allocate<object_object>();
    const rooted keep_handler{*this, value::object(handler)};
    const auto trap = [&](const char * name, native_fn fn) {
        handler->set(name, value::object(allocate<native_object>(name, std::move(fn))));
    };

    // IsSymbolLikeNamespaceKey: a symbol, or "then".
    const auto symbol_like = [](context & c, value k) {
        return k.is_kind(heap_kind::symbol) || c.to_string(k) == "then";
    };
    // EnsureDeferredNamespaceEvaluation. False when the module threw, which
    // is then this operation's throw.
    const auto evaluate = [key](context & c) {
        const auto found = c.modules_.find(key);
        if (found == c.modules_.end() || found->second.evaluated) { return true; }
        if (!c.module_evaluator_) {
            c.raise("a deferred module has no evaluator installed");
            return false;
        }
        if (!c.module_evaluator_(c, found->second)) {
            const value thrown = c.last_thrown();
            c.throw_value(thrown.is_undefined()
                              ? c.make_error("Error", "module `" + key + "` failed to evaluate")
                              : thrown);
            return false;
        }
        return true;
    };
    // The export's cell, or null for a name the module does not export.
    const auto cell_of = [key](context & c, const std::string & name) -> const value * {
        const auto found = c.modules_.find(key);
        if (found == c.modules_.end()) { return nullptr; }
        const auto cell = found->second.exports.find(name);
        return cell == found->second.exports.end() ? nullptr : &cell->second;
    };
    const auto slot_of = [](const value & cell) {
        return cell.is_kind(heap_kind::cell) ? static_cast<cell_object *>(cell.as_heap())->slot
                                             : value::undefined();
    };

    trap("get", [symbol_like, evaluate, cell_of, slot_of](context & c, std::span<value> a) {
        if (symbol_like(c, a[1])) { return c.lookup_property(a[0], c.to_string(a[1])); }
        if (!evaluate(c)) { return value::undefined(); }
        const value * cell = cell_of(c, c.to_string(a[1]));
        return cell == nullptr ? value::undefined() : slot_of(*cell);
    });
    trap("getOwnPropertyDescriptor",
         [symbol_like, evaluate, cell_of, slot_of](context & c, std::span<value> a) {
             const std::string name = c.to_string(a[1]);
             property_descriptor found;
             if (symbol_like(c, a[1])) {
                 return c.own_property(a[0], name, found) ? c.from_property_descriptor(found)
                                                          : value::undefined();
             }
             if (!evaluate(c)) { return value::undefined(); }
             const value * cell = cell_of(c, name);
             if (cell == nullptr) { return value::undefined(); }
             return c.from_property_descriptor(
                 property_descriptor::data(slot_of(*cell), attr_writable | attr_enumerable));
         });
    // 10.4.6.6 for a string key: only a description the binding already
    // satisfies is "defined".
    trap("defineProperty",
         [symbol_like, evaluate, cell_of, slot_of](context & c, std::span<value> a) {
             const std::string name = c.to_string(a[1]);
             const property_descriptor wanted = c.to_property_descriptor(a[2]);
             if (c.throw_pending()) { return value::boolean(false); }
             if (symbol_like(c, a[1])) {
                 return value::boolean(c.define_own_property(a[0], name, wanted));
             }
             if (!evaluate(c)) { return value::boolean(false); }
             const value * cell = cell_of(c, name);
             if (cell == nullptr) { return value::boolean(false); }
             if ((wanted.has_configurable && wanted.configurable) ||
                 (wanted.has_enumerable && !wanted.enumerable) || wanted.is_accessor() ||
                 (wanted.has_writable && !wanted.writable)) {
                 return value::boolean(false);
             }
             return value::boolean(!wanted.has_value || wanted.held.same_value(slot_of(*cell)));
         });
    trap("has", [symbol_like, evaluate, cell_of](context & c, std::span<value> a) {
        if (symbol_like(c, a[1])) { return value::boolean(c.has_property(a[0], a[1])); }
        if (!evaluate(c)) { return value::boolean(false); }
        return value::boolean(cell_of(c, c.to_string(a[1])) != nullptr);
    });
    trap("deleteProperty", [symbol_like, evaluate, cell_of](context & c, std::span<value> a) {
        const std::string name = c.to_string(a[1]);
        if (symbol_like(c, a[1])) { return value::boolean(c.delete_own_property(a[0], name)); }
        if (!evaluate(c)) { return value::boolean(false); }
        return value::boolean(cell_of(c, name) == nullptr);
    });
    trap("ownKeys", [evaluate, names](context & c, std::span<value>) {
        if (!evaluate(c)) { return value::undefined(); }
        const value keys = c.make_array();
        auto * list = static_cast<array_object *>(keys.as_heap());
        for (const std::string & name : names) { list->items.push_back(c.string(name)); }
        list->items.push_back(c.key_value(CTBROWSER_SYMBOL_KEY("toStringTag")));
        return keys;
    });
    trap("set", [](context &, std::span<value>) { return value::boolean(false); });

    of.deferred_namespace_object =
        value::object(allocate<proxy_object>(value::object(target), value::object(handler)));
    return of.deferred_namespace_object;
}

namespace {

// AS WRITTEN IS NOT AS KEYED: `./dep.js` in one module and in another are two
// different files. The loader left the translation in the record - see
// module_record::resolved - and the bytecode can only carry what was written,
// so every lookup by specifier goes through here first.
//
// A REFERENCE INTO `written` when there is no translation, which is why the
// caller must keep the string alive across the lookup. Both callers below hold
// it in a parameter.
[[nodiscard]] const std::string & keyed_by(const module_record * current,
                                           const std::string & written) {
    if (current == nullptr) { return written; }
    const auto mapped = current->resolved.find(written);
    return mapped == current->resolved.end() ? written : mapped->second;
}

} // namespace

// THE FOUR MODULE OPCODE BODIES. See the declarations in vm.hpp for why they
// are members rather than four blocks inside run_loop: a compiled module and an
// interpreted one must not be able to disagree, and one copy is the only way to
// guarantee it.

value context::module_import_cell(const std::string & specifier, const std::string & export_name) {
    // The exporter has been evaluated already - the loader walks the graph
    // depth-first - so its cell is there to be taken.
    const std::string & from = keyed_by(current_module_, specifier);
    const auto found = modules_.find(from);
    if (found == modules_.end()) {
        raise("module `" + from + "` was not loaded");
        return value::undefined();
    }
    const auto cell = found->second.exports.find(export_name);
    if (cell == found->second.exports.end()) {
        raise("`" + from + "` has no export named `" + export_name + "`");
        return value::undefined();
    }
    return cell->second;
}

value context::module_export_cell(const std::string & name, value current) {
    // NOT IN A MODULE IS NOT AN ERROR, and it must not write. The register
    // holds the local being exported, so answering undefined here would destroy
    // it - the interpreter simply skips the store, and answering `current` is
    // the same thing in a form both tiers can express.
    if (current_module_ == nullptr) { return current; }
    // ADOPT THE RECORD'S CELL, do not publish this register's. The cell is
    // created before ANY module in the graph runs - see instantiate_module - so
    // by the time this executes it already exists and something in a cycle may
    // already be holding it. Overwriting the record here would hand that
    // importer a box nobody ever writes to again.
    //
    // THE CREATING ARM IS LIVE, not a belt: instantiate_module is called by
    // browser::instantiate_module and by nothing else, so a host that runs a
    // module through run_module without instantiating it first arrives here
    // with an empty `exports` map.
    value & slot = current_module_->exports[name];
    if (!slot.is_kind(heap_kind::cell)) {
        slot = value::object(allocate<cell_object>(value::undefined()));
    }
    return slot;
}

value context::module_namespace_for(const std::string & specifier) {
    const std::string & from = keyed_by(current_module_, specifier);
    const auto found = modules_.find(from);
    if (found == modules_.end()) {
        raise("module `" + from + "` was not loaded");
        return value::undefined();
    }
    return module_namespace(found->second);
}

value context::deferred_module_namespace_for(const std::string & specifier) {
    const std::string & from = keyed_by(current_module_, specifier);
    const auto found = modules_.find(from);
    if (found == modules_.end()) {
        raise("module `" + from + "` was not loaded");
        return value::undefined();
    }
    return deferred_module_namespace(found->second);
}

value context::dynamic_import(value specifier, const std::string & referrer) {
    if (!module_loader_) {
        raise("dynamic import() has no loader installed");
        return value::undefined();
    }
    // to_string BEFORE the loader and INSIDE this member. For an object it runs
    // the page's own toString, which can throw and can re-enter - so a tier that
    // converted at its call site instead would be a second conversion with a
    // second set of effects.
    //
    // BOTH BEHIND A FENCE (13.3.10.1 EvaluateImportCall): a ToString that
    // throws, and a module whose evaluation throws, REJECT the promise the
    // import answers (IfAbruptRejectPromise; a rejected evaluation promise).
    // Called bare, the throw landed on the IMPORTER's handler, synchronously -
    // `import(x).catch(...)` never saw it and a script with no try died.
    auto * step =
        allocate<native_object>("import()", [referrer](context & c, std::span<value> args) {
            const std::string text = c.to_string(args.empty() ? value::undefined() : args[0]);
            if (c.throw_pending()) { return value::undefined(); }
            return c.module_loader_(c, text, referrer);
        });
    const rooted keep{*this, value::object(step)};
    bool threw = false;
    value thrown = value::undefined();
    const value arg[1] = {specifier};
    const value promise = call_fenced(value::object(step), arg, value::undefined(), threw, thrown);
    if (failed_) { return value::undefined(); }
    return threw ? make_promise(thrown, true) : promise;
}

// A MODULE IS RUN LIKE ANY OTHER PROGRAM, with two differences: it knows which
// record it is filling in, so `bind_export` knows which cells to adopt, and its
// exports outlive the call.
run_result context::run_module(const program & prog, module_record & into) {
    module_record * const outer = current_module_;
    current_module_ = &into;
    into.compiled = &prog;
    const run_result result = frames_.empty() ? run(prog) : run_reentrant(prog);
    into.evaluated = true;
    current_module_ = outer;
    return result;
}

} // namespace ctbrowser::script
