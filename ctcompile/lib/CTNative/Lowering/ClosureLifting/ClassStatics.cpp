//===- ClosureLifting/ClassStatics.cpp - Static members of classes --------===//
//
// A class's static methods and static getters are functions stored on the
// constructor (Classes.cpp shows the importer's spelling). When the class
// itself is never used as a value, every read of a static member names one
// function, so each read becomes a direct call and the definitions lower to
// nothing:
//
//   K.s(a, b)               ctjs.call_direct @s(undefined, ..., a, b)
//   K.g                     ctjs.call_direct @g(undefined, ...)
//   this.s(a) inside a      the same: a static member's `this` is K, because
//     static member of K    nothing but these calls can reach it
//   this.constructor.g      the same, inside a method or the constructor of
//     inside an instance    K: an instance of K reaches K through its
//     method of K           prototype's `constructor`
//
// The values that name the class are the constructor's closure, the binding
// it is stored in (and the static variables it became, RunOnce.cpp), the
// captures of that binding, a static member's `this`, `this.constructor` in
// a method, and the parameter of a helper the class is passed to. A helper is
// a function called directly; it is cloned for the class, together with the
// functions it creates, the call goes to the clone, and the clone's parameter
// names the class, so `helper(K)` with `k.s()` inside becomes a direct call
// of K's `s` in a copy of the helper made for K. The rewrite needs, for every
// one of them:
//  1. every use is a static-member read this file rewrites, the receiver of a
//     call through one, class setup, an argument of a direct call to a
//     helper that reads neither new.target nor its own closure, a `new`, a
//     write of the binding or a capture of it (the rewrite leaves a static
//     member's `this` and `this.constructor` without a value, so `new this`
//     in a static member becomes a construct of the class, marked
//     kNativeClassConstruct for Constructors.cpp, and any other `new` of
//     them is refused where it can run); anything else could
//     reach a static member this file does not see;
//  2. each static member's closure is used only by its definition and its
//     `__home` edge; a static member that a read outside the static members
//     reaches reads neither new.target nor its own closure (its `this` is
//     checked as a name of the class, by rule 1); and none is named `__home`,
//     which the engine reads as the home object;
//  3. every read runs after the class is defined (runsAfter, as for the
//     singleton tables): before that, the static member does not exist yet;
//  4. no class extends this one and it extends none, so `this` in a static
//     member and `this.constructor` in a method name exactly this class;
//  5. no object writes a `constructor` property of its own.
// A class that fails records why in `staticRefusals`, and classCensus()
// refuses it with that reason.
//
//===----------------------------------------------------------------------===//

#include "ClosureLifter.h"
#include "mlir/Dialect/Arith/IR/Arith.h"

#include "llvm/ADT/SetVector.h"

#include <functional>
#include <variant>

