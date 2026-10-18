//===- Instances.cpp - Class instances owned by a registry Map ------------===//
//
// proveInstanceRegistries (Analysis/Instances.h): which classes store `this`
// into a Map that can own the instances as `std::shared_ptr<C>`, and why the
// others cannot. Each check names its refusal in one sentence.
//
//===----------------------------------------------------------------------===//

#include "ctcompile/CTNative/Analysis/Instances.h"

#include "ctcompile/CTNative/Analysis/NativeMap.h"
#include "ctcompile/CTNative/Analysis/TypeInference.h"
#include "ctcompile/CTNative/IR/CTNativeAttrNames.h"
#include "ctcompile/CTNative/IR/CTNativeDialect.h"

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
/// mayBeFalsy stands for another falsy value, such as the `false` of
/// `map.has(k) && ...`.
enum : unsigned {
    mayBeUndefined = 1,
    mayBeNull = 2,
    mayBeFalsy = 4
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

/// The class a static method `made` is stored on, by `set_property
/// C[key] = made` with C a class's closure and the key stored once; null
/// otherwise.
std::pair<mlir::Value, llvm::StringRef> staticSlot(ctjs::CreateClosureOp made) {
    for (mlir::OpOperand & use : made.getResult().getUses()) {
        auto store = llvm::dyn_cast<ctjs::SetPropertyOp>(use.getOwner());
        if (!store || use.getOperandNumber() != 2) { continue; }
        const mlir::Value owner = store.getObject();
        const llvm::StringRef key = ctjs::constantKey(store.getKey());
        const bool isClass = llvm::any_of(owner.getUsers(), [&](mlir::Operation * user) {
            auto attach = llvm::dyn_cast<ctjs::SetPropertyOp>(user);
            return attach && attach.getObject() == owner &&
                   ctjs::constantKey(attach.getKey()) == "prototype";
        });
        const unsigned writes = llvm::count_if(owner.getUsers(), [&](mlir::Operation * user) {
            auto other = llvm::dyn_cast<ctjs::SetPropertyOp>(user);
            return other && other.getObject() == owner && ctjs::constantKey(other.getKey()) == key;
        });
        if (isClass && ctjs::ordinaryKey(key) && writes == 1 &&
            owner.getDefiningOp<ctjs::CreateClosureOp>()) {
            return {owner, key};
        }
    }
    return {};
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
        if (made) { return targetOf(made); }
        // `C.key(...)` of a static method stored once on class C.
        auto read = plain ? ctjs::receiverMethod(plain) : ctjs::GetPropertyOp{};
        if (!read || read.getObject() != plain.getReceiver()) { return {}; }
        for (mlir::Operation * user : read.getObject().getUsers()) {
            auto store = llvm::dyn_cast<ctjs::SetPropertyOp>(user);
            auto member =
                store && store.getObject() == read.getObject() &&
                        ctjs::constantKey(store.getKey()) == ctjs::constantKey(read.getKey())
                    ? store.getValue().getDefiningOp<ctjs::CreateClosureOp>()
                    : ctjs::CreateClosureOp{};
            if (member && staticSlot(member).first == read.getObject()) { return targetOf(member); }
        }
        return {};
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
            // A static method: stored on its class (and its `__home` edge
            // set), then called as `C.key(...)`.
            auto store = llvm::dyn_cast<ctjs::SetPropertyOp>(use.getOwner());
            const auto [owner, key] = staticSlot(closure);
            if (store && owner &&
                ((use.getOperandNumber() == 2 && store.getObject() == owner) ||
                 (use.getOperandNumber() == 0 && ctjs::constantKey(store.getKey()) == "__home"))) {
                continue;
            }
            return std::nullopt;
        }
        // The calls of a static method through its class.
        const auto [owner, key] = staticSlot(closure);
        if (!owner) { continue; }
        for (mlir::Operation * user : owner.getUsers()) {
            auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(user);
            if (!read || read.getObject() != owner || ctjs::constantKey(read.getKey()) != key) {
                continue;
            }
            for (mlir::OpOperand & use : read.getResult().getUses()) {
                auto call = llvm::dyn_cast<ctjs::CallOp>(use.getOwner());
                if (!call || use.getOperandNumber() != 0 || call.getReceiver() != owner) {
                    return std::nullopt;
                }
                calls.push_back(call);
            }
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

/// The parameter of a known callee that `use`, an argument of a ctjs.call or
/// ctjs.call_direct, fills; null for any other use or a callee whose callers
/// are not all known.
mlir::Value parameterFilledBy(mlir::OpOperand & use, mlir::ModuleOp module,
                              const functionIndex & functions) {
    mlir::Operation * user = use.getOwner();
    if (!llvm::isa<ctjs::CallOp, ctjs::CallDirectOp>(user)) { return {}; }
    const unsigned first = ctjs::callView(user)->args.getBeginOperandIndex();
    ctjs::FuncOp callee = functions.calleeOf(user);
    if (use.getOperandNumber() < first || !callee || !callersOf(callee, module, functions)) {
        return {};
    }
    const unsigned number = use.getOperandNumber() - first + ctjs::implicit_arguments;
    mlir::Block & entry = callee.getBody().front();
    return number < entry.getNumArguments() ? entry.getArgument(number) : mlir::Value{};
}

/// What a value stored into the Map can be: the values it is followed back
/// to through parameters whose callers are all known (`this` values and the
/// parameters on the way), and whether some path ends at anything else.
struct origins {
    llvm::SetVector<mlir::Value> seen;
    bool other = false;
};
origins receiverOrigins(mlir::Value value, mlir::ModuleOp module, const functionIndex & functions) {
    origins out;
    out.seen.insert(value);
    for (std::size_t at = 0; at < out.seen.size(); ++at) {
        const mlir::Value v = out.seen[at];
        if (isReceiver(v)) { continue; }
        auto argument = llvm::dyn_cast<mlir::BlockArgument>(v);
        auto fn = argument ? llvm::dyn_cast<ctjs::FuncOp>(argument.getOwner()->getParentOp())
                           : ctjs::FuncOp{};
        const auto callers = fn && argument.getOwner()->isEntryBlock() &&
                                     argument.getArgNumber() >= ctjs::implicit_arguments
                                 ? callersOf(fn, module, functions)
                                 : std::nullopt;
        if (!callers) {
            out.other = true;
            continue;
        }
        for (mlir::Operation * call : *callers) {
            const mlir::Value actual = actualOf(call, argument.getArgNumber());
            if (actual) {
                out.seen.insert(actual);
            } else {
                out.other = true;
            }
        }
    }
    return out;
}

/// Calls `visit(condition, holds, arm)` for each scf.if around `op`, from the
/// innermost out: `condition` is the value whose truthiness selects the arm
/// `op` sits in (seen through `!`), `holds` whether that arm runs when it is
/// truthy, and `arm` the operation of that arm that contains `op`. Stops when
/// `visit` returns true, and returns whether it did.
template <class Visit> bool anyTest(mlir::Operation * op, Visit visit) {
    for (mlir::Operation * child = op; child && !llvm::isa<ctjs::FuncOp>(child);
         child = child->getParentOp()) {
        auto branch = llvm::dyn_cast_or_null<mlir::scf::IfOp>(child->getParentOp());
        if (!branch) { continue; }
        bool holds = child->getParentRegion() == &branch.getThenRegion();
        mlir::Value condition = branch.getCondition();
        for (;;) {
            if (auto truthy = condition.getDefiningOp<ctjs::TruthyOp>()) {
                condition = truthy.getValue();
            } else if (auto unary = condition.getDefiningOp<ctjs::UnaryOp>();
                       unary && unary.getKind() == ctjs::UnaryKind::Not) {
                holds = !holds;
                condition = unary.getOperand();
            } else {
                break;
            }
        }
        if (visit(condition, holds, child)) { return true; }
    }
    return false;
}

/// Whether `op` runs only where `value` is falsy.
bool falsyAt(mlir::Value value, mlir::Operation * op) {
    return anyTest(op, [&](mlir::Value condition, bool holds, mlir::Operation *) {
        return condition == value && !holds;
    });
}

/// Whether nothing from `from` up to `op`, in one block, can run code or
/// change a Map, except the calls `quiet` accepts.
template <class Quiet> bool quietBefore(mlir::Operation * op, mlir::Operation * from, Quiet quiet) {
    for (mlir::Operation * at = from; at && at != op; at = at->getNextNode()) {
        if (at->getNumRegions() != 0 || llvm::isa<ctjs::SetPropertyOp, ctjs::ConstructOp>(at)) {
            return false;
        }
        if (llvm::isa<ctjs::CallOp, ctjs::CallDirectOp>(at) && !quiet(at)) { return false; }
    }
    return true;
}

/// A `for (const k of Object.getOwnPropertyNames(self)) self[k] = null` loop
/// in `self`'s block, after --ctjs-lift-to-scf: the names call, then the
/// iteration the importer spells with `__ctbrowser_*` helpers, up to the
/// iterator's close. Nothing else may happen in between, and nothing the
/// loop defines may be used after it. Its first and last operation, or none.
std::optional<std::pair<mlir::Operation *, mlir::Operation *>> ownNameNulling(
    mlir::OpOperand & use) {
    auto names = llvm::dyn_cast<ctjs::CallOp>(use.getOwner());
    auto method = names ? ctjs::receiverMethod(names) : ctjs::GetPropertyOp{};
    auto object =
        method ? method.getObject().getDefiningOp<ctjs::LoadGlobalOp>() : ctjs::LoadGlobalOp{};
    if (!object || object.getName() != "Object" ||
        ctjs::methodKey(names) != "getOwnPropertyNames" || names.getArgs().size() != 1 ||
        names.getArgs()[0] != use.get()) {
        return std::nullopt;
    }
    const mlir::Value self = use.get();
    mlir::Operation * last = nullptr;
    for (mlir::Operation * at = names->getNextNode(); at && !last; at = at->getNextNode()) {
        auto call = llvm::dyn_cast<ctjs::CallOp>(at);
        if (call && ctjs::calleeGlobalName(call) == "__ctbrowser_iter_close") { last = at; }
    }
    if (!last) { return std::nullopt; }
    llvm::DenseSet<mlir::Operation *> inside;
    unsigned stores = 0;
    bool fits = true;
    for (mlir::Operation * at = names;; at = at->getNextNode()) {
        at->walk([&](mlir::Operation * op) {
            inside.insert(op);
            const llvm::StringRef dialect = op->getName().getDialectNamespace();
            if (dialect == "arith" || dialect == "ub" || dialect == "scf") { return; }
            if (llvm::isa<ctjs::ConstantOp, ctjs::CompareOp, ctjs::TruthyOp, ctjs::UnaryOp,
                          ctjs::BinaryOp, ctjs::BinaryStaticOp>(op) ||
                op->getName().getStringRef() == "ctjs.iterable") {
                return;
            }
            if (auto global = llvm::dyn_cast<ctjs::LoadGlobalOp>(op)) {
                fits &= global.getName().starts_with("__ctbrowser_");
                return;
            }
            if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(op)) {
                fits &= read.getObject() != self;
                return;
            }
            if (auto call = llvm::dyn_cast<ctjs::CallOp>(op)) {
                fits &= op == names.getOperation() ||
                        ctjs::calleeGlobalName(call).starts_with("__ctbrowser_");
                return;
            }
            if (auto store = llvm::dyn_cast<ctjs::SetPropertyOp>(op)) {
                ++stores;
                fits &= store.getObject() == self && ctjs::constantKey(store.getKey()).empty() &&
                        static_cast<bool>(ctjs::constantAttr<ctjs::NullAttr>(store.getValue()));
                return;
            }
            fits = false;
        });
        if (at == last) { break; }
    }
    if (!fits || stores != 1) { return std::nullopt; }
    for (mlir::Operation * op : inside) {
        for (mlir::Value result : op->getResults()) {
            for (mlir::Operation * user : result.getUsers()) {
                if (!inside.contains(user)) { return std::nullopt; }
            }
        }
    }
    return std::make_pair(names.getOperation(), last);
}

/// The values that name one `new Map()`, the method calls made on them, and
/// the first use that lets the Map escape.
struct mapUses {
    llvm::SetVector<mlir::Value> aliases;
    llvm::SmallVector<ctjs::CallOp> sets, gets, deletes, has;
    // The closures that capture the binding holding the Map.
    llvm::SmallVector<ctjs::CreateClosureOp> captors;
    bool iterated = false;
    std::string escape;
};

/// Whether `iterator`, the result of a Map's `keys()`, is only copied at once
/// by `Array.from`, so no code runs while it is live.
bool copiedAtOnce(mlir::Value iterator) {
    return llvm::all_of(iterator.getUses(), [](mlir::OpOperand & use) {
        auto call = llvm::dyn_cast<ctjs::CallOp>(use.getOwner());
        auto from = call ? ctjs::receiverMethod(call) : ctjs::GetPropertyOp{};
        auto array =
            from ? from.getObject().getDefiningOp<ctjs::LoadGlobalOp>() : ctjs::LoadGlobalOp{};
        return array && array.getName() == "Array" && ctjs::methodKey(call) == "from" &&
               call.getArgs().size() == 1 && use.get() == call.getArgs()[0];
    });
}

/// Follows the values in `roots`, which name Maps, and with `bindings`
/// through the one binding they are stored in and the closures that capture
/// that binding. A binding of a run-once function may be a static variable
/// (ClosureLifting/RunOnce.cpp): one cell per function that uses it, all
/// naming the same symbol. A Map reached any other way, or used other than
/// through the methods the proof models, escapes.
mapUses usesOf(llvm::ArrayRef<mlir::Value> roots, bool bindings, mlir::ModuleOp module,
               const functionIndex & functions) {
    mapUses out;
    out.aliases.insert(roots.begin(), roots.end());
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
                auto binding = store.getCell().getDefiningOp<ctjs::CreateCellOp>();
                if (!binding || !bindings) {
                    escape("the registry Map is stored into a binding this proof cannot follow");
                    continue;
                }
                llvm::SmallVector<mlir::Value> cells{binding.getResult()};
                if (auto symbol = binding->getAttr(kNativeStaticCell)) {
                    // The variable's own cell and one reference cell per
                    // function that reads it, all naming it. A second owner
                    // under the same name would make the references
                    // ambiguous.
                    module.walk([&](ctjs::CreateCellOp other) {
                        if (other == binding || other->getAttr(kNativeStaticCell) != symbol) {
                            return;
                        }
                        if (!other->hasAttr(kNativeStaticReference)) {
                            escape("the registry Map's static variable shares its name with "
                                   "another binding");
                        }
                        cells.push_back(other.getResult());
                    });
                }
                for (const mlir::Value cell : cells) {
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
                        if (!target || cellUse.getOperandNumber() <
                                           made.getUpvalues().getBeginOperandIndex()) {
                            escape("the binding holding the registry Map reaches " + named(reader));
                            continue;
                        }
                        out.captors.push_back(made);
                        const auto upvalues = made.getUpvalues();
                        for (unsigned slot = 0; slot < upvalues.size(); ++slot) {
                            if (upvalues[slot] == cell) { captured(target, slot); }
                        }
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
                out.iterated = out.iterated || !copiedAtOnce(call.getResult());
            } else if (key == "has" && arity == 1) {
                out.has.push_back(call);
            } else {
                escape(
                    ("the registry Map's `" + key + "` is not a method this proof models").str());
            }
        }
    }
    return out;
}

