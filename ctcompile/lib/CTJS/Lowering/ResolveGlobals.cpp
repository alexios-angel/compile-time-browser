//===- ResolveGlobals.cpp - Resolve global calls to direct calls ----------===//
//
// Implements --ctjs-resolve-globals. A whole-program compile knows every call
// site, but the importer's IR does not say so. `function add(a, b) {...}` at
// the top level is
//
//     %6 = ctjs.create_closure %arg2[1] this %arg0
//     ctjs.store_global "add", %6
//
// and every `add(x, x)` anywhere in the program is
//
//     %f = ctjs.load_global "add"
//     %r = ctjs.call %f(%receiver, %x, %x)
//
// a name, a table lookup and a dispatch. No pass can type `add`'s parameters
// from that, because no pass can see who calls `add`. This one proves who
// does, per global name across the whole module, and records the answer: a
// ctjs.call_direct @add$1 at every call site, `private` on the function once
// every caller is one of those, and a `ctjs.globals` row per name saying why.
//
// A global name is resolved to its function when all six clauses below hold.
// Clauses 1-3 and 6 are checked per name by bound_closure(), clause 4 by
// own_closure_escapes(), and clause 5 for the whole module by
// dynamic_global_writes().
//
//  1. The name is stored exactly once in the program. Two stores mean the
//     binding changes at run time and a call site cannot know which closure
//     it reaches (`function f(){}; f = 1;`).
//  2. That store is of a closure, directly: the store's operand is a
//     ctjs.create_closure result, not a load, a call result or a block
//     argument. The closure's function index F is the callee.
//  3. The store is in the top level's hoisting prologue: the entry block of
//     function 0, preceded only by frame_enter, constants and other
//     closure/store pairs. A single store that runs later (`f(); f =
//     function(){}`, or a binding inside another function) leaves a window in
//     which the VM throws for an unbound name and a direct call would not.
//     Function declarations are hoisted into this prologue, so every call runs
//     after it.
//  4. The closure value has no other use than the store, and F's own callee
//     argument (%arg2, which `load_callee` reads) is used only to build nested
//     closures or read upvalues. Otherwise the closure escapes and the VM may
//     call F through a value this IR cannot see. This clause gates `private`,
//     not the rewrite: the rewrite is right whenever the callee is F, while
//     `private` claims every caller is visible.
//  5. Nothing writes the globals table dynamically. In the VM that table is
//     the current realm's `globals`, written by op::set_global (run_loop.cpp,
//     VM_CASE(set_global)), its AOT helper, and context::define_global
//     (script/vm/execution_inline.hpp).
//
//     `globalThis.add = 1` rebinds `add` in this engine: the window proxy's
//     "set" trap (lib/Shell/bindings/window/window.cpp) falls back to
//     `define_global(name, value)` for any key that is not an own property
//     of the window. Do not relax this clause on the grounds that the
//     engine is simpler than a browser.
//
//     So the walk follows the global object (a ctjs.load_global of `window`,
//     `globalThis` or `self`, and the top level's `this`) and the receivers of
//     the bodies calls enter. If such a value reaches a write under a key this
//     pass cannot name, a call this pass cannot name, or an operation the walk
//     does not know, no name in the module resolves. A named read off it does
//     not propagate unless the name is one of the host's self-aliases
//     (hands_back_the_global_object()), so `window.getComputedStyle(el)` is
//     harmless.
//
//     A call whose callee known_callee() can prove, or that binding_names
//     resolves (a local function binding, or a method of a frozen object
//     literal), continues the walk at the callee's parameter instead of
//     counting as an escape. As the receiver of a method read off itself, the
//     global object may meet a host method NativeRealm.h lists as inert
//     (`window.scrollTo(0, 0)`), unless the program rebinds that name. A
//     value stored in a binding is followed to every read of the binding,
//     including captured reads in other functions, and a returned value to
//     the result of every call of its function when binding_names sees all of
//     them. Every refusal is collected into `ctjs.global_escapes`. A write through the
//     global object under a constant key refuses only that name (as clause 6
//     does for a refused body's stores); a computed key, `__proto__`, a
//     delete, an accessor, set_proto and copy_props still refuse every name.
//
//     Calls whose callee is a ctjs.create_closure result need no global name
//     at all; runOnOperation() rewrites them separately (see "Second shape"
//     below). That is the only way a call inside a bundle becomes a
//     ctjs.call_direct, because bundles declare nothing at global scope.
//
//     `Function(...)` compiles and runs a new program
//     (builtins/objects/function.cpp, install_dynamic_function) whose body can
//     store any global, so a load of "Function" or "eval", or a
//     ctjs.dynamic_import, also resolves nothing.
//
//  6. No body the importer refused stores this name. A `gave_up` function
//     emits no ctjs.func, so its stores are invisible here. The importer reads
//     the names such a body stores from its bytecode name pool (op::set_global
//     names its target by static index) and lists them on the ctjs.skipped
//     row as `stores`. Only a body whose pools name `Function`, `eval`,
//     `constructor`, `globalThis` or `window`, or that does a dynamic import,
//     refuses the whole module (`opaque`). A run-time-computed `constructor`
//     key is invisible to both the summary and this pass - one stated hole.
//
// Clause 5 also follows `.constructor` reads, because every function's
// prototype exposes `Function` as `constructor`. It only marks reads whose
// receiver may be a function (may_be_function()), so `({}).constructor` is not
// marked, and hands_back_the_compiler() decides which named reads off a marked
// value can still be the compiler (`this.constructor.NAME` is a string).
//
// What the rewrite passes: ctjs.call_direct's operands are the callee's entry
// block in order: receiver, new.target, callee, parameters (BytecodeImport.cpp,
// `implicit_arguments`). The receiver is the call's own; the callee is the
// load_global result, kept because the boxed backend dispatches on it;
// new.target is a fresh `ctjs.constant #ctjs.undefined`, which is what the VM
// gives a plain call: VM_CASE(call) sets `entered.new_target =
// pending_new_target_`, which is undefined unless a pass_new_target or a
// construct set it (run_loop.cpp; context::invoke in vm/call/invoke.cpp does
// the same). A function containing ctjs.pass_new_target therefore has none of
// its calls rewritten: nothing checks that pass_new_target sits right before
// its call, so the pending flag could be live at any call in it.
//
// Arity: fewer arguments than parameters are padded with undefined, as
// VM_CASE(call) fills `registers_[new_base + i] = undefined` for i in
// [argc, param_count). More are not rewritten: the surplus lands in the
// callee's raw register window, where `arguments` and a rest parameter read
// it, and call_direct has no slot for a value the callee's block does not
// name. Native lowering diagnoses such a site.
//
// One program per module is assumed. A function index means nothing outside
// the program it was compiled in, and the index is recovered from the symbol
// the importer built (`name$N`, BytecodeImport.cpp). A module holding two
// programs would have two `$0`s and fail to verify before this pass ran.
//
// Written in C++ rather than DRR because this is a module-wide analysis (a
// symbol table, a store count per name, SSA reachability) and a DRR Pat<>
// matches one operation's shape. The op it produces (ctjs.call_direct,
// CTJSOps.td) and the pass declaration (Passes.td) are TableGen.
//
//===----------------------------------------------------------------------===//
#include "Globals/Effects.h"
#include "Globals/RegisterFlow.h"
#include "ctcompile/CTJS/IR/CTJSAttrs.h"
#include "ctcompile/CTJS/IR/CTJSDialect.h"
#include "ctcompile/CTJS/IR/CTJSOps.h"
#include "ctcompile/CTJS/Import/EngineGlobals.h"
#include "ctcompile/CTJS/Lowering/NativeRealm.h"
#include "ctcompile/CTJS/Lowering/WholePage.h"
#include "ctcompile/CTJS/Transforms/Passes.h"

#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Interfaces/ControlFlowInterfaces.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringSet.h"

#include <cassert>
#include <cstdint>
#include <optional>
#include <string>

