//===- Instances.cpp - Class instances owned by a registry Map ------------===//
//
// proveInstanceRegistries (Analysis/Instances.h): which classes store `this`
// into a Map that can own the instances as `std::shared_ptr<C>`, and why the
// others cannot. Each check names its refusal in one sentence.
//
//===----------------------------------------------------------------------===//

#include "ctcompile/CTNative/Analysis/Instances.h"

#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/IR/SymbolTable.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SetVector.h"

#include <optional>

namespace ctcompile::ctnative {
namespace {

/// The JavaScript name of a function: its symbol before the importer's `$`.
std::string jsName(ctjs::FuncOp fn) {
    return fn.getSymName().split('$').first.str();
}

/// The operation name of `op`, quoted for a refusal sentence.
std::string named(mlir::Operation * op) {
    return "`" + op->getName().getStringRef().str() + "`";
}

/// Whether `value` is the `this` of the function whose entry block holds it.
bool isReceiver(mlir::Value value) {
    auto argument = llvm::dyn_cast<mlir::BlockArgument>(value);
    return argument && argument.getArgNumber() == ctjs::arg_receiver &&
           llvm::isa<ctjs::FuncOp>(argument.getOwner()->getParentOp()) &&
           argument.getOwner()->isEntryBlock();
}

/// The null or undefined constant `value` is, as a nullishness bit, or 0.
enum : unsigned {
    mayBeUndefined = 1,
    mayBeNull = 2
};
unsigned nullishBits(mlir::Value value) {
    if (ctjs::constantAttr<ctjs::UndefinedAttr>(value)) { return mayBeUndefined; }
    if (ctjs::constantAttr<ctjs::NullAttr>(value)) { return mayBeNull; }
    auto global = value ? value.getDefiningOp<ctjs::LoadGlobalOp>() : ctjs::LoadGlobalOp{};
    return global && global.getName() == "undefined" ? mayBeUndefined : 0;
}

/// Whether `use` dereferences the object it names: a constant-key field read
/// or write on it, or a method call with it as the receiver of a method read
/// from it.
bool dereferences(mlir::OpOperand & use) {
    mlir::Operation * user = use.getOwner();
    if (llvm::isa<ctjs::GetPropertyOp, ctjs::SetPropertyOp>(user)) {
        return use.getOperandNumber() == 0 && !ctjs::constantKey(ctjs::propertyKey(user)).empty();
    }
    auto call = llvm::dyn_cast<ctjs::CallOp>(user);
    auto method = call ? ctjs::receiverMethod(call) : ctjs::GetPropertyOp{};
    return method && use.getOperandNumber() == 1 && method.getObject() == use.get();
}

/// The functions each create_closure names and the closures naming each
/// function, by the importer's function index.
class functionIndex {
    llvm::DenseMap<unsigned, ctjs::FuncOp> byIndex;
    llvm::DenseMap<mlir::Operation *, llvm::SmallVector<ctjs::CreateClosureOp>> closures;

public:
    explicit functionIndex(mlir::ModuleOp module) {
        module.walk([&](ctjs::FuncOp fn) {
            if (auto index = ctjs::functionIndex(fn)) { byIndex[*index] = fn; }
        });
        module.walk([&](ctjs::CreateClosureOp made) {
            if (ctjs::FuncOp target = targetOf(made)) {
                closures[target.getOperation()].push_back(made);
            }
        });
    }
    ctjs::FuncOp targetOf(ctjs::CreateClosureOp made) const {
        auto index = ctjs::closureFunction(made);
        ctjs::FuncOp fn = index ? byIndex.lookup(*index) : ctjs::FuncOp{};
        return fn && !fn.getBody().empty() ? fn : ctjs::FuncOp{};
    }
    llvm::ArrayRef<ctjs::CreateClosureOp> closuresOf(ctjs::FuncOp fn) const {
        auto found = closures.find(fn.getOperation());
        return found == closures.end() ? llvm::ArrayRef<ctjs::CreateClosureOp>{}
                                       : llvm::ArrayRef(found->second);
    }
    /// The function a ctjs.call or ctjs.call_direct runs, when it names one.
    ctjs::FuncOp calleeOf(mlir::Operation * call) const {
        if (auto direct = llvm::dyn_cast<ctjs::CallDirectOp>(call)) { return direct.getTarget(); }
        auto plain = llvm::dyn_cast<ctjs::CallOp>(call);
        auto made = plain ? plain.getCallee().getDefiningOp<ctjs::CreateClosureOp>()
                          : ctjs::CreateClosureOp{};
        return made ? targetOf(made) : ctjs::FuncOp{};
    }
};

/// Every call of `fn`, when each is a direct call or a call through one of
/// its closures and its own closure goes nowhere else; none otherwise.
std::optional<llvm::SmallVector<mlir::Operation *>> callersOf(ctjs::FuncOp fn,
                                                              mlir::ModuleOp module,
                                                              const functionIndex & functions) {
    if (ctjs::ownClosureEscape(fn)) { return std::nullopt; }
    const auto made = functions.closuresOf(fn);
    if (!fn.isPrivate() && made.empty()) { return std::nullopt; } // The embedder may call it.
    llvm::SmallVector<mlir::Operation *> calls;
    const auto uses = mlir::SymbolTable::getSymbolUses(fn, module);
    if (!uses) { return std::nullopt; }
    for (const auto & use : *uses) {
        if (!llvm::isa<ctjs::CallDirectOp>(use.getUser())) { return std::nullopt; }
        calls.push_back(use.getUser());
    }
    for (ctjs::CreateClosureOp closure : made) {
        for (mlir::OpOperand & use : closure.getResult().getUses()) {
            auto direct = llvm::dyn_cast<ctjs::CallDirectOp>(use.getOwner());
            if (direct && use.getOperandNumber() == ctjs::arg_callee && direct.getTarget() == fn) {
                continue; // Counted by its symbol.
            }
            if (llvm::isa<ctjs::CallOp>(use.getOwner()) && use.getOperandNumber() == 0) {
                calls.push_back(use.getOwner());
                continue;
            }
            return std::nullopt;
        }
    }
    return calls;
}

/// The value a call passes to entry-block argument `number` of its callee:
/// null when the call passes fewer arguments, which JavaScript fills with
/// undefined.
mlir::Value actualOf(mlir::Operation * call, unsigned number) {
    const auto view = ctjs::callView(call);
    const unsigned position = number - ctjs::implicit_arguments;
    return position < view->args.size() ? view->args[position] : mlir::Value{};
}

/// The values that name one `new Map()`, the method calls made on them, and
/// the first use that lets the Map escape.
struct mapUses {
    llvm::SetVector<mlir::Value> aliases;
    llvm::SmallVector<ctjs::CallOp> sets, gets, deletes;
    bool iterated = false;
    std::string escape;
};

/// Follows `map` through the one binding it is stored in and the closures
/// that capture that binding. A Map reached any other way, or used other than
/// through the methods the proof models, escapes.
mapUses usesOf(ctjs::ConstructOp map, const functionIndex & functions) {
    mapUses out;
    out.aliases.insert(map.getResult());
    const auto escape = [&](const std::string & why) {
        if (out.escape.empty()) { out.escape = why; }
    };
    // The reads of `cell` captured as upvalue `slot` of closures of `fn`.
    const auto captured = [&](ctjs::FuncOp fn, unsigned slot) {
        const mlir::Value self = fn.getBody().front().getArgument(ctjs::arg_callee);
        fn.walk([&](mlir::Operation * op) {
            if (auto read = llvm::dyn_cast<ctjs::LoadUpvalueOp>(op)) {
                if (read.getClosure() == self && read.getIndex() == static_cast<int32_t>(slot)) {
                    out.aliases.insert(read.getResult());
                }
            } else if (auto write = llvm::dyn_cast<ctjs::StoreUpvalueOp>(op)) {
                if (write.getClosure() == self && write.getIndex() == static_cast<int32_t>(slot)) {
                    escape("the binding holding the registry Map is assigned again in `" +
                           jsName(fn) + "`");
                }
            } else if (auto inner = llvm::dyn_cast<ctjs::CreateClosureOp>(op)) {
                const auto indices = inner.getEnclosingIndicesAttr();
                if (indices &&
                    llvm::is_contained(indices.asArrayRef(), static_cast<int32_t>(slot))) {
                    escape("the registry Map is captured through an enclosing closure, which "
                           "this proof does not follow");
                }
            }
        });
    };
    for (std::size_t at = 0; at < out.aliases.size(); ++at) {
        const mlir::Value alias = out.aliases[at];
        for (mlir::OpOperand & use : alias.getUses()) {
            mlir::Operation * user = use.getOwner();
            if (auto store = llvm::dyn_cast<ctjs::CellSetOp>(user);
                store && use.getOperandNumber() == 1) {
                const mlir::Value cell = store.getCell();
                if (!cell.getDefiningOp<ctjs::CreateCellOp>()) {
                    escape("the registry Map is stored into a binding this proof cannot follow");
                    continue;
                }
                for (mlir::OpOperand & cellUse : cell.getUses()) {
                    mlir::Operation * reader = cellUse.getOwner();
                    if (auto other = llvm::dyn_cast<ctjs::CellSetOp>(reader)) {
                        if (other != store) {
                            escape("the binding holding the registry Map is assigned twice");
                        }
                        continue;
                    }
                    if (auto get = llvm::dyn_cast<ctjs::CellGetOp>(reader)) {
                        out.aliases.insert(get.getResult());
                        continue;
                    }
                    auto made = llvm::dyn_cast<ctjs::CreateClosureOp>(reader);
                    ctjs::FuncOp target = made ? functions.targetOf(made) : ctjs::FuncOp{};
                    if (!target ||
                        cellUse.getOperandNumber() < made.getUpvalues().getBeginOperandIndex()) {
                        escape("the binding holding the registry Map reaches " + named(reader));
                        continue;
                    }
                    const auto upvalues = made.getUpvalues();
                    for (unsigned slot = 0; slot < upvalues.size(); ++slot) {
                        if (upvalues[slot] == cell) { captured(target, slot); }
                    }
                }
                continue;
            }
            // The receiver of `alias.method(...)`, seen from the method read.
            if (auto call = llvm::dyn_cast<ctjs::CallOp>(user);
                call && use.getOperandNumber() == 1) {
                auto method = ctjs::receiverMethod(call);
                if (method && method.getObject() == alias) { continue; }
            }
            auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(user);
            const llvm::StringRef key =
                read && use.getOperandNumber() == 0 ? ctjs::constantKey(read.getKey()) : "";
            if (key == "size") { continue; }
            auto call = read && read.getResult().hasOneUse()
                            ? llvm::dyn_cast<ctjs::CallOp>(*read.getResult().getUsers().begin())
                            : ctjs::CallOp{};
            if (!call || call.getCallee() != read.getResult() || call.getReceiver() != alias) {
                escape(llvm::isa<ctjs::SetPropertyOp>(user)
                           ? "the registry Map is stored into an object"
                           : "the registry Map reaches " + named(user));
                continue;
            }
            const std::size_t arity = call.getArgs().size();
            if (key == "set" && arity == 2) {
                out.sets.push_back(call);
            } else if (key == "get" && arity == 1) {
                out.gets.push_back(call);
            } else if (key == "delete" && arity == 1) {
                out.deletes.push_back(call);
            } else if (key == "keys" && arity == 0) {
                out.iterated = true;
            } else if (key != "has" || arity != 1) {
                escape(
                    ("the registry Map's `" + key + "` is not a method this proof models").str());
            }
        }
    }
    return out;
}

/// The registry proof for one Map whose `set` stores a constructor's `this`.
class registryProof {
    mlir::ModuleOp module;
    const functionIndex & functions;
    const mapUses & map;
    InstanceRegistry & out;
    std::string name; // The class's JavaScript name.
    llvm::SmallVector<ctjs::ConstructOp> constructions;
    llvm::SmallVector<ctjs::FuncOp> methods;
    llvm::DenseSet<llvm::StringRef> methodNames;
    // Values that may hold an instance read back from the Map, or null or
    // undefined; for each, the values that flow into it and where; and which
    // of null and undefined each may be.
    struct source {
        mlir::Value value; // Null for an argument a call does not pass.
        mlir::Operation * site;
    };
    llvm::SetVector<mlir::Value> maybe;
    llvm::DenseMap<mlir::Value, llvm::SmallVector<source>> sources;
    using nullishMap = llvm::DenseMap<mlir::Value, unsigned>;
    nullishMap nullish;
    // The values that name an instance: `this` in the constructor and the
    // methods, `new` results, and `maybe`.
    llvm::SetVector<mlir::Value> receivers;

