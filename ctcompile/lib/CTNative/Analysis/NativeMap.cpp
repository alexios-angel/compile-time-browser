//===- NativeMap.cpp - standard identity and instance-use proofs ---------===//
#include "ctcompile/CTNative/Analysis/NativeMap.h"
#include "ctcompile/CTNative/Analysis/NativeClosure.h"
#include "ctcompile/CTNative/Analysis/NativeObjectIdentity.h"

#include "ClosedValueFlow.h"
#include "NativeMap/Presence.h"
#include "NativeMap/SnapshotCopies.h"
#include "OwnedGlobalRoots.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/IR/SymbolTable.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallVector.h"

#include <functional>
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
    return op != nullptr && (op->hasAttr(kNativeMapConstructor) || op->hasAttr(kNativeMapMethod) ||
                             op->hasAttr(kNativeMapSnapshotBuiltin));
}

bool isNativeMapSnapshot(mlir::Operation * op) {
    const auto action = nativeMapAction(op);
    return op != nullptr &&
           (action == "keys" || action == "values" || op->hasAttr(kNativeMapSnapshotCopy));
}

mlir::Value nativeMapRecordPayload(mlir::Value map) {
    if (auto call = map.getDefiningOp<ctjs::CallOp>(); nativeMapAction(call) == "set") {
        map = call.getReceiver();
    }
    auto made = map.getDefiningOp<ctjs::ConstructOp>();
    if (!made || !made->hasAttr(kNativeMapSite) || !made->hasAttr(kNativeMapRecords)) { return {}; }
    for (mlir::Operation * user : map.getUsers()) {
        auto call = llvm::dyn_cast<ctjs::CallOp>(user);
        if (call && call.getReceiver() == map && nativeMapAction(call) == "set") {
            return call.getArgs()[1];
        }
    }
    return {};
}

mlir::Value nativeMapRecordOrigin(mlir::Value read) {
    auto get = read.getDefiningOp<ctjs::CallOp>();
    if (!get || nativeMapAction(get) != "get" || !nativeMapRecordPayload(get.getReceiver())) {
        return {};
    }
    mlir::Value origin;
    const auto key = ctjs::constantKey(get.getArgs()[0]);
    for (mlir::Operation & op : *get->getBlock()) {
        if (&op == get.getOperation()) { return origin; }
        auto call = llvm::dyn_cast<ctjs::CallOp>(op);
        if (!call || call.getReceiver() != get.getReceiver()) { continue; }
        const auto action = nativeMapAction(call);
        if (action == "clear") { origin = {}; }
        if ((action == "set" || action == "delete") &&
            ctjs::constantKey(call.getArgs()[0]) == key) {
            origin = action == "set" ? call.getArgs()[1] : mlir::Value{};
        }
    }
    return {};
}

bool isNativeMapRecordStore(mlir::OpOperand & use) {
    auto call = llvm::dyn_cast<ctjs::CallOp>(use.getOwner());
    return call && use.getOperandNumber() == 3 && nativeMapAction(call) == "set" &&
           nativeMapRecordPayload(call.getReceiver());
}