namespace ctcompile::ctjs {

#define GEN_PASS_DEF_CTJSRESOLVEGLOBALS
#include "ctcompile/CTJS/Transforms/Passes.h.inc"

namespace {
using namespace globals_detail;

/// Every store and load of one global name in the module.
struct binding {
    llvm::SmallVector<StoreGlobalOp> stores;
    llvm::SmallVector<LoadGlobalOp> loads;
};

/// The module-wide census of global names. Clause 5's walk reads it too:
/// following the global object into a callee asks the same "which ctjs.func is
/// this?" question as the per-name proof, and keeping one copy of that proof
/// keeps the two consistent.
struct census {
    llvm::MapVector<mlir::StringAttr, binding> bindings;
    llvm::DenseMap<std::uint32_t, FuncOp> by_index;
    llvm::DenseSet<mlir::StringAttr> refused; ///< Clause 6, per name.
    std::optional<std::string> opaque_skip;   ///< A refused body no summary bounds.
    FuncOp top;                               ///< Function index 0, or null.
};

/// The answer bound_closure() gives for one name: the function it is bound to,
/// or the reason there is none.
struct verdict {
    FuncOp target;            ///< Null unless the name is bound to exactly one closure.
    CreateClosureOp closure;  ///< The ctjs.create_closure the store bound, for clause 4.
    std::string reason;       ///< Why not, when the target is null.
    bool hostBinding = false; ///< Never stored: a read is the realm's own value.
};

/// Checks clauses 1-3 and 6 for \p name: everything the census proves that
/// does not depend on clause 5's SSA walk.
///
/// Clause 5 is deliberately absent. It is a module-wide answer computed by the
/// walk, and the walk calls this to decide where a marked value goes next, so
/// asking for it here would be circular. That is sound: this says only which
/// function a name is bound to, and clause 5's own refusal still decides
/// whether any name resolves at all.
verdict bound_closure(const census & world, mlir::StringAttr name, const binding & facts) {
    const auto no = [](std::string because) {
        return verdict{FuncOp{}, CreateClosureOp{}, std::move(because)};
    };
    if (world.refused.contains(name)) {
        return no("a function the importer refused (ctjs.skipped) stores this name, so the "
                  "binding this pass can see is not the only one");
    }
    if (facts.stores.empty()) {
        return verdict{FuncOp{}, CreateClosureOp{}, "never stored in this program - a host binding",
                       true};
    }
    if (facts.stores.size() != 1) {
        return no("stored " + std::to_string(facts.stores.size()) + " times");
    }
    StoreGlobalOp store = facts.stores.front();
    auto made = store.getValue().getDefiningOp<CreateClosureOp>();
    if (!made) { return no("bound to something other than a closure"); }
    FuncOp top = world.top;
    if (!top) { return no("no top-level function (index 0) in the module"); }
    if (store->getParentOfType<FuncOp>() != top || store->getBlock() != &top.getBody().front() ||
        !in_prologue(store)) {
        return no("bound outside the top level's hoisting prologue, so a call may run "
                  "before the binding exists");
    }
    const auto index = static_cast<std::uint32_t>(made.getFunction());
    FuncOp target = world.by_index.lookup(index);
    if (!target) {
        return no("function " + std::to_string(index) +
                  " emitted no ctjs.func - refused by the importer (ctjs.skipped)");
    }
    if (target->hasAttr(notLoweredMarker)) {
        return no("function " + target.getSymName().str() + " was refused");
    }
    if (!implicitEntry(target)) {
        return no("function " + target.getSymName().str() + " has no body");
    }
    return verdict{target, made, {}};
}

/// The verdict on the name \p load reads, or nullopt without a census row.
std::optional<verdict> load_verdict(const census & world, LoadGlobalOp load) {
    const auto row = world.bindings.find(load.getNameAttr());
    if (row == world.bindings.end()) { return std::nullopt; }
    return bound_closure(world, load.getNameAttr(), row->second);
}

/// Returns the ctjs.func that calling \p callee provably enters, or null.
/// Two shapes are known:
///
///   * A ctjs.create_closure result. The closure names its function index
///     outright, and calling it enters that function. This is the IIFE, which
///     is what a UMD header is: `(function (global) { ... })(globalThis)`.
///   * A ctjs.load_global of a name bound_closure() resolves, by the same
///     proof the per-name census uses.
///
/// Everything else is unknown: a parameter, a property read
/// (`window.scrollTo`), a call result, a native. The body behind such a value
/// is not in this module and may store any global.
FuncOp known_callee(const census & world, mlir::Value callee) {
    if (auto made = callee.getDefiningOp<CreateClosureOp>()) {
        return world.by_index.lookup(static_cast<std::uint32_t>(made.getFunction()));
    }
    if (auto load = callee.getDefiningOp<LoadGlobalOp>()) {
        const std::optional<verdict> answer = load_verdict(world, load);
        return answer ? answer->target : FuncOp{};
    }
    return FuncOp{};
}

/// Whether calling \p callee enters an arrow function. The importer hands
/// ctjs.create_closure the enclosing receiver only when the target is an arrow,
/// and a `ctjs.constant` undefined otherwise (op::closure in Instructions.cpp),
/// so that operand is the one mark the IR carries. An arrow ignores the
/// receiver a call passes it.
bool enters_arrow(const census & world, mlir::Value callee) {
    CreateClosureOp made = callee.getDefiningOp<CreateClosureOp>();
    if (auto load = callee.getDefiningOp<LoadGlobalOp>()) {
        if (const std::optional<verdict> answer = load_verdict(world, load)) {
            made = answer->closure;
        }
    }
    return made && made.getEnclosingThis().getDefiningOp<ConstantOp>() == nullptr;
}

/// The operation of type `Op` that `value` always comes from (through block
/// arguments), or null.
template <class Op> Op originOp(mlir::Value value) {
    const mlir::Value origin = registerFlowOrigin(value);
    return origin ? origin.getDefiningOp<Op>() : Op{};
}

/// Names the function behind callees known_callee() does not know, for the
/// clause 5 walk only: a local function binding (`function f() {}` inside a
/// body, called as `f(x)` here or from a nested closure), and a method of a
/// frozen object literal (`P.on(x)` where `const P = { on(t) {...} }`).
///
/// A binding is a ctjs.create_cell. Every value it can hold is its initial, a
/// ctjs.cell_set of it, or a ctjs.store_upvalue in a function that captured it;
/// every read is a ctjs.cell_get or such a function's ctjs.load_upvalue. The
/// binding names a function when all of those values are one closure, apart
/// from an undefined initial (a call before the declaration runs throws). An
/// object literal is frozen when it is only built by its own define_own
/// stores, read with a key it owns, and held by bindings whose every read is
/// also only read with a key it owns: nothing can replace a method then, and
/// the object reaches no code that could. A key it does not own is looked up
/// on its prototype, where an inherited getter would get the literal as `this`
/// and could replace a method before the call the walk follows.
///
/// Used only where the walk follows a watched value into a call, so it widens
/// nothing else: callers this names do not get their receivers watched.
struct binding_names {
    mlir::ModuleOp module;
    const census & world;
    llvm::DenseMap<mlir::Operation *, llvm::SmallVector<CreateClosureOp, 1>> creators;
    llvm::DenseMap<mlir::Operation *, std::optional<llvm::SmallVector<mlir::Value>>> readsMemo;
    llvm::DenseMap<mlir::Operation *, mlir::Value> valueMemo;
    llvm::DenseMap<mlir::Operation *, bool> frozenMemo;

    explicit binding_names(mlir::ModuleOp in, const census & of) : module(in), world(of) {
        module.walk([&](CreateClosureOp made) {
            if (FuncOp target =
                    world.by_index.lookup(static_cast<std::uint32_t>(made.getFunction()))) {
                creators[target.getOperation()].push_back(made);
            }
        });
    }

    /// The binding cell a function's capture slot holds, or null.
    CreateCellOp slotCell(FuncOp function, std::int64_t slot, unsigned depth = 0) {
        const auto made = creators.lookup(function.getOperation());
        if (made.size() != 1 || slot < 0 || depth > 64) { return {}; }
        CreateClosureOp closure = made.front();
        if (static_cast<std::size_t>(slot) >= closure.getUpvalues().size()) { return {}; }
        if (const mlir::DenseI32ArrayAttr indices = closure.getEnclosingIndicesAttr()) {
            if (const std::int32_t from = indices[static_cast<std::size_t>(slot)]; from >= 0) {
                auto outer = closure->getParentOfType<FuncOp>();
                return outer ? slotCell(outer, from, depth + 1) : CreateCellOp{};
            }
        }
        return originOp<CreateCellOp>(closure.getUpvalues()[static_cast<std::size_t>(slot)]);
    }

    /// Collects every read and written value of `cell` through the function
    /// that captured it at `slot` (and the closures it hands the slot on to).
    /// False when something else touches the slot or a creator is ambiguous.
    bool slotAccess(CreateClosureOp made, std::size_t slot,
                    llvm::SmallVectorImpl<mlir::Value> & reads,
                    llvm::SmallVectorImpl<mlir::Value> & writes,
                    llvm::DenseSet<std::pair<mlir::Operation *, std::size_t>> & seen) {
        if (!seen.insert({made.getOperation(), slot}).second) { return true; }
        FuncOp target = world.by_index.lookup(static_cast<std::uint32_t>(made.getFunction()));
        if (!target || creators.lookup(target.getOperation()).size() != 1) { return false; }
        bool complete = true;
        target.getBody().walk([&](mlir::Operation * op) {
            if (auto read = mlir::dyn_cast<LoadUpvalueOp>(op);
                read && static_cast<std::size_t>(read.getIndex()) == slot) {
                reads.push_back(read.getResult());
            } else if (auto write = mlir::dyn_cast<StoreUpvalueOp>(op);
                       write && static_cast<std::size_t>(write.getIndex()) == slot) {
                writes.push_back(write.getValue());
            } else if (auto nested = mlir::dyn_cast<CreateClosureOp>(op)) {
                if (const mlir::DenseI32ArrayAttr indices = nested.getEnclosingIndicesAttr()) {
                    for (auto [at, from] : llvm::enumerate(indices.asArrayRef())) {
                        if (from == static_cast<std::int32_t>(slot)) {
                            complete &= slotAccess(nested, at, reads, writes, seen);
                        }
                    }
                }
            }
        });
        return complete;
    }

    /// Every value `cell` can be read as, or nullopt when its accesses are not
    /// all reads, writes and captures. `writes` receives what it can hold.
    std::optional<llvm::SmallVector<mlir::Value>> access(
        CreateCellOp cell, llvm::SmallVectorImpl<mlir::Value> & writes) {
        llvm::SmallVector<mlir::Value> reads;
        writes.push_back(cell.getInitial());
        auto uses = registerFlowUses(cell.getResult());
        if (!uses) { return std::nullopt; }
        llvm::DenseSet<std::pair<mlir::Operation *, std::size_t>> seen;
        for (mlir::OpOperand * use : *uses) {
            mlir::Operation * user = use->getOwner();
            if (auto read = mlir::dyn_cast<CellGetOp>(user)) {
                reads.push_back(read.getResult());
            } else if (auto write = mlir::dyn_cast<CellSetOp>(user);
                       write && use->getOperandNumber() == 0) {
                writes.push_back(write.getValue());
            } else if (auto made = mlir::dyn_cast<CreateClosureOp>(user)) {
                const std::size_t first = made->getNumOperands() - made.getUpvalues().size();
                if (use->getOperandNumber() < first ||
                    !slotAccess(made, use->getOperandNumber() - first, reads, writes, seen)) {
                    return std::nullopt;
                }
            } else if (!mlir::isa<RootOp>(user)) {
                return std::nullopt;
            }
        }
        return reads;
    }

    /// The one value `cell` holds once written (an undefined initial aside),
    /// or null.
    mlir::Value heldValue(CreateCellOp cell) {
        if (auto found = valueMemo.find(cell.getOperation()); found != valueMemo.end()) {
            return found->second;
        }
        valueMemo[cell.getOperation()] = {}; // A cycle answers "unknown".
        llvm::SmallVector<mlir::Value> writes;
        mlir::Value held;
        if (access(cell, writes)) {
            for (mlir::Value write : writes) {
                const mlir::Value origin = registerFlowOrigin(write);
                if (origin && isUndefinedConstant(origin)) { continue; }
                if (!origin || (held && held != origin)) {
                    held = {};
                    break;
                }
                held = origin;
            }
        }
        valueMemo[cell.getOperation()] = held;
        return held;
    }

    /// What `value` is, seen through register flow and binding reads.
    mlir::Value resolve(mlir::Value value, unsigned depth = 0) {
        value = registerFlowOrigin(value);
        if (!value || depth > 64) { return {}; }
        if (auto read = value.getDefiningOp<CellGetOp>()) {
            auto cell = originOp<CreateCellOp>(read.getCell());
            const mlir::Value held = cell ? heldValue(cell) : mlir::Value{};
            return held ? resolve(held, depth + 1) : mlir::Value{};
        }
        if (auto read = value.getDefiningOp<LoadUpvalueOp>()) {
            auto function = read->getParentOfType<FuncOp>();
            CreateCellOp cell = function ? slotCell(function, read.getIndex()) : CreateCellOp{};
            const mlir::Value held = cell ? heldValue(cell) : mlir::Value{};
            return held ? resolve(held, depth + 1) : mlir::Value{};
        }
        return value;
    }

    /// The method `object` defines under `key` with its own define_own
    /// store, or null.
    FuncOp fieldMethod(CreateObjectOp object, llvm::StringRef key) {
        auto uses = registerFlowUses(object.getResult());
        if (!uses || key.empty()) { return {}; }
        for (mlir::OpOperand * use : *uses) {
            auto set = mlir::dyn_cast<SetPropertyOp>(use->getOwner());
            if (set && use->getOperandNumber() == 0 && set->hasAttr(defineOwnMarker) &&
                constantKey(set.getKey()) == key) {
                const mlir::Value made = resolve(set.getValue());
                auto closure = made ? made.getDefiningOp<CreateClosureOp>() : CreateClosureOp{};
                return closure ? world.by_index.lookup(
                                     static_cast<std::uint32_t>(closure.getFunction()))
                               : FuncOp{};
            }
        }
        return {};
    }

    /// Whether `use` reads one of the keys `own` lists off the value it uses.
    static bool ownRead(mlir::OpOperand * use, const llvm::StringSet<> & own) {
        auto read = mlir::dyn_cast<GetPropertyOp>(use->getOwner());
        return read && use->getOperandNumber() == 0 && own.contains(constantKey(read.getKey()));
    }

