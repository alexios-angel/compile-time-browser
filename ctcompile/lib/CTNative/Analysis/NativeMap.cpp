//===- NativeMap.cpp - standard identity and instance-use proofs ---------===//
#include "ctcompile/CTNative/Analysis/NativeMap.h"

#include "mlir/IR/SymbolTable.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallVector.h"

#include <string>

namespace ctcompile::ctnative {

llvm::StringRef nativeMapAction(mlir::Operation * op) {
    if (op == nullptr) { return {}; }
    auto action = op->getAttrOfType<mlir::StringAttr>(kNativeMapAction);
    return action ? action.getValue() : llvm::StringRef{};
}

int64_t nativeMapGroup(mlir::Value value) {
    if (auto arg = llvm::dyn_cast<mlir::BlockArgument>(value)) {
        auto fn = llvm::dyn_cast<ctjs::FuncOp>(arg.getOwner()->getParentOp());
        if (!fn || fn.getBody().empty() || arg.getOwner() != &fn.getBody().front()) { return -1; }
        auto groups = fn->getAttrOfType<mlir::DenseI64ArrayAttr>(kNativeMapArgGroups);
        return groups && arg.getArgNumber() < groups.size()
                   ? groups.asArrayRef()[arg.getArgNumber()]
                   : -1;
    }
    auto * op = value.getDefiningOp();
    if (op == nullptr || op->getNumResults() != 1) { return -1; }
    auto group = op->getAttrOfType<mlir::IntegerAttr>(kNativeMapGroup);
    return group ? group.getInt() : -1;
}

bool isNativeMapBookkeeping(mlir::Operation * op) {
    return op != nullptr && (op->hasAttr(kNativeMapConstructor) || op->hasAttr(kNativeMapMethod));
}

namespace {

llvm::StringRef keyOf(mlir::Value key) {
    auto constant = key.getDefiningOp<ctjs::ConstantOp>();
    if (!constant) { return {}; }
    auto text = llvm::dyn_cast<ctjs::StringAttr>(constant.getValue());
    return text ? text.getValue() : llvm::StringRef{};
}

struct plan {
    llvm::SmallVector<ctjs::ConstructOp> made;
    llvm::SmallVector<mlir::Value> members;
    llvm::SmallVector<ctjs::GetPropertyOp> methods;
    llvm::SmallVector<ctjs::GetPropertyOp> sizes;
    llvm::SmallVector<ctjs::CallOp> calls;
};

// This graph unifies C++ SCHEMAS, never runtime identities. Two fresh Maps
// passed to the same parameter need one key/value carrier but remain distinct
// owning allocations. Every producer in a family must be checked below: a
// union containing one Map does not prove that another incoming value is one.
struct flowGraph {
    llvm::DenseMap<mlir::Value, mlir::Value> parent;
    llvm::SmallVector<mlir::Value> nodes;
    llvm::DenseMap<mlir::Operation *, llvm::SmallVector<ctjs::CallDirectOp>> callers;
    llvm::DenseMap<mlir::Operation *, llvm::SmallVector<ctjs::ReturnOp>> returns;