    bool refuse(const std::string & why) {
        if (out.refusal.empty()) { out.refusal = why; }
        return false;
    }

    /// The class around the constructor: its closure, its `new` sites and
    /// its prototype methods.
    bool collectClass() {
        const auto made = functions.closuresOf(out.constructor);
        if (made.size() != 1) {
            return refuse("the class `" + name + "` is not created exactly once");
        }
        ctjs::CreateClosureOp only = made.front();
        const mlir::Value closure = only.getResult();
        mlir::Value prototype;
        for (mlir::OpOperand & use : closure.getUses()) {
            mlir::Operation * user = use.getOwner();
            auto store = llvm::dyn_cast<ctjs::SetPropertyOp>(user);
            if (store && use.getOperandNumber() == 0) {
                if (ctjs::constantKey(store.getKey()) == "prototype") {
                    prototype = store.getValue();
                }
                continue;
            }
            if (store && use.getOperandNumber() == 2 &&
                ctjs::constantKey(store.getKey()) == "constructor") {
                continue; // The prototype's `constructor` slot, checked below.
            }
            if (auto made = llvm::dyn_cast<ctjs::ConstructOp>(user);
                made && made.getCallee() == closure && made.getNewTarget() == closure) {
                if (use.getOperandNumber() == 0) { constructions.push_back(made); }
                continue;
            }
            if (auto call = llvm::dyn_cast<ctjs::CallOp>(user);
                call && ctjs::calleeGlobalName(call) == "__ctbrowser_class_defined") {
                continue;
            }
            return refuse("the class `" + name + "` reaches " + named(user) +
                          ", so this proof cannot see every `new`");
        }
        if (!prototype || !prototype.getDefiningOp<ctjs::CreateObjectOp>()) {
            return refuse("the class `" + name + "` has no prototype object this proof can read");
        }
        for (mlir::OpOperand & use : prototype.getUses()) {
            auto store = llvm::dyn_cast<ctjs::SetPropertyOp>(use.getOwner());
            const llvm::StringRef key = store ? ctjs::constantKey(store.getKey()) : "";
            if (store && use.getOperandNumber() == 2 && key == "prototype") { continue; }
            if (store && use.getOperandNumber() == 2 && key == "__home") { continue; }
            if (store && use.getOperandNumber() == 0 && key == "constructor" &&
                store.getValue() == closure) {
                continue;
            }
            auto method = store && use.getOperandNumber() == 0
                              ? store.getValue().getDefiningOp<ctjs::CreateClosureOp>()
                              : ctjs::CreateClosureOp{};
            ctjs::FuncOp target = method ? functions.targetOf(method) : ctjs::FuncOp{};
            if (!target || key.empty()) {
                return refuse("the prototype of `" + name + "` reaches " + named(use.getOwner()));
            }
            methods.push_back(target);
            methodNames.insert(key);
        }
        return true;
    }

