// THE CLOSED WORLD - Phase 62½-A.
//
// A whole-program compile knows every call site, and the importer's IR says
// nothing of the kind. `function add(a, b) {...}` at the top level is
//
//     %6 = ctjs.create_closure %arg2[1] this %arg0
//     ctjs.store_global "add", %6
//
// and every `add(x, x)` anywhere in the program is
//
//     %f = ctjs.load_global "add"
//     %r = ctjs.call %f(%receiver, %x, %x)
//
// - a name, a table lookup, and a dispatch. No pass can type `add`'s
// parameters from that, because no pass can see who calls `add`. This one
// proves who does, per global NAME across the whole module, and writes the
// answer where MLIR reads it: a ctjs.call_direct @add$1 at every call site,
// and `private` on the function once every caller is one of those.
//
// WHAT IS PROVED, AND WHY EACH CLAUSE IS THERE.
//
//  1. THE NAME IS STORED EXACTLY ONCE IN THE PROGRAM. Two stores mean the
//     binding changes at run time and a call site cannot know which closure
//     it reaches (`function f(){}; f = 1;` - the negative case in the test).
//  2. THAT STORE IS OF A CLOSURE, DIRECTLY. The store's SSA operand is a
//     ctjs.create_closure result - not a load, not a call result, not a
//     block argument. The closure's function index F is the callee.
//  3. THE STORE IS IN THE TOP LEVEL'S HOISTING PROLOGUE: in the entry block of
//     the function with index 0, preceded only by frame_enter, constants and
//     other closure/store pairs. This is the clause the plan's sentence
//     ("bound exactly once ... never reassigned") does not spell and that
//     soundness needs: a single store that runs LATE - `f(); f = function(){}`
//     or a closure bound inside another function - leaves a window in which
//     the VM throws for an unbound name and a direct call would not. Function
//     declarations are hoisted to the start of the script by the compiler,
//     so every call in the program runs after the prologue.
//  4. THE CLOSURE VALUE HAS NO OTHER USE than the store, and F's own callee
//     argument (%arg2, the value `load_callee` reads) is used only to build
//     nested closures or read upvalues. Otherwise the closure escapes and the
//     VM may call F through a value this IR cannot see - which is why that
//     clause gates `private` rather than the rewrite: the rewrite is right
//     whenever the callee is F; `private` claims every caller is visible.
//  5. NOTHING WRITES THE GLOBALS TABLE DYNAMICALLY. In this VM that table is
//     context::globals_, written only by op::set_global (run_loop.cpp,
//     VM_CASE(set_global)), its AOT helper, and the host's define_global.
//     `window` and `globalThis` are the Shell's window_view native object
//     (lib/Shell/bindings/window.cpp:919-920) and do NOT alias it - so in this
//     engine `globalThis.add = 1` never rebinds `add`. The plan's rule (part
//     24, Phase 62½-A: "read through globalThis ... is unresolved") is kept
//     anyway, conservatively, because it is what a spec-conformant engine
//     would need and the cost is nothing on the programs this MVP targets:
//     any ctjs.load_global of those two names whose value reaches an
//     operation that writes through it, escapes into a call or a store, or
//     reaches an operation this walk does not know, resolves NOTHING in the
//     module. The eval-like case is real here: `Function(...)` compiles and
//     runs a NEW program (builtins/objects.cpp, install_dynamic_function),
//     whose top level can set_global anything, so a load of "Function" (or
//     "eval", should one ever be installed) also resolves nothing, as does
//     ctjs.dynamic_import, whose module can do the same.
//
// WHAT THE REWRITE PASSES. ctjs.call_direct's operands are the callee's entry
// block in order - receiver, new.target, callee, parameters (BytecodeImport
// .cpp, `implicit_arguments`). The receiver is the call's own; the callee is
// the load_global result, KEPT, because the boxed tier dispatches on it;
// new.target is a fresh `ctjs.constant #ctjs.undefined`, which is what the VM
// gives a plain call: VM_CASE(call) pushes `entered.new_target =
// pending_new_target_` and that root is undefined unless a pass_new_target or
// a construct set it (run_loop.cpp; context::call in call.cpp does the same
// two lines). A function containing ctjs.pass_new_target therefore has none
// of its calls rewritten - adjacency is an invariant nothing checks, and the
// pending flag could be live at any call in it.
//
// ARITY. Fewer arguments than parameters are padded with undefined, as
// VM_CASE(call) fills `registers_[new_base + i] = undefined` for i in
// [argc, param_count). MORE are not rewritten: the surplus lands in the
// callee's raw register window, where `arguments` and a rest parameter read
// it (type_record.cpp's note on context::call copying EVERY argument), and
// call_direct has no slot for a value the callee's block does not name. The
// native tier diagnoses such a site.
//
// ONE PROGRAM PER MODULE, ASSUMED AND SAID. A function index means nothing
// outside the program it was compiled in (ctjs.create_closure's own words),
// and the index is recovered from the symbol the importer built - `name$N`,
// BytecodeImport.cpp: `name += "$" + std::to_string(index)`. A module
// holding two programs would have two `$0`s and fail to verify long before
// this pass ran.
//
// C++ RATHER THAN DRR, per part 23 §1.2: this is a census across a module - a
// symbol table, a store count per name, an SSA reachability - and a Pat<>
// matches one operation's shape. The op it produces IS TableGen
// (CTJSOps.td, ctjs.call_direct); the pass declaration IS TableGen
// (Passes.td); only the analysis is here.
#include "ctcompile/CTJS/IR/CTJSAttrs.h"
#include "ctcompile/CTJS/IR/CTJSDialect.h"
#include "ctcompile/CTJS/IR/CTJSOps.h"
#include "ctcompile/CTJS/Transforms/Passes.h"