    /// Whether `use` passes `object` as the receiver of a call of one of its
    /// own methods, read off that same value, whose body only reads keys
    /// `object` owns off `this`: such a call cannot replace a method either.
    bool receiverOfOwnMethod(mlir::OpOperand * use, CreateObjectOp object,
                             const llvm::StringSet<> & own) {
        auto call = mlir::dyn_cast<CallOp>(use->getOwner());
        if (!call || use->getOperandNumber() != 1) { return false; }
        auto read = originOp<GetPropertyOp>(call.getCallee());
        const mlir::Value receiver = registerFlowOrigin(use->get());
        if (!read || !receiver || registerFlowOrigin(read.getObject()) != receiver) {
            return false;
        }
        FuncOp method = fieldMethod(object, constantKey(read.getKey()));
        if (!method || !implicitEntry(method)) { return false; }
        auto uses = registerFlowUses(method.getBody().front().getArgument(0));
        return uses && llvm::all_of(*uses, [&](mlir::OpOperand * self) {
                   return ownRead(self, own) || mlir::isa<RootOp>(self->getOwner());
               });
    }

    /// Whether every use of `value`, a read of a binding holding `object`,
    /// reads a key it owns or calls one of its methods.
    bool onlyRead(mlir::Value value, CreateObjectOp object, const llvm::StringSet<> & own) {
        auto uses = registerFlowUses(value);
        return uses && llvm::all_of(*uses, [&](mlir::OpOperand * use) {
                   return ownRead(use, own) || mlir::isa<RootOp>(use->getOwner()) ||
                          receiverOfOwnMethod(use, object, own);
               });
    }

    /// Whether `object` is a frozen literal (see the struct comment).
    bool frozen(CreateObjectOp object) {
        if (auto found = frozenMemo.find(object.getOperation()); found != frozenMemo.end()) {
            return found->second;
        }
        bool answer = true;
        auto uses = registerFlowUses(object.getResult());
        if (!uses) { answer = false; }
        const llvm::SmallVector<mlir::OpOperand *> none;
        const auto defines = [](mlir::OpOperand * use) {
            auto set = mlir::dyn_cast<SetPropertyOp>(use->getOwner());
            return set && use->getOperandNumber() == 0 && set->hasAttr(defineOwnMarker);
        };
        // The keys it owns: each define_own store's constant key, once.
        llvm::StringSet<> keys;
        for (mlir::OpOperand * use : uses ? *uses : none) {
            if (defines(use)) {
                const llvm::StringRef key = constantKey(propertyKey(use->getOwner()));
                answer &= !key.empty() && key != "__proto__" && keys.insert(key).second;
            }
        }
        for (mlir::OpOperand * use : uses && answer ? *uses : none) {
            mlir::Operation * user = use->getOwner();
            if (ownRead(use, keys) || defines(use) ||
                (mlir::isa<RootOp>(user) && use->getOperandNumber() == 0) ||
                receiverOfOwnMethod(use, object, keys)) {
                continue;
            }
            CreateCellOp cell = mlir::dyn_cast<CreateCellOp>(user);
            if (auto write = mlir::dyn_cast<CellSetOp>(user);
                write && use->getOperandNumber() == 1) {
                cell = originOp<CreateCellOp>(write.getCell());
            }
            llvm::SmallVector<mlir::Value> writes;
            const auto reads = cell ? access(cell, writes) : std::nullopt;
            if (reads && heldValue(cell) == object.getResult() &&
                llvm::all_of(*reads,
                             [&](mlir::Value read) { return onlyRead(read, object, keys); })) {
                continue;
            }
            answer = false;
            break;
        }
        frozenMemo[object.getOperation()] = answer;
        return answer;
    }

    /// Collects in `results` the result of every call through `value`, a
    /// value of `fn`'s closure, following it into the bindings that hold it.
    /// Returns the operation that uses it any other way, or null.
    mlir::Operation * calledOnly(mlir::Value value, FuncOp fn,
                                 llvm::SmallVectorImpl<mlir::Value> & results,
                                 llvm::DenseSet<mlir::Value> & seen) {
        if (!seen.insert(value).second) { return nullptr; }
        auto uses = registerFlowUses(value);
        if (!uses) { return value.getDefiningOp() ? value.getDefiningOp() : fn.getOperation(); }
        for (mlir::OpOperand * use : *uses) {
            mlir::Operation * user = use->getOwner();
            if (auto call = mlir::dyn_cast<CallOp>(user); call && use->getOperandNumber() == 0) {
                results.push_back(call.getResult());
                continue;
            }
            if (auto direct = mlir::dyn_cast<CallDirectOp>(user);
                direct && use->getOperandNumber() == 2 && direct.getCallee() == fn.getSymName()) {
                continue; // Its result is collected with the symbol uses.
            }
            if (mlir::isa<RootOp>(user)) { continue; }
            CreateCellOp cell = mlir::dyn_cast<CreateCellOp>(user);
            if (auto write = mlir::dyn_cast<CellSetOp>(user);
                write && use->getOperandNumber() == 1) {
                cell = originOp<CreateCellOp>(write.getCell());
            }
            llvm::SmallVector<mlir::Value> writes;
            const auto reads = cell ? access(cell, writes) : std::nullopt;
            if (!reads) { return user; }
            for (mlir::Value read : *reads) {
                if (mlir::Operation * other = calledOnly(read, fn, results, seen)) { return other; }
            }
        }
        return nullptr;
    }

    /// The results of every call of `fn`, or why some caller is out of sight.
    std::optional<std::string> callResults(FuncOp fn,
                                           llvm::SmallVectorImpl<mlir::Value> & results) {
        if (functionIndex(fn) == 0u) { return std::string{"the script hands it to the host"}; }
        if (auto uses = mlir::SymbolTable::getSymbolUses(fn.getOperation(), module)) {
            for (const mlir::SymbolTable::SymbolUse & use : *uses) {
                auto direct = mlir::dyn_cast<CallDirectOp>(use.getUser());
                if (!direct || direct.getCallee() != fn.getSymName()) {
                    return "`" + fn.getSymName().str() + "` is named by " + describe(use.getUser());
                }
                results.push_back(direct.getResult());
            }
        }
        llvm::DenseSet<mlir::Value> seen;
        for (CreateClosureOp made : creators.lookup(fn.getOperation())) {
            if (mlir::Operation * other = calledOnly(made.getResult(), fn, results, seen)) {
                return "`" + fn.getSymName().str() + "` is also used as a value by " +
                       describe(other);
            }
        }
        return std::nullopt;
    }

    /// The function calling `callee` enters, or null.
    FuncOp callee(mlir::Value value) {
        const mlir::Value resolved = resolve(value);
        if (!resolved) { return {}; }
        if (auto made = resolved.getDefiningOp<CreateClosureOp>()) {
            return world.by_index.lookup(static_cast<std::uint32_t>(made.getFunction()));
        }
        auto method = resolved.getDefiningOp<GetPropertyOp>();
        const llvm::StringRef key = method ? constantKey(method.getKey()) : llvm::StringRef{};
        if (key.empty()) { return {}; }
        const mlir::Value holder = resolve(method.getObject());
        auto object = holder ? holder.getDefiningOp<CreateObjectOp>() : CreateObjectOp{};
        if (!object || !frozen(object)) { return {}; }
        return fieldMethod(object, key);
    }
};

/// The `typeof` reads of global names that run before anything could bind a
/// global: the flow-sensitive rule for absent names, which holds even when
/// clause 5 refuses the module.
///
/// The script entry starts in that state. An operation keeps it when it can
/// neither run code nor write the globals table (inert()): constants, closure
/// and literal construction, binding reads and writes, global loads, stores
/// of a global by name (they bind only that name), `typeof`, `!`, ToBoolean,
/// a comparison that cannot call valueOf, and branches. Anything else, every
/// call included, ends it. A block keeps it when every predecessor ends in it
/// (a greatest fixpoint, so a loop through an unsafe operation loses it). A
/// call made in that state enters a function whose closure is created once
/// and called only there, the IIFE a UMD wrapper is, so its body starts in the
/// state too; the call itself then ends it.
struct global_prefix {
    mlir::ModuleOp module;
    const census & world;
    llvm::DenseMap<mlir::Operation *, llvm::SmallVector<CreateClosureOp, 1>> creators;
    llvm::DenseSet<mlir::Operation *> reads;
    llvm::DenseSet<mlir::Operation *> entered;

    global_prefix(mlir::ModuleOp in, const census & of) : module(in), world(of) {
        module.walk([&](CreateClosureOp made) {
            if (FuncOp target =
                    world.by_index.lookup(static_cast<std::uint32_t>(made.getFunction()))) {
                creators[target.getOperation()].push_back(made);
            }
        });
    }

    /// Whether `value` is a constant or a `typeof` result: a primitive, so a
    /// loose comparison with it calls no valueOf.
    static bool primitive(mlir::Value value) {
        const mlir::Value origin = registerFlowOrigin(value);
        if (!origin) { return false; }
        if (origin.getDefiningOp<ConstantOp>()) { return true; }
        auto unary = origin.getDefiningOp<UnaryOp>();
        return unary && unary.getKind() == UnaryKind::TypeOf;
    }

    static bool inert(mlir::Operation * op) {
        if (op->getNumRegions() != 0) { return false; }
        if (mlir::isa<mlir::BranchOpInterface>(op)) { return true; }
        if (mlir::isa<ConstantOp, CreateClosureOp, CreateCellOp, CellGetOp, CellSetOp,
                      LoadUpvalueOp, StoreUpvalueOp, LoadGlobalOp, StoreGlobalOp, TruthyOp,
                      FrameEnterOp, FrameExitOp, RootOp, CreateObjectOp, CreateArrayOp>(op)) {
            return true;
        }
        if (auto unary = mlir::dyn_cast<UnaryOp>(op)) {
            return unary.getKind() == UnaryKind::TypeOf || unary.getKind() == UnaryKind::Not;
        }
        if (auto compare = mlir::dyn_cast<CompareOp>(op)) {
            return compare.getKind() == CompareKind::StrictEq ||
                   (primitive(compare.getLhs()) && primitive(compare.getRhs()));
        }
        // A literal's own field, defined on the object being built.
        if (auto set = mlir::dyn_cast<SetPropertyOp>(op)) {
            return set->hasAttr(defineOwnMarker) && originOp<CreateObjectOp>(set.getObject());
        }
        return false;
    }

    /// The function `call` enters when it is the only way that function runs,
    /// or null.
    FuncOp soleCallee(mlir::Operation * call) {
        CreateClosureOp made;
        FuncOp target;
        if (auto plain = mlir::dyn_cast<CallOp>(call)) {
            made = originOp<CreateClosureOp>(plain.getCallee());
        } else if (auto direct = mlir::dyn_cast<CallDirectOp>(call)) {
            target =
                mlir::SymbolTable::lookupNearestSymbolFrom<FuncOp>(direct, direct.getCalleeAttr());
            made = originOp<CreateClosureOp>(direct.getOperand(arg_callee));
        }
        if (made) {
            target = world.by_index.lookup(static_cast<std::uint32_t>(made.getFunction()));
        }
        if (!made || !target || !implicitEntry(target) ||
            creators.lookup(target.getOperation()).size() != 1) {
            return {};
        }
        auto uses = registerFlowUses(made.getResult());
        if (!uses || !llvm::all_of(*uses, [&](mlir::OpOperand * use) {
                return use->getOwner() == call || mlir::isa<RootOp>(use->getOwner());
            })) {
            return {};
        }
        if (auto symbols = mlir::SymbolTable::getSymbolUses(target.getOperation(), module)) {
            for (const mlir::SymbolTable::SymbolUse & use : *symbols) {
                if (use.getUser() != call) { return {}; }
            }
        }
        return target;
    }