namespace {

struct plan {
    llvm::SmallVector<ctjs::ConstructOp> made;
    llvm::SmallVector<mlir::Value> members;
    llvm::SmallVector<ctjs::GetPropertyOp> methods;
    llvm::SmallVector<ctjs::GetPropertyOp> sizes;
    llvm::SmallVector<ctjs::CallOp> calls;
    bool records = false;
};

// Record owners remain stack objects. The Map borrows their addresses and
// saved reads keep that address across overwrite/delete; neither owns a record.
// ponytail: one entry-block Map and literal keys; transport needs a wider lifetime proof.
std::string proveRecords(plan & candidate) {
    if (!llvm::any_of(candidate.calls, [](ctjs::CallOp call) {
            auto get = call.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
            if (ctjs::constantKey(get.getKey()) != "set" ||
                !call.getArgs()[1].getDefiningOp<ctjs::CreateObjectOp>()) {
                return false;
            }
            // Existing plain-object identity Maps keep their independent
            // owning representation. A lifted receiver needs its real record.
            return llvm::any_of(call.getArgs()[1].getUses(), [](mlir::OpOperand & use) {
                return llvm::isa<ctjs::CallDirectOp>(use.getOwner()) &&
                       use.getOperandNumber() == 0 && use.getOwner()->hasAttr("ctnative.receiver");
            });
        })) {
        return {};
    }
    if (candidate.made.size() != 1) { return "native record Map requires a direct local owner"; }
    auto map = candidate.made.front();
    for (mlir::Value member : candidate.members) {
        if (member == map.getResult()) { continue; }
        auto set = member.getDefiningOp<ctjs::CallOp>();
        if (!set || set.getReceiver() != map.getResult() || !member.use_empty()) {
            return "native record Map requires a direct local owner";
        }
    }
    auto function = map->getParentOfType<ctjs::FuncOp>();
    if (!function || map->getBlock() != &function.getBody().front()) {
        return "native record Map requires entry-frame ownership";
    }
    for (mlir::OpOperand & use : map.getResult().getUses()) {
        auto * user = use.getOwner();
        if (user->getBlock() != map->getBlock() ||
            !((llvm::isa<ctjs::GetPropertyOp>(user) && use.getOperandNumber() == 0) ||
              (llvm::isa<ctjs::CallOp>(user) && use.getOperandNumber() == 1))) {
            return "native record Map cannot escape its owning frame";
        }
    }
    for (ctjs::CallOp call : candidate.calls) {
        const auto action =
            ctjs::constantKey(call.getCallee().getDefiningOp<ctjs::GetPropertyOp>().getKey());
        if (call->getBlock() != map->getBlock() || action == "keys" || action == "values") {
            return "native record Map requires direct entry-block operations";
        }
    }
    llvm::StringMap<mlir::Value> entries;
    unsigned work = 0;
    for (mlir::Operation & op : *map->getBlock()) {
        if (++work > 65536) { return "native record Map lifetime proof exceeded its work limit"; }
        auto call = llvm::dyn_cast<ctjs::CallOp>(op);
        if (!call || call.getReceiver() != map.getResult()) { continue; }
        const auto action =
            ctjs::constantKey(call.getCallee().getDefiningOp<ctjs::GetPropertyOp>().getKey());
        if (action == "clear") {
            entries.clear();
            continue;
        }
        auto key = call.getArgs()[0].getDefiningOp<ctjs::ConstantOp>();
        auto text = key ? llvm::dyn_cast<ctjs::StringAttr>(key.getValue()) : ctjs::StringAttr{};
        if (!text) { return "native record Map requires literal String keys"; }
        if (action == "set") {
            auto record = call.getArgs()[1].getDefiningOp<ctjs::CreateObjectOp>();
            if (!record || record->getBlock() != map->getBlock() ||
                !record->isBeforeInBlock(call)) {
                return "native record Map requires enclosing record owners";
            }
            entries[text.getValue()] = record.getResult();
        } else if (action == "delete") {
            entries.erase(text.getValue());
        } else if (action == "get") {
            if (!entries.count(text.getValue())) {
                return "native record Map requires present reads";
            }
            // Saved aliases cannot outlive the enclosing owner or be published.
            for (mlir::OpOperand & use : call.getResult().getUses()) {
                if (++work > 65536) {
                    return "native record Map lifetime proof exceeded its work limit";
                }
                auto * user = use.getOwner();
                if (auto direct = llvm::dyn_cast<ctjs::CallDirectOp>(user);
                    direct && use.getOperandNumber() == 0 &&
                    user->getParentOfType<ctjs::FuncOp>() == function &&
                    direct->hasAttr("ctnative.receiver")) {
                    auto target = direct.getTarget();
                    if (target && target->hasAttr("ctnative.receiver") &&
                        !target.getBody().empty() &&
                        target.getBody().front().getNumArguments() == direct->getNumOperands()) {
                        // Final target admission must recheck its closed
                        // receiver uses; input annotations alone prove nothing.
                        continue;
                    }
                }
                if (use.getOperandNumber() != 0 ||
                    user->getParentOfType<ctjs::FuncOp>() != function ||
                    !llvm::isa<ctjs::GetPropertyOp, ctjs::SetPropertyOp>(user) ||
                    !ctjs::ordinaryKey(user->getOperand(1))) {
                    return "native record Map alias requires confined data-field uses";
                }
            }
        }
    }
    candidate.records = true;
    return {};
}

// The value flow unifies C++ SCHEMAS, never runtime identities. Two fresh Maps
// passed to the same parameter need one key/value carrier but remain distinct
// owning allocations. Every producer in a family must be checked below: a
// union containing one Map does not prove that another incoming value is one.
bool unify(closedValueFlow & flow, mlir::Value left, mlir::Value right) {
    flow.add(left);
    flow.add(right);
    if (flow.find(left) == flow.find(right)) { return false; }
    flow.join(left, right);
    return true;
}

// The Map-specific edges around closedValueFlow::build: an environment read
// is its captured slot, and `.set` returns its receiver.
void connectEnvironmentReads(closedValueFlow & flow, mlir::ModuleOp module) {
    llvm::StringMap<ctjs::CreateClosureOp> environments;
    module.walk([&](ctjs::CreateClosureOp made) {
        if (!environmentTarget(made).empty()) { environments[environmentTarget(made)] = made; }
    });
    module.walk([&](ctjs::LoadUpvalueOp read) {
        if (!read->hasAttr(kNativeEnvironmentRead)) { return; }
        auto made = environments.lookup(environmentTarget(read));
        if (made && read.getIndex() >= 0 &&
            static_cast<size_t>(read.getIndex()) < made.getUpvalues().size()) {
            flow.join(read.getResult(), made.getUpvalues()[static_cast<size_t>(read.getIndex())]);
        }
    });
}

void connectSetReceivers(closedValueFlow & flow, mlir::ModuleOp module) {
    module.walk([&](ctjs::CallOp call) {
        auto get = call.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
        if (get && ctjs::constantKey(get.getKey()) == "set") {
            flow.join(call.getReceiver(), call.getResult());
        }
    });
}

// A container's stored values and its get results share one schema.
// Discover recursively: a get result may itself become a Map receiver.
// Primitive families remain outside the Map plans; a mixed Map/scalar
// family reaches collect(), whose producer check refuses it.
void connectPayloads(closedValueFlow & flow, mlir::ModuleOp module,
                     llvm::ArrayRef<ctjs::ConstructOp> sites) {
    bool changed = true;
    while (changed) {
        changed = false;
        llvm::DenseSet<mlir::Value> maps;
        for (ctjs::ConstructOp made : sites) { maps.insert(flow.find(made.getResult())); }
        llvm::DenseMap<mlir::Value, mlir::Value> payload;
        module.walk([&](ctjs::CallOp call) {
            const auto receiver = flow.find(call.getReceiver());
            if (!maps.contains(receiver)) { return; }
            auto get = call.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
            if (!get || get.getObject() != call.getReceiver()) { return; }
            const auto key = ctjs::constantKey(get.getKey());
            mlir::Value value;
            if (key == "set" && call.getArgs().size() == 2) {
                value = call.getArgs()[1];
            } else if (key == "get" && call.getArgs().size() == 1) {
                value = call.getResult();
            } else {
                return;
            }
            auto [at, fresh] = payload.try_emplace(receiver, value);
            if (!fresh) { changed |= unify(flow, at->second, value); }
        });
    }
}

// A method value may only be called on the exact instance from which it was
// loaded. Detached methods, .call/.apply, property writes, escaping instances
// and real loop-carried aliases need additional proofs and are refused.
std::string collect(plan & out, closedValueFlow & graph,
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
            if (!closedValueFlow::closed(fn) || arg.getOwner() != &fn.getBody().front() ||
                arg.getArgNumber() < 3 || graph.callers[fn].empty()) {
                return "native Map parameter requires a closed function with visible callers";
            }
            for (ctjs::CallDirectOp call : graph.callers[fn]) {
                if (!closedValueFlow::exactCall(call, fn)) {
                    return "native Map call requires matching argument and parameter counts";
                }
            }
        } else if (auto call = object.getDefiningOp<ctjs::CallDirectOp>()) {
            auto fn = call.getTarget();
            if (!closedValueFlow::closed(fn) || !closedValueFlow::exactCall(call, fn) ||
                graph.returns[fn].empty()) {
                return "native Map result requires a closed function with visible returns";
            }
        } else if (auto call = object.getDefiningOp<ctjs::CallOp>()) {
            auto get = call.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
            if (!get || (ctjs::constantKey(get.getKey()) != "set" &&
                         ctjs::constantKey(get.getKey()) != "get")) {
                return "native Map flow contains an unproved call result";
            }
        } else if (auto read = object.getDefiningOp<ctjs::LoadUpvalueOp>();
                   read && read->hasAttr(kNativeEnvironmentRead)) {
            // The graph connected this extraction to its proved owning slot.
        } else if (!sites.contains(object.getDefiningOp())) {
            return ("native Map flow contains a non-Map producer `" +
                    object.getDefiningOp()->getName().getStringRef() + "`")
                .str();
        }
        for (mlir::OpOperand & use : object.getUses()) {
            if (erasedCapture(use)) { continue; }
            if (llvm::isa<ctjs::CreateClosureOp>(use.getOwner()) && use.getOperandNumber() >= 2 &&
                !environmentTarget(use.getOwner()).empty()) {
                continue;
            }
            if (auto call = llvm::dyn_cast<ctjs::CallDirectOp>(use.getOwner());
                call && use.getOperandNumber() >= 3) {
                auto fn = call.getTarget();
                if (!closedValueFlow::closed(fn)) {
                    return "native Map argument requires a closed callee";
                }
                if (!closedValueFlow::exactCall(call, fn)) {
                    return "native Map call requires matching argument and parameter counts";
                }
                continue;
            }
            if (auto ret = llvm::dyn_cast<ctjs::ReturnOp>(use.getOwner())) {
                if (!closedValueFlow::closed(ret->getParentOfType<ctjs::FuncOp>())) {
                    return "native Map return requires a closed function";
                }
                continue;
            }
            if (auto call = llvm::dyn_cast<ctjs::CallOp>(use.getOwner());
                call && use.getOperandNumber() == 1) {
                auto get = call.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
                if (get && get.getObject() == object) { continue; }
            }
            if (auto call = llvm::dyn_cast<ctjs::CallOp>(use.getOwner());
                call && call.getArgs().size() == 2 && use.getOperandNumber() == 3) {
                auto get = call.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
                if (get && ctjs::constantKey(get.getKey()) == "set" &&
                    get.getObject() == call.getReceiver()) {
                    continue;
                }
            }
            auto get = llvm::dyn_cast<ctjs::GetPropertyOp>(use.getOwner());
            if (!get || use.getOperandNumber() != 0) {
                return ("native Map instance escapes or is mutated through `" +
                        use.getOwner()->getName().getStringRef() + "`")
                    .str();
            }
            const llvm::StringRef key = ctjs::constantKey(get.getKey());
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

std::string provePayloads(mlir::ModuleOp module, llvm::ArrayRef<plan> plans,
                          closedValueFlow & graph, llvm::DenseMap<mlir::Value, unsigned> & families,
                          const llvm::DenseSet<mlir::Operation *> & snapshotCopies,
                          const OwnedGlobalRoots * globals) {
    llvm::SmallVector<llvm::SmallVector<unsigned>, 4> children(plans.size());
    for (auto [index, candidate] : llvm::enumerate(plans)) {
        for (ctjs::CallOp call : candidate.calls) {
            auto get = call.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
            if (ctjs::constantKey(get.getKey()) != "set") { continue; }
            const auto child = families.find(graph.find(call.getArgs()[1]));
            if (child != families.end()) { children[index].push_back(child->second); }
        }
    }
    llvm::SmallVector<unsigned> state(plans.size(), 0);
    std::function<bool(unsigned)> acyclic = [&](unsigned index) {
        if (state[index] == 1) { return false; }
        if (state[index] == 2) { return true; }
        state[index] = 1;
        for (unsigned child : children[index]) {
            if (!acyclic(child)) { return false; }
        }
        state[index] = 2;
        return true;
    };
    for (unsigned index = 0; index < plans.size(); ++index) {
        if (!acyclic(index)) { return "native Map payload schemas contain an ownership cycle"; }
    }

    llvm::SmallVector<ctjs::CallOp> calls;
    llvm::SmallVector<ctjs::CallOp> reads;
    llvm::SmallVector<ctjs::CallOp> optionalReads;
    llvm::SmallVector<ctjs::CallOp> typedReads;
    llvm::SmallVector<ctjs::GetPropertyOp> sizes;
    llvm::DenseSet<mlir::Operation *> publishedCalls;
    llvm::DenseMap<mlir::Value, PrimitiveAlternatives> parameters;
    if (globals && globals->proved()) {
        for (const auto & root : globals->roots()) {
            if (!root.methodTable || !root.methodTable->capturedMap) { continue; }
            for (ctjs::CallOp call : root.methodTable->capturedMap->calls) {
                publishedCalls.insert(call);
            }
            // These are current semantic actual/formal facts from the complete
            // host body worklist, not solver types or input annotations. They
            // may seed primitive alternatives. Only a checked write of that
            // value can establish a payload fact, never the signature alone.
            for (const HostMethodParameters & method : root.methodTable->capturedMap->parameters) {
                auto function = method.function;
                auto & body = function.getBody().front();
                const auto offset = body.getNumArguments() - method.alternatives.size();
                for (unsigned index = 0; index < method.alternatives.size(); ++index) {
                    const auto argument = body.getArgument(static_cast<unsigned>(offset) + index);
                    auto [found, inserted] =
                        parameters.try_emplace(argument, method.alternatives[index]);
                    if (!inserted) {
                        found->second = found->second.joined(method.alternatives[index]);
                    }
                }
            }
        }
    }
    for (auto [index, candidate] : llvm::enumerate(plans)) {
        llvm::append_range(calls, candidate.calls);
        llvm::append_range(sizes, candidate.sizes);
        // Recognize a closed primitive mixed family before the monotone
        // solver starts. Otherwise an early broad/optional result would
        // permanently pollute a read whose last literal write is exact.
        // Nonliteral published writes already have an independent primitive
        // body/actual proof. A local get is also a candidate, but its scalar
        // tag must be proved from exact contents below; neither this census
        // nor the schema supplies the tag. Arbitrary parameters stay excluded.
        llvm::DenseSet<mlir::Value> savedReads;
        for (ctjs::CallOp call : candidate.calls) {
            auto method = call.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
            const auto action = ctjs::constantKey(method.getKey());
            if (action == "get" || action == "has" || action == "delete") {
                savedReads.insert(call.getResult());
            }
        }
        for (ctjs::GetPropertyOp size : candidate.sizes) { savedReads.insert(size.getResult()); }
        const auto scalarCandidate = [&](auto && self, mlir::Value value, unsigned depth) -> bool {
            if (depth > 32) { return false; }
            if (savedReads.contains(value)) { return true; }
            if (auto constant = value.getDefiningOp<ctjs::ConstantOp>()) {
                return ctjs::isPrimitiveAttr(constant.getValue());
            }
            auto result = llvm::dyn_cast<mlir::OpResult>(value);
            auto branch =
                result ? llvm::dyn_cast<mlir::scf::IfOp>(result.getOwner()) : mlir::scf::IfOp{};
            if (!branch) { return false; }
            for (mlir::Region & region : branch->getRegions()) {
                if (!region.hasOneBlock()) { return false; }
                auto yield = llvm::dyn_cast<mlir::scf::YieldOp>(region.front().getTerminator());
                if (!yield || result.getResultNumber() >= yield.getNumOperands() ||
                    !self(self, yield.getOperand(result.getResultNumber()), depth + 1)) {
                    return false;
                }
            }
            // This is only a candidate census. Presence independently checks
            // both reaching payload tags before a read can acquire a type.
            savedReads.insert(value);
            return true;
        };
        bool primitive = true, boolean = false, number = false, string = false;
        bool nonliteral = false;
        for (ctjs::CallOp call : candidate.calls) {
            auto method = call.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
            if (ctjs::constantKey(method.getKey()) != "set") { continue; }
            auto constant = call.getArgs()[1].getDefiningOp<ctjs::ConstantOp>();
            if (!constant) {
                nonliteral = true;
                primitive &= publishedCalls.contains(call) ||
                             scalarCandidate(scalarCandidate, call.getArgs()[1], 0);
                continue;
            }
            auto value = constant.getValue();
            boolean |= llvm::isa<ctjs::BooleanAttr>(value);
            number |= llvm::isa<ctjs::NumberAttr>(value);
            string |= llvm::isa<ctjs::StringAttr>(value);
            primitive &= ctjs::isPrimitiveAttr(value);
        }
        // A conditional or saved value may supply every String/absent
        // alternative without a direct literal set. Such a family can need
        // an exact read seed too; only Presence establishes that seed.
        const bool mixed = primitive && (nonliteral || (boolean && (number != string)));
        for (ctjs::CallOp read : candidate.calls) {
            auto method = read.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
            if (ctjs::constantKey(method.getKey()) != "get") { continue; }
            if (mixed) { typedReads.push_back(read); }
            if (!children[index].empty()) {
                reads.push_back(read);
            } else if (publishedCalls.contains(read)) {
                // Ownership supplies only the closed call census. Presence
                // is rederived below for this instance/key on every path;
                // an unproved primitive read keeps its nullable result.
                optionalReads.push_back(read);
            }
        }
    }
    return map_detail::provePresence(
        module, calls, sizes, reads, optionalReads, typedReads, snapshotCopies,
        [&](mlir::Value value) { return graph.find(value); }, parameters, globals);
}

} // namespace

void prepareNativeMaps(mlir::ModuleOp module, const OwnedGlobalRoots * globals) {
    // Proof annotations are derived, not trusted input, including on reruns.
    module.walk([&](mlir::Operation * op) {
        for (llvm::StringRef name :
             {kNativeMapSite, kNativeMapAction, kNativeMapMethod, kNativeMapConstructor,
              kNativeMapReason, kNativeMapGroup, kNativeMapArgGroups, kNativeMapPresent,
              kNativeMapReadType, kNativeMapWriteType, kNativeMapKeyType, kNativeMapSnapshotCopy,
              kNativeMapSnapshotBuiltin, kNativeMapRecords}) {
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
    closedValueFlow graph;
    for (ctjs::ConstructOp made : madeSites) { graph.add(made.getResult()); }
    connectEnvironmentReads(graph, module);
    graph.build(module);
    connectSetReceivers(graph, module);
    connectPayloads(graph, module, madeSites);
    llvm::SmallVector<plan, 4> plans;
    llvm::DenseMap<mlir::Value, unsigned> families;
    for (ctjs::ConstructOp made : madeSites) {
        mlir::Value root = graph.find(made.getResult());
        auto inserted = families.try_emplace(root, static_cast<unsigned>(plans.size()));
        if (inserted.second) { plans.emplace_back(); }
        plans[inserted.first->second].made.push_back(made);
    }
    for (mlir::Value member : graph.nodes()) {
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
    map_detail::snapshotCopies copies;
    if (reason.empty()) { reason = map_detail::collectSnapshotCopies(module, calls, copies); }
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
                   load && load.getName() != "Map" && !copies.builtins.contains(op)) {
            // A complete live source-owner proof identifies this load as the
            // ordinary exported root or an independently proved Number or Boolean.
            // Both require the complete live family and source effects; names,
            // observations and native annotations never authorize a read.
            if (globals && globals->proved()) {
                if (globals->lookup(load)) { return; }
                if (globals->objectGlobal(load)) { return; }
                if (globals->mutableScalarRead(load)) { return; }
                const auto * scalar = globals->scalarRead(load);
                if (scalar &&
                    (scalar->alternatives.tag() == mlir::TypeID::get<ctjs::NumberAttr>() ||
                     scalar->alternatives.tag() == mlir::TypeID::get<ctjs::BooleanAttr>() ||
                     scalar->alternatives.tag() == mlir::TypeID::get<ctjs::StringAttr>())) {
                    return;
                }
            }
            for (mlir::OpOperand & use : load.getResult().getUses()) {
                if (!llvm::isa<ctjs::CallDirectOp>(use.getOwner()) || use.getOperandNumber() != 2) {
                    reason = "standard Map identity is unproved with other host/global value reads";
                    break;
                }
            }
        } else if (llvm::isa<ctjs::CallOp>(op) && !calls.contains(op) &&
                   !copies.calls.contains(op)) {
            reason = "standard Map identity is unproved across an unknown call";
        } else if (llvm::isa<ctjs::ConstructOp>(op) && !sites.contains(op)) {
            reason = "standard Map identity is unproved across an unknown constructor";
        } else if (llvm::isa<ctjs::PassNewTargetOp, ctjs::DeleteNamedOp, ctjs::CallSpreadOp,
                             ctjs::ConstructSpreadOp, ctjs::DynamicImportOp>(op) ||
                   op->getName().getStringRef().starts_with("ctjs.module_")) {
            reason = "standard Map identity is unproved across dynamic invocation or deletion";
        }
    });
    // Presence may preserve membership across a snapshot copy only after its
    // live builtin, iterator-use and whole-module identity proofs succeed.
    if (reason.empty()) {
        reason = provePayloads(module, plans, graph, families, copies.calls, globals);
    }
    for (plan & candidate : plans) {
        if (reason.empty()) { reason = proveRecords(candidate); }
    }
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
    for (mlir::Operation * builtin : copies.builtins) {
        builtin->setAttr(kNativeMapSnapshotBuiltin, mlir::UnitAttr::get(context));
    }
    for (mlir::Operation * copy : copies.calls) {
        copy->setAttr(kNativeMapSnapshotCopy, mlir::UnitAttr::get(context));
    }
    int64_t group = 0;
    for (const plan & candidate : plans) {
        for (ctjs::ConstructOp made : candidate.made) {
            made->setAttr(kNativeMapSite, mlir::UnitAttr::get(context));
            if (candidate.records) {
                made->setAttr(kNativeMapRecords, mlir::UnitAttr::get(context));
            }
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
            call->setAttr(kNativeMapAction,
                          mlir::StringAttr::get(context, ctjs::constantKey(get.getKey())));
            if (candidate.records && ctjs::constantKey(get.getKey()) == "get") {
                call->setAttr(kNativeMapPresent, mlir::UnitAttr::get(context));
            }
        }
    }
}

} // namespace ctcompile::ctnative