#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Interfaces/ControlFlowInterfaces.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SmallVector.h"

#include <cstdint>
#include <optional>
#include <string>

namespace ctcompile::ctjs {

#define GEN_PASS_DEF_CTJSRESOLVEGLOBALS
#include "ctcompile/CTJS/Transforms/Passes.h.inc"

namespace {

// The function index the importer put after the last `$` of the symbol.
std::optional<std::uint32_t> function_index_of(FuncOp function) {
    const llvm::StringRef name = function.getSymName();
    const std::size_t dollar = name.rfind('$');
    if (dollar == llvm::StringRef::npos) { return std::nullopt; }
    std::uint32_t index = 0;
    if (name.substr(dollar + 1).getAsInteger(10, index)) { return std::nullopt; }
    return index;
}

// The names the Shell binds the window object to.
bool names_global_object(llvm::StringRef name) {
    return name == "globalThis" || name == "window";
}

// A global whose value compiles source text at run time.
bool names_eval(llvm::StringRef name) {
    return name == "Function" || name == "eval";
}

std::string describe(mlir::Operation * op) {
    std::string text = op->getName().getStringRef().str();
    if (auto file = mlir::dyn_cast<mlir::FileLineColLoc>(op->getLoc())) {
        text += " at " + file.getFilename().str() + ":" + std::to_string(file.getLine());
    } else if (auto fused = mlir::dyn_cast<mlir::FusedLoc>(op->getLoc())) {
        for (const mlir::Location part : fused.getLocations()) {
            if (auto inner = mlir::dyn_cast<mlir::FileLineColLoc>(part)) {
                text += " at " + inner.getFilename().str() + ":" + std::to_string(inner.getLine());
                break;
            }
        }
    }
    return text;
}

// The two kinds of value this walk follows. They escape and are written
// through in the same places, but they are different objects and a diagnostic
// that names the wrong one sends a reader to the wrong line.
enum class watched {
    global_object, // globalThis / window, whose properties ARE the globals
    compiler,      // a value that may be `Function`, which compiles source
};

// The constant string key of a property access, or empty.
llvm::StringRef constant_key(mlir::Value key) {
    auto constant = key.getDefiningOp<ConstantOp>();
    if (!constant) { return {}; }
    auto text = llvm::dyn_cast<StringAttr>(constant.getValue());
    return text ? text.getValue() : llvm::StringRef{};
}

// CLAUSE 5: can anything in the module write the globals table other than a
// ctjs.store_global this pass can count? Answers the reason if so.
std::optional<std::string> dynamic_global_writes(mlir::ModuleOp module) {
    std::optional<std::string> reason;
    llvm::DenseMap<mlir::Value, watched> marked;
    llvm::SmallVector<mlir::Value> work;
    const auto mark = [&](mlir::Value value, watched kind) {
        if (marked.try_emplace(value, kind).second) { work.push_back(value); }
    };
    const auto name_of = [](watched kind) {
        return kind == watched::global_object ? "the global object"
                                              : "a value that may be the run-time compiler";
    };

    // A FUNCTION THE IMPORTER REFUSED IS A FUNCTION THIS PASS CANNOT READ, and
    // its `ctjs.store_global`s are not in the module to be counted. The census
    // below would then see one store where the program has two and resolve a
    // name that is rebound at run time - a call compiled to the WRONG function
    // rather than a diagnostic. Whole-module bail, because nothing says which
    // names the missing bodies touch.
    if (auto skipped = module->getAttrOfType<mlir::ArrayAttr>("ctjs.skipped");
        skipped && !skipped.empty()) {
        return "the importer refused " + std::to_string(skipped.size()) +
               " function(s) (ctjs.skipped), and a body this pass cannot read may store any global";
    }

    module.walk([&](mlir::Operation * op) {
        if (reason) { return mlir::WalkResult::interrupt(); }
        if (auto load = mlir::dyn_cast<LoadGlobalOp>(op)) {
            if (names_global_object(load.getName())) {
                mark(load.getResult(), watched::global_object);
            }
            if (names_eval(load.getName())) {
                reason = "a run-time compiler is reachable (" + describe(op) +
                         ") and the program it builds can store any global";
            }
        }
        // `Function` IS NOT ONLY A GLOBAL NAME. It sits on every function's
        // prototype as `.constructor`, so `(function(){}).constructor` is the
        // compiler reached through a property read this pass would otherwise
        // never look at. Following the value rather than refusing the key is
        // what keeps `o.constructor === C` and `o.constructor.name` resolvable:
        // those end at a comparison, and only a call or an escape answers.
        if (auto get = mlir::dyn_cast<GetPropertyOp>(op)) {
            if (constant_key(get.getKey()) == "constructor") {
                mark(get.getResult(), watched::compiler);
            }
        }
        if (mlir::isa<DynamicImportOp>(op)) {
            reason = "a dynamic import (" + describe(op) +
                     ") loads code this module cannot see, and it can store any global";
        }
        return mlir::WalkResult::advance();
    });
    if (reason) { return reason; }

    // FORWARD SSA REACHABILITY from the global object. A read through it
    // yields another value that may BE it (`window.window`), so reads
    // propagate; predicates and arithmetic answer primitives and stop; a
    // branch forwards into the successor's block argument; a write through
    // it, or an escape into anything that could write through it later, is
    // the answer.
    while (!work.empty()) {
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
                continue; // a primitive comes out, never the object
            }
            if (mlir::isa<GetPropertyOp, GetProtoOp, IterableOp, ConvertOp, CellGetOp,
                          LoadUpvalueOp>(op)) {
                for (const mlir::Value result : op->getResults()) { mark(result, kind); }
                continue;
            }
            if (mlir::isa<SetPropertyOp, DeletePropertyOp, DeleteNamedOp, DefineAccessorOp,
                          SetProtoOp, CopyPropsOp>(op)) {
                return std::string{name_of(kind)} + " is written through (" + describe(op) + ")";
            }
            // Passed, stored, captured, returned, thrown, called, or an
            // operation this walk does not know: the value is out of sight and
            // anything may write through it - or compile through it - from
            // here on.
            return std::string{name_of(kind)} + " escapes into " + describe(op);
        }
    }
    return std::nullopt;
}

