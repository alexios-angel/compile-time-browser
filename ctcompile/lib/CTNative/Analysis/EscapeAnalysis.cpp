// THE ALIAS TRANSFER, THE POST-PASS, THE KIND SWITCH, AND THE TWO REFUSALS.
//
// Everything structural is upstream's - the worklist, the fixpoint, the joins
// at block arguments through BranchOpInterface successor operands - and
// everything tabular is TableGen's: which operand sinks and why is an
// Arg<..., [CTJS_Sink...]> in CTJSOps.td, read here through
// ctjs::EscapeEffectOpInterface without naming a single operation. What this
// file owns is (a) the two tracked sites, (b) the default rule that makes an
// unannotated operation sound, (c) the ~15-line switch for the three
// operations whose role depends on a kind attribute, (d) the post-fixpoint
// verdict walk, and (e) R1's placement guard and R4's whole-function refusal.
//
// WHY THE SINKS ARE NOT IN visitOperation. The sparse framework's
// AbstractSparseForwardDataFlowAnalysis::visitOperation(Operation *) begins
// with "Exit early on operations with no results" - and the operations that
// make an object escape are precisely the ones with no results: return,
// throw, store_global, set_property, append, cell_set, store_upvalue,
// set_proto, define_accessor, copy_props, delete_*. A sink fired there would
// never fire for `return {}` and every site would read confined. That is the
// finding the judge panel made against the first design, and the unit test's
// return/throw rows are its regression test.
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
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/raw_ostream.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <utility>