    /// `this` in the constructor: constant-key field accesses and the value
    /// of the registry `set` only. In a method: field accesses and method
    /// calls only.
    bool checkReceivers() {
        const mlir::Value self = out.constructor.getBody().front().getArgument(ctjs::arg_receiver);
        receivers.insert(self);
        for (mlir::OpOperand & use : self.getUses()) {
            if (llvm::isa<ctjs::GetPropertyOp, ctjs::SetPropertyOp>(use.getOwner()) &&
                dereferences(use)) {
                continue;
            }
            if (llvm::is_contained(map.sets, use.getOwner()) && use.getOperandNumber() == 3) {
                continue;
            }
            return refuse("`this` in the constructor of `" + name + "` also reaches " +
                          named(use.getOwner()));
        }
        for (ctjs::FuncOp method : methods) {
            const mlir::Value self = method.getBody().front().getArgument(ctjs::arg_receiver);
            receivers.insert(self);
            for (mlir::OpOperand & use : self.getUses()) {
                if (!dereferences(use)) {
                    return refuse("`this` in the method `" + jsName(method) + "` of `" + name +
                                  "` reaches " + named(use.getOwner()));
                }
            }
        }
        for (ctjs::ConstructOp made : constructions) {
            receivers.insert(made.getResult());
            for (mlir::OpOperand & use : made.getResult().getUses()) {
                if (!dereferences(use)) {
                    return refuse("a new `" + name + "` reaches " + named(use.getOwner()) +
                                  " - only its fields and methods may use it");
                }
            }
        }
        return true;
    }