// Whether every operation before `store` in its block belongs to the hoisting
// prologue: the frame, constants, and closure/store pairs.
bool in_prologue(StoreGlobalOp store) {
    for (mlir::Operation & before : *store->getBlock()) {
        if (&before == store.getOperation()) { return true; }
        if (!mlir::isa<FrameEnterOp, ConstantOp, CreateClosureOp, StoreGlobalOp>(before)) {
            return false;
        }
    }
    return false;
}

// CLAUSE 4's second half: F's own closure, as its body sees it, feeds nothing
// but nested closures and upvalue reads. Anything else is an escape.
std::optional<std::string> own_closure_escapes(FuncOp function) {
    if (function.getBody().empty() || function.getBody().front().getNumArguments() < 3) {
        return "the function has no callee argument";
    }
    const mlir::BlockArgument callee = function.getBody().front().getArgument(2);
    for (mlir::OpOperand & use : callee.getUses()) {
        mlir::Operation * op = use.getOwner();
        if (auto made = mlir::dyn_cast<CreateClosureOp>(op); made && use.getOperandNumber() == 0) {
            continue; // the enclosing closure of a nested one
        }
        if (mlir::isa<LoadUpvalueOp, StoreUpvalueOp>(op) && use.getOperandNumber() == 0) {
            continue; // a cell read or write through it
        }
        return "the function's own closure escapes into " + describe(op);
    }
    return std::nullopt;
}