    void add(mlir::Value value) {
        if (parent.try_emplace(value, value).second) { nodes.push_back(value); }
    }
    mlir::Value find(mlir::Value value) {
        mlir::Value root = parent.lookup(value);
        if (!root) { return {}; }
        while (parent.lookup(root) != root) { root = parent.lookup(root); }
        while (value != root) {
            mlir::Value next = parent.lookup(value);
            parent[value] = root;
            value = next;
        }
        return root;
    }
    void join(mlir::Value left, mlir::Value right) {
        add(left);
        add(right);
        parent[find(right)] = find(left);
    }
    static ctjs::FuncOp target(ctjs::CallDirectOp call) {
        return mlir::SymbolTable::lookupNearestSymbolFrom<ctjs::FuncOp>(call, call.getCalleeAttr());
    }
    static bool closed(ctjs::FuncOp fn) {
        return fn && !fn.getBody().empty() &&
               mlir::SymbolTable::getSymbolVisibility(fn) == mlir::SymbolTable::Visibility::Private;
    }
    static bool exactCall(ctjs::CallDirectOp call, ctjs::FuncOp fn) {
        return fn && !fn.getBody().empty() &&
               call->getNumOperands() == fn.getBody().front().getNumArguments();
    }
    void build(mlir::ModuleOp module) {
        module.walk([&](ctjs::FuncOp fn) {
            fn.getBody().walk([&](ctjs::ReturnOp ret) { returns[fn].push_back(ret); });
            auto & exits = returns[fn];
            for (ctjs::ReturnOp ret : exits) { join(exits.front().getValue(), ret.getValue()); }
        });
        module.walk([&](ctjs::CallDirectOp call) {
            auto fn = target(call);
            if (!fn || fn.getBody().empty()) { return; }
            callers[fn].push_back(call);
            mlir::Block & entry = fn.getBody().front();
            for (unsigned i = 3; i < call->getNumOperands() && i < entry.getNumArguments(); ++i) {
                join(call->getOperand(i), entry.getArgument(i));
            }
            for (ctjs::ReturnOp ret : returns[fn]) { join(call.getResult(), ret.getValue()); }
        });
        module.walk([&](ctjs::CallOp call) {
            auto get = call.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
            if (get && keyOf(get.getKey()) == "set") { join(call.getReceiver(), call.getResult()); }
        });
    }
};

bool erasedCapture(mlir::OpOperand & use) {
    auto cell = llvm::dyn_cast<ctjs::CreateCellOp>(use.getOwner());
    if (!cell || use.getOperandNumber() != 0 || !cell->hasAttr("ctnative.unboxed")) {
        return false;
    }
    return llvm::all_of(cell.getResult().getUses(), [](mlir::OpOperand & capture) {
        return llvm::isa<ctjs::CreateClosureOp>(capture.getOwner()) &&
               capture.getOperandNumber() >= 2 && capture.getOwner()->hasAttr("ctnative.lifted");
    });
}

// A method value may only be called on the exact instance from which it was
// loaded. Detached methods, .call/.apply, property writes, escaping instances
// and real loop-carried aliases need additional proofs and are refused.
std::string collect(plan & out, flowGraph & graph,
                    const llvm::DenseSet<mlir::Operation *> & sites) {
    for (ctjs::ConstructOp made : out.made) {
        if (!made.getArgs().empty()) { return "native Map requires an empty constructor"; }
        if (made.getNewTarget() != made.getCallee()) {
            return "native Map requires its standard constructor as new.target";
        }
    }
    for (mlir::Value object : out.members) {
        if (auto arg = llvm::dyn_cast<mlir::BlockArgument>(object)) {
            auto fn = llvm::dyn_cast<ctjs::FuncOp>(arg.getOwner()->getParentOp());
            if (!flowGraph::closed(fn) || arg.getOwner() != &fn.getBody().front() ||
                arg.getArgNumber() < 3 || graph.callers[fn].empty()) {
                return "native Map parameter requires a closed function with visible callers";
            }
            for (ctjs::CallDirectOp call : graph.callers[fn]) {
                if (!flowGraph::exactCall(call, fn)) {
                    return "native Map call requires matching argument and parameter counts";
                }
            }
        } else if (auto call = object.getDefiningOp<ctjs::CallDirectOp>()) {
            auto fn = flowGraph::target(call);
            if (!flowGraph::closed(fn) || !flowGraph::exactCall(call, fn) ||
                graph.returns[fn].empty()) {
                return "native Map result requires a closed function with visible returns";
            }
        } else if (auto call = object.getDefiningOp<ctjs::CallOp>()) {
            auto get = call.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
            if (!get || keyOf(get.getKey()) != "set") {
                return "native Map flow contains an unproved call result";
            }
        } else if (!sites.contains(object.getDefiningOp())) {
            return ("native Map flow contains a non-Map producer `" +
                    object.getDefiningOp()->getName().getStringRef() + "`")
                .str();
        }
        for (mlir::OpOperand & use : object.getUses()) {
            if (erasedCapture(use)) { continue; }
            if (auto call = llvm::dyn_cast<ctjs::CallDirectOp>(use.getOwner());
                call && use.getOperandNumber() >= 3) {
                auto fn = flowGraph::target(call);
                if (!flowGraph::closed(fn)) {
                    return "native Map argument requires a closed callee";
                }
                if (!flowGraph::exactCall(call, fn)) {
                    return "native Map call requires matching argument and parameter counts";
                }
                continue;
            }
            if (auto ret = llvm::dyn_cast<ctjs::ReturnOp>(use.getOwner())) {
                if (!flowGraph::closed(ret->getParentOfType<ctjs::FuncOp>())) {
                    return "native Map return requires a closed function";
                }
                continue;
            }
            if (auto call = llvm::dyn_cast<ctjs::CallOp>(use.getOwner());
                call && use.getOperandNumber() == 1) {
                auto get = call.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
                if (get && get.getObject() == object) { continue; }
            }
            auto get = llvm::dyn_cast<ctjs::GetPropertyOp>(use.getOwner());
            if (!get || use.getOperandNumber() != 0) {
                return ("native Map instance escapes or is mutated through `" +
                        use.getOwner()->getName().getStringRef() + "`")
                    .str();
            }
            const llvm::StringRef key = keyOf(get.getKey());
            if (key == "size") {
                out.sizes.push_back(get);
                continue;
            }
            const unsigned arity = key == "set"                                         ? 2U
                                   : key == "get" || key == "has" || key == "delete"    ? 1U
                                   : key == "clear" || key == "keys" || key == "values" ? 0U
                                                                                        : 99U;
            if (arity == 99U) {
                return "native Map property is not a supported constant method or size";
            }
            out.methods.push_back(get);
            for (mlir::OpOperand & methodUse : get.getResult().getUses()) {
                auto call = llvm::dyn_cast<ctjs::CallOp>(methodUse.getOwner());
                if (!call || methodUse.getOperandNumber() != 0 || call.getReceiver() != object) {
                    return "native Map method is detached, escapes, or has a different receiver";
                }
                if (call.getArgs().size() != arity) {
                    return "native Map method requires its exact argument count";
                }
                out.calls.push_back(call);
            }
        }
    }
    return {};
}

} // namespace

void prepareNativeMaps(mlir::ModuleOp module) {
    // Proof annotations are derived, not trusted input, including on reruns.
    module.walk([&](mlir::Operation * op) {
        for (llvm::StringRef name :
             {kNativeMapSite, kNativeMapAction, kNativeMapMethod, kNativeMapConstructor,
              kNativeMapReason, kNativeMapGroup, kNativeMapArgGroups}) {
            op->removeAttr(name);
        }
    });
    llvm::SmallVector<ctjs::ConstructOp> madeSites;
    llvm::SmallVector<ctjs::LoadGlobalOp> constructors;
    module.walk([&](ctjs::LoadGlobalOp load) {
        if (load.getName() == "Map") { constructors.push_back(load); }
    });
    if (constructors.empty()) { return; }

    std::string reason;
    llvm::DenseSet<mlir::Operation *> sites;
    for (ctjs::LoadGlobalOp load : constructors) {
        for (mlir::OpOperand & use : load.getResult().getUses()) {
            auto made = llvm::dyn_cast<ctjs::ConstructOp>(use.getOwner());
            if (!made || use.getOperandNumber() > 1 || made.getCallee() != load.getResult()) {
                if (reason.empty()) {
                    reason = "standard Map constructor identity escapes or is inspected";
                }
                continue;
            }
            if (sites.insert(made).second) { madeSites.push_back(made); }
        }
    }
    flowGraph graph;
    for (ctjs::ConstructOp made : madeSites) { graph.add(made.getResult()); }
    graph.build(module);
    llvm::SmallVector<plan, 4> plans;
    llvm::DenseMap<mlir::Value, unsigned> families;
    for (ctjs::ConstructOp made : madeSites) {
        mlir::Value root = graph.find(made.getResult());
        auto inserted = families.try_emplace(root, static_cast<unsigned>(plans.size()));
        if (inserted.second) { plans.emplace_back(); }
        plans[inserted.first->second].made.push_back(made);
    }
    for (mlir::Value member : graph.nodes) {
        const auto found = families.find(graph.find(member));
        if (found != families.end()) { plans[found->second].members.push_back(member); }
    }
    for (plan & candidate : plans) {
        const std::string problem = collect(candidate, graph, sites);
        if (reason.empty()) { reason = problem; }
    }
    llvm::DenseSet<mlir::Operation *> calls;
    for (const plan & candidate : plans) {
        for (ctjs::CallOp call : candidate.calls) { calls.insert(call); }
    }
    // Standard builtins are installed by the reference/native program's
    // initial environment. A spelling alone is insufficient: explicit stores,
    // global-object/reflection access and unknown invocations can replace the
    // constructor or its prototype. Refuse module-wide in this first slice.
    module.walk([&](mlir::Operation * op) {
        if (!reason.empty()) { return; }
        if (auto store = llvm::dyn_cast<ctjs::StoreGlobalOp>(op);
            store && store.getName() == "Map") {
            reason = "standard Map binding is assigned in this program";
        } else if (auto load = llvm::dyn_cast<ctjs::LoadGlobalOp>(op);
                   load && load.getName() != "Map") {
            for (mlir::OpOperand & use : load.getResult().getUses()) {
                if (!llvm::isa<ctjs::CallDirectOp>(use.getOwner()) || use.getOperandNumber() != 2) {
                    reason = "standard Map identity is unproved with other host/global value reads";
                    break;
                }
            }
        } else if (llvm::isa<ctjs::CallOp>(op) && !calls.contains(op)) {
            reason = "standard Map identity is unproved across an unknown call";
        } else if (llvm::isa<ctjs::ConstructOp>(op) && !sites.contains(op)) {
            reason = "standard Map identity is unproved across an unknown constructor";
        } else if (llvm::isa<ctjs::PassNewTargetOp, ctjs::DeleteNamedOp, ctjs::CallSpreadOp,
                             ctjs::ConstructSpreadOp, ctjs::DynamicImportOp>(op) ||
                   op->getName().getStringRef().starts_with("ctjs.module_")) {
            reason = "standard Map identity is unproved across dynamic invocation or deletion";
        }
    });
    auto * context = module.getContext();
    if (!reason.empty()) {
        for (ctjs::LoadGlobalOp load : constructors) {
            load->setAttr(kNativeMapReason, mlir::StringAttr::get(context, reason));
        }
        for (ctjs::ConstructOp made : madeSites) {
            made->setAttr(kNativeMapReason, mlir::StringAttr::get(context, reason));
        }
        return;
    }
    for (ctjs::LoadGlobalOp load : constructors) {
        load->setAttr(kNativeMapConstructor, mlir::UnitAttr::get(context));
    }
    int64_t group = 0;
    for (const plan & candidate : plans) {
        for (ctjs::ConstructOp made : candidate.made) {
            made->setAttr(kNativeMapSite, mlir::UnitAttr::get(context));
        }
        for (mlir::Value member : candidate.members) {
            if (auto arg = llvm::dyn_cast<mlir::BlockArgument>(member)) {
                auto fn = llvm::cast<ctjs::FuncOp>(arg.getOwner()->getParentOp());
                llvm::SmallVector<int64_t> groups(arg.getOwner()->getNumArguments(), -1);
                if (auto old = fn->getAttrOfType<mlir::DenseI64ArrayAttr>(kNativeMapArgGroups)) {
                    groups.assign(old.asArrayRef().begin(), old.asArrayRef().end());
                }
                groups[arg.getArgNumber()] = group;
                fn->setAttr(kNativeMapArgGroups, mlir::DenseI64ArrayAttr::get(context, groups));
            } else {
                member.getDefiningOp()->setAttr(
                    kNativeMapGroup,
                    mlir::IntegerAttr::get(mlir::IntegerType::get(context, 64), group));
            }
        }
        ++group;
        for (ctjs::GetPropertyOp get : candidate.methods) {
            get->setAttr(kNativeMapMethod, mlir::UnitAttr::get(context));
        }
        for (ctjs::GetPropertyOp size : candidate.sizes) {
            size->setAttr(kNativeMapAction, mlir::StringAttr::get(context, "size"));
        }
        for (ctjs::CallOp call : candidate.calls) {
            auto get = call.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
            call->setAttr(kNativeMapAction, mlir::StringAttr::get(context, keyOf(get.getKey())));
        }
    }
}

} // namespace ctcompile::ctnative