namespace ctcompile::ctnative::lowering_detail {
namespace {

/// The number of JavaScript parameters `target`'s entry block takes.
size_t parametersOf(ctjs::FuncOp target) {
    return target.getBody().front().getNumArguments() - ctjs::implicit_arguments;
}

/// Erases `op` and then each constant or global load that only it used.
void eraseWithFeeders(mlir::Operation * op) {
    for (mlir::Operation * user : llvm::make_early_inc_range(op->getUsers())) {
        if (llvm::isa<ctjs::RootOp>(user)) { user->erase(); }
    }
    llvm::SetVector<mlir::Operation *> feeders;
    for (mlir::Value operand : op->getOperands()) {
        if (mlir::Operation * made = operand.getDefiningOp();
            llvm::isa_and_nonnull<ctjs::ConstantOp, ctjs::LoadGlobalOp>(made)) {
            feeders.insert(made);
        }
    }
    op->erase();
    for (mlir::Operation * made : feeders) {
        if (made->use_empty()) { made->erase(); }
    }
}

/// Whether `fn` reads new.target, or its own closure for anything but making
/// closures that relay none of its captures. A direct call passes neither.
bool needsCallee(ctjs::FuncOp fn) {
    mlir::Block & entry = fn.getBody().front();
    if (!entry.getArgument(1).use_empty()) { return true; }
    return llvm::any_of(entry.getArgument(2).getUses(), [](mlir::OpOperand & use) {
        auto made = llvm::dyn_cast<ctjs::CreateClosureOp>(use.getOwner());
        if (!made || use.getOperandNumber() != 0) { return true; }
        for (unsigned i = 0; i < made.getUpvalues().size(); ++i) {
            if (closureLifter::enclosingIndex(made, i) >= 0) { return true; }
        }
        return false;
    });
}

/// Whether `value` is the global object on every path: a load of
/// `globalThis`, `window` or `self`, the script's own `this`, a choice between
/// such values, or a parameter every direct caller passes one for (a UMD
/// header's `(function (global) { ... })(this)`).
bool isGlobalObject(mlir::ModuleOp module, mlir::Value value, unsigned depth = 0) {
    if (depth > 8) { return false; }
    if (auto load = value.getDefiningOp<ctjs::LoadGlobalOp>()) {
        return llvm::is_contained(
            std::initializer_list<llvm::StringRef>{"globalThis", "window", "self"}, load.getName());
    }
    if (auto choice = value.getDefiningOp<mlir::scf::IfOp>()) {
        const unsigned index = llvm::cast<mlir::OpResult>(value).getResultNumber();
        return llvm::all_of(choice->getRegions(), [&](mlir::Region & region) {
            return !region.empty() &&
                   isGlobalObject(module, region.front().getTerminator()->getOperand(index),
                                  depth + 1);
        });
    }
    auto argument = llvm::dyn_cast<mlir::BlockArgument>(value);
    auto fn = argument ? llvm::dyn_cast<ctjs::FuncOp>(argument.getOwner()->getParentOp())
                       : ctjs::FuncOp{};
    if (!fn || argument.getOwner() != &fn.getBody().front()) { return false; }
    if (fn.getSymName() == "_script_$0") { return argument.getArgNumber() == ctjs::arg_receiver; }
    if (argument.getArgNumber() < ctjs::implicit_arguments) { return false; }
    // Every caller is visible: the function is private, or each closure of
    // it is only the callee of direct calls of it.
    const auto index = ctjs::functionIndex(fn);
    bool hidden = false;
    module.walk([&](ctjs::CreateClosureOp made) {
        if (!index || ctjs::closureFunction(made) != index) { return; }
        for (mlir::OpOperand & use : made.getResult().getUses()) {
            auto call = llvm::dyn_cast<ctjs::CallDirectOp>(use.getOwner());
            hidden |= !llvm::isa<ctjs::RootOp>(use.getOwner()) &&
                      (!call || use.getOperandNumber() != 2 || call.getCallee() != fn.getSymName());
        }
    });
    if (hidden) { return false; }
    auto uses = mlir::SymbolTable::getSymbolUses(fn, module.getOperation());
    if (!uses || uses->empty()) { return false; }
    return llvm::all_of(*uses, [&](const mlir::SymbolTable::SymbolUse & use) {
        auto call = llvm::dyn_cast<ctjs::CallDirectOp>(use.getUser());
        return call && argument.getArgNumber() < call->getNumOperands() &&
               isGlobalObject(module, call->getOperand(argument.getArgNumber()), depth + 1);
    });
}

/// The function that `op` sits in, by name, for a refusal.
std::string where(mlir::Operation * op) {
    auto fn = op->getParentOfType<ctjs::FuncOp>();
    return fn ? (" in `" + fn.getSymName() + "`").str() : std::string{};
}

} // namespace

bool closureLifter::bindClassStatics(liftReport & out) {
    staticRefusals.clear();
    classCensus();
    if (jsClasses.empty()) { return false; }

    // Rule 5, for the whole module.
    bool ownConstructor = false;
    module.walk([&](ctjs::SetPropertyOp set) {
        if (ctjs::constantKey(set.getKey()) == "constructor" && !isHomeEdge(set) &&
            !set.getObject().getDefiningOp<ctjs::CreateObjectOp>()) {
            ownConstructor = true;
        }
    });
    // Rule 1 for `x.constructor` outside a class's own methods: an instance
    // of a class can be read that way wherever it flows, and such a read
    // names a class this rewrite would not see.
    llvm::DenseSet<mlir::Value> instanceThis;
    // The class each constructor, method and getter belongs to.
    llvm::DenseMap<mlir::Operation *, mlir::Operation *> memberOf;
    for (auto & [key, cls] : jsClasses) {
        llvm::SmallVector<ctjs::CreateClosureOp> members{cls.constructor};
        for (const auto & entry : cls.methods) { members.push_back(entry.getValue()); }
        for (const auto & entry : cls.getters) { members.push_back(entry.getValue()); }
        for (ctjs::CreateClosureOp member : members) {
            ctjs::FuncOp fn = targetOf(member);
            if (fn && !fn.getBody().empty()) {
                instanceThis.insert(fn.getBody().front().getArgument(ctjs::arg_receiver));
                memberOf[fn.getOperation()] = key;
            }
        }
    }
    // ponytail: `this.constructor` inside an arrow reads the arrow's lexical
    // `this`, not its receiver, so it counts as a stray read here until the
    // arrow-`this` lowering (ArrowThis.cpp) gives the arrow the method's
    // receiver.
    // A read of `this` held in a cell (a derived constructor's this-binding,
    // which only its receiver or the bind-this helper's answer is written
    // to) names that member's class, or a class extending it; any other
    // stray read may name any class.
    const auto heldThis = [&](mlir::Value object, mlir::Operation * holder) {
        auto read = object.getDefiningOp<ctjs::CellGetOp>();
        auto cell =
            read ? read.getCell().getDefiningOp<ctjs::CreateCellOp>() : ctjs::CreateCellOp{};
        if (!cell || cell->getParentOfType<ctjs::FuncOp>().getOperation() != holder) {
            return false;
        }
        const mlir::Value receiver =
            llvm::cast<ctjs::FuncOp>(holder).getBody().front().getArgument(ctjs::arg_receiver);
        const auto thisValue = [&](mlir::Value v) {
            if (v == receiver || ctjs::isUndefinedConstant(v)) { return true; }
            auto call = v.getDefiningOp<ctjs::CallOp>();
            auto load =
                call ? call.getCallee().getDefiningOp<ctjs::LoadGlobalOp>() : ctjs::LoadGlobalOp{};
            return load && load.getName() == "__ctbrowser_bind_this";
        };
        if (!thisValue(cell.getInitial())) { return false; }
        return llvm::all_of(cell.getResult().getUses(), [&](mlir::OpOperand & use) {
            auto set = llvm::dyn_cast<ctjs::CellSetOp>(use.getOwner());
            return !set || use.getOperandNumber() != 1 || thisValue(set.getValue());
        });
    };
    // Each stray read, with the class whose member it is in when it reads
    // that member's `this`, or null.
    llvm::SmallVector<std::pair<mlir::Operation *, mlir::Operation *>> strayReads;
    module.walk([&](ctjs::GetPropertyOp read) {
        if (ctjs::constantKey(read.getKey()) != "constructor" ||
            instanceThis.contains(read.getObject())) {
            return;
        }
        auto holder = read->getParentOfType<ctjs::FuncOp>();
        // A function nothing creates or names cannot run (flattening's
        // erased subclass sources stay until the unreachable pass).
        if (holder && holder.getSymName() != "_script_$0") {
            const auto index = ctjs::functionIndex(holder);
            bool made = false;
            module.walk([&](ctjs::CreateClosureOp c) {
                made |= index && ctjs::closureFunction(c) == index;
            });
            if (!made && mlir::SymbolTable::symbolKnownUseEmpty(holder, module)) { return; }
        }
        const auto owner = holder ? memberOf.find(holder.getOperation()) : memberOf.end();
        // `super(...)` looks up the parent's constructor as the `constructor`
        // of the home object's prototype: a class in the member's heritage.
        auto proto = read.getObject().getDefiningOp<ctjs::GetProtoOp>();
        const bool superLookup = proto && proto->getOperand(0).getDefiningOp<ctjs::LoadHomeOp>();
        const bool own =
            owner != memberOf.end() && (superLookup || heldThis(read.getObject(), holder));
        strayReads.push_back({read, own ? owner->second : nullptr});
    });
    // Rule 4, from every heritage call in the module, including the ones
    // whose shape the class census does not recognise (and so records no
    // parent for): each side is the class a binding or closure names, and a
    // side naming none makes every class suspect.
    llvm::DenseSet<mlir::Operation *> derived, parents;
    bool unnamedHeritage = false;
    const auto classNamed = [](mlir::Value value) -> mlir::Operation * {
        if (auto made = value.getDefiningOp<ctjs::CreateClosureOp>()) { return made; }
        auto read = value.getDefiningOp<ctjs::CellGetOp>();
        auto cell =
            read ? read.getCell().getDefiningOp<ctjs::CreateCellOp>() : ctjs::CreateCellOp{};
        if (!cell) { return nullptr; }
        mlir::Operation * held = nullptr;
        for (mlir::Operation * user : cell.getResult().getUsers()) {
            auto set = llvm::dyn_cast<ctjs::CellSetOp>(user);
            if (!set) { continue; }
            auto made = set.getValue().getDefiningOp<ctjs::CreateClosureOp>();
            if (!made || (held && held != made)) { return nullptr; }
            held = made;
        }
        return held;
    };
    module.walk([&](ctjs::CallOp call) {
        auto load = call.getCallee().getDefiningOp<ctjs::LoadGlobalOp>();
        if (!load || load.getName() != "__ctbrowser_class_heritage") { return; }
        mlir::Operation * child =
            call.getArgs().size() > 0 ? classNamed(call.getArgs()[0]) : nullptr;
        mlir::Operation * parent =
            call.getArgs().size() > 1 ? classNamed(call.getArgs()[1]) : nullptr;
        if (!child || !parent) { unnamedHeritage = true; }
        if (child) { derived.insert(child); }
        if (parent) { parents.insert(parent); }
    });
    indexStarters();

    // A helper that a class is passed to is cloned for that class, with the
    // functions it creates, under fresh function indices.
    unsigned nextIndex = 0;
    module.walk([&](ctjs::FuncOp fn) {
        if (const std::optional<unsigned> index = ctjs::functionIndex(fn)) {
            nextIndex = std::max(nextIndex, *index + 1);
        }
    });
    llvm::DenseMap<mlir::Operation *, mlir::StringAttr> copiedFrom;
    std::function<ctjs::FuncOp(ctjs::FuncOp, llvm::SmallVectorImpl<mlir::Operation *> &)>
        cloneDeep = [&](ctjs::FuncOp fn, llvm::SmallVectorImpl<mlir::Operation *> & made) {
            auto copy = llvm::cast<ctjs::FuncOp>(fn->clone());
            const unsigned index = nextIndex++;
            const llvm::StringRef name = fn.getSymName();
            copy.setSymName((name.substr(0, name.rfind('$')) + "$" + llvm::Twine(index)).str());
            mlir::SymbolTable::setSymbolVisibility(copy, mlir::SymbolTable::Visibility::Private);
            module.getBody()->getOperations().insertAfter(fn->getIterator(), copy);
            byIndex[index] = copy;
            if (passesNewTarget.contains(fn.getOperation())) {
                passesNewTarget.insert(copy.getOperation());
            }
            made.push_back(copy);
            copiedFrom[copy.getOperation()] = fn.getSymNameAttr();
            copy.walk([&](ctjs::CreateClosureOp c) {
                if (ctjs::FuncOp target = targetOf(c)) {
                    ctjs::FuncOp nested = cloneDeep(target, made);
                    c.setFunction(*ctjs::functionIndex(nested));
                }
            });
            return copy;
        };

    bool changed = false;
    for (auto & [key, cls] : jsClasses) {
        if (cls.statics.empty() && cls.staticGetters.empty()) { continue; }
        const auto refuse = [&](const llvm::Twine & why) {
            staticRefusals.try_emplace(key, why.str());
        };
        if (cls.parent || cls.heritage || derived.contains(key) || parents.contains(key)) {
            refuse(cls.parent || cls.heritage || derived.contains(key)
                       ? "it extends another class, so `this` in a static member is "
                         "not one class; inherited statics are the next slice"
                       : "another class extends it, so `this` in a static member is "
                         "not one class; inherited statics are the next slice");
            continue;
        }
        if (unnamedHeritage) {
            refuse("a class in this module extends a value this rewrite cannot name, so `this` "
                   "in a static member may not be one class");
            continue;
        }
        if (const auto stray = llvm::find_if(
                strayReads, [&](auto & read) { return !read.second || read.second == key; });
            stray != strayReads.end()) {
            refuse("`x.constructor` is read" + where(stray->first) +
                   ", where `x` may be an instance of the class");
            continue;
        }
        if (ownConstructor) {
            refuse("an object writes its own `constructor`, so `this.constructor` may not "
                   "name the class");
            continue;
        }
        ctjs::CallOp defined;
        for (mlir::Operation * op : cls.setup) {
            if (auto call = llvm::dyn_cast<ctjs::CallOp>(op)) { defined = call; }
        }
        if (!defined) { continue; }

        // Each static member by name, with its closure and function.
        struct member {
            ctjs::CreateClosureOp closure;
            ctjs::FuncOp target;
            bool getter;
        };
        llvm::StringMap<member> statics;
        for (const auto & entry : cls.statics) {
            statics[entry.getKey()] = {entry.getValue(), targetOf(entry.getValue()), false};
        }
        for (const auto & entry : cls.staticGetters) {
            statics[entry.getKey()] = {entry.getValue(), targetOf(entry.getValue()), true};
        }
        // Rule 2.
        for (auto & entry : statics) {
            member m = entry.getValue();
            // The VM reads its own home-object edge for `K.__home`, whatever
            // the class defines under that name.
            if (entry.getKey() == "__home") {
                refuse("its static member `__home` is the engine's home-object key");
                break;
            }
            if (!m.target || m.target.getBody().empty()) {
                refuse("its static member `" + entry.getKey() + "` has no body");
                break;
            }
            for (mlir::Operation * user : m.closure->getUsers()) {
                if (!llvm::isa<ctjs::RootOp>(user) && !cls.definitions.contains(user) &&
                    !isHomeEdge(user)) {
                    refuse("its static member `" + entry.getKey() + "` is also used by `" +
                           user->getName().getStringRef() + "`" + where(user));
                    break;
                }
            }
        }
        if (staticRefusals.contains(key)) { continue; }

        // Rule 1: collect every value that names the class, and check each
        // use of each one.
        llvm::SetVector<mlir::Value> names;
        // The names that are a static member's `this` or `this.constructor`.
        llvm::DenseSet<mlir::Value> selves;
        llvm::SetVector<mlir::Operation *> boxes;
        llvm::DenseSet<std::pair<mlir::Operation *, unsigned>> slots;
        // The same slots in the order found: a slot before the slots of the
        // closures nested in its target that are filled from it.
        llvm::SmallVector<std::pair<ctjs::CreateClosureOp, unsigned>> slotOrder;
        names.insert(cls.constructor.getResult());
        for (auto & entry : statics) {
            names.insert(entry.getValue().target.getBody().front().getArgument(0));
            selves.insert(entry.getValue().target.getBody().front().getArgument(0));
        }
        // The constructor's own closure and new.target, which a `new` of the
        // class passes it: the direct call that replaces that `new` passes
        // neither, so they must be names the rewrite resolves.
        mlir::Value constructorCallee;
        if (ctjs::FuncOp constructor = targetOf(cls.constructor);
            constructor && !constructor.getBody().empty()) {
            constructorCallee = constructor.getBody().front().getArgument(ctjs::arg_callee);
            for (unsigned implicit : {1u, 2u}) {
                names.insert(constructor.getBody().front().getArgument(implicit));
                selves.insert(constructor.getBody().front().getArgument(implicit));
            }
        }
        // `this.constructor` in a method or the constructor.
        llvm::SmallVector<ctjs::FuncOp> instanceFunctions{targetOf(cls.constructor)};
        for (const auto & entry : cls.methods) {
            instanceFunctions.push_back(targetOf(entry.getValue()));
        }
        for (ctjs::FuncOp fn : instanceFunctions) {
            if (!fn || fn.getBody().empty()) { continue; }
            for (mlir::OpOperand & use : fn.getBody().front().getArgument(0).getUses()) {
                auto load = llvm::dyn_cast<ctjs::GetPropertyOp>(use.getOwner());
                if (load && use.getOperandNumber() == 0 &&
                    ctjs::constantKey(load.getKey()) == "constructor") {
                    names.insert(load.getResult());
                    selves.insert(load.getResult());
                }
            }
        }
        struct staticCall {
            ctjs::CallOp call;
            ctjs::GetPropertyOp load;
            ctjs::FuncOp target;
        };
        llvm::SmallVector<staticCall> calls;
        llvm::SmallVector<std::pair<ctjs::GetPropertyOp, ctjs::FuncOp>> getterReads;
        llvm::SmallPtrSet<mlir::Operation *, 8> receiving;
        // Each `new this` or `new this.constructor`.
        llvm::SmallVector<mlir::Operation *> selfConstructs;
        // Each truth test of the class (`if (C)`, `!C`): a class is a
        // function, always truthy.
        llvm::SmallVector<mlir::Operation *> truths;
        const std::string className = classNameOf(targetOf(cls.constructor));
        // A capture slot of a closure that holds the class: its reads in the
        // target, and the slots of closures there that are filled from it.
        const auto followSlot = [&](ctjs::CreateClosureOp c, unsigned slot) {
            if (!slots.insert({c.getOperation(), slot}).second) { return; }
            slotOrder.push_back({c, slot});
            ctjs::FuncOp target = targetOf(c);
            if (!target || target.getBody().empty() || closuresOf.lookup(target) != 1) {
                refuse("a closure that captures `" + className +
                       "` names a function made more than once" + where(c));
                return;
            }
            const mlir::Value self = target.getBody().front().getArgument(2);
            target.getBody().walk([&](mlir::Operation * op) {
                if (auto read = llvm::dyn_cast<ctjs::LoadUpvalueOp>(op);
                    read && read.getClosure() == self && read.getIndex() == slot) {
                    names.insert(read.getResult());
                }
            });
        };
        const auto nestedSlots =
            [&](ctjs::CreateClosureOp c, unsigned slot,
                llvm::SmallVectorImpl<std::pair<ctjs::CreateClosureOp, unsigned>> & into) {
                ctjs::FuncOp target = targetOf(c);
                if (!target || target.getBody().empty()) { return; }
                target.getBody().walk([&](ctjs::CreateClosureOp nested) {
                    for (unsigned j = 0; j < nested.getUpvalues().size(); ++j) {
                        if (enclosingIndex(nested, j) == static_cast<std::int32_t>(slot)) {
                            into.push_back({nested, j});
                        }
                    }
                });
            };
        const auto captured = [&](ctjs::CreateClosureOp c, unsigned slot) {
            llvm::SmallVector<std::pair<ctjs::CreateClosureOp, unsigned>> pending{{c, slot}};
            while (!pending.empty()) {
                auto [made, at] = pending.pop_back_val();
                if (slots.contains({made.getOperation(), at})) { continue; }
                followSlot(made, at);
                nestedSlots(made, at, pending);
            }
        };
        // Every box of a binding that holds the class: the box itself and the
        // static variables of the same symbol.
        const auto followBox = [&](ctjs::CreateCellOp cell) {
            llvm::SmallVector<ctjs::CreateCellOp> group{cell};
            if (auto symbol = cell->getAttr(kNativeStaticCell)) {
                group.clear();
                module.walk([&](ctjs::CreateCellOp other) {
                    if (other->getAttr(kNativeStaticCell) == symbol) { group.push_back(other); }
                });
            }
            for (ctjs::CreateCellOp box : group) {
                if (!boxes.insert(box.getOperation())) { continue; }
                if (!box->hasAttr(kNativeStaticReference) &&
                    !ctjs::constantAttr<ctjs::UndefinedAttr>(box.getInitial()) &&
                    !names.contains(box.getInitial())) {
                    refuse("the binding of `" + className + "` starts with another value");
                }
                for (mlir::OpOperand & use : box.getResult().getUses()) {
                    switch (classify(use)) {
                    case cellUse::read:
                        names.insert(llvm::cast<ctjs::CellGetOp>(use.getOwner()).getResult());
                        break;
                    case cellUse::write:
                        if (!names.contains(
                                llvm::cast<ctjs::CellSetOp>(use.getOwner()).getValue())) {
                            refuse("the binding of `" + className +
                                   "` is also written with another value" + where(use.getOwner()));
                        }
                        break;
                    case cellUse::capture:
                        captured(llvm::cast<ctjs::CreateClosureOp>(use.getOwner()),
                                 use.getOperandNumber() - kFirstCapture);
                        break;
                    default:
                        if (!llvm::isa<ctjs::RootOp>(use.getOwner())) {
                            refuse("the binding of `" + className + "` is used by `" +
                                   use.getOwner()->getName().getStringRef().str() + "`" +
                                   where(use.getOwner()));
                        }
                        break;
                    }
                }
            }
        };
        // Each call that passes the class to a helper, redirected to the
        // helper's clone for this class, and the clones; undone if the class
        // is refused.
        struct specialization {
            ctjs::CallDirectOp call;
            mlir::FlatSymbolRefAttr callee;
            mlir::Value calleeValue;
            unsigned argument;
            ctjs::FuncOp clone;
        };
        llvm::SmallVector<specialization> specialized;
        llvm::SmallVector<mlir::Operation *> clones;
        llvm::DenseMap<std::pair<mlir::Operation *, unsigned>, ctjs::FuncOp> cloneOf;
        // The parameter of `call`'s helper that receives the class as its
        // argument `i`, in the helper's clone for this class; a reason when
        // the helper cannot be cloned.
        const auto specialize = [&](ctjs::CallDirectOp call,
                                    unsigned i) -> std::variant<mlir::Value, std::string> {
            ctjs::FuncOp target = call.getTarget();
            if (!target || target.getBody().empty()) {
                return std::string("is passed to a function this rewrite cannot see");
            }
            mlir::Block & entry = target.getBody().front();
            if (ctjs::implicit_arguments + i >= entry.getNumArguments() ||
                ctjs::readsRawArguments(target)) {
                return ("is passed to `" + target.getSymName() +
                        "`, which reads it from its argument window")
                    .str();
            }
            if (needsCallee(target)) {
                return ("is passed to `" + target.getSymName() +
                        "`, which reads new.target or its own closure")
                    .str();
            }
            if (llvm::is_contained(clones, target.getOperation())) {
                return entry.getArgument(ctjs::implicit_arguments + i);
            }
            ctjs::FuncOp & clone = cloneOf[{target.getOperation(), i}];
            if (!clone) {
                // The clones call each other, not the functions they copy.
                const size_t first = clones.size();
                clone = cloneDeep(target, clones);
                llvm::SmallVector<std::pair<mlir::StringAttr, mlir::StringAttr>> renamed;
                for (size_t k = first; k < clones.size(); ++k) {
                    auto copy = llvm::cast<ctjs::FuncOp>(clones[k]);
                    renamed.push_back(
                        {copiedFrom.lookup(copy.getOperation()), copy.getSymNameAttr()});
                }
                for (size_t k = first; k < clones.size(); ++k) {
                    for (auto & [from, to] : renamed) {
                        if (mlir::failed(
                                mlir::SymbolTable::replaceAllSymbolUses(from, to, clones[k]))) {
                            return std::string("is passed to a function whose copy could not be "
                                               "renamed");
                        }
                    }
                }
                indexStarters();
            }
            mlir::OpBuilder at(call);
            specialized.push_back({call, call.getCalleeAttr(), call.getCalleeValue(), i, clone});
            call.setCalleeAttr(mlir::FlatSymbolRefAttr::get(clone.getSymNameAttr()));
            call.getCalleeValueMutable().assign(undefinedAt(at, call.getLoc()));
            return clone.getBody().front().getArgument(ctjs::implicit_arguments + i);
        };
        const auto revert = [&] {
            for (specialization & s : llvm::reverse(specialized)) {
                mlir::Operation * placeholder = s.call.getCalleeValue().getDefiningOp();
                s.call.setCalleeAttr(s.callee);
                s.call.getCalleeValueMutable().assign(s.calleeValue);
                if (placeholder && placeholder->use_empty()) { placeholder->erase(); }
            }
            for (mlir::Operation * fn : clones) {
                if (const std::optional<unsigned> index =
                        ctjs::functionIndex(llvm::cast<ctjs::FuncOp>(fn))) {
                    byIndex.erase(*index);
                }
                passesNewTarget.erase(fn);
                fn->erase();
            }
            if (!clones.empty()) { indexStarters(); }
            specialized.clear();
            clones.clear();
        };
        const auto failed = [&] {
            if (!staticRefusals.contains(key)) { return false; }
            revert();
            return true;
        };
        // The class stored as a field of an object that is published as a
        // global (`return {Alert: Alert}` by a library's factory, which a UMD
        // header writes to `globalThis.bootstrap`): the reads of that field,
        // through the factory's result or the global, each name the class. A
        // reason when the object, the result or the global is used any other
        // way, or when a read of the global may come before the write.
        llvm::SmallVector<ctjs::SetPropertyOp> exports;
        // The reads of a published field, which follow the class's
        // definition by condition 4 of the export proof.
        llvm::DenseSet<mlir::Value> exported;
        const auto exportReads = [&](ctjs::SetPropertyOp store)
            -> std::variant<llvm::SmallVector<mlir::Value>, std::string> {
            using reason = std::string;
            const llvm::StringRef field = ctjs::constantKey(store.getKey());
            auto object = store.getObject().getDefiningOp<ctjs::CreateObjectOp>();
            if (!object || field.empty() || !store->hasAttr(ctjs::defineOwnMarker)) {
                return reason("is stored into something other than an object literal's own "
                              "field");
            }
            bool returned = false;
            llvm::SmallVector<mlir::Value> reads;
            for (mlir::OpOperand & use : object.getResult().getUses()) {
                mlir::Operation * user = use.getOwner();
                if (llvm::isa<ctjs::RootOp>(user)) { continue; }
                if (auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(user);
                    set && use.getOperandNumber() == 0 && set->hasAttr(ctjs::defineOwnMarker) &&
                    !ctjs::constantKey(set.getKey()).empty()) {
                    if (set != store && ctjs::constantKey(set.getKey()) == field) {
                        return reason("is stored into an object field that is defined twice");
                    }
                    continue;
                }
                // A read of the field in the object's own frame, after the
                // store that defines it.
                if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(user);
                    read && use.getOperandNumber() == 0 &&
                    !ctjs::constantKey(read.getKey()).empty()) {
                    if (ctjs::constantKey(read.getKey()) != field) { continue; }
                    if (!reachesInFrame(store.getOperation(), read.getOperation())) {
                        return "is stored into an object field read" + where(read) +
                               " where the store may not have run";
                    }
                    reads.push_back(read.getResult());
                    continue;
                }
                if (llvm::isa<ctjs::ReturnOp>(user)) {
                    // The class is defined before the object is returned, on
                    // the factory's own path: a read of the published field
                    // then follows the definition (rule 3).
                    returned = defined && defined->getBlock() == user->getBlock() &&
                               defined->isBeforeInBlock(user);
                    if (!returned) {
                        return reason("is returned in an object before it is defined");
                    }
                    continue;
                }
                return ("is stored into an object that `" + user->getName().getStringRef() +
                        "` uses" + where(user))
                    .str();
            }
            auto factory = object->getParentOfType<ctjs::FuncOp>();
            if (!returned) { return reads; }
            // Every call of the factory is a direct call, and its result is
            // read by constant keys or written once through the global object.
            llvm::SmallVector<ctjs::SetPropertyOp> published;
            auto uses = mlir::SymbolTable::getSymbolUses(factory, module.getOperation());
            if (!uses) { return reason("is returned by a function this rewrite cannot follow"); }
            for (const mlir::SymbolTable::SymbolUse & symbol : *uses) {
                auto call = llvm::dyn_cast<ctjs::CallDirectOp>(symbol.getUser());
                if (!call) {
                    return ("is returned by `" + factory.getSymName() +
                            "`, which is also named by `" +
                            symbol.getUser()->getName().getStringRef() + "`")
                        .str();
                }
                for (mlir::OpOperand & use : call.getResult().getUses()) {
                    mlir::Operation * user = use.getOwner();
                    if (llvm::isa<ctjs::RootOp>(user)) { continue; }
                    if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(user);
                        read && use.getOperandNumber() == 0 &&
                        !ctjs::constantKey(read.getKey()).empty()) {
                        if (ctjs::constantKey(read.getKey()) == field) {
                            reads.push_back(read.getResult());
                        }
                        continue;
                    }
                    auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(user);
                    if (set && use.getOperandNumber() == 2 &&
                        isGlobalObject(module, set.getObject()) &&
                        !ctjs::constantKey(set.getKey()).empty()) {
                        published.push_back(set);
                        continue;
                    }
                    return ("is returned in an object that `" + user->getName().getStringRef() +
                            "` uses" + where(user))
                        .str();
                }
            }
            if (published.size() > 1) {
                return reason("is returned in an object written to more than one global");
            }
            if (published.empty()) { return reads; }
            ctjs::SetPropertyOp write = published.front();
            const llvm::StringRef global = ctjs::constantKey(write.getKey());
            // The write always runs once its function runs, and that function
            // is started by one call, which every read of the global follows.
            auto writer = write->getParentOfType<ctjs::FuncOp>();
            // A call reached both through the closure and by symbol is listed
            // twice; it is still one start.
            const auto starts = starters.lookup(writer.getOperation());
            const bool once = !starts.empty() && llvm::all_of(starts, [&](mlir::Operation * start) {
                return start == starts.front();
            });
            if (write->getBlock() != &writer.getBody().front() || !once) {
                return ("is published as global `" + global +
                        "` by a write that may not run, or runs more than once")
                    .str();
            }
            bool other = false;
            std::string misuse;
            const llvm::DenseMap<mlir::Operation *, llvm::SmallVector<mlir::Operation *>> noFields;
            module.walk([&](mlir::Operation * op) {
                if (auto store = llvm::dyn_cast<ctjs::StoreGlobalOp>(op);
                    store && store.getName() == global) {
                    other = true;
                }
                if (auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(op);
                    set && set != write && ctjs::constantKey(set.getKey()) == global) {
                    if (isGlobalObject(module, set.getObject())) { other = true; }
                }
                auto load = llvm::dyn_cast<ctjs::LoadGlobalOp>(op);
                if (!load || load.getName() != global || !misuse.empty()) { return; }
                for (mlir::OpOperand & use : load.getResult().getUses()) {
                    mlir::Operation * user = use.getOwner();
                    if (llvm::isa<ctjs::RootOp>(user)) { continue; }
                    auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(user);
                    if (!read || use.getOperandNumber() != 0 ||
                        ctjs::constantKey(read.getKey()).empty()) {
                        misuse = ("is published as global `" + global + "`, which `" +
                                  user->getName().getStringRef() + "` uses" + where(user))
                                     .str();
                        return;
                    }
                    if (ctjs::constantKey(read.getKey()) != field) { continue; }
                    if (!runsAfter(read, starts.front(), noFields)) {
                        misuse = ("is published as global `" + global + "`, which" + where(read) +
                                  " may read before it is written")
                                     .str();
                        return;
                    }
                    reads.push_back(read.getResult());
                }
            });
            if (other) {
                return ("is published as global `" + global + "`, which is also written").str();
            }
            if (!misuse.empty()) { return misuse; }
            return reads;
        };
        // A use inside a static member is refused only if that member can
        // run (see `reached` below); until then it waits here.
        llvm::SmallPtrSet<mlir::Operation *, 8> staticTargets, reached;
        for (auto & entry : statics) {
            staticTargets.insert(entry.getValue().target.getOperation());
        }
        const auto holderOf = [](mlir::Operation * op) {
            return op->getParentOfType<ctjs::FuncOp>().getOperation();
        };
        llvm::SmallVector<std::pair<mlir::Operation *, std::string>> deferred;
        const auto refuseAt = [&](mlir::Operation * at, const llvm::Twine & why) {
            if (staticTargets.contains(holderOf(at))) {
                deferred.push_back({holderOf(at), why.str()});
                return false;
            }
            refuse(why);
            return true;
        };
        for (size_t i = 0; i < names.size() && !staticRefusals.contains(key); ++i) {
            const mlir::Value name = names[i];
            for (mlir::OpOperand & use : name.getUses()) {
                mlir::Operation * user = use.getOwner();
                if (llvm::isa<ctjs::RootOp>(user) || cls.setup.contains(user) ||
                    cls.definitions.contains(user) || isHomeEdge(user)) {
                    continue;
                }
                // The constructor reaching its captures through its own
                // closure, which the closure lift turns into parameters.
                if (name == constructorCallee &&
                    (llvm::isa<ctjs::LoadUpvalueOp, ctjs::StoreUpvalueOp>(user) ||
                     (llvm::isa<ctjs::CreateClosureOp>(user) && use.getOperandNumber() == 0))) {
                    continue;
                }
                if (llvm::isa<ctjs::ConstructOp>(user)) {
                    if (selves.contains(name)) { selfConstructs.push_back(user); }
                    continue;
                }
                if (auto unary = llvm::dyn_cast<ctjs::UnaryOp>(user);
                    llvm::isa<ctjs::TruthyOp>(user) ||
                    (unary && unary.getKind() == ctjs::UnaryKind::Not)) {
                    truths.push_back(user);
                    continue;
                }
                if (auto set = llvm::dyn_cast<ctjs::CellSetOp>(user);
                    set && use.getOperandNumber() == 1) {
                    if (auto cell = set.getCell().getDefiningOp<ctjs::CreateCellOp>()) {
                        followBox(cell);
                        continue;
                    }
                }
                if (auto made = llvm::dyn_cast<ctjs::CreateClosureOp>(user);
                    made && use.getOperandNumber() >= kFirstCapture) {
                    captured(made, use.getOperandNumber() - kFirstCapture);
                    continue;
                }
                // Stored as a field of a published object: its reads name it.
                if (auto store = llvm::dyn_cast<ctjs::SetPropertyOp>(user);
                    store && use.getOperandNumber() == 2) {
                    auto found = exportReads(store);
                    if (auto * reads = std::get_if<llvm::SmallVector<mlir::Value>>(&found)) {
                        exports.push_back(store);
                        for (mlir::Value read : *reads) {
                            names.insert(read);
                            exported.insert(read);
                        }
                        continue;
                    }
                    if (refuseAt(user, "`" + className + "` " + std::get<std::string>(found) +
                                           where(user))) {
                        break;
                    }
                    continue;
                }
                // A binding (a captured parameter's box) that starts with it.
                if (auto cell = llvm::dyn_cast<ctjs::CreateCellOp>(user)) {
                    followBox(cell);
                    continue;
                }
                // Passed to a helper: the helper's clone for this class takes
                // it as a parameter, which then names the class.
                if (auto direct = llvm::dyn_cast<ctjs::CallDirectOp>(user);
                    direct && use.getOperandNumber() >= direct.getArgs().getBeginOperandIndex()) {
                    auto param = specialize(direct, use.getOperandNumber() -
                                                        direct.getArgs().getBeginOperandIndex());
                    if (auto * value = std::get_if<mlir::Value>(&param)) {
                        names.insert(*value);
                        continue;
                    }
                    if (refuseAt(user, "`" + className + "` " + std::get<std::string>(param) +
                                           where(user))) {
                        break;
                    }
                    continue;
                }
                if (llvm::isa<ctjs::CallOp>(user) && use.getOperandNumber() == 1) {
                    receiving.insert(user);
                    continue;
                }
                auto load = llvm::dyn_cast<ctjs::GetPropertyOp>(user);
                const llvm::StringRef field = load && use.getOperandNumber() == 0
                                                  ? ctjs::constantKey(load.getKey())
                                                  : llvm::StringRef{};
                const auto found = statics.find(field);
                if (field.empty() || found == statics.end()) {
                    if (refuseAt(user, load && !field.empty()
                                           ? "it reads `" + className + "." + field.str() +
                                                 "`, which is not a static member" + where(user)
                                           : "`" + className + "` is used as a value by `" +
                                                 user->getName().getStringRef().str() + "`" +
                                                 where(user))) {
                        break;
                    }
                    continue;
                }
                member m = found->getValue();
                if (m.getter) {
                    getterReads.push_back({load, m.target});
                    continue;
                }
                for (mlir::OpOperand & loaded : load.getResult().getUses()) {
                    auto call = llvm::dyn_cast<ctjs::CallOp>(loaded.getOwner());
                    if (llvm::isa<ctjs::RootOp>(loaded.getOwner())) { continue; }
                    if (!call || loaded.getOperandNumber() != 0) {
                        if (refuseAt(loaded.getOwner(), "its static method `" + field +
                                                            "` is read as a value" +
                                                            where(loaded.getOwner()))) {
                            break;
                        }
                        continue;
                    }
                    if (call.getArgs().size() > parametersOf(m.target)) {
                        if (refuseAt(call, "a call passes its static method `" + field +
                                               "` more arguments than it takes" + where(call))) {
                            break;
                        }
                        continue;
                    }
                    calls.push_back({call, load, m.target});
                }
            }
        }
        if (failed()) { continue; }
        // A call whose receiver names the class must be a static call whose
        // receiver also names it.
        for (mlir::Operation * call : receiving) {
            const bool known =
                llvm::any_of(calls, [&](staticCall s) { return s.call.getOperation() == call; });
            if (!known && refuseAt(call, "`" + className +
                                             "` is the receiver of a call through something else" +
                                             where(call))) {
                break;
            }
        }
        for (staticCall s : calls) {
            if (!names.contains(s.call.getReceiver()) &&
                refuseAt(s.call,
                         "its static method is called with another receiver" + where(s.call))) {
                break;
            }
        }
        // After the rewrite a static member's `this` is undefined and an
        // instance has no `constructor` field, so neither may be constructed
        // where it can run: in an instance method, or in a static member that
        // a read outside the static members reaches.
        for (bool grew = true; grew;) {
            grew = false;
            const auto reach = [&](mlir::Operation * read, ctjs::FuncOp target) {
                mlir::Operation * from = holderOf(read);
                if ((!staticTargets.contains(from) || reached.contains(from)) &&
                    reached.insert(target.getOperation()).second) {
                    grew = true;
                }
            };
            for (staticCall & s : calls) { reach(s.call, s.target); }
            for (auto & [load, target] : getterReads) { reach(load, target); }
        }
        for (auto & [holder, why] : deferred) {
            if (reached.contains(holder)) {
                refuse(why);
                break;
            }
        }
        // A direct call passes no closure and no new.target, so a static
        // member that can run must read neither: its captures would be gone.
        // One nothing reads stays uncalled, whatever its body reads.
        for (auto & entry : statics) {
            ctjs::FuncOp target = entry.getValue().target;
            if (reached.contains(target.getOperation()) && needsCallee(target)) {
                refuse("its static member `" + entry.getKey() +
                       "` reads new.target or its own closure");
                break;
            }
        }
        // `new this(...)` in a static member constructs the class itself: the
        // checks above proved that member's `this` names the class at every
        // call that reaches it. The rewrite marks it for the constructor lift
        // (Constructors.cpp), which builds the instance there. A `new` of
        // `this.constructor`, or of a `this` that is not the member's own,
        // has no such proof.
        llvm::SmallVector<ctjs::ConstructOp> classConstructs;
        for (mlir::Operation * made : selfConstructs) {
            auto built = llvm::cast<ctjs::ConstructOp>(made);
            mlir::Operation * holder = holderOf(made);
            if (staticTargets.contains(holder)) {
                const mlir::Value self =
                    llvm::cast<ctjs::FuncOp>(holder).getBody().front().getArgument(0);
                if (built.getCallee() == self && built.getNewTarget() == self) {
                    classConstructs.push_back(built);
                    continue;
                }
            }
            if (!staticTargets.contains(holder) || reached.contains(holder)) {
                refuse("it constructs `new this` or `new this.constructor`" + where(made));
                break;
            }
        }
        if (failed()) { continue; }