    /// Records the prefix reads of `fn`, entered in the clean state.
    void enter(FuncOp fn) {
        if (!entered.insert(fn.getOperation()).second || fn.getBody().empty()) { return; }
        mlir::Region & body = fn.getBody();
        llvm::DenseMap<mlir::Block *, bool> clean;
        for (mlir::Block & block : body) { clean[&block] = true; }
        const auto endsClean = [&](mlir::Block * block) {
            return clean.lookup(block) &&
                   llvm::all_of(*block, [](mlir::Operation & op) { return inert(&op); });
        };
        for (bool changed = true; changed;) {
            changed = false;
            for (mlir::Block & block : body) {
                if (&block == &body.front() || !clean.lookup(&block)) { continue; }
                bool stays = !block.hasNoPredecessors();
                for (mlir::Block * pred : block.getPredecessors()) { stays &= endsClean(pred); }
                if (!stays) {
                    clean[&block] = false;
                    changed = true;
                }
            }
        }
        for (mlir::Block & block : body) {
            if (!clean.lookup(&block)) { continue; }
            for (mlir::Operation & op : block) {
                if (auto load = mlir::dyn_cast<LoadGlobalOp>(op); load && load.getTypeofLookup()) {
                    reads.insert(&op);
                }
                if (mlir::isa<CallOp, CallDirectOp>(op)) {
                    if (FuncOp target = soleCallee(&op)) { enter(target); }
                }
                if (!inert(&op)) { break; }
            }
        }
    }
};

/// Whether padding a short call would be visible to the callee's `arguments`.
///
/// The rewrite fills a call that passes fewer arguments than the callee
/// declares with `ctjs.constant #ctjs.undefined`, which is exactly what
/// VM_CASE(call) does to the callee's registers (run_loop.cpp:
/// `registers_[new_base + i] = undefined` for i in [argc, param_count)). It is
/// not what op::call does to the argument window: make_arguments_object copies
/// the raw window, whose length is argc, so `function f(a, b) { return
/// arguments.length; } f(1)` is 1 in the VM. A padded ctjs.call_direct would
/// make it 2, because the boxed backend hands ct_aot_call `getArgs()` as that
/// window.
///
/// At exact arity there is no pad, which is why this asks about the padding
/// rather than refusing every callee that mentions `arguments`.
bool padding_hides_arguments(FuncOp target, std::size_t supplied, unsigned parameters) {
    return supplied < parameters && readsRawArguments(target);
}

/// Returns why \p call cannot become a ctjs.call_direct into \p target, or
/// nullopt if it can.
///
/// Arity is a hard verifier requirement: CallDirectOp::verifySymbolUses holds
/// the operand count equal to the entry block's, so a surplus call is refused;
/// a short one is padded, as VM_CASE(call) pads, unless padding_hides_arguments()
/// says the pad would be visible. A function holding a ctjs.pass_new_target
/// may have the pending new.target live at any call in it, so its calls are
/// refused too.
std::optional<std::string> direct_refusal(
    CallOp call, unsigned parameters, FuncOp target,
    const llvm::DenseSet<mlir::Operation *> & passes_new_target) {
    const std::size_t supplied = call.getArgs().size();
    const auto counts = [&] {
        return "a call passes " + std::to_string(supplied) + " argument(s) to " +
               std::to_string(parameters) + " parameter(s)";
    };
    if (supplied > parameters) {
        return counts() + " - the surplus has frame semantics (" + describe(call) + ")";
    }
    if (passes_new_target.contains(call->getParentOfType<FuncOp>())) {
        return "a call sits in a function that passes new.target (" + describe(call) + ")";
    }
    if (padding_hides_arguments(target, supplied, parameters)) {
        return counts() +
               " and the callee reads its raw argument window, which the pad would lengthen (" +
               describe(call) + ")";
    }
    return std::nullopt;
}

/// Replaces \p call with a ctjs.call_direct into \p target, padding a short
/// argument list with undefined.
void rewrite_direct(CallOp call, FuncOp target, unsigned parameters) {
    mlir::OpBuilder at(call);
    mlir::MLIRContext * context = call.getContext();
    const auto value_type = ValueType::get(context);
    const mlir::Value undefined =
        ConstantOp::create(at, call.getLoc(), value_type, UndefinedAttr::get(context));
    llvm::SmallVector<mlir::Value> arguments(call.getArgs());
    while (arguments.size() < parameters) { arguments.push_back(undefined); }
    auto direct = CallDirectOp::create(at, call.getLoc(), value_type,
                                       mlir::FlatSymbolRefAttr::get(target.getSymNameAttr()),
                                       call.getReceiver(), undefined, call.getCallee(), arguments,
                                       /*arg_attrs=*/nullptr, /*res_attrs=*/nullptr);
    call.getResult().replaceAllUsesWith(direct.getResult());
    call.erase();
}

/// Whether every object, the global object included, inherits a property
/// named \p key from Object.prototype, so that reading it never answers
/// undefined for want of an own property.
bool inheritedFromObject(llvm::StringRef key) {
    return llvm::is_contained({"constructor", "hasOwnProperty", "isPrototypeOf",
                               "propertyIsEnumerable", "toString", "toLocaleString", "valueOf",
                               "__proto__", "__defineGetter__", "__defineSetter__",
                               "__lookupGetter__", "__lookupSetter__"},
                              key);
}

/// How a watched value of kind \p kind is described in a refusal reason.
std::string name_of(watched kind) {
    switch (kind) {
    case watched::global_object: return "the global object";
    case watched::receiver: return "a receiver that may be the global object";
    case watched::compiler: break;
    }
    return "a value that may be the run-time compiler";
}

/// How a literal or array made here that may hold a watched value of kind
/// \p kind is described in a refusal reason.
std::string holder_name(watched kind) {
    return "an object that may hold " + name_of(kind);
}

/// Whether \p key names an array element: a canonical non-negative integer.
bool array_index(llvm::StringRef key) {
    return !key.empty() && llvm::all_of(key, llvm::isDigit) && (key.size() == 1 || key[0] != '0');
}

/// A named write through a watched value: where it is, and through which kind.
struct bound_write {
    std::string at;
    watched through;
};

/// Clause 5: can anything in the module write the globals table other than a
/// ctjs.store_global this pass can count? Returns the reason if so, and fills
/// \p bound_through with names written through the global object under a
/// constant key.
///
/// The walk follows the global object into a call whose callee known_callee()
/// proves, continuing at the matching parameter, except when the call passes
/// more arguments than parameters or the callee reads its raw `arguments`
/// window (neither has a block argument to follow). An unknown callee is an
/// escape.
///
/// Termination: entering a callee marks a block argument, and mark() enqueues
/// a value only the first time it is seen, so a recursive chain marks each
/// parameter once. The module has finitely many values and the walk visits
/// each at most once.
///
/// A named write through the global object binds only that name: the proxy's
/// "set" trap either stores an own property of the window or calls
/// define_global(name, value). A computed key, `__proto__`, a delete, an
/// accessor definition, ctjs.set_proto and ctjs.copy_props still refuse every
/// name.
/// Whether the computed key \p key can never be the string `Map`: every value
/// that reaches it is a constant other than that string, through block
/// arguments and through a cell of this function that only constants are
/// written to and that no closure captures (Popper's `A[E]`, where E is
/// "clientHeight" or "scrollHeight").
bool never_names_map(mlir::Value key) {
    llvm::SmallVector<mlir::Value> work{key};
    llvm::DenseSet<mlir::Value> seen;
    while (!work.empty()) {
        const mlir::Value value = work.pop_back_val();
        if (!seen.insert(value).second) { continue; }
        if (seen.size() > 32) { return false; }
        if (auto constant = value.getDefiningOp<ConstantOp>()) {
            auto text = mlir::dyn_cast<StringAttr>(constant.getValue());
            if (text && text.getValue() == "Map") { return false; }
            continue;
        }
        if (auto read = value.getDefiningOp<CellGetOp>()) {
            auto cell = read.getCell().getDefiningOp<CreateCellOp>();
            if (!cell) { return false; }
            work.push_back(cell.getInitial());
            for (mlir::Operation * user : cell.getResult().getUsers()) {
                if (mlir::isa<CellGetOp>(user)) { continue; }
                auto write = mlir::dyn_cast<CellSetOp>(user);
                if (!write || write.getCell() != cell.getResult()) { return false; }
                work.push_back(write.getValue());
            }
            continue;
        }
        auto argument = mlir::dyn_cast<mlir::BlockArgument>(value);
        mlir::Block * block = argument ? argument.getOwner() : nullptr;
        if (!block || block->isEntryBlock()) { return false; }
        for (auto it = block->pred_begin(), end = block->pred_end(); it != end; ++it) {
            auto branch = mlir::dyn_cast<mlir::BranchOpInterface>((*it)->getTerminator());
            if (!branch) { return false; }
            const mlir::Value incoming =
                branch.getSuccessorOperands(it.getSuccessorIndex())[argument.getArgNumber()];
            if (!incoming) { return false; }
            work.push_back(incoming);
        }
    }
    return true;
}

std::optional<std::string> dynamic_global_writes(
    mlir::ModuleOp module, const census & world,
    llvm::MapVector<mlir::StringAttr, bound_write> & bound_through,
    llvm::SmallVectorImpl<std::string> & every, llvm::SmallVectorImpl<std::string> & map_reads) {
    std::optional<std::string> reason;
    // Computed once: it is a question about the whole module, and
    // may_be_function() is asked once per `.constructor` read.
    const bool super_is_open = prototype_replaced(module);
    // Asked once per call of a method read off a value that may be the
    // compiler (`this.constructor.eventName(t)`) or the global object.
    const prototype_writes writes = read_prototype_writes(module);
    llvm::DenseMap<mlir::Value, watched> marked;
    llvm::SmallVector<mlir::Value> work;
    llvm::DenseMap<mlir::Operation *, bool> raw_arguments;
    const auto mark = [&](mlir::Value value, watched kind) {
        if (marked.try_emplace(value, kind).second) { work.push_back(value); }
    };
    // A literal or array made here that a watched value was stored into, at
    // any depth, and every value that may be it (through branches, bindings,
    // calls, returns and reads out of another such object): not the watched
    // value, so writing into it writes no global, but a read out of it may be
    // that value. `any_key` is set when the value went in under a computed key;
    // otherwise only a key the program writes, or an element index, can hold it.
    struct holding {
        watched kind;
        bool any_key;
    };
    llvm::DenseMap<mlir::Value, holding> held;
    llvm::SmallVector<mlir::Value> held_work;
    const auto hold = [&](mlir::Value value, watched kind, bool any_key) {
        auto [row, added] = held.try_emplace(value, holding{kind, any_key});
        if (added) {
            held_work.push_back(value);
        } else if (any_key && !row->second.any_key) {
            row->second.any_key = true;
            held_work.push_back(value);
        }
    };
    // The literal or array made here that `object` always is, or null.
    const auto made_here = [](mlir::Value object) {
        const mlir::Value origin = registerFlowOrigin(object);
        return origin &&
                       mlir::isa_and_nonnull<CreateObjectOp, CreateArrayOp>(origin.getDefiningOp())
                   ? origin
                   : mlir::Value{};
    };
    const auto reads_arguments = [&](FuncOp target) {
        const auto row = raw_arguments.try_emplace(target.getOperation(), false);
        if (row.second) { row.first->second = readsRawArguments(target); }
        return row.first->second;
    };

    // A body the importer refused is invisible here, so its stores cannot be
    // counted. Only an `opaque` summary (see clause 6) refuses the whole
    // module; other refused bodies refuse just the names they store.
    if (world.opaque_skip) {
        every.push_back(*world.opaque_skip);
        return world.opaque_skip;
    }

    // The global object needs no name to be `this`. A classic script's top
    // level runs with it as the receiver (run.cpp selects the realm's global
    // value for every classic script, strict or not), so `function f() {
    // return 1; } this.f = function () { return 2; }; f();` rebinds `f`;
    // resolving it would answer 1 where the VM answers 2.
    if (FuncOp top = world.top; top && implicitEntry(top)) {
        mark(top.getBody().front().getArgument(0), watched::global_object);
    }
    // A body no operation here creates is created inside one the importer
    // refused, whose `this` the summary already takes for the global object:
    // an arrow there inherits it, and a function there is called from code
    // this pass cannot see.
    llvm::DenseSet<std::uint32_t> created;
    module.walk([&](CreateClosureOp made) {
        created.insert(static_cast<std::uint32_t>(made.getFunction()));
    });
    for (const auto & [index, entered] : world.by_index) {
        if (FuncOp function = entered;
            index != 0 && !created.contains(index) && implicitEntry(function)) {
            mark(function.getBody().front().getArgument(0), watched::receiver);
        }
    }

    module.walk([&](mlir::Operation * op) {
        if (reason) { return mlir::WalkResult::interrupt(); }
        // A call this pass can name passes its callee an undefined receiver,
        // which OrdinaryCallBindThis turns into the global object in a sloppy
        // body (the VM's load_this and the AOT helper ct_aot_this both do).
        // The IR does not record strictness, so every such callee's `this` is
        // watched; an arrow keeps the `this` it was created with.
        //
        // A caller this pass cannot name (a host timer or animation-frame
        // callback, Array.prototype.forEach, `f.call(x)`) is not followed:
        // those entries are not operations in this module. Watching every
        // receiver instead would refuse any function that returns or passes
        // on `this`, which loses most resolvable names.
        if (auto call = mlir::dyn_cast<CallOp>(op)) {
            FuncOp target = known_callee(world, call.getCallee());
            if (target && implicitEntry(target) && !enters_arrow(world, call.getCallee())) {
                mark(target.getBody().front().getArgument(0), watched::receiver);
            }
        }
        if (auto direct = mlir::dyn_cast<CallDirectOp>(op)) {
            auto target =
                mlir::SymbolTable::lookupNearestSymbolFrom<FuncOp>(direct, direct.getCalleeAttr());
            if (target && implicitEntry(target)) {
                mark(target.getBody().front().getArgument(0), watched::receiver);
            }
        }
        if (auto load = mlir::dyn_cast<LoadGlobalOp>(op)) {
            if (names_global_object(load.getName())) {
                mark(load.getResult(), watched::global_object);
            }
            if (names_eval(load.getName())) {
                reason = "a run-time compiler is reachable (" + describe(op) +
                         ") and the program it builds can store any global";
            }
        }
        // `Function` is not only a global name. It sits on every function's
        // prototype as `.constructor`, so `(function(){}).constructor` is the
        // compiler reached through a property read. Following the value
        // rather than refusing the key keeps `o.constructor === C` and
        // `o.constructor.name` resolvable: those end at a comparison, and only
        // a call or an escape refuses.
        //
        // Only a receiver that may be a function is marked: `({}).constructor`
        // is `Object`, not the compiler. may_be_function() decides.
        if (auto get = mlir::dyn_cast<GetPropertyOp>(op)) {
            if (constantKey(get.getKey()) == "constructor" &&
                (super_is_open || may_be_function(get.getObject()))) {
                mark(get.getResult(), watched::compiler);
            }
        }
        if (mlir::isa<DynamicImportOp>(op)) {
            reason = "a dynamic import (" + describe(op) +
                     ") loads code this module cannot see, and it can store any global";
        }
        return mlir::WalkResult::advance();
    });
    if (reason) {
        every.push_back(*reason);
        return reason;
    }

    // Enters a known callee at one argument position, or returns why not. The
    // position is already in the callee's entry-block order: receiver,
    // new.target, callee, then the declared parameters.
    // With `facts`, the value entered is an object that may hold one (see
    // `held`), and the parameter is followed as one.
    const auto enter = [&](FuncOp target, std::size_t supplied, unsigned parameter,
                           mlir::Operation * at, watched kind,
                           const holding * facts = nullptr) -> std::optional<std::string> {
        const std::string what = facts ? holder_name(kind) : name_of(kind);
        if (!target || !implicitEntry(target)) {
            return what + " escapes into a call whose callee this module does not name (" +
                   describe(at) + ")";
        }
        mlir::Block & entry = target.getBody().front();
        const unsigned parameters = parameterCount(entry);
        if (supplied > parameters) {
            return what + " is passed to " + target.getSymName().str() + " beyond its " +
                   std::to_string(parameters) +
                   " parameter(s), where the surplus keeps frame semantics (" + describe(at) + ")";
        }
        if (reads_arguments(target)) {
            return what + " is passed to " + target.getSymName().str() +
                   ", which reads its raw argument window (" + describe(at) + ")";
        }
        // In range by the arity check above: a receiver is 0, a callee value
        // is 2, and argument i of at most `parameters` is 3 + i.
        assert(parameter < entry.getNumArguments() && "a call operand with no block argument");
        if (facts) {
            hold(entry.getArgument(parameter), kind, facts->any_key);
        } else {
            mark(entry.getArgument(parameter), kind);
        }
        return std::nullopt;
    };

    // Every refusal is collected, not only the first, so that a module with
    // several reports them all (`ctjs.global_escapes`); the first is the
    // verdict. A value that escapes is not followed further.
    const auto escape = [&](std::string why) { every.push_back(std::move(why)); };
    llvm::SmallVector<std::pair<mlir::StringAttr, mlir::Operation *>> inert;
    // Calls that may convert the global object to a string, as an argument
    // of a method argument_inert_native admits.
    llvm::SmallVector<mlir::Operation *> converted;
    binding_names names(module, world);

    // Follows one object that may hold a watched value (see `held`). Writing
    // into it, comparing it strictly and testing it are harmless; a read out
    // of it may be the value, or another such object; storing it into a
    // literal or array made here makes that one a holder too; and a call this
    // module can name is entered. Converting it to a primitive runs its own
    // toString or valueOf with it as `this`, so that is harmless only while
    // no object has one the program gave it. Anything else is an escape.
    const auto follow_holder = [&](mlir::Value value) {
        const holding facts = held.find(value)->second;
        const std::string what = holder_name(facts.kind);
        const bool converts_safely =
            !writes.defined.contains("toString") && !writes.defined.contains("valueOf");
        const auto conversion = [&](mlir::Operation * op) {
            if (!converts_safely) {
                escape(what + " is converted by " + describe(op) +
                       ", which may run a toString or valueOf the program gave it");
            }
        };
        for (mlir::OpOperand & use : value.getUses()) {
            mlir::Operation * op = use.getOwner();
            const unsigned at = use.getOperandNumber();
            if (auto branch = mlir::dyn_cast<mlir::BranchOpInterface>(op)) {
                if (const std::optional<mlir::BlockArgument> argument =
                        branch.getSuccessorBlockArgument(at)) {
                    hold(*argument, facts.kind, facts.any_key);
                }
                continue;
            }
            if (mlir::isa<TruthyOp, InstanceOfOp, HasPropertyOp, RootOp, GetProtoOp,
                          DeletePropertyOp, DeleteNamedOp>(op)) {
                continue;
            }
            if (auto compare = mlir::dyn_cast<CompareOp>(op)) {
                if (compare.getKind() != CompareKind::StrictEq) { conversion(op); }
                continue;
            }
            if (auto unary = mlir::dyn_cast<UnaryOp>(op)) {
                if (unary.getKind() != UnaryKind::Not && unary.getKind() != UnaryKind::TypeOf &&
                    unary.getKind() != UnaryKind::Void) {
                    conversion(op);
                }
                continue;
            }
            if (mlir::isa<BinaryOp, BinaryStaticOp>(op)) {
                conversion(op);
                continue;
            }
            if (auto convert = mlir::dyn_cast<ConvertOp>(op)) {
                if (convert.getKind() == ConvertKind::ToObject) {
                    hold(convert.getResult(), facts.kind, facts.any_key);
                } else if (convert.getKind() != ConvertKind::ToBoolean) {
                    conversion(op);
                }
                continue;
            }
            if (auto get = mlir::dyn_cast<GetPropertyOp>(op)) {
                if (at == 1) {
                    conversion(op);
                    continue;
                }
                // Only a key some store wrote, an element, or any key after a
                // computed store can hold the value or another holder.
                const llvm::StringRef key = constantKey(get.getKey());
                if (facts.any_key || key.empty() || array_index(key) ||
                    writes.defined.contains(key)) {
                    mark(get.getResult(), facts.kind);
                    hold(get.getResult(), facts.kind, true);
                }
                continue;
            }
            if (auto set = mlir::dyn_cast<SetPropertyOp>(op)) {
                if (at == 0) { continue; }
                if (at == 1) {
                    conversion(op);
                    continue;
                }
                if (const mlir::Value literal = made_here(set.getObject())) {
                    hold(literal, facts.kind, constantKey(set.getKey()).empty());
                    continue;
                }
                escape(what + " escapes into " + describe(op));
                continue;
            }
            if (auto append = mlir::dyn_cast<AppendOp>(op)) {
                if (at == 0) { continue; }
                if (const mlir::Value array = made_here(append.getArray())) {
                    hold(array, facts.kind, false);
                    continue;
                }
                escape(what + " escapes into " + describe(op));
                continue;
            }
            if (auto array = mlir::dyn_cast<CreateArrayOp>(op)) {
                hold(array.getResult(), facts.kind, false);
                continue;
            }
            if (auto copy = mlir::dyn_cast<CopyPropsOp>(op)) {
                if (at == 0) { continue; }
                if (const mlir::Value literal = made_here(copy.getTarget())) {
                    hold(literal, facts.kind, true);
                    continue;
                }
                escape(what + " escapes into " + describe(op));
                continue;
            }
            if (mlir::isa<IterableOp>(op)) {
                hold(op->getResult(0), facts.kind, facts.any_key);
                continue;
            }
            if (auto made = mlir::dyn_cast<CreateClosureOp>(op); made && at == 1) {
                FuncOp arrow =
                    world.by_index.lookup(static_cast<std::uint32_t>(made.getFunction()));
                if (!arrow || !implicitEntry(arrow)) {
                    escape(what + " is captured by an arrow whose body was not imported (" +
                           describe(op) + ")");
                    continue;
                }
                hold(arrow.getBody().front().getArgument(0), facts.kind, facts.any_key);
                continue;
            }
            if (auto call = mlir::dyn_cast<CallOp>(op)) {
                if (at == 0) {
                    escape(what + " escapes into " + describe(op));
                    continue;
                }
                const unsigned parameter = at == 1 ? 0u : at + 1u;
                FuncOp target = known_callee(world, call.getCallee());
                if (!target) { target = names.callee(call.getCallee()); }
                if (const std::optional<std::string> refusal =
                        enter(target, call.getArgs().size(), parameter, op, facts.kind, &facts)) {
                    escape(*refusal);
                }
                continue;
            }
            if (auto direct = mlir::dyn_cast<CallDirectOp>(op)) {
                if (const std::optional<std::string> refusal =
                        enter(mlir::SymbolTable::lookupNearestSymbolFrom<FuncOp>(
                                  direct, direct.getCalleeAttr()),
                              direct.getArgs().size(), at, op, facts.kind, &facts)) {
                    escape(*refusal);
                }
                continue;
            }
            CreateCellOp cell;
            if (mlir::isa<CreateCellOp>(op)) {
                cell = mlir::cast<CreateCellOp>(op);
            } else if (auto write = mlir::dyn_cast<CellSetOp>(op); write && at == 1) {
                cell = originOp<CreateCellOp>(write.getCell());
            } else if (auto write = mlir::dyn_cast<StoreUpvalueOp>(op); write && at == 1) {
                auto function = op->getParentOfType<FuncOp>();
                cell = function ? names.slotCell(function, write.getIndex()) : CreateCellOp{};
            }
            if (cell || mlir::isa<CreateCellOp, CellSetOp, StoreUpvalueOp>(op)) {
                llvm::SmallVector<mlir::Value> stored;
                const auto reads = cell ? names.access(cell, stored) : std::nullopt;
                if (!reads) {
                    escape(what + " escapes into " + describe(op) +
                           " - a binding this walk cannot follow");
                    continue;
                }
                for (mlir::Value read : *reads) { hold(read, facts.kind, facts.any_key); }
                continue;
            }
            if (mlir::isa<ReturnOp>(op)) {
                FuncOp function = op->getParentOfType<FuncOp>();
                llvm::SmallVector<mlir::Value> results;
                if (const auto why = function ? names.callResults(function, results)
                                              : std::optional<std::string>{"no function"}) {
                    escape(what + " escapes into " + describe(op) + " - " + *why);
                    continue;
                }
                for (mlir::Value result : results) { hold(result, facts.kind, facts.any_key); }
                continue;
            }
            escape(what + " escapes into " + describe(op));
        }
    };

    // Forward SSA reachability from the watched values. A read through one
    // may yield another value that may be it (`window.window`), so reads
    // propagate; predicates and arithmetic answer primitives and stop; a
    // branch forwards into the successor's block argument; a call this module
    // can name forwards into the callee's parameter; a named write through it
    // binds that one name; storing it into a literal or array made here
    // follows that object (follow_holder); and anything else is the answer.
    while (!work.empty() || !held_work.empty()) {
        if (!held_work.empty()) {
            follow_holder(held_work.pop_back_val());
            continue;
        }
        const mlir::Value value = work.pop_back_val();
        const watched kind = marked.find(value)->second;
        for (mlir::OpOperand & use : value.getUses()) {
            mlir::Operation * op = use.getOwner();
            if (auto branch = mlir::dyn_cast<mlir::BranchOpInterface>(op)) {
                if (const std::optional<mlir::BlockArgument> argument =
                        branch.getSuccessorBlockArgument(use.getOperandNumber())) {
                    mark(*argument, kind);
                }
                continue;
            }
            if (mlir::isa<TruthyOp, CompareOp, InstanceOfOp, HasPropertyOp, UnaryOp, BinaryOp,
                          BinaryStaticOp, RootOp>(op)) {
                continue; // A primitive comes out, never the object.
            }
            if (auto get = mlir::dyn_cast<GetPropertyOp>(op)) {
                // A named read off a watched value is not that value, unless
                // it is one of the few names that hand it back. `Function.NAME`
                // is undefined and `window.getComputedStyle` is a function.
                //
                // That applies only when the watched value is the receiver. As
                // the key it is converted to a string, and the property that
                // comes out belongs to another object; it is marked
                // conservatively.
                const llvm::StringRef key = constantKey(get.getKey());
                // A read that may yield the realm's `Map` hands its prototype
                // to whatever reads `.prototype` next, which this walk does
                // not follow; the native Map proof (Analysis/NativeMap.cpp)
                // refuses it.
                if (kind != watched::compiler && use.getOperandNumber() == 0 &&
                    (key == "Map" || (key.empty() && !never_names_map(get.getKey())))) {
                    map_reads.push_back(describe(op));
                }
                const bool hands_back = kind == watched::compiler
                                            ? hands_back_the_compiler(key)
                                            : hands_back_the_global_object(key);
                if (use.getOperandNumber() == 0 && !hands_back) { continue; }
                mark(get.getResult(), kind);
                continue;
            }
            if (mlir::isa<GetProtoOp, IterableOp, ConvertOp, CellGetOp, LoadUpvalueOp>(op)) {
                for (const mlir::Value result : op->getResults()) { mark(result, kind); }
                continue;
            }
            if (auto set = mlir::dyn_cast<SetPropertyOp>(op)) {
                // As the value stored into a literal made here, it is followed
                // through that literal. As the key, or the value stored into
                // somebody else's object, it is not being written through: it
                // is converted to a string, or it leaves this walk's sight.
                if (const mlir::Value literal = made_here(set.getObject());
                    literal && use.getOperandNumber() == 2) {
                    hold(literal, kind, constantKey(set.getKey()).empty());
                    continue;
                }
                if (use.getOperandNumber() != 0) {
                    escape(name_of(kind) + " escapes into " + describe(op));
                    continue;
                }
                const llvm::StringRef key = constantKey(set.getKey());
                if (kind != watched::compiler && !key.empty() && key != "__proto__") {
                    bound_through.insert(
                        {mlir::StringAttr::get(module.getContext(), key), {describe(op), kind}});
                    continue;
                }
                escape(name_of(kind) + " is written through (" + describe(op) + ")");
                continue;
            }
            if (mlir::isa<DeletePropertyOp, DeleteNamedOp, DefineAccessorOp, SetProtoOp,
                          CopyPropsOp>(op)) {
                escape(name_of(kind) + " is written through (" + describe(op) + ")");
                continue;
            }
            // An element of an array made here, followed through that array.
            if (auto append = mlir::dyn_cast<AppendOp>(op); append && use.getOperandNumber() == 1) {
                if (const mlir::Value array = made_here(append.getArray())) {
                    hold(array, kind, false);
                    continue;
                }
            }
            if (auto array = mlir::dyn_cast<CreateArrayOp>(op)) {
                hold(array.getResult(), kind, false);
                continue;
            }
            // An arrow's lexical `this`. The VM reads $enclosing_this only for
            // an arrow (make_closure's captured_this), and the arrow reads it
            // back as its own receiver argument.
            if (auto made = mlir::dyn_cast<CreateClosureOp>(op);
                made && use.getOperandNumber() == 1) {
                FuncOp arrow =
                    world.by_index.lookup(static_cast<std::uint32_t>(made.getFunction()));
                if (!arrow || !implicitEntry(arrow)) {
                    escape(name_of(kind) +
                           " is captured by an arrow whose body was not imported (" + describe(op) +
                           ")");
                    continue;
                }
                mark(arrow.getBody().front().getArgument(0), kind);
                continue;
            }
            if (auto call = mlir::dyn_cast<CallOp>(op)) {
                // The watched value as the callee itself (`globalThis(...)`)
                // names no body, and this is also where `seed.constructor(src)`
                // is refused.
                if (use.getOperandNumber() == 0) {
                    escape(name_of(kind) + " escapes into " + describe(op));
                    continue;
                }
                // The receiver of a host method read off the same value
                // (`window.getComputedStyle(el)`): the global object, or
                // something that is not it, in which case `this` is not the
                // global object either. A method on the inert list leaves the
                // globals table alone, and so does one the window does not
                // have (`node.getBoundingClientRect()` on a node that may be
                // the window), whose read answers nothing callable, unless the
                // program has rebound that name (checked once the walk ends,
                // against every binding).
                if (use.getOperandNumber() == 1) {
                    auto read = originOp<GetPropertyOp>(call.getCallee());
                    const llvm::StringRef key = read ? constantKey(read.getKey()) : "";
                    const mlir::Value receiver = registerFlowOrigin(call.getReceiver());
                    const bool readOffReceiver =
                        read && (read.getObject() == call.getReceiver() ||
                                 (receiver && registerFlowOrigin(read.getObject()) == receiver));
                    if (kind != watched::compiler && readOffReceiver &&
                        (nativeHostMethodKeepsGlobals(key) || global_object_lacks(writes, key))) {
                        inert.push_back({mlir::StringAttr::get(module.getContext(), key), op});
                        continue;
                    }
                    // A method read off the same value that may be the
                    // compiler, as Bootstrap calls `this.constructor.eventName(t)`.
                    // If the value is `Function`, compiler_lacks proves the read
                    // answers undefined, so the call throws before it runs any
                    // code; if it is not, it is not the compiler. Either way it
                    // reaches no code as the compiler.
                    if (kind == watched::compiler && readOffReceiver &&
                        compiler_lacks(writes, key)) {
                        continue;
                    }
                }
                // An argument of a DOM method that only compares it or converts
                // it to a string (`parent.contains(child)` with `child` maybe
                // the window): argument_inert_native shows the method is the
                // realm's or nothing callable. The conversion uses the realm's
                // `toString` and `valueOf` unless the program binds a global of
                // either name (checked once the walk ends).
                if (use.getOperandNumber() >= 2 && kind != watched::compiler) {
                    auto read = originOp<GetPropertyOp>(call.getCallee());
                    if (read && argument_inert_native(writes, constantKey(read.getKey()))) {
                        converted.push_back(op);
                        continue;
                    }
                }
                // ctjs.call is (callee, receiver, args...) and the entry block
                // is (receiver, new.target, callee, parameters...) - the same
                // remapping ctjs.call_direct is built from.
                const unsigned parameter =
                    use.getOperandNumber() == 1 ? 0u : use.getOperandNumber() + 1u;
                FuncOp target = known_callee(world, call.getCallee());
                if (!target) { target = names.callee(call.getCallee()); }
                if (const std::optional<std::string> refusal =
                        enter(target, call.getArgs().size(), parameter, op, kind)) {
                    escape(*refusal);
                }
                continue;
            }
            if (auto direct = mlir::dyn_cast<CallDirectOp>(op)) {
                // An earlier run of this pass already named this call, and its
                // operands are the entry block in order, so the position needs
                // no remapping at all.
                if (const std::optional<std::string> refusal =
                        enter(mlir::SymbolTable::lookupNearestSymbolFrom<FuncOp>(
                                  direct, direct.getCalleeAttr()),
                              direct.getArgs().size(), use.getOperandNumber(), op, kind)) {
                    escape(*refusal);
                }
                continue;
            }
            // Held in a binding: every read of the binding, in this frame or
            // in a closure that captured it, may be the value. Writing
            // something else into it too only adds values; the reads stay
            // watched.
            CreateCellOp holder;
            if (mlir::isa<CreateCellOp>(op)) {
                holder = mlir::cast<CreateCellOp>(op);
            } else if (auto write = mlir::dyn_cast<CellSetOp>(op);
                       write && use.getOperandNumber() == 1) {
                holder = originOp<CreateCellOp>(write.getCell());
            } else if (auto write = mlir::dyn_cast<StoreUpvalueOp>(op);
                       write && use.getOperandNumber() == 1) {
                auto function = op->getParentOfType<FuncOp>();
                holder = function ? names.slotCell(function, write.getIndex()) : CreateCellOp{};
            }
            if (holder || mlir::isa<CreateCellOp, CellSetOp, StoreUpvalueOp>(op)) {
                llvm::SmallVector<mlir::Value> writes;
                const auto reads = holder ? names.access(holder, writes) : std::nullopt;
                if (!reads) {
                    escape(name_of(kind) + " escapes into " + describe(op) +
                           " - a binding this walk cannot follow");
                    continue;
                }
                for (mlir::Value read : *reads) { mark(read, kind); }
                continue;
            }
            // Returned: the result of every call of the function may be the
            // value, when every caller is in sight.
            if (mlir::isa<ReturnOp>(op)) {
                FuncOp function = op->getParentOfType<FuncOp>();
                llvm::SmallVector<mlir::Value> results;
                if (const auto why = function ? names.callResults(function, results)
                                              : std::optional<std::string>{"no function"}) {
                    escape(name_of(kind) + " escapes into " + describe(op) + " - " + *why);
                    continue;
                }
                for (mlir::Value result : results) { mark(result, kind); }
                continue;
            }
            // Stored, captured, returned, thrown, constructed with, spread, or
            // an operation this walk does not know: the value is out of sight
            // and anything may write through it - or compile through it - from
            // here on.
            escape(name_of(kind) + " escapes into " + describe(op));
        }
    }
    for (const auto & [key, at] : inert) {
        const auto row = world.bindings.find(key);
        if (bound_through.contains(key) ||
            (row != world.bindings.end() && !row->second.stores.empty())) {
            escape("the global object is the receiver of `" + key.str() +
                   "`, which this program rebinds (" + describe(at) + ")");
        }
    }
    for (mlir::Operation * at : converted) {
        for (llvm::StringRef name : {"toString", "valueOf"}) {
            const auto key = mlir::StringAttr::get(module.getContext(), name);
            const auto row = world.bindings.find(key);
            if (bound_through.contains(key) ||
                (row != world.bindings.end() && !row->second.stores.empty())) {
                escape("the global object may be converted to a string (" + describe(at) +
                       "), and this program binds `" + name.str() + "`");
                break;
            }
        }
    }
    if (every.empty()) { return std::nullopt; }
    return every.front();
}

/// Clause 4's second half: \p function's own closure, as its body sees it,
/// feeds nothing but nested closures and upvalue reads. Returns the escape
/// otherwise.
std::optional<std::string> own_closure_escapes(FuncOp function) {
    if (!implicitEntry(function)) { return "the function has no callee argument"; }
    if (mlir::Operation * op = ownClosureEscape(function)) {
        return "the function's own closure escapes into " + describe(op);
    }
    return std::nullopt;
}

struct CTJSResolveGlobalsPass : impl::CTJSResolveGlobalsBase<CTJSResolveGlobalsPass> {
    using CTJSResolveGlobalsBase::CTJSResolveGlobalsBase;