struct binding {
    llvm::SmallVector<StoreGlobalOp> stores;
    llvm::SmallVector<LoadGlobalOp> loads;
};

struct CTJSResolveGlobalsPass : impl::CTJSResolveGlobalsBase<CTJSResolveGlobalsPass> {
    using CTJSResolveGlobalsBase::CTJSResolveGlobalsBase;

    void runOnOperation() override {
        mlir::ModuleOp module = getOperation();
        mlir::MLIRContext * context = &getContext();
        mlir::Builder builder(context);

        // THE CENSUS. In first-seen order, so the attribute is stable.
        llvm::MapVector<mlir::StringAttr, binding> bindings;
        llvm::DenseMap<std::uint32_t, FuncOp> by_index;
        llvm::DenseSet<mlir::Operation *> passes_new_target;
        module.walk([&](mlir::Operation * op) {
            if (auto store = mlir::dyn_cast<StoreGlobalOp>(op)) {
                bindings[store.getNameAttr()].stores.push_back(store);
            } else if (auto load = mlir::dyn_cast<LoadGlobalOp>(op)) {
                bindings[load.getNameAttr()].loads.push_back(load);
            } else if (auto function = mlir::dyn_cast<FuncOp>(op)) {
                if (const std::optional<std::uint32_t> index = function_index_of(function)) {
                    by_index.try_emplace(*index, function);
                }
            } else if (mlir::isa<PassNewTargetOp>(op)) {
                passes_new_target.insert(op->getParentOfType<FuncOp>());
            }
        });

        const std::optional<std::string> dynamic = dynamic_global_writes(module);
        FuncOp top = by_index.lookup(0);

        llvm::SmallVector<mlir::Attribute> rows;
        const auto record = [&](mlir::StringAttr name, std::size_t stores, FuncOp resolved,
                                const std::string & reason) {
            rows.push_back(builder.getDictionaryAttr({
                builder.getNamedAttr("name", name),
                builder.getNamedAttr("stores",
                                     builder.getI32IntegerAttr(static_cast<std::int32_t>(stores))),
                builder.getNamedAttr("resolved",
                                     resolved ? mlir::Attribute{mlir::FlatSymbolRefAttr::get(
                                                    resolved.getSymNameAttr())}
                                              : mlir::Attribute{builder.getStringAttr("none")}),
                builder.getNamedAttr("reason", builder.getStringAttr(reason)),
            }));
        };

        for (auto & [name, facts] : bindings) {
            // ---- clauses 1-3, 5: is the name resolvable at all? ------------
            if (dynamic) {
                record(name, facts.stores.size(), FuncOp{}, *dynamic);
                continue;
            }
            if (facts.stores.empty()) {
                record(name, 0, FuncOp{}, "never stored in this program - a host binding");
                continue;
            }
            if (facts.stores.size() != 1) {
                record(name, facts.stores.size(), FuncOp{},
                       "stored " + std::to_string(facts.stores.size()) + " times");
                continue;
            }
            StoreGlobalOp store = facts.stores.front();
            auto made = store.getValue().getDefiningOp<CreateClosureOp>();
            if (!made) {
                record(name, facts.stores.size(), FuncOp{},
                       "bound to something other than a closure");
                continue;
            }
            if (!top) {
                record(name, facts.stores.size(), FuncOp{},
                       "no top-level function (index 0) in the module");
                continue;
            }
            if (store->getParentOfType<FuncOp>() != top ||
                store->getBlock() != &top.getBody().front() || !in_prologue(store)) {
                record(name, facts.stores.size(), FuncOp{},
                       "bound outside the top level's hoisting prologue, so a call may run "
                       "before the binding exists");
                continue;
            }
            const auto index = static_cast<std::uint32_t>(made.getFunction());
            FuncOp target = by_index.lookup(index);
            if (!target) {
                record(name, facts.stores.size(), FuncOp{},
                       "function " + std::to_string(index) +
                           " emitted no ctjs.func - refused by the importer (ctjs.skipped)");
                continue;
            }
            if (target->hasAttr("ctjs.not_lowered")) {
                record(name, facts.stores.size(), FuncOp{},
                       "function " + target.getSymName().str() + " was refused");
                continue;
            }
            if (target.getBody().empty() || target.getBody().front().getNumArguments() < 3) {
                record(name, facts.stores.size(), FuncOp{},
                       "function " + target.getSymName().str() + " has no body");
                continue;
            }
            ++resolvedGlobals;
            const unsigned parameters = target.getBody().front().getNumArguments() - 3;

            // ---- the rewrite, per load ----------------------------------------
            std::optional<std::string> open;
            if (!made.getResult().hasOneUse()) {
                open = "the closure value has a use other than the store";
            }
            std::size_t rewritten = 0;
            for (LoadGlobalOp load : facts.loads) {
                llvm::SmallVector<mlir::OpOperand *> uses;
                for (mlir::OpOperand & use : load.getResult().getUses()) { uses.push_back(&use); }
                for (mlir::OpOperand * use : uses) {
                    mlir::Operation * user = use->getOwner();
                    // Already resolved by an earlier run: the callee VALUE of a
                    // call_direct naming this very function.
                    if (auto direct = mlir::dyn_cast<CallDirectOp>(user);
                        direct && use->getOperandNumber() == 2 &&
                        direct.getCallee() == target.getSymName()) {
                        continue;
                    }
                    auto call = mlir::dyn_cast<CallOp>(user);
                    if (!call || use->getOperandNumber() != 0) {
                        if (!open) { open = "the binding is used by " + describe(user); }
                        continue;
                    }
                    if (call.getArgs().size() > parameters) {
                        if (!open) {
                            open = "a call passes " + std::to_string(call.getArgs().size()) +
                                   " argument(s) to " + std::to_string(parameters) +
                                   " parameter(s) - the surplus has frame semantics (" +
                                   describe(user) + ")";
                        }
                        continue;
                    }
                    if (passes_new_target.contains(call->getParentOfType<FuncOp>())) {
                        if (!open) {
                            open = "a call sits in a function that passes new.target (" +
                                   describe(user) + ")";
                        }
                        continue;
                    }

                    mlir::OpBuilder at(call);
                    const auto value_type = ValueType::get(context);
                    const mlir::Value undefined = ConstantOp::create(at, call.getLoc(), value_type,
                                                                     UndefinedAttr::get(context));
                    llvm::SmallVector<mlir::Value> arguments(call.getArgs());
                    while (arguments.size() < parameters) { arguments.push_back(undefined); }
                    auto direct = CallDirectOp::create(
                        at, call.getLoc(), value_type,
                        mlir::FlatSymbolRefAttr::get(target.getSymNameAttr()), call.getReceiver(),
                        undefined, load.getResult(), arguments,
                        /*arg_attrs=*/nullptr, /*res_attrs=*/nullptr);
                    call.getResult().replaceAllUsesWith(direct.getResult());
                    call.erase();
                    ++rewritten;
                    ++rewrittenCalls;
                }
            }

            // ---- clause 4: is the world closed around F? ----------------------
            if (!open) { open = own_closure_escapes(target); }
            if (open) {
                record(name, facts.stores.size(), target,
                       "open: " + *open + "; " + std::to_string(rewritten) + " call(s) rewritten");
                continue;
            }
            mlir::SymbolTable::setSymbolVisibility(target, mlir::SymbolTable::Visibility::Private);
            ++closedFunctions;
            record(name, facts.stores.size(), target,
                   "closed: " + std::to_string(rewritten) + " call(s) rewritten");
        }

        module->setAttr("ctjs.globals", builder.getArrayAttr(rows));
    }
};

} // namespace

} // namespace ctcompile::ctjs