/// Whether `key` names a per-class copy of a parent's constructor that
/// inheritance flattening (ClosureLifting/ClassInheritance.cpp) put on a
/// prototype: `super:<parent>:constructor`.
bool superConstructorKey(llvm::StringRef key) {
    return key.starts_with("super:") && key.ends_with(":constructor");
}

/// The class `fn` builds instances of: the class whose constructor it is,
/// or, for a per-class copy of a parent's constructor, the class whose
/// prototype holds it under a `super:` key. Null otherwise.
ctjs::CreateClosureOp classOfConstructor(ctjs::FuncOp fn, const functionIndex & functions) {
    for (ctjs::CreateClosureOp made : functions.closuresOf(fn)) {
        for (mlir::OpOperand & use : made.getResult().getUses()) {
            auto store = llvm::dyn_cast<ctjs::SetPropertyOp>(use.getOwner());
            if (!store) { continue; }
            const llvm::StringRef key = ctjs::constantKey(store.getKey());
            if (use.getOperandNumber() == 0 && key == "prototype") { return made; }
            if (use.getOperandNumber() != 2 || !superConstructorKey(key)) { continue; }
            // The prototype holding the copy, and the class it belongs to.
            for (mlir::Operation * user : store.getObject().getUsers()) {
                auto attach = llvm::dyn_cast<ctjs::SetPropertyOp>(user);
                if (attach && attach.getValue() == store.getObject() &&
                    ctjs::constantKey(attach.getKey()) == "prototype") {
                    if (auto owner = attach.getObject().getDefiningOp<ctjs::CreateClosureOp>()) {
                        return owner;
                    }
                }
            }
        }
    }
    return {};
}

/// Whether `value` is a class's closure (it has a `prototype`).
bool isClassClosure(mlir::Value value) {
    return value && value.getDefiningOp<ctjs::CreateClosureOp>() &&
           llvm::any_of(value.getUsers(), [&](mlir::Operation * user) {
               auto attach = llvm::dyn_cast<ctjs::SetPropertyOp>(user);
               return attach && attach.getObject() == value &&
                      ctjs::constantKey(attach.getKey()) == "prototype";
           });
}

