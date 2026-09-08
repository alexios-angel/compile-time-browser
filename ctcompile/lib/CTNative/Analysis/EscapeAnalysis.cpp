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
                // A sink outranks a carry on the same position, should both
                // ever be written: sinking is the conservative answer.
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
    if (workLimit == 0 || verdicts.unvisitedSites != 0 || verdicts.unvisitedOperands != 0 ||
        verdicts.wholeFunction || !verdicts.directStorage.complete ||
        !verdicts.directLoads.complete || !llvm::any_of(verdicts.sites, [](const auto & entry) {
            return entry.second.reason == EscapeReason::Stored;
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
    // was overwritten before return stays outside this refinement. Count
    // duplicate edges independently so Kahn's traversal handles shared children
    // and repeated writes without treating either as a cycle.
    llvm::DenseMap<mlir::Operation *, std::size_t> incoming;
    llvm::DenseMap<mlir::Operation *, llvm::SmallVector<mlir::Operation *, 2>> successors;
    for (mlir::Operation * array : contents.arrays) {
        if (!spend()) { return; }
        incoming.try_emplace(array, 0);
    }
    for (const ArrayElementWrite & write : contents.writes) {
        if (!spend()) { return; }
        mlir::Operation * child = write.value.getDefiningOp();
        auto found = incoming.find(child);
        if (found == incoming.end()) { continue; } // constant or property-free object
        successors[write.array].push_back(child);
        ++found->second;
    }
    llvm::SmallVector<mlir::Operation *, 8> pending;
    for (mlir::Operation * array : contents.arrays) {
        if (!spend()) { return; }
        if (incoming.lookup(array) == 0) { pending.push_back(array); }
    }
    std::size_t visited = 0;
    while (!pending.empty()) {
        if (!spend()) { return; }
        mlir::Operation * array = pending.pop_back_val();
        ++visited;
        auto found = successors.find(array);
        if (found == successors.end()) { continue; }
        for (mlir::Operation * child : found->second) {
            if (!spend()) { return; }
            if (--incoming[child] == 0) { pending.push_back(child); }
        }
    }
    if (visited != contents.arrays.size()) { return; }

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
        if (verdict.reason == EscapeReason::Stored && retained.count(site) == 0) {
            confined.push_back(site);
        }
    }
    // Transactional commit: not even the first candidate changes before the
    // final graph/verdict visit. Reachable children keep their Stored witnesses,
    // because returning a container does not give each child a unique owner.
    for (mlir::Operation * site : confined) {
        verdicts.sites[site] = Verdict{};
        ++verdicts.confinedStoredSites;
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
    // live blocks only. TypeClaims.cpp's split applied to sites: a site in a
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
                } else if (role.role == OperandRole::Carry) {
                    for (mlir::Value result : op.getResults()) {
                        if (isValueTyped(result)) {
                            copies.emplace_back(op.getOperand(index), result);
                        }
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

// An own array element, not a property requiring conversion/prototype lookup.
// -0 Number is index zero; the String "-0" is a different named property.
// 2^32-1 is an ordinary property key, not an array element. Limiting strings
// before parsing also bounds proof work independently of source key length.
std::optional<std::size_t> ownArrayIndex(mlir::Value value) {
    auto constant = value.getDefiningOp<ctjs::ConstantOp>();
    if (!constant) { return std::nullopt; }
    if (auto number = llvm::dyn_cast<ctjs::NumberAttr>(constant.getValue())) {
        const double index = number.getDouble();
        if (std::isfinite(index) && index >= 0 && index < 4294967295.0 &&
            std::floor(index) == index) {
            return static_cast<std::size_t>(index);
        }
    } else if (auto string = llvm::dyn_cast<ctjs::StringAttr>(constant.getValue())) {
        const llvm::StringRef text = string.getValue();
        if (text.empty() || text.size() > 10 || (text.size() > 1 && text.front() == '0') ||
            !llvm::all_of(text, [](char c) { return c >= '0' && c <= '9'; })) {
            return std::nullopt;
        }
        std::uint64_t index = 0;
        if (!text.getAsInteger(10, index) && index < 4294967295ULL) {
            return static_cast<std::size_t>(index);
        }
    }
    return std::nullopt;
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
    // Both conditional edges are visited, including a constant predicate's
    // untaken edge. An operation unsupported on any path refuses everything.
    struct State {
        llvm::DenseMap<mlir::Value, mlir::Value> origins;
        llvm::MapVector<mlir::Operation *, llvm::SmallVector<mlir::Value, 4>> arrays;
        llvm::SmallPtrSet<mlir::Block *, 8> visited;
        ctjs::FrameEnterOp frame;
        bool frameExited = false;
        mlir::Operation * current = nullptr;
    };
    State state;
    state.visited.insert(&function.getBody().front());
    state.current = &function.getBody().front().front();
    llvm::SmallVector<State, 2> alternatives;
    llvm::SmallPtrSet<mlir::Operation *, 8> arraySites;
    const auto origin = [&](mlir::Value value) { return state.origins.lookup(value); };
    const auto forward = [&](State & path, mlir::Block * next, mlir::ValueRange operands) {
        if (next->getParent() != &function.getBody() || next->empty() ||
            next->getNumArguments() != operands.size() || !path.visited.insert(next).second) {
            return ArrayContentsFailure::UnsupportedControlFlow;
        }
        for (auto [argument, value] : llvm::zip(next->getArguments(), operands)) {
            if (!spend()) { return ArrayContentsFailure::WorkLimit; }
            const mlir::Value exact = path.origins.lookup(value);
            if (!exact) { return ArrayContentsFailure::UnknownValue; }
            path.origins[argument] = exact;
        }
        path.current = &next->front();
        return ArrayContentsFailure::None;
    };
    while (true) {
        bool returned = false;
        while (state.current != nullptr) {
            mlir::Operation & op = *state.current;
            state.current = op.getNextNode();
            if (!spend()) { return refuse(ArrayContentsFailure::WorkLimit, &op); }
            if (op.getNumRegions() != 0 ||
                (op.getNumSuccessors() != 0 &&
                 !llvm::isa<mlir::cf::BranchOp, mlir::cf::CondBranchOp>(&op))) {
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
                state.origins[state.frame.getResult()] = state.frame.getResult();
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
            if (auto branch = llvm::dyn_cast<mlir::cf::BranchOp>(&op)) {
                const auto failure = forward(state, branch.getDest(), branch.getDestOperands());
                if (failure != ArrayContentsFailure::None) { return refuse(failure, &op); }
                continue;
            }
            if (auto branch = llvm::dyn_cast<mlir::cf::CondBranchOp>(&op)) {
                if (!origin(branch.getCondition())) {
                    return refuse(ArrayContentsFailure::UnknownValue, &op);
                }
                // Path enumeration can be exponential. Charge every copied value,
                // visited block, array and element before allocating the snapshot.
                if (!spend(state.origins.size()) || !spend(state.visited.size())) {
                    return refuse(ArrayContentsFailure::WorkLimit, &op);
                }
                for (const auto & [array, elements] : state.arrays) {
                    (void)array;
                    if (!spend() || !spend(elements.size())) {
                        return refuse(ArrayContentsFailure::WorkLimit, &op);
                    }
                }
                State alternative = state;
                auto failure =
                    forward(alternative, branch.getFalseDest(), branch.getFalseDestOperands());
                if (failure != ArrayContentsFailure::None) { return refuse(failure, &op); }
                failure = forward(state, branch.getTrueDest(), branch.getTrueDestOperands());
                if (failure != ArrayContentsFailure::None) { return refuse(failure, &op); }
                alternatives.push_back(std::move(alternative));
                continue;
            }
            // Truthy is total, noncapturing and nonthrowing (Operators.td). An
            // external input remains unknown for every other use; only its i1
            // result can be carried as a predicate. Neither branch is pruned.
            if (llvm::isa<ctjs::ConstantOp, ctjs::CreateObjectOp, ctjs::TruthyOp>(&op)) {
                state.origins[op.getResult(0)] = op.getResult(0);
                continue;
            }
            if (auto array = llvm::dyn_cast<ctjs::CreateArrayOp>(&op)) {
                if (arraySites.insert(&op).second) { out.arrays.push_back(&op); }
                auto & elements = state.arrays[&op];
                for (unsigned position = 0; position < array.getElements().size(); ++position) {
                    if (!spend()) { return refuse(ArrayContentsFailure::WorkLimit, &op); }
                    const mlir::Value value = origin(array.getElements()[position]);
                    if (!value) { return refuse(ArrayContentsFailure::UnknownValue, &op); }
                    elements.push_back(value);
                    out.writes.push_back({&op, position, &op, position, value});
                }
                state.origins[array.getResult()] = array.getResult();
                continue;
            }
            if (llvm::isa<ctjs::AppendOp, ctjs::SetPropertyOp, ctjs::GetPropertyOp>(&op)) {
                const mlir::Value base = origin(op.getOperand(0));
                mlir::Operation * array = base ? base.getDefiningOp() : nullptr;
                auto found = state.arrays.find(array);
                if (found == state.arrays.end()) {
                    return refuse(ArrayContentsFailure::UnknownArray, &op);
                }
                auto & elements = found->second;
                if (auto append = llvm::dyn_cast<ctjs::AppendOp>(&op)) {
                    const mlir::Value value = origin(append.getElement());
                    if (!value) { return refuse(ArrayContentsFailure::UnknownValue, &op); }
                    if (elements.size() >= 4294967295ULL) {
                        return refuse(ArrayContentsFailure::MissingElement, &op);
                    }
                    out.writes.push_back({&op, 1, array, elements.size(), value});
                    elements.push_back(value);
                    continue;
                }
                const mlir::Value key = origin(op.getOperand(1));
                const auto index = key ? ownArrayIndex(key) : std::nullopt;
                if (!index) { return refuse(ArrayContentsFailure::UnknownIndex, &op); }
                // Overwrite only. Extending with set_property can leave holes or
                // consult a prototype setter; literal append has neither behavior.
                if (*index >= elements.size()) {
                    return refuse(ArrayContentsFailure::MissingElement, &op);
                }
                if (auto store = llvm::dyn_cast<ctjs::SetPropertyOp>(&op)) {
                    const mlir::Value value = origin(store.getValue());
                    if (!value) { return refuse(ArrayContentsFailure::UnknownValue, &op); }
                    elements[*index] = value;
                    out.writes.push_back({&op, 2, array, *index, value});
                } else {
                    state.origins[op.getResult(0)] = elements[*index];
                    out.reads.push_back({&op, array, *index, elements[*index]});
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
                    if (array == state.arrays.end()) { continue; }
                    for (mlir::Value element : array->second) {
                        if (!spend()) { return refuse(ArrayContentsFailure::WorkLimit, &op); }
                        pending.push_back(element);
                    }
                }
                exit.arrays = std::move(state.arrays);
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