    void runOnOperation() override {
        mlir::ModuleOp module = getOperation();
        mlir::MLIRContext * context = &getContext();
        mlir::Builder builder(context);

        // Local counters for the --report remark. The LLVM release this builds
        // against compiles pass statistics out, so a Statistic has no value to
        // print; these are the only counts that exist in a release build.
        std::size_t resolved_here = 0;
        std::size_t rewritten_here = 0;
        std::size_t closed_here = 0;
        // Calls rewritten by the second shape, counted apart so a test can pin
        // them. `rewritten_here` is both shapes together
        // because tools/check/native-claims.py holds it equal to the count of
        // ctjs.call_direct in the IR, and that invariant is what stops a pass
        // reporting a rewrite it did not make.
        std::size_t closure_here = 0;

        // The census, in first-seen order so the attribute is stable.
        census world;
        llvm::DenseSet<mlir::Operation *> passes_new_target;
        module.walk([&](mlir::Operation * op) {
            if (auto store = mlir::dyn_cast<StoreGlobalOp>(op)) {
                world.bindings[store.getNameAttr()].stores.push_back(store);
            } else if (auto load = mlir::dyn_cast<LoadGlobalOp>(op)) {
                world.bindings[load.getNameAttr()].loads.push_back(load);
            } else if (auto function = mlir::dyn_cast<FuncOp>(op)) {
                if (const std::optional<unsigned> index = functionIndex(function)) {
                    world.by_index.try_emplace(*index, function);
                }
            } else if (mlir::isa<PassNewTargetOp>(op)) {
                passes_new_target.insert(op->getParentOfType<FuncOp>());
            }
        });
        skipped_summary skipped = read_skipped(module);
        world.refused = std::move(skipped.stores);
        world.opaque_skip = std::move(skipped.opaque);
        world.top = world.by_index.lookup(0);

        // Clause 5. `bound_through` collects the names a `globalThis.NAME = v`
        // binds; each is refused like a second ctjs.store_global would refuse
        // it, and refuses nothing else.
        llvm::MapVector<mlir::StringAttr, bound_write> bound_through;
        llvm::SmallVector<std::string> escapes;
        llvm::SmallVector<std::string> map_reads;
        const std::optional<std::string> dynamic =
            dynamic_global_writes(module, world, bound_through, escapes, map_reads);
        // The reads off the global object that may yield `Map`, for the
        // native Map proof.
        if (map_reads.empty()) {
            module->removeAttr("ctjs.global_map_reads");
        } else {
            module->setAttr(
                "ctjs.global_map_reads",
                builder.getStrArrayAttr(llvm::to_vector_of<llvm::StringRef>(map_reads)));
        }
        // Every clause 5 refusal, for a reader deciding what to widen next;
        // the rows below carry only the first.
        if (escapes.empty()) {
            module->removeAttr("ctjs.global_escapes");
        } else {
            module->setAttr("ctjs.global_escapes",
                            builder.getStrArrayAttr(llvm::to_vector_of<llvm::StringRef>(escapes)));
        }
        // A name bound only that way has neither a load nor a store, so it has
        // no census row yet; add one so its refusal is reported.
        // Skipped when the module is refused outright: those names would carry
        // the module-wide reason, which says nothing about them.
        //
        // Only names written through the global object itself get a row. A
        // receiver is almost always
        // an instance, and `this.x = 1` binds the global `x` only in the world
        // where it is not; a name no load or store in the program mentions has
        // nothing to resolve either way, so a row for every field a constructor
        // writes would be noise.
        if (!dynamic) {
            for (const auto & written : bound_through) {
                if (written.second.through == watched::global_object) {
                    (void)world.bindings[written.first];
                }
            }
        }

        // Absent names. When nothing writes the globals table dynamically, a
        // name the program never stores, that no refused body stores, that is
        // not written through the global object and that the native realm does
        // not provide (NativeRealm.h) does not exist at run time. `typeof` of
        // it is "undefined", so each such read becomes that constant, which
        // lets precomputation drop the branches a feature test guards (a UMD
        // wrapper's AMD and CommonJS arms). A plain read throws a
        // ReferenceError in the VM; it is left alone, and native admission
        // refuses it.
        //
        // When clause 5 refuses the module, the same holds for a read that runs
        // before anything could bind a global (global_prefix): a UMD wrapper's
        // `typeof define` at the top of its body. Its row says which rule
        // folded it.
        //
        // Only a whole page folds (WholePage.h): on a module that is only part
        // of one, a later script can bind the name this one never stores.
        const bool whole_page = wholePage(module);
        const auto named = pageNamedElements(module);
        module.walk([&](LoadGlobalOp load) { load->removeAttr(kAbsentGlobalTypeof); });
        llvm::DenseMap<mlir::StringAttr, std::size_t> folded;
        global_prefix prefix(module, world);
        if ((dynamic || !named.vmComplete) && world.top) { prefix.enter(world.top); }
        // A name the script declares with `var` is a binding of the realm even
        // when nothing in this program stores it: the declaration keeps a
        // value an earlier script gave the name. The importer lists those
        // declarations (ctjs.hoisted_vars), and none of them is absent.
        llvm::DenseSet<mlir::StringAttr> declared;
        if (auto list = module->getAttrOfType<mlir::ArrayAttr>("ctjs.hoisted_vars")) {
            for (mlir::Attribute entry : list) {
                if (auto name = mlir::dyn_cast<mlir::StringAttr>(entry)) { declared.insert(name); }
            }
        }
        {
            for (auto & [name, facts] : world.bindings) {
                if (!whole_page || !facts.stores.empty() || world.refused.contains(name) ||
                    declared.contains(name) || (!dynamic && bound_through.contains(name)) ||
                    nativeRealmProvides(name.getValue()) || named.contains(name.getValue())) {
                    continue;
                }
                llvm::erase_if(facts.loads, [&](LoadGlobalOp load) {
                    if (!load.getTypeofLookup() ||
                        (dynamic && !prefix.reads.contains(load.getOperation()))) {
                        return false;
                    }
                    if (!named.complete ||
                        (!named.vmComplete && !prefix.reads.contains(load.getOperation()))) {
                        load->setAttr(kAbsentGlobalTypeof, builder.getUnitAttr());
                        return false;
                    }
                    mlir::OpBuilder at(load);
                    auto undefined = ConstantOp::create(at, load.getLoc(), load.getType(),
                                                        UndefinedAttr::get(context));
                    load.getResult().replaceAllUsesWith(undefined.getResult());
                    load.erase();
                    ++folded[name];
                    return true;
                });
            }
        }

        // The same names read as properties of the global object. With
        // nothing writing the globals table dynamically, `window.jQuery` for a
        // name the program never binds is an own property no one created,
        // so it is undefined, and a library's probe for an optional global
        // (`window.jQuery ? window.jQuery : null`) folds like `typeof jQuery`
        // does. Unlike the bare name it does not throw, so every read folds,
        // not only `typeof`. The holder must be the global object itself (a
        // load of `window`, `self` or `globalThis` the program never binds),
        // and a name that the global object inherits from Object.prototype is
        // never absent. The fold also takes the page to have no element named
        // like the property: HTML's named access on the Window object (7.3.3),
        // which the shell's window implements, makes `window.jQuery` an
        // element whose id or name is "jQuery".
        //
        // When clause 5 refused the module, each such read is marked
        // kAbsentGlobalProperty instead, for the native lowering to fold. That
        // needs the walk that collects the writes through the global object to
        // have run to the end: a load of `Function` or `eval`, a dynamic import
        // or a refused body that may store any global stops it first, and a
        // name written through the global object would then look unbound.
        // A mark already on the input is no verdict of this run.
        //
        // The fold takes the page to have no element of that id or name
        // (HTML's named access on the Window object): only a whole page, whose
        // driver recorded the page's elements, supports that, and a name the
        // page does name an element by is kept.
        llvm::MapVector<mlir::StringAttr, std::size_t> folded_through;
        bool walked = whole_page && !world.opaque_skip;
        module.walk([&](mlir::Operation * op) {
            op->removeAttr(kAbsentGlobalProperty);
            auto load = mlir::dyn_cast<LoadGlobalOp>(op);
            walked &= !(load && names_eval(load.getName())) && !mlir::isa<DynamicImportOp>(op);
        });
        if (!dynamic || walked) {
            const auto bound = [&](mlir::StringAttr name) {
                const auto facts = world.bindings.find(name);
                return (facts != world.bindings.end() && !facts->second.stores.empty()) ||
                       world.refused.contains(name) || declared.contains(name) ||
                       bound_through.contains(name);
            };
            llvm::SmallVector<GetPropertyOp> reads;
            module.walk([&](GetPropertyOp get) {
                auto holder = get.getObject().getDefiningOp<LoadGlobalOp>();
                const llvm::StringRef key = constantKey(get.getKey());
                if (!whole_page || !holder || !names_global_object(holder.getName()) ||
                    bound(holder.getNameAttr()) || key.empty() || named.contains(key) ||
                    inheritedFromObject(key) || nativeRealmProvides(key) ||
                    bound(mlir::StringAttr::get(context, key))) {
                    return;
                }
                reads.push_back(get);
            });
            for (GetPropertyOp get : reads) {
                if (dynamic || !named.complete || !named.vmComplete) {
                    get->setAttr(kAbsentGlobalProperty, builder.getUnitAttr());
                    continue;
                }
                mlir::OpBuilder at(get);
                auto undefined = ConstantOp::create(at, get.getLoc(), get.getType(),
                                                    UndefinedAttr::get(context));
                ++folded_through[mlir::StringAttr::get(context, constantKey(get.getKey()))];
                get.getResult().replaceAllUsesWith(undefined.getResult());
                auto holder = get.getObject().getDefiningOp<LoadGlobalOp>();
                mlir::Operation * key = get.getKey().getDefiningOp();
                get.erase();
                if (key && mlir::isa<ConstantOp>(key) && key->use_empty()) { key->erase(); }
                // A load of the global object that only held the folded read
                // goes too, so the census below does not count it as a use.
                if (holder.getResult().use_empty()) {
                    llvm::erase(world.bindings[holder.getNameAttr()].loads, holder);
                    holder.erase();
                }
            }
        }

        llvm::SmallVector<mlir::Attribute> rows;
        const auto record = [&](mlir::StringAttr name, std::size_t stores, FuncOp resolved,
                                const std::string & reason, bool hostBinding = false) {
            llvm::SmallVector<mlir::NamedAttribute, 5> fields{
                builder.getNamedAttr("name", name),
                builder.getNamedAttr("stores",
                                     builder.getI32IntegerAttr(static_cast<std::int32_t>(stores))),
                builder.getNamedAttr("resolved",
                                     resolved ? mlir::Attribute{mlir::FlatSymbolRefAttr::get(
                                                    resolved.getSymNameAttr())}
                                              : mlir::Attribute{builder.getStringAttr("none")}),
                builder.getNamedAttr("reason", builder.getStringAttr(reason)),
            };
            if (hostBinding) {
                fields.push_back(builder.getNamedAttr(kGlobalsHostBinding, builder.getUnitAttr()));
            }
            rows.push_back(builder.getDictionaryAttr(fields));
        };

        for (auto & [name, facts] : world.bindings) {
            // Clause 5 first: is the whole module refused?
            if (dynamic) {
                const auto absent = folded.find(name);
                record(name, facts.stores.size(), FuncOp{},
                       absent == folded.end()
                           ? *dynamic
                           : "absent before any global can be bound: never stored and not "
                             "provided by the native realm; " +
                                 std::to_string(absent->second) +
                                 " typeof read(s) in the script's prefix folded to undefined");
                continue;
            }
            // A name written as `globalThis.NAME = v` has a second binding this
            // pass cannot count, so only that name is refused.
            if (const auto written = bound_through.find(name); written != bound_through.end()) {
                record(name, facts.stores.size(), FuncOp{},
                       "written through " + name_of(written->second.through) + " (" +
                           written->second.at + "), which binds this name");
                continue;
            }
            if (const auto absent = folded.find(name); absent != folded.end()) {
                record(name, facts.stores.size(), FuncOp{},
                       "absent: never stored and not provided by the native realm; " +
                           std::to_string(absent->second) + " typeof read(s) folded to undefined");
                continue;
            }
            // A VM class helper's IR name (EngineGlobals.h) is never resolved:
            // its calls reach the engine-internal helper whatever a script
            // stores under that name.
            if (!engineHelperVMName(name.getValue()).empty()) {
                record(name, facts.stores.size(), FuncOp{},
                       "an engine helper, which no store of the script reaches");
                continue;
            }
            // Clauses 1-3 and 6, which known_callee() also asks.
            const verdict answer = bound_closure(world, name, facts);
            if (!answer.target) {
                record(name, facts.stores.size(), FuncOp{}, answer.reason, answer.hostBinding);
                continue;
            }
            FuncOp target = answer.target;
            ++resolvedGlobals;
            ++resolved_here;
            const unsigned parameters = parameterCount(target.getBody().front());

            // The rewrite, per load.
            std::optional<std::string> open;
            CreateClosureOp closure = answer.closure;
            if (!closure.getResult().hasOneUse()) {
                open = "the closure value has a use other than the store";
            }
            std::size_t rewritten = 0;
            // Listener registrations that pass this function's value; see the
            // use loop below.
            llvm::SmallVector<CallOp> listeners;
            for (LoadGlobalOp load : facts.loads) {
                // Importer checks retain the complete register file. Follow
                // both register vectors to discover all escaping uses, then
                // prove all incoming definitions before naming a forwarded
                // call. A
                // closure sharing a join with another value is not that value.
                auto uses = registerFlowUses(load.getResult());
                if (!uses) {
                    if (!open) { open = "the binding's register-flow work budget was exhausted"; }
                    continue;
                }
                llvm::SmallVector<CallOp> calls;
                for (mlir::OpOperand * use : *uses) {
                    mlir::Operation * user = use->getOwner();
                    // Already resolved by an earlier run: the callee value of a
                    // call_direct naming this very function.
                    if (auto direct = mlir::dyn_cast<CallDirectOp>(user);
                        direct && use->getOperandNumber() == 2 &&
                        direct.getCallee() == target.getSymName()) {
                        continue;
                    }
                    auto call = mlir::dyn_cast<CallOp>(user);
                    // The listener argument of addEventListener or
                    // removeEventListener, read straight from the binding:
                    // the host keeps the function, but only to call it or to
                    // compare it, and the native lowering gives the one
                    // declaration one identity (HostCallbacks.cpp), so the
                    // world stays closed. Marked below once it is.
                    if (call && use->getOperandNumber() == 3 && use->get() == load.getResult() &&
                        (methodKey(call) == "addEventListener" ||
                         methodKey(call) == "removeEventListener")) {
                        listeners.push_back(call);
                        continue;
                    }
                    if (!call || use->getOperandNumber() != 0) {
                        if (!open) { open = "the binding is used by " + describe(user); }
                        continue;
                    }
                    if (!registerFlowHasOrigin(call.getCallee(), load.getResult())) {
                        if (!open) {
                            open = "the callee register has another or unproved incoming value (" +
                                   describe(user) + ")";
                        }
                        continue;
                    }
                    if (auto why = direct_refusal(call, parameters, target, passes_new_target)) {
                        if (!open) { open = std::move(why); }
                        continue;
                    }
                    calls.push_back(call);
                }
                // Do not erase a call while its other operand uses are still
                // in the census above (f(f) is an escape as well as a call).
                for (CallOp call : calls) {
                    rewrite_direct(call, target, parameters);
                    ++rewritten;
                    ++rewrittenCalls;
                    ++rewritten_here;
                }
            }

            // Clause 4: is the world closed around the target?
            if (!open) { open = own_closure_escapes(target); }
            if (open) {
                record(name, facts.stores.size(), target,
                       "open: " + *open + "; " + std::to_string(rewritten) + " call(s) rewritten");
                continue;
            }
            mlir::SymbolTable::setSymbolVisibility(target, mlir::SymbolTable::Visibility::Private);
            for (CallOp call : listeners) {
                call->setAttr("ctjs.host_listener",
                              mlir::FlatSymbolRefAttr::get(target.getSymNameAttr()));
            }
            ++closedFunctions;
            ++closed_here;
            record(name, facts.stores.size(), target,
                   "closed: " + std::to_string(rewritten) + " call(s) rewritten");
        }

        for (const auto & [name, reads] : folded_through) {
            if (world.bindings.contains(name)) { continue; } // its row is above
            record(name, 0, FuncOp{},
                   "absent: never stored and not provided by the native realm; " +
                       std::to_string(reads) + " read(s) through the global object folded to " +
                       "undefined");
        }

        // Second shape: a ctjs.call whose callee is a ctjs.create_closure
        // result enters that closure's function, with no global name involved
        // (known_callee() proves it). This depends on no clause of the per-name
        // census, so it runs even when clause 5 refused every name above. It
        // is the only shape bundles contain, since they bind no globals.
        // --ctnative-lower-to-emitc's closure lift names some of the same
        // calls later; doing it here makes the rewrite visible to
        // tools/check/native-claims.py.
        llvm::SmallVector<CallOp> closure_calls;
        module.walk([&](CallOp call) {
            if (call.getCallee().getDefiningOp<CreateClosureOp>() != nullptr) {
                closure_calls.push_back(call);
            }
        });
        for (CallOp call : closure_calls) {
            FuncOp target = known_callee(world, call.getCallee());
            if (!target || target->hasAttr(notLoweredMarker) || !implicitEntry(target)) {
                continue;
            }
            const unsigned parameters = parameterCount(target.getBody().front());
            if (direct_refusal(call, parameters, target, passes_new_target)) { continue; }
            rewrite_direct(call, target, parameters);
            ++rewrittenCalls;
            ++rewritten_here;
            ++closure_here;
        }

        module->setAttr("ctjs.globals", builder.getArrayAttr(rows));

        // Report the counts as a remark, since pass statistics are compiled
        // out of the release LLVM build (--ctjs-lift-to-scf does the same).
        // Tests pin these numbers.
        if (report) {
            module->emitRemark() << "resolved " << resolved_here << " global(s), rewrote "
                                 << rewritten_here << " call(s), closed " << closed_here
                                 << " function(s) over " << rows.size() << " name(s), named "
                                 << closure_here << " closure call(s)";
        }
    }
};

} // namespace

} // namespace ctcompile::ctjs