/// The class a value names when it is the class itself: the closure, the
/// `constructor` of a constructor function's `this`, or the `this` of a
/// static member stored on the class (inheritance flattening copies a
/// parent's statics onto each class, so that `this` is that class).
mlir::Value classNamedBy(mlir::Value value, const functionIndex & functions) {
    if (isClassClosure(value)) { return value; }
    if (auto read = value.getDefiningOp<ctjs::GetPropertyOp>();
        read && ctjs::constantKey(read.getKey()) == "constructor" && isReceiver(read.getObject())) {
        auto fn = llvm::cast<ctjs::FuncOp>(
            llvm::cast<mlir::BlockArgument>(read.getObject()).getOwner()->getParentOp());
        if (auto owner = classOfConstructor(fn, functions)) { return owner.getResult(); }
        return {};
    }
    if (!isReceiver(value)) { return {}; }
    auto fn =
        llvm::cast<ctjs::FuncOp>(llvm::cast<mlir::BlockArgument>(value).getOwner()->getParentOp());
    for (ctjs::CreateClosureOp made : functions.closuresOf(fn)) {
        if (mlir::Value owner = staticSlot(made).first) { return owner; }
        for (mlir::Operation * user : made.getResult().getUsers()) {
            auto accessor = llvm::dyn_cast<ctjs::DefineAccessorOp>(user);
            if (accessor && accessor.getGetter() == made.getResult() &&
                isClassClosure(accessor.getTarget())) {
                return accessor.getTarget();
            }
        }
    }
    return {};
}

/// The value `fn` returns, when it has exactly one `return`.
mlir::Value onlyReturned(ctjs::FuncOp fn) {
    llvm::SmallVector<ctjs::ReturnOp> exits;
    if (fn) {
        fn.walk([&](ctjs::ReturnOp exit) { exits.push_back(exit); });
    }
    return exits.size() == 1 ? exits.front().getValue() : mlir::Value{};
}

/// The string `value` always is, read through constants, concatenation and
/// the static getters and fields of a class (Bootstrap's
/// `static get DATA_KEY() { return `bs.${this.NAME}` }`); none otherwise.
std::optional<std::string> constantString(mlir::Value value, const functionIndex & functions,
                                          unsigned depth = 0) {
    if (depth > 8) { return std::nullopt; }
    if (auto text = ctjs::constantAttr<ctjs::StringAttr>(value)) { return text.getValue().str(); }
    // A static getter read through the class, which the class statics
    // rewrite (ClosureLifting/ClassStatics.cpp) made a direct call.
    if (auto call = value.getDefiningOp<ctjs::CallDirectOp>();
        call && call->hasAttr(kNativeStaticMemberCall)) {
        const mlir::Value returned = onlyReturned(functions.calleeOf(call));
        if (!returned) { return std::nullopt; }
        return constantString(returned, functions, depth + 1);
    }
    if (auto binary = value.getDefiningOp<ctjs::BinaryOp>()) {
        // `+` of two Strings, or the concatenation a template literal is.
        if (binary.getKind() != ctjs::BinaryKind::Add &&
            binary.getKind() != ctjs::BinaryKind::Concat) {
            return std::nullopt;
        }
        auto left = constantString(binary.getLhs(), functions, depth + 1);
        auto right = left ? constantString(binary.getRhs(), functions, depth + 1) : std::nullopt;
        if (!left || !right) { return std::nullopt; }
        return *left + *right;
    }
    auto read = value.getDefiningOp<ctjs::GetPropertyOp>();
    const mlir::Value owner = read ? classNamedBy(read.getObject(), functions) : mlir::Value{};
    const llvm::StringRef key = read ? ctjs::constantKey(read.getKey()) : "";
    if (!owner || key.empty()) { return std::nullopt; }
    std::optional<std::string> found;
    unsigned definitions = 0;
    for (mlir::Operation * user : owner.getUsers()) {
        if (auto accessor = llvm::dyn_cast<ctjs::DefineAccessorOp>(user);
            accessor && accessor.getTarget() == owner && accessor.getName() == key) {
            ++definitions;
            auto getter = accessor.getGetter().getDefiningOp<ctjs::CreateClosureOp>();
            const mlir::Value returned =
                onlyReturned(getter ? functions.targetOf(getter) : ctjs::FuncOp{});
            if (returned) { found = constantString(returned, functions, depth + 1); }
        } else if (auto store = llvm::dyn_cast<ctjs::SetPropertyOp>(user);
                   store && store.getObject() == owner &&
                   ctjs::constantKey(store.getKey()) == key) {
            ++definitions;
            found = constantString(store.getValue(), functions, depth + 1);
        }
    }
    return definitions == 1 ? found : std::nullopt;
}

/// The class family sharing one registry Map: every class some instance of
/// which the Map stores. Each class is proved on its own; these are the
/// facts the proofs share.
struct familyContext {
    /// The `this` of every function that may store into the Map: each
    /// class's constructor and its copies of parent constructors.
    llvm::DenseSet<mlir::Value> selves;
    /// Every method name of every class: a value read back from the Map may
    /// be an instance of any of them.
    llvm::DenseSet<llvm::StringRef> methodNames;
    /// The classes, by their closure, in the order first seen.
    llvm::SmallVector<ctjs::CreateClosureOp> classes;
};

/// The registry proof for one Map whose `set` stores a constructor's `this`.
class registryProof {
    mlir::ModuleOp module;
    const functionIndex & functions;
    const mapUses & map;
    const mapUses * outer; // For a Map of Maps, the outer Map; `map` is then the inner ones.
    const familyContext & family;
    InstanceRegistry & out;
    // The constructor and the copies of parent constructors it runs on the
    // same instance (`super:<parent>:constructor` methods).
    llvm::SmallVector<ctjs::FuncOp> constructors;
    std::string name; // The class's JavaScript name.
    // The constructor's `this` and the parameters it is passed through on its
    // way to the Map's `set`.
    llvm::SetVector<mlir::Value> carriers;
    llvm::SmallVector<ctjs::ConstructOp> constructions;
    // The `new this` of the family's static members once the class statics
    // rewrite marked them (kNativeClassConstruct): each makes an instance
    // the static member may join with one read back from the Map
    // (Bootstrap's `getInstance(t) || new this(t, ...)`) and return. A read
    // may be an instance of any class of the family, so the joins of every
    // class's `new this` are followed.
    llvm::SmallVector<ctjs::ConstructOp> freshInstances;
    llvm::SmallVector<ctjs::FuncOp> methods;
    llvm::DenseSet<llvm::StringRef> methodNames;
    // Whether the constructor or a method captures the Map: the class, read
    // through `constructor`, then reaches the Map.
    bool classCaptures = false;
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
    /// The closure that is the class; collectClass checks there is one.
    mlir::Value classClosure() {
        const auto made = functions.closuresOf(out.constructor);
        if (made.size() != 1) { return {}; }
        ctjs::CreateClosureOp only = made.front();
        return only.getResult();
    }

    /// Whether a static member `fn` of the class constructs `this`, the
    /// class itself, which makes instances no `new` of the closure shows
    /// (`getOrCreateInstance`'s `new this(...)`).
    static bool constructsThis(ctjs::FuncOp fn) {
        if (fn.getBody().empty()) { return false; }
        const mlir::Value self = fn.getBody().front().getArgument(ctjs::arg_receiver);
        return llvm::any_of(self.getUsers(), [&](mlir::Operation * user) {
            auto made = llvm::dyn_cast<ctjs::ConstructOp>(user);
            return made && made.getCallee() == self;
        });
    }

