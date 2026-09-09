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
// Number -0 is index zero; 2^32-1 is not an array element. String indices
// remain refused: current VM lookup_index/store_index use dense array slots
// only for Number keys, so even String "0" disagrees with JavaScript here.
// Refusal preserves sound retention claims against both execution behaviors.
std::optional<std::size_t> ownArrayIndex(mlir::Value value) {
    auto constant = value.getDefiningOp<ctjs::ConstantOp>();
    if (!constant) { return std::nullopt; }
    if (auto number = llvm::dyn_cast<ctjs::NumberAttr>(constant.getValue())) {
        const double index = number.getDouble();
        if (std::isfinite(index) && index >= 0 && index < 4294967295.0 &&
            std::floor(index) == index) {
            return static_cast<std::size_t>(index);
        }
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

// This is an ORIGINAL origin already accepted on this exact contents path,
// not the operation immediately defining a forwarded or loaded SSA value.
// Keep the list explicit: an unknown origin can be BigInt, and binary_static
// reaches catchable TypeError/RangeError paths before its static conversions.
bool primitiveNonBigIntOrigin(mlir::Value origin) {
    mlir::Operation * definition = origin.getDefiningOp();
    if (auto constant = llvm::dyn_cast_or_null<ctjs::ConstantOp>(definition)) {
        return llvm::isa<ctjs::UndefinedAttr, ctjs::NullAttr, ctjs::BooleanAttr, ctjs::NumberAttr,
                         ctjs::StringAttr>(constant.getValue());
    }
    return llvm::isa_and_nonnull<ctjs::CompareOp, ctjs::ConvertOp, ctjs::UnaryOp, ctjs::BinaryOp,
                                 ctjs::BinaryStaticOp>(definition);
}

bool nonBigIntOrigin(mlir::Value origin) {
    return primitiveNonBigIntOrigin(origin) ||
           llvm::isa_and_nonnull<ctjs::CreateObjectOp, ctjs::CreateArrayOp>(origin.getDefiningOp());
}

bool bigIntConstantOrigin(mlir::Value origin) {
    auto constant = origin.getDefiningOp<ctjs::ConstantOp>();
    return constant && llvm::isa<ctjs::BigIntAttr>(constant.getValue());
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
    // Every branch edge is visited, including a constant flag's untaken edge
    // and a switch's default edge. Any unsupported path refuses everything.
    struct State {
        llvm::DenseMap<mlir::Value, mlir::Value> origins;
        // Imported successors forward every raw register, including unused
        // receiver/parameter values. Keep their exact entry identity separate:
        // forwarding or testing one never proves its contents or retention.
        llvm::DenseMap<mlir::Value, mlir::Value> opaqueOrigins;
        llvm::MapVector<mlir::Operation *, llvm::SmallVector<mlir::Value, 4>> arrays;
        llvm::MapVector<mlir::Operation *, ObjectOwnProperties> objects;
        llvm::SmallPtrSet<mlir::Block *, 8> visited;
        ctjs::FrameEnterOp frame;
        bool frameExited = false;
        mlir::Operation * current = nullptr;
    };
    State state;
    for (mlir::BlockArgument argument : function.getBody().front().getArguments()) {
        if (!spend()) { return refuse(ArrayContentsFailure::WorkLimit, function); }
        if (llvm::isa<ctjs::ValueType>(argument.getType())) {
            state.opaqueOrigins[argument] = argument;
        }
    }
    state.visited.insert(&function.getBody().front());
    state.current = &function.getBody().front().front();
    llvm::SmallVector<State, 2> alternatives;
    llvm::SmallPtrSet<mlir::Operation *, 8> arraySites;
    llvm::SmallPtrSet<mlir::Operation *, 8> objectSites;
    const auto origin = [&](mlir::Value value) { return state.origins.lookup(value); };
    const auto forward = [&](State & path, mlir::Block * next, mlir::ValueRange operands) {
        if (next->getParent() != &function.getBody() || next->empty() ||
            next->getNumArguments() != operands.size() || !path.visited.insert(next).second) {
            return ArrayContentsFailure::UnsupportedControlFlow;
        }
        for (auto [argument, value] : llvm::zip(next->getArguments(), operands)) {
            if (!spend()) { return ArrayContentsFailure::WorkLimit; }
            const mlir::Value exact = path.origins.lookup(value);
            if (exact) {
                path.origins[argument] = exact;
            } else {
                const mlir::Value opaque = path.opaqueOrigins.lookup(value);
                if (!opaque) { return ArrayContentsFailure::UnknownValue; }
                path.opaqueOrigins[argument] = opaque;
            }
        }
        path.current = &next->front();
        return ArrayContentsFailure::None;
    };
    const auto alternative = [&](mlir::Block * next, mlir::ValueRange operands) {
        // Path enumeration can be exponential. Charge every copied value,
        // visited block, container and element before allocating the snapshot.
        if (!spend(state.origins.size()) || !spend(state.opaqueOrigins.size()) ||
            !spend(state.visited.size())) {
            return ArrayContentsFailure::WorkLimit;
        }
        for (const auto & [array, elements] : state.arrays) {
            (void)array;
            if (!spend() || !spend(elements.size())) { return ArrayContentsFailure::WorkLimit; }
        }
        for (const auto & [object, properties] : state.objects) {
            (void)object;
            if (!spend() || !spend(properties.size())) { return ArrayContentsFailure::WorkLimit; }
        }
        State copy = state;
        const auto failure = forward(copy, next, operands);
        if (failure == ArrayContentsFailure::None) { alternatives.push_back(std::move(copy)); }
        return failure;
    };
    while (true) {
        bool returned = false;
        while (state.current != nullptr) {
            mlir::Operation & op = *state.current;
            state.current = op.getNextNode();
            if (!spend()) { return refuse(ArrayContentsFailure::WorkLimit, &op); }
            if (op.getNumRegions() != 0 ||
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
            if (llvm::isa<ctjs::ConstantOp, ctjs::TruthyOp>(&op)) {
                state.origins[op.getResult(0)] = op.getResult(0);
                continue;
            }
            if (auto compare = llvm::dyn_cast<ctjs::CompareOp>(&op)) {
                // Strict equality reads primitive contents or object identity;
                // it cannot coerce, call, throw or retain either operand
                // (Operators.td, value::strict_equals). The independent Boolean
                // result is known even when an operand is an opaque entry.
                // Eq needs a separate proof for BOTH original origins. Its
                // five primitive non-BigInt categories stay in guard-free
                // loose_equals tag/string/static-number paths, without user
                // conversion, a catchable JS throw or an input alias. Relational
                // kinds instead need the guarded retention argument below.
                // String parsing may allocate C++ temporaries; allocation
                // success is not proved. No value/key/liveness is inferred.
                switch (compare.getKind()) {
                case ctjs::CompareKind::StrictEq: break;
                case ctjs::CompareKind::Eq:
                case ctjs::CompareKind::Lt:
                case ctjs::CompareKind::Le:
                case ctjs::CompareKind::Gt:
                case ctjs::CompareKind::Ge: {
                    const mlir::Value lhs = origin(compare.getLhs());
                    const mlir::Value rhs = origin(compare.getRhs());
                    if (!lhs || !rhs) {
                        return refuse(ArrayContentsFailure::UnsupportedOperation, &op);
                    }
                    // Only BOTH independently proved original BigInt constants
                    // reach the exact digit comparison. Saved/forwarded reads
                    // retain that origin; mixed and computed inputs still refuse.
                    const bool bigIntPair = bigIntConstantOrigin(lhs) && bigIntConstantOrigin(rhs);
                    if (compare.getKind() == ctjs::CompareKind::Eq && bigIntPair) { break; }
                    if (!bigIntPair &&
                        (!primitiveNonBigIntOrigin(lhs) || !primitiveNonBigIntOrigin(rhs))) {
                        return refuse(ArrayContentsFailure::UnsupportedOperation, &op);
                    }
                    // Relational kinds enter to_primitive's depth guard even
                    // for primitives, including BigInts. Its primitive fast path
                    // preserves BigInts before exact digit comparison; normal
                    // String/static-number paths likewise retain no input
                    // identity. The guard's unrelated Error
                    // cannot expose this whole-frame query's unpublished fresh
                    // locals. Calls, handlers and publication still refuse.
                    // This proves retention, not normal completion or no-throw
                    // effects. Mixed BigInt and object conversion stay refused.
                    break;
                }
                default: return refuse(ArrayContentsFailure::UnsupportedOperation, &op);
                }
                state.origins[compare.getResult()] = compare.getResult();
                continue;
            }
            if (auto convert = llvm::dyn_cast<ctjs::ConvertOp>(&op)) {
                // ToBoolean, like Truthy, is total and noncapturing, but returns
                // a !ctjs.value Boolean. Its primitive result carries no heap
                // alias; this does not turn its input into a known origin.
                if (convert.getKind() != ctjs::ConvertKind::ToBoolean) {
                    return refuse(ArrayContentsFailure::UnsupportedOperation, &op);
                }
                state.origins[convert.getResult()] = convert.getResult();
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
                switch (unary.getKind()) {
                case ctjs::UnaryKind::Not:
                case ctjs::UnaryKind::TypeOf:
                case ctjs::UnaryKind::Void: break;
                case ctjs::UnaryKind::Neg:
                case ctjs::UnaryKind::Plus:
                case ctjs::UnaryKind::BitNot: {
                    const mlir::Value input = origin(unary.getOperand());
                    if (!input || !primitiveNonBigIntOrigin(input)) {
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
                    break;
                }
                default: return refuse(ArrayContentsFailure::UnsupportedOperation, &op);
                }
                state.origins[unary.getResult()] = unary.getResult();
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
                const mlir::Value lhs = origin(binary.getLhs());
                const mlir::Value rhs = origin(binary.getRhs());
                if (!lhs || !rhs || !primitiveNonBigIntOrigin(lhs) ||
                    !primitiveNonBigIntOrigin(rhs)) {
                    return refuse(ArrayContentsFailure::UnsupportedOperation, &op);
                }
                // With primitive non-BigInt originals, binary_op cannot call
                // object conversions or return an operand object. Sub/Mul/Div/
                // Mod/Pow return Number via to_number_value. Add uses guarded
                // to_primitive followed by static Number/String operations;
                // Concat uses primitive to_string. Their result is independent,
                // without a Number/String tag, value, index or key inference.
                // Add and numeric conversions have a depth guard that may
                // throw an unrelated RangeError. This whole-frame query refuses
                // calls, handlers and publication, so it cannot retain fresh
                // locals on that exit. This is retention-only evidence, never
                // a normal-completion or no-throw/effect contract. String
                // results allocate in the VM and static conversions can allocate
                // C++ temporaries; absence/success of allocation is unproved.
                state.origins[binary.getResult()] = binary.getResult();
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
                const mlir::Value lhs = origin(binary.getLhs());
                const mlir::Value rhs = origin(binary.getRhs());
                if (!lhs || !rhs) { return refuse(ArrayContentsFailure::UnknownValue, &op); }
                if (!nonBigIntOrigin(lhs) || !nonBigIntOrigin(rhs)) {
                    return refuse(ArrayContentsFailure::UnsupportedOperation, &op);
                }
                // binary_op_static's non-BigInt arm uses only static to_number /
                // to_int32 / to_uint32. It returns Number without user reentry,
                // an input alias or a catchable JS throw. String parsing may
                // allocate ordinary C++ temporaries; this proves neither absence
                // of allocation nor its success. Even fresh objects convert
                // statically here, a documented VM deviation from source JS.
                // This proves no numeric value, key, alias or branch liveness.
                state.origins[binary.getResult()] = binary.getResult();
                continue;
            }
            if (auto object = llvm::dyn_cast<ctjs::CreateObjectOp>(&op)) {
                if (objectSites.insert(&op).second) { out.objects.push_back(&op); }
                state.objects.try_emplace(&op);
                state.origins[object.getResult()] = object.getResult();
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
                const ObjectOwnProperties snapshot = sourceObject->second;
                for (const auto & [key, value] : snapshot) {
                    if (!spend()) { return refuse(ArrayContentsFailure::WorkLimit, &op); }
                    targetObject->second[key] = value;
                    out.propertyCopies.push_back({&op, from, into, key, value});
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
                    found == properties.end() ? mlir::Value{} : found->second;
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
                    // CreateObject has a null explicit prototype and no accessors
                    // (Containers/Properties.td). Only known own writes/reads are
                    // modeled; no call, descriptor or prototype mutation is allowed.
                    // Refuse __proto__ even though the current VM stores it as data.
                    const mlir::Value keyValue = origin(op.getOperand(1));
                    const auto key = keyValue ? ownObjectKey(keyValue) : mlir::StringAttr{};
                    if (!key) { return refuse(ArrayContentsFailure::UnknownPropertyKey, &op); }
                    auto & properties = object->second;
                    if (auto store = llvm::dyn_cast<ctjs::SetPropertyOp>(&op)) {
                        const mlir::Value value = origin(store.getValue());
                        if (!value) { return refuse(ArrayContentsFailure::UnknownValue, &op); }
                        properties[key] = value;
                        out.propertyWrites.push_back({&op, 2, container, key, value});
                    } else {
                        auto found = properties.find(key);
                        if (found == properties.end()) {
                            return refuse(ArrayContentsFailure::MissingProperty, &op);
                        }
                        state.origins[op.getResult(0)] = found->second;
                        out.propertyReads.push_back({&op, container, key, found->second});
                    }
                    continue;
                }
                auto found = state.arrays.find(container);
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
                    out.writes.push_back({&op, 1, container, elements.size(), value});
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
                    out.writes.push_back({&op, 2, container, *index, value});
                } else {
                    state.origins[op.getResult(0)] = elements[*index];
                    out.reads.push_back({&op, container, *index, elements[*index]});
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
                        for (mlir::Value element : array->second) {
                            if (!spend()) { return refuse(ArrayContentsFailure::WorkLimit, &op); }
                            pending.push_back(element);
                        }
                    }
                    auto object = state.objects.find(site);
                    if (object != state.objects.end()) {
                        for (const auto & [key, element] : object->second) {
                            (void)key;
                            if (!spend()) { return refuse(ArrayContentsFailure::WorkLimit, &op); }
                            pending.push_back(element);
                        }
                    }
                }
                exit.arrays = std::move(state.arrays);
                exit.objects = std::move(state.objects);
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