namespace ctcompile::ctnative {

// --- the lattice element ------------------------------------------------------

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
    // THE ONE PLACE A SITE ENTERS AN ALIAS SET, and it refuses anything but
    // the two kinds the NEITHER proofs are valid for.
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

bool AliasValue::operator==(const AliasValue & other) const {
    return initialized_ == other.initialized_ && external_ == other.external_ &&
           sites_ == other.sites_;
}

void AliasValue::print(llvm::raw_ostream & os) const {
    if (!initialized_) {
        os << "<uninitialized>";
        return;
    }
    os << '{';
    bool first = true;
    for (mlir::Operation * site : sites_) {
        os << (first ? "" : ", ") << site->getName().getStringRef();
        first = false;
    }
    if (external_) { os << (first ? "" : ", ") << "external"; }
    os << '}';
}

// --- the table, read generically ----------------------------------------------

namespace {

bool isValueTyped(mlir::Value v) {
    return llvm::isa<ctjs::ValueType>(v.getType());
}

// A route's name IS the spelling of a CTNative_EscapeReason case, and this is
// the only bridge between the two: the ctjs dialect knows nothing of ctnative.
// A route the enum cannot name is a .td/.h mismatch; the unit test asserts
// every reason by name, so it cannot hide behind the fallback.
EscapeReason reasonOf(mlir::SideEffects::Resource * route) {
    const std::optional<EscapeReason> reason = symbolizeEscapeReason(route->getName());
    assert(reason.has_value() && "an escape route's name is not an EscapeReason case");
    return reason.value_or(EscapeReason::UnknownOp);
}

using EscapeInstance = mlir::SideEffects::EffectInstance<ctjs::EscapeEffects::Effect>;

// THE KIND SWITCH - the only operation-name-specific C++ the design allows,
// and only because a kind ATTRIBUTE decides the role, which an Arg decorator
// cannot express. Each case refines the default (sink) in the cases §2.2
// lists, and nowhere else; every "converted" here is a may_reenter 1 bytecode
// row that runs the operand's own valueOf/toString.
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
        // strict_equals is a bit/content compare (value.hpp:293-311); loose
        // equality and the four relations convert (def:394-396, 411).
        return compare.getKind() == ctjs::CompareKind::StrictEq ? neither : converted;
    }
    if (auto convert = llvm::dyn_cast<ctjs::ConvertOp>(op)) {
        // ToBoolean is total. ToObject is the identity on an object - a CARRY
        // in principle, a sink in the MVP with no consumer.
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
        llvm::SmallVector<EscapeInstance, 6> effects;
        roles.getEffects(effects);
        RoleOf out = neither;
        for (const EscapeInstance & effect : effects) {
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
    // the framework joins into the successor's block arguments: CARRY. Its
    // non-successor operands (a condition, a switch flag) are not values and
    // were answered above.
    if (llvm::isa<mlir::BranchOpInterface>(op)) {
        return RoleOf{OperandRole::Carry, EscapeReason::Confined};
    }

    // THE DEFAULT RULE. No annotation means every value operand sinks. This
    // is what makes omission sound: a new ctjs operation cannot weaken the
    // analysis by not being in a table, because there is no table to be
    // missing from - only a decorator to add once its VM behaviour is read.
    return RoleOf{OperandRole::Sink, EscapeReason::UnknownOp};
}

bool isBoxedSite(mlir::Operation * op, EscapeReason & reason) {
    auto roles = llvm::dyn_cast<ctjs::EscapeEffectOpInterface>(op);
    if (!roles) { return false; }
    llvm::SmallVector<EscapeInstance, 6> effects;
    roles.getEffects(effects);
    for (const EscapeInstance & effect : effects) {
        if (!llvm::isa<ctjs::EscapeEffects::BoxedSite>(effect.getEffect())) { continue; }
        reason = reasonOf(effect.getResource());
        return true;
    }
    return false;
}

std::optional<unsigned> allocationPc(mlir::Operation * op) {
    // The importer's location_for: a NameLoc "program:<id>:<fn>:<at>" fused
    // with a FileLineColLoc, or the NameLoc alone (BytecodeImport.cpp).
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

// --- the alias half -----------------------------------------------------------

void EscapeAnalysis::setToEntryState(AliasLattice * lattice) {
    propagateIfChanged(lattice, lattice->join(AliasValue::external()));
}

mlir::LogicalResult EscapeAnalysis::visitOperation(mlir::Operation * op,
                                                   llvm::ArrayRef<const AliasLattice *> operands,
                                                   llvm::ArrayRef<AliasLattice *> results) {
    // ONLY EVER CALLED FOR AN OPERATION WITH RESULTS, which is why no sink
    // lives here. Every result of one operation gets the same answer: no ctjs
    // operation has two value results (catch_land's first is an i32).
    AliasValue answer;
    if (isTrackedSite(op)) {
        answer = AliasValue::site(op);
    } else if (llvm::isa<ctjs::ConstantOp>(op)) {
        answer = AliasValue::none(); // a primitive; strings are untracked
    } else if (auto roles = llvm::dyn_cast<ctjs::EscapeEffectOpInterface>(op)) {
        // CARRY: the result may be any carried operand's object - PLUS an
        // external one, because the one carrier (ctjs.iterable) substitutes a
        // fresh array on every non-array arm. An interface operation with no
        // carry is external, like everything else.
        answer = AliasValue::external();
        llvm::SmallVector<EscapeInstance, 6> effects;
        roles.getEffects(effects);
        for (const EscapeInstance & effect : effects) {
            if (!llvm::isa<ctjs::EscapeEffects::Carry>(effect.getEffect())) { continue; }
            mlir::OpOperand * on = effect.getEffectValue<mlir::OpOperand *>();
            if (on == nullptr) { continue; }
            answer = AliasValue::join(answer, operands[on->getOperandNumber()]->getValue());
        }
    } else {
        answer = AliasValue::external();
    }
    // THE CLOSURE HOLE, CLOSED: nothing above can put a create_closure (or any
    // other allocating operation) into a set, and this says so every visit.
    assert(answer.onlyTrackedSites() && "an untracked operation entered an alias set");

    for (AliasLattice * result : results) {
        const AliasValue forThis = isValueTyped(result->getAnchor()) ? answer : AliasValue::none();
        propagateIfChanged(result, result->join(forThis));
    }
    return mlir::success();
}

// --- the post-pass ------------------------------------------------------------

namespace {

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

void refineArrayRetention(EscapeVerdicts & verdicts, ctjs::FuncOp function, std::size_t workLimit) {
    const auto candidate = [](mlir::Operation * site, const Verdict & verdict) {
        return verdict.reason == EscapeReason::Stored ||
               (llvm::isa<ctjs::CreateArrayOp>(site) && verdict.reason == EscapeReason::Passed &&
                verdict.position == 0 &&
                llvm::isa_and_nonnull<ctjs::GetPropertyOp, ctjs::SetPropertyOp>(verdict.by));
    };
    if (workLimit == 0 || verdicts.unvisitedSites != 0 || verdicts.unvisitedOperands != 0 ||
        verdicts.wholeFunction || !verdicts.directStorage.complete ||
        !verdicts.directLoads.complete || !llvm::any_of(verdicts.sites, [&](const auto & entry) {
            return candidate(entry.first, entry.second);
        })) {
        return;
    }

    // Candidate alias closure cannot discharge a sink. The independent query
    // must prove every operation in the CURRENT function, including effects
    // after the last read and the exact origin of a saved, later-returned read.
    const ArrayContentsEvidence contents = computeArrayContents(function, workLimit);
    verdicts.arrayRetentionWork = contents.work;
    if (!contents.complete) { return; }
    const auto spend = [&]() {
        if (verdicts.arrayRetentionWork == workLimit) { return false; }
        ++verdicts.arrayRetentionWork;
        return true;
    };

    // This increment discharges no cycle ownership obligation. Reject cycles
    // in the union of ALL writes, not just final contents: even a cycle that
    // was overwritten or deleted before return stays outside this refinement. Count
    // duplicate edges independently so Kahn's traversal handles shared children
    // and repeated writes without treating either as a cycle.
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
    // Transactional commit: not even the first candidate changes before the
    // final graph/verdict visit. Reachable children keep their Stored witnesses,
    // because returning a container does not give each child a unique owner.
    // Array receivers lose Passed only after the complete current query proved
    // every access own and excluded all other exposure, including later effects.
    for (mlir::Operation * site : confined) {
        if (verdicts.sites[site].reason == EscapeReason::Stored) { ++verdicts.confinedStoredSites; }
        verdicts.sites[site] = Verdict{};
    }
    verdicts.arrayRetentionComplete = true;
}

} // namespace

AliasValue directStorageTarget(const mlir::DataFlowSolver & solver, const Verdict & verdict) {
    const mlir::Value target = storageTargetValue(verdict);
    if (!target) { return {}; }
    const AliasLattice * lattice = solver.lookupState<AliasLattice>(target);
    return lattice != nullptr ? lattice->getValue() : AliasValue{};
}

EscapeVerdicts computeVerdicts(mlir::DataFlowSolver & solver, ctjs::FuncOp function,
                               std::size_t arrayRetentionWorkLimit) {
    EscapeVerdicts out;

    const auto blockIsLive = [&](mlir::Block & block) {
        const auto * executable =
            solver.lookupState<mlir::dataflow::Executable>(solver.getProgramPointBefore(&block));
        return executable != nullptr && executable->isLive();
    };

    // PASS 1: the sites, the arguments builders, the suspension points -
    // live blocks only. ctcompile/test/Analysis/Types/Claims.cpp's split applied
    // to sites: a site in a
    // block DeadCodeAnalysis proved dead never executes and is dropped; a
    // site in a live block with no lattice is a gap and is counted.
    llvm::SmallVector<mlir::Operation *, 2> argumentsBuilders;
    mlir::Operation * suspension = nullptr;
    llvm::SmallVector<mlir::Block *, 8> live;
    for (mlir::Block & block : function.getBody()) {
        ++out.blocks;
        if (!blockIsLive(block)) {
            for (mlir::Operation & op : block) {
                if (isTrackedSite(&op)) { ++out.deadSites; }
            }
            continue;
        }
        ++out.liveBlocks;
        live.push_back(&block);
        for (mlir::Operation & op : block) {
            // A region may hide writes that the top-level operand census
            // cannot model. Keep the records from this CFG, but never present
            // them as complete. The capture refusal below is still separate.
            if (op.getNumRegions() != 0) { out.directStorage.complete = false; }
            if (isTrackedSite(&op)) {
                Verdict verdict;
                const AliasLattice * lattice = solver.lookupState<AliasLattice>(op.getResult(0));
                if (lattice == nullptr || lattice->getValue().isUninitialized()) {
                    verdict = Verdict{EscapeReason::Unvisited, &op, 0};
                    ++out.unvisitedSites;
                }
                out.sites.insert({&op, verdict});
            }
            // These operations retain raw frame registers, including values
            // with no explicit SSA use. Nesting cannot hide their refusal,
            // even though region-local allocation verdicts are out of scope.
            op.walk([&](mlir::Operation * nested) {
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

    // FIRST REASON WINS, everywhere below. A whole-function refusal is applied
    // before any sink so it IS the first reason for every site it refuses.
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

    // R4: a suspension point copies the entire register window into a
    // coroutine object on the heap (run_loop.cpp:866-867, 922-923), so every
    // site of the function outlives its frame. The importer refuses these
    // functions already; this line is for hand-written IR.
    if (suspension != nullptr) { refuseWholeFunction(EscapeReason::Suspended, suspension); }

    // R1's GUARD: the per-site `arguments` refusal is sound only because the
    // arguments array holds what the parameter registers held in the
    // PROLOGUE - make_arguments is emitted after the parameters are declared
    // and before any body statement (compile_function_body in
    // compile/statements/functions.cpp), gather_rest has
    // exactly one emitter, the parameter prologue. That placement is CHECKED
    // here, not believed: every builder must be in the entry block and
    // precede every site there (a site in any other block is after the entry
    // block by dominance). Otherwise the whole function is refused.
    if (!argumentsBuilders.empty()) {
        mlir::Block & entry = function.getBody().front();
        mlir::Operation * late = nullptr;
        for (mlir::Operation * builder : argumentsBuilders) {
            if (builder->getBlock() != &entry) {
                late = builder;
                break;
            }
            for (auto & siteEntry : out.sites) {
                mlir::Operation * site = siteEntry.first;
                if (site->getBlock() == &entry && !builder->isBeforeInBlock(site)) {
                    late = builder;
                    break;
                }
            }
            if (late != nullptr) { break; }
        }
        if (late != nullptr) { refuseWholeFunction(EscapeReason::ArgumentsLate, late); }
    }

    // PASS 2: THE SINKS, over every operand of every operation in every live
    // block, through operandRole - the interface, the kind switch, the branch
    // carry, or the default rule. Nothing feeds back into the alias lattice
    // (a store's target does not change what the stored value aliases), so
    // one pass is the fixpoint.
    const auto sink = [&](mlir::Value value, EscapeReason reason, mlir::Operation * by,
                          unsigned position) {
        const AliasLattice * lattice = solver.lookupState<AliasLattice>(value);
        if (reason == EscapeReason::Stored) {
            const AliasValue aliases = lattice != nullptr ? lattice->getValue() : AliasValue{};
            const AliasValue target = directStorageTarget(solver, Verdict{reason, by, position});
            out.directStorage.writes.push_back({by, position, aliases, target});
            if (aliases.isUninitialized() || target.isUninitialized()) {
                out.directStorage.complete = false;
            }
        }
        if (lattice == nullptr || lattice->getValue().isUninitialized()) {
            // A gap: the solver never told us what this operand may denote,
            // so it may denote anything - every site of the function.
            ++out.unvisitedOperands;
            for (auto & entry : out.sites) {
                mark(entry.first, EscapeReason::UnvisitedOperand, by, position);
            }
            return;
        }
        for (mlir::Operation * site : lattice->getValue().getSites()) {
            mark(site, reason, by, position);
        }
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
            // Even a nested branch proved dead is refused here: admitting
            // region semantics is a separate proof, not an operand-table row.
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
            const AliasLattice * lattice = solver.lookupState<AliasLattice>(load.getObject());
            if (lattice != nullptr) { read.base = lattice->getValue(); }
            if (read.base.isUninitialized()) { out.directLoads.complete = false; }
            for (mlir::Operation * site : read.base.getSites()) {
                const auto found = writesByTarget.find(site);
                if (found != writesByTarget.end()) {
                    llvm::append_range(read.candidateWrites, found->second);
                }
            }
            llvm::sort(read.candidateWrites);
            read.candidateWrites.erase(
                std::unique(read.candidateWrites.begin(), read.candidateWrites.end()),
                read.candidateWrites.end());
            out.directLoads.reads.push_back(std::move(read));
        }
    }
    refineArrayRetention(out, function, arrayRetentionWorkLimit);
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
        const AliasLattice * lattice = solver.lookupState<AliasLattice>(value);
        const AliasValue initial = lattice != nullptr ? lattice->getValue() : AliasValue{};
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
    const auto live = [&](mlir::Block * block) {
        const auto * executable =
            solver.lookupState<mlir::dataflow::Executable>(solver.getProgramPointBefore(block));
        return executable != nullptr && executable->isLive();
    };

    // Branch copies are edge-specific, including duplicate successor blocks.
    // If both blocks are live we retain the edge conservatively, even when
    // another edge made the successor live. This can add candidates, not erase
    // them. Produced arguments and opaque successor semantics stay incomplete.
    llvm::SmallVector<std::pair<mlir::Value, mlir::Value>, 0> copies;
    llvm::SmallVector<EscapeExposure, 0> sinks;
    for (mlir::Block & block : function.getBody()) {
        if (!live(&block)) { continue; }
        for (mlir::Operation & op : block) {
            if (auto branch = llvm::dyn_cast<mlir::BranchOpInterface>(&op)) {
                for (unsigned index = 0; index < op.getNumSuccessors(); ++index) {
                    mlir::Block * target = op.getSuccessor(index);
                    if (!live(target)) { continue; }
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
                llvm::SmallVector<EscapeInstance, 6> effects;
                roles.getEffects(effects);
                for (const EscapeInstance & effect : effects) {
                    if (!llvm::isa<ctjs::EscapeEffects::Carry>(effect.getEffect())) { continue; }
                    auto * on = effect.getEffectValue<mlir::OpOperand *>();
                    if (on == nullptr) { continue; }
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
    const auto spend = [&]() {
        if (out.work == workLimit) { return false; }
        ++out.work;
        return true;
    };
    bool exhausted = false;
    bool changed = true;
    while (changed && !exhausted) {
        changed = false;
        for (const auto & [source, target] : copies) {
            if (!spend()) {
                exhausted = true;
                break;
            }
            changed |= mergeValue(target, aliases(source));
        }
        if (exhausted) { break; }
        for (const DirectStorageWrite & write : verdicts.directStorage.writes) {
            if (!spend()) {
                exhausted = true;
                break;
            }
            const AliasValue value = aliases(write.by->getOperand(write.position));
            const AliasValue target =
                aliases(storageTargetValue({EscapeReason::Stored, write.by, write.position}));
            for (mlir::Operation * site : target.getSites()) {
                if (!spend()) {
                    exhausted = true;
                    break;
                }
                AliasValue & before = contents[site];
                const AliasValue joined = AliasValue::join(before, value);
                changed |= !(before == joined);
                before = joined;
            }
            if (exhausted) { break; }
        }
        if (exhausted) { break; }
        for (const DirectPropertyRead & read : verdicts.directLoads.reads) {
            if (!spend()) {
                exhausted = true;
                break;
            }
            auto load = llvm::cast<ctjs::GetPropertyOp>(read.by);
            const AliasValue base = aliases(load.getObject());
            for (mlir::Operation * site : base.getSites()) {
                if (!spend()) {
                    exhausted = true;
                    break;
                }
                const auto found = contents.find(site);
                if (found != contents.end()) {
                    changed |= mergeValue(load.getResult(), found->second);
                }
            }
            if (exhausted) { break; }
        }
    }
    out.converged = !exhausted;

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

namespace {

// An original Number in the exact array-length range; -0 has index value zero.
// Negation also recognizes the frontend's single Neg of an original literal.
std::optional<std::size_t> boundedNumber(mlir::Value value, bool negate = false) {
    if (!value) { return std::nullopt; }
    if (negate) {
        if (auto unary = value.getDefiningOp<ctjs::UnaryOp>();
            unary && unary.getKind() == ctjs::UnaryKind::Neg) {
            return boundedNumber(unary.getOperand());
        }
    }
    auto constant = value.getDefiningOp<ctjs::ConstantOp>();
    auto number =
        constant ? llvm::dyn_cast<ctjs::NumberAttr>(constant.getValue()) : ctjs::NumberAttr{};
    if (number) {
        const double integer = negate ? -number.getDouble() : number.getDouble();
        if (std::isfinite(integer) && integer >= 0 && integer <= 4294967295.0 &&
            std::floor(integer) == integer) {
            return static_cast<std::size_t>(integer);
        }
    }
    return std::nullopt;
}

// An own array element, not a property requiring conversion/prototype lookup.
// Number -0 and canonical String/BigInt "0" are index zero; 2^32-1 is not an element.
// Original literals, or one subtraction of two bounded original BigInt literals.
std::optional<std::size_t> ownArrayIndex(mlir::Value value) {
    if (auto binary = value.getDefiningOp<ctjs::BinaryOp>();
        binary && binary.getKind() == ctjs::BinaryKind::Sub) {
        auto lhs = binary.getLhs().getDefiningOp<ctjs::ConstantOp>();
        auto rhs = binary.getRhs().getDefiningOp<ctjs::ConstantOp>();
        if (!lhs || !rhs || !llvm::isa<ctjs::BigIntAttr>(lhs.getValue()) ||
            !llvm::isa<ctjs::BigIntAttr>(rhs.getValue())) {
            return std::nullopt;
        }
        // ponytail: two literal parses only; loaded operands/chains need charged provenance.
        const auto left = ownArrayIndex(lhs.getResult());
        const auto right = ownArrayIndex(rhs.getResult());
        if (left && right && *left >= *right) { return *left - *right; }
        return std::nullopt;
    }
    auto constant = value.getDefiningOp<ctjs::ConstantOp>();
    if (!constant) { return std::nullopt; }
    if (const auto number = boundedNumber(value); number && *number < 4294967295ULL) {
        return number;
    }
    llvm::StringRef key;
    unsigned radix = 10;
    if (auto string = llvm::dyn_cast<ctjs::StringAttr>(constant.getValue())) {
        key = string.getValue();
    } else if (auto bigint = llvm::dyn_cast<ctjs::BigIntAttr>(constant.getValue())) {
        // compile/expressions.cpp removes source 'n' before load_bigint. The VM
        // converts literal digits to an index without object hooks. Do not strip
        // an attribute suffix: the literal parser rejects it and substitutes 0n.
        key = bigint.getText();
        if (key.consume_front_insensitive("0x")) {
            radix = 16;
        } else if (key.consume_front_insensitive("0o")) {
            radix = 8;
        } else if (key.consume_front_insensitive("0b")) {
            radix = 2;
        }
    }
    std::uint32_t index = 0;
    // ponytail: at most 32 nondecimal digits; extend only with charged parsing.
    if (!key.empty() && key.size() <= (radix == 10 ? 10U : 32U) &&
        (radix != 10 || key.size() == 1 || key.front() != '0') && !key.getAsInteger(radix, index) &&
        index < 4294967295ULL) {
        return static_cast<std::size_t>(index);
    }
    return std::nullopt;
}

mlir::StringAttr ownObjectKey(mlir::StringAttr key) {
    if (!key || key.getValue().size() > 256 || key.getValue() == "__proto__") { return {}; }
    return key;
}

mlir::StringAttr ownObjectKey(mlir::Value value) {
    auto constant = value.getDefiningOp<ctjs::ConstantOp>();
    if (!constant) { return {}; }
    auto string = llvm::dyn_cast<ctjs::StringAttr>(constant.getValue());
    if (!string || string.getValue().size() > 256 || string.getValue() == "__proto__") {
        return {};
    }
    return mlir::StringAttr::get(constant.getContext(), string.getValue());
}

enum class ContentsKind {
    Identity,
    Opaque,
    NonBigInt,
    BigInt,
    String
};

// Facts belong to the held value, not mutable metadata on its SSA producer.
// Copies into successors and containers keep the read-time scalar snapshot.
// The original producer remains the identity used by public evidence records.
struct ContentsValue {
    mlir::Value original;
    ContentsKind kind = ContentsKind::Identity;
    // Exact bounded Numbers survive simultaneous successor transport and replay.
    std::optional<std::size_t> integerNumber = std::nullopt;
    // A negative Number's magnitude is never an own index or nonnegative start.
    std::optional<std::size_t> negativeIntegerNumber = std::nullopt;

    mlir::Value origin() const { return kind == ContentsKind::Opaque ? mlir::Value{} : original; }
    bool nonBigInt() const {
        return kind == ContentsKind::NonBigInt || kind == ContentsKind::String;
    }
    bool bigInt() const { return kind == ContentsKind::BigInt; }
    bool string() const { return kind == ContentsKind::String; }
};

// The nonnegative part of an exact primitive Number conversion. Facts attach
// only to the operation result; Boolean/null origins keep their property keys.
std::optional<std::size_t> boundedConvertedNumber(const ContentsValue & input) {
    if (input.integerNumber) { return input.integerNumber; }
    const auto origin = input.origin();
    if (!origin) { return std::nullopt; }
    if (auto number = boundedNumber(origin)) { return number; }
    if (input.string()) { return ownArrayIndex(origin); }
    if (auto literal = origin.getDefiningOp<ctjs::ConstantOp>()) {
        if (auto boolean = llvm::dyn_cast<ctjs::BooleanAttr>(literal.getValue())) {
            return boolean.getValue() ? 1 : 0;
        }
        if (llvm::isa<ctjs::NullAttr>(literal.getValue())) { return 0; }
    }
    return std::nullopt;
}

void boundedNumberSum(const ContentsValue & left, const ContentsValue & right,
                      ContentsValue & result) {
    const auto a = left.integerNumber ? left.integerNumber : boundedNumber(left.origin());
    const auto b = right.integerNumber ? right.integerNumber : boundedNumber(right.origin());
    // Both original operands must be exact Numbers. Guard before adding so
    // neither dynamic nor static Add can borrow coercion, rounding or wrap.
    const auto negativeA = left.negativeIntegerNumber ? left.negativeIntegerNumber
                                                      : boundedNumber(left.origin(), true);
    const auto negativeB = right.negativeIntegerNumber ? right.negativeIntegerNumber
                                                       : boundedNumber(right.origin(), true);
    // Cancellation stays exact and bounded. Keep negative magnitudes separate
    // from indices; the original result still carries the sign of zero.
    if (a && b && *a <= 4294967295ULL - *b) {
        result.integerNumber = *a + *b;
    } else if (a && negativeB) {
        if (*a >= *negativeB) {
            result.integerNumber = *a - *negativeB;
        } else {
            result.negativeIntegerNumber = *negativeB - *a;
        }
    } else if (negativeA && b) {
        if (*b >= *negativeA) {
            result.integerNumber = *b - *negativeA;
        } else {
            result.negativeIntegerNumber = *negativeA - *b;
        }
    } else if (negativeA && negativeB && *negativeA <= 4294967295ULL - *negativeB) {
        result.negativeIntegerNumber = *negativeA + *negativeB;
    }
}

} // namespace

ArrayContentsEvidence computeArrayContents(ctjs::FuncOp function, std::size_t workLimit) {
    ArrayContentsEvidence out;
    const auto refuse = [&](ArrayContentsFailure reason, mlir::Operation * by) {
        ArrayContentsEvidence failed;
        failed.failure = reason;
        failed.refusedBy = by;
        failed.work = out.work;
        return failed;
    };
    const auto spend = [&](std::size_t count = 1) {
        if (count > workLimit - out.work) {
            out.work = workLimit;
            return false;
        }
        out.work += count;
        return true;
    };
    if (function.getBody().empty() || function.getBody().front().empty()) {
        return refuse(ArrayContentsFailure::UnsupportedControlFlow, function);
    }

    // Enumerate structural paths, never solver flags or annotations. A join
    // keeps separate exact states: unioning array aliases before a strong
    // overwrite would incorrectly erase an element of an unmodified array.
    // Every ordinary branch edge is visited, including a constant flag's untaken
    // edge and a switch's default edge. Only a certified finite loop selects its
    // guard edge. Any unsupported path refuses everything.
    using HeldProperties = llvm::MapVector<mlir::StringAttr, ContentsValue>;
    struct CountedLoop {
        mlir::Block * header;
        mlir::Block * body;
        mlir::Value index;
        mlir::Value array;
        mlir::Operation * site;
        std::size_t length;
        std::size_t finalIndex;
    };
    struct State {
        // Imported successors forward every raw register, including unused
        // receiver/parameter values. Their Opaque kind keeps entry identity:
        // forwarding or testing one never proves its contents or retention.
        llvm::DenseMap<mlir::Value, ContentsValue> values;
        llvm::MapVector<mlir::Operation *, llvm::SmallVector<ContentsValue, 4>> arrays;
        llvm::MapVector<mlir::Operation *, HeldProperties> objects;
        llvm::SmallPtrSet<mlir::Block *, 8> visited;
        ctjs::FrameEnterOp frame;
        bool frameExited = false;
        mlir::Operation * current = nullptr;
        std::optional<CountedLoop> loop;
    };
    State state;
    for (mlir::BlockArgument argument : function.getBody().front().getArguments()) {
        if (!spend()) { return refuse(ArrayContentsFailure::WorkLimit, function); }
        if (llvm::isa<ctjs::ValueType>(argument.getType())) {
            state.values[argument] = {argument, ContentsKind::Opaque};
        }
    }
    state.visited.insert(&function.getBody().front());
    state.current = &function.getBody().front().front();
    llvm::SmallVector<State, 2> alternatives;
    llvm::SmallPtrSet<mlir::Operation *, 8> arraySites;
    llvm::SmallPtrSet<mlir::Operation *, 8> objectSites;
    const auto held = [&](mlir::Value value) { return state.values.lookup(value); };
    const auto origin = [&](mlir::Value value) { return held(value).origin(); };
    const auto transport = [&](State & path, mlir::ValueRange arguments,
                               mlir::ValueRange operands) {
        if (arguments.size() != operands.size()) {
            return ArrayContentsFailure::UnsupportedControlFlow;
        }
        // Read every source before assigning any destination: successor operands
        // are simultaneous, including swaps. One kind replaces exact/opaque state.
        if (!spend(operands.size())) { return ArrayContentsFailure::WorkLimit; }
        llvm::SmallVector<ContentsValue, 8> incoming;
        for (mlir::Value value : operands) {
            if (!spend()) { return ArrayContentsFailure::WorkLimit; }
            const ContentsValue fact = path.values.lookup(value);
            if (!fact.original) { return ArrayContentsFailure::UnknownValue; }
            incoming.push_back(fact);
        }
        for (auto [argument, fact] : llvm::zip(arguments, incoming)) {
            path.values[argument] = fact;
        }
        return ArrayContentsFailure::None;
    };
    const auto forward = [&](State & path, mlir::Block * next, mlir::ValueRange operands) {
        if (next->getParent() != &function.getBody() || next->empty() ||
            (!path.visited.insert(next).second &&
             (!path.loop || (next != path.loop->header && next != path.loop->body)))) {
            return ArrayContentsFailure::UnsupportedControlFlow;
        }
        const auto failure = transport(path, next->getArguments(), operands);
        if (failure != ArrayContentsFailure::None) { return failure; }
        path.current = &next->front();
        return ArrayContentsFailure::None;
    };
    const auto snapshot = [&]() -> std::optional<State> {
        // Path enumeration can be exponential. Charge every copied value,
        // visited block, container and element before allocating the snapshot.
        if (!spend(state.values.size()) || !spend(state.visited.size())) { return std::nullopt; }
        for (const auto & [array, elements] : state.arrays) {
            (void)array;
            if (!spend() || !spend(elements.size())) { return std::nullopt; }
        }
        for (const auto & [object, properties] : state.objects) {
            (void)object;
            if (!spend() || !spend(properties.size())) { return std::nullopt; }
        }
        return state;
    };
    const auto alternative = [&](mlir::Block * next, mlir::ValueRange operands) {
        auto copy = snapshot();
        if (!copy) { return ArrayContentsFailure::WorkLimit; }
        const auto failure = forward(*copy, next, operands);
        if (failure == ArrayContentsFailure::None) { alternatives.push_back(std::move(*copy)); }
        return failure;
    };
    const auto countedLoop = [&](mlir::Block * header, mlir::Block * body, mlir::ValueRange initial,
                                 mlir::Value condition, mlir::ValueRange intoBody,
                                 mlir::ValueRange backedge) {
        constexpr auto unsupported = ArrayContentsFailure::UnsupportedControlFlow;
        if (state.loop || body == header || initial.size() != header->getNumArguments() ||
            intoBody.size() != body->getNumArguments() ||
            backedge.size() != header->getNumArguments()) {
            return unsupported;
        }
        auto truthy = condition.getDefiningOp<ctjs::TruthyOp>();
        auto negation = truthy ? truthy.getValue().getDefiningOp<ctjs::UnaryOp>() : ctjs::UnaryOp{};
        auto compare = truthy ? (negation ? negation.getOperand() : truthy.getValue())
                                    .getDefiningOp<ctjs::CompareOp>()
                              : ctjs::CompareOp{};
        const auto forwardKind = negation ? ctjs::CompareKind::Ge : ctjs::CompareKind::Lt;
        const auto reversedKind = negation ? ctjs::CompareKind::Le : ctjs::CompareKind::Gt;
        if (!compare || (compare.getKind() != forwardKind && compare.getKind() != reversedKind) ||
            (negation &&
             (negation.getKind() != ctjs::UnaryKind::Not || negation->getBlock() != header)) ||
            truthy->getBlock() != header || compare->getBlock() != header) {
            return unsupported;
        }
        // Normalize only the proof operands. The original comparison and its
        // evaluation order stay intact. Negated inclusive comparisons require
        // both operands independently proved bounded Numbers below: NaN would
        // invalidate !(index >= length) == (index < length).
        const bool reversed = compare.getKind() == reversedKind;
        auto index =
            llvm::dyn_cast<mlir::BlockArgument>(reversed ? compare.getRhs() : compare.getLhs());
        auto length =
            (reversed ? compare.getLhs() : compare.getRhs()).getDefiningOp<ctjs::GetPropertyOp>();
        const mlir::Value array = length ? length.getObject() : mlir::Value{};
        auto carriedArray = llvm::dyn_cast_if_present<mlir::BlockArgument>(array);
        auto directArray =
            array ? array.getDefiningOp<ctjs::CreateArrayOp>() : ctjs::CreateArrayOp{};
        if (!index || index.getOwner() != header ||
            (!(carriedArray && carriedArray.getOwner() == header) && !directArray) ||
            length->getBlock() != header ||
            ownObjectKey(length->getOperand(1)) !=
                mlir::StringAttr::get(function.getContext(), "length")) {
            return unsupported;
        }
        // Read actual/formal transport, not register numbers or source names.
        const auto fromHeader = [&](mlir::Value value) -> mlir::Value {
            auto argument = llvm::dyn_cast<mlir::BlockArgument>(value);
            if (!argument || argument.getOwner() != body) { return {}; }
            return intoBody[argument.getArgNumber()];
        };
        auto * step = backedge[index.getArgNumber()].getDefiningOp();
        auto dynamic = llvm::dyn_cast_or_null<ctjs::BinaryOp>(step);
        auto numeric = llvm::dyn_cast_or_null<ctjs::BinaryStaticOp>(step);
        if ((!dynamic && !numeric) || step->getBlock() != body ||
            (carriedArray && fromHeader(backedge[carriedArray.getArgNumber()]) != array)) {
            return unsupported;
        }
        const bool subtract = dynamic && dynamic.getKind() == ctjs::BinaryKind::Sub;
        if (!subtract &&
            (dynamic ? dynamic.getKind() : numeric.getKind()) != ctjs::BinaryKind::Add) {
            return unsupported;
        }
        // Normalize only Add proof operands; subtraction requires index - stride.
        // The original source order and exact Number requirements stay intact.
        const unsigned indexOperand =
            subtract || fromHeader(step->getOperand(0)) == index ? 0U : 1U;
        if (fromHeader(step->getOperand(indexOperand)) != index) { return unsupported; }
        // A held positive step must survive every backedge unchanged. Read a body
        // formal through its actual header operand before the body has executed.
        // Add needs a positive Number; Sub needs an original negative Number.
        // A String or unknown operand cannot borrow an opcode's certificate.
        mlir::Value increment = step->getOperand(1U - indexOperand);
        if (mlir::Value forwarded = fromHeader(increment)) { increment = forwarded; }
        if (!spend()) { return ArrayContentsFailure::WorkLimit; }
        if (auto argument = llvm::dyn_cast<mlir::BlockArgument>(increment);
            argument && argument.getOwner() == header) {
            const mlir::Value next = backedge[argument.getArgNumber()];
            if (next != increment && fromHeader(next) != increment) { return unsupported; }
        }
        auto stride = boundedNumber(increment, subtract);
        if (!stride) {
            // Repeated producers need their own invariant proof; a prior
            // iteration's saved fact cannot certify a header/body computation.
            auto * definition = increment.getDefiningOp();
            if (definition &&
                (definition->getBlock() == header || definition->getBlock() == body)) {
                return unsupported;
            }
            const ContentsValue value = held(increment);
            stride = subtract ? value.negativeIntegerNumber : value.integerNumber;
            if (!stride) { stride = boundedNumber(value.origin(), subtract); }
        }
        if (!stride || *stride == 0) { return unsupported; }
        // Initialization may be a saved length or an exact arithmetic result.
        // The original input and its transported snapshot must independently
        // supply the same bounded Number on this exact path.
        std::optional<std::size_t> start;
        for (mlir::Value value : {initial[index.getArgNumber()], mlir::Value{index}}) {
            if (!spend()) { return ArrayContentsFailure::WorkLimit; }
            const ContentsValue input = held(value);
            const auto number =
                input.integerNumber ? input.integerNumber : boundedNumber(input.origin());
            if (!number || (start && start != number)) { return unsupported; }
            start = number;
        }
        // ponytail: one read-only header/body pair; nested control, allocation
        // and mutation need a separate instance/lifetime proof. Primitive kinds
        // and every indexed element still pass the ordinary operation transfers.
        for (mlir::Block * block : {header, body}) {
            for (mlir::Operation & operation : *block) {
                if (!spend()) { return ArrayContentsFailure::WorkLimit; }
                if (&operation == block->getTerminator()) { continue; }
                if (!llvm::isa<ctjs::ConstantOp, ctjs::GetPropertyOp, ctjs::CompareOp,
                               ctjs::TruthyOp, ctjs::UnaryOp, ctjs::BinaryOp, ctjs::BinaryStaticOp,
                               ctjs::ConvertOp, ctjs::RootOp>(&operation)) {
                    return unsupported;
                }
            }
        }
        const mlir::Value base = origin(array);
        // SCF can eliminate an invariant array parameter. A direct allocation
        // already executed on this exact path needs no backedge transport;
        // the read-only body census above still excludes repeated allocation.
        if (directArray && (base != array || directArray->getBlock() == header ||
                            directArray->getBlock() == body)) {
            return unsupported;
        }
        auto found = state.arrays.find(base ? base.getDefiningOp() : nullptr);
        if (found == state.arrays.end() || found->second.size() > 4294967295ULL) {
            return unsupported;
        }
        if (!spend()) { return ArrayContentsFailure::WorkLimit; }
        const std::size_t size = found->second.size();
        // A zero-trip loop preserves its original index. Otherwise find the last
        // visited index relative to the start, and bound its final update before
        // addition; overshooting length must stay in the exact Number range.
        std::size_t finalIndex = *start;
        if (*start < size) {
            const std::size_t last = size - 1 - (size - 1 - *start) % *stride;
            if (*stride > 4294967295ULL - last) { return unsupported; }
            finalIndex = last + *stride;
        }
        state.loop = CountedLoop{header, body, index, array, found->first, size, finalIndex};
        return ArrayContentsFailure::None;
    };
    const auto cfgCountedLoop = [&](mlir::cf::CondBranchOp branch, mlir::cf::BranchOp latch) {
        constexpr auto unsupported = ArrayContentsFailure::UnsupportedControlFlow;
        mlir::Block * header = branch->getBlock();
        mlir::Block * body = branch.getTrueDest();
        if (branch.getFalseDest() == header || branch.getFalseDest() == body ||
            body->getParent() != &function.getBody()) {
            return unsupported;
        }
        mlir::Block * entry = nullptr;
        unsigned predecessors = 0;
        for (mlir::Block * predecessor : header->getPredecessors()) {
            if (!spend()) { return ArrayContentsFailure::WorkLimit; }
            ++predecessors;
            if (predecessor != body) { entry = predecessor; }
        }
        if (predecessors != 2 || entry == nullptr) { return unsupported; }
        predecessors = 0;
        for (mlir::Block * predecessor : body->getPredecessors()) {
            if (!spend()) { return ArrayContentsFailure::WorkLimit; }
            if (predecessor != header) { return unsupported; }
            ++predecessors;
        }
        if (predecessors != 1) { return unsupported; }
        auto incoming = llvm::dyn_cast<mlir::cf::BranchOp>(entry->getTerminator());
        if (!incoming || incoming.getDest() != header) { return unsupported; }
        return countedLoop(header, body, incoming.getDestOperands(), branch.getCondition(),
                           branch.getTrueDestOperands(), latch.getDestOperands());
    };
    const auto loopContinues = [&]() -> std::optional<bool> {
        const CountedLoop & loop = *state.loop;
        const ContentsValue index = held(loop.index);
        const auto number =
            index.integerNumber ? index.integerNumber : boundedNumber(index.origin());
        const mlir::Value base = origin(loop.array);
        if (!number || *number > loop.finalIndex || !base || base.getDefiningOp() != loop.site) {
            return std::nullopt;
        }
        return *number < loop.length;
    };
    while (true) {
        bool returned = false;
        while (state.current != nullptr) {
            mlir::Operation & op = *state.current;
            state.current = op.getNextNode();
            if (!spend()) { return refuse(ArrayContentsFailure::WorkLimit, &op); }
            if ((op.getNumRegions() != 0 && !llvm::isa<mlir::scf::IfOp, mlir::scf::WhileOp>(&op)) ||
                (op.getNumSuccessors() != 0 &&
                 !llvm::isa<mlir::cf::BranchOp, mlir::cf::CondBranchOp, mlir::cf::SwitchOp>(&op))) {
                return refuse(ArrayContentsFailure::UnsupportedControlFlow, &op);
            }
            if (auto entered = llvm::dyn_cast<ctjs::FrameEnterOp>(&op)) {
                // The importer enters before seeding registers or allocating any
                // tracked object. Its depth failure is real, but on that path no
                // object from these sites exists. This is NOT an effect proof that
                // permits erasing entry or treating it as pure/nonthrowing.
                if (&op != &function.getBody().front().front() || state.frame ||
                    entered.getRegCountAttr().getInt() < 0) {
                    return refuse(ArrayContentsFailure::InvalidFrame, &op);
                }
                state.frame = entered;
                state.values[state.frame.getResult()] = {state.frame.getResult()};
                continue;
            }
            if (auto exited = llvm::dyn_cast<ctjs::FrameExitOp>(&op)) {
                if (!state.frame || state.frameExited ||
                    origin(exited->getOperand(0)) != state.frame.getResult() ||
                    !llvm::isa_and_nonnull<ctjs::ReturnOp>(op.getNextNode())) {
                    return refuse(ArrayContentsFailure::InvalidFrame, &op);
                }
                state.frameExited = true;
                continue;
            }
            if (auto root = llvm::dyn_cast<ctjs::RootOp>(&op)) {
                // RootOp parks only in THIS frame's window (Frames.td). The exact
                // matching exit kills that window; no call, suspension or unknown
                // effect in this query can keep it or expose its contents.
                if (!state.frame || state.frameExited ||
                    origin(root->getOperand(0)) != state.frame.getResult()) {
                    return refuse(ArrayContentsFailure::InvalidFrame, &op);
                }
                if (!origin(root.getValue())) {
                    return refuse(ArrayContentsFailure::UnknownValue, &op);
                }
                continue;
            }
            if (state.frameExited && !llvm::isa<ctjs::ReturnOp>(&op)) {
                return refuse(ArrayContentsFailure::InvalidFrame, &op);
            }
            if (auto loop = llvm::dyn_cast<mlir::scf::WhileOp>(&op)) {
                if (!llvm::hasSingleElement(loop.getBefore()) ||
                    !llvm::hasSingleElement(loop.getAfter()) || loop.getBefore().front().empty() ||
                    loop.getAfter().front().empty()) {
                    return refuse(ArrayContentsFailure::UnsupportedControlFlow, &op);
                }
                auto condition = llvm::dyn_cast<mlir::scf::ConditionOp>(
                    loop.getBefore().front().getTerminator());
                auto yield =
                    llvm::dyn_cast<mlir::scf::YieldOp>(loop.getAfter().front().getTerminator());
                if (!condition || !yield || condition.getArgs().size() != loop.getNumResults()) {
                    return refuse(ArrayContentsFailure::UnsupportedControlFlow, &op);
                }
                auto failure = transport(state, loop.getBeforeArguments(), loop.getInits());
                if (failure != ArrayContentsFailure::None) { return refuse(failure, &op); }
                failure = countedLoop(&loop.getBefore().front(), &loop.getAfter().front(),
                                      loop.getInits(), condition.getCondition(),
                                      condition.getArgs(), yield.getOperands());
                if (failure != ArrayContentsFailure::None) { return refuse(failure, &op); }
                state.current = &loop.getBefore().front().front();
                continue;
            }
            if (auto condition = llvm::dyn_cast<mlir::scf::ConditionOp>(&op)) {
                auto loop = llvm::dyn_cast<mlir::scf::WhileOp>(condition->getParentOp());
                if (!loop || !state.loop || state.loop->header != op.getBlock()) {
                    return refuse(ArrayContentsFailure::UnsupportedControlFlow, &op);
                }
                const auto continued = loopContinues();
                if (!continued) {
                    return refuse(ArrayContentsFailure::UnsupportedControlFlow, &op);
                }
                const auto failure =
                    transport(state,
                              *continued ? mlir::ValueRange(loop.getAfterArguments())
                                         : mlir::ValueRange(loop.getResults()),
                              condition.getArgs());
                if (failure != ArrayContentsFailure::None) { return refuse(failure, &op); }
                state.current = *continued ? &loop.getAfter().front().front() : loop->getNextNode();
                if (!*continued) { state.loop.reset(); }
                continue;
            }
            if (auto branch = llvm::dyn_cast<mlir::scf::IfOp>(&op)) {
                if (!origin(branch.getCondition())) {
                    return refuse(ArrayContentsFailure::UnknownValue, &op);
                }
                // ponytail: single-block structured arms only; nested CFG
                // needs its own lifetime/transport proof. Both arms run in
                // separate exact states, including an implicit empty else.
                for (mlir::Region & region : branch->getRegions()) {
                    if (!spend()) { return refuse(ArrayContentsFailure::WorkLimit, &op); }
                    if (region.empty() && &region == &branch.getElseRegion() &&
                        branch.getNumResults() == 0) {
                        continue;
                    }
                    if (!llvm::hasSingleElement(region) || region.front().empty() ||
                        region.front().getNumArguments() != 0 ||
                        !llvm::isa<mlir::scf::YieldOp>(region.front().getTerminator())) {
                        return refuse(ArrayContentsFailure::UnsupportedControlFlow, &op);
                    }
                }
                auto copy = snapshot();
                if (!copy) { return refuse(ArrayContentsFailure::WorkLimit, &op); }
                if (!branch.getElseRegion().empty()) {
                    copy->current = &branch.getElseRegion().front().front();
                }
                alternatives.push_back(std::move(*copy));
                state.current = &branch.getThenRegion().front().front();
                continue;
            }
            if (auto yield = llvm::dyn_cast<mlir::scf::YieldOp>(&op)) {
                if (auto loop = llvm::dyn_cast<mlir::scf::WhileOp>(yield->getParentOp())) {
                    if (!state.loop || state.loop->body != op.getBlock()) {
                        return refuse(ArrayContentsFailure::UnsupportedControlFlow, &op);
                    }
                    const auto failure =
                        transport(state, loop.getBeforeArguments(), yield.getOperands());
                    if (failure != ArrayContentsFailure::None) { return refuse(failure, &op); }
                    state.current = &loop.getBefore().front().front();
                    continue;
                }
                auto branch = llvm::dyn_cast<mlir::scf::IfOp>(yield->getParentOp());
                if (!branch) { return refuse(ArrayContentsFailure::UnsupportedControlFlow, &op); }
                const auto failure = transport(state, branch.getResults(), yield.getOperands());
                if (failure != ArrayContentsFailure::None) { return refuse(failure, &op); }
                state.current = branch->getNextNode();
                continue;
            }
            if (auto branch = llvm::dyn_cast<mlir::cf::BranchOp>(&op)) {
                const auto failure = forward(state, branch.getDest(), branch.getDestOperands());
                if (failure != ArrayContentsFailure::None) { return refuse(failure, &op); }
                continue;
            }
            if (auto branch = llvm::dyn_cast<mlir::cf::CondBranchOp>(&op)) {
                if (!origin(branch.getCondition())) {
                    return refuse(ArrayContentsFailure::UnknownValue, &op);
                }
                if (!state.loop && !branch.getTrueDest()->empty()) {
                    auto latch =
                        llvm::dyn_cast<mlir::cf::BranchOp>(branch.getTrueDest()->getTerminator());
                    if (latch && latch.getDest() == op.getBlock()) {
                        const auto failure = cfgCountedLoop(branch, latch);
                        if (failure != ArrayContentsFailure::None) { return refuse(failure, &op); }
                    }
                }
                if (state.loop && state.loop->header == op.getBlock()) {
                    const auto continued = loopContinues();
                    if (!continued) {
                        return refuse(ArrayContentsFailure::UnsupportedControlFlow, &op);
                    }
                    // Only this independently checked finite guard selects an
                    // edge. Exact replay records every actual element alternative;
                    // all unrelated conditions retain both structural paths.
                    if (!*continued) { state.loop.reset(); }
                    const auto failure = forward(
                        state, *continued ? branch.getTrueDest() : branch.getFalseDest(),
                        *continued ? branch.getTrueDestOperands() : branch.getFalseDestOperands());
                    if (failure != ArrayContentsFailure::None) { return refuse(failure, &op); }
                    continue;
                }
                auto failure = alternative(branch.getFalseDest(), branch.getFalseDestOperands());
                if (failure != ArrayContentsFailure::None) { return refuse(failure, &op); }
                failure = forward(state, branch.getTrueDest(), branch.getTrueDestOperands());
                if (failure != ArrayContentsFailure::None) { return refuse(failure, &op); }
                continue;
            }
            if (auto branch = llvm::dyn_cast<mlir::cf::SwitchOp>(&op)) {
                if (!origin(branch.getFlag())) {
                    return refuse(ArrayContentsFailure::UnknownValue, &op);
                }
                // The flag selects an edge without JS coercion. Prove every
                // structural edge, so neither case values nor exhaustiveness
                // supply a liveness fact. Repeated destinations still carry
                // their own operands. Push in reverse for default/case order.
                for (unsigned i = branch->getNumSuccessors() - 1; i != 0; --i) {
                    const auto failure = alternative(branch.getCaseDestinations()[i - 1],
                                                     branch.getCaseOperands(i - 1));
                    if (failure != ArrayContentsFailure::None) { return refuse(failure, &op); }
                }
                const auto failure =
                    forward(state, branch.getDefaultDestination(), branch.getDefaultOperands());
                if (failure != ArrayContentsFailure::None) { return refuse(failure, &op); }
                continue;
            }
            // Truthy is total, noncapturing and nonthrowing (Operators.td). An
            // external input remains unknown for every other use; only its i1
            // result can be carried as a predicate. Neither branch is pruned.
            if (auto constant = llvm::dyn_cast<ctjs::ConstantOp>(&op)) {
                ContentsKind kind = ContentsKind::Identity;
                if (llvm::isa<ctjs::StringAttr>(constant.getValue())) {
                    kind = ContentsKind::String;
                } else if (llvm::isa<ctjs::BigIntAttr>(constant.getValue())) {
                    kind = ContentsKind::BigInt;
                } else if (llvm::isa<ctjs::UndefinedAttr, ctjs::NullAttr, ctjs::BooleanAttr,
                                     ctjs::NumberAttr>(constant.getValue())) {
                    kind = ContentsKind::NonBigInt;
                }
                state.values[constant.getResult()] = {constant.getResult(), kind};
                continue;
            }
            if (auto truthy = llvm::dyn_cast<ctjs::TruthyOp>(&op)) {
                state.values[truthy.getResult()] = {truthy.getResult()};
                continue;
            }
            if (auto compare = llvm::dyn_cast<ctjs::CompareOp>(&op)) {
                // Strict equality reads primitive contents or object identity;
                // it cannot coerce, call, throw or retain either operand
                // (Operators.td, value::strict_equals). The independent Boolean
                // result is known even when an operand is an opaque entry.
                // Every coercing kind needs BOTH original primitive origins.
                // Mixed BigInt comparisons have separate VM value-semantics
                // gaps, but no primitive conversion calls an object hook or
                // retains an input identity. Every structural arm is checked;
                // this query never infers a value, key or liveness fact.
                switch (compare.getKind()) {
                case ctjs::CompareKind::StrictEq: break;
                case ctjs::CompareKind::Eq:
                case ctjs::CompareKind::Lt:
                case ctjs::CompareKind::Le:
                case ctjs::CompareKind::Gt:
                case ctjs::CompareKind::Ge: {
                    const ContentsValue left = held(compare.getLhs());
                    const ContentsValue right = held(compare.getRhs());
                    const mlir::Value lhs = left.origin();
                    const mlir::Value rhs = right.origin();
                    if (!lhs || !rhs) {
                        return refuse(ArrayContentsFailure::UnsupportedOperation, &op);
                    }
                    if ((!left.nonBigInt() && !left.bigInt()) ||
                        (!right.nonBigInt() && !right.bigInt())) {
                        return refuse(ArrayContentsFailure::UnsupportedOperation, &op);
                    }
                    // loose_equals uses digits, String parsing and static
                    // numeric conversions. Its BigInt/Boolean arm additionally
                    // enters to_primitive's guard; all relational kinds do so.
                    // Primitive inputs return before any lookup or user call.
                    // The guard's independent Error cannot expose this whole
                    // frame's unpublished fresh locals. Calls, handlers and
                    // publication still refuse. Retention supplies no normal
                    // completion, allocation-success or no-throw contract.
                    break;
                }
                default: return refuse(ArrayContentsFailure::UnsupportedOperation, &op);
                }
                state.values[compare.getResult()] = {compare.getResult(), ContentsKind::NonBigInt};
                continue;
            }
            if (auto convert = llvm::dyn_cast<ctjs::ConvertOp>(&op)) {
                // ToBoolean, like Truthy, is total and noncapturing, but returns
                // a !ctjs.value Boolean. Its primitive result carries no heap
                // alias; this does not turn its input into a known origin.
                if (convert.getKind() != ctjs::ConvertKind::ToBoolean) {
                    return refuse(ArrayContentsFailure::UnsupportedOperation, &op);
                }
                state.values[convert.getResult()] = {convert.getResult(), ContentsKind::NonBigInt};
                continue;
            }
            if (auto unary = llvm::dyn_cast<ctjs::UnaryOp>(&op)) {
                // Not/TypeOf/Void never invoke user code or retain their
                // already-evaluated operand (Operators.td, VM coerce.cpp).
                // Not yields a Boolean; TypeOf yields a String; Void yields
                // Undefined. TypeOf's VM String allocation may hit the fatal
                // allocation ceiling, but carries no operand object identity.
                // This is a contents proof, not a no-allocation/effect claim.
                // Keep an independent primitive origin without a value, key,
                // operand alias or structural-edge liveness fact.
                ContentsKind kind = ContentsKind::NonBigInt;
                std::optional<std::size_t> integerNumber;
                std::optional<std::size_t> negativeIntegerNumber;
                switch (unary.getKind()) {
                case ctjs::UnaryKind::Not:
                case ctjs::UnaryKind::Void: break;
                case ctjs::UnaryKind::TypeOf: kind = ContentsKind::String; break;
                case ctjs::UnaryKind::Neg:
                case ctjs::UnaryKind::Plus:
                case ctjs::UnaryKind::BitNot: {
                    const ContentsValue input = held(unary.getOperand());
                    if (input.bigInt()) {
                        if (unary.getKind() == ctjs::UnaryKind::Plus) {
                            // to_number_value rejects BigInt before any lookup
                            // or user conversion. Its TypeError (or depth-guard
                            // Error) has no input/local object edge. The entire
                            // frame still excludes calls, handlers and publication.
                            // Keep the independent Number carrier used by the VM
                            // and inspect EVERY structural continuation; neither
                            // successful completion nor dead code follows here.
                            break;
                        }
                        // negate_value/bit_not_value allocate independent BigInt
                        // digits before any conversion or user callback. Record
                        // the actual category, never a Number or concrete value.
                        // No allocation-success/native effect claim follows.
                        if (!spend()) { return refuse(ArrayContentsFailure::WorkLimit, &op); }
                        kind = ContentsKind::BigInt;
                        break;
                    }
                    if (!input.nonBigInt()) {
                        return refuse(ArrayContentsFailure::UnsupportedOperation, &op);
                    }
                    // Known primitive non-BigInt inputs cannot invoke object
                    // conversion or reach Plus's catchable BigInt TypeError.
                    // All three return an independent Number. BitNot uses the
                    // VM's static conversion, but objects stay outside this
                    // common source-compatible proof. String parsing may
                    // allocate C++ temporaries; allocation success is unproved.
                    // Neg/Plus still enter a recursion guard that may throw an
                    // unrelated RangeError. This whole-frame retention query
                    // rejects publication, calls and handlers, so that early
                    // exit cannot expose its fresh locals. This is NOT proof
                    // of normal completion or an effect/no-throw contract.
                    const auto positive = boundedConvertedNumber(input);
                    const auto negative = input.negativeIntegerNumber
                                              ? input.negativeIntegerNumber
                                              : boundedNumber(input.origin(), true);
                    if (unary.getKind() != ctjs::UnaryKind::BitNot) {
                        // Preserve held signed Numbers through Plus/Neg, keeping
                        // negative magnitudes separate from own-index facts.
                        // Canonical original Strings share the exact decimal
                        // conversion used by Sub; zero stays nonnegative.
                        integerNumber = positive;
                        negativeIntegerNumber = positive ? std::nullopt : negative;
                        if (unary.getKind() == ctjs::UnaryKind::Neg && integerNumber != 0) {
                            std::swap(integerNumber, negativeIntegerNumber);
                        }
                        if ((integerNumber || negativeIntegerNumber) && !spend()) {
                            return refuse(ArrayContentsFailure::WorkLimit, &op);
                        }
                    } else {
                        // Complement the exact ToUint32 bits, then recover the
                        // signed Number magnitude without a signed overflow.
                        // Canonical original Strings use the same exact decimal
                        // conversion as Plus/Neg; the result keeps its identity.
                        if (positive || negative) {
                            const std::uint32_t bits =
                                ~(positive ? static_cast<std::uint32_t>(*positive)
                                           : 0U - static_cast<std::uint32_t>(*negative));
                            if (bits < 2147483648ULL) {
                                integerNumber = bits;
                            } else {
                                negativeIntegerNumber = 4294967296ULL - bits;
                            }
                            if (!spend()) { return refuse(ArrayContentsFailure::WorkLimit, &op); }
                        }
                    }
                    break;
                }
                default: return refuse(ArrayContentsFailure::UnsupportedOperation, &op);
                }
                state.values[unary.getResult()] = {unary.getResult(), kind, integerNumber,
                                                   negativeIntegerNumber};
                continue;
            }
            if (auto binary = llvm::dyn_cast<ctjs::BinaryOp>(&op)) {
                switch (binary.getKind()) {
                case ctjs::BinaryKind::Sub:
                case ctjs::BinaryKind::Mul:
                case ctjs::BinaryKind::Div:
                case ctjs::BinaryKind::Mod:
                case ctjs::BinaryKind::Pow:
                case ctjs::BinaryKind::Add:
                case ctjs::BinaryKind::Concat: break;
                default: return refuse(ArrayContentsFailure::UnsupportedOperation, &op);
                }
                const ContentsValue left = held(binary.getLhs());
                const ContentsValue right = held(binary.getRhs());
                const mlir::Value lhs = left.origin();
                const mlir::Value rhs = right.origin();
                if (lhs && rhs && (left.nonBigInt() || left.bigInt()) &&
                    (right.nonBigInt() || right.bigInt()) &&
                    (binary.getKind() == ctjs::BinaryKind::Concat ||
                     (binary.getKind() == ctjs::BinaryKind::Add &&
                      (left.string() || right.string())))) {
                    // Concat converts both primitives before bigint_binary;
                    // Add selects its String arm before mixed-BigInt errors.
                    // BigInt conversion copies digits, never an input object
                    // or a user callback. Only an independently proved String
                    // permits Add: a generic non-BigInt result can be Number.
                    // Add's guarded ToPrimitive still has an unrelated Error
                    // exit. The entire frame's call/publication/handler refusals
                    // apply; this proves neither completion nor native effects.
                    if (binary.getKind() == ctjs::BinaryKind::Add) {
                        if (!spend()) { return refuse(ArrayContentsFailure::WorkLimit, &op); }
                    }
                    state.values[binary.getResult()] = {binary.getResult(), ContentsKind::String};
                    continue;
                }
                if (lhs && rhs && left.bigInt() && right.bigInt() &&
                    (binary.getKind() == ctjs::BinaryKind::Add ||
                     binary.getKind() == ctjs::BinaryKind::Sub ||
                     binary.getKind() == ctjs::BinaryKind::Mul ||
                     binary.getKind() == ctjs::BinaryKind::Div ||
                     binary.getKind() == ctjs::BinaryKind::Mod ||
                     binary.getKind() == ctjs::BinaryKind::Pow)) {
                    // bigint_binary combines digits into a fresh independent
                    // BigInt. Add first enters to_primitive's depth guard;
                    // Div/Mod raise an independent RangeError for zero divisors,
                    // and Pow for negative or VM-capped oversized exponents.
                    // bigint_pow checks those bounds before computing digits,
                    // including the VM's unconditional cap for small bases.
                    // No user conversion or local object edge enters those
                    // errors. The whole-frame exclusion of calls,
                    // handlers and publication keeps either early exit from
                    // exposing unpublished fresh objects. The normal result's
                    // category proves neither allocation success nor normal
                    // completion or no-throw/native effects. Mixed inputs remain
                    // a separate boundary even on observed success.
                    if (!spend()) { return refuse(ArrayContentsFailure::WorkLimit, &op); }
                    state.values[binary.getResult()] = {binary.getResult(), ContentsKind::BigInt};
                    continue;
                }
                if ((binary.getKind() == ctjs::BinaryKind::Add ||
                     binary.getKind() == ctjs::BinaryKind::Sub ||
                     binary.getKind() == ctjs::BinaryKind::Mul ||
                     binary.getKind() == ctjs::BinaryKind::Div ||
                     binary.getKind() == ctjs::BinaryKind::Mod ||
                     binary.getKind() == ctjs::BinaryKind::Pow) &&
                    lhs && rhs &&
                    ((left.bigInt() && right.nonBigInt()) ||
                     (left.nonBigInt() && right.bigInt()))) {
                    // Mixed original primitives cannot retain local objects:
                    // bigint_binary returns an independent TypeError/Undefined.
                    // Add first makes both operands primitive and may concatenate
                    // Strings instead; neither that result nor its depth-guard
                    // Error aliases an input. Only the separate String proof
                    // above supplies that category; this result is never BigInt.
                    // Calls, handlers and publication remain excluded across
                    // the whole frame. Check EVERY structural continuation:
                    // retention proves no successful completion or native effect.
                    state.values[binary.getResult()] = {binary.getResult(),
                                                        ContentsKind::NonBigInt};
                    continue;
                }
                if (!lhs || !rhs || !left.nonBigInt() || !right.nonBigInt()) {
                    return refuse(ArrayContentsFailure::UnsupportedOperation, &op);
                }
                // With primitive non-BigInt originals, binary_op cannot call
                // object conversions or return an operand object. Sub/Mul/Div/
                // Mod/Pow return Number via to_number_value. Add uses guarded
                // to_primitive followed by static Number/String operations;
                // Concat uses primitive to_string. Their result is independent,
                // without a Number/String tag, value, index or key inference
                // except for bounded exact Number arithmetic below.
                // Add and numeric conversions have a depth guard that may
                // throw an unrelated RangeError. This whole-frame query refuses
                // calls, handlers and publication, so it cannot retain fresh
                // locals on that exit. This is retention-only evidence, never
                // a normal-completion or no-throw/effect contract. String
                // results allocate in the VM and static conversions can allocate
                // C++ temporaries; absence/success of allocation is unproved.
                ContentsValue result{binary.getResult(), ContentsKind::NonBigInt};
                if (binary.getKind() == ctjs::BinaryKind::Add) {
                    boundedNumberSum(left, right, result);
                }
                if (binary.getKind() == ctjs::BinaryKind::Sub) {
                    auto original = left.integerNumber ? left.integerNumber : boundedNumber(lhs);
                    if (!original && left.string()) { original = ownArrayIndex(lhs); }
                    auto literal = rhs.getDefiningOp<ctjs::ConstantOp>();
                    const auto number =
                        right.integerNumber ? right.integerNumber : boundedNumber(rhs);
                    const auto stringOffset =
                        literal && llvm::isa<ctjs::StringAttr>(literal.getValue())
                            ? ownArrayIndex(rhs)
                            : std::nullopt;
                    const auto offset = number ? number : stringOffset;
                    // Held Numbers keep their read-time value; canonical decimal
                    // Strings convert exactly without object hooks. Both operands
                    // are bounded integers; guard subtraction before unsigned wrap.
                    if (original && offset && *offset <= *original) {
                        if (!spend()) { return refuse(ArrayContentsFailure::WorkLimit, &op); }
                        result.integerNumber = *original - *offset;
                    } else if (original && offset && *original < *offset) {
                        // The same exact canonical String conversion can yield
                        // a negative Number; keep its magnitude out of indices.
                        if (!spend()) { return refuse(ArrayContentsFailure::WorkLimit, &op); }
                        result.negativeIntegerNumber = *offset - *original;
                    } else if (const auto magnitude = right.negativeIntegerNumber
                                                          ? right.negativeIntegerNumber
                                                          : boundedNumber(rhs, true);
                               original && magnitude && *magnitude <= 4294967295ULL - *original) {
                        if (!spend()) { return refuse(ArrayContentsFailure::WorkLimit, &op); }
                        result.integerNumber = *original + *magnitude;
                    } else if (const auto negative = left.negativeIntegerNumber
                                                         ? left.negativeIntegerNumber
                                                         : boundedNumber(lhs, true);
                               negative) {
                        // Negative left Numbers use the same exact offset proof.
                        // Cancellation is bounded; adding magnitudes must exclude
                        // overflow. The result keeps its original zero.
                        if (offset && *offset <= 4294967295ULL - *negative) {
                            if (!spend()) { return refuse(ArrayContentsFailure::WorkLimit, &op); }
                            result.negativeIntegerNumber = *negative + *offset;
                        } else if (magnitude) {
                            if (!spend()) { return refuse(ArrayContentsFailure::WorkLimit, &op); }
                            if (*magnitude >= *negative) {
                                result.integerNumber = *magnitude - *negative;
                            } else {
                                result.negativeIntegerNumber = *negative - *magnitude;
                            }
                        }
                    }
                }
                if (binary.getKind() == ctjs::BinaryKind::Mul) {
                    auto a = boundedConvertedNumber(left);
                    auto b = boundedConvertedNumber(right);
                    const bool negative = a.has_value() != b.has_value();
                    if (!a) {
                        a = left.negativeIntegerNumber ? left.negativeIntegerNumber
                                                       : boundedNumber(lhs, true);
                    }
                    if (!b) {
                        b = right.negativeIntegerNumber ? right.negativeIntegerNumber
                                                        : boundedNumber(rhs, true);
                    }
                    // Original Boolean/null and canonical Strings share unary's
                    // exact conversion. A bounded product excludes rounding and
                    // wrap; zero keeps its original signed value as the origin.
                    if (a && b && (*b == 0 || *a <= 4294967295ULL / *b)) {
                        if (!spend()) { return refuse(ArrayContentsFailure::WorkLimit, &op); }
                        const auto product = *a * *b;
                        if (negative && product != 0) {
                            result.negativeIntegerNumber = product;
                        } else {
                            result.integerNumber = product;
                        }
                    }
                }
                if (binary.getKind() == ctjs::BinaryKind::Div ||
                    binary.getKind() == ctjs::BinaryKind::Mod) {
                    auto a = boundedConvertedNumber(left);
                    auto b = boundedConvertedNumber(right);
                    const bool remainder = binary.getKind() == ctjs::BinaryKind::Mod;
                    const bool negative = remainder ? !a : a.has_value() != b.has_value();
                    if (!a) {
                        a = left.negativeIntegerNumber ? left.negativeIntegerNumber
                                                       : boundedNumber(lhs, true);
                    }
                    if (!b) {
                        b = right.negativeIntegerNumber ? right.negativeIntegerNumber
                                                        : boundedNumber(rhs, true);
                    }
                    // Original Boolean/null and canonical Strings share unary's
                    // exact conversion. Bounded operands give an exact remainder;
                    // division also needs zero remainder. Mod keeps the dividend's
                    // sign, regardless of divisor sign. Keep the original signed zero.
                    if (a && b && *b != 0 && (remainder || *a % *b == 0)) {
                        if (!spend()) { return refuse(ArrayContentsFailure::WorkLimit, &op); }
                        const auto magnitude = remainder ? *a % *b : *a / *b;
                        if (negative && magnitude != 0) {
                            result.negativeIntegerNumber = magnitude;
                        } else {
                            result.integerNumber = magnitude;
                        }
                    }
                }
                if (binary.getKind() == ctjs::BinaryKind::Pow) {
                    auto exponent = right.integerNumber ? right.integerNumber : boundedNumber(rhs);
                    auto positive = left.integerNumber ? left.integerNumber : boundedNumber(lhs);
                    if (!exponent && right.string()) { exponent = ownArrayIndex(rhs); }
                    if (!positive && left.string()) { positive = ownArrayIndex(lhs); }
                    const auto negative = left.negativeIntegerNumber ? left.negativeIntegerNumber
                                                                     : boundedNumber(lhs, true);
                    const auto negativeExponent = right.negativeIntegerNumber
                                                      ? right.negativeIntegerNumber
                                                      : boundedNumber(rhs, true);
                    // ponytail: only exact zero/unit identities; general powers
                    // need Number's implementation-approximated result proof.
                    // Canonical Strings share Sub's exact conversion. Keep the
                    // result identity, including signed zero's parity.
                    if (exponent && *exponent <= 1 && (positive || negative)) {
                        if (!spend()) { return refuse(ArrayContentsFailure::WorkLimit, &op); }
                        result.integerNumber =
                            *exponent == 0 ? std::optional<std::size_t>{1} : positive;
                        if (*exponent == 1 && !positive) {
                            result.negativeIntegerNumber = negative;
                        }
                    } else if ((positive == 0 && exponent) ||
                               (positive == 1 && (exponent || negativeExponent))) {
                        // Zero needs a positive exponent; exponent zero was
                        // handled above. One still requires a finite Number.
                        if (!spend()) { return refuse(ArrayContentsFailure::WorkLimit, &op); }
                        result.integerNumber = positive;
                    } else if (negative == 1 && (exponent || negativeExponent)) {
                        // The sign of an integer exponent does not change parity.
                        if (!spend()) { return refuse(ArrayContentsFailure::WorkLimit, &op); }
                        const auto magnitude = exponent ? *exponent : *negativeExponent;
                        if (magnitude % 2 == 0) {
                            result.integerNumber = 1;
                        } else {
                            result.negativeIntegerNumber = 1;
                        }
                    }
                }
                state.values[binary.getResult()] = result;
                continue;
            }
            if (auto binary = llvm::dyn_cast<ctjs::BinaryStaticOp>(&op)) {
                switch (binary.getKind()) {
                case ctjs::BinaryKind::Add:
                case ctjs::BinaryKind::BitAnd:
                case ctjs::BinaryKind::BitOr:
                case ctjs::BinaryKind::BitXor:
                case ctjs::BinaryKind::Shl:
                case ctjs::BinaryKind::Shr:
                case ctjs::BinaryKind::UShr: break;
                default: return refuse(ArrayContentsFailure::UnsupportedOperation, &op);
                }
                const ContentsValue left = held(binary.getLhs());
                const ContentsValue right = held(binary.getRhs());
                const mlir::Value lhs = left.origin();
                const mlir::Value rhs = right.origin();
                if (!lhs || !rhs) { return refuse(ArrayContentsFailure::UnknownValue, &op); }
                if ((left.bigInt() && right.nonBigInt()) || (left.nonBigInt() && right.bigInt()) ||
                    (binary.getKind() == ctjs::BinaryKind::UShr && left.bigInt() &&
                     right.bigInt())) {
                    // bigint_binary rejects mixed original primitives, or two
                    // BigInts for UShr, before conversion. Its independent
                    // TypeError/Undefined carrier has no operand/local edge or
                    // BigInt category. Keep all continuations and whole-frame
                    // exclusions; normal completion/native effects are unproved.
                    state.values[binary.getResult()] = {binary.getResult(),
                                                        ContentsKind::NonBigInt};
                    continue;
                }
                if (left.bigInt() && right.bigInt() &&
                    (binary.getKind() == ctjs::BinaryKind::Add ||
                     binary.getKind() == ctjs::BinaryKind::BitAnd ||
                     binary.getKind() == ctjs::BinaryKind::BitOr ||
                     binary.getKind() == ctjs::BinaryKind::BitXor ||
                     binary.getKind() == ctjs::BinaryKind::Shl ||
                     binary.getKind() == ctjs::BinaryKind::Shr)) {
                    // These exact bigint_binary arms allocate independent digits
                    // before static Number conversion, with no input alias or
                    // user conversion. Signed shifts may instead raise an
                    // independent RangeError for an oversized left shift,
                    // including a negative right-shift count. The whole-frame
                    // exclusion of calls, handlers and publication prevents
                    // that early exit from retaining unpublished local objects.
                    // Keep the normal result's separate per-path category; this
                    // proves neither allocation success nor normal completion
                    // or no-throw/native effects.
                    if (!spend()) { return refuse(ArrayContentsFailure::WorkLimit, &op); }
                    state.values[binary.getResult()] = {binary.getResult(), ContentsKind::BigInt};
                    continue;
                }
                if (!left.nonBigInt() || !right.nonBigInt()) {
                    return refuse(ArrayContentsFailure::UnsupportedOperation, &op);
                }
                // Independent primitive origins exclude source user conversion.
                // Fresh objects/arrays are insufficient: inherited valueOf or
                // toString can retain them or their contents even though today's
                // VM converts them statically. Only bounded exact primitive
                // conversions or saved Number facts supply an index.
                ContentsValue result{binary.getResult(), ContentsKind::NonBigInt};
                if (binary.getKind() == ctjs::BinaryKind::Add) {
                    boundedNumberSum(left, right, result);
                }
                if (binary.getKind() == ctjs::BinaryKind::BitAnd ||
                    binary.getKind() == ctjs::BinaryKind::BitOr ||
                    binary.getKind() == ctjs::BinaryKind::BitXor ||
                    binary.getKind() == ctjs::BinaryKind::Shl ||
                    binary.getKind() == ctjs::BinaryKind::Shr ||
                    binary.getKind() == ctjs::BinaryKind::UShr) {
                    const auto a = boundedConvertedNumber(left);
                    const auto b = boundedConvertedNumber(right);
                    const auto negativeA = left.negativeIntegerNumber ? left.negativeIntegerNumber
                                                                      : boundedNumber(lhs, true);
                    const auto negativeB = right.negativeIntegerNumber ? right.negativeIntegerNumber
                                                                       : boundedNumber(rhs, true);
                    // Original Boolean/null and canonical Strings convert exactly;
                    // signed Numbers wrap. Mask counts; C++23 signed right shift
                    // preserves the sign. Only UShr keeps an unsigned result;
                    // negative magnitudes never become own-index facts.
                    if ((a || negativeA) && (b || negativeB)) {
                        const auto x = a ? static_cast<std::uint32_t>(*a)
                                         : 0U - static_cast<std::uint32_t>(*negativeA);
                        const auto y = b ? static_cast<std::uint32_t>(*b)
                                         : 0U - static_cast<std::uint32_t>(*negativeB);
                        const auto bits = binary.getKind() == ctjs::BinaryKind::BitAnd   ? x & y
                                          : binary.getKind() == ctjs::BinaryKind::BitOr  ? x | y
                                          : binary.getKind() == ctjs::BinaryKind::BitXor ? x ^ y
                                          : binary.getKind() == ctjs::BinaryKind::Shl
                                              ? static_cast<std::uint32_t>(x << (y & 31U))
                                          : binary.getKind() == ctjs::BinaryKind::Shr
                                              ? static_cast<std::uint32_t>(
                                                    static_cast<std::int32_t>(x) >> (y & 31U))
                                              : x >> (y & 31U);
                        if (binary.getKind() == ctjs::BinaryKind::UShr || bits < 2147483648ULL) {
                            result.integerNumber = bits;
                        } else {
                            result.negativeIntegerNumber = 4294967296ULL - bits;
                        }
                        if (!spend()) { return refuse(ArrayContentsFailure::WorkLimit, &op); }
                    }
                }
                state.values[binary.getResult()] = result;
                continue;
            }
            if (auto object = llvm::dyn_cast<ctjs::CreateObjectOp>(&op)) {
                if (objectSites.insert(&op).second) { out.objects.push_back(&op); }
                state.objects.try_emplace(&op);
                state.values[object.getResult()] = {object.getResult()};
                continue;
            }
            if (auto array = llvm::dyn_cast<ctjs::CreateArrayOp>(&op)) {
                if (arraySites.insert(&op).second) { out.arrays.push_back(&op); }
                auto & elements = state.arrays[&op];
                for (unsigned position = 0; position < array.getElements().size(); ++position) {
                    if (!spend()) { return refuse(ArrayContentsFailure::WorkLimit, &op); }
                    const ContentsValue value = held(array.getElements()[position]);
                    if (!value.origin()) { return refuse(ArrayContentsFailure::UnknownValue, &op); }
                    elements.push_back(value);
                    out.writes.push_back({&op, position, &op, position, value.original});
                }
                state.values[array.getResult()] = {array.getResult()};
                continue;
            }
            if (auto copy = llvm::dyn_cast<ctjs::CopyPropsOp>(&op)) {
                const mlir::Value source = origin(copy.getSource());
                const mlir::Value target = origin(copy.getTarget());
                mlir::Operation * from = source ? source.getDefiningOp() : nullptr;
                mlir::Operation * into = target ? target.getDefiningOp() : nullptr;
                auto sourceObject = state.objects.find(from);
                auto targetObject = state.objects.find(into);
                if (sourceObject == state.objects.end() || targetObject == state.objects.end()) {
                    return refuse(ArrayContentsFailure::UnsupportedOperation, &op);
                }
                // The live runtime copies enumerable own entries through property
                // lookup, which can invoke getters in general (objects/chain.cpp).
                // This complete query admits only fresh own data with attr_default:
                // no accessors, prototypes, descriptors or unknown effects occur.
                // Snapshot before writes, including when both exact aliases name
                // one object. Charge the allocation and each subsequent copy edge.
                if (!spend(sourceObject->second.size())) {
                    return refuse(ArrayContentsFailure::WorkLimit, &op);
                }
                const HeldProperties snapshot = sourceObject->second;
                for (const auto & [key, value] : snapshot) {
                    if (!spend()) { return refuse(ArrayContentsFailure::WorkLimit, &op); }
                    targetObject->second[key] = value;
                    out.propertyCopies.push_back({&op, from, into, key, value.original});
                }
                continue;
            }
            if (llvm::isa<ctjs::DeletePropertyOp, ctjs::DeleteNamedOp>(&op)) {
                const mlir::Value base = origin(op.getOperand(0));
                mlir::Operation * container = base ? base.getDefiningOp() : nullptr;
                auto object = state.objects.find(container);
                if (object == state.objects.end()) {
                    // Array deletion has different hole behavior in JS and the VM.
                    // This proof covers only fresh ordinary own data properties.
                    return refuse(ArrayContentsFailure::UnsupportedOperation, &op);
                }
                mlir::StringAttr key;
                if (auto named = llvm::dyn_cast<ctjs::DeleteNamedOp>(&op)) {
                    key = ownObjectKey(named.getNameAttr());
                } else {
                    const mlir::Value keyValue = origin(op.getOperand(1));
                    if (keyValue) { key = ownObjectKey(keyValue); }
                }
                if (!key) { return refuse(ArrayContentsFailure::UnknownPropertyKey, &op); }
                auto & properties = object->second;
                auto found = properties.find(key);
                const mlir::Value removed =
                    found == properties.end() ? mlir::Value{} : found->second.original;
                if (removed) {
                    // Fresh set_property fields are configurable (value.hpp's
                    // attr_default). delete_own_property erases exactly that own
                    // field; no prototype walk or accessor can occur in this subset.
                    // MapVector erase shifts fields and repairs its index. Charge
                    // the complete field set before mutating even a single entry.
                    if (!spend(properties.size())) {
                        return refuse(ArrayContentsFailure::WorkLimit, &op);
                    }
                    properties.erase(key);
                }
                out.propertyDeletions.push_back({&op, container, key, removed});
                continue;
            }
            if (llvm::isa<ctjs::AppendOp, ctjs::SetPropertyOp, ctjs::GetPropertyOp>(&op)) {
                const mlir::Value base = origin(op.getOperand(0));
                mlir::Operation * container = base ? base.getDefiningOp() : nullptr;
                auto object = state.objects.find(container);
                if (object != state.objects.end() && !llvm::isa<ctjs::AppendOp>(&op)) {
                    // The implicit prototype can intercept even a first write
                    // and retain its value. Until an own-data/prototype proof
                    // exists, an assignment cannot establish object contents.
                    if (llvm::isa<ctjs::SetPropertyOp>(&op)) {
                        return refuse(ArrayContentsFailure::UnsupportedOperation, &op);
                    }
                    const mlir::Value keyValue = origin(op.getOperand(1));
                    const auto key = keyValue ? ownObjectKey(keyValue) : mlir::StringAttr{};
                    if (!key) { return refuse(ArrayContentsFailure::UnknownPropertyKey, &op); }
                    auto & properties = object->second;
                    auto found = properties.find(key);
                    if (found == properties.end()) {
                        return refuse(ArrayContentsFailure::MissingProperty, &op);
                    }
                    state.values[op.getResult(0)] = found->second;
                    out.propertyReads.push_back({&op, container, key, found->second.original});
                    continue;
                }
                auto found = state.arrays.find(container);
                if (found == state.arrays.end()) {
                    return refuse(ArrayContentsFailure::UnknownArray, &op);
                }
                auto & elements = found->second;
                if (auto append = llvm::dyn_cast<ctjs::AppendOp>(&op)) {
                    const ContentsValue value = held(append.getElement());
                    if (!value.origin()) { return refuse(ArrayContentsFailure::UnknownValue, &op); }
                    if (elements.size() >= 4294967295ULL) {
                        return refuse(ArrayContentsFailure::MissingElement, &op);
                    }
                    out.writes.push_back({&op, 1, container, elements.size(), value.original});
                    elements.push_back(value);
                    continue;
                }
                const ContentsValue keyValue = held(op.getOperand(1));
                const mlir::Value key = keyValue.origin();
                if (llvm::isa<ctjs::GetPropertyOp>(&op)) {
                    const auto name = key ? ownObjectKey(key) : mlir::StringAttr{};
                    if (name && name.getValue() == "length") {
                        // lookup_property returns js_length as Number before any
                        // prototype lookup. This exact tracked array admits no
                        // sparse writes/deletion/accessors. Snapshot this read;
                        // a saved origin must survive later array appends unchanged.
                        if (elements.size() > 4294967295ULL) {
                            return refuse(ArrayContentsFailure::MissingElement, &op);
                        }
                        if (!spend()) { return refuse(ArrayContentsFailure::WorkLimit, &op); }
                        state.values[op.getResult(0)] = {op.getResult(0), ContentsKind::NonBigInt,
                                                         elements.size()};
                        continue;
                    }
                } else if (auto store = llvm::dyn_cast<ctjs::SetPropertyOp>(&op)) {
                    const auto name = key ? ownObjectKey(key) : mlir::StringAttr{};
                    if (name && name.getValue() == "length") {
                        const ContentsValue target = held(store.getValue());
                        const mlir::Value value = target.origin();
                        auto literal =
                            value ? value.getDefiningOp<ctjs::ConstantOp>() : ctjs::ConstantOp{};
                        auto wanted = target.integerNumber;
                        if (!wanted && literal && llvm::isa<ctjs::NumberAttr>(literal.getValue())) {
                            wanted = ownArrayIndex(value);
                        }
                        if (!wanted) { return refuse(ArrayContentsFailure::UnknownIndex, &op); }
                        if (*wanted > elements.size()) {
                            return refuse(ArrayContentsFailure::MissingElement, &op);
                        }
                        // Fresh dense arrays own writable length and configurable
                        // elements; an exact Number runs no coercion hook. Keep
                        // saved origins/lengths and every historical cycle edge.
                        // ponytail: non-growing lengths only; growth needs hole evidence.
                        if (!spend(elements.size() - *wanted)) {
                            return refuse(ArrayContentsFailure::WorkLimit, &op);
                        }
                        elements.resize(*wanted);
                        continue;
                    }
                }
                auto index = key ? ownArrayIndex(key) : std::nullopt;
                if (keyValue.integerNumber && *keyValue.integerNumber < 4294967295ULL) {
                    index = keyValue.integerNumber;
                }
                if (!index) { return refuse(ArrayContentsFailure::UnknownIndex, &op); }
                // Overwrite only. Extending with set_property can leave holes or
                // consult a prototype setter; literal append has neither behavior.
                if (*index >= elements.size()) {
                    return refuse(ArrayContentsFailure::MissingElement, &op);
                }
                if (auto store = llvm::dyn_cast<ctjs::SetPropertyOp>(&op)) {
                    const ContentsValue value = held(store.getValue());
                    if (!value.origin()) { return refuse(ArrayContentsFailure::UnknownValue, &op); }
                    elements[*index] = value;
                    out.writes.push_back({&op, 2, container, *index, value.original});
                } else {
                    state.values[op.getResult(0)] = elements[*index];
                    out.reads.push_back({&op, container, *index, elements[*index].original});
                }
                continue;
            }
            if (llvm::isa<ctjs::ReturnOp>(&op)) {
                if (state.frame && !state.frameExited) {
                    return refuse(ArrayContentsFailure::InvalidFrame, &op);
                }
                const mlir::Value value = origin(op.getOperand(0));
                if (!value) { return refuse(ArrayContentsFailure::UnknownValue, &op); }
                ArrayContentsExit exit;
                exit.by = &op;
                exit.value = value;
                llvm::SmallVector<mlir::Value, 8> pending{value};
                llvm::SmallPtrSet<mlir::Operation *, 8> visited;
                while (!pending.empty()) {
                    if (!spend()) { return refuse(ArrayContentsFailure::WorkLimit, &op); }
                    mlir::Operation * site = pending.pop_back_val().getDefiningOp();
                    if (!isTrackedSite(site) || !visited.insert(site).second) { continue; }
                    exit.reachableSites.push_back(site);
                    auto array = state.arrays.find(site);
                    if (array != state.arrays.end()) {
                        for (const ContentsValue & element : array->second) {
                            if (!spend()) { return refuse(ArrayContentsFailure::WorkLimit, &op); }
                            pending.push_back(element.original);
                        }
                    }
                    auto object = state.objects.find(site);
                    if (object != state.objects.end()) {
                        for (const auto & [key, element] : object->second) {
                            (void)key;
                            if (!spend()) { return refuse(ArrayContentsFailure::WorkLimit, &op); }
                            pending.push_back(element.original);
                        }
                    }
                }
                // Public evidence keeps original producers, never private scalar facts.
                // Charge the projection before copying each container and its values.
                for (const auto & [array, elements] : state.arrays) {
                    if (!spend() || !spend(elements.size())) {
                        return refuse(ArrayContentsFailure::WorkLimit, &op);
                    }
                    auto & originals = exit.arrays[array];
                    for (const ContentsValue & element : elements) {
                        originals.push_back(element.original);
                    }
                }
                for (const auto & [object, properties] : state.objects) {
                    if (!spend() || !spend(properties.size())) {
                        return refuse(ArrayContentsFailure::WorkLimit, &op);
                    }
                    auto & originals = exit.objects[object];
                    for (const auto & [key, element] : properties) {
                        originals[key] = element.original;
                    }
                }
                out.exits.push_back(std::move(exit));
                returned = true;
                continue;
            }
            // Throw is intentionally outside the subset: uncaught diagnostic
            // formatting may reenter JavaScript through toString/prototype hooks.
            return refuse(ArrayContentsFailure::UnsupportedOperation, &op);
        }
        if (!returned) { return refuse(ArrayContentsFailure::UnsupportedControlFlow, function); }
        if (alternatives.empty()) { break; }
        state = alternatives.pop_back_val();
    }
    out.complete = true;
    return out;
}

} // namespace ctcompile::ctnative