    bool collectClass() {
        const auto made = functions.closuresOf(out.constructor);
        if (made.size() != 1) {
            return refuse("the class `" + name + "` is not created exactly once");
        }
        ctjs::CreateClosureOp only = made.front();
        methodNames.insert(family.methodNames.begin(), family.methodNames.end());
        classCaptures = llvm::is_contained(map.captors, only);
        const mlir::Value closure = only.getResult();
        mlir::Value prototype;
        for (mlir::OpOperand & use : closure.getUses()) {
            mlir::Operation * user = use.getOwner();
            auto store = llvm::dyn_cast<ctjs::SetPropertyOp>(user);
            if (store && use.getOperandNumber() == 0) {
                if (ctjs::constantKey(store.getKey()) == "prototype") {
                    prototype = store.getValue();
                } else if (auto member = store.getValue().getDefiningOp<ctjs::CreateClosureOp>()) {
                    ctjs::FuncOp target = functions.targetOf(member);
                    if (target && constructsThis(target)) {
                        return refuse("the static method `" +
                                      ctjs::constantKey(store.getKey()).str() + "` of `" + name +
                                      "` constructs `this`, which this proof does not follow yet");
                    }
                }
                continue;
            }
            if (store && use.getOperandNumber() == 2 &&
                ctjs::constantKey(store.getKey()) == "constructor") {
                continue; // The prototype's `constructor` slot, checked below.
            }
            // Static members: a static method's `__home`, a static accessor,
            // and reads and calls of them on the class. None makes an
            // instance unless it constructs `this`, checked below.
            if (store && use.getOperandNumber() == 2 &&
                ctjs::constantKey(store.getKey()) == "__home") {
                continue;
            }
            if (auto accessor = llvm::dyn_cast<ctjs::DefineAccessorOp>(user);
                accessor && use.getOperandNumber() == 0) {
                for (mlir::Value side : {accessor.getGetter(), accessor.getSetter()}) {
                    auto member = side.getDefiningOp<ctjs::CreateClosureOp>();
                    ctjs::FuncOp target = member ? functions.targetOf(member) : ctjs::FuncOp{};
                    if (target && constructsThis(target)) {
                        return refuse("a static member of `" + name +
                                      "` constructs `this`, which this proof does not follow "
                                      "yet");
                    }
                }
                continue;
            }
            if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(user);
                read && use.getOperandNumber() == 0 && !ctjs::constantKey(read.getKey()).empty() &&
                ctjs::constantKey(read.getKey()) != "prototype") {
                continue;
            }
            if (auto call = llvm::dyn_cast<ctjs::CallOp>(user);
                call && use.getOperandNumber() == 1) {
                auto method = ctjs::receiverMethod(call);
                if (method && method.getObject() == closure) { continue; }
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
            auto heritage = llvm::dyn_cast<ctjs::CallOp>(user);
            if (llvm::isa<ctjs::CellSetOp>(user) ||
                (heritage && ctjs::calleeGlobalName(heritage) == "__ctbrowser_class_heritage")) {
                // Bootstrap's BaseComponent: subclasses extend it and static
                // methods construct it through the binding.
                return refuse("the class `" + name +
                              "` is extended, or held in a binding through which subclasses "
                              "and static methods reach it - a class family this proof does not "
                              "follow yet");
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
            if (superConstructorKey(key)) { constructors.push_back(target); }
            classCaptures = classCaptures || llvm::is_contained(map.captors, method);
            methodNames.insert(key);
        }
        llvm::DenseSet<llvm::StringRef> classes;
        for (ctjs::CreateClosureOp owner : family.classes) {
            if (ctjs::FuncOp constructor = functions.targetOf(owner)) {
                classes.insert(constructor.getSymName());
            }
        }
        module.walk([&](ctjs::ConstructOp made) {
            auto mark = made->getAttrOfType<mlir::StringAttr>(kNativeClassConstruct);
            if (mark && classes.contains(mark.getValue())) { freshInstances.push_back(made); }
        });
        return true;
    }

    /// `this` in the constructor: constant-key field accesses and the value
    /// of the registry `set` only. In a method: field accesses and method
    /// calls only.
    bool checkReceivers() {
        // The `this` of the constructor and of each parent constructor copy
        // it runs on the instance: these may also hand `this` to the Map.
        llvm::DenseSet<mlir::Value> selves;
        for (ctjs::FuncOp constructor : constructors) {
            const mlir::Value self = constructor.getBody().front().getArgument(ctjs::arg_receiver);
            selves.insert(self);
            receivers.insert(self);
            carriers.insert(self);
        }
        for (const mlir::Value carrier : carriers) {
            const bool self = selves.contains(carrier);
            for (mlir::OpOperand & use : carrier.getUses()) {
                if (self && (instanceUse(use) || heldByArrows(use))) { continue; }
                if (llvm::is_contained(map.sets, use.getOwner()) && use.getOperandNumber() == 3) {
                    continue;
                }
                if (carriers.contains(parameterFilledBy(use, module, functions))) { continue; }
                return refuse(self ? "`this` in the constructor of `" + name + "` also reaches " +
                                         named(use.getOwner())
                                   : "`this` of `" + name + "`, passed on as a parameter of `" +
                                         jsName(use.getOwner()->getParentOfType<ctjs::FuncOp>()) +
                                         "`, also reaches " + named(use.getOwner()));
            }
        }
        for (ctjs::FuncOp method : methods) {
            if (llvm::is_contained(constructors, method)) { continue; }
            const mlir::Value self = method.getBody().front().getArgument(ctjs::arg_receiver);
            receivers.insert(self);
            // The nulling loops first: a loop's own store is another use.
            for (mlir::OpOperand & use : self.getUses()) {
                if (auto loop = ownNameNulling(use)) { out.ownNameNullings.push_back(*loop); }
            }
            for (mlir::OpOperand & use : self.getUses()) {
                if (inNulling(use.getOwner())) { continue; }
                if (!instanceUse(use) && !heldByArrows(use)) {
                    return refuse("`this` in the method `" + jsName(method) + "` of `" + name +
                                  "` reaches " + named(use.getOwner()));
                }
            }
        }
        for (ctjs::ConstructOp made : constructions) {
            receivers.insert(made.getResult());
            for (mlir::OpOperand & use : made.getResult().getUses()) {
                if (!instanceUse(use)) {
                    return refuse("a new `" + name + "` reaches " + named(use.getOwner()) +
                                  " - only its fields and methods may use it");
                }
            }
        }
        return true;
    }

    /// Whether `use` of an instance's `this` boxes it in the cell
    /// captureLexicalThis makes for an arrow (ClosureLifting/ArrowThis.cpp),
    /// and every arrow capturing that cell only reads fields and calls
    /// methods through it, or boxes it again for an arrow of its own that
    /// does. Such an arrow may outlive the call that made it, and then holds
    /// the instance through its capture, the registry's std::shared_ptr. It
    /// writes no field, which collectFields would not see, and hands `this`
    /// to no call, which follow() would have to check.
    bool heldByArrows(mlir::OpOperand & use, unsigned depth = 0) {
        auto cell = llvm::dyn_cast<ctjs::CreateCellOp>(use.getOwner());
        if (!cell || !cell->hasAttr(kNativeLexicalThis) || depth > 8) { return false; }
        for (mlir::OpOperand & capture : cell.getResult().getUses()) {
            auto made = llvm::dyn_cast<ctjs::CreateClosureOp>(capture.getOwner());
            ctjs::FuncOp arrow = made ? functions.targetOf(made) : ctjs::FuncOp{};
            auto slot = arrow ? arrow->getAttrOfType<mlir::IntegerAttr>(kNativeLexicalThis)
                              : mlir::IntegerAttr{};
            if (!slot || capture.getOperandNumber() !=
                             made.getUpvalues().getBeginOperandIndex() + slot.getInt()) {
                return false;
            }
            const mlir::Value callee = arrow.getBody().front().getArgument(ctjs::arg_callee);
            bool reads = true;
            arrow.walk([&](ctjs::LoadUpvalueOp read) {
                if (!reads || read.getClosure() != callee || read.getIndex() != slot.getInt()) {
                    return;
                }
                // instanceUse admits a store only through a receiver, which
                // this read is not.
                for (mlir::OpOperand & inner : read.getResult().getUses()) {
                    reads = reads && (instanceUse(inner) || heldByArrows(inner, depth + 1));
                }
            });
            if (!reads) { return false; }
        }
        out.heldThis.push_back(cell);
        return true;
    }

    /// Whether `op` sits in a nulling loop already recorded.
    bool inNulling(mlir::Operation * op) {
        return llvm::any_of(out.ownNameNullings, [&](const auto & loop) {
            for (mlir::Operation * at = loop.first;; at = at->getNextNode()) {
                if (at->isAncestor(op)) { return true; }
                if (at == loop.second) { return false; }
            }
        });
    }

    /// The fields every instance has: those the constructor writes through
    /// `this` at the top of its body, before any call. A nulling loop sets
    /// own properties, so every field the class ever writes must be one.
    bool collectFields() {
        mlir::Block & entry = out.constructor.getBody().front();
        const mlir::Value self = entry.getArgument(ctjs::arg_receiver);
        for (mlir::Operation & op : entry) {
            if (llvm::isa<ctjs::CallOp, ctjs::CallDirectOp, ctjs::ConstructOp>(op) ||
                op.getNumRegions() != 0) {
                break;
            }
            auto store = llvm::dyn_cast<ctjs::SetPropertyOp>(op);
            const std::string key = store && store.getObject() == self
                                        ? ctjs::constantKey(store.getKey()).str()
                                        : std::string{};
            if (!key.empty() && !llvm::is_contained(out.fields, key)) { out.fields.push_back(key); }
        }
        if (out.ownNameNullings.empty()) { return true; }
        llvm::SmallVector<mlir::Value> selves{self};
        for (ctjs::FuncOp method : methods) {
            selves.push_back(method.getBody().front().getArgument(ctjs::arg_receiver));
        }
        for (mlir::Value receiver : selves) {
            for (mlir::Operation * user : receiver.getUsers()) {
                auto store = llvm::dyn_cast<ctjs::SetPropertyOp>(user);
                const llvm::StringRef key = store ? ctjs::constantKey(store.getKey()) : "";
                if (store && store.getObject() == receiver && !key.empty() &&
                    !llvm::is_contained(out.fields, key)) {
                    return refuse(("the own-property loop of `" + name + "` would miss `" + key +
                                   "`, a field its constructor may not have written yet")
                                      .str());
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
            if (instanceUse(use) || llvm::isa<ctjs::TruthyOp>(user)) { continue; }
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
            if (llvm::isa<ctjs::SetPropertyOp>(user) && use.getOperandNumber() == 0) {
                return refuse("a field of an instance read from the registry Map is written "
                              "outside its class's constructor and methods");
            }
            if (llvm::isa<ctjs::SetPropertyOp, ctjs::ConstructOp>(user)) {
                return refuse("a value read from the registry Map is stored into an object, so "
                              "shared ownership could form a cycle no destructor breaks");
            }
            return refuse("a value read from the registry Map reaches " + named(user));
        }
        return true;
    }

    /// The falsy yields whose join only truthiness reads: every use of the
    /// joined value is a truthiness test, or a yield where that test holds.
    void collectFalsyYields() {
        for (mlir::Value value : maybe) {
            const bool truthinessOnly = llvm::all_of(value.getUses(), [&](mlir::OpOperand & use) {
                mlir::Operation * user = use.getOwner();
                return llvm::isa<ctjs::TruthyOp>(user) ||
                       (llvm::isa<mlir::scf::YieldOp>(user) && guardedAt(value, user, nullptr));
            });
            if (!truthinessOnly) { continue; }
            for (const source & from : sources.lookup(value)) {
                auto yield = llvm::dyn_cast_or_null<mlir::scf::YieldOp>(from.site);
                if (!yield || !from.value || nullishBits(from.value) ||
                    maybe.contains(from.value)) {
                    continue;
                }
                const auto index = llvm::cast<mlir::OpResult>(value).getResultNumber();
                out.falsyYields.emplace_back(yield, index);
            }
        }
    }

    /// Whether every value joined into an instance is an instance, null or
    /// undefined.
    bool checkJoins() {
        for (mlir::Value value : maybe) {
            for (const source & from : sources.lookup(value)) {
                if (!from.value || nullishBits(from.value) || maybe.contains(from.value) ||
                    falsyAt(from.value, from.site)) {
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
                    } else if (guardedAt(from.value, from.site, reference)) {
                        // Checked there: an instance.
                    } else if (!maybe.contains(from.value) && !nullishBits(from.value)) {
                        joined |= mayBeFalsy; // Falsy at its site (checkJoins).
                    } else {
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
        return anyTest(op, [&](mlir::Value condition, bool holds, mlir::Operation *) {
            if (condition == value) { return holds; }
            auto compare = condition.getDefiningOp<ctjs::CompareOp>();
            if (!compare || holds) { return false; } // `x == null` holds an instance when false.
            const mlir::Value other = compare.getLhs() == value   ? compare.getRhs()
                                      : compare.getRhs() == value ? compare.getLhs()
                                                                  : mlir::Value{};
            const unsigned against = other ? nullishBits(other) : 0;
            if (!against || !reference) { return false; }
            const unsigned may = reference->lookup(value);
            if (compare.getKind() == ctjs::CompareKind::Eq) { return (may & mayBeFalsy) == 0; }
            return compare.getKind() == ctjs::CompareKind::StrictEq && (may & ~against) == 0;
        });
    }

    /// Whether `use` reaches an instance only through its own fields and its
    /// class's methods: a method call through any other key could run a
    /// function that keeps `this`.
    /// A method read from an instance must be called on it at once, and its
    /// class read only when no class function captures the Map: either
    /// function held as a value could own the Map (see checkCaptors).
    bool instanceUse(mlir::OpOperand & use) {
        if (!dereferences(use)) { return false; }
        // Only the class's own code writes its fields, so the native type
        // of each field is the join of the stores through `this`.
        if (llvm::isa<ctjs::SetPropertyOp>(use.getOwner())) { return isReceiver(use.get()); }
        if (auto call = llvm::dyn_cast<ctjs::CallOp>(use.getOwner())) {
            return methodNames.contains(ctjs::methodKey(call));
        }
        auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(use.getOwner());
        const llvm::StringRef key = read ? ctjs::constantKey(read.getKey()) : "";
        if (key == "constructor") { return !classCaptures; }
        if (!methodNames.contains(key)) { return true; }
        return llvm::all_of(read.getResult().getUses(), [&](mlir::OpOperand & callee) {
            auto call = llvm::dyn_cast<ctjs::CallOp>(callee.getOwner());
            return call && callee.getOperandNumber() == 0 && call.getReceiver() == use.get();
        });
    }

    /// Whether `call`, an outer `has` or `get`, asks about `key`.
    bool asks(mlir::Operation * call, llvm::ArrayRef<ctjs::CallOp> calls, mlir::Value key) {
        auto found = llvm::find(calls, call);
        if (found == calls.end()) { return false; }
        ctjs::CallOp asked = *found;
        return asked.getArgs()[0] == key;
    }

    /// Whether `get`, a read of the outer Map, finds an inner Map: it runs
    /// where `has` of the same key held, or right after the entry was made
    /// when missing (`has(k) || set(k, new Map())`), with nothing between
    /// that could remove it.
    bool presentAt(ctjs::CallOp get) {
        const mlir::Value key = get.getArgs()[0];
        const auto quiet = [&](mlir::Operation * call) {
            return llvm::is_contained(outer->has, call) || llvm::is_contained(outer->gets, call);
        };
        // Under `if (outer.has(key))`, from the start of that arm.
        mlir::Block * block = get->getBlock();
        const bool tested =
            anyTest(get, [&](mlir::Value condition, bool holds, mlir::Operation * arm) {
                return holds && arm->getBlock() == block &&
                       asks(condition.getDefiningOp(), outer->has, key);
            });
        if (tested && quietBefore(get, &block->front(), quiet)) { return true; }
        // After `if (!outer.has(key)) outer.set(key, new Map())`.
        for (mlir::Operation * at = get->getPrevNode(); at; at = at->getPrevNode()) {
            auto branch = llvm::dyn_cast<mlir::scf::IfOp>(at);
            if (!branch) { continue; }
            // The condition, seen through `!`; `missing` is the arm that runs
            // when `has` answered false.
            mlir::Value condition = branch.getCondition();
            bool flipped = false;
            for (;;) {
                if (auto truthy = condition.getDefiningOp<ctjs::TruthyOp>()) {
                    condition = truthy.getValue();
                } else if (auto unary = condition.getDefiningOp<ctjs::UnaryOp>();
                           unary && unary.getKind() == ctjs::UnaryKind::Not) {
                    flipped = !flipped;
                    condition = unary.getOperand();
                } else {
                    break;
                }
            }
            mlir::Region & missing = flipped ? branch.getThenRegion() : branch.getElseRegion();
            mlir::Region & found = flipped ? branch.getElseRegion() : branch.getThenRegion();
            bool made = false;
            missing.walk([&](ctjs::CallOp set) { made = made || asks(set, outer->sets, key); });
            const bool foundEmpty = found.empty() || found.front().getOperations().size() == 1;
            return made && foundEmpty && asks(condition.getDefiningOp(), outer->has, key) &&
                   quietBefore(get, branch->getNextNode(), quiet);
        }
        return false;
    }

    /// The outer Map of a Map of Maps: it does not escape, is not iterated
    /// while entries can go, and every inner Map read from it is present.
    bool checkOuter() {
        if (!outer) { return true; }
        if (outer->iterated && !outer->deletes.empty()) {
            return refuse("the outer registry Map is iterated, and `delete` removes entries "
                          "from it - a removal during the iteration changes what it visits");
        }
        for (ctjs::CallOp get : outer->gets) {
            if (!presentAt(get)) {
                return refuse("an inner Map is read from the outer registry Map where `has` "
                              "did not just find or make it - `get` answers undefined for a "
                              "missing key");
            }
        }
        return true;
    }

    /// Whether every function that captures the Map's binding is only called,
    /// constructed or kept as a class method. Such a function held as a value
    /// would let whatever holds it own the Map, and through the Map the
    /// instances: if an instance held it, ownership would form a cycle.
    bool checkCaptors() {
        for (ctjs::CreateClosureOp made : map.captors) {
            const mlir::Value closure = made.getResult();
            for (mlir::OpOperand & use : closure.getUses()) {
                mlir::Operation * user = use.getOwner();
                const unsigned at = use.getOperandNumber();
                if (llvm::isa<ctjs::CallOp>(user) &&
                    (at == 0 || ctjs::calleeGlobalName(llvm::cast<ctjs::CallOp>(user)) ==
                                    "__ctbrowser_class_defined")) {
                    continue;
                }
                if (auto direct = llvm::dyn_cast<ctjs::CallDirectOp>(user);
                    direct && closure == direct.getCalleeValue() && at == ctjs::arg_callee) {
                    continue;
                }
                if (llvm::isa<ctjs::ConstructOp>(user) && at <= 1) { continue; }
                auto store = llvm::dyn_cast<ctjs::SetPropertyOp>(user);
                if (store && at == 0) { continue; }
                if (store && at == 2 && isPrototype(store.getObject())) { continue; }
                return refuse("a function that captures the registry Map is used at " +
                              named(user) +
                              " as a value, so an instance holding it could own the Map - a "
                              "cycle no destructor breaks");
            }
        }
        return true;
    }

    /// Whether `object` is the `prototype` of some class.
    static bool isPrototype(mlir::Value object) {
        if (!object.getDefiningOp<ctjs::CreateObjectOp>()) { return false; }
        return llvm::any_of(object.getUsers(), [&](mlir::Operation * user) {
            auto store = llvm::dyn_cast<ctjs::SetPropertyOp>(user);
            return store && store.getValue() == object &&
                   ctjs::constantKey(store.getKey()) == "prototype";
        });
    }

public:
    registryProof(mlir::ModuleOp module, const functionIndex & functions, const mapUses & map,
                  const mapUses * outer, const familyContext & family, InstanceRegistry & out)
        : module(module), functions(functions), map(map), outer(outer), family(family), out(out),
          name(jsName(out.constructor)) {
        constructors.push_back(out.constructor);
    }

    bool run() {
        if (!map.escape.empty()) { return refuse(map.escape); }
        for (ctjs::CallOp set : map.sets) {
            const origins from = receiverOrigins(set.getArgs()[1], module, functions);
            const bool only = !from.other && llvm::all_of(from.seen, [&](mlir::Value v) {
                return !isReceiver(v) || family.selves.contains(v);
            });
            if (!only) {
                return refuse("the registry Map also holds a value that is not a new `" + name +
                              "`");
            }
            // This class's own selves and the parameters on the way; another
            // class's `this` is that class's proof's to check.
            for (mlir::Value v : from.seen) {
                if (!isReceiver(v) ||
                    classOfConstructor(
                        llvm::cast<ctjs::FuncOp>(
                            llvm::cast<mlir::BlockArgument>(v).getOwner()->getParentOp()),
                        functions)
                            .getResult() == classClosure()) {
                    carriers.insert(v);
                }
            }
        }
        if (map.iterated && !map.deletes.empty()) {
            return refuse("the registry Map is iterated, and `delete` removes entries from it "
                          "- a removal during the iteration changes what it visits");
        }
        if (!checkOuter() || !collectClass() || !checkReceivers() || !collectFields()) {
            return false;
        }
        // Instances read back: every flow followed, every dereference checked.
        // A fresh instance of a static member's `new this` is followed the
        // same way; it is never null or undefined, so its own dereferences
        // need no check.
        for (ctjs::CallOp get : map.gets) { maybe.insert(get.getResult()); }
        for (ctjs::ConstructOp made : freshInstances) { maybe.insert(made.getResult()); }
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
        // Acyclic. Every value that can hold an instance is followed above
        // and none is stored into an object, so no field reaches an instance
        // but through the Map; and neither the Map nor a function capturing
        // it can be stored anywhere. So no field of an instance can reach the
        // Map or an instance, whatever else it holds.
        if (!checkCaptors()) { return false; }
        collectFalsyYields();
        for (ctjs::ConstructOp made : constructions) { out.instances.push_back(made.getResult()); }
        for (ctjs::ConstructOp made : freshInstances) {
            if (made->getAttrOfType<mlir::StringAttr>(kNativeClassConstruct).getValue() ==
                out.constructor.getSymName()) {
                out.instances.push_back(made.getResult());
            }
        }
        out.methods = methods;
        llvm::append_range(out.readBack, maybe);
        // The tests against null or undefined that a null pointer answers
        // exactly: a loose one on a value that is an instance, null or
        // undefined, or a strict one on a value that can only be the nullish
        // value it names.
        for (mlir::Value value : maybe) {
            for (mlir::Operation * user : value.getUsers()) {
                auto compare = llvm::dyn_cast<ctjs::CompareOp>(user);
                if (!compare) { continue; }
                const mlir::Value other =
                    compare.getLhs() == value ? compare.getRhs() : compare.getLhs();
                // A falsy non-nullish value (mayBeFalsy) is also a null
                // pointer, and no comparison with null answers true for it.
                const unsigned against = nullishBits(other);
                const unsigned may = nullish.lookup(value);
                if (compare.getKind() == ctjs::CompareKind::Eq ? (may & mayBeFalsy) == 0
                                                               : (may & ~against) == 0) {
                    out.exactTests.push_back(compare);
                }
            }
        }
        return true;
    }
};

/// The fresh `new Map()` each `set` of `outer` stores, when every one stores
/// such a Map used for nothing else: the inner Maps of a Map of Maps.
std::optional<llvm::SmallVector<mlir::Value>> innerMaps(const mapUses & outer) {
    llvm::SmallVector<mlir::Value> made;
    for (ctjs::CallOp set : outer.sets) {
        auto inner = set.getArgs()[1].getDefiningOp<ctjs::ConstructOp>();
        if (!inner || ctjs::calleeGlobalName(inner) != "Map" || !inner.getArgs().empty() ||
            !inner.getResult().hasOneUse()) {
            return std::nullopt;
        }
        made.push_back(inner.getResult());
    }
    if (made.empty()) { return std::nullopt; }
    return made;
}

} // namespace

/// The key a family's class registers under and the reads each class's key
/// selects (InstanceRegistry::key, keyedReads). Each `set` of the Map stores
/// a `this` passed with its key into a helper (Bootstrap's
/// `Data.set(element, this.constructor.DATA_KEY, this)`), or directly; each
/// key must be one constant string per class, and no two classes may share
/// one, or a read could not know which class it finds.
static void keyFamily(llvm::MutableArrayRef<InstanceRegistry> family, const mapUses & map,
                      mlir::ModuleOp module, const functionIndex & functions) {
    const auto refuseAll = [&](const std::string & why) {
        for (InstanceRegistry & registry : family) { registry.refusal = why; }
    };
    // The class whose constructor function's `this` is `value`, by name.
    const auto classOf = [&](mlir::Value value) -> InstanceRegistry * {
        auto argument = llvm::dyn_cast<mlir::BlockArgument>(value);
        auto fn = argument ? llvm::dyn_cast<ctjs::FuncOp>(argument.getOwner()->getParentOp())
                           : ctjs::FuncOp{};
        ctjs::CreateClosureOp owner =
            fn ? classOfConstructor(fn, functions) : ctjs::CreateClosureOp{};
        for (InstanceRegistry & registry : family) {
            if (owner && functions.targetOf(owner) == registry.constructor) { return &registry; }
        }
        return nullptr;
    };
    // The pairs (key, value) that reach each `set`, through one helper's
    // parameters when the set stores its parameters.
    const auto parameterIndex = [](mlir::Value value) -> std::optional<unsigned> {
        auto argument = llvm::dyn_cast<mlir::BlockArgument>(value);
        if (!argument || !argument.getOwner()->isEntryBlock() ||
            argument.getArgNumber() < ctjs::implicit_arguments) {
            return std::nullopt;
        }
        return argument.getArgNumber();
    };
    for (ctjs::CallOp set : map.sets) {
        llvm::SmallVector<std::pair<mlir::Value, mlir::Value>> pairs;
        const mlir::Value key = set.getArgs()[0], value = set.getArgs()[1];
        const auto keyAt = parameterIndex(key), valueAt = parameterIndex(value);
        auto helper = set->getParentOfType<ctjs::FuncOp>();
        if (keyAt && valueAt) {
            const auto callers = callersOf(helper, module, functions);
            if (!callers) {
                return refuseAll("the registry Map's key comes from a helper whose callers are "
                                 "not all known");
            }
            for (mlir::Operation * call : *callers) {
                pairs.emplace_back(actualOf(call, *keyAt), actualOf(call, *valueAt));
            }
        } else {
            pairs.emplace_back(key, value);
        }
        for (const auto & [actualKey, actualValue] : pairs) {
            InstanceRegistry * owner = actualValue ? classOf(actualValue) : nullptr;
            // A class nothing constructs (a parent whose subclasses run their
            // own copies of its constructor) never registers anything.
            if (!owner && actualValue && isReceiver(actualValue)) {
                auto fn = llvm::cast<ctjs::FuncOp>(
                    llvm::cast<mlir::BlockArgument>(actualValue).getOwner()->getParentOp());
                ctjs::CreateClosureOp unused = classOfConstructor(fn, functions);
                if (unused && llvm::none_of(unused.getResult().getUsers(), [](mlir::Operation * u) {
                        return llvm::isa<ctjs::ConstructOp>(u);
                    })) {
                    continue;
                }
            }
            const auto text = actualKey ? constantString(actualKey, functions) : std::nullopt;
            if (!owner || !text) {
                return refuseAll("a class of the registry Map's family registers under a key "
                                 "that is not one constant string");
            }
            if (!owner->key.empty() && owner->key != *text) {
                return refuseAll("the class `" + jsName(owner->constructor) +
                                 "` registers under two keys");
            }
            owner->key = *text;
        }
    }
    for (InstanceRegistry & registry : family) {
        for (InstanceRegistry & other : family) {
            if (&other != &registry && !registry.key.empty() && other.key == registry.key) {
                return refuseAll("the classes `" + jsName(registry.constructor) + "` and `" +
                                 jsName(other.constructor) + "` both register under the key `" +
                                 registry.key + "`, so a read cannot know which it finds");
            }
        }
    }
    // The reads: a `get` whose key is a helper's parameter, at each call of
    // the helper with a constant key.
    for (ctjs::CallOp get : map.gets) {
        const auto keyAt = parameterIndex(get.getArgs()[0]);
        auto helper = get->getParentOfType<ctjs::FuncOp>();
        const auto callers = keyAt ? callersOf(helper, module, functions) : std::nullopt;
        if (!callers) { continue; }
        for (mlir::Operation * call : *callers) {
            const mlir::Value actual = actualOf(call, *keyAt);
            const auto text = actual ? constantString(actual, functions) : std::nullopt;
            for (InstanceRegistry & registry : family) {
                if (text && registry.key == *text) { registry.keyedReads.push_back(call); }
            }
        }
    }
}

std::vector<InstanceRegistry> proveInstanceRegistries(mlir::ModuleOp module) {
    const functionIndex functions(module);
    std::vector<InstanceRegistry> out;
    module.walk([&](ctjs::ConstructOp made) {
        if (ctjs::calleeGlobalName(made) != "Map" || !made.getArgs().empty()) { return; }
        mapUses uses = usesOf({made.getResult()}, /*bindings=*/true, module, functions);
        // A Map of Maps: the instances live in the inner Maps, which are the
        // values read back from the outer one.
        std::optional<mapUses> outer;
        llvm::SmallVector<mlir::Value> inner;
        if (auto fresh = innerMaps(uses)) {
            inner = std::move(*fresh);
            llvm::SmallVector<mlir::Value> reads;
            for (ctjs::CallOp get : uses.gets) { reads.push_back(get.getResult()); }
            mapUses values = usesOf(reads, /*bindings=*/false, module, functions);
            values.aliases.insert(uses.aliases.begin(), uses.aliases.end());
            values.aliases.insert(inner.begin(), inner.end());
            values.captors = uses.captors;
            if (values.escape.empty()) { values.escape = uses.escape; }
            outer = std::move(uses);
            uses = std::move(values);
        }
        // A registry Map stores the `this` of some function: one class's
        // constructor, or, for a family of classes, each class's own
        // constructor or its copy of their parent's (inheritance flattening).
        familyContext family;
        llvm::SmallVector<ctjs::FuncOp> unknown;
        for (ctjs::CallOp set : uses.sets) {
            const origins from = receiverOrigins(set.getArgs()[1], module, functions);
            for (mlir::Value v : from.seen) {
                if (!isReceiver(v)) { continue; }
                auto fn = llvm::cast<ctjs::FuncOp>(
                    llvm::cast<mlir::BlockArgument>(v).getOwner()->getParentOp());
                family.selves.insert(v);
                ctjs::CreateClosureOp owner = classOfConstructor(fn, functions);
                if (!owner) {
                    unknown.push_back(fn);
                } else if (!llvm::is_contained(family.classes, owner)) {
                    family.classes.push_back(owner);
                }
            }
        }
        if (family.selves.empty()) { return; }
        // A class nothing constructs has no instances; the copies of its
        // constructor its subclasses run belong to them.
        // A class is constructed by a `new` of its closure or by a static
        // member's `new this` (which the class's proof then refuses by name).
        // A static member's `new this` that the class statics rewrite turned
        // into a marked construct of the class also constructs it.
        llvm::DenseSet<llvm::StringRef> classConstructs;
        module.walk([&](ctjs::ConstructOp made) {
            if (auto mark = made->getAttrOfType<mlir::StringAttr>(kNativeClassConstruct)) {
                classConstructs.insert(mark.getValue());
            }
        });
        const auto constructed = [&](ctjs::CreateClosureOp owner) {
            ctjs::FuncOp constructor = functions.targetOf(owner);
            if (constructor && classConstructs.contains(constructor.getSymName())) { return true; }
            return llvm::any_of(owner.getResult().getUsers(), [&](mlir::Operation * user) {
                if (auto made = llvm::dyn_cast<ctjs::ConstructOp>(user)) {
                    return made.getCallee() == owner.getResult();
                }
                mlir::Value member;
                if (auto store = llvm::dyn_cast<ctjs::SetPropertyOp>(user);
                    store && store.getObject() == owner.getResult()) {
                    member = store.getValue();
                } else if (auto accessor = llvm::dyn_cast<ctjs::DefineAccessorOp>(user);
                           accessor && accessor.getTarget() == owner.getResult()) {
                    member = accessor.getGetter();
                }
                auto closure = member ? member.getDefiningOp<ctjs::CreateClosureOp>()
                                      : ctjs::CreateClosureOp{};
                ctjs::FuncOp target = closure ? functions.targetOf(closure) : ctjs::FuncOp{};
                if (!target || target.getBody().empty()) { return false; }
                const mlir::Value self = target.getBody().front().getArgument(ctjs::arg_receiver);
                return llvm::any_of(self.getUsers(), [&](mlir::Operation * use) {
                    auto made = llvm::dyn_cast<ctjs::ConstructOp>(use);
                    return made && made.getCallee() == self;
                });
            });
        };
        llvm::SmallVector<ctjs::CreateClosureOp> live;
        for (ctjs::CreateClosureOp owner : family.classes) {
            if (constructed(owner)) { live.push_back(owner); }
        }
        // Every method name of every class, for the reads that may be any.
        for (ctjs::CreateClosureOp owner : live) {
            for (mlir::Operation * user : owner.getResult().getUsers()) {
                auto attach = llvm::dyn_cast<ctjs::SetPropertyOp>(user);
                if (!attach || attach.getObject() != owner.getResult() ||
                    ctjs::constantKey(attach.getKey()) != "prototype") {
                    continue;
                }
                for (mlir::Operation * member : attach.getValue().getUsers()) {
                    auto store = llvm::dyn_cast<ctjs::SetPropertyOp>(member);
                    if (store && store.getObject() == attach.getValue() &&
                        store.getValue().getDefiningOp<ctjs::CreateClosureOp>()) {
                        family.methodNames.insert(ctjs::constantKey(store.getKey()));
                    }
                }
            }
        }
        const auto record = [&](ctjs::FuncOp constructor) -> InstanceRegistry & {
            InstanceRegistry & proof = out.emplace_back();
            proof.map = made;
            for (mlir::Value map : inner) {
                proof.innerMaps.push_back(map.getDefiningOp<ctjs::ConstructOp>());
            }
            proof.constructor = constructor;
            return proof;
        };
        if (!unknown.empty()) {
            record(unknown.front()).refusal =
                "the registry Map holds the `this` of `" + jsName(unknown.front()) +
                "`, which is neither a class's constructor nor a class's copy of its parent's";
            return;
        }
        const std::size_t first = out.size();
        for (ctjs::CreateClosureOp owner : live) {
            ctjs::FuncOp constructor = functions.targetOf(owner);
            if (!constructor) { continue; }
            registryProof(module, functions, uses, outer ? &*outer : nullptr, family,
                          record(constructor))
                .run();
        }
        // One Map: if any class of the family is refused, so is every other.
        std::string refused;
        for (std::size_t at = first; at < out.size() && refused.empty(); ++at) {
            if (!out[at].refusal.empty()) {
                refused = "`" + jsName(out[at].constructor) + "`: " + out[at].refusal;
            }
        }
        if (out.size() - first > 1 && !refused.empty()) {
            for (std::size_t at = first; at < out.size(); ++at) {
                if (out[at].refusal.empty()) {
                    out[at].refusal = "it shares the registry Map with " + refused;
                }
            }
        }
        if (out.size() - first > 1 && refused.empty()) {
            keyFamily(llvm::MutableArrayRef(out).drop_front(first), uses, module, functions);
        }
    });
    return out;
}

std::vector<InstanceRegistry> markInstanceRegistries(mlir::ModuleOp module) {
    module.walk([](mlir::Operation * op) {
        op->removeAttr(kNativeInstanceClass);
        op->removeAttr(kNativeInstanceTest);
        op->removeAttr(kNativeInstanceMethod);
        op->removeAttr(kNativeInstanceKeyClass);
        op->removeAttr(kNativeHeldThis);
    });
    std::vector<InstanceRegistry> proved;
    for (InstanceRegistry & registry : proveInstanceRegistries(module)) {
        if (!registry.refusal.empty()) { continue; }
        registry.constructor->setAttr(
            kNativeInstanceClass,
            mlir::StringAttr::get(module.getContext(), jsName(registry.constructor)));
        for (mlir::Operation * read : registry.keyedReads) {
            read->setAttr(kNativeInstanceKeyClass,
                          mlir::StringAttr::get(module.getContext(), jsName(registry.constructor)));
        }
        for (ctjs::FuncOp method : registry.methods) {
            method->setAttr(kNativeInstanceMethod,
                            registry.constructor->getAttr(kNativeInstanceClass));
        }
        for (const auto & [yield, index] : registry.falsyYields) {
            mlir::OpBuilder at(yield);
            auto null = ctjs::ConstantOp::create(at, yield->getLoc(),
                                                 ctjs::ValueType::get(module.getContext()),
                                                 ctjs::NullAttr::get(module.getContext()));
            yield->setOperand(index, null);
        }
        for (mlir::Operation * test : registry.exactTests) {
            test->setAttr(kNativeInstanceTest, mlir::UnitAttr::get(module.getContext()));
        }
        for (mlir::Operation * cell : registry.heldThis) {
            cell->setAttr(kNativeHeldThis, mlir::UnitAttr::get(module.getContext()));
        }
        proved.push_back(std::move(registry));
    }
    // A value read back from a family's Map may be an instance of any of its
    // classes.
    for (InstanceRegistry & registry : proved) {
        llvm::SetVector<mlir::Value> all;
        for (const InstanceRegistry & other : proved) {
            if (other.map == registry.map) {
                all.insert(other.instances.begin(), other.instances.end());
            }
        }
        registry.instances.assign(all.begin(), all.end());
    }
    return proved;
}

bool isIdentityClass(ctjs::FuncOp fn) {
    return fn && fn->hasAttr(kNativeInstanceClass);
}

namespace {

/// The identity class a `ctjs.create_object` is a `new` of: the constructor
/// lift (ClosureLifting/Constructors.cpp) turns `new C(...)` into an empty
/// literal and a receiver call of C's constructor, whose mark names it.
llvm::StringRef instanceClassOf(ctjs::CreateObjectOp object) {
    if (!object->hasAttr(kNativeClassName)) { return {}; }
    for (mlir::Operation * user : object.getResult().getUsers()) {
        auto call = llvm::dyn_cast<ctjs::CallDirectOp>(user);
        if (!call || !call->hasAttr(kNativeReceiver) || call.getReceiver() != object.getResult()) {
            continue;
        }
        if (auto mark = call.getTarget() ? call.getTarget()->getAttrOfType<mlir::StringAttr>(
                                               kNativeInstanceClass)
                                         : mlir::StringAttr{}) {
            return mark.getValue();
        }
    }
    return {};
}

/// The identity class of a value of type `instance<C>` or `opt<instance<C>>`.
llvm::StringRef instanceClassOf(mlir::Type type) {
    if (auto optional = llvm::dyn_cast_or_null<OptType>(type)) { type = optional.getElementType(); }
    auto instance = llvm::dyn_cast_or_null<InstanceType>(type);
    return instance ? instance.getClassName() : llvm::StringRef{};
}

/// The constructor of identity class `name`, or null.
ctjs::FuncOp constructorOf(mlir::ModuleOp module, llvm::StringRef name) {
    ctjs::FuncOp found;
    module.walk([&](ctjs::FuncOp fn) {
        auto mark = fn->getAttrOfType<mlir::StringAttr>(kNativeInstanceClass);
        if (mark && mark.getValue() == name) { found = fn; }
    });
    return found;
}

/// Whether the constructor writes field `key` before anything else can see
/// the instance: a store to `this` at the top of its body, before any call.
/// Every instance a registry hands out has then been through that store.
bool assignedInConstructor(ctjs::FuncOp constructor, llvm::StringRef key) {
    if (!constructor || constructor.getBody().empty()) { return false; }
    mlir::Block & entry = constructor.getBody().front();
    const mlir::Value self = entry.getArgument(ctjs::arg_receiver);
    for (mlir::Operation & op : entry) {
        if (llvm::isa<ctjs::CallOp, ctjs::CallDirectOp, ctjs::ConstructOp>(op) ||
            op.getNumRegions() != 0) {
            return false;
        }
        auto store = llvm::dyn_cast<ctjs::SetPropertyOp>(op);
        if (store && store.getObject() == self && ctjs::constantKey(store.getKey()) == key) {
            return true;
        }
    }
    return false;
}

} // namespace

std::optional<mlir::Type> TypeInference::instanceTypeOf(
    mlir::Operation * op, llvm::ArrayRef<const TypeLattice *> operands) {
    mlir::MLIRContext * context = op->getContext();
    if (auto object = llvm::dyn_cast<ctjs::CreateObjectOp>(op)) {
        const llvm::StringRef name = instanceClassOf(object);
        if (name.empty()) { return std::nullopt; }
        return InstanceType::get(context, name);
    }
    auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(op);
    if (!read || operands.empty()) { return std::nullopt; }
    // A Map's `get` is absent alone until a value stored in the Map is
    // visited; answering now would let the generic field rule box the read
    // before the instance arrives.
    const mlir::Type object = operands[0]->getValue().getType();
    if (object == OptType::get(context, BottomType::get(context)) &&
        read.getObject().getDefiningOp() &&
        nativeMapAction(read.getObject().getDefiningOp()) == "get") {
        return mlir::Type{};
    }
    const llvm::StringRef name = instanceClassOf(object);
    const llvm::StringRef key = ctjs::constantKey(read.getKey());
    if (name.empty() || !ctjs::ordinaryKey(key)) { return std::nullopt; }
    auto module = op->getParentOfType<mlir::ModuleOp>();
    if (!instanceStoresBuilt_) {
        instanceStoresBuilt_ = true;
        module.walk([&](ctjs::FuncOp fn) {
            auto mark = fn->getAttrOfType<mlir::StringAttr>(kNativeInstanceClass);
            if (!mark) { mark = fn->getAttrOfType<mlir::StringAttr>(kNativeInstanceMethod); }
            if (!mark || fn.getBody().empty()) { return; }
            const mlir::Value self = fn.getBody().front().getArgument(ctjs::arg_receiver);
            for (mlir::Operation * user : self.getUsers()) {
                auto store = llvm::dyn_cast<ctjs::SetPropertyOp>(user);
                if (!store || store.getObject() != self) { continue; }
                instanceStores_[(mark.getValue() + llvm::Twine('\0') +
                                 ctjs::constantKey(store.getKey()))
                                    .str()]
                    .push_back(store.getValue());
            }
        });
    }
    // The class's stores of `key`. A field the constructor writes first is
    // never read absent; any other may be.
    mlir::Type field = assignedInConstructor(constructorOf(module, name), key)
                           ? mlir::Type{}
                           : OptType::get(context, BottomType::get(context));
    const auto stores = instanceStores_.find((name + llvm::Twine('\0') + key).str());
    if (stores != instanceStores_.end()) { field = joinLattices(op, stores->second, field); }
    return field;
}

void TypeInference::joinInstanceReceivers(mlir::Operation * top,
                                          llvm::EquivalenceClasses<mlir::Value> & classes) {
    llvm::StringMap<mlir::Value> leader; // Per class, the first member seen.
    const auto join = [&](llvm::StringRef name, mlir::Value member) {
        auto [entry, fresh] = leader.try_emplace(name, member);
        if (fresh) {
            classes.insert(member);
        } else {
            classes.unionSets(entry->second, member);
        }
    };
    top->walk([&](mlir::Operation * op) {
        if (auto object = llvm::dyn_cast<ctjs::CreateObjectOp>(op)) {
            const llvm::StringRef name = instanceClassOf(object);
            if (!name.empty()) { join(name, object.getResult()); }
            return;
        }
        auto fn = llvm::dyn_cast<ctjs::FuncOp>(op);
        if (!fn || fn.getBody().empty()) { return; }
        auto mark = fn->getAttrOfType<mlir::StringAttr>(kNativeInstanceClass);
        if (!mark) { mark = fn->getAttrOfType<mlir::StringAttr>(kNativeInstanceMethod); }
        if (mark) { join(mark.getValue(), fn.getBody().front().getArgument(ctjs::arg_receiver)); }
    });
}

void rewriteInstanceRegistries(mlir::ModuleOp module) {
    mlir::MLIRContext * context = module.getContext();
    const auto value = ctjs::ValueType::get(context);
    for (const InstanceRegistry & registry : markInstanceRegistries(module)) {
        for (const auto & [first, last] : registry.ownNameNullings) {
            auto names = llvm::cast<ctjs::CallOp>(first);
            const mlir::Value self = names.getArgs()[0];
            // One store of null per own property, where the loop began.
            mlir::OpBuilder at(first);
            for (const std::string & field : registry.fields) {
                auto key = ctjs::ConstantOp::create(at, first->getLoc(), value,
                                                    ctjs::StringAttr::get(context, field));
                auto null = ctjs::ConstantOp::create(at, first->getLoc(), value,
                                                     ctjs::NullAttr::get(context));
                ctjs::SetPropertyOp::create(at, first->getLoc(), self, key, null);
            }
            // The loop, last operation first, then what only fed the names
            // call (`Object.getOwnPropertyNames`).
            mlir::Value callee = names.getCallee();
            llvm::SmallVector<mlir::Operation *> loop;
            for (mlir::Operation * op = first;; op = op->getNextNode()) {
                loop.push_back(op);
                if (op == last) { break; }
            }
            for (mlir::Operation * op : llvm::reverse(loop)) {
                op->dropAllUses();
                op->erase();
            }
            for (mlir::Operation * fed = callee.getDefiningOp(); fed && fed->use_empty();) {
                mlir::Operation * next =
                    fed->getNumOperands() ? fed->getOperand(0).getDefiningOp() : nullptr;
                fed->erase();
                fed = next;
            }
        }
    }
}

} // namespace ctcompile::ctnative