        // The calls just redirected to helper clones start those clones.
        if (!specialized.empty()) { indexStarters(); }
        // Rule 3. A member of the class starts only through the class or an
        // instance of it, which exist once the class is defined.
        // ponytail: trusts `new K` to come after the definition; a `new` of a
        // binding read before it would need the same climb as a static read.
        llvm::DenseMap<mlir::Operation *, llvm::SmallVector<mlir::Operation *>> startedBy;
        for (ctjs::FuncOp fn : instanceFunctions) {
            if (fn) { startedBy[fn.getOperation()]; }
        }
        for (auto & entry : statics) { startedBy[entry.getValue().target.getOperation()]; }
        for (const auto & entry : cls.getters) {
            if (ctjs::FuncOp fn = targetOf(entry.getValue())) { startedBy[fn.getOperation()]; }
        }
        const auto early = [&](mlir::Operation * read) {
            const auto through = [&](mlir::Value object) { return exported.contains(object); };
            if (auto call = llvm::dyn_cast<ctjs::CallOp>(read);
                call && through(call.getReceiver())) {
                return false;
            }
            if (auto load = llvm::dyn_cast<ctjs::GetPropertyOp>(read);
                load && through(load.getObject())) {
                return false;
            }
            if (runsAfter(read, defined, startedBy)) { return false; }
            refuse("a read of a static member" + where(read) +
                   " can run before the class is defined");
            return true;
        };
        if (llvm::any_of(calls, [&](staticCall s) { return early(s.call); }) ||
            llvm::any_of(getterReads, [&](auto & r) { return early(r.first); })) {
            revert();
            continue;
        }