    /// Adds `into` to the values that may hold an instance, fed by `from` at
    /// `site`.
    void flow(mlir::Value into, mlir::Value from, mlir::Operation * site) {
        maybe.insert(into);
        sources[into].push_back({from, site});
    }

    /// Adds every value a read-back instance flows into from `value`'s uses,
    /// and refuses a use that is neither a flow, a test nor a dereference.
    bool follow(mlir::Value value) {
        for (mlir::OpOperand & use : value.getUses()) {
            mlir::Operation * user = use.getOwner();
            if (dereferences(use) || llvm::isa<ctjs::TruthyOp>(user)) { continue; }
            if (auto unary = llvm::dyn_cast<ctjs::UnaryOp>(user);
                unary && unary.getKind() == ctjs::UnaryKind::Not) {
                continue;
            }
            if (auto compare = llvm::dyn_cast<ctjs::CompareOp>(user)) {
                const bool equality = compare.getKind() == ctjs::CompareKind::Eq ||
                                      compare.getKind() == ctjs::CompareKind::StrictEq;
                if (equality && (nullishBits(compare.getLhs()) || nullishBits(compare.getRhs()))) {
                    continue;
                }
                return refuse("a value read from the registry Map is compared with something "
                              "other than null or undefined");
            }
            if (auto yield = llvm::dyn_cast<mlir::scf::YieldOp>(user)) {
                auto branch = llvm::dyn_cast<mlir::scf::IfOp>(yield->getParentOp());
                if (!branch) {
                    return refuse("a value read from the registry Map is carried around a loop");
                }
                const unsigned index = use.getOperandNumber();
                const mlir::Value joined = branch.getResult(index);
                if (!maybe.contains(joined)) {
                    for (mlir::Region * arm : {&branch.getThenRegion(), &branch.getElseRegion()}) {
                        auto end = llvm::cast<mlir::scf::YieldOp>(arm->front().getTerminator());
                        flow(joined, end.getOperand(index), end);
                    }
                }
                continue;
            }
            if (llvm::isa<ctjs::ReturnOp>(user)) {
                auto fn = user->getParentOfType<ctjs::FuncOp>();
                const auto callers = callersOf(fn, module, functions);
                if (!callers) {
                    return refuse("a value read from the registry Map is returned by `" +
                                  jsName(fn) + "`, whose callers are not all known");
                }
                for (mlir::Operation * call : *callers) {
                    const mlir::Value result = call->getResult(0);
                    if (maybe.contains(result)) { continue; }
                    fn.walk([&](ctjs::ReturnOp exit) { flow(result, exit.getValue(), exit); });
                }
                continue;
            }
            if (llvm::isa<ctjs::CallOp, ctjs::CallDirectOp>(user)) {
                const auto view = ctjs::callView(user);
                const unsigned first = view->args.getBeginOperandIndex();
                ctjs::FuncOp callee = functions.calleeOf(user);
                const auto callers = callee ? callersOf(callee, module, functions) : std::nullopt;
                if (use.getOperandNumber() < first || !callers) {
                    return refuse("a value read from the registry Map is passed to " + named(user) +
                                  ", whose callee or callers are not all known");
                }
                const unsigned number = use.getOperandNumber() - first + ctjs::implicit_arguments;
                mlir::Block & entry = callee.getBody().front();
                if (number >= entry.getNumArguments()) { continue; } // Never read.
                const mlir::Value parameter = entry.getArgument(number);
                if (!maybe.contains(parameter)) {
                    for (mlir::Operation * call : *callers) {
                        flow(parameter, actualOf(call, number), call);
                    }
                }
                continue;
            }
            if (llvm::isa<ctjs::SetPropertyOp, ctjs::ConstructOp>(user)) {
                return refuse("a value read from the registry Map is stored into an object, so "
                              "shared ownership could form a cycle no destructor breaks");
            }
            return refuse("a value read from the registry Map reaches " + named(user));
        }
        return true;
    }

