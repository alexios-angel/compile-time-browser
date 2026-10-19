//===- EscapeAnalysis.cpp - Escape analysis for ctjs IR -------------------===//
//
// Escape analysis for create_object/create_array sites in ctjs IR.
//
// The MLIR sparse dataflow framework supplies the worklist, the fixpoint and
// the joins at block arguments (through BranchOpInterface successor operands).
// Per-operand roles (sink, carry or neither) come from the Arg<..., [CTJS_Sink...]>
// decorators in CTJSOps.td, read here through ctjs::EscapeEffectOpInterface
// without naming any operation. This file owns: the two tracked site kinds, the
// default rule that makes an unannotated operand sink, a small switch for the
// three ops whose role depends on a kind attribute, the post-fixpoint verdict
// walk, and two whole-function refusals (`arguments`/rest built after the
// prologue, and suspension).
//
// Sinks are applied in the post-pass, not in visitOperation, because
// AbstractSparseForwardDataFlowAnalysis::visitOperation exits early on
// operations with no results, and the ops that make an object escape (return,
// throw, store_global, set_property, append, cell_set, store_upvalue, set_proto,
// define_accessor, copy_props, delete_*) have none. A sink placed there would
// never fire for `return {}`. The unit test's return/throw rows guard this.
//
//===----------------------------------------------------------------------===//
#include "ctcompile/CTNative/Analysis/EscapeAnalysis.h"

#include "ctcompile/CTJS/IR/CTJSDialect.h"
#include "ctcompile/CTJS/IR/CTJSEscapeEffects.h"
#include "ctcompile/CTJS/IR/CTJSTypes.h"

#include "mlir/Analysis/DataFlow/DeadCodeAnalysis.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/IR/Block.h"
#include "mlir/IR/Location.h"
#include "mlir/Interfaces/ControlFlowInterfaces.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/raw_ostream.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <utility>