        // The rewrite. A read of a published field, and each direct call
        // made through one, is marked with the class it resolves to.
        const auto exportMark = mlir::StringAttr::get(context, className);
        for (mlir::Value read : exported) {
            if (mlir::Operation * made = read.getDefiningOp()) {
                made->setAttr(kNativeExportClass, exportMark);
            }
        }
        // A `new this` in a static member constructs the class.
        if (ctjs::FuncOp constructor = targetOf(cls.constructor)) {
            for (ctjs::ConstructOp built : classConstructs) {
                // A name, not a symbol reference: a `new this` the constructor
                // lift leaves in a refused member must not keep the
                // constructor's function alive once the class lowers.
                built->setAttr(kNativeClassConstruct, constructor.getSymNameAttr());
            }
        }
        for (staticCall & s : calls) {
            mlir::OpBuilder at(s.call);
            const mlir::Value undefined = undefinedAt(at, s.call.getLoc());
            auto direct = directCall(at, s.call.getLoc(), s.target, undefined, undefined, undefined,
                                     {}, s.call.getArgs(), parametersOf(s.target));
            if (exported.contains(s.call.getReceiver())) {
                direct->setAttr(kNativeExportClass, exportMark);
            }
            s.call.getResult().replaceAllUsesWith(direct.getResult());
            s.call.erase();
            ++out.calls;
        }
        for (auto & [load, target] : getterReads) {
            mlir::OpBuilder at(load);
            const mlir::Value undefined = undefinedAt(at, load.getLoc());
            auto direct = directCall(at, load.getLoc(), target, undefined, undefined, undefined, {},
                                     mlir::ValueRange{}, parametersOf(target));
            if (exported.contains(load.getObject())) {
                direct->setAttr(kNativeExportClass, exportMark);
            }
            load.getResult().replaceAllUsesWith(direct.getResult());
            eraseWithFeeders(load);
            ++out.calls;
        }
        for (mlir::Operation * test : truths) {
            mlir::OpBuilder here(test);
            mlir::Value folded =
                llvm::isa<ctjs::TruthyOp>(test)
                    ? mlir::arith::ConstantOp::create(here, test->getLoc(), here.getBoolAttr(true))
                          .getResult()
                    : ctjs::ConstantOp::create(here, test->getLoc(), ctjs::ValueType::get(context),
                                               ctjs::BooleanAttr::get(context, false))
                          .getResult();
            test->getResult(0).replaceAllUsesWith(folded);
            test->erase();
        }
        // A field of a published object holds the class: it now holds a
        // value of the class's tag type, which carries no data, since every
        // read of the field resolved above.
        for (ctjs::SetPropertyOp store : exports) {
            mlir::OpBuilder at(store);
            auto tag = ctjs::ConstantOp::create(at, store.getLoc(), ctjs::ValueType::get(context),
                                                ctjs::UndefinedAttr::get(context));
            tag->setAttr(kNativeClassTag, exportMark);
            store.getValueMutable().assign(tag.getResult());
        }
        llvm::SetVector<mlir::Operation *> loads;
        for (staticCall & s : calls) { loads.insert(s.load); }
        for (mlir::Operation * load : loads) {
            if (load->use_empty() || llvm::all_of(load->getUsers(), [](mlir::Operation * u) {
                    return llvm::isa<ctjs::RootOp>(u);
                })) {
                eraseWithFeeders(load);
            }
        }
        // Names nothing reads any more: `this.constructor`, reads of the
        // binding and of its captures. Later ones can feed earlier ones.
        const auto eraseUnreadNames = [&] {
            for (bool erased = true; erased;) {
                erased = false;
                for (mlir::Value name : names) {
                    mlir::Operation * made = name.getDefiningOp();
                    if (!llvm::isa_and_nonnull<ctjs::GetPropertyOp, ctjs::CellGetOp,
                                               ctjs::LoadUpvalueOp>(made) ||
                        !llvm::all_of(made->getUsers(), [](mlir::Operation * u) {
                            return llvm::isa<ctjs::RootOp>(u);
                        })) {
                        continue;
                    }
                    names.remove(name);
                    eraseWithFeeders(made);
                    erased = true;
                    break;
                }
            }
        };
        eraseUnreadNames();
        // Capture slots that held the class and that nothing reads any more,
        // nested closures first, so that no slot is removed while a closure
        // inside its target is still filled from it.
        llvm::MapVector<mlir::Operation *, llvm::SmallVector<unsigned>> unread;
        for (auto & [made, slot] : llvm::reverse(slotOrder)) {
            unread[made.getOperation()].push_back(slot);
        }
        for (auto & [op, candidates] : unread) {
            auto made = llvm::cast<ctjs::CreateClosureOp>(op);
            ctjs::FuncOp target = targetOf(made);
            llvm::SmallVector<unsigned> gone;
            for (const unsigned slot : candidates) {
                bool read = false;
                target.getBody().walk([&](mlir::Operation * o) {
                    if (llvm::isa<ctjs::LoadUpvalueOp, ctjs::StoreUpvalueOp>(o)) {
                        read |= o->getAttrOfType<mlir::IntegerAttr>("index").getInt() ==
                                static_cast<std::int64_t>(slot);
                    } else if (auto nested = llvm::dyn_cast<ctjs::CreateClosureOp>(o)) {
                        for (unsigned j = 0; j < nested.getUpvalues().size(); ++j) {
                            read |= enclosingIndex(nested, j) == static_cast<std::int32_t>(slot);
                        }
                    }
                });
                if (!read) { gone.push_back(slot); }
            }
            if (!gone.empty()) { removeCaptureSlots(made, gone); }
        }
        // A local binding of the class that no closure captures any more
        // holds the constructor at every read a write dominates: forward it,
        // so that `new K` names the constructor's closure as it does for an
        // uncaptured binding. One nothing uses any more (a helper's captured
        // parameter) is erased.
        const auto forwardBoxes = [&] {
            for (mlir::Operation * op : boxes.takeVector()) {
                auto box = llvm::cast<ctjs::CreateCellOp>(op);
                if (box->getAttr(kNativeStaticCell)) { continue; }
                if (llvm::all_of(box->getUsers(),
                                 [](mlir::Operation * u) { return llvm::isa<ctjs::RootOp>(u); })) {
                    eraseWithFeeders(box);
                    continue;
                }
                llvm::SmallVector<ctjs::CellSetOp> writes;
                llvm::SmallVector<ctjs::CellGetOp> reads;
                bool other = false;
                for (mlir::OpOperand & use : box.getResult().getUses()) {
                    switch (classify(use)) {
                    case cellUse::read:
                        reads.push_back(llvm::cast<ctjs::CellGetOp>(use.getOwner()));
                        break;
                    case cellUse::write:
                        writes.push_back(llvm::cast<ctjs::CellSetOp>(use.getOwner()));
                        break;
                    default: other |= !llvm::isa<ctjs::RootOp>(use.getOwner()); break;
                    }
                }
                const auto written = [&](ctjs::CellGetOp read) {
                    return llvm::any_of(writes, [&](ctjs::CellSetOp write) {
                        return reachesInFrame(write.getOperation(), read.getOperation());
                    });
                };
                if (other || writes.empty() || !llvm::all_of(reads, written)) {
                    boxes.insert(op);
                    continue;
                }
                for (ctjs::CellGetOp read : reads) {
                    names.remove(read.getResult());
                    read.getResult().replaceAllUsesWith(cls.constructor.getResult());
                    eraseWithFeeders(read);
                }
                for (ctjs::CellSetOp write : writes) { write.erase(); }
                eraseWithFeeders(box);
            }
        };
        forwardBoxes();
        // A helper's clone that no longer reads the class's parameter is
        // passed undefined for it instead.
        for (specialization & s : specialized) {
            const unsigned slot =
                static_cast<unsigned>(s.call.getArgs().getBeginOperandIndex()) + s.argument;
            if (!s.clone.getBody()
                     .front()
                     .getArgument(ctjs::implicit_arguments + s.argument)
                     .use_empty()) {
                continue;
            }
            mlir::OpBuilder at(s.call);
            s.call->setOperand(slot, undefinedAt(at, s.call.getLoc()));
        }
        eraseUnreadNames();
        forwardBoxes();
        // A helper's closure that only those calls used goes with them.
        llvm::SetVector<mlir::Operation *> helpers;
        for (specialization & s : specialized) {
            if (auto made = s.calleeValue.getDefiningOp<ctjs::CreateClosureOp>()) {
                helpers.insert(made);
            }
        }
        for (mlir::Operation * made : helpers) {
            if (llvm::all_of(made->getUsers(),
                             [](mlir::Operation * u) { return llvm::isa<ctjs::RootOp>(u); })) {
                eraseWithFeeders(made);
            }
        }
        // The definitions, their `__home` edges and the closures.
        for (auto & entry : statics) {
            member m = entry.getValue();
            for (mlir::Operation * user : llvm::make_early_inc_range(m.closure->getUsers())) {
                cls.setup.erase(user);
                cls.definitions.erase(user);
                eraseWithFeeders(user);
            }
            eraseWithFeeders(m.closure);
            if (--closuresOf[m.target.getOperation()] == 0) {
                // Every caller is now a visible direct call; as in lift(), the
                // function must be private for type inference to see them all.
                mlir::SymbolTable::setSymbolVisibility(m.target,
                                                       mlir::SymbolTable::Visibility::Private);
            }
        }
        // A static member nothing reads stays as a function nobody calls.
        // If it is refused, the unreachable-refusal pass in LowerToEmitC.cpp
        // removes it and records a row in ctnative.unreachable_refusals.
        cls.statics.clear();
        cls.staticGetters.clear();
        changed = true;
        indexStarters();
    }
    return changed;
}

} // namespace ctcompile::ctnative::lowering_detail