    /// Whether every value joined into an instance is an instance, null or
    /// undefined.
    bool checkJoins() {
        for (mlir::Value value : maybe) {
            for (const source & from : sources.lookup(value)) {
                if (!from.value || nullishBits(from.value) || maybe.contains(from.value)) {
                    continue;
                }
                return refuse("a value read from the registry Map is joined with a value that "
                              "is not an instance of `" +
                              name + "`");
            }
        }
        return true;
    }

    /// Which of null and undefined each value may hold: the least fixpoint
    /// over its sources, where a source checked at its site adds nothing.
    /// `reference` answers which nullish values a strict comparison must rule
    /// out; without one, strict comparisons are not checks.
    nullishMap solveNullish(const nullishMap * reference) {
        nullishMap bits;
        for (ctjs::CallOp get : map.gets) { bits[get.getResult()] = mayBeUndefined; }
        for (bool changed = true; changed;) {
            changed = false;
            for (mlir::Value value : maybe) {
                unsigned joined = bits.lookup(value);
                for (const source & from : sources.lookup(value)) {
                    if (!from.value) {
                        joined |= mayBeUndefined;
                    } else if (!guardedAt(from.value, from.site, reference)) {
                        joined |= nullishBits(from.value) | bits.lookup(from.value);
                    }
                }
                if (joined != bits.lookup(value)) {
                    bits[value] = joined;
                    changed = true;
                }
            }
        }
        return bits;
    }

