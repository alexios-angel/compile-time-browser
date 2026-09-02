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
#include "mlir/IR/Block.h"
#include "mlir/IR/Location.h"
#include "mlir/Interfaces/ControlFlowInterfaces.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/raw_ostream.h"

#include <algorithm>
#include <cassert>
#include <iterator>

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

EscapeVerdicts computeVerdicts(mlir::DataFlowSolver & solver, ctjs::FuncOp function) {
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
    bool suspends = false;
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
            if (isTrackedSite(&op)) {
                Verdict verdict;
                const AliasLattice * lattice = solver.lookupState<AliasLattice>(op.getResult(0));
                if (lattice == nullptr || lattice->getValue().isUninitialized()) {
                    verdict = Verdict{EscapeReason::Unvisited, &op, 0};
                    ++out.unvisitedSites;
                }
                out.sites.insert({&op, verdict});
            }
            if (llvm::isa<ctjs::MakeArgumentsOp, ctjs::GatherRestOp>(op)) {
                argumentsBuilders.push_back(&op);
                out.capturesAllArguments = true;
            }
            if (llvm::isa<ctjs::SuspendOp>(op)) { suspends = true; }
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
        for (auto & entry : out.sites) { mark(entry.first, reason, by, 0); }
    };

    // R4: a suspension point copies the entire register window into a
    // coroutine object on the heap (run_loop.cpp:866-867, 922-923), so every
    // site of the function outlives its frame. The importer refuses these
    // functions already; this line is for hand-written IR.
    if (suspends) {
        mlir::Operation * by = nullptr;
        for (mlir::Block * block : live) {
            for (mlir::Operation & op : *block) {
                if (llvm::isa<ctjs::SuspendOp>(op)) {
                    by = &op;
                    break;
                }
            }
            if (by != nullptr) { break; }
        }
        refuseWholeFunction(EscapeReason::Suspended, by);
    }

    // R1's GUARD: the per-site `arguments` refusal is sound only because the
    // arguments array holds what the parameter registers held in the
    // PROLOGUE - make_arguments is emitted after the parameters are declared
    // and before any body statement (compile/statements.cpp), gather_rest has
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
        }
    }
    return out;
}

} // namespace ctcompile::ctnative