namespace ctcompile::ctnative {

bool isTrackedSite(mlir::Operation * op) {
    return llvm::isa<ctjs::CreateObjectOp, ctjs::CreateArrayOp>(op);
}

AliasValue AliasValue::none() {
    AliasValue v;
    v.initialized_ = true;
    return v;
}

AliasValue AliasValue::external() {
    AliasValue v;
    v.initialized_ = true;
    v.external_ = true;
    return v;
}

AliasValue AliasValue::site(mlir::Operation * site) {
    // The only place a site enters an alias set. Only create_object and
    // create_array are accepted, because the .td's `Neither` operand roles were
    // proved only for those allocation kinds.
    assert(isTrackedSite(site) && "only create_object/create_array may be an alias site");
    AliasValue v;
    v.initialized_ = true;
    v.sites_.push_back(site);
    return v;
}

AliasValue AliasValue::join(const AliasValue & lhs, const AliasValue & rhs) {
    if (lhs.isUninitialized()) { return rhs; }
    if (rhs.isUninitialized()) { return lhs; }
    AliasValue out;
    out.initialized_ = true;
    out.external_ = lhs.external_ || rhs.external_;
    std::set_union(lhs.sites_.begin(), lhs.sites_.end(), rhs.sites_.begin(), rhs.sites_.end(),
                   std::back_inserter(out.sites_));
    return out;
}

bool AliasValue::onlyTrackedSites() const {
    return llvm::all_of(sites_, [](mlir::Operation * site) { return isTrackedSite(site); });
}

bool AliasValue::operator==(const AliasValue &) const = default;

void AliasValue::print(llvm::raw_ostream & os) const {
    if (!initialized_) {
        os << "<uninitialized>";
        return;
    }
    os << '{';
    llvm::ListSeparator separator;
    for (mlir::Operation * site : sites_) { os << separator << site->getName().getStringRef(); }
    if (external_) { os << separator << "external"; }
    os << '}';
}

namespace {

bool isValueTyped(mlir::Value v) {
    return llvm::isa<ctjs::ValueType>(v.getType());
}

/// Maps an escape route to its EscapeReason. A route's name is the spelling of
/// a CTNative_EscapeReason case, and this is the only bridge between the two:
/// the ctjs dialect knows nothing of ctnative. A route the enum cannot name is
/// a .td/.h mismatch; the unit test asserts every reason by name, so it cannot
/// hide behind the UnknownOp fallback.
EscapeReason reasonOf(mlir::SideEffects::Resource * route) {
    const std::optional<EscapeReason> reason = symbolizeEscapeReason(route->getName());
    assert(reason.has_value() && "an escape route's name is not an EscapeReason case");
    return reason.value_or(EscapeReason::UnknownOp);
}

using EscapeInstance = mlir::SideEffects::EffectInstance<ctjs::EscapeEffects::Effect>;

llvm::SmallVector<EscapeInstance, 6> escapeEffects(ctjs::EscapeEffectOpInterface roles) {
    llvm::SmallVector<EscapeInstance, 6> effects;
    roles.getEffects(effects);
    return effects;
}

/// The operands of `roles`' effects of kind E, in effect order.
template <class E>
llvm::SmallVector<mlir::OpOperand *, 2> operandsWith(ctjs::EscapeEffectOpInterface roles) {
    llvm::SmallVector<mlir::OpOperand *, 2> out;
    for (const EscapeInstance & effect : escapeEffects(roles)) {
        auto * on = effect.getEffectValue<mlir::OpOperand *>();
        if (llvm::isa<E>(effect.getEffect()) && on != nullptr) { out.push_back(on); }
    }
    return out;
}

bool isLive(mlir::DataFlowSolver & solver, mlir::Block * block) {
    const auto * executable =
        solver.lookupState<mlir::dataflow::Executable>(solver.getProgramPointBefore(block));
    return executable != nullptr && executable->isLive();
}

AliasValue aliasOf(const mlir::DataFlowSolver & solver, mlir::Value value) {
    const AliasLattice * lattice = solver.lookupState<AliasLattice>(value);
    return lattice != nullptr ? lattice->getValue() : AliasValue{};
}

/// Spends one unit of `work` against `limit`; false once it is exhausted.
bool spendUsed(std::size_t & work, std::size_t limit) {
    if (work == limit) { return false; }
    ++work;
    return true;
}

/// Roles for the three ops whose behaviour depends on a kind attribute, which
/// an Arg decorator in the .td cannot express. Returns nullopt for every other
/// op. A `converted` answer means the op may run the operand's own
/// valueOf/toString (may_reenter in bytecode_opcodes.def), so the operand sinks.
std::optional<RoleOf> kindSwitch(mlir::Operation * op) {
    constexpr RoleOf neither{OperandRole::Neither, EscapeReason::Confined};
    constexpr RoleOf converted{OperandRole::Sink, EscapeReason::Converted};
    if (auto unary = llvm::dyn_cast<ctjs::UnaryOp>(op)) {
        switch (unary.getKind()) {
        case ctjs::UnaryKind::Not:
        case ctjs::UnaryKind::TypeOf:
        case ctjs::UnaryKind::Void: return neither; // total; no conversion runs
        case ctjs::UnaryKind::Neg:
        case ctjs::UnaryKind::Plus:
        case ctjs::UnaryKind::BitNot: return converted; // negate / to_number, may_reenter 1
        }
        return converted;
    }
    if (auto compare = llvm::dyn_cast<ctjs::CompareOp>(op)) {
        // Strict equality compares bits/contents without running user code
        // (value::strict_equals in script/value/base.hpp); loose equality and
        // the four relational operators convert their operands.
        return compare.getKind() == ctjs::CompareKind::StrictEq ? neither : converted;
    }
    if (auto convert = llvm::dyn_cast<ctjs::ConvertOp>(op)) {
        // ToBoolean runs no user code. ToObject returns an object operand
        // unchanged, so it could be a carry, but it is treated as a sink until
        // something needs the precision.
        return convert.getKind() == ctjs::ConvertKind::ToBoolean ? neither : converted;
    }
    return std::nullopt;
}

} // namespace

RoleOf operandRole(mlir::Operation * op, unsigned index) {
    const mlir::Value operand = op->getOperand(index);
    constexpr RoleOf neither{OperandRole::Neither, EscapeReason::Confined};
    if (!isValueTyped(operand)) { return neither; } // an i1, an i32, a context: not an object

    if (const std::optional<RoleOf> refined = kindSwitch(op)) { return *refined; }

    if (auto roles = llvm::dyn_cast<ctjs::EscapeEffectOpInterface>(op)) {
        RoleOf out = neither;
        for (const EscapeInstance & effect : escapeEffects(roles)) {
            mlir::OpOperand * on = effect.getEffectValue<mlir::OpOperand *>();
            if (on == nullptr || on->getOperandNumber() != index) { continue; }
            if (llvm::isa<ctjs::EscapeEffects::Sink>(effect.getEffect())) {
                // A sink outranks a carry on the same position. Alias and
                // provenance propagation must still retain the carry edge.
                return RoleOf{OperandRole::Sink, reasonOf(effect.getResource())};
            }
            if (llvm::isa<ctjs::EscapeEffects::Carry>(effect.getEffect())) {
                out = RoleOf{OperandRole::Carry, EscapeReason::Confined};
            }
        }
        return out;
    }

    // A branch terminator's value operands are its successor operands, which
    // the framework joins into the successor's block arguments: a carry. Its
    // non-successor operands (a condition, a switch flag) are not values and
    // were answered above.
    if (llvm::isa<mlir::BranchOpInterface>(op)) {
        return RoleOf{OperandRole::Carry, EscapeReason::Confined};
    }

    // The default rule: with no annotation, every value operand sinks. This
    // makes omission sound. A new ctjs operation cannot weaken the analysis by
    // being missing from a table; it only lacks a decorator, to be added once
    // its VM behaviour has been read.
    return RoleOf{OperandRole::Sink, EscapeReason::UnknownOp};
}

bool isBoxedSite(mlir::Operation * op, EscapeReason & reason) {
    auto roles = llvm::dyn_cast<ctjs::EscapeEffectOpInterface>(op);
    if (!roles) { return false; }
    for (const EscapeInstance & effect : escapeEffects(roles)) {
        if (!llvm::isa<ctjs::EscapeEffects::BoxedSite>(effect.getEffect())) { continue; }
        reason = reasonOf(effect.getResource());
        return true;
    }
    return false;
}

std::optional<unsigned> allocationPc(mlir::Operation * op) {
    // The importer's location_for (BytecodeImport.cpp) writes a NameLoc
    // "program:<id>:<fn>:<at>", fused with a FileLineColLoc or alone. The
    // trailing <at> is the bytecode pc.
    mlir::NameLoc name;
    if (auto fused = llvm::dyn_cast<mlir::FusedLoc>(op->getLoc())) {
        for (mlir::Location part : fused.getLocations()) {
            if ((name = llvm::dyn_cast<mlir::NameLoc>(part))) { break; }
        }
    } else {
        name = llvm::dyn_cast<mlir::NameLoc>(op->getLoc());
    }
    if (!name) { return std::nullopt; }
    llvm::StringRef text = name.getName().getValue();
    if (!text.consume_front("program:")) { return std::nullopt; }
    const std::size_t colon = text.rfind(':');
    if (colon == llvm::StringRef::npos) { return std::nullopt; }
    unsigned at = 0;
    if (text.substr(colon + 1).getAsInteger(10, at)) { return std::nullopt; }
    return at;
}

void EscapeAnalysis::setToEntryState(AliasLattice * lattice) {
    propagateIfChanged(lattice, lattice->join(AliasValue::external()));
}

mlir::LogicalResult EscapeAnalysis::visitOperation(mlir::Operation * op,
                                                   llvm::ArrayRef<const AliasLattice *> operands,
                                                   llvm::ArrayRef<AliasLattice *> results) {
    // The framework calls this only for operations with results, which is why
    // no sink lives here. Every result of one operation gets the same answer:
    // no ctjs operation has two value results (catch_land's first is an i32).
    AliasValue answer;
    if (isTrackedSite(op)) {
        answer = AliasValue::site(op);
    } else if (llvm::isa<ctjs::ConstantOp>(op)) {
        answer = AliasValue::none(); // a primitive; strings are untracked
    } else if (auto roles = llvm::dyn_cast<ctjs::EscapeEffectOpInterface>(op)) {
        // Carry: the result may be any carried operand's object, plus an
        // external one, because the only carrier (ctjs.iterable) substitutes a
        // fresh array on every non-array arm. An interface operation with no
        // carry is external, like everything else.
        answer = AliasValue::external();
        for (mlir::OpOperand * on : operandsWith<ctjs::EscapeEffects::Carry>(roles)) {
            answer = AliasValue::join(answer, operands[on->getOperandNumber()]->getValue());
        }
    } else {
        answer = AliasValue::external();
    }
    // Only tracked sites may enter an alias set; in particular a create_closure
    // (or any other allocating op) must never appear in one.
    assert(answer.onlyTrackedSites() && "an untracked operation entered an alias set");

    for (AliasLattice * result : results) {
        const AliasValue forThis = isValueTyped(result->getAnchor()) ? answer : AliasValue::none();
        propagateIfChanged(result, result->join(forThis));
    }
    return mlir::success();
}

namespace {

/// The container a Stored verdict writes into: the array of a create_array
/// element, an append's array (operand 1 is the element), or a set_property's
/// object (operand 2 is the value). Null for anything else.
mlir::Value storageTargetValue(const Verdict & verdict) {
    if (verdict.reason != EscapeReason::Stored || verdict.by == nullptr) { return {}; }
    if (auto array = llvm::dyn_cast<ctjs::CreateArrayOp>(verdict.by)) {
        if (verdict.position < array.getElements().size()) { return array.getResult(); }
    } else if (auto append = llvm::dyn_cast<ctjs::AppendOp>(verdict.by)) {
        if (verdict.position == 1) { return append.getArray(); }
    } else if (auto store = llvm::dyn_cast<ctjs::SetPropertyOp>(verdict.by)) {
        if (verdict.position == 2) { return store.getObject(); }
    }
    return {};
}

/// Checks that every operation nested in scf.if/scf.while regions has solver
/// state for all its operands and results, so refineArrayRetention may use the
/// incomplete top-level storage records. Spends `work` against `workLimit`.
bool structuredRetentionInputs(const mlir::DataFlowSolver & solver, ctjs::FuncOp function,
                               std::size_t & work, std::size_t workLimit) {
    const auto spend = [&]() { return spendUsed(work, workLimit); };
    const auto initialized = [&](mlir::Value value) {
        if (!spend() || !value) { return false; }
        if (!isValueTyped(value)) { return true; }
        const AliasLattice * lattice = solver.lookupState<AliasLattice>(value);
        return lattice != nullptr && !lattice->getValue().isUninitialized();
    };
    const auto supportedAncestors = [&](mlir::Operation * site) {
        // The complete contents query below rejects repeated root CFG blocks
        // and permits allocations in counted loops only in exactly-once bodies.
        // These ancestors supply no instance proof without that complete query.
        for (mlir::Operation * parent = site->getParentOp(); parent != function.getOperation();
             parent = parent->getParentOp()) {
            if (!spend() || !llvm::isa<mlir::scf::IfOp, mlir::scf::WhileOp>(parent)) {
                return false;
            }
        }
        return true;
    };
    // computeVerdicts' direct-storage/load records cover only top-level CFG
    // operations. When those records are incomplete, walk every nested op here
    // and require each operand/result to have a solver lattice; scf.if and
    // scf.while regions are accepted, any other region refuses. This walk adds
    // no alias or contents fact: nested sites still need the
    // computeArrayContents proof.
    return !function.getBody()
                .walk<mlir::WalkOrder::PreOrder>([&](mlir::Operation * operation) {
                    if (!spend() ||
                        (operation->getNumRegions() != 0 &&
                         !llvm::isa<mlir::scf::IfOp, mlir::scf::WhileOp>(operation)) ||
                        (isTrackedSite(operation) &&
                         operation->getParentRegion() != &function.getBody() &&
                         !supportedAncestors(operation)) ||
                        !llvm::all_of(operation->getOperands(), initialized) ||
                        !llvm::all_of(operation->getResults(), initialized)) {
                        return mlir::WalkResult::interrupt();
                    }
                    for (unsigned index = 0; index < operation->getNumOperands(); ++index) {
                        const RoleOf role = operandRole(operation, index);
                        if (role.role == OperandRole::Sink && role.reason == EscapeReason::Stored &&
                            !initialized(storageTargetValue({role.reason, operation, index}))) {
                            return mlir::WalkResult::interrupt();
                        }
                    }
                    return mlir::WalkResult::advance();
                })
                .wasInterrupted();
}

/// Turns Stored (and array get/set receiver) verdicts back into Confined when
/// computeArrayContents proves the function's containers form an acyclic graph
/// and the site is reachable from no return value. Commits all-or-nothing.
void refineArrayRetention(EscapeVerdicts & verdicts, const mlir::DataFlowSolver & solver,
                          ctjs::FuncOp function, std::size_t workLimit) {
    const auto retentionReason = [&](const Verdict & verdict) {
        if (verdict.reason != EscapeReason::UnknownOp || verdict.by == nullptr ||
            verdict.by->getParentRegion() == &function.getBody()) {
            return verdict.reason;
        }
        // computeVerdicts marks a value captured implicitly by a nested region
        // as UnknownOp even when its use is a known sink. Recover that use's
        // real reason from operandRole; the candidate filter still limits
        // Passed to array get/set receivers.
        const RoleOf role = operandRole(verdict.by, verdict.position);
        return role.role == OperandRole::Sink ? role.reason : verdict.reason;
    };
    const auto candidate = [&](mlir::Operation * site, const Verdict & verdict) {
        const EscapeReason reason = retentionReason(verdict);
        return (reason == EscapeReason::UnknownOp && verdict.by == nullptr &&
                site->getParentRegion() != &function.getBody()) ||
               reason == EscapeReason::Stored ||
               (llvm::isa<ctjs::CreateArrayOp>(site) && reason == EscapeReason::Passed &&
                verdict.position == 0 &&
                llvm::isa_and_nonnull<ctjs::GetPropertyOp, ctjs::SetPropertyOp>(verdict.by));
    };
    if (workLimit == 0 || verdicts.unvisitedSites != 0 || verdicts.unvisitedOperands != 0 ||
        verdicts.wholeFunction || !llvm::any_of(verdicts.sites, [&](const auto & entry) {
            return candidate(entry.first, entry.second);
        })) {
        return;
    }
    if ((!verdicts.directStorage.complete || !verdicts.directLoads.complete) &&
        !structuredRetentionInputs(solver, function, verdicts.arrayRetentionWork, workLimit)) {
        return;
    }

    // Alias candidates alone cannot discharge a sink. The contents query must
    // prove every operation in this function, including effects after the last
    // read and the exact origin of a saved, later-returned read.
    const ArrayContentsEvidence contents =
        computeArrayContents(function, workLimit - verdicts.arrayRetentionWork);
    verdicts.arrayRetentionWork += contents.work;
    if (!contents.complete) { return; }
    const auto spend = [&]() { return spendUsed(verdicts.arrayRetentionWork, workLimit); };

    // This refinement cannot prove ownership of cyclic structures, so reject
    // any cycle in the union of all writes (including ones later overwritten
    // or deleted). Duplicate edges are counted separately so Kahn's algorithm
    // treats shared children and repeated writes as acyclic.
    llvm::DenseMap<mlir::Operation *, std::size_t> incoming;
    llvm::DenseMap<mlir::Operation *, llvm::SmallVector<mlir::Operation *, 2>> successors;
    for (mlir::Operation * array : contents.arrays) {
        if (!spend()) { return; }
        incoming.try_emplace(array, 0);
    }
    for (mlir::Operation * object : contents.objects) {
        if (!spend()) { return; }
        incoming.try_emplace(object, 0);
    }
    const auto addEdge = [&](mlir::Operation * container, mlir::Value value) {
        if (!spend()) { return false; }
        mlir::Operation * child = value.getDefiningOp();
        auto found = incoming.find(child);
        if (found == incoming.end()) { return true; } // a primitive constant
        successors[container].push_back(child);
        ++found->second;
        return true;
    };
    for (const ArrayElementWrite & write : contents.writes) {
        if (!addEdge(write.array, write.value)) { return; }
    }
    for (const ObjectPropertyWrite & write : contents.propertyWrites) {
        if (!addEdge(write.object, write.value)) { return; }
    }
    for (const ObjectPropertyCopy & copy : contents.propertyCopies) {
        if (!addEdge(copy.target, copy.value)) { return; }
    }
    llvm::SmallVector<mlir::Operation *, 8> pending;
    for (const auto & [container, count] : incoming) {
        if (!spend()) { return; }
        if (count == 0) { pending.push_back(container); }
    }
    std::size_t visited = 0;
    while (!pending.empty()) {
        if (!spend()) { return; }
        mlir::Operation * container = pending.pop_back_val();
        ++visited;
        auto found = successors.find(container);
        if (found == successors.end()) { continue; }
        for (mlir::Operation * child : found->second) {
            if (!spend()) { return; }
            if (--incoming[child] == 0) { pending.push_back(child); }
        }
    }
    if (visited != incoming.size()) { return; }

    llvm::SmallPtrSet<mlir::Operation *, 8> retained;
    for (const ArrayContentsExit & exit : contents.exits) {
        for (mlir::Operation * site : exit.reachableSites) {
            if (!spend()) { return; }
            retained.insert(site);
        }
    }
    llvm::SmallVector<mlir::Operation *, 8> confined;
    for (const auto & [site, verdict] : verdicts.sites) {
        if (!spend()) { return; }
        if (candidate(site, verdict) && incoming.contains(site) && retained.count(site) == 0) {
            confined.push_back(site);
        }
    }
    // Commit all-or-nothing: no verdict changes until every check above has
    // passed. Sites reachable from a return keep their original verdicts,
    // because returning a container does not give each child a unique owner.
    // An array receiver is confined only after the contents query proved every
    // access is to its own elements and excluded all other exposure. A nested
    // site is confined to the frame, not its region: yielded aliases may still
    // be observed after the region ends.
    for (mlir::Operation * site : confined) {
        if (retentionReason(verdicts.sites[site]) == EscapeReason::Stored) {
            ++verdicts.confinedStoredSites;
        }
        verdicts.sites[site] = Verdict{};
    }
    verdicts.arrayRetentionComplete = true;
}

} // namespace

AliasValue directStorageTarget(const mlir::DataFlowSolver & solver, const Verdict & verdict) {
    const mlir::Value target = storageTargetValue(verdict);
    if (!target) { return {}; }
    return aliasOf(solver, target);
}

EscapeVerdicts computeVerdicts(mlir::DataFlowSolver & solver, ctjs::FuncOp function,
                               std::size_t arrayRetentionWorkLimit) {
    EscapeVerdicts out;

    // Pass 1: inventory every site under live top-level blocks. Nested regions
    // have no instance/control-flow proof here, so even their dead arms stay
    // in the inventory. A dead top-level block drops all its descendant sites;
    // a top-level site in a live block with no lattice is a counted gap.
    llvm::SmallVector<mlir::Operation *, 2> argumentsBuilders;
    mlir::Operation * suspension = nullptr;
    llvm::SmallVector<mlir::Block *, 8> live;
    for (mlir::Block & block : function.getBody()) {
        ++out.blocks;
        if (!isLive(solver, &block)) {
            for (mlir::Operation & op : block) {
                op.walk([&](mlir::Operation * nested) {
                    if (isTrackedSite(nested)) { ++out.deadSites; }
                });
            }
            continue;
        }
        ++out.liveBlocks;
        live.push_back(&block);
        for (mlir::Operation & op : block) {
            // A region may hide writes that the top-level operand scan cannot
            // model. Keep the records from this CFG, but mark them incomplete.
            // The implicit-capture sinks below are a separate check.
            if (op.getNumRegions() != 0) { out.directStorage.complete = false; }
            op.walk<mlir::WalkOrder::PreOrder>([&](mlir::Operation * nested) {
                if (isTrackedSite(nested)) {
                    Verdict verdict;
                    if (nested == &op) {
                        const AliasLattice * lattice =
                            solver.lookupState<AliasLattice>(nested->getResult(0));
                        if (lattice == nullptr || lattice->getValue().isUninitialized()) {
                            verdict = Verdict{EscapeReason::Unvisited, nested, 0};
                            ++out.unvisitedSites;
                        }
                    }
                    out.sites.insert({nested, verdict});
                }
                // These operations retain raw frame registers, including values
                // with no explicit SSA use. Nesting cannot hide their refusal.
                if (llvm::isa<ctjs::MakeArgumentsOp, ctjs::GatherRestOp>(nested)) {
                    argumentsBuilders.push_back(nested);
                    out.capturesAllArguments = true;
                }
                if (suspension == nullptr && llvm::isa<ctjs::SuspendOp>(nested)) {
                    suspension = nested;
                }
            });
        }
    }

    // The first reason recorded for a site wins, everywhere below. Whole-function
    // refusals are applied before any sink so they are the first reason for
    // every site they refuse.
    const auto mark = [&](mlir::Operation * site, EscapeReason reason, mlir::Operation * by,
                          unsigned position) {
        auto found = out.sites.find(site);
        if (found == out.sites.end() || found->second.reason != EscapeReason::Confined) { return; }
        found->second = Verdict{reason, by, position};
    };
    const auto refuseWholeFunction = [&](EscapeReason reason, mlir::Operation * by) {
        if (!out.wholeFunction) { out.wholeFunction = reason; }
        out.directStorage.complete = false;
        for (auto & entry : out.sites) { mark(entry.first, reason, by, 0); }
    };

    // A suspension point (generator/async `suspend`) copies the whole register
    // window into a heap coroutine object (context::suspend_frame in
    // vm/call/coroutines.cpp), so every site in the function outlives its
    // frame. The importer already refuses such functions; this check covers
    // hand-written IR.
    if (suspension != nullptr) { refuseWholeFunction(EscapeReason::Suspended, suspension); }

    // The per-site `arguments` refusal is sound only if the arguments array is
    // built in the prologue, from the parameter registers. make_arguments is
    // emitted before any body statement (compile_function_body in
    // lib/Script/compile/statements/functions.cpp) and gather_rest only in the
    // parameter prologue. Check that rather than trust it: every builder must
    // be in the entry block before every site there (sites in other blocks
    // follow the entry block by dominance). Otherwise refuse the function.
    if (!argumentsBuilders.empty()) {
        mlir::Block & entry = function.getBody().front();
        mlir::Operation * late = nullptr;
        for (mlir::Operation * builder : argumentsBuilders) {
            if (builder->getBlock() != &entry) {
                late = builder;
                break;
            }
            for (auto & siteEntry : out.sites) {
                mlir::Operation * site =
                    function.getBody().findAncestorOpInRegion(*siteEntry.first);
                if (site->getBlock() == &entry && !builder->isBeforeInBlock(site)) {
                    late = builder;
                    break;
                }
            }
            if (late != nullptr) { break; }
        }
        if (late != nullptr) { refuseWholeFunction(EscapeReason::ArgumentsLate, late); }
    }

    // ponytail: every site inside a nested region is conservatively UnknownOp,
    // since nothing here proves whether or how often it runs; upgrade with a
    // region instance/control-flow proof. Applied after the whole-function
    // refusals so those stay the first reason.
    for (const auto & [site, verdict] : out.sites) {
        if (site->getParentRegion() != &function.getBody()) {
            mark(site, EscapeReason::UnknownOp, nullptr, 0);
        }
    }

    // Pass 2: apply the sinks, over every operand of every operation in every
    // live block, through operandRole (the interface, the kind switch, the
    // branch carry, or the default rule). Nothing feeds back into the alias
    // lattice (a store's target does not change what the stored value
    // aliases), so one pass is the fixpoint.
    const auto sink = [&](mlir::Value value, EscapeReason reason, mlir::Operation * by,
                          unsigned position) {
        const AliasValue aliases = aliasOf(solver, value);
        if (reason == EscapeReason::Stored) {
            const AliasValue target = directStorageTarget(solver, Verdict{reason, by, position});
            out.directStorage.writes.push_back({by, position, aliases, target});
            if (aliases.isUninitialized() || target.isUninitialized()) {
                out.directStorage.complete = false;
            }
        }
        if (aliases.isUninitialized()) {
            // A gap: the solver never said what this operand may denote, so it
            // may denote anything, which is every site of the function.
            ++out.unvisitedOperands;
            for (auto & entry : out.sites) {
                mark(entry.first, EscapeReason::UnvisitedOperand, by, position);
            }
            return;
        }
        for (mlir::Operation * site : aliases.getSites()) { mark(site, reason, by, position); }
    };
    for (mlir::Block * block : live) {
        for (mlir::Operation & op : *block) {
            for (unsigned i = 0, n = op.getNumOperands(); i < n; ++i) {
                const RoleOf role = operandRole(&op, i);
                if (role.role == OperandRole::Sink) { sink(op.getOperand(i), role.reason, &op, i); }
            }
            // This query models the function's CFG, not nested region control
            // flow. A region can still refer to an outer SSA value without
            // listing it as an operand of its parent operation: scf.if with
            // a nested store_global is one example. The default rule must
            // cover those implicit captures too. Inspect every nested use,
            // but only sink values defined outside this region owner; local
            // region values have no required lattice in this CFG query.
            // Even a nested branch proved dead is refused here: modelling
            // region semantics would need its own proof.
            op.walk([&](mlir::Operation * nested) {
                if (nested == &op) { return; }
                for (unsigned i = 0, n = nested->getNumOperands(); i < n; ++i) {
                    mlir::Value value = nested->getOperand(i);
                    if (!isValueTyped(value)) { continue; }
                    mlir::Operation * owner = value.getParentRegion()->getParentOp();
                    if (owner == &op || op.isProperAncestor(owner)) { continue; }
                    sink(value, EscapeReason::UnknownOp, nested, i);
                }
            });
        }
    }

    // Evidence only: a shared allocation site is a candidate connection, not
    // a must-alias object or the value of a field at this read. Index writes
    // once, then retain all matches, including writes after a read and writes
    // to other keys. No fact feeds back into a lattice or a verdict.
    llvm::DenseMap<mlir::Operation *, llvm::SmallVector<std::size_t, 2>> writesByTarget;
    for (const auto & [index, write] : llvm::enumerate(out.directStorage.writes)) {
        for (mlir::Operation * site : write.target.getSites()) {
            writesByTarget[site].push_back(index);
        }
    }
    out.directLoads.complete = out.directStorage.complete;
    for (mlir::Block * block : live) {
        for (mlir::Operation & op : *block) {
            auto load = llvm::dyn_cast<ctjs::GetPropertyOp>(&op);
            if (!load) { continue; }
            DirectPropertyRead read;
            read.by = &op;
            read.base = aliasOf(solver, load.getObject());
            if (read.base.isUninitialized()) { out.directLoads.complete = false; }
            for (mlir::Operation * site : read.base.getSites()) {
                const auto found = writesByTarget.find(site);
                if (found != writesByTarget.end()) {
                    llvm::append_range(read.candidateWrites, found->second);
                }
            }
            llvm::sort(read.candidateWrites);
            read.candidateWrites.erase(llvm::unique(read.candidateWrites),
                                       read.candidateWrites.end());
            out.directLoads.reads.push_back(std::move(read));
        }
    }
    refineArrayRetention(out, solver, function, arrayRetentionWorkLimit);
    return out;
}

LoadProvenanceEvidence computeLoadProvenance(mlir::DataFlowSolver & solver, ctjs::FuncOp function,
                                             const EscapeVerdicts & verdicts,
                                             std::size_t workLimit) {
    LoadProvenanceEvidence out;
    out.inputsComplete = verdicts.directLoads.complete;
    llvm::DenseMap<mlir::Value, AliasValue> values;
    llvm::DenseMap<mlir::Operation *, AliasValue> contents;
    const auto aliases = [&](mlir::Value value) {
        if (!value) {
            out.inputsComplete = false;
            return AliasValue{};
        }
        auto found = values.find(value);
        if (found != values.end()) { return found->second; }
        const AliasValue initial = aliasOf(solver, value);
        if (initial.isUninitialized()) { out.inputsComplete = false; }
        values.insert({value, initial});
        return initial;
    };
    const auto mergeValue = [&](mlir::Value value, const AliasValue & incoming) {
        const AliasValue before = aliases(value);
        const AliasValue joined = AliasValue::join(before, incoming);
        if (before == joined) { return false; }
        values[value] = joined;
        return true;
    };
    // Branch copies are edge-specific, including duplicate successor blocks.
    // If both blocks are live we retain the edge conservatively, even when
    // another edge made the successor live. This can add candidates, not erase
    // them. Produced arguments and opaque successor semantics stay incomplete.
    llvm::SmallVector<std::pair<mlir::Value, mlir::Value>, 0> copies;
    llvm::SmallVector<EscapeExposure, 0> sinks;
    for (mlir::Block & block : function.getBody()) {
        if (!isLive(solver, &block)) { continue; }
        for (mlir::Operation & op : block) {
            if (auto branch = llvm::dyn_cast<mlir::BranchOpInterface>(&op)) {
                for (unsigned index = 0; index < op.getNumSuccessors(); ++index) {
                    mlir::Block * target = op.getSuccessor(index);
                    if (!isLive(solver, target)) { continue; }
                    mlir::SuccessorOperands passed = branch.getSuccessorOperands(index);
                    for (mlir::BlockArgument argument : target->getArguments()) {
                        if (!isValueTyped(argument)) { continue; }
                        const unsigned position = argument.getArgNumber();
                        if (position >= passed.size() || passed.isOperandProduced(position)) {
                            out.inputsComplete = false;
                            continue;
                        }
                        copies.emplace_back(passed[position], argument);
                    }
                }
            } else if (op.getNumSuccessors() != 0) {
                out.inputsComplete = false;
            }
            for (unsigned index = 0; index < op.getNumOperands(); ++index) {
                const RoleOf role = operandRole(&op, index);
                if (role.role == OperandRole::Sink) {
                    sinks.push_back({&op, index, role.reason, {}});
                }
            }
            if (auto roles = llvm::dyn_cast<ctjs::EscapeEffectOpInterface>(&op)) {
                for (mlir::OpOperand * on : operandsWith<ctjs::EscapeEffects::Carry>(roles)) {
                    for (mlir::Value result : op.getResults()) {
                        if (isValueTyped(result)) { copies.emplace_back(on->get(), result); }
                    }
                }
            }
        }
    }

    // This private candidate map starts with the original aliases and only
    // grows. In particular get_property keeps its external alternative. Every
    // store contributes to every known target site, irrespective of key/order;
    // every load imports that site's recorded candidates. Repeated instances,
    // overwrites and cycles are unions, never strong updates or copied objects.
    const auto spend = [&]() { return spendUsed(out.work, workLimit); };
    out.converged = [&] {
        bool changed = true;
        while (changed) {
            changed = false;
            for (const auto & [source, target] : copies) {
                if (!spend()) { return false; }
                changed |= mergeValue(target, aliases(source));
            }
            for (const DirectStorageWrite & write : verdicts.directStorage.writes) {
                if (!spend()) { return false; }
                const AliasValue value = aliases(write.by->getOperand(write.position));
                const AliasValue target =
                    aliases(storageTargetValue({EscapeReason::Stored, write.by, write.position}));
                for (mlir::Operation * site : target.getSites()) {
                    if (!spend()) { return false; }
                    AliasValue & before = contents[site];
                    const AliasValue joined = AliasValue::join(before, value);
                    changed |= !(before == joined);
                    before = joined;
                }
            }
            for (const DirectPropertyRead & read : verdicts.directLoads.reads) {
                if (!spend()) { return false; }
                auto load = llvm::cast<ctjs::GetPropertyOp>(read.by);
                const AliasValue base = aliases(load.getObject());
                for (mlir::Operation * site : base.getSites()) {
                    if (!spend()) { return false; }
                    const auto found = contents.find(site);
                    if (found != contents.end()) {
                        changed |= mergeValue(load.getResult(), found->second);
                    }
                }
            }
        }
        return true;
    }();

    // Keep all records even on budget exhaustion. They describe the partial
    // candidate graph; the original verdicts remain the only escape claims.
    for (const DirectStorageWrite & write : verdicts.directStorage.writes) {
        out.writes.push_back(
            {write.by, write.position, aliases(write.by->getOperand(write.position)),
             aliases(storageTargetValue({EscapeReason::Stored, write.by, write.position}))});
    }
    for (const DirectPropertyRead & read : verdicts.directLoads.reads) {
        auto load = llvm::cast<ctjs::GetPropertyOp>(read.by);
        out.reads.push_back({read.by, aliases(load.getObject()), aliases(load.getResult())});
    }
    for (EscapeExposure & exposure : sinks) {
        exposure.value = aliases(exposure.by->getOperand(exposure.position));
    }
    out.exposures = std::move(sinks);
    return out;
}

} // namespace ctcompile::ctnative