    /// Whether `op` runs only where `value` holds an instance: it sits in the
    /// arm of an scf.if that a truthiness test of `value`, or an equality
    /// test of it against null or undefined that rules out every nullish
    /// value `reference` says it may hold, selects.
    bool guardedAt(mlir::Value value, mlir::Operation * op, const nullishMap * reference) {
        for (mlir::Operation * child = op; child && !llvm::isa<ctjs::FuncOp>(child);
             child = child->getParentOp()) {
            auto branch = llvm::dyn_cast_or_null<mlir::scf::IfOp>(child->getParentOp());
            if (!branch) { continue; }
            bool positive = child->getParentRegion() == &branch.getThenRegion();
            mlir::Value condition = branch.getCondition();
            for (;;) {
                if (auto truthy = condition.getDefiningOp<ctjs::TruthyOp>()) {
                    condition = truthy.getValue();
                } else if (auto unary = condition.getDefiningOp<ctjs::UnaryOp>();
                           unary && unary.getKind() == ctjs::UnaryKind::Not) {
                    positive = !positive;
                    condition = unary.getOperand();
                } else {
                    break;
                }
            }
            if (condition == value && positive) { return true; }
            auto compare = condition.getDefiningOp<ctjs::CompareOp>();
            if (!compare || positive) { continue; } // `x == null` holds an instance when false.
            const mlir::Value other = compare.getLhs() == value   ? compare.getRhs()
                                      : compare.getRhs() == value ? compare.getLhs()
                                                                  : mlir::Value{};
            const unsigned against = other ? nullishBits(other) : 0;
            if (!against) { continue; }
            if (compare.getKind() == ctjs::CompareKind::Eq) { return true; }
            if (compare.getKind() == ctjs::CompareKind::StrictEq && reference &&
                (reference->lookup(value) & ~against) == 0) {
                return true;
            }
        }
        return false;
    }

    /// Whether `value`, stored into a field of an instance, is a primitive:
    /// so no instance owns the Map or another instance, and shared ownership
    /// forms no cycle.
    bool primitive(mlir::Value value, unsigned depth = 0) {
        mlir::Operation * made = value.getDefiningOp();
        if (llvm::isa_and_nonnull<ctjs::ConstantOp, ctjs::BinaryOp, ctjs::UnaryOp, ctjs::CompareOp>(
                made)) {
            return true;
        }
        if (auto read = llvm::dyn_cast_or_null<ctjs::GetPropertyOp>(made)) {
            const llvm::StringRef key = ctjs::constantKey(read.getKey());
            return receivers.contains(read.getObject()) && ctjs::ordinaryKey(key) &&
                   !methodNames.contains(key);
        }
        auto argument = llvm::dyn_cast<mlir::BlockArgument>(value);
        if (!argument || depth > 8 || argument.getArgNumber() < ctjs::implicit_arguments ||
            argument.getOwner()->getParentOp() != out.constructor.getOperation()) {
            return false;
        }
        const unsigned position = argument.getArgNumber() - ctjs::implicit_arguments;
        return llvm::all_of(constructions, [&](ctjs::ConstructOp made) {
            return position >= made.getArgs().size() ||
                   primitive(made.getArgs()[position], depth + 1);
        });
    }

    /// What a field value that is not a primitive may hold, for the refusal.
    std::string describe(mlir::Value value) {
        if (map.aliases.contains(value)) { return "the registry Map"; }
        if (receivers.contains(value)) { return "an instance of `" + name + "`"; }
        mlir::Operation * made = value.getDefiningOp();
        if (llvm::isa_and_nonnull<ctjs::CreateObjectOp, ctjs::CreateArrayOp, ctjs::ConstructOp>(
                made)) {
            return "an object";
        }
        if (llvm::isa_and_nonnull<ctjs::CreateClosureOp>(made)) { return "a function"; }
        return "a value this proof cannot show is a primitive";
    }

public:
    registryProof(mlir::ModuleOp module, const functionIndex & functions, const mapUses & map,
                  InstanceRegistry & out)
        : module(module), functions(functions), map(map), out(out), name(jsName(out.constructor)) {}

    bool run() {
        if (!map.escape.empty()) { return refuse(map.escape); }
        const mlir::Value self = out.constructor.getBody().front().getArgument(ctjs::arg_receiver);
        for (ctjs::CallOp set : map.sets) {
            if (set.getArgs()[1] != self) {
                return refuse("the registry Map also holds a value that is not a new `" + name +
                              "`");
            }
        }
        if (map.iterated && !map.deletes.empty()) {
            return refuse("the registry Map is iterated, and `delete` removes entries from it "
                          "- a removal during the iteration changes what it visits");
        }
        if (!collectClass() || !checkReceivers()) { return false; }
        // Instances read back: every flow followed, every dereference checked.
        for (ctjs::CallOp get : map.gets) { maybe.insert(get.getResult()); }
        for (std::size_t at = 0; at < maybe.size(); ++at) {
            if (!follow(maybe[at])) { return false; }
        }
        if (!checkJoins()) { return false; }
        // Strict comparisons count as checks only against the nullish values
        // a first solution without them allows, which over-approximates.
        const nullishMap loose = solveNullish(nullptr);
        nullish = solveNullish(&loose);
        for (mlir::Value value : maybe) {
            receivers.insert(value);
            if (nullish.lookup(value) == 0) {
                if (llvm::isa<mlir::BlockArgument>(value)) { out.nonNull.insert(value); }
                continue;
            }
            for (mlir::OpOperand & use : value.getUses()) {
                if (dereferences(use) && !guardedAt(value, use.getOwner(), &nullish)) {
                    return refuse("a value read from the registry Map is used at " +
                                  named(use.getOwner()) +
                                  " without a check - `get` answers undefined for a missing key");
                }
            }
        }
        // Acyclic: every field of an instance holds a primitive.
        for (mlir::Value receiver : receivers) {
            for (mlir::Operation * user : receiver.getUsers()) {
                auto store = llvm::dyn_cast<ctjs::SetPropertyOp>(user);
                if (!store || store.getObject() != receiver || primitive(store.getValue())) {
                    continue;
                }
                return refuse("the field `" + ctjs::constantKey(store.getKey()).str() + "` of `" +
                              name + "` can hold " + describe(store.getValue()) +
                              ", so shared ownership could form a cycle no destructor breaks");
            }
        }
        return true;
    }
};

} // namespace

llvm::SmallVector<InstanceRegistry> proveInstanceRegistries(mlir::ModuleOp module) {
    const functionIndex functions(module);
    llvm::SmallVector<InstanceRegistry> out;
    module.walk([&](ctjs::ConstructOp made) {
        if (ctjs::calleeGlobalName(made) != "Map" || !made.getArgs().empty()) { return; }
        const mapUses uses = usesOf(made, functions);
        // A registry Map stores the `this` of some function.
        ctjs::CallOp stored;
        for (ctjs::CallOp set : uses.sets) {
            if (!stored && isReceiver(set.getArgs()[1])) { stored = set; }
        }
        if (!stored) { return; }
        InstanceRegistry & proof = out.emplace_back();
        proof.map = made;
        proof.constructor = llvm::cast<ctjs::FuncOp>(
            llvm::cast<mlir::BlockArgument>(stored.getArgs()[1]).getOwner()->getParentOp());
        registryProof(module, functions, uses, proof).run();
    });
    return out;
}

} // namespace ctcompile::ctnative
